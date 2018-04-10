#ifndef __RWLOCK_H__
#define __RWLOCK_H__

#include <stack>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <deque>
#include <stack>
#include <typeinfo>
#include <cxxabi.h>
#include "log_event.h"

using namespace std;

enum SUB_LOCK {
	READER_LOCK = 0,
	WRITER_LOCK	
};

struct LockPos {
	enum SUB_LOCK subLock;										// Which side of the lock (reader or write) has been acquired
	unsigned long long start;									// Timestamp when the lock has been acquired
	int lastLine;												// Position within the file where the lock has been acquired for the last time
	std::string lastFile;											// Last file from where the lock has been acquired
	std::string lastFn;												// Last caller
//	string lastLockFn;											// Lock function used the last time
	unsigned long long lastPreemptCount;						// Value of preemptcount() after the lock has been acquired
	enum IRQ_SYNC lastIRQSync;									// IRQ synchronization used
};

/**
 * Represents a Transaction (TXN).
 *
 * A TXN starts with a P or V, and ends with a P or V -- i.e., with a change in
 * the set of currently acquired locks.  If no lock is held, no TXN is active.
 * Hence, each TXN is associated with a set of held locks, which can be ordered
 * by looking at their start timestamp.
 *
 * Note that a TXN does not necessarily end with a V() on the same lock as it
 * started with using a P().
 */
struct TXN {
	unsigned long long id;										// ID
	unsigned long long start;									// Timestamp when this TXN started
	unsigned long long memAccessCounter;						// Memory accesses in this TXN (allows suppressing empty TXNs in the output)
	unsigned long long lockPtr;									// Ptr of the lock that started this TXN
	enum SUB_LOCK subLock;
	
};

// Must be definied here, because the lock subclasses need it.
// returns stringified var if cond is false, or "NULL" if cond is true
template <typename T>
static inline std::string sql_null_if(T var, bool cond)
{
	if (!cond) {
		return std::to_string(var);
	}
	return "\\N";
}

template <>
inline std::string sql_null_if<const char*>(const char *var, bool cond)
{
    if (!cond) {
        return std::string(var);
    }   
    return "\\N";
}

template <>
inline std::string sql_null_if<std::string>(std::string var, bool cond)
{
    if (!cond) {
        return var;
    }   
    return "\\N";
}

/**
 * Describes an instance of a lock. In our model every lock is a reader-writer lock.
 * It internally consists of two so-called sub locks, a writer and a reader sub lock.
 * The subclasses RLock and WLock only use the respective sub locks.
 */
struct RWLock {
	static const char DELIMITER = ',';
	unsigned long long read_id;									// A unique id which describes a particular lock within our dataset
	unsigned long long write_id;								// A unique id which describes a particular lock within our dataset
	unsigned long long lockAddress;								// The pointer to the memory area where the lock resides
	int reader_count;											// Indicates whether the lock is held or not (may be > 1 for recursive locks)
	int writer_count;											// Indicates whether the lock is held or not (0 or 1)
	unsigned allocation_id;										// ID of the allocation this lock resides in (0 if not embedded)
	std::string lockType;										// Describes the lock type
	std::string lockVarName;									// The variable name of the lock, e.g., console_sem, if static (allocation_id == 0)
	std::stack<LockPos> lastNPos;								// Last N takes of this lock, max. one element besides for recursive locks (such as RCU)
	
	RWLock (unsigned long long _lockAddress, unsigned _allocID, std::string _lockType, const char *_lockVarName) : 
		lockAddress(_lockAddress), reader_count(0), writer_count(0), 
		allocation_id(_allocID), lockType(_lockType) {
		if (_lockVarName) {
			lockVarName = string(_lockVarName);
		}
	};

	/**
	 * Convert a SUB_LOCK to a humand-readable string
	 * 
	 */
	static std::string SubLockToString(enum SUB_LOCK subLock) {
			switch(subLock) {
				case READER_LOCK:
					return "READER_LOCK";
					
				case WRITER_LOCK:
					return "WRITER_LOCK";
					
				default:
					return "UNKNOWN";
			}
	}

	/**
	 * Convert a LOCK_OP to a humand-readable string
	 * 
	 */
	static std::string LockOPToString(enum LOCK_OP lockOp) {
			switch(lockOp) {
				case P_WRITE:
					return "P_WRITE";
					
				case P_READ:
					return "P_READ";
					
				case V_WRITE:
					return "V_WRITE";
					
				case V_READ:
					return "V_READ";
				default:
					return "UNKNOWN";
			}
	}
	
	static bool isPseudoLock(unsigned long long lockAddress) {
		return lockAddress == PSEUDOLOCK_ADDR_RCU || lockAddress == PSEUDOLOCK_ADDR_SOFTIRQ || lockAddress == PSEUDOLOCK_ADDR_HARDIRQ;
	}

	/**
	 * Put all usefull information about this lock in one string
	 * 
	 */
	virtual std::string toString() {
		stringstream ss;
		ss << this->toString(READER_LOCK) << "--";
		ss << this->toString(WRITER_LOCK);
		
		return ss.str();
	}

