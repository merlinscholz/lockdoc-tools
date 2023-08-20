#include <set>
#include "config.h"
#include "lockmanager.h"
#include "rlock.h"
#include "wlock.h"

long LockManager::findTXN(RWLock *lock, enum SUB_LOCK subLock, long ctx) {
	for (auto& kv : m_activeTXNs) {
		if (!this->hasActiveTXN(kv.first)) {
			continue;
		}
		for (auto& elem : kv.second) {
			if (elem.lock == lock && elem.subLock == subLock) {
				return kv.first;
			}
		}
	}
	return ctx;
}

bool LockManager::hasActiveTXN(long ctx) {
	return !m_activeTXNs[ctx].empty();
}

struct TXN& LockManager::getActiveTXN(long ctx) {
	return m_activeTXNs[ctx].back();
}

RWLock* LockManager::findLock(unsigned long long address) {
	auto itLock = m_locks.find(address);
	if (itLock != m_locks.end()) {
		return itLock->second;
	}
	return NULL;
}

void LockManager::deleteLockByArea(unsigned long long address, unsigned long long size) {
	map<unsigned long long,RWLock*>::iterator itLock, itTemp;
	// Iterate through the set of locks, and delete any lock that resided in the freed memory area
	for (itLock = m_locks.begin(); itLock != m_locks.end();) {
		if (itLock->second->lockAddress >= address && itLock->second->lockAddress < (address + size)) {
			// Lock should not be held anymore
			if (itLock->second->isHeld()) {
				PRINT_ERROR("baseAddress=" << hex << showbase << address << noshowbase, "Lock at " << itLock->second->lockAddress << "is being freed but held!");
			}
			// Since the iterator will be invalid as soon as we delete the element, we have to advance the iterator to the next element, and remember the current one.
			itTemp = itLock;
			itLock++;
			m_locks.erase(itTemp);
		} else {
			itLock++;
		}
	}
}

void LockManager::closeAllTXNs(unsigned long long ts) {
	// Close every open TXN for every context
	for (auto& kv : m_activeTXNs) { 
		// Flush TXNs if there are still open ones
		while (this->hasActiveTXN(kv.first)) {
			auto txn = this->getActiveTXN(kv.first);
			cerr << "TXN[" << kv.first << "]: There are still " << m_activeTXNs[kv.first].size() << " TXNs active, flushing the topmost one." << endl;
			// pretend there's a V() matching the top-most TXN's starting (P())
			// lock at the last seen timestamp
			this->finishTXN(txn.lock, ts, txn.subLock, false, kv.first);
		}
	}
}

bool LockManager::isOnTXNStack(long ctx, RWLock *lock, enum SUB_LOCK subLock) {
	for (auto& elem : m_activeTXNs[ctx]) {
		if (elem.lock == lock && elem.subLock == subLock) {
			return true;
		}
	}
	return false;
}

/**
 * Releases a lock, and finishes the corresponding TXN.
 *
 * @param ts             Current timestamp
 * @param lockPtr        Lock to be released
 * @param m_txnsOFile      ofstream for txns.csv
 * @param m_locksHeldOFile ofstream for locks_held.csv
 */
