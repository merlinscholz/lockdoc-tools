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
#include "log_event.h"
#include "git_version.h"
#include "rwlock.h"
#include "rlock.h"
#include "wlock.h"

#include "binaryread.h"
#include "gzstream/gzstream.h"

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
	unsigned long long preemptCount;								// __preempt_count
};

/**
 * The kernel source tree
 */
static const char *kernelBaseDir = "/opt/kernel/linux-32-lockdebugging-4-10/";

/**
 * Contains all known locks. The ptr of a lock is used as an index.
 */
static map<unsigned long long,RWLock*> lockPrimKey;
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
 * The key is the first instrptr of a stacktrace, and the value is a map.
 * That map in turn maps the remaining stacktrace to a stacktrace's global id.
 */
static map<unsigned long long, map<string,unsigned long long> > stacktraces;

/**
 * Start address and size of the bss and data section. All information is read from the dwarf information during startup.
 */
static uint64_t bssStart = 0, bssSize = 0, dataStart = 0, dataSize = 0;

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
		" -g  The kernel source tree, default: " << kernelBaseDir << "\n"
		" -h  help\n";
	exit(EXIT_FAILURE);
}

static void printVersion()
{
	cerr << "convert version: " << GIT_BRANCH << ", " << GIT_MESSAGE << endl;
}

void startTXN(unsigned long long ts, unsigned long long lockPtr, enum SUB_LOCK subLock)
{
	activeTXNs.push_back(TXN());
	activeTXNs.back().id = curTXNID++;
	activeTXNs.back().start = ts;
	activeTXNs.back().memAccessCounter = 0;
	activeTXNs.back().lockPtr = lockPtr;
	activeTXNs.back().subLock = subLock;
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

/**
 * Releases a lock, and finishes the corresponding TXN.
 *
 * @param ts             Current timestamp
 * @param lockPtr        Lock to be released
 * @param txnsOFile      ofstream for txns.csv
 * @param locksHeldOFile ofstream for locks_held.csv
 */
bool finishTXN(unsigned long long ts, unsigned long long lockPtr, enum SUB_LOCK subLock, bool removeReader, std::ofstream& txnsOFile, std::ofstream& locksHeldOFile)
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
			std::set<decltype(RWLock::read_id)> locks_seen;
			for (auto thisTXN : activeTXNs) {
				RWLock *tempLock = lockPrimKey[thisTXN.lockPtr];
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
					locksHeldOFile << tempLockPos.lastPreemptCount << delimiter << (tempLockPos.lastIRQSync + 1) << "\n";
					// Add one to tempLockPos.lastIRQSync, because MySQL enums start at 1. Zero has a special meaning.
					// https://dev.mysql.com/doc/refman/5.7/en/enum.html#enum-indexes
				} else {
					PRINT_ERROR(tempLock->toString(thisTXN.subLock) << ",ts=" << dec << ts, "TXN: Internal error, lock is part of the TXN hierarchy but not held?");
				}
			}
		}

		// are we done deconstructing the TXN stack?
		if (activeTXNs.back().lockPtr == lockPtr) {
			if (activeTXNs.back().subLock == subLock) {
				// We have deconstructed the TXN stack until the topmost
				// TXN belongs to the lock for which we have seen a V().
				activeTXNs.pop_back();
				found = true;
				// But still, the TXN stack may contain TXNs belonging to lockPtr.
				if (removeReader) {
					// The caller wants to remove all READER_LOCKs from the TXN stack.
					for (std::deque<TXN>::iterator it = activeTXNs.begin(); it != activeTXNs.end();) {
						if (it->lockPtr != lockPtr) {
							it++;
							continue;
						}
						RWLock *tempLock;
						// Does subLock and lockPtr match?
						if (it->subLock == READER_LOCK) {
							tempLock = lockPrimKey[it->lockPtr];
							PRINT_DEBUG(tempLock->toString(it->subLock) << ",ts=" << dec << ts << ",txn=" << it->id, "Flushing TXN");
							it = activeTXNs.erase(it);
						} else {
							it++;
							tempLock = lockPrimKey[it->lockPtr];
							PRINT_ERROR(tempLock->toString(it->subLock) << ",ts=" << dec << ts, "Multiple active txns for one writer lock");
						}
					}
				}
				break;
			} else {
				RWLock *tempLock = lockPrimKey[activeTXNs.back().lockPtr];
				PRINT_ERROR(tempLock->toString(activeTXNs.back().subLock) << ",ts=" << dec << ts, "sublock does not match");
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
		RWLock *tempLock = lockPrimKey[lockPtr];
		PRINT_ERROR(tempLock->toString(subLock) << ",ts=" << dec << ts, "TXN: Internal error -- V() but no matching TXN!");
	}

	// recreate TXNs
	std::move(restartTXNs.begin(), restartTXNs.end(), std::back_inserter(activeTXNs));
	return found;
}

