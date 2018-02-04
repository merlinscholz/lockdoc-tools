#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <unistd.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <stack>

#include <bfd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "config.h"
#include "git_version.h"

#include "dwarves_api.h"
#include "gzstream/gzstream.h"

#define PSEUDOLOCK_ADDR_RCU		0x42
#define PSEUDOLOCK_ADDR_SOFTIRQ	0x43
#define PSEUDOLOCK_ADDR_HARDIRQ	0x44
#define IS_MULTILVL_LOCK(x)		((x).ptr == PSEUDOLOCK_ADDR_RCU)
#define IS_STATIC_LOCK_ADDR(x)	((x) == PSEUDOLOCK_ADDR_RCU || (x) == PSEUDOLOCK_ADDR_SOFTIRQ || (x) == PSEUDOLOCK_ADDR_HARDIRQ)

/**
 * Authors: Alexander Lochmann, Horst Schirmeier
 * Attention: This programm has to be compiled with -std=c++11 !
 * This program takes a csv as input, which contains a series of events.
 * Each event might be of the following types: alloc, free, p(acquire), v (release), read, or write.
 * The program groups the alloc and free events as well as the p and v events by their pointer, and assigns an unique id to each unique entitiy (allocation or lock).
 * For each read or write access it generates the
 * OUTPUT: TODO
 */


using namespace std;

enum AccessTypes {
	ACCESS = 0,
	READ,
	WRITE,
	TYPES_END
};


struct LockPos {
	unsigned long long start;									// Timestamp when the lock has been acquired
	int lastLine;												// Position within the file where the lock has been acquired for the last time
	string lastFile;											// Last file from where the lock has been acquired
	string lastFn;												// Last caller
//	string lastLockFn;											// Lock function used the last time
	unsigned long long lastPreemptCount;						// Value of preemptcount() after the lock has been acquired

};

/**
 * Describes an instance of a lock
 */
struct Lock {
	unsigned long long id;										// A unique id which describes a particular lock within our dataset
	unsigned long long ptr;										// The pointer to the memory area where the lock resides
	int held;													// Indicates whether the lock is held or not (may be > 1 for recursive locks)
	unsigned allocation_id;										// ID of the allocation this lock resides in (0 if not embedded)
	string lockType;											// Describes the lock type
	stack<LockPos> lastNPos;									// Last N takes of this lock, max. one element besides for recursive locks (such as RCU)
};

/**
 * Describes a certain datatype that is observed by our experiment
 */
struct DataType {
	DataType(unsigned id, std::string name) : id(id), name(name), foundInDw(false) { }
	unsigned long long id;										// An unique id for a particular datatype
	std::string name;												// Unique to describe a certain datatype, e.g., task_struct
	bool foundInDw;												// True if the struct has been found in the dwarf information. False otherwise.
};

/**
 * Describes an instance of a certain datatype, e.g., an instance of task_struct
 *
 */
struct Allocation {
	unsigned long long start;									// Timestamp when it has been allocated
	unsigned long long id;										// A unique id which refers to that particular instance
	int size;													// Size in bytes
	int idx;													// An index into the types array, which describes the datatype of this allocation
};

/**
 * Represents a memory access
 */
struct MemAccess {
	unsigned long long id;										// Unique ID of a certain memory access
	unsigned long long ts;										// Timestamp
	unsigned long long alloc_id;								// ID of the memory allocation which has been accessed
	char action;												// Access type: r or w
	int size;													// Size of memory access
	unsigned long long address;									// Accessed address
	unsigned long long stacktrace_id;								// Stack pointer
	unsigned long long instrPtr;								// Instruction pointer
	unsigned long long preemptCount;								// __preempt_count
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
};

/**
 * Contains all known locks. The ptr of a lock is used as an index.
 */
static map<unsigned long long,Lock> lockPrimKey;
/**
 * Contains all active allocations. The ptr to the memory area is used as an index.
 */
static map<unsigned long long,Allocation> activeAllocs;
/**
 * Contains all observed datatypes.
 */
static std::vector<DataType> types;
/**
 * The list of the LOOK_BEHIND_WINDOW last memory accesses.
 */
static vector<MemAccess> lastMemAccesses;
/**
 * A stack of currently active, nested TXNs.  Implemented as a deque for mass
 * insert() in finishTXN().
 */
static std::deque<TXN> activeTXNs;
/**
 * A map of all member names found in all data types.
 * The key is the name, and the value is a name's global id.
 */
static map<string,unsigned long long> memberNames;
/**
 * A map of all stacktraces found in all data types.
 * The key is the stacktrace, and the value is a stacktrace's global id.
 */
static map<string,unsigned long long> stacktraces;

/**
 * Start address and size of the bss and data section. All information is read from the dwarf information during startup.
 */
static uint64_t bssStart = 0, bssSize = 0, dataStart = 0, dataSize = 0;
/**
 * Used to pass context information to the dwarves callback.
 * Have a look at convert_cus_iterator().
 */
struct CusIterArgs {
	std::vector<DataType> *types = nullptr;
	FILE *fp = nullptr;
};
// address -> function name cache
std::map<uint64_t, const char *> functionAddresses;

/**
 * The next id for a new data type.
 */
static unsigned long long curTypeID = 1;
/**
 * The next id for a new lock.
 */
static unsigned long long curLockID = 1;
/**
 * The next id for a new allocation.
 */
static unsigned long long curAllocID = 1;
/**
 * The next id for a new memory access.
 */
static unsigned long long curAccessID = 1;
/**
 * The next id for a new TXN.
 */
static unsigned long long curTXNID = 1;
/**
 * The next id for a member name
 */
static unsigned long long curMemberNameID = 1;
/**
 * The next id for a stacktrace
 */
static unsigned long long curStacktraceID = 1;

static struct cus *cus;
char delimiter = DELIMITER_CHAR;