bool LockManager::finishTXN(RWLock *lock, unsigned long long ts, enum SUB_LOCK subLock, bool removeReader, long ctx) {
	// We have to differentiate two cases:
	//
	// 1. The lock we're seeing a V() on (lockPtr) belongs to the top-most,
	//    currently active TXN.  Here we simply close this TXN: We write it to
	//    disk along with all locks that are currently held (which is
	//    equivalent to "the locks belonging to all currently active TXNs).
	// 2. The lock we're seeing a V() on belongs to a TXN below the top-most
	//    one.  Then we have to close all TXNs down to (and including) this
	//    TXN, and open new TXNs for all TXNs (excluding the one with the
	//    matching lock) we just closed, in the same layering order as we
	//    closed them.
	//
	// The following code assumes we're observing case 2, of which case 1 is a
	// special case (while loop terminates after one iteration).

	std::deque<TXN> restartTXNs;
	bool found = false;

	while (this->hasActiveTXN(ctx)) {
		if (!SKIP_EMPTY_TXNS || this->getActiveTXN(ctx).memAccessCounter > 0) {
			// Record this TXN
			m_txnsOFile << this->getActiveTXN(ctx).id << delimiter;
			m_txnsOFile << this->getActiveTXN(ctx).start << delimiter;
			m_txnsOFile << ts << "\n";

			// Note which locks were held during this TXN by looking at all
			// TXNs "below" it (the order does not matter because we record the
			// start timestamp).  Don't mention a lock more than once (see
			// below).
			std::set<decltype(RWLock::read_id)> locks_seen;
			for (auto thisTXN : m_activeTXNs[ctx]) {
				RWLock *tempLock = thisTXN.lock;
				if (tempLock->isHeld()) {
					if (tempLock->lastNPos.empty()) {
						PRINT_ERROR(tempLock->toString(thisTXN.subLock) << ",ts=" << dec << ts, "TXN: Internal error, stack underflow in lastNPos");
						continue;
					}
					LockPos& tempLockPos = tempLock->lastNPos.top();
					decltype(RWLock::read_id) lockID = tempLock->getID(thisTXN.subLock);
					// Have we already seen this lock?
					if (locks_seen.find(lockID) != locks_seen.end()) {
						// All reader locks, for example, RCU, and the read-side of
						// of reader-writer locks may be held multiple times, but the
						// locks_held table structure currently does not allow
						// this (because the lock_id is part of the PK).
						continue;
					}
					locks_seen.insert(lockID);
					m_locksHeldOFile << dec << this->getActiveTXN(ctx).id << delimiter << lockID << delimiter;
					m_locksHeldOFile << tempLockPos.start << delimiter;
					m_locksHeldOFile << tempLockPos.lastFile << delimiter;
					m_locksHeldOFile << tempLockPos.lastLine << delimiter << tempLockPos.lastFn << delimiter;
					m_locksHeldOFile << tempLockPos.lastPreemptCount << delimiter;
					switch (tempLockPos.lastIRQSync) {
						case LOCK_NONE:
							m_locksHeldOFile << "LOCK_NONE";
							break;

						case LOCK_IRQ:
							m_locksHeldOFile << "LOCK_IRQ";
							break;

						case LOCK_IRQ_NESTED:
							m_locksHeldOFile << "LOCK_IRQ_NESTED";
							break;

						case LOCK_BH:
							m_locksHeldOFile << "LOCK_BH";
							break;

						default:
							return EXIT_FAILURE;
					}
					m_locksHeldOFile << "\n";
				} else {
					PRINT_ERROR(tempLock->toString(thisTXN.subLock) << ",ts=" << dec << ts, "TXN: Internal error, lock is part of the TXN hierarchy but not held?");
				}
			}
		}

		// are we done deconstructing the TXN stack?
		if (this->getActiveTXN(ctx).lock == lock) {
			if (this->getActiveTXN(ctx).subLock == subLock) {
				// We have deconstructed the TXN stack until the topmost
				// TXN belongs to the lock for which we have seen a V().
				m_activeTXNs[ctx].pop_back();
				found = true;
				// But still, the TXN stack may contain TXNs belonging to lockPtr.
				if (removeReader) {
					// The caller wants to remove all READER_LOCKs from the TXN stack.
					for (std::deque<TXN>::iterator it = m_activeTXNs[ctx].begin(); it != m_activeTXNs[ctx].end();) {
						if (it->lock != lock) {
							it++;
							continue;
						}
						// Does subLock and lockPtr match?
						if (it->subLock == READER_LOCK) {
							PRINT_DEBUG(it->lock->toString(it->subLock) << ",ts=" << dec << ts << ",txn=" << it->id, "Flushing TXN");
							it = m_activeTXNs[ctx].erase(it);
						} else {
							it++;
							PRINT_ERROR(it->lock->toString(it->subLock) << ",ts=" << dec << ts, "Multiple active txns for one writer lock");
						}
					}
				}
				break;
			} else {
				PRINT_ERROR(this->getActiveTXN(ctx).lock->toString(this->getActiveTXN(ctx).subLock) << ",ts=" << dec << ts, "sublock does not match");
			}
		}

		// this is a TXN we need to recreate under a different ID after we're done

		// pushing in front to preserve order
		restartTXNs.push_front(std::move(this->getActiveTXN(ctx)));
		m_activeTXNs[ctx].pop_back();
		// give TXN a new ID + timestamp + memAccessCounter
		restartTXNs.front().id = m_nextTXNID++;
		restartTXNs.front().start = ts;
		restartTXNs.front().memAccessCounter = 0;
	}

	// sanity check whether m_activeTXNs is not empty -- this should never happen
	// because we check whether we know this lock before we call finishTXN()!
	if (!found) {
		PRINT_ERROR(lock->toString(subLock) << ",ts=" << dec << ts, "TXN: Internal error -- V() but no matching TXN!");
	}

	// recreate TXNs
	std::move(restartTXNs.begin(), restartTXNs.end(), std::back_inserter(m_activeTXNs[ctx]));
	return found;

}

