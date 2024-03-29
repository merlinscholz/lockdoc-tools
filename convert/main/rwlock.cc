#include "rwlock.h"
#include "rlock.h"
#include "wlock.h"
#include "config.h"
#include "lockmanager.h"
#include <sstream>
#include <iostream>
#include <set>


void RWLock::writeTransition(
	enum LOCK_OP lockOP,
	unsigned long long ts,
	std::string const& file,
	unsigned long long line,
	string const& lockMember,
	unsigned flags,
	const char *kernelDir,
	long ctx) {
	long ctxOld = ctx;

	this->updateFlags(flags);
	switch (lockOP) {
		case P_WRITE:
			{
				if (this->writer_count == 0 && this->reader_count > 0) {
					PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "Read-side is already beeing held.");
					// Flush all reader
					// For more information about using the return value of finishTXN(), and why it is important,
					// have a look at RWLock::readTransition() in V_READ case (approx. line 181).
					if (lockManager->finishTXN(this, ts,  READER_LOCK, true, ctx, ctxOld)) {
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
						if (lockManager->finishTXN(this, ts, WRITER_LOCK, false, ctx, ctxOld)) {
							// forget locking position because this kind of
							// lock can officially only be held once
							this->lastNPos.pop();
						}
					}
				} else if (this->writer_count > 0 && this->reader_count > 0) {
					stringstream ss;
					ss << "Invalid state on reader-writer lock (op=" << lockOP << ",addr=" << hex << showbase << this->lockAddress << noshowbase << ", ts=" << ts << "): ";
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

				PRINT_DEBUG(this->toString(WRITER_LOCK, lockOP, ts), "P_WRITE in ctx " << ctx);
				// a P() suspends the current TXN and creates a new one
				lockManager->startTXN(this, ts, WRITER_LOCK, ctx);
				break;
			}

		case V_WRITE:
			{
				if (!this->lastNPos.empty()) {
					// a V() finishes the current TXN (even if it does
					// not match its starting P()!) and continues the
					// enclosing one
					PRINT_DEBUG(this->toString(WRITER_LOCK, lockOP, ts), "ctx=" << ctx << ", active=" <<lockManager->hasActiveTXN(ctx) << ", isOnStack=" <<  lockManager->isOnTXNStack(ctx, this, WRITER_LOCK));
					if (!lockManager->hasActiveTXN(ctx) ||
						!lockManager->isOnTXNStack(ctx, this, WRITER_LOCK)) {
						ctx = lockManager->findTXN(this, WRITER_LOCK, ctx);
						if (ctx == ctxOld && !lockManager->hasActiveTXN(ctx)) {
							PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "TXN: V() but no running TXN!");
						} else if (ctx != ctxOld && lockManager->hasActiveTXN(ctx)) {
							PRINT_DEBUG(this->toString(WRITER_LOCK, lockOP, ts), "TXN: V() no running TXN in ctx " << ctxOld << ", stealing from " << ctx);
						}
					}
					// For more information about using the return value of finishTXN(), and why it is important,
					// have a look at RWLock::readTransition() in V_READ case (approx. line 181).
					if (lockManager->finishTXN(this, ts, WRITER_LOCK, false, ctx, ctxOld)) {
						this->lastNPos.pop();
					}
				} else {
					PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "No last locking position known, cannot pop.");
				}
				if (this->writer_count == 0) {
					PRINT_ERROR(this->toString(WRITER_LOCK, lockOP, ts), "Lock has already been released.");
				} else {
					this->writer_count--;
					PRINT_DEBUG(this->toString(WRITER_LOCK, lockOP, ts), "V_WRITE in ctx " << ctx);
				}
				break;
			}
			
		case P_READ:
		case V_READ:
			{
				stringstream ss;
				ss << "Invalid op on writer lock (" << hex << showbase << this->lockAddress << noshowbase << ") at ts " << dec << ts << endl;
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
	string const& lockMember,
	unsigned flags,
	const char *kernelDir,
	long ctx) {
	long ctxOld = ctx;

	this->updateFlags(flags);
	switch (lockOP) {
		case P_WRITE:
		case V_WRITE:
			{
				stringstream ss;
				ss << "Invalid op on reader lock (" << hex << showbase << this->lockAddress << noshowbase << ") at ts " << dec << ts << endl;
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
					if (lockManager->finishTXN(this, ts, WRITER_LOCK, true, ctx, ctxOld)) {
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
					ss << "Invalid state on reader-writer lock (op=" << lockOP << ",addr=" << hex << showbase << this->lockAddress << noshowbase << ", ts=" << ts << "): ";
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
				PRINT_DEBUG(this->toString(READER_LOCK, lockOP, ts), "P_READ in ctx " << ctx);

				// a P() suspends the current TXN and creates a new one
				lockManager->startTXN(this, ts, READER_LOCK, ctx);
				break;
			}

		case V_READ:
			{
				if (!this->lastNPos.empty()) {
					// a V() finishes the current TXN (even if it does
					// not match its starting P()!) and continues the
					// enclosing one
					PRINT_DEBUG(this->toString(READER_LOCK, lockOP, ts), "ctx=" << ctx << ", active=" <<lockManager->hasActiveTXN(ctx) << ", isOnStack=" <<  lockManager->isOnTXNStack(ctx, this, READER_LOCK));
					if (!lockManager->hasActiveTXN(ctx) ||
						!lockManager->isOnTXNStack(ctx, this, READER_LOCK)) {
						ctx = lockManager->findTXN(this, READER_LOCK, ctx);
						if (ctx == ctxOld && !lockManager->hasActiveTXN(ctx)) {
							PRINT_ERROR(this->toString(READER_LOCK, lockOP, ts), "TXN: V() but no running TXN!");
						} else if (ctx != ctxOld && lockManager->hasActiveTXN(ctx)) {
							PRINT_DEBUG(this->toString(READER_LOCK, lockOP, ts), "TXN: V() no running TXN in ctx " << ctxOld << ", stealing from " << ctx);
						}
					}
					// The lockAddress as well as the subLock must match for a txn to be removed from the stack.
					// In rare corner cases where the trace does *not* contain a valid sequence of P()s and V()s
					// finishTXN() might fail. If so, we are *not* allowed to remove top element of lastNPos.
					// Otherwise, lastNPos and activeTXNs get out-of-sync.
					if (lockManager->finishTXN(this, ts, READER_LOCK, false, ctx, ctxOld)) {
						this->lastNPos.pop();
					}
				} else {
					PRINT_ERROR(this->toString(READER_LOCK, lockOP, ts), "No last locking position known, cannot pop.");
				}
				if (this->reader_count == 0) {
					PRINT_ERROR(this->toString(READER_LOCK, lockOP, ts), "Lock has already been released.");
				} else {
					this->reader_count--;
					PRINT_DEBUG(this->toString(READER_LOCK, lockOP, ts), "V_READ in ctx " << ctx);
				}
				break;
			}
			
		default:
			throw logic_error("Unknown lock operation");
	}	
}
