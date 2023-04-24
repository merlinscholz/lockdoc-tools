#ifndef __LOCKMANAGER_H__
#define __LOCKMANAGER_H__

#include <deque>
#include <map>
#include "rwlock.h"

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
	unsigned long long start_ts;									// Timestamp when this TXN started
	unsigned long long start_ctx;									// Context where this TXN started
	unsigned long long memAccessCounter;						// Memory accesses in this TXN (allows suppressing empty TXNs in the output)
	RWLock *lock;
	enum SUB_LOCK subLock;	
};

struct LockManager {
	private: 
	/**
	 * A stack of currently active, nested TXNs.  Implemented as a deque for mass
	 * insert() in finishTXN().
	 * We maintain one stack per context. In Linux terms, a context
	 * is irq, softirq, or a task.
	 * A context's id corresponds to a task's tid. For irq and softirq,
	 * we use the artifical ids -2 and -1, respectively.
	 */
	std::map<long, std::deque<TXN>> m_activeTXNs;
	/**
	 * The next id for a new TXN.
	 */
	unsigned long long m_nextTXNID;
	/**
	 * The next id for a new lock.
	 */
	unsigned long long m_nextLockID;
	/**
	 * Contains all known locks. The ptr of a lock is used as an index.
	 */
	std::map<unsigned long long,RWLock*> m_locks;
	std::ofstream& m_txnsOFile;
	std::ofstream& m_locksHeldOFile;
	void startTXN(RWLock *lock, unsigned long long ts, enum SUB_LOCK subLock, long ctx);
	bool finishTXN(RWLock *lock, unsigned long long ts, enum SUB_LOCK subLock, bool removeReader, long ctx, long ctxOld);
	long findTXN(RWLock *lck, enum SUB_LOCK subLock, long ctx);
	public:
	friend struct RWLock;
	LockManager(std::ofstream& txnsOFile, std::ofstream& locksHeldOFile) : m_nextTXNID(1), m_nextLockID(1), m_txnsOFile(txnsOFile), m_locksHeldOFile(locksHeldOFile) {

	}
	/**
	 * Create and init an instance of a new lock
	 * 
	 */
	RWLock* allocLock(unsigned long long lockAddress, unsigned allocID, string lockType, const char *lockVarName, unsigned flags);
	/**
	 * Get top (= current active) TXN
	 */
	struct TXN& getActiveTXN(long ctx);
	bool hasActiveTXN(long ctx);
	bool isOnTXNStack(long ctx, RWLock *lock, enum SUB_LOCK subLock);
	void closeAllTXNs(unsigned long long ts);
	RWLock* findLock(unsigned long long address);
	void deleteLockByArea(unsigned long long address, unsigned long long size);
};

#endif // __LOCKMANAGER_H__