void LockManager::startTXN(RWLock *lock, unsigned long long ts, enum SUB_LOCK subLock, long ctx) {
	m_activeTXNs[ctx].push_back(TXN());
	auto& curTXN = this->getActiveTXN(ctx);
	curTXN.id = m_nextTXNID++;
	curTXN.start = ts;
	curTXN.memAccessCounter = 0;
	curTXN.lock = lock;
	curTXN.subLock = subLock;
}

RWLock* LockManager::allocLock(unsigned long long lockAddress, unsigned allocID, string lockType, const char *lockVarName, unsigned flags) {
	// Insert virgin lock into map, and write entry to file
	RWLock *ret;
	
	if (lockType.compare("raw_spinlock_t") == 0 ||
		lockType.compare("mutex") == 0 ||
		lockType.compare(PSEUDOLOCK_NAME_SOFTIRQ) == 0 ||
		lockType.compare(PSEUDOLOCK_NAME_HARDIRQ) == 0 ||
		lockType.compare("semaphore") == 0 ||
		lockType.compare("bit_spin_lock") == 0 ||
		lockType.compare("sleep mutex") == 0 ||
		lockType.compare("spin mutex") == 0 ||
		lockType.compare("kmutex_t") == 0 ||	// NetBSD Kernel mutex
		lockType.compare("b_cflags") == 0) {	// NetBSD buf_t BC_BUSY flag hack
		ret = new WLock(lockAddress, allocID, lockType, lockVarName, flags, this);
		if (!ret) {
			PRINT_ERROR("lockAddress=" << showbase << hex << lockAddress << noshowbase, "Cannot allocate WLock.");
			// This is a severe error. Abort immediately!
			exit(1);
		}
	} else if (lockType.compare(PSEUDOLOCK_NAME_RCU) == 0) {
		ret = new RLock(lockAddress, allocID, lockType, lockVarName, flags, this);
		if (!ret) {
			PRINT_ERROR("lockAddress=" << showbase << hex << lockAddress << noshowbase, "Cannot allocate WLock.");
			// This is a severe error. Abort immediately!
			exit(1);
		}
	} else if (lockType.compare("rwlock_t") == 0 ||
			   lockType.compare("rw_semaphore") == 0 ||
			   lockType.compare("sx") == 0 ||
			   lockType.compare("rw") == 0 ||
			   lockType.compare("sleepable rm") == 0 ||
			   lockType.compare("rm") == 0 ||
			   lockType.compare("lockmgr") == 0 ||
			   lockType.compare("krwlock_t") == 0) {	// NetBSD Kernel RW lock
		ret = new RWLock(lockAddress, allocID, lockType, lockVarName, flags, this);
		if (!ret) {
			PRINT_ERROR("lockAddress=" << showbase << hex << lockAddress << noshowbase, "Cannot allocate WLock.");
			// This is a severe error. Abort immediately!
			exit(1);
		}
	} else {
		PRINT_ERROR("lockAddress=" << showbase << hex << lockAddress << noshowbase,"Unknown lock type: " << lockType);
		// This is a severe error. Abort immediately!
		exit(1);
	}

	// ... , and assign ids to the sub locks
	ret->initIDs(m_nextLockID);
	// Store the lock in our global map
	pair<map<unsigned long long,RWLock*>::iterator,bool> retLock;
	retLock = m_locks.insert(pair<unsigned long long,RWLock*>(lockAddress, ret));
	if (!retLock.second) {
		PRINT_ERROR("lockAddress=" << showbase << hex << lockAddress << noshowbase, "Cannot insert lock into map.");
		// This is a severe error. Abort immediately!
		exit(1);
	}
	return ret;
}
