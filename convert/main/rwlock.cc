#include "rwlock.h"
#include "rlock.h"
#include "wlock.h"
#include "config.h"
#include <sstream>
#include <iostream>
#include <set>

/**
 * The next id for a new TXN.
 */
static unsigned long long curTXNID = 1;

void RWLock::writeTransition(
	enum LOCK_OP lockOP,
	unsigned long long ts,
	std::string const& file,
	unsigned long long line,
	std::string const& fn,
	string const& lockMember,
	unsigned long long preemptCount,
	enum IRQ_SYNC irqSync,
	unsigned flags,
	std::deque<TXN>& activeTXNs,
	std::ofstream& txnsOFile,
	std::ofstream& locksHeldOFile,
	const char *kernelDir) {

	this->updateFlags(flags);
	switch (lockOP) {
		case P_WRITE:
			{
				if (this->writer_count == 0 && this->reader_count > 0) {
					PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "Read-side is already beeing held.");
					// Flush all reader
					// For more information about using the return value of finishTXN(), and why it is important,
					// have a look at RWLock::readTransition() in V_READ case (approx. line 181).
					if (this->finishTXN(ts,  READER_LOCK, true, txnsOFile, locksHeldOFile, activeTXNs)) {
						// finishTXN has cleaned up all all TXNs. We must now deconstruct der last pos stack.
						while (!lastNPos.empty()) {
							this->reader_count--;
							this->lastNPos.pop();
						}
						if (this->reader_count != 0) {
							PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "Inconsistent reader_count, still above zero (" << this->reader_count << ")." );
							this->reader_count = 0;
						}
					} else {
						PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "Cannot flush all reader." );
					}
				} else if (this->writer_count > 0 && this->reader_count == 0) {
					if (this->flags & LOCK_FLAGS_RECURSIVE) {
						// Nothing to do since acquiring the writer lock multiple times is allowed.
						// We do *not* perform any owner tracking.
					} else {
						PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "Lock is already beeing held.");
						// finish TXN for this lock, we may have missed
						// the corresponding V()
						// For more information about using the return value of finishTXN(), and why it is important,
						// have a look at RWLock::readTransition() in V_READ case (approx. line 181).
						if (this->finishTXN(ts, WRITER_LOCK, false, txnsOFile, locksHeldOFile, activeTXNs)) {
							// forget locking position because this kind of
							// lock can officially only be held once
							this->lastNPos.pop();
						}
					}
				} else if (this->writer_count > 0 && this->reader_count > 0) {
					stringstream ss;
					ss << "Invalid state on reader-writer lock (op=" << lockOP << ",addr=" << hex << showbase << this->lockAddress << noshowbase << ", lockMember=" << lockMember << ", ts=" << ts << "): ";
					ss << "this->writer_count == 0 && this->reader_count > 0";
					throw logic_error(ss.str());
				} else if (this->writer_count == 0 && this->reader_count == 0) {
					// Nothing to do
				}
				this->writer_count++;
				this->lastNPos.push(LockPos());
				LockPos& tempLockPos = this->lastNPos.top();
				tempLockPos.subLock = WRITER_LOCK;
				tempLockPos.start = ts;
				tempLockPos.lastLine = line;
				string tmp = kernelDir;
				if (tmp.back() != '/') {
					tmp.append("/");
				}
				if (file.find(tmp) != string::npos) {
					tempLockPos.lastFile = file.substr(tmp.length());
				} else {
					tempLockPos.lastFile = file;
				}
				tempLockPos.lastFn = fn;
				tempLockPos.lastPreemptCount = preemptCount;
				tempLockPos.lastIRQSync = irqSync;

				// a P() suspends the current TXN and creates a new one
				startTXN(ts, WRITER_LOCK, activeTXNs);
				break;
			}

		case V_WRITE:
			{
				if (!this->lastNPos.empty()) {
					// a V() finishes the current TXN (even if it does
					// not match its starting P()!) and continues the
					// enclosing one
					if (activeTXNs.empty()) {
						PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts),"TXN: V() but no running TXN!");
					}
					// For more information about using the return value of finishTXN(), and why it is important,
					// have a look at RWLock::readTransition() in V_READ case (approx. line 181).
					if (this->finishTXN(ts, WRITER_LOCK, false, txnsOFile, locksHeldOFile, activeTXNs)) {
						this->lastNPos.pop();
					}
				} else {
					PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "No last locking position known, cannot pop.");
				}
				if (this->writer_count == 0) {
					PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "Lock has already been released.");
				} else {
					this->writer_count--;
				}
				break;
			}
			
		case P_READ:
		case V_READ:
			{
				stringstream ss;
				ss << "Invalid op on writer lock (" << hex << showbase << this->lockAddress << noshowbase << "," << lockMember << ") at ts " << dec << ts << endl;
				throw logic_error(ss.str());
				break;
			}
			
		default:
			throw logic_error("Unknown lock operation");
	}	
}