static void printUsageAndExit(const char *elf) {
	cerr << "usage: " << elf
		<< " [options] -t path/to/data_types.csv -k path/to/vmlinux -b path/to/function_blacklist.csv -m path/to/member_blacklist.csv input.csv[.gz]\n\n"
		"Options:\n"
		" -s  enable processing of seqlock_t (EXPERIMENTAL)\n"
		" -p  enable processing of hardirq/softirq levels from preempt_count as two pseudo locks\n"
		" -v  show version\n"
		" -d  delimiter used in input.csv, and used for the output csv files later on\n"
		" -u  include non-static locks with unknown allocation in output\n"
		"     (these will be assigned to a pseudo allocation with ID 1)\n"
		" -h  help\n";
	exit(EXIT_FAILURE);
}

static void printVersion()
{
	cerr << "convert version: " << GIT_BRANCH << ", " << GIT_MESSAGE << endl;
}

// returns stringified var if cond is false, or "NULL" if cond is true
template <typename T>
static inline std::string sql_null_if(T var, bool cond)
{
	if (!cond) {
		return std::to_string(var);
	}
	return "\\N";
}

// caching wrapper around cus__get_function_at_addr
static const char *get_function_at_addr(const struct cus *cus, uint64_t addr)
{
	auto it = functionAddresses.find(addr);
	if (it == functionAddresses.end()) {
		return functionAddresses[addr] = cus__get_function_at_addr(cus, addr);
	} else {
		return it->second;
	}
}

static void startTXN(unsigned long long ts, unsigned long long lockPtr)
{
	activeTXNs.push_back(TXN());
	activeTXNs.back().id = curTXNID++;
	activeTXNs.back().start = ts;
	activeTXNs.back().memAccessCounter = 0;
	activeTXNs.back().lockPtr = lockPtr;
}

#if 0
// debugging stuff
static void dumpTXNs(const std::deque<TXN>& txns)
{
	cerr << "[ ";
	for (auto&& txn : txns) {
		cerr << txn.lockPtr << " ";
	}
	cerr << "]" << endl;
}
#endif

static void finishTXN(unsigned long long ts, unsigned long long lockPtr, std::ofstream& txnsOFile, std::ofstream& locksHeldOFile)
{
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
			std::set<decltype(Lock::id)> locks_seen;
			for (auto thisTXN : activeTXNs) {
				Lock& tempLock = lockPrimKey[thisTXN.lockPtr];
				if ((IS_MULTILVL_LOCK(tempLock) && tempLock.held >= 1) || (!IS_MULTILVL_LOCK(tempLock) && tempLock.held == 1)) {
					LockPos& tempLockPos = tempLock.lastNPos.top();
					// Have we already seen this lock?
					if (locks_seen.find(tempLock.id) != locks_seen.end()) {
						// For example, RCU may be held multiple times, but the
						// locks_held table structure currently does not allow
						// this (because the lock_id is part of the PK).
						continue;
					}
					locks_seen.insert(tempLock.id);
					locksHeldOFile << dec << activeTXNs.back().id << delimiter << tempLock.id << delimiter;
					locksHeldOFile << tempLockPos.start << delimiter;
					locksHeldOFile << tempLockPos.lastFile << delimiter;
					locksHeldOFile << tempLockPos.lastLine << delimiter << tempLockPos.lastFn << delimiter;
					locksHeldOFile << tempLockPos.lastPreemptCount << "\n";
				} else {
					cerr << "TXN: Internal error, lock is part of the TXN hierarchy but not held? lockPtr = " << showbase << hex << thisTXN.lockPtr << noshowbase << endl;
				}
			}
		}

		// are we done deconstructing the TXN stack?
		if (activeTXNs.back().lockPtr == lockPtr) {
			activeTXNs.pop_back();
			found = true;
			break;
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
		cerr << "TXN: Internal error -- V() but no matching TXN! lockPtr = " << showbase << hex << lockPtr << noshowbase << endl;
	}

	// recreate TXNs
	std::move(restartTXNs.begin(), restartTXNs.end(), std::back_inserter(activeTXNs));
}