/* handle P() / V() events */
static void handlePV(
	enum LOCK_OP lockOP,
	unsigned long long ts,
	unsigned long long lockAddress,
	string const& file,
	unsigned long long line,
	string const& fn,
	string const& lockMember,
	string const& lockType,
	unsigned long long preemptCount,
	enum IRQ_SYNC irqSync,
	unsigned flags,
	bool includeAllLocks,
	unsigned long long pseudoAllocID,
	ofstream& locksOFile,
	ofstream& txnsOFile,
	ofstream& locksHeldOFile,
	const char *kernelBaseDir
	)
{
	RWLock *tempLock;

	auto itLock = lockPrimKey.find(lockAddress);
	if (itLock != lockPrimKey.end()) {
		tempLock = itLock->second;
	} else {
		// categorize currently unknown lock
		unsigned allocation_id = 0;
		const char *lockVarName = NULL;
		// A lock which probably resides in one of the observed allocations. If not, check if it is a global lock
		// This way, locks which reside in global structs are recognized as 'embedded in'.
		auto itAlloc = activeAllocs.upper_bound(lockAddress);
		if (itAlloc != activeAllocs.begin()) {
			itAlloc--;
			if (lockAddress < itAlloc->first + itAlloc->second.size) {
				allocation_id = itAlloc->second.id;
			}
		}
		if (allocation_id == 0) {
			if ((lockAddress >= bssStart && lockAddress < bssStart + bssSize) ||
				(lockAddress >= dataStart && lockAddress < dataStart + dataSize) ||
				(lockMember.compare(PSEUDOLOCK_VAR) == 0 && RWLock::isPseudoLock(lockAddress))) {
				// static lock which resides either in the bss segment or in the data segment
				// or global static lock aka rcu lock
				PRINT_DEBUG("ts=" << dec << ts << ",lockAddress=" << hex << showbase << lockAddress, "Found static lock.");
				// Try to resolve what the name of this lock is
				// This also catches cases where the lock resides inside another data structure.
				if (lockMember.compare("static") != 0) {
					lockVarName = getGlobalLockVar(lockAddress);
				}
			} else if (includeAllLocks) {
				// non-static lock, but we don't known the allocation it belongs to
				PRINT_DEBUG("ts=" << dec << ts << ",lockAddress=" << hex << showbase << lockAddress, "Found non-static lock belonging to unknown allocation, assigning to pseudo allocation.");
				allocation_id = pseudoAllocID;
			} else {
				PRINT_DEBUG("ts=" << dec << ts << ",lockAddress=" << hex << showbase << lockAddress, "Lock does not belong to any of the observed memory regions. Ignoring it.");
				return;
			}
		}
		if (lockOP == V_READ || lockOP == V_WRITE) {
			PRINT_ERROR("ts=" << ts << ",lockAddress=" << hex << showbase << lockAddress << noshowbase << ",lockOP=" << dec << lockOP, "Cannot find a lock at given address.");
			return;
		}
		// Instantiate the corresponding class ...
		tempLock = RWLock::allocLock(lockAddress, allocation_id, lockType, lockVarName, flags);
		// ... , and assign ids to the sub locks
		tempLock->initIDs(curLockID);
		PRINT_DEBUG("", "Created lock: " << tempLock);
		// Store the lock in our global map
		pair<map<unsigned long long,RWLock*>::iterator,bool> retLock;
		retLock = lockPrimKey.insert(pair<unsigned long long,RWLock*>(lockAddress, tempLock));
		if (!retLock.second) {
			PRINT_ERROR("lockAddress=" << showbase << hex << lockAddress << noshowbase, "Cannot insert lock into map.");
			// This is a severe error. Abort immediately!
			exit(1);
		}
		// Write the lock to disk (aka locks.csv)
		tempLock->writeLock(locksOFile, delimiter);
	}
	tempLock->transition(lockOP, ts, file, line, fn, lockMember, preemptCount, irqSync, activeTXNs, txnsOFile, locksHeldOFile, kernelBaseDir);
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
	ofstream& locksHeldOFile,
	const char *kernelBaseDir
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
		handlePV(P_WRITE, ts, PSEUDOLOCK_ADDR_SOFTIRQ, file, line, fn, "static", "softirq",
			preemptCount, LOCK_NONE, 0, includeAllLocks, pseudoAllocID, locksOFile, txnsOFile, locksHeldOFile, kernelBaseDir);
	} else if (prev_softirq && !cur_softirq) {
		/* V(softirq_pseudo_lock) */
		handlePV(V_WRITE, ts, PSEUDOLOCK_ADDR_SOFTIRQ, file, line, fn, "static", "softirq",
			preemptCount, LOCK_NONE, 0, includeAllLocks, pseudoAllocID, locksOFile, txnsOFile, locksHeldOFile, kernelBaseDir);
	}

	if (!prev_hardirq && cur_hardirq) {
		/* P(hardirq_pseudo_lock) */
		handlePV(P_WRITE, ts, PSEUDOLOCK_ADDR_HARDIRQ, file, line, fn, "static", "hardirq",
			preemptCount, LOCK_NONE, 0, includeAllLocks, pseudoAllocID, locksOFile, txnsOFile, locksHeldOFile, kernelBaseDir);
	} else if (prev_hardirq && !cur_hardirq) {
		/* V(hardirq_pseudo_lock) */
		handlePV(V_WRITE, ts, PSEUDOLOCK_ADDR_HARDIRQ, file, line, fn, "static", "hardirq",
			preemptCount, LOCK_NONE, 0, includeAllLocks, pseudoAllocID, locksOFile, txnsOFile, locksHeldOFile, kernelBaseDir);
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
			PRINT_DEBUG("ts=" << dec << window[0].ts << ",type=" << pAction << ",address=" << hex << showbase << pAddress, "Discarding r+w event of size " << dec << window[0].size);
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
		*pMemAccessOFile << delimiter << tempAccess.address << delimiter << tempAccess.stacktrace_id;
		*pMemAccessOFile << delimiter << sql_null_if(tempAccess.preemptCount,tempAccess.preemptCount == (unsigned long long)-1) << "\n";
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

static unsigned long long addStacktrace(const char *kernelBaseDir, ostream &stacktracesOFile, char delimiter, unsigned long long instrPtr, std::string stacktrace) {
	unsigned long long ret;

	// Remove the last character since it always is a comma.
	stacktrace.pop_back();
	auto itStacktrace = stacktraces.emplace(instrPtr,map<std::string,unsigned long long>());
	auto &subStacktraces = itStacktrace.first->second;
	// Do we know that substacktrace?
	const auto itSubStacktrace = find_if(subStacktraces.cbegin(), subStacktraces.cend(),
		[&stacktrace](const pair<string, unsigned long long> &value ) { return value.first == stacktrace; } );
	if (itSubStacktrace == subStacktraces.cend()) {
		int sequence = 0;
		stringstream ss;
		ss << hex << showbase << instrPtr;
		std::string token = ss.str();
		ss.clear(); ss.str("");

		ret = curStacktraceID++;
		subStacktraces.emplace(stacktrace,ret);
		
		ss << stacktrace;
		do {
			instrPtr = std::stoull(token,NULL,16);
			const struct ResolvedInstructionPtr &resolvedInstrPtr = get_function_at_addr(kernelBaseDir, instrPtr);
			stacktracesOFile << ret << delimiter << sequence << delimiter << instrPtr << delimiter;
			stacktracesOFile << resolvedInstrPtr.codeLocation.fn << delimiter << resolvedInstrPtr.codeLocation.line << delimiter << resolvedInstrPtr.codeLocation.file << "\n";
			sequence++;
			if (resolvedInstrPtr.inlinedBy.size() > 0) {
				for (auto &inlinedFn : resolvedInstrPtr.inlinedBy) {
					stacktracesOFile << ret << delimiter << sequence << delimiter << instrPtr << delimiter;
					stacktracesOFile << inlinedFn.fn << delimiter << inlinedFn.line << delimiter << inlinedFn.file << "\n";
					sequence++;
				}
			}
		} while (getline(ss,token,','));
	} else {
		ret = itSubStacktrace->second;
	}
	return ret;
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
	string inputLine, token, typeStr, file, fn, lockType, stacktrace, lockMember;
	vector<string> lineElems; // input CSV columns
	map<unsigned long long,Allocation>::iterator itAlloc;
	map<unsigned long long,RWLock*>::iterator itLock, itTemp;
	unsigned long long ts = 0, address = 0x1337, size = 4711, line = 1337, baseAddress = 0x4711, instrPtr = 0xc0ffee, preemptCount = 0xaa, flags = 0x4712;
	unsigned long long prevPreemptCount = 0, curPreemptCount = 0;
	int lineCounter, isGZ;
	char action = '.', param, *vmlinuxName = NULL, *fnBlacklistName = nullptr, *memberBlacklistName = nullptr, *datatypesName = nullptr;
	bool processSeqlock = false, includeAllLocks = false, processPreemptCount = false;
	enum IRQ_SYNC irqSync = LOCK_NONE;
	enum LOCK_OP lockOP = P_WRITE;
	unsigned long long pseudoAllocID = 0; // allocID for locks belonging to unknown allocation

	while ((param = getopt(argc,argv,"k:b:m:t:svhd:upg:")) != -1) {
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
			cerr << "Processing of preempt count is currently broken!" << endl;
			return EXIT_FAILURE;
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
		case 'g':
			kernelBaseDir = optarg;
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

	if (binaryread_init(vmlinuxName)) {
		cerr << "Cannot init binaryread" << endl;
		return EXIT_FAILURE;
	}

	// Examine Linux-kernel ELF: retrieve BSS + data segment locations
	if (readSections(bssStart, bssSize, dataStart, dataSize)) {
		return EXIT_FAILURE;
	}

	if (bssStart == 0 || bssSize == 0 || dataStart == 0 || dataSize == 0 ) {
		cerr << "Invalid values for bss start, bss size, data start or data size!" << endl;
		printUsageAndExit(argv[0]);
	}

	if (extractStructDefs("structs_layout.csv", delimiter, &types, expand_type, addMemberName)) {
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

	allocOFile << "id" << delimiter << "data_type_id" << delimiter << "base_address" << delimiter;
	allocOFile << "size" << delimiter << "start" << delimiter << "end" << endl;

	accessOFile << "id" << delimiter << "alloc_id" << delimiter << "txn_id" << delimiter;
	accessOFile << "ts" << delimiter;
	accessOFile << "type" << delimiter << "size" << delimiter << "address" << delimiter;
	accessOFile << "stacktrace_id" << delimiter << "preemptcount" << delimiter << "fn" << endl;

	locksOFile << "id" << delimiter << "address" << delimiter;
	locksOFile << "embedded_in" << delimiter << "lock_type_name" << delimiter;
	locksOFile << "sub_lock" << delimiter << "lock_var_name" << delimiter;
	locksOFile << "flags" << endl;

	locksHeldOFile << "txn_id" << delimiter << "lock_id" << delimiter;
	locksHeldOFile << "start" << delimiter;
	locksHeldOFile << "last_file" << delimiter << "last_line" << delimiter << "last_fn" << delimiter;
	locksHeldOFile << "last_preempt_count" << delimiter << "last_irq_sync" << endl;

	txnsOFile << "id" << delimiter << "start" << delimiter << "end" << endl;

	fnblacklistOFile << "id" << delimiter << "data_type_id" << delimiter << "member_name_id"
		<< delimiter << "fn" << endl;

	memberblacklistOFile << "datatype_id" << delimiter << "datatype_member_id" << endl;
		
	membernamesOFile << "id" << delimiter << "member_name" << endl;
	
	stacktracesOFile << "id" << delimiter << "sequence" << delimiter << "instruction_ptr" << delimiter << "function" << delimiter << "line" << delimiter << "file" << endl;

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
		if (lineElems.size() != MAX_COLUMNS) {
			cerr << "Line (ts=" << ts << ") contains " << lineElems.size() << " elements. Expected " << MAX_COLUMNS << "." << endl;
			return EXIT_FAILURE;
		}
		address = 0x1337, size = 4711, line = 1337, baseAddress = 0x4711, instrPtr = 0xc0ffee, preemptCount = 0xaa, flags = 0x4712;
		lockType = file = stacktrace = fn = "empty";
		try {
			action = lineElems.at(1).at(0);
			switch (action) {
			case 'a':
			case 'f':
				{
					typeStr = lineElems.at(6);
					baseAddress = std::stoull(lineElems.at(3),NULL,16);
					size = std::stoull(lineElems.at(4));
					break;
				}
			case 'l':
				{
					int temp;
					lockMember = lineElems.at(7);
					address = std::stoull(lineElems.at(3),NULL,16);
					file = lineElems.at(8);
					line = std::stoull(lineElems.at(9));
					fn = lineElems.at(10);
					lockType = lineElems.at(6);
					prevPreemptCount = curPreemptCount;
					curPreemptCount =
						preemptCount = std::stoull(lineElems.at(12),NULL,16);
					temp = std::stoi(lineElems.at(13),NULL,10);
					flags = std::stoi(lineElems.at(15),NULL,10);
					switch(temp) {
						case LOCK_NONE:
							irqSync = LOCK_NONE;
							break;

						case LOCK_IRQ:
							irqSync = LOCK_IRQ;
							break;

						case LOCK_IRQ_NESTED:
							irqSync = LOCK_IRQ_NESTED;
							break;

						case LOCK_BH:
							irqSync = LOCK_BH;
							break;

						default:
							cerr << "Line (ts=" << ts << ") contains invalid value for irq_sync" << endl;
							return EXIT_FAILURE;
					}
					temp = std::stoi(lineElems.at(2),NULL,10);
					switch(temp) {
						case P_READ:
							lockOP = P_READ;
							break;

						case P_WRITE:
							lockOP = P_WRITE;
							break;

						case V_READ:
							lockOP = V_READ;
							break;

						case V_WRITE:
							lockOP = V_WRITE;
							break;

						default:
							cerr << "Line (ts=" << ts << ") contains invalid value for lock_op" << endl;
							return EXIT_FAILURE;
					}
					handlePreemptCountChange(
						processPreemptCount, prevPreemptCount, curPreemptCount,
						ts, file, line, fn, typeStr, lockType, preemptCount, includeAllLocks, pseudoAllocID,
						locksOFile, txnsOFile,locksHeldOFile, kernelBaseDir);
					break;
				}
			case 'r':
			case 'w':
				{
					address = std::stoull(lineElems.at(3),NULL,16);
					size = std::stoull(lineElems.at(4));
					baseAddress = std::stoull(lineElems.at(5),NULL,16);
					instrPtr = std::stoull(lineElems.at(11),NULL,16);
					stacktrace = lineElems.at(14);
					if (lineElems.at(12).compare("NULL") != 0) {
						prevPreemptCount = curPreemptCount;
						curPreemptCount =
							preemptCount = std::stoull(lineElems.at(11),NULL,16);
						handlePreemptCountChange(
							processPreemptCount, prevPreemptCount, curPreemptCount,
							ts, file, line, fn, typeStr, lockType, preemptCount, includeAllLocks, pseudoAllocID,
							locksOFile, txnsOFile,locksHeldOFile, kernelBaseDir);
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
				if (activeAllocs.find(baseAddress) != activeAllocs.end()) {
					PRINT_ERROR("ts=" << ts << ",baseAddress=" << hex << showbase << baseAddress << noshowbase,"Found active allocation at address.");
					continue;
				}
				// Do we know that datatype?
				const auto it = find_if(types.cbegin(), types.cend(),
					[&typeStr](const DataType& type) { return type.name == typeStr; } );
				if (it == types.cend()) {
					PRINT_ERROR("ts=" << ts,"Found unknown datatype: " << typeStr);
					continue;
				}
				int datatype_idx = it - types.cbegin();
				// Remember that allocation
				pair<map<unsigned long long,Allocation>::iterator,bool> retAlloc =
					activeAllocs.insert(pair<unsigned long long,Allocation>(baseAddress,Allocation()));
				if (!retAlloc.second) {
					PRINT_ERROR("ts=" << ts << ",baseAddress=" << hex << showbase << baseAddress << noshowbase,"Cannot insert allocation into map.");
				}
				Allocation& tempAlloc = retAlloc.first->second;
				tempAlloc.id = curAllocID++;
				tempAlloc.start = ts;
				tempAlloc.idx = datatype_idx;
				tempAlloc.size = size;
				PRINT_DEBUG("baseAddress=" << showbase << hex << baseAddress << noshowbase << dec << ",type=" << typeStr << ",size=" << size,"Added allocation");
				break;
				}
		case 'f':
				{
				itAlloc = activeAllocs.find(baseAddress);
				if (itAlloc == activeAllocs.end()) {
					PRINT_ERROR("ts=" << ts << ",baseAddress=" << hex << showbase << baseAddress << noshowbase, "Didn't find active allocation for address.");
					continue;
				}
				Allocation& tempAlloc = itAlloc->second;
				// An allocations datatype is
				allocOFile << tempAlloc.id << delimiter << tempAlloc.idx + 1 << delimiter << baseAddress << delimiter << dec << size << delimiter << dec << tempAlloc.start << delimiter << ts << "\n";
				// Iterate through the set of locks, and delete any lock that resided in the freed memory area
				for (itLock = lockPrimKey.begin(); itLock != lockPrimKey.end();) {
					if (itLock->second->lockAddress >= itAlloc->first && itLock->second->lockAddress < (itAlloc->first + tempAlloc.size)) {
						// Lock should not be held anymore
						if (itLock->second->isHeld()) {
							PRINT_ERROR("ts=" << ts << ",baseAddress=" << hex << showbase << baseAddress << noshowbase, "Lock at " << itLock->second->lockAddress << "is being freed but held!");
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
				PRINT_DEBUG("baseAddress=" << showbase << hex << baseAddress << noshowbase << dec << ",type=" << typeStr << ",size=" << size, "Removed allocation");
				break;
				}
		case 'l':
			handlePV(lockOP, ts, address, file, line, fn, lockMember, lockType,
				preemptCount, irqSync, flags, includeAllLocks, pseudoAllocID, locksOFile, txnsOFile, locksHeldOFile, kernelBaseDir);
			break;
		case 'w':
		case 'r':
				{
				itAlloc = activeAllocs.find(baseAddress);
				if (itAlloc == activeAllocs.end()) {
					PRINT_ERROR("ts=" << ts << ",baseAddress=" << hex << showbase << baseAddress << noshowbase, "Didn't find active allocation");
					continue;
				}
				// sanity check
				if (address < baseAddress || address > (baseAddress + itAlloc->second.size)
					|| (address + size) < baseAddress || (address + size) > (baseAddress + itAlloc->second.size)) {
					PRINT_ERROR("ts=" << ts << ",baseAddress=" << hex << showbase << baseAddress << noshowbase, "Memory-access address " << showbase << hex << address << " does not belong to indicated allocation");
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
				tempAccess.stacktrace_id = addStacktrace(kernelBaseDir, stacktracesOFile, delimiter, instrPtr, stacktrace);
				tempAccess.preemptCount = preemptCount;
				break;
				}
		default:
			{
				PRINT_ERROR("ts" << dec << ts, "Unknown action:")
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
		finishTXN(ts, activeTXNs.back().lockPtr, activeTXNs.back().subLock, false, txnsOFile, locksHeldOFile);
	}

	if (isGZ) {
		delete gzinfile;
	} else {
		delete rawinfile;
	}

	binaryread_destroy();

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

		string dataTypeID;
		if (lineElems.at(0) != "\\N") {
			auto itType = type2id.find(lineElems.at(0));
			if (itType == type2id.end()) {
				cerr << "Unknown type in blacklist (function) line " << (lineCounter + 1)
					<< ": " << lineElems.at(0) << endl;
				continue;
			}
			dataTypeID = std::to_string(itType->second);
		} else {
			dataTypeID = lineElems.at(0);
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

		// Write a MySQL NULL for the id which forces MySQL to allocate a new unique id for this entry
		fnblacklistOFile << "\\N" << delimiter << dataTypeID << delimiter
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

	cerr << "Finished." << endl;

	return EXIT_SUCCESS;
}