	/**
	 * Put all usefull information about this lock's {@param subLock} in one string
	 */
	std::string toString(enum SUB_LOCK subLock) {
		stringstream ss;
		int status;
		char * demangled = abi::__cxa_demangle(typeid(*this).name(),0,0,&status);

		ss << demangled << RWLock::DELIMITER;
		ss << this->getID(subLock) << RWLock::DELIMITER;
		ss << RWLock::SubLockToString(subLock) << RWLock::DELIMITER;
		ss << hex << showbase << this->lockAddress;
		free(demangled);
		
		return ss.str();
	}

	/**
	 * Put all usefull information about this lock's {@param subLock} in one string
	 * Additionally append the current lock operation {@param lockOP} and the timestamp {@param ts}.
	 */
	std::string toString(enum SUB_LOCK subLock, enum LOCK_OP lockOP, unsigned long long ts) {
		stringstream ss;
		ss << this->toString(subLock) << RWLock::DELIMITER;
		ss << RWLock::LockOPToString(lockOP) << RWLock::DELIMITER;
		ss << dec << ts;
		
		return ss.str();
	}

	/**
	 * Assign each sub lock a new, and unique {@param id}.
	 */
	virtual void initIDs(unsigned long long &id) {
		read_id = id++;
		write_id = id++;
	}

	/**
	 * Returns true if the lock is currently held. False otherwise.
	 */
	virtual bool isHeld() {
		return this->reader_count > 0 || this->writer_count > 0;
	}

	/**
	 * Get a {@param subLock}'s id.
	 * If an invalid value for {@param subLock} is provided,
	 * an exception is thrown.
	 */
	virtual unsigned long long getID(enum SUB_LOCK subLock) {
		if (subLock == READER_LOCK) {
			return read_id;
		} else if (subLock == WRITER_LOCK) {
			return write_id;
		}
		throw invalid_argument("Invalid sub lock.");
	}

	/**
	 * Write this lock's information to {@param oFile} using {@param delimiter}
	 * as separator between each data. 
	 */
	virtual void writeLock(std::ofstream &oFile, char delimiter) {
		this->writeWriterLock(oFile, delimiter);
		this->writeReaderLock(oFile, delimiter);
	}

	/**
	 * Perform the lock operation {@param lockOP}.
	 * If an invalid value for {@param lockOP} is given,
	 * an exception is thrown. Valid values are P_WRITE, V_WRITE, P_READ, and V_READ.
	 */
	virtual void transition(
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
		if (lockOP == P_WRITE || lockOP == V_WRITE) {
			writeTransition(lockOP, ts, file, line, fn, lockMember, preemptCount, irqSync, activeTXNs, txnsOFile, locksHeldOFile);
		} else if (lockOP == P_READ || lockOP == V_READ) {
			readTransition(lockOP, ts, file, line, fn, lockMember, preemptCount, irqSync, activeTXNs, txnsOFile, locksHeldOFile);
		} else {
			stringstream ss;
			ss << "Invalid op(" << lockOP << "," << hex << showbase << this->lockAddress << noshowbase << "," << lockMember << ") at ts " << ts << endl;
			throw logic_error(ss.str());
		}
	}

	/**
	 * Create and init an instance of 
	 * 
	 */
	static RWLock* allocLock(unsigned long long lockAddress, unsigned allocID, string lockType, const char *lockVarName);

	protected:
	/**
	 * Perform the lock operation {@param lockOP} on the write sub lock.
	 * If an invalid value for {@param lockOP} is given,
	 * an exception is thrown. Valid values are P_WRITE, and V_WRITE.
	 */
	void writeTransition(
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
	std::ofstream& locksHeldOFile);

	/**
	 * Perform the lock operation {@param lockOP} on the read sub lock.
	 * If an invalid value for {@param lockOP} is given,
	 * an exception is thrown. Valid values are P_READ, and V_READ.
	 */
	void readTransition(
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
	std::ofstream& locksHeldOFile);

	virtual void writeWriterLock(std::ofstream &oFile, char delimiter) {
		oFile << dec << write_id << delimiter << lockAddress;
		oFile << delimiter << sql_null_if(allocation_id, allocation_id == 0) << delimiter << lockType << delimiter;
		oFile << 'w' << delimiter << sql_null_if(lockVarName, lockVarName.empty()) << "\n";
	}

	virtual void writeReaderLock(std::ofstream &oFile, char delimiter) {
		oFile << dec << read_id << delimiter << lockAddress;
		oFile << delimiter << sql_null_if(allocation_id, allocation_id == 0) << delimiter << lockType << delimiter;
		oFile << 'r' << delimiter << sql_null_if(lockVarName, lockVarName.empty()) << "\n";
	}
};

void startTXN(unsigned long long ts, unsigned long long lockPtr, enum SUB_LOCK subLock);
bool finishTXN(unsigned long long ts, unsigned long long lockPtr, enum SUB_LOCK subLock, bool removeReader, std::ofstream& txnsOFile, std::ofstream& locksHeldOFile);
#endif // __RWLOCK_H__