/* handle P() / V() events */
static void handlePV(
	char action,
	unsigned long long ts,
	unsigned long long ptr,
	string const& file,
	unsigned long long line,
	string const& fn,
	string const& typeStr,
	string const& lockType,
	unsigned long long preemptCount,
	bool includeAllLocks,
	unsigned long long pseudoAllocID,
	ofstream& locksOFile,
	ofstream& txnsOFile,
	ofstream& locksHeldOFile
	)
{
	auto itLock = lockPrimKey.find(ptr);
	if (itLock != lockPrimKey.end()) {
		if (action == 'p') {
			// Found a known lock. Just update its metainformation
#ifdef VERBOSE
			cerr << "Found existing lock at address " << showbase << hex << ptr << noshowbase << ". Just updating the meta information." << endl;
#endif
			if (ptr == PSEUDOLOCK_ADDR_RCU) {
				itLock->second.held++;
			} else {
				// Has it already been acquired?
				if (itLock->second.held != 0) {
					cerr << "Lock at address " << showbase << hex << ptr << noshowbase << " is already being held!" << PRINT_CONTEXT << endl;
					// finish TXN for this lock, we may have missed
					// the corresponding V()
					finishTXN(ts, ptr, txnsOFile, locksHeldOFile);
					// forget locking position because this kind of
					// lock can officially only be held once
					itLock->second.lastNPos.pop();
				}
				itLock->second.held = 1;
			}
			itLock->second.lastNPos.push(LockPos());
			LockPos& tempLockPos = itLock->second.lastNPos.top();
			tempLockPos.start = ts;
			tempLockPos.lastLine = line;
			tempLockPos.lastFile = file;
			tempLockPos.lastFn = fn;
			tempLockPos.lastPreemptCount = preemptCount;

			// a P() suspends the current TXN and creates a new one
			startTXN(ts, ptr);
		} else if (action == 'v') {
			if (!itLock->second.lastNPos.empty()) {
				// a V() finishes the current TXN (even if it does
				// not match its starting P()!) and continues the
				// enclosing one
				if (activeTXNs.empty()) {
					cerr << "TXN: V() but no running TXN!" << PRINT_CONTEXT << endl;
				} else {
					finishTXN(ts, ptr, txnsOFile, locksHeldOFile);
				}

				itLock->second.lastNPos.pop();
			} else {
				cerr << "No last locking position known for lock at address " << showbase << hex << ptr << noshowbase << ", cannot pop." << PRINT_CONTEXT << endl;
			}
			if (ptr == PSEUDOLOCK_ADDR_RCU) {
				if (itLock->second.held == 0) {
					cerr << "RCU lock has already been released." << PRINT_CONTEXT << endl;
				} else {
					itLock->second.held--;
				}
			} else {
				// Has it already been released?
				if (itLock->second.held == 0) {
					cerr << "Lock at address " << showbase << hex << ptr << noshowbase << " has already been released." << PRINT_CONTEXT << endl;
				}
				itLock->second.held = 0;
			}
		}
		// Since the lock alreadys exists, and the metainformation has been updated, no further action is required
		return;
	}

	// categorize currently unknown lock
	unsigned allocation_id = 0;
	// A lock which probably resides in one of the observed allocations. If not, check if it is a global lock
	// This way, locks which reside in global structs are recognized as 'embedded in'.
	auto itAlloc = activeAllocs.upper_bound(ptr);
	if (itAlloc != activeAllocs.begin()) {
		itAlloc--;
		if (ptr < itAlloc->first + itAlloc->second.size) {
			allocation_id = itAlloc->second.id;
		}
	}
	if (allocation_id == 0) {
		if ((ptr >= bssStart && ptr < bssStart + bssSize) ||
			(ptr >= dataStart && ptr < dataStart + dataSize) ||
			(typeStr.compare("static") == 0 && IS_STATIC_LOCK_ADDR(ptr))) {
			// static lock which resides either in the bss segment or in the data segment
			// or global static lock aka rcu lock
#ifdef VERBOSE
			cout << "Found static lock: " << showbase << hex << ptr << noshowbase << endl;
#endif
		} else if (includeAllLocks) {
			// non-static lock, but we don't known the allocation it belongs to
#ifdef VERBOSE
			cout << "Found non-static lock belonging to unknown allocation, assigning to pseudo allocation: "
				<< showbase << hex << ptr << noshowbase << endl;
#endif
			allocation_id = pseudoAllocID;
		} else {
#ifdef VERBOSE
			cerr << "Lock at address " << showbase << hex << ptr << noshowbase << " does not belong to any of the observed memory regions. Ignoring it." << PRINT_CONTEXT << endl;
#endif
			return;
		}
	}

	if (action == 'v') {
		cerr << "Cannot find a lock at address " << showbase << hex << ptr << noshowbase << PRINT_CONTEXT << endl;
		return;
	}

	// Insert virgin lock into map, and write entry to file
	pair<map<unsigned long long,Lock>::iterator,bool> retLock =
		lockPrimKey.insert(pair<unsigned long long,Lock>(ptr,Lock()));
	if (!retLock.second) {
		cerr << "Cannot insert lock into map: " << showbase << hex << ptr << noshowbase << PRINT_CONTEXT << endl;
	}
	Lock& tempLock = retLock.first->second;
	tempLock.ptr = ptr;
	tempLock.held = 1;
	tempLock.id = curLockID++;
	tempLock.allocation_id = allocation_id;
	tempLock.lockType = lockType;
	tempLock.lastNPos.push(LockPos());
	LockPos& tempLockPos = tempLock.lastNPos.top();
	tempLockPos.start = ts;
	tempLockPos.lastLine = line;
	tempLockPos.lastFile = file;
	tempLockPos.lastFn = fn;
	tempLockPos.lastPreemptCount = preemptCount;

	locksOFile << dec << tempLock.id << delimiter << tempLock.ptr;
	locksOFile << delimiter << sql_null_if(tempLock.allocation_id, tempLock.allocation_id == 0) << delimiter << tempLock.lockType << "\n";

	// a P() suspends the current TXN and creates a new one
	startTXN(ts, ptr);
}

/* taken from Linux 4.10 include/linux/preempt.h */
#define PREEMPT_BITS	8
#define SOFTIRQ_BITS	8
#define HARDIRQ_BITS	4
#define NMI_BITS		1

#define PREEMPT_SHIFT	0
#define SOFTIRQ_SHIFT	(PREEMPT_SHIFT + PREEMPT_BITS)
#define HARDIRQ_SHIFT	(SOFTIRQ_SHIFT + SOFTIRQ_BITS)
#define NMI_SHIFT		(HARDIRQ_SHIFT + HARDIRQ_BITS)

#define __IRQ_MASK(x)	((1UL << (x))-1)

#define PREEMPT_MASK	(__IRQ_MASK(PREEMPT_BITS) << PREEMPT_SHIFT)
#define SOFTIRQ_MASK	(__IRQ_MASK(SOFTIRQ_BITS) << SOFTIRQ_SHIFT)
#define HARDIRQ_MASK	(__IRQ_MASK(HARDIRQ_BITS) << HARDIRQ_SHIFT)

static void handlePreemptCountChange(
	bool enabled, unsigned long long prev, unsigned long long cur,
	unsigned long long ts,
	string const& file,
	unsigned long long line,
	string const& fn,
	string const& typeStr,
	string const& lockType,
	unsigned long long preemptCount,
	bool includeAllLocks,
	unsigned long long pseudoAllocID,
	ofstream& locksOFile,
	ofstream& txnsOFile,
	ofstream& locksHeldOFile
	)
{
	if (!enabled) {
		return;
	}

	bool
		prev_softirq = !!(prev & SOFTIRQ_MASK),
		 cur_softirq = !!( cur & SOFTIRQ_MASK),
		prev_hardirq = !!(prev & HARDIRQ_MASK),
		 cur_hardirq = !!( cur & HARDIRQ_MASK);

	if (!prev_softirq && cur_softirq) {
		/* P(softirq_pseudo_lock) */
		handlePV('p', ts, PSEUDOLOCK_ADDR_SOFTIRQ, file, line, fn, "static", "softirq",
			preemptCount, includeAllLocks, pseudoAllocID, locksOFile, txnsOFile, locksHeldOFile);
	} else if (prev_softirq && !cur_softirq) {
		/* V(softirq_pseudo_lock) */
		handlePV('v', ts, PSEUDOLOCK_ADDR_SOFTIRQ, file, line, fn, "static", "softirq",
			preemptCount, includeAllLocks, pseudoAllocID, locksOFile, txnsOFile, locksHeldOFile);
	}

	if (!prev_hardirq && cur_hardirq) {
		/* P(hardirq_pseudo_lock) */
		handlePV('p', ts, PSEUDOLOCK_ADDR_HARDIRQ, file, line, fn, "static", "hardirq",
			preemptCount, includeAllLocks, pseudoAllocID, locksOFile, txnsOFile, locksHeldOFile);
	} else if (prev_hardirq && !cur_hardirq) {
		/* V(hardirq_pseudo_lock) */
		handlePV('v', ts, PSEUDOLOCK_ADDR_HARDIRQ, file, line, fn, "static", "hardirq",
			preemptCount, includeAllLocks, pseudoAllocID, locksOFile, txnsOFile, locksHeldOFile);
	}
}

