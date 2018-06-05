#include "rwlock.h"
#include "rlock.h"
#include "wlock.h"
#include "config.h"
#include <sstream>
#include <iostream>

void RWLock::writeTransition(
	enum LOCK_OP lockOP,
	unsigned long long ts,
	std::string const& file,
	unsigned long long line,
	std::string const& fn,
	string const& lockMember,
	unsigned long long preemptCount,
	enum IRQ_SYNC irqSync,
	std::deque<TXN> activeTXNs,
	std::ofstream& txnsOFile,
	std::ofstream& locksHeldOFile) {
		
	switch (lockOP) {
		case P_WRITE:
			{
				if (this->writer_count == 0 && this->reader_count > 0) {
					PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "Read-side is already beeing held.");
					// Flush all reader
					// For more information about using the return value of finishTXN(), and why it is important,
					// have a look at RWLock::readTransition() in V_READ case (approx. line 181).
					if (finishTXN(ts, this->lockAddress, READER_LOCK, true, txnsOFile, locksHeldOFile)) {
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
				} else if (this->writer_count == 1 && this->reader_count == 0) {
					PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "Lock is already beeing held.");
					// finish TXN for this lock, we may have missed
					// the corresponding V()
					// For more information about using the return value of finishTXN(), and why it is important,
					// have a look at RWLock::readTransition() in V_READ case (approx. line 181).
					if (finishTXN(ts, this->lockAddress, WRITER_LOCK, false, txnsOFile, locksHeldOFile)) {
						// forget locking position because this kind of
						// lock can officially only be held once
						this->lastNPos.pop();
					}
				} else if (this->writer_count == 1 && this->reader_count > 0) {
					stringstream ss;
					ss << "Invalid state on reader-writer lock (op=" << lockOP << ",addr=" << hex << showbase << this->lockAddress << noshowbase << ", lockMember=" << lockMember << ", ts=" << ts << "): ";
					ss << "this->writer_count == 0 && this->reader_count > 0";
					throw logic_error(ss.str());
				} else if (this->writer_count == 0 && this->reader_count == 0) {
					// Nothing to do
				}
				this->writer_count = 1;
				this->lastNPos.push(LockPos());
				LockPos& tempLockPos = this->lastNPos.top();
				tempLockPos.subLock = WRITER_LOCK;
				tempLockPos.start = ts;
				tempLockPos.lastLine = line;
				tempLockPos.lastFile = file;
				tempLockPos.lastFn = fn;
				tempLockPos.lastPreemptCount = preemptCount;
				tempLockPos.lastIRQSync = irqSync;

				// a P() suspends the current TXN and creates a new one
				startTXN(ts, this->lockAddress, WRITER_LOCK);
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
					if (finishTXN(ts, this->lockAddress, WRITER_LOCK, false, txnsOFile, locksHeldOFile)) {
						this->lastNPos.pop();
					}
				} else {
					PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "No last locking position known, cannot pop.");
				}
				if (this->writer_count == 0) {
					PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "Lock has already been released.");
				} else {
					this->writer_count = 0;
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
	std::deque<TXN> activeTXNs,
	std::ofstream& txnsOFile,
	std::ofstream& locksHeldOFile) {

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
				} else if (this->writer_count == 1 && this->reader_count == 0) {
					PRINT_ERROR(this->toString(READER_LOCK, lockOP, ts), "Lock is already beeing held.");
					// finish TXN for this lock, we may have missed
					// the corresponding V()
					// For more information about using the return value of finishTXN(), and why it is important,
					// have a look at RWLock::readTransition() in V_READ case (approx. line 181).
					if (finishTXN(ts, this->lockAddress, WRITER_LOCK, false, txnsOFile, locksHeldOFile)) {
						// forget locking position because this kind of
						// lock can officially only be held once
						this->lastNPos.pop();
					}
				} else if (this->writer_count == 1 && this->reader_count > 0) {
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
				tempLockPos.lastFile = file;
				tempLockPos.lastFn = fn;
				tempLockPos.lastPreemptCount = preemptCount;
				tempLockPos.lastIRQSync = irqSync;

				// a P() suspends the current TXN and creates a new one
				startTXN(ts, this->lockAddress, READER_LOCK);
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
					if (finishTXN(ts, this->lockAddress, READER_LOCK, false, txnsOFile, locksHeldOFile)) {
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

RWLock* RWLock::allocLock(unsigned long long lockAddress, unsigned allocID, string lockType, const char *lockVarName) {
	// Insert virgin lock into map, and write entry to file
	RWLock *ret;
	
	if (lockType.compare("raw_spinlock_t") == 0 ||
		lockType.compare("mutex") == 0 ||
		lockType.compare(PSEUDOLOCK_NAME_SOFTIRQ) == 0 ||
		lockType.compare(PSEUDOLOCK_NAME_HARDIRQ) == 0 ||
		lockType.compare("semaphore") == 0 ||
		lockType.compare("bit_spin_locK") == 0) {
		ret = new WLock(lockAddress, allocID, lockType, lockVarName);
		if (!ret) {
			PRINT_ERROR("lockAddress=" << showbase << hex << lockAddress << noshowbase, "Cannot allocate WLock.");
			// This is a severe error. Abort immediately!
			exit(1);
		}
	} else if (lockType.compare(PSEUDOLOCK_NAME_RCU) == 0) {
		ret = new RLock(lockAddress, allocID, lockType, lockVarName);
		if (!ret) {
			PRINT_ERROR("lockAddress=" << showbase << hex << lockAddress << noshowbase, "Cannot allocate WLock.");
			// This is a severe error. Abort immediately!
			exit(1);
		}
	} else if (lockType.compare("rwlock_t") == 0 ||
			   lockType.compare("rw_semaphore") == 0) {
		ret = new RWLock(lockAddress, allocID, lockType, lockVarName);
		if (!ret) {
			PRINT_ERROR("lockAddress=" << showbase << hex << lockAddress << noshowbase, "Cannot allocate WLock.");
			// This is a severe error. Abort immediately!
			exit(1);
		}
	} else {
		PRINT_ERROR("","Unknown lock type: " << lockType);
		// This is a severe error. Abort immediately!
		exit(1);
	}

	return ret;
}