void RWLock::readTransition(
	enum LOCK_OP lockOP,
	unsigned long long ts,
	std::string const& file,
	unsigned long long line,
	std::string const& fn,
	string const& lockMember,
	unsigned long long preemptCount,
	enum IRQ_SYNC irqSync,
	unsigned flags,
	std::deque<TXN>& activeTXNs,
	std::ofstream& txnsOFile,
	std::ofstream& locksHeldOFile,
	const char *kernelDir) {

	this->updateFlags(flags);
	switch (lockOP) {
		case P_WRITE:
		case V_WRITE:
			{
				stringstream ss;
				ss << "Invalid op on reader lock (" << hex << showbase << this->lockAddress << noshowbase << "," << lockMember << ") at ts " << dec << ts << endl;
				throw logic_error(ss.str());
				break;
			}

		case P_READ:
			{
				if (this->writer_count == 0 && this->reader_count > 0) {
					// Nothing to do
				} else if (this->writer_count > 0 && this->reader_count == 0) {
					PRINT_ERROR(this->toString(READER_LOCK, lockOP, ts), "Write-side is already beeing held.");
					// Flush all writer
					// For more information about using the return value of finishTXN(), and why it is important,
					// have a look at RWLock::readTransition() in V_READ case (approx. line 181).
					if (this->finishTXN(ts, WRITER_LOCK, true, txnsOFile, locksHeldOFile, activeTXNs)) {
						// finishTXN has cleaned up all all TXNs. We must now deconstruct der last pos stack.
						while (!lastNPos.empty()) {
							this->writer_count--;
							this->lastNPos.pop();
						}
						if (this->writer_count != 0) {
							PRINT_ERROR(this->toString(READER_LOCK, lockOP, ts), "Inconsistent writer_count, still above zero (" << this->writer_count << ")." );
							this->writer_count = 0;
						}
					} else {
						PRINT_ERROR(this->toString(READER_LOCK, lockOP, ts), "Cannot flush all reader." );
					}
				} else if (this->writer_count > 0 && this->reader_count > 0) {
					stringstream ss;
					ss << "Invalid state on reader-writer lock (op=" << lockOP << ",addr=" << hex << showbase << this->lockAddress << noshowbase << ", lockMember=" << lockMember << ", ts=" << ts << "): ";
					ss << "this->writer_count == 0 && this->reader_count > 0";
					throw logic_error(ss.str());
				} else if (this->writer_count == 0 && this->reader_count == 0) {
					// Nothing to do
				}
				this->reader_count++;
				this->lastNPos.push(LockPos());
				LockPos& tempLockPos = this->lastNPos.top();
				tempLockPos.subLock = READER_LOCK;
				tempLockPos.start = ts;
				tempLockPos.lastLine = line;
				string tmp = kernelDir;
				if (tmp.back() != '/') {
					tmp.append("/");
				}
				if (file.find(tmp) != string::npos) {
					tempLockPos.lastFile = file.substr(tmp.length());
				} else {
					tempLockPos.lastFile = file;
				}
				tempLockPos.lastFn = fn;
				tempLockPos.lastPreemptCount = preemptCount;
				tempLockPos.lastIRQSync = irqSync;

				// a P() suspends the current TXN and creates a new one
				startTXN(ts, READER_LOCK, activeTXNs);
				break;
			}

		case V_READ:
			{
				if (!this->lastNPos.empty()) {
					// a V() finishes the current TXN (even if it does
					// not match its starting P()!) and continues the
					// enclosing one
					if (activeTXNs.empty()) {
						PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "TXN: V() but no running TXN!");
					}
					// The lockAddress as well as the subLock must match for a txn to be removed from the stack.
					// In rare corner cases where the trace does *not* contain a valid sequence of P()s and V()s
					// finishTXN() might fail. If so, we are *not* allowed to remove top element of lastNPos.
					// Otherwise, lastNPos and activeTXNs get out-of-sync.
					if (this->finishTXN(ts, READER_LOCK, false, txnsOFile, locksHeldOFile, activeTXNs)) {
						this->lastNPos.pop();
					}
				} else {
					PRINT_ERROR(this->toString(READER_LOCK, lockOP, ts), "No last locking position known, cannot pop.");
				}
				if (this->reader_count == 0) {
					PRINT_ERROR(this->toString(READER_LOCK, lockOP, ts), "Lock has already been released.");
				} else {
					this->reader_count--;
				}
				break;
			}
			
		default:
			throw logic_error("Unknown lock operation");
	}	
}