static void writeMemAccesses(char pAction, unsigned long long pAddress, ofstream *pMemAccessOFile, vector<MemAccess> *pMemAccesses) {
	vector<MemAccess>::iterator itAccess;
	MemAccess window[LOOK_BEHIND_WINDOW];
	int size;

	// Since we want to build up a history of the n last memory accesses, we do nothing if a r or w event is imminent.
	if (pAction == 'r' || pAction == 'w') {
		return;
	}

	if ((pAction == 'p' || pAction == 'v') &&
	    pMemAccesses->size() >= LOOK_BEHIND_WINDOW) {
		size = pMemAccesses->size();
		// Have a look at the two last events
		window[0] = pMemAccesses->at(size - 2);
		window[1] = pMemAccesses->at(size - 1);
		// If they have the same timestamp, access the same address, access the same amount of memory, and one is a read and the other event is a write,
		// they'll probably belong to the upcoming acquire or release events.
		// To increase the certainty that both events belong to the lock operation, the read/write address is compared to the address of the lock.
		// As long as the Linux Kernel developer do *not* change the layout of spinlock_t, this step will work.
		// That means they have to be discarded. Otherwise, the dataset will be polluted, and the following steps might produce wrong results.
		if (window[0].ts == window[1].ts &&
		    window[0].action == 'r' &&
		    window[1].action == 'w' &&
		    window[0].address == window[1].address &&
		    window[0].address == pAddress &&
		    window[0].size == window[1].size) {
#ifdef VERBOSE
			cout << "Discarding event r+w " << dec << window[0].ts << " reading and writing " << window[0].size << " bytes at address " << showbase << hex << window[0].address << noshowbase << endl;
#endif
			// Remove the two last memory accesses from the list, because they belong to the upcoming acquire or release on a spinlock.
			// We do *not* want them to be in our dataset.
			pMemAccesses->pop_back();
			pMemAccesses->pop_back();
		}
	}

	// If this is a P() or V(), the current TXN will be terminated after
	// returning from this function.  This means we need to write out all seen
	// memory accesses, and associate them with the current TXN.
	//
	// If this is an Alloc or Free, we also need to write out all memory
	// accesses to prevent them from being discarded later by the above
	// heuristic.

	// write memory accesses to disk and associate them with the current TXN
	unsigned accessCount = 0;
	for (auto&& tempAccess : *pMemAccesses) {
		*pMemAccessOFile << dec << tempAccess.id << delimiter << tempAccess.alloc_id;
		*pMemAccessOFile << delimiter << (activeTXNs.empty() ? "\\N" : std::to_string(activeTXNs.back().id));
		*pMemAccessOFile << delimiter << tempAccess.ts;
		*pMemAccessOFile << delimiter << tempAccess.action << delimiter << dec << tempAccess.size;
		*pMemAccessOFile << delimiter << tempAccess.address << delimiter << tempAccess.stacktrace_id << delimiter << tempAccess.instrPtr;
		*pMemAccessOFile << delimiter << sql_null_if(tempAccess.preemptCount,tempAccess.preemptCount == (unsigned long long)-1);
		*pMemAccessOFile << delimiter << get_function_at_addr(cus, tempAccess.instrPtr) << "\n";
		++accessCount;
	}

	// count memory accesses for the current TXN if there's one active
	if (!activeTXNs.empty()) {
		activeTXNs.back().memAccessCounter += accessCount;
	}

	// We'll record the TXN and which locks were held while it ran when it
	// finishes (with the final V()).

	// Disabled flush of output files for performance reasons
//	pMemAccessOFile->flush();
	pMemAccesses->clear();
}

static bool expand_type(const char *struct_typename)
{
	return find_if(types.cbegin(), types.cend(),
		[&struct_typename](const DataType& type) { return type.name == struct_typename; } )
		!= types.cend();
}

static unsigned long long addMemberName(const char *member_name) {
	unsigned long long ret;

	// Do we know that member name?
	const auto it = find_if(memberNames.cbegin(), memberNames.cend(),
		[&member_name](const pair<string, unsigned long long> &value ) { return value.first == member_name; } );
	if (it == memberNames.cend()) {
		ret = curMemberNameID++;
		memberNames.emplace(member_name,ret);
	} else {
		ret = it->second;
	}
	return ret;
}

static unsigned long long addStacktrace(std::string stacktrace) {
	unsigned long long ret;

	// Remove the last character since it always is a comma.
	stacktrace.pop_back();
	// Do we know that member name?
	const auto it = find_if(stacktraces.cbegin(), stacktraces.cend(),
		[&stacktrace](const pair<string, unsigned long long> &value ) { return value.first == stacktrace; } );
	if (it == stacktraces.cend()) {
		ret = curStacktraceID++;
		stacktraces.emplace(stacktrace,ret);
	} else {
		ret = it->second;
	}
	return ret;
}

