#ifndef __WLOCK_H__
#define __WLOCK_H__

#include "rwlock.h"

struct WLock : public RWLock {
	WLock (unsigned long long _lockAddress, unsigned _allocID, std::string _lockType, const char *_lockVarName) : RWLock(_lockAddress, _allocID, _lockType, _lockVarName) {
			
	}

	std::string toString() {
		return RWLock::toString(WRITER_LOCK);
	}

	void initIDs(unsigned long long &id) {
		read_id = -1;
		write_id = id++;
	}

	bool isHeld() {
			return this->writer_count > 0;
	}

	unsigned long long getID(enum SUB_LOCK subLock) {
		if (subLock == WRITER_LOCK) {
			return write_id;
		}
		throw invalid_argument("ID for reader sub lock requested.");
	}

	virtual void writeLock(std::ofstream &oFile, char delimiter) {
		this->writeWriterLock(oFile, delimiter);
	}

	void transition(
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
	std::ofstream& locksHeldOFile,
	const char *kernelDir) {
		RWLock::writeTransition(lockOP, ts, file, line, fn, lockMember, preemptCount, irqSync, activeTXNs, txnsOFile, locksHeldOFile, kernelDir);
	}
};

#endif // __WLOCK_H__