/**
 * Releases a lock, and finishes the corresponding TXN.
 *
 * @param ts             Current timestamp
 * @param lockPtr        Lock to be released
 * @param txnsOFile      ofstream for txns.csv
 * @param locksHeldOFile ofstream for locks_held.csv
 */
bool RWLock::finishTXN(unsigned long long ts, enum SUB_LOCK subLock, bool removeReader, std::ofstream& txnsOFile, std::ofstream& locksHeldOFile, std::deque<TXN>& activeTXNs) {
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

	while (!activeTXNs.empty()) {
		if (!SKIP_EMPTY_TXNS || activeTXNs.back().memAccessCounter > 0) {
			// Record this TXN
			txnsOFile << activeTXNs.back().id << delimiter;
			txnsOFile << activeTXNs.back().start << delimiter;
			txnsOFile << ts << "\n";

			// Note which locks were held during this TXN by looking at all
			// TXNs "below" it (the order does not matter because we record the
			// start timestamp).  Don't mention a lock more than once (see
			// below).
			std::set<decltype(RWLock::read_id)> locks_seen;
			for (auto thisTXN : activeTXNs) {
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
					locksHeldOFile << dec << activeTXNs.back().id << delimiter << lockID << delimiter;
					locksHeldOFile << tempLockPos.start << delimiter;
					locksHeldOFile << tempLockPos.lastFile << delimiter;
					locksHeldOFile << tempLockPos.lastLine << delimiter << tempLockPos.lastFn << delimiter;
					locksHeldOFile << tempLockPos.lastPreemptCount << delimiter;
					switch (tempLockPos.lastIRQSync) {
						case LOCK_NONE:
							locksHeldOFile << "LOCK_NONE";
							break;

						case LOCK_IRQ:
							locksHeldOFile << "LOCK_IRQ";
							break;

						case LOCK_IRQ_NESTED:
							locksHeldOFile << "LOCK_IRQ_NESTED";
							break;

						case LOCK_BH:
							locksHeldOFile << "LOCK_BH";
							break;

						default:
							return EXIT_FAILURE;
					}
					locksHeldOFile << "\n";
				} else {
					PRINT_ERROR(tempLock->toString(thisTXN.subLock) << ",ts=" << dec << ts, "TXN: Internal error, lock is part of the TXN hierarchy but not held?");
				}
			}
		}

		// are we done deconstructing the TXN stack?
		if (activeTXNs.back().lock == this) {
			if (activeTXNs.back().subLock == subLock) {
				// We have deconstructed the TXN stack until the topmost
				// TXN belongs to the lock for which we have seen a V().
				activeTXNs.pop_back();
				found = true;
				// But still, the TXN stack may contain TXNs belonging to lockPtr.
				if (removeReader) {
					// The caller wants to remove all READER_LOCKs from the TXN stack.
					for (std::deque<TXN>::iterator it = activeTXNs.begin(); it != activeTXNs.end();) {
						if (it->lock != this) {
							it++;
							continue;
						}
						// Does subLock and lockPtr match?
						if (it->subLock == READER_LOCK) {
							PRINT_DEBUG(it->lock->toString(it->subLock) << ",ts=" << dec << ts << ",txn=" << it->id, "Flushing TXN");
							it = activeTXNs.erase(it);
						} else {
							it++;
							PRINT_ERROR(it->lock->toString(it->subLock) << ",ts=" << dec << ts, "Multiple active txns for one writer lock");
						}
					}
				}
				break;
			} else {
				PRINT_ERROR(activeTXNs.back().lock->toString(activeTXNs.back().subLock) << ",ts=" << dec << ts, "sublock does not match");
			}
		}

		// this is a TXN we need to recreate under a different ID after we're done

		// pushing in front to preserve order
		restartTXNs.push_front(std::move(activeTXNs.back()));
		activeTXNs.pop_back();
		// give TXN a new ID + timestamp + memAccessCounter
		restartTXNs.front().id = curTXNID++;
		restartTXNs.front().start = ts;
		restartTXNs.front().memAccessCounter = 0;
	}

	// sanity check whether activeTXNs is not empty -- this should never happen
	// because we check whether we know this lock before we call finishTXN()!
	if (!found) {
		PRINT_ERROR(this->toString(subLock) << ",ts=" << dec << ts, "TXN: Internal error -- V() but no matching TXN!");
	}

	// recreate TXNs
	std::move(restartTXNs.begin(), restartTXNs.end(), std::back_inserter(activeTXNs));
	return found;

}