static int convert_cus_iterator(struct cu *cu, void *cookie) {
	uint16_t class_id;
	struct tag *ret;
	CusIterArgs *cusIterArgs = (CusIterArgs*)cookie;
	struct dwarves_convert_ext dwarvesconfig = { 0 }; // initializes all members

	// Setup callback
	dwarvesconfig.expand_type = expand_type;
	dwarvesconfig.add_member_name = addMemberName;

	for (auto& type : *cusIterArgs->types) {
		// Skip known datatypes
		if (type.foundInDw) {
			continue;
		}
		// Does this compilation unit contain information on this type?
		ret = cu__find_struct_by_name(cu, type.name.c_str(), 0, &class_id);
		if (ret == NULL) {
			continue;
		}

		// Is it really a class or a struct?
		if (ret->tag == DW_TAG_class_type ||
			ret->tag == DW_TAG_interface_type ||
			ret->tag == DW_TAG_structure_type) {

			dwarvesconfig.type_id = type.id;
			if (class__fprintf(ret, cu, cusIterArgs->fp, &dwarvesconfig)) {
				type.foundInDw = true;
			}
		} else {
			cerr << "Internal error: Found struct for " << type.name << " that is no struct but tag ID " << ret->tag << endl;
		}
	}

	// If at least the information about one datatype is still missing, continue iterating through the cus.
	for (const auto& type : *cusIterArgs->types) {
		if (!type.foundInDw) {
			return 0;
		}
	}

	// No need to proceed with the remaining compilation units. Stop iteration.
	return 1;
}

// find .bss and .data sections
static int readSections(const char *filename,
	uint64_t& bssStart, uint64_t& bssSize, uint64_t& dataStart, uint64_t& dataSize) {
	asection *bsSection, *dataSection;
	bfd *kernelBfd;

	bfd_init();

	kernelBfd = bfd_openr(filename,"elf32-i386");
	if (kernelBfd == NULL) {
		bfd_perror("open vmlinux");
		return -1;
	}
	// This check is not only a sanity check. Moreover, it is necessary
	// to allow looking up of sections.
	if (!bfd_check_format (kernelBfd, bfd_object)) {
		cerr << "bfd: unknown format" << endl;
		bfd_close(kernelBfd);
		return -1;
	}

	bsSection = bfd_get_section_by_name(kernelBfd,".bss");
	if (bsSection == NULL) {
		bfd_perror("Cannot find section '.bss'");
		bfd_close(kernelBfd);
		return -1;
	}
	bssStart = bfd_section_vma(kernelBfd, bsSection);
	bssSize = bfd_section_size(kernelBfd, bsSection);
	cout << bfd_section_name(kernelBFD,bsSection) << ": " << bssSize << " bytes @ " << hex << showbase << bssStart << dec << noshowbase << endl;

	dataSection = bfd_get_section_by_name(kernelBfd,".data");
	if (bsSection == NULL) {
		bfd_perror("Cannot find section '.bss'");
		bfd_close(kernelBfd);
		return -1;
	}
	dataStart = bfd_section_vma(kernelBfd, dataSection);
	dataSize = bfd_section_size(kernelBfd,dataSection);
	cout << bfd_section_name(kernelBFD,dataSection) << ": " << dataSize << " bytes @ " << hex << showbase << dataStart << dec << noshowbase << endl;

	bfd_close(kernelBfd);

	return 0;
}

static int extractStructDefs(struct cus *cus, const char *filename) {
	CusIterArgs cusIterArgs;
	struct conf_load confLoad;
	FILE *structsLayoutOFile;

	memset(&confLoad, 0, sizeof(confLoad));
	confLoad.get_addr_info = true;

	// Load the dwarf information of every compilation unit
	if (cus__load_file(cus, &confLoad, filename) != 0) {
		cerr << "No debug information found in " << filename << endl;
		return -1;
	}

	// Open the output file and add the header
	structsLayoutOFile = fopen("structs_layout.csv", "w+");
	if (structsLayoutOFile == NULL) {
		perror("fopen structs_layout.csv");
		cus__delete(cus);
		dwarves__exit();
		return -1;
	}
	fprintf(structsLayoutOFile,
		"type_id%ctype%cmember%coffset%csize\n",delimiter,delimiter,delimiter,delimiter);

	// Pass the context information to the callback: types array and the outputfile
	cusIterArgs.types = &types;
	cusIterArgs.fp = structsLayoutOFile;
	// Iterate through every compilation unit, and look for information about the datatypes of interest
	cus__for_each_cu(cus, convert_cus_iterator, &cusIterArgs, NULL);

	fclose(structsLayoutOFile);

	return 0;
}

static int isGZIPFile(const char *filename) {
	int fd, bytes;
	unsigned char buffer[2];

	fd = open(filename,O_RDONLY);
	if (fd < 0) {
		perror("isGZIPFile()->open");
		return -1;
	}
	bytes = read(fd,buffer,2);
	if (bytes < 0) {
		perror("isGZIPFile()->read");
		return -1;
	} else if (bytes != 2) {
		return -1;
	}
	close(fd);
	if (buffer[0] == 0x1f && buffer[1] == 0x8b) {
		return 1;
	} else {
		return 0;
	}
}

