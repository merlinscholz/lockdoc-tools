#ifndef __RLOCK_H__
#define __RLOCK_H__

#include <stdexcept>
#include "rwlock.h"

using std::dec;

struct RLock : public RWLock {
	RLock (unsigned long long _lockAddress, unsigned _allocID, std::string _lockType, const char *_lockVarName, unsigned _flags, LockManager *_lockManager) : RWLock(_lockAddress, _allocID, _lockType, _lockVarName, _flags, _lockManager) {
			
	}

	std::string toString() {
		return RWLock::toString(READER_LOCK);
	}

	void initIDs(unsigned long long &id) {
		read_id = id++;
		write_id = -1;
	}

	bool isHeld() {
			return this->reader_count > 0;
	}

	unsigned long long getID(enum SUB_LOCK subLock) {
		if (subLock == READER_LOCK) {
			return read_id;
		}
		throw invalid_argument("ID for writer sub lock requested.");
	}

	virtual void writeLock(std::ofstream &oFile, char delimiter) {
		this->writeReaderLock(oFile, delimiter);
	}

	void transition(
	enum LOCK_OP lockOP,
	unsigned long long ts,
	std::string const& file,
	unsigned long long line,
	string const& lockMember,
	unsigned flags,
	const char *kernelDir,
	long ctx) {
		RWLock::readTransition(lockOP, ts, file, line, lockMember, flags, kernelDir, ctx);
	}
};

#endif // __RLOCK_H__