void RWLock::startTXN(unsigned long long ts, enum SUB_LOCK subLock, std::deque<TXN>& activeTXNs) {
	activeTXNs.push_back(TXN());
	activeTXNs.back().id = curTXNID++;
	activeTXNs.back().start = ts;
	activeTXNs.back().memAccessCounter = 0;
	activeTXNs.back().lock = this;
	activeTXNs.back().subLock = subLock;
}

RWLock* RWLock::allocLock(unsigned long long lockAddress, unsigned allocID, string lockType, const char *lockVarName, unsigned flags) {
	// Insert virgin lock into map, and write entry to file
	RWLock *ret;
	
	if (lockType.compare("raw_spinlock_t") == 0 ||
		lockType.compare("mutex") == 0 ||
		lockType.compare(PSEUDOLOCK_NAME_SOFTIRQ) == 0 ||
		lockType.compare(PSEUDOLOCK_NAME_HARDIRQ) == 0 ||
		lockType.compare("semaphore") == 0 ||
		lockType.compare("bit_spin_lock") == 0 ||
		lockType.compare("sleep mutex") == 0 ||
		lockType.compare("spin mutex") == 0) {
		ret = new WLock(lockAddress, allocID, lockType, lockVarName, flags);
		if (!ret) {
			PRINT_ERROR("lockAddress=" << showbase << hex << lockAddress << noshowbase, "Cannot allocate WLock.");
			// This is a severe error. Abort immediately!
			exit(1);
		}
	} else if (lockType.compare(PSEUDOLOCK_NAME_RCU) == 0) {
		ret = new RLock(lockAddress, allocID, lockType, lockVarName, flags);
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
			   lockType.compare("lockmgr") == 0) {
		ret = new RWLock(lockAddress, allocID, lockType, lockVarName, flags);
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

	return ret;
}