int main(int argc, char *argv[]) {
	stringstream ss;
	string inputLine, token, typeStr, file, fn, lockType, stacktrace;
	vector<string> lineElems; // input CSV columns
	map<unsigned long long,Allocation>::iterator itAlloc;
	map<unsigned long long,Lock>::iterator itLock, itTemp;
	unsigned long long ts = 0, ptr = 0x1337, size = 4711, line = 1337, address = 0x4711, instrPtr = 0xc0ffee, preemptCount = 0xaa;
	unsigned long long prevPreemptCount = 0, curPreemptCount = 0;
	int lineCounter, isGZ;
	char action = '.', param, *vmlinuxName = NULL, *fnBlacklistName = nullptr, *memberBlacklistName = nullptr, *datatypesName = nullptr;
	bool processSeqlock = false, includeAllLocks = false, processPreemptCount = false;
	unsigned long long pseudoAllocID = 0; // allocID for locks belonging to unknown allocation

	while ((param = getopt(argc,argv,"k:b:m:t:svhd:up")) != -1) {
		switch (param) {
		case 'k':
			vmlinuxName = optarg;
			break;
		case 'b':
			fnBlacklistName = optarg;
			break;
		case 't':
			datatypesName = optarg;
			break;
		case 's':
			processSeqlock = true;
			break;
		case 'u':
			includeAllLocks = true;
			break;
		case 'p':
			processPreemptCount = true;
			break;
		case 'm':
			memberBlacklistName = optarg;
			break;
		case 'v':
			printVersion();
			return EXIT_SUCCESS;
		case 'h':
			printUsageAndExit(argv[0]);
		case 'd':
			delimiter = *optarg;
			break;
		}
	}
	if (!vmlinuxName || !fnBlacklistName || ! memberBlacklistName || !datatypesName || optind == argc) {
		printUsageAndExit(argv[0]);
	}

	printVersion();
	if (processSeqlock) {
		cerr << "Enabled experimental feature 'processing of seq{lock,count}_t'" << endl;
	}
	cerr << "Using delimiter: " << delimiter << endl;

	// Load data types
	ifstream datatypesinfile(datatypesName);
	if (!datatypesinfile.is_open()) {
		cerr << "Cannot open file: " << datatypesName << endl;
		return EXIT_FAILURE;
	}

	for (lineCounter = 0; getline(datatypesinfile, inputLine); lineCounter++) {
		// Skip CSV header
		if (lineCounter == 0) {
			continue;
		}
		types.emplace_back(curTypeID++, inputLine);
	}

	// Examine Linux-kernel ELF: retrieve BSS + data segment locations
	if (readSections(vmlinuxName, bssStart, bssSize, dataStart, dataSize)) {
		return EXIT_FAILURE;
	}

	if (bssStart == 0 || bssSize == 0 || dataStart == 0 || dataSize == 0 ) {
		cerr << "Invalid values for bss start, bss size, data start or data size!" << endl;
		printUsageAndExit(argv[0]);
	}

	// Examine Linux-kernel ELF: retrieve struct definitions
	dwarves__init(0);
	cus = cus__new();
	if (cus == NULL) {
		cerr << "Insufficient memory" << endl;
		return EXIT_FAILURE;
	}

	if (extractStructDefs(cus, vmlinuxName)) {
		return EXIT_FAILURE;
	}

	// This is very bad design practise!
	// Only the fstream does have a close() method.
	// Since the gzstream is a direct subclass of iostream, a ptr of that type
	// cannot be stored in one common ptr variable without losing the
	// ability to call close().
	istream *infile;
	igzstream *gzinfile = NULL;
	ifstream *rawinfile = NULL;
	char *fname = argv[optind];
	isGZ = isGZIPFile(fname);

	if (isGZ == 1) {
		gzinfile = new igzstream(fname);
		if (!gzinfile->is_open()) {
			cerr << "Cannot open file: " << fname << endl;
			return EXIT_FAILURE;
		}
		infile = gzinfile;
	} else if (isGZ == 0) {
		rawinfile = new ifstream(fname);
		if (!rawinfile->is_open()) {
			cerr << "Cannot open file: " << fname << endl;
			return EXIT_FAILURE;
		}
		infile = rawinfile;
	} else {
		cerr << "Cannot read inputfile: " << fname << endl;
		return EXIT_FAILURE;
	}

	ifstream fnBlacklistInfile(fnBlacklistName);
	if (!fnBlacklistInfile.is_open()) {
		cerr << "Cannot open file: " << fnBlacklistName << endl;
		return EXIT_FAILURE;
	}
	ifstream memberBlacklistInfile(memberBlacklistName);
	if (!memberBlacklistInfile.is_open()) {
		cerr << "Cannot open file: " << memberBlacklistName << endl;
		return EXIT_FAILURE;
	}

	// Create the outputfiles. One for each table.
	ofstream datatypesOFile("data_types.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream allocOFile("allocations.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream accessOFile("accesses.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream locksOFile("locks.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream locksHeldOFile("locks_held.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream txnsOFile("txns.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream fnblacklistOFile("function_blacklist.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream memberblacklistOFile("member_blacklist.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream membernamesOFile("member_names.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream stacktracesOFile("stacktraces.csv",std::ofstream::out | std::ofstream::trunc);

	// CSV headers
	datatypesOFile << "id" << delimiter << "name" << endl;

	allocOFile << "id" << delimiter << "type_id" << delimiter << "ptr" << delimiter;
	allocOFile << "size" << delimiter << "start" << delimiter << "end" << endl;

	accessOFile << "id" << delimiter << "alloc_id" << delimiter << "txn_id" << delimiter;
	accessOFile << "ts" << delimiter;
	accessOFile << "type" << delimiter << "size" << delimiter << "address" << delimiter;
	accessOFile << "stacktrace_id" << delimiter << "instrptr" << delimiter << "preemptcount" << delimiter << "fn" << endl;

	locksOFile << "id" << delimiter << "ptr" << delimiter;
	locksOFile << "embedded" << delimiter << "locktype" << endl;

	locksHeldOFile << "txn_id" << delimiter << "lock_id" << delimiter;
	locksHeldOFile << "start" << delimiter;
	locksHeldOFile << "lastFile" << delimiter << "lastLine" << delimiter << "lastFn" << delimiter;
	locksHeldOFile << "lastPreemptCount" << endl;

	txnsOFile << "id" << delimiter << "start" << delimiter << "end" << endl;

	fnblacklistOFile << "datatype_id" << delimiter << "datatype_member"
		<< delimiter << "fn" << endl;

	memberblacklistOFile << "datatype_id" << delimiter << "datatype_member_id" << endl;
		
	membernamesOFile << "id" << delimiter << "member_name" << endl;
	
	stacktracesOFile << "id" << delimiter << "stacktrace" << endl;

	for (const auto& type : types) {
		datatypesOFile << type.id << delimiter << type.name << endl;
	}
	
	for (const auto& memberName : memberNames) {
		membernamesOFile << memberName.second << delimiter << memberName.first << endl;
	}

	if (includeAllLocks) {
		// create pseudo alloc for locks we don't know the alloc they belong to
		pseudoAllocID = curAllocID++;
		allocOFile << pseudoAllocID << delimiter << 0 << delimiter << 0 << delimiter;
		allocOFile << 0 << delimiter << 0 << delimiter << "\\N" << "\n";
	}

	// Start reading the inputfile
	for (lineCounter = 0;
		getline(*infile,inputLine);
		ss.clear(), ss.str(""), lineElems.clear(), lineCounter++) {
#ifdef DEBUG_DATASTRUCTURE_GROWTH
		if ((lineCounter % 100000) == 0) {
			cerr << lockPrimKey.size() << " "
				<< activeAllocs.size() << " "
				<< lastMemAccesses.size() << " "
				<< functionAddresses.size() << " "
				<< lineElems.size() << " "
				<< ss.str().size() << std::endl;
		}
#endif
		// Skip the header if there is one.  This check exploits the fact that
		// any valid input line must start with a decimal digit.
		if (lineCounter == 0) {
			if (inputLine.length() == 0 || !isdigit(inputLine[0])) {
				continue;
			} else {
				cerr << "Warning: Input data does not start with a CSV header." << endl;
			}
		}

		ss << inputLine;
		// Tokenize each line by delimiter, and store each element in a vector
		while (getline(ss,token,delimiter)) {
			lineElems.push_back(token);
		}

		// Parse each element
		ts = std::stoull(lineElems.at(0));
		/*if (lineElems.size() != MAX_COLUMNS) {
			cerr << "Line (ts=" << ts << ") contains " << lineElems.size() << " elements. Expected " << MAX_COLUMNS << "." << endl;
			return EXIT_FAILURE;
		}*/
		ptr = 0x1337, size = 4711, line = 1337, address = 0x4711, instrPtr = 0xc0ffee, preemptCount = 0xaa;
		lockType = file = stacktrace = fn = "empty";
		try {
			action = lineElems.at(1).at(0);
			switch (action) {
			case 'a':
			case 'f':
				{
					typeStr = lineElems.at(2);
					ptr = std::stoull(lineElems.at(3),NULL,16);
					size = std::stoull(lineElems.at(4));
					break;
				}
			case 'p':
			case 'v':
				{
					typeStr = lineElems.at(2);
					ptr = std::stoull(lineElems.at(3),NULL,16);
					file = lineElems.at(5);
					line = std::stoull(lineElems.at(6));
					fn = lineElems.at(7);
					lockType = lineElems.at(8);
					prevPreemptCount = curPreemptCount;
					curPreemptCount =
						preemptCount = std::stoull(lineElems.at(9),NULL,16);
					handlePreemptCountChange(
						processPreemptCount, prevPreemptCount, curPreemptCount,
						ts, file, line, fn, typeStr, lockType, preemptCount, includeAllLocks, pseudoAllocID,
						locksOFile, txnsOFile,locksHeldOFile);
					break;
				}
			case 'r':
			case 'w':
				{
					ptr = std::stoull(lineElems.at(2),NULL,16);
					size = std::stoull(lineElems.at(3));
					address = std::stoull(lineElems.at(4),NULL,16);
					instrPtr = std::stoull(lineElems.at(5),NULL,16);
					stacktrace = lineElems.at(6);
					if (lineElems.at(7).compare("NULL") != 0) {
						prevPreemptCount = curPreemptCount;
						curPreemptCount =
							preemptCount = std::stoull(lineElems.at(7),NULL,16);
						handlePreemptCountChange(
							processPreemptCount, prevPreemptCount, curPreemptCount,
							ts, file, line, fn, typeStr, lockType, preemptCount, includeAllLocks, pseudoAllocID,
							locksOFile, txnsOFile,locksHeldOFile);
					} else {
						preemptCount = -1;
					}
					break;
				}
			}
		} catch (exception &e) {
			cerr << "Exception occurred (ts="<< ts << "): " << e.what() << endl;
		}

		if (!processSeqlock && (lockType.compare("seqlock_t") == 0 || lockType.compare("seqcount_t") == 0)) {
			continue;
		}

		writeMemAccesses(action, address, &accessOFile, &lastMemAccesses);
		switch (action) {
		case 'a':
				{
				if (activeAllocs.find(ptr) != activeAllocs.end()) {
					cerr << "Found active allocation at address " << showbase << hex << ptr << noshowbase << PRINT_CONTEXT << endl;
					continue;
				}
				// Do we know that datatype?
				const auto it = find_if(types.cbegin(), types.cend(),
					[&typeStr](const DataType& type) { return type.name == typeStr; } );
				if (it == types.cend()) {
					cerr << "Found unknown datatype: " << typeStr << endl;
					continue;
				}
				int datatype_idx = it - types.cbegin();
				// Remember that allocation
				pair<map<unsigned long long,Allocation>::iterator,bool> retAlloc =
					activeAllocs.insert(pair<unsigned long long,Allocation>(ptr,Allocation()));
				if (!retAlloc.second) {
					cerr << "Cannot insert allocation into map: " << showbase << hex << ptr << noshowbase << PRINT_CONTEXT << endl;
				}
				Allocation& tempAlloc = retAlloc.first->second;
				tempAlloc.id = curAllocID++;
				tempAlloc.start = ts;
				tempAlloc.idx = datatype_idx;
				tempAlloc.size = size;
#ifdef VERBOSE
				cerr << "Added allocation at " << showbase << hex << ptr << noshowbase << dec << ", Type:" << typeStr << ", Size:" << size << endl;
#endif
				break;
				}
		case 'f':
				{
				itAlloc = activeAllocs.find(ptr);
				if (itAlloc == activeAllocs.end()) {
					cerr << "Didn't find active allocation for address " << showbase << hex << ptr << noshowbase << PRINT_CONTEXT << endl;
					continue;
				}
				Allocation& tempAlloc = itAlloc->second;
				// An allocations datatype is
				allocOFile << tempAlloc.id << delimiter << tempAlloc.idx + 1 << delimiter << ptr << delimiter << dec << size << delimiter << dec << tempAlloc.start << delimiter << ts << "\n";
				// Iterate through the set of locks, and delete any lock that resided in the freed memory area
				for (itLock = lockPrimKey.begin(); itLock != lockPrimKey.end();) {
					if (itLock->second.ptr >= itAlloc->first && itLock->second.ptr < (itAlloc->first + tempAlloc.size)) {
						// Lock should not be held anymore
						if (itLock->second.held > 0) {
							cerr << "Lock at address " << showbase << hex << itLock->second.ptr << noshowbase << " is being freed but held = " << itLock->second.held << "! " << PRINT_CONTEXT << endl;
						}
						// Since the iterator will be invalid as soon as we delete the element, we have to advance the iterator to the next element, and remember the current one.
						itTemp = itLock;
						itLock++;
						lockPrimKey.erase(itTemp);
					} else {
						itLock++;
					}
				}

				activeAllocs.erase(itAlloc);
#ifdef VERBOSE
				cerr << "Removed allocation at " << showbase << hex << ptr << noshowbase << dec << ", Type:" << typeStr << ", Size:" << size << endl;
#endif
				break;
				}
		case 'v':
		case 'p':
			handlePV(action, ts, ptr, file, line, fn, typeStr, lockType,
				preemptCount, includeAllLocks, pseudoAllocID, locksOFile, txnsOFile, locksHeldOFile);
			break;
		case 'w':
		case 'r':
				{
				itAlloc = activeAllocs.find(ptr);
				if (itAlloc == activeAllocs.end()) {
					cerr << "Didn't find active allocation for address " << showbase << hex << ptr << noshowbase << PRINT_CONTEXT << endl;
					continue;
				}
				// sanity check
				if (ptr + itAlloc->second.size < address || address + size < ptr) {
					cerr << "Memory-access address " << showbase << hex << address << " does not belong to indicated allocation " << ptr << noshowbase << PRINT_CONTEXT << endl;
					return EXIT_FAILURE;
				}

				lastMemAccesses.push_back(MemAccess());
				MemAccess& tempAccess = lastMemAccesses.back();
				tempAccess.id = curAccessID++;
				tempAccess.ts = ts;
				tempAccess.alloc_id = itAlloc->second.id;
				tempAccess.action = action;
				tempAccess.size = size;
				tempAccess.address = address;
				tempAccess.stacktrace_id = addStacktrace(stacktrace);
				tempAccess.instrPtr = instrPtr;
				tempAccess.preemptCount = preemptCount;
				break;
				}
		}
	}

	// Due to the fact that we abort the experiment as soon as the benchmark has finished, some allocations may not have been freed.
	// Hence, print every allocation, which is still stored in the map, and set the freed timestamp to NULL.
	for (itAlloc = activeAllocs.begin(); itAlloc != activeAllocs.end(); itAlloc++) {
		Allocation& tempAlloc = itAlloc->second;
		allocOFile << tempAlloc.id << delimiter << types[tempAlloc.idx].id << delimiter << itAlloc->first << delimiter;
		allocOFile << dec << tempAlloc.size << delimiter << dec << tempAlloc.start << delimiter << "\\N" << "\n";
	}

	// Flush memory writes by pretending there's a final V()
	writeMemAccesses('v', 0, &accessOFile, &lastMemAccesses);

	// Flush TXNs if there are still open ones
	while (!activeTXNs.empty()) {
		cerr << "TXN: There are still " << activeTXNs.size() << " TXNs active, flushing the topmost one." << endl;
		// pretend there's a V() matching the top-most TXN's starting (P())
		// lock at the last seen timestamp
		finishTXN(ts, activeTXNs.back().lockPtr, txnsOFile, locksHeldOFile);
	}

	if (isGZ) {
		delete gzinfile;
	} else {
		delete rawinfile;
	}

	cus__delete(cus);
	dwarves__exit();

	// Helper: type -> ID mapping
	std::map<std::string, decltype(DataType::id)> type2id;
	for (const auto& type : types) {
		type2id[type.name] = type.id;
	}

	// Process function blacklist
	for (lineCounter = 0;
		getline(fnBlacklistInfile, inputLine);
		ss.clear(), ss.str(""), lineElems.clear(), lineCounter++) {

		// Skip the CSV header
		if (lineCounter == 0) {
			continue;
		}

		ss << inputLine;
		// Tokenize each line
		while (getline(ss, token, DELIMITER_BLACKLISTS)) {
			lineElems.push_back(token);
		}

		// Sanity check
		if (lineElems.size() != 3) {
			cerr << "Ignoring invalid blacklist (function) entry in line " << (lineCounter + 1)
				<< ": " << inputLine << endl;
			continue;
		}

		auto itType = type2id.find(lineElems.at(0));
		if (itType == type2id.end()) {
			cerr << "Unknown type in blacklist (function) line " << (lineCounter + 1)
				<< ": " << lineElems.at(0) << endl;
			continue;
		}
		
		string memberID;
		if (lineElems.at(1) != "\\N") {
			auto itMember = memberNames.find(lineElems.at(1));
			if (itMember == memberNames.end()) {
				cerr << "Unknown member name in blacklist (function) line " << (lineCounter + 1)
					<< ": " << lineElems.at(1) << endl;
				continue;
			}
			memberID = std::to_string(itMember->second);
		} else {
			memberID = lineElems.at(1);
		}

		fnblacklistOFile << itType->second << delimiter
			<< memberID << delimiter
			<< lineElems.at(2) << endl;
	}

	// Process member blacklist
	for (lineCounter = 0;
		getline(memberBlacklistInfile, inputLine);
		ss.clear(), ss.str(""), lineElems.clear(), lineCounter++) {

		// Skip the CSV header
		if (lineCounter == 0) {
			continue;
		}

		ss << inputLine;
		// Tokenize each line
		while (getline(ss, token, DELIMITER_BLACKLISTS)) {
			lineElems.push_back(token);
		}

		// Sanity check
		if (lineElems.size() != 2) {
			cerr << "Ignoring invalid blacklist (member) entry in line " << (lineCounter + 1)
				<< ": " << inputLine << endl;
			continue;
		}

		auto itType = type2id.find(lineElems.at(0));
		if (itType == type2id.end()) {
			cerr << "Unknown type in blacklist (member) line " << (lineCounter + 1)
				<< ": " << lineElems.at(0) << endl;
			continue;
		}

		auto itMember = memberNames.find(lineElems.at(1));
		if (itMember == memberNames.end()) {
			cerr << "Unknown member in blacklist (member) line " << (lineCounter + 1)
				<< ": " << lineElems.at(1) << endl;
			continue;
		}

		memberblacklistOFile << itType->second << delimiter
			<< itMember->second << endl;
	}

	for (const auto& stacktrace : stacktraces) {
		stacktracesOFile << stacktrace.second << delimiter << stacktrace.first << endl;
	}
	
	cerr << "Finished." << endl;

	return EXIT_SUCCESS;
}
