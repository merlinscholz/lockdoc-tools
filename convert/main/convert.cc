#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <unistd.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <bfd.h>
#include <stack>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "dwarves_api.h"
#include "gzstream/gzstream.h"
#include "config.h"
#include "git_version.h"


#define IS_MULTILVL_LOCK(x)	((x).ptr == 0x42)

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
	string lastLockFn;											// Lock function used the last time
	int lastPreemptCount;										// Value of preemptcount() after the lock has been acquired

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
 * Describes a certain datatype which is observed by our experiment
 */
struct DataType {
	string typeStr;												// Unique to describe a certain datatype, e.g., task_struct
	bool foundInDw;												// True if the struct has been found in the dwarf information. False otherwise.
	unsigned long long id;										// An unique id for a particular datatype
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
	unsigned long long stackPtr;								// Stack pointer
	unsigned long long instrPtr;								// Instruction pointer
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
 * An array of all observed datatypes. For now, we observe three datatypes.
 */
static DataType types[MAX_OBSERVED_TYPES];
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
 * Start address and size of the bss and data section. All information is read from the dwarf information during startup.
 */
static unsigned long long bssStart = 0, bssSize = 0, dataStart = 0, dataSize = 0;
/**
 * Used to pass context information to the dwarves callback.
 * Have a look at convert_cus_iterator().
 */
struct CusIterArgs {
	DataType *types;
	FILE *fp;
};
// address -> function name cache
std::map<uint64_t, const char *> functionAddresses;

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

static struct cus *cus;

static void printUsageAndExit(const char *elf) {
	cerr << "usage: " << elf;
	cerr << " -s enable processing of seqlock_t (EXPERIMENTAL)" << endl;
	cerr << " -k <path/to/vmlinux";
	cerr << " <inputfile>.gz" << endl;
	exit(EXIT_FAILURE);
}

// caching wrapper around cus__get_function_at_addr
static const char *get_function_at_addr(const struct cus *cus, uint64_t addr)
{
	auto it = functionAddresses.find(addr);
	const char *ret;
	if (it == functionAddresses.end()) {
		functionAddresses[addr] = ret = cus__get_function_at_addr(cus, addr);
	} else {
		ret = it->second;
	}
	return ret;
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
			txnsOFile << activeTXNs.back().id << DELIMITER_CHAR;
			txnsOFile << activeTXNs.back().start << DELIMITER_CHAR;
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
					locksHeldOFile << dec << activeTXNs.back().id << DELIMITER_CHAR << tempLock.id << DELIMITER_CHAR;
					locksHeldOFile << tempLockPos.start << DELIMITER_CHAR;
					locksHeldOFile << tempLockPos.lastFile << DELIMITER_CHAR;
					locksHeldOFile << tempLockPos.lastLine << DELIMITER_CHAR << tempLockPos.lastFn << DELIMITER_CHAR;
					locksHeldOFile << tempLockPos.lastPreemptCount << DELIMITER_CHAR << tempLockPos.lastLockFn << "\n";
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
		*pMemAccessOFile << dec << tempAccess.id << DELIMITER_CHAR << tempAccess.alloc_id;
		*pMemAccessOFile << DELIMITER_CHAR << (activeTXNs.empty() ? "\\N" : std::to_string(activeTXNs.back().id));
		*pMemAccessOFile << DELIMITER_CHAR << tempAccess.ts;
		*pMemAccessOFile << DELIMITER_CHAR << tempAccess.action << DELIMITER_CHAR << dec << tempAccess.size;
		*pMemAccessOFile << DELIMITER_CHAR << tempAccess.address << DELIMITER_CHAR << tempAccess.stackPtr << DELIMITER_CHAR << tempAccess.instrPtr;
		*pMemAccessOFile << DELIMITER_CHAR << get_function_at_addr(cus, tempAccess.instrPtr) << "\n";
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

static int convert_cus_iterator(struct cu *cu, void *cookie) {
	uint16_t class_id;
	int i;
	struct tag *ret;
	CusIterArgs *cusIterArgs = (CusIterArgs*)cookie;

	for (i = 0; i < MAX_OBSERVED_TYPES; i++) {
		// Skip known datatypes
		if (cusIterArgs->types[i].foundInDw) {
			continue;
		}
		// Does this compilation unit contain information of at datatype at index i?
		ret = cu__find_struct_by_name(cu, cusIterArgs->types[i].typeStr.c_str(),1,&class_id);
		if (ret == NULL) {
			continue;
		}

		// Is it really a class or a struct?
		if (ret->tag == DW_TAG_class_type ||
			ret->tag == DW_TAG_interface_type ||
			ret->tag == DW_TAG_structure_type) {

			if (class__fprintf(ret, cu, cusIterArgs->fp, cusIterArgs->types[i].id)) {
				cusIterArgs->types[i].foundInDw = 1;
			}
		}
	}

	// If at least the information about one datatype is still missing, continue iterating through the cus.
	for (i = 0; i < MAX_OBSERVED_TYPES; i++) {
		if (cusIterArgs->types[i].foundInDw == 0) {
			return 0;
		}
	}

	// No need to proceed with the remaining compilation untis. Stop iteration.
	return 1;
}

// find .bss and .data sections
static int readSections(const char *filename) {
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

	memset(&confLoad,0,sizeof(confLoad));
	confLoad.get_addr_info = true;
	// Load the dwarf information of every compilation unit
	if (cus__load_file(cus, &confLoad, filename) != 0) {
		cerr << "No debug information found in " << filename << endl;
		return -1;
	}

	memset(&cusIterArgs,0,sizeof(cusIterArgs));

	// Open the output file and add the header
	structsLayoutOFile = fopen("structs_layout.csv","w+");
	if (structsLayoutOFile == NULL) {
		perror("fopen structs_layout.csv");
		cus__delete(cus);
		dwarves__exit();
		return -1;
	}
	fprintf(structsLayoutOFile,"type_id" DELIMITER_STRING "type" DELIMITER_STRING "member" DELIMITER_STRING "offset" DELIMITER_STRING "size\n");

	// Pass the context information to the callback: types array and the outputfile
	cusIterArgs.types = (DataType*)&types;
	cusIterArgs.fp = structsLayoutOFile;
	// Iterate through every compilation unit, and look for information about the datatypes of interest
	cus__for_each_cu(cus, convert_cus_iterator,&cusIterArgs,NULL);

	fclose(structsLayoutOFile);

	return 0;
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
	string inputLine, token, typeStr, file, fn, lockfn, lockType;
	vector<string> lineElems; // input CSV columns
	map<unsigned long long,Allocation>::iterator itAlloc;
	map<unsigned long long,Lock>::iterator itLock, itTemp;
	unsigned long long ts = 0, ptr, size = 0, line = 0, address = 0x4711, stackPtr = 0x1337, instrPtr = 0xc0ffee, preemptCount = 0xaa;
	int lineCounter = 0, isGZ;
	char action, param, *vmlinuxName = NULL;
	bool processSeqlock = false;

	if (argc < 2) {
		cerr << "Need at least an input file!" << endl;
		return EXIT_FAILURE;
	}

	while ((param = getopt(argc,argv,"k:s")) != -1) {
		switch (param) {
		case 'k':
			vmlinuxName = optarg;
			break;
		case 's':
			processSeqlock = true;
			break;
		}
	}
	if (vmlinuxName == NULL || optind == argc) {
		printUsageAndExit(argv[0]);
	}
	
	cerr << "convert version: " << GIT_BRANCH << ", " << GIT_MESSAGE << endl;
	if (processSeqlock) {
		cerr << "Enabled experimental feature 'processing of seq{lock,count}_t'" << endl;
	}

	types[0].typeStr = "task_struct";
	types[0].foundInDw = 0;
	types[0].id = 1;
	types[1].typeStr = "inode";
	types[1].foundInDw = 0;
	types[1].id = 2;
	types[2].typeStr = "super_block";
	types[2].foundInDw = 0;
	types[2].id = 3;
	types[3].typeStr = "backing_dev_info";
	types[3].foundInDw = 0;
	types[3].id = 4;

	if (readSections(vmlinuxName)) {
		return EXIT_FAILURE;
	}

	dwarves__init(0);
	cus = cus__new();
	if (cus == NULL) {
		cerr << "Insufficient memory" << endl;
		return EXIT_FAILURE;
	}

	if (extractStructDefs(cus,vmlinuxName)) {
		return EXIT_FAILURE;
	}

	if (bssStart == 0 || bssSize == 0 || dataStart == 0 || dataSize == 0 ) {
		cerr << "Invalid values for bss start, bss size, data start or data size!" << endl;
		printUsageAndExit(argv[0]);
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


	// Create the outputfiles. One for each table.
	ofstream datatypesOFile("data_types.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream allocOFile("allocations.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream accessOFile("accesses.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream locksOFile("locks.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream locksHeldOFile("locks_held.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream txnsOFile("txns.csv",std::ofstream::out | std::ofstream::trunc);

	// CSV headers
	datatypesOFile << "id" << DELIMITER_CHAR << "name" << endl;

	allocOFile << "id" << DELIMITER_CHAR << "type_id" << DELIMITER_CHAR << "ptr" << DELIMITER_CHAR;
	allocOFile << "size" << DELIMITER_CHAR << "start" << DELIMITER_CHAR << "end" << endl;

	accessOFile << "id" << DELIMITER_CHAR << "alloc_id" << DELIMITER_CHAR << "txn_id" << DELIMITER_CHAR;
	accessOFile << "ts" << DELIMITER_CHAR;
	accessOFile << "type" << DELIMITER_CHAR << "size" << DELIMITER_CHAR << "address" << DELIMITER_CHAR;
	accessOFile << "stackptr" << DELIMITER_CHAR << "instrptr" << DELIMITER_CHAR << "fn" << endl;

	locksOFile << "id" << DELIMITER_CHAR << "ptr" << DELIMITER_CHAR;
	locksOFile << "embedded" << DELIMITER_CHAR << "locktype" << endl;

	locksHeldOFile << "txn_id" << DELIMITER_CHAR << "lock_id" << DELIMITER_CHAR;
	locksHeldOFile << "start" << DELIMITER_CHAR;
	locksHeldOFile << "lastFile" << DELIMITER_CHAR << "lastLine" << DELIMITER_CHAR << "lastFn" << DELIMITER_CHAR;
	locksHeldOFile << "lastPreemptCount" << DELIMITER_CHAR << "lastLockFn" << endl;

	txnsOFile << "id" << DELIMITER_CHAR << "start" << DELIMITER_CHAR << "end" << endl;

	for (int i = 0; i < MAX_OBSERVED_TYPES; i++) {
		// The unique id for each datatype will be its index + 1
		datatypesOFile << types[i].id << DELIMITER_CHAR << types[i].typeStr << endl;
	}

	// Start reading the inputfile
	for (;getline(*infile,inputLine); ss.clear(), ss.str(""), lineElems.clear(), lineCounter++) {
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
		// Tokenize each line by DELIMITER_CHAR, and store each element in a vector
		while (getline(ss,token,DELIMITER_CHAR)) {
			lineElems.push_back(token);
		}

		// Parse each element
		ts = std::stoull(lineElems.at(0));
		if (lineElems.size() != MAX_COLUMNS) {
			cerr << "Line (ts=" << ts << ") contains " << lineElems.size() << " elements. Expected " << MAX_COLUMNS << "." << endl;
			return EXIT_FAILURE;
		}
		try {
			action = lineElems.at(1).at(0);
			typeStr = lineElems.at(2);
			if (lineElems.at(3).compare("NULL") != 0) {
				ptr = std::stoull(lineElems.at(3),NULL,16);
			} else {
				ptr = 0;
			}
			if (lineElems.at(4).compare("NULL") != 0) {
				size = std::stoull(lineElems.at(4));
			} else {
				size = 0;
			}
			file = lineElems.at(5);
			if (lineElems.at(6).compare("NULL") != 0) {
				line = std::stoull(lineElems.at(6));
			} else {
				line = 42;
			}
			fn = lineElems.at(7);
			lockfn = lineElems.at(8);
			lockType = lineElems.at(9);
			if (lineElems.at(10).compare("NULL") != 0) {
				preemptCount = std::stoull(lineElems.at(10),NULL,16);
			}
			if (lineElems.at(11).compare("NULL") != 0) {
				address = std::stoull(lineElems.at(11),NULL,16);
			}
			if (lineElems.at(12).compare("NULL") != 0) {
				instrPtr = std::stoull(lineElems.at(12),NULL,16);
			}
			if (lineElems.at(13).compare("NULL") != 0) {
				stackPtr = std::stoull(lineElems.at(13),NULL,16);
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
				int datatype_idx;
				for (datatype_idx = 0; datatype_idx < MAX_OBSERVED_TYPES; datatype_idx++) {
					if (types[datatype_idx].typeStr.compare(typeStr) == 0) {
						break;
					}
				}
				if (datatype_idx >= MAX_OBSERVED_TYPES) {
					cerr << "Found unknown datatype: " << typeStr << endl;
					continue;
				}
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
				allocOFile << tempAlloc.id << DELIMITER_CHAR << tempAlloc.idx + 1 << DELIMITER_CHAR << ptr << DELIMITER_CHAR << dec << size << DELIMITER_CHAR << dec << tempAlloc.start << DELIMITER_CHAR << ts << "\n";
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
				break;
				}
		case 'v':
		case 'p':
				{
				itLock = lockPrimKey.find(ptr);
				if (itLock != lockPrimKey.end()) {
					if (action == 'p') {
						// Found a known lock. Just update its metainformation
#ifdef VERBOSE
						cerr << "Found existing lock at address " << showbase << hex << ptr << noshowbase << ". Just updating the meta information." << endl;
#endif
						if (ptr == 0x42) { // RCU
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
						tempLockPos.lastLockFn = lockfn;
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
						if (ptr == 0x42) {
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
					continue;
				}

				// categorize currently unknown lock
				unsigned allocation_id = 0;
				// A lock which probably resides in one of the observed allocations. If not, check if it is a global lock
				// This way, locks which reside in global structs are recognized as 'embedded in'.
				itAlloc = activeAllocs.upper_bound(ptr);
				if (itAlloc != activeAllocs.begin()) {
					itAlloc--;
					if (ptr < itAlloc->first + itAlloc->second.size) {
						allocation_id = itAlloc->second.id;
					}
				}
				if (allocation_id == 0) {
					if ((ptr >= bssStart && ptr < bssStart + bssSize) || ( ptr >= dataStart && ptr < dataStart + dataSize) || (typeStr.compare("static") == 0 && ptr == 0x42)) {
						// static lock which resides either in the bss segment or in the data segment
						// or global static lock aka rcu lock
#ifdef VERBOSE
						cout << "Found static lock: " << showbase << hex << ptr << noshowbase << endl;
#endif
					} else {
#ifdef VERBOSE
						cerr << "Lock at address " << showbase << hex << ptr << noshowbase << " does not belong to any of the observed memory regions. Ignoring it." << PRINT_CONTEXT << endl;
#endif
						continue;
					}
				}

				if (action == 'v') {
					cerr << "Cannot find a lock at address " << showbase << hex << ptr << noshowbase << PRINT_CONTEXT << endl;
					continue;
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
				tempLockPos.lastLockFn = lockfn;
				tempLockPos.lastPreemptCount = preemptCount;

				locksOFile << dec << tempLock.id << DELIMITER_CHAR << tempLock.ptr;
				locksOFile << DELIMITER_CHAR << sql_null_if(tempLock.allocation_id, tempLock.allocation_id == 0) << DELIMITER_CHAR << tempLock.lockType << "\n";

				// a P() suspends the current TXN and creates a new one
				startTXN(ts, ptr);
				break;
				}
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
				tempAccess.stackPtr = stackPtr;
				tempAccess.instrPtr = instrPtr;
				break;
				}
		}
	}

	// Due to the fact that we abort the experiment as soon as the benchmark has finished, some allocations may not have been freed.
	// Hence, print every allocation, which is still stored in the map, and set the freed timestamp to NULL.
	for (itAlloc = activeAllocs.begin(); itAlloc != activeAllocs.end(); itAlloc++) {
		Allocation& tempAlloc = itAlloc->second;
		allocOFile << tempAlloc.id << DELIMITER_CHAR << types[tempAlloc.idx].id << DELIMITER_CHAR << itAlloc->first << DELIMITER_CHAR;
		allocOFile << dec << tempAlloc.size << DELIMITER_CHAR << dec << tempAlloc.start << DELIMITER_CHAR << "\\N" << "\n";
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
		gzinfile->close();;
		delete gzinfile;
	} else {
		rawinfile->close();
		delete rawinfile;
	}

	datatypesOFile.close();
	allocOFile.close();
	accessOFile.close();
	locksOFile.close();
	locksHeldOFile.close();
	txnsOFile.close();

	cus__delete(cus);
	dwarves__exit();

	cerr << "Finished." << endl;

	return EXIT_SUCCESS;
}
