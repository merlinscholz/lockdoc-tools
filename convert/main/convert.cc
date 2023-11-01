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
#include "lockdoc_event.h"
#include "git_version.h"
#include "rwlock.h"
#include "lockmanager.h"

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
	unsigned long long id;										// A nunique id which refers to that particular instance
	int size;													// Size in bytes
	int subclass_idx;											// An index into the subclass array, which describes the datatype of this allocation
};
/**
 * Describes a subclass of the observed data types.
 */
struct Subclass {
	Subclass (unsigned long long _id, std::string _name, int _data_type_idx, bool _real_subclass) :
		id(_id), name(_name), data_type_idx(_data_type_idx), real_subclass(_real_subclass) {}
	unsigned long long id;										// An unique id which refers to that particular instance
	std::string name;											// An unique name that describes this type of subclass
	int data_type_idx;											// An index into the types array, which describes the data type of this allocation
	bool real_subclass;
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
	long ctx;
};

/**
 * The kernel source tree
 */
static const char *kernelBaseDir = "/opt/kernel/linux-32-lockdebugging-4-10/";

static LockManager *lockManager;
/**
 * Contains all active allocations. The ptr to the memory area is used as an index.
 */
static map<unsigned long long,Allocation> activeAllocs;
/**
 * Contains all observed datatypes.
 */
static std::vector<DataType> types;
/**
 * Contains all observed subclasses.
 */
static std::vector<Subclass> subclasses;
/**
 * The list of the LOOK_BEHIND_WINDOW last memory accesses.
 */
static vector<MemAccess> lastMemAccesses;
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
 * Pairs of start addresses and sizees of the named data section
 */
static map<string, pair<uint64_t, uint64_t>> dataSections;

/**
 * Enable context tracing?
 * Enabled via cmdline argument -c. Disabled by default.
 * If disabled, a default context is used. 0 for example.
 */
static int ctxTracing = 0;
/**
 * The next id for a new data type.
 */
static unsigned long long curTypeID = 1;
/**
 * The next id for a new allocation.
 */
static unsigned long long curAllocID = 1;
/**
 * The next id for a new subclass.
 */
static unsigned long long curSubclassID = 1;
/**
 * The next id for a new memory access.
 */
static unsigned long long curAccessID = 1;
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
		" -v  show version\n"
		" -h  Print this help\n"
		" -d  delimiter used in input.csv, and used for the output csv files later on\n"
		" -u  include non-static locks with unknown allocation in output\n"
		"     (these will be assigned to a pseudo allocation with ID 1)\n"
		" -g  The kernel source tree, default: " << kernelBaseDir << "\n"
		" -c  Use one TXN stack per contex\n"
		" -h  help\n";
	exit(EXIT_FAILURE);
}

static void printVersion()
{
	cerr << "convert version: " << GIT_BRANCH << ", " << GIT_MESSAGE << endl;
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

static bool checkLockInSections(uint64_t lockAddress, map<string, pair<uint64_t, uint64_t>>& dataSections)
{
	for (const pair<string, pair<uint64_t, uint64_t>> section : dataSections) {
    	if(lockAddress >= section.second.first && lockAddress <= (section.second.first + section.second.second)){
			return true;
		}
	}

	PRINT_DEBUG("lockAddress=" << hex << showbase << lockAddress, "Lock could not be found in specified ELF sections");
	return false;
}

/* handle P() / V() events */
static void handlePV(
	enum LOCK_OP lockOP,
	unsigned long long ts,
	unsigned long long lockAddress,
	string const& file,
	unsigned long long line,
	string const& lockMember,
	string const& lockType,
	unsigned flags,
	bool includeAllLocks,
	unsigned long long pseudoAllocID,
	ofstream& locksOFile,
	ofstream& txnsOFile,
	ofstream& locksHeldOFile,
	const char *kernelBaseDir,
	long ctx
	)
{
	RWLock *tempLock = lockManager->findLock(lockAddress);
	if (tempLock == NULL) {
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
			if (checkLockInSections(lockAddress, dataSections)
				|| (lockMember.compare(PSEUDOLOCK_VAR) == 0 && RWLock::isPseudoLock(lockAddress))) {
				// static lock which resides either in the bss segment or in the data segment
				// or global static lock aka rcu lock
				PRINT_DEBUG("ts=" << dec << ts << ",lockAddress=" << hex << showbase << lockAddress, "Found static lock.");
				// Try to resolve what the name of this lock is
				// This also catches cases where the lock resides inside another data structure.
				lockVarName = getGlobalLockVar(lockAddress);
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
		tempLock = lockManager->allocLock(lockAddress, allocation_id, lockType, lockVarName, flags);
		PRINT_DEBUG("", "Created lock: " << tempLock);
		// Write the lock to disk (aka locks.csv)
		tempLock->writeLock(locksOFile, delimiter);
	}
	tempLock->transition(lockOP, ts, file, line, lockMember, flags, kernelBaseDir, ctx);
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
	for (auto&& tempAccess : *pMemAccesses) {
		long ctx;
		if (ctxTracing) {
			ctx = tempAccess.ctx;
		} else {
			ctx = DUMMY_EXECUTION_CONTEXT;
		}
		*pMemAccessOFile << dec << tempAccess.id << delimiter << tempAccess.alloc_id;
		*pMemAccessOFile << delimiter << (lockManager->hasActiveTXN(ctx) ? std::to_string(lockManager->getActiveTXN(ctx).id) : "\\N");
		*pMemAccessOFile << delimiter << tempAccess.ts;
		*pMemAccessOFile << delimiter << tempAccess.action << delimiter << dec << tempAccess.size;
		*pMemAccessOFile << delimiter << tempAccess.address << delimiter << tempAccess.stacktrace_id;
		*pMemAccessOFile << delimiter << tempAccess.ctx;
		*pMemAccessOFile << "\n";
		// count memory accesses for the current TXN if there's one active
		if (lockManager->hasActiveTXN(ctx)) {
			lockManager->getActiveTXN(ctx).memAccessCounter += 1;
		}
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

static unsigned long long addStacktrace(const char *kernelBaseDir, ostream &stacktracesOFile, char delimiter, unsigned long long instrPtr, std::string &stacktrace) {
	unsigned long long ret;

	// Remove the last character since it always is a comma.
	if (!stacktrace.empty() && stacktrace.find_last_of(',') != string::npos) {
		stacktrace.pop_back();
	}
	auto itStacktrace = stacktraces.emplace(instrPtr,map<std::string,unsigned long long>());
	auto &subStacktraces = itStacktrace.first->second;
	// Do we know that substacktrace?
	const auto itSubStacktrace = find_if(subStacktraces.cbegin(), subStacktraces.cend(),
		[&stacktrace](const pair<string, unsigned long long> &value ) { return value.first == stacktrace; } );
	if (itSubStacktrace == subStacktraces.cend()) {
		int sequence = 0;
		stringstream ss;
		ss << hex << showbase << instrPtr;
		if (!stacktrace.empty()) {
		       ss << "," << stacktrace;
		}
		std::string token;

		ret = curStacktraceID++;
		subStacktraces.emplace(stacktrace,ret);

		while (getline(ss,token,',')) {
			auto instrPtrPrev = instrPtr = std::stoull(token,NULL,16);
			if (sequence > 0) {
				instrPtrPrev--;
			}
			const struct ResolvedInstructionPtr &resolvedInstrPtr = get_function_at_addr(kernelBaseDir, instrPtrPrev);
			stacktracesOFile << ret << delimiter << sequence << delimiter << instrPtr << delimiter << instrPtrPrev << delimiter;
			stacktracesOFile << resolvedInstrPtr.codeLocation.fn << delimiter << resolvedInstrPtr.codeLocation.line << delimiter << resolvedInstrPtr.codeLocation.file << "\n";
			sequence++;
			if (resolvedInstrPtr.inlinedBy.size() > 0) {
				for (auto &inlinedFn : resolvedInstrPtr.inlinedBy) {
					stacktracesOFile << ret << delimiter << sequence << delimiter << instrPtr << delimiter << instrPtrPrev << delimiter;
					stacktracesOFile << inlinedFn.fn << delimiter << inlinedFn.line << delimiter << inlinedFn.file << "\n";
					sequence++;
				}
			}
		}
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
	string inputLine, token, typeStr, file, lockType, stacktrace, lockMember;
	vector<string> lineElems; // input CSV columns
	map<unsigned long long,Allocation>::iterator itAlloc;
	unsigned long long ts = 0, address = 0x1337, size = 4711, line = 1337, baseAddress = 0x4711, instrPtr = 0xc0ffee, flags = 0x4712;
	int lineCounter, isGZ, param;
	char action = '.', *vmlinuxName = NULL, *fnBlacklistName = nullptr, *memberBlacklistName = nullptr, *datatypesName = nullptr;
	bool processSeqlock = false, includeAllLocks = false;
	enum LOCK_OP lockOP = P_WRITE;
	long ctx = 0;
	unsigned long long pseudoAllocID = 0; // allocID for locks belonging to unknown allocation

	while ((param = getopt(argc,argv,"k:b:m:t:svhd:ug:c")) != -1) {
		switch (param) {
		case 'c':
			ctxTracing = 1;
			break;
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

	// Examine Kernel ELF: retrieve .bss, .data and other, optional segment locations
	readSections(dataSections);

	if (dataSections[".bss"].first == 0 || dataSections[".bss"].second == 0
		|| dataSections[".data"].first == 0 || dataSections[".data"].second == 0 ) {
		cerr << "Invalid values for bss start, bss size, data start or data size! Maybe .bss or .data are missing in ELF_SECTIONS?" << endl;
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
	ofstream subclassesOFile("subclasses.csv", std::ofstream::out | std::ofstream::trunc);

	// CSV headers
	datatypesOFile << "id" << delimiter << "name" << endl;

	allocOFile << "id" << delimiter << "subclass_id" << delimiter << "base_address" << delimiter;
	allocOFile << "size" << delimiter << "start" << delimiter << "end" << endl;

	accessOFile << "id" << delimiter << "alloc_id" << delimiter << "txn_id" << delimiter;
	accessOFile << "ts" << delimiter;
	accessOFile << "type" << delimiter << "size" << delimiter << "address" << delimiter;
	accessOFile << "stacktrace_id" << delimiter << "fn" << delimiter;
	accessOFile << "context" << endl;

	locksOFile << "id" << delimiter << "address" << delimiter;
	locksOFile << "embedded_in" << delimiter << "lock_type_name" << delimiter;
	locksOFile << "sub_lock" << delimiter << "lock_var_name" << delimiter;
	locksOFile << "flags" << endl;

	locksHeldOFile << "txn_id" << delimiter << "lock_id" << delimiter;
	locksHeldOFile << "start" << delimiter;
	locksHeldOFile << "last_file" << delimiter << "last_line" << endl;

	txnsOFile << "id" << delimiter << "start_ts" << delimiter;
	txnsOFile << "start_ctx" << delimiter << "end_ts" << delimiter;
	txnsOFile << "end_ctx" << endl;

	fnblacklistOFile << "id" << delimiter << "subclass_id" << delimiter << "member_name_id"
		<< delimiter << "fn" << endl;

	memberblacklistOFile << "subclass_id" << delimiter << "member_name_id" << endl;
		
	membernamesOFile << "id" << delimiter << "member_name" << endl;
	
	stacktracesOFile << "id" << delimiter << "sequence" << delimiter << "instruction_ptr" << delimiter;
	stacktracesOFile << "instruction_ptr_prev" << delimiter << "function" << delimiter << "line" << delimiter << "file" << endl;

	subclassesOFile << "id" << delimiter << "data_type_id" << delimiter << "name" << endl;

	lockManager = new LockManager(txnsOFile, locksHeldOFile);

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
		address = 0x1337, size = 4711, line = 1337, baseAddress = 0x4711, instrPtr = 0xc0ffee, flags = 0x4712;
		lockType = file = stacktrace = "empty";
		try {
			action = lineElems.at(1).at(0);
			switch (action) {
			case LOCKDOC_ALLOC:
			case LOCKDOC_FREE:
				{
					typeStr = lineElems.at(6);
					baseAddress = std::stoull(lineElems.at(3),NULL,16);
					size = std::stoull(lineElems.at(4));
					break;
				}
			case LOCKDOC_LOCK_OP:
				{
					int temp;
					address = std::stoull(lineElems.at(3),NULL,16);
					lockMember = lineElems.at(7);
					file = lineElems.at(8);
					line = std::stoull(lineElems.at(9));
					lockType = lineElems.at(6);
					flags = std::stoi(lineElems.at(12),NULL,10);
					if (ctxTracing) {
						ctx = std::stoul(lineElems.at(13),NULL,10);
					} else {
						ctx = DUMMY_EXECUTION_CONTEXT;
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
					break;
				}
			case LOCKDOC_READ:
			case LOCKDOC_WRITE:
				{
					address = std::stoull(lineElems.at(3),NULL,16);
					size = std::stoull(lineElems.at(4));
					baseAddress = std::stoull(lineElems.at(5),NULL,16);
					instrPtr = std::stoull(lineElems.at(10),NULL,16);
					stacktrace = lineElems.at(11);
					ctx = std::stoul(lineElems.at(13),NULL,10);
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
		case LOCKDOC_ALLOC:
				{
				if (activeAllocs.find(baseAddress) != activeAllocs.end()) {
					PRINT_ERROR("ts=" << ts << ",baseAddress=" << hex << showbase << baseAddress << noshowbase,"Found active allocation at address.");
					continue;
				}
				string subclassName, dataTypeName;
				/*
				 * This flag just speeds up the lookup of a subclass.
				 * If a data type does *not* have a real subclass, e.g., journal_t,

				 * and we leave the subclass name empty,
				 * the lookup in the lines below would fail.
				 * Furthermore, we would have to look into the types array as well to find the appropriate data type.
				 * If realSubclass is not set, the subclass will be NULL in the corresponding csv output.
				 */
				bool realSubclass;
				if (typeStr.find(DELIMITER_SUBCLASS) != string::npos) {
					dataTypeName = typeStr.substr(0, typeStr.find(DELIMITER_SUBCLASS));
					subclassName = typeStr.substr(typeStr.find(DELIMITER_SUBCLASS) + 1);
					realSubclass = true;
					PRINT_DEBUG("dataTypeName=" << dataTypeName << ",subclassName=" << subclassName, "Names extracted");
				} else {
					dataTypeName = subclassName = typeStr;
					realSubclass = false;
				}
				int subclass_idx;
				// Do we know that subclass?
				const auto itSubclass = find_if(subclasses.cbegin(), subclasses.cend(),
					[&subclassName](const Subclass& subclass) { return subclass.name == subclassName; } );
				if (itSubclass == subclasses.cend()) {
					// Do we know that data type?
					auto itDataType = find_if(types.begin(), types.end(),
						[&dataTypeName](const DataType& type) { return type.name == dataTypeName; } );
					if (itDataType == types.cend()) {
						PRINT_ERROR("ts=" << ts,"Found unknown datatype: " << typeStr);
						continue;
					}
					/*
					 * Sanity check: Does this data type have a dummy subclass?
					 * Either *each* allocation/free must specify a subclass
					 * or no memory operations does it.
					 * Mixing it up is not allowed!
					 */
					const auto itSubclass_ = find_if(subclasses.cbegin(), subclasses.cend(),
						[&dataTypeName](const Subclass& subclass) { return subclass.name == dataTypeName; } );
					if (itSubclass_ != subclasses.cend()) {
						PRINT_ERROR("ts=" << ts,"Found dummy subclass, although dedicated subclass exists: " << typeStr);
						exit(-1);
					}
					int data_type_idx = itDataType - types.cbegin();
					subclasses.emplace_back(curSubclassID++, subclassName, data_type_idx, realSubclass);
					subclass_idx = subclasses.size() - 1;
					PRINT_DEBUG("subclass=\"" << subclasses[subclass_idx].name << "\",data_type=\"" << types[data_type_idx].name << "\",idx=" << subclass_idx << ",real_subclass=" << realSubclass, "Created subclass");
				} else {
					subclass_idx = itSubclass - subclasses.cbegin();
				}
				// Remember that allocation
				pair<map<unsigned long long,Allocation>::iterator,bool> retAlloc =
					activeAllocs.insert(pair<unsigned long long,Allocation>(baseAddress,Allocation()));
				if (!retAlloc.second) {
					PRINT_ERROR("ts=" << ts << ",baseAddress=" << hex << showbase << baseAddress << noshowbase,"Cannot insert allocation into map.");
				}
				Allocation& tempAlloc = retAlloc.first->second;
				tempAlloc.id = curAllocID++;
				tempAlloc.start = ts;
				tempAlloc.subclass_idx = subclass_idx;
				tempAlloc.size = size;
				PRINT_DEBUG("baseAddress=" << showbase << hex << baseAddress << noshowbase << dec << ",type=" << typeStr << ",size=" << size,"Added allocation");
				break;
				}
		case LOCKDOC_FREE:
				{
				itAlloc = activeAllocs.find(baseAddress);
				if (itAlloc == activeAllocs.end()) {
					PRINT_ERROR("ts=" << ts << ",baseAddress=" << hex << showbase << baseAddress << noshowbase, "Didn't find active allocation for address.");
					continue;
				}
				Allocation& tempAlloc = itAlloc->second;
				// An allocations datatype is
				allocOFile << tempAlloc.id << delimiter << subclasses[tempAlloc.subclass_idx].id << delimiter << baseAddress << delimiter << dec << size << delimiter << dec << tempAlloc.start << delimiter << ts << "\n";
				lockManager->deleteLockByArea(itAlloc->first, tempAlloc.size);

				activeAllocs.erase(itAlloc);
				PRINT_DEBUG("baseAddress=" << showbase << hex << baseAddress << noshowbase << dec << ",type=" << typeStr << ",size=" << size, "Removed allocation");
				break;
				}
		case LOCKDOC_LOCK_OP:
			handlePV(lockOP, ts, address, file, line, lockMember, lockType,
				flags, includeAllLocks, pseudoAllocID, locksOFile, txnsOFile, locksHeldOFile, kernelBaseDir, ctx);
			break;
		case LOCKDOC_READ:
		case LOCKDOC_WRITE:
				{
				itAlloc = activeAllocs.find(baseAddress);
				if (itAlloc == activeAllocs.end()) {
					PRINT_ERROR("ts=" << ts << ",baseAddress=" << hex << showbase << baseAddress << noshowbase, "Didn't find active allocation");
					continue;
				}
				// sanity check
				if (address < baseAddress || address > (baseAddress + itAlloc->second.size)
					|| (address + size) < baseAddress || (address + size) > (baseAddress + itAlloc->second.size)) {
					PRINT_ERROR("ts=" << ts << ",baseAddress=" << hex << showbase << baseAddress << noshowbase, "Memory-access address " << showbase << hex << address << " does not belong to indicated allocation, size=" << itAlloc->second.size << ",asize=" << size);
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
				tempAccess.ctx = ctx;
				tempAccess.stacktrace_id = addStacktrace(kernelBaseDir, stacktracesOFile, delimiter, instrPtr, stacktrace);
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
		allocOFile << tempAlloc.id << delimiter << subclasses[tempAlloc.subclass_idx].id << delimiter << itAlloc->first << delimiter;
		allocOFile << dec << tempAlloc.size << delimiter << dec << tempAlloc.start << delimiter << "\\N" << "\n";
	}

	// Flush memory writes by pretending there's a final V()
	writeMemAccesses('v', 0, &accessOFile, &lastMemAccesses);
	lockManager->closeAllTXNs(ts);

	if (isGZ) {
		delete gzinfile;
	} else {
		delete rawinfile;
	}

	binaryread_destroy();

	// Dump all observed subclasses
	int i = 1;
	for (const auto &subclass : subclasses) {
		subclassesOFile << i << delimiter << types[subclass.data_type_idx].id << delimiter;
		subclassesOFile << sql_null_if(subclass.name, !subclass.real_subclass) << endl;
		i++;
	}

	// Helper: type -> IDs mapping
	std::map<std::string, vector<std::string> > type2id;
	for (const auto& subclass : subclasses) {
		const auto& type = types[subclass.data_type_idx];
		type2id[type.name].push_back(std::to_string(subclass.id));
	}
	// Helper: subclass -> ID mapping
	std::map<std::string, decltype(Subclass::id)> subclass2id;
	for (const auto& subclass : subclasses) {
		subclass2id[subclass.name] = subclass.id;
	}

	int fnBlID = 1;
	vector<std::string> blacklistIDs;
	// Process function blacklist
	for (lineCounter = 0;
		getline(fnBlacklistInfile, inputLine);
		ss.clear(), ss.str(""), lineElems.clear(), blacklistIDs.clear(), lineCounter++) {

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
		if (lineElems.size() != 4) {
			cerr << "Ignoring invalid function blacklist entry, line " << dec << (lineCounter + 1)
				<< ": " << inputLine << endl;
			continue;
		}

		if (lineElems.at(0) != "\\N") {
			string subclassName, dataTypeName, temp = lineElems.at(0);
			bool isRealSubclass;
			if (temp.find(DELIMITER_SUBCLASS) != string::npos) {
				dataTypeName = temp.substr(0, temp.find(DELIMITER_SUBCLASS));
				subclassName = temp.substr(temp.find(DELIMITER_SUBCLASS) + 1);
				isRealSubclass = true;
			} else {
				dataTypeName = subclassName = temp;
				isRealSubclass = false;
			}

			if (isRealSubclass) {
				auto itSubclass = subclass2id.find(subclassName);
				if (itSubclass == subclass2id.end()) {
					cerr << "Unknown subclass in function blacklist, line " << dec << (lineCounter + 1)
						<< ": " << subclassName << endl;
					continue;
				}
				blacklistIDs.push_back(std::to_string(itSubclass->second));
			} else {
				auto itType = type2id.find(dataTypeName);
				if (itType == type2id.end()) {
					cerr << "Unknown data type in function blacklist, line " << dec << (lineCounter + 1)
						<< ": " << dataTypeName << endl;
					continue;
				}
				blacklistIDs.insert(blacklistIDs.end(), itType->second.begin(), itType->second.end());
			}
		} else {
			blacklistIDs.push_back(lineElems.at(0));
		}
		
		string memberID;
		if (lineElems.at(1) != "\\N") {
			auto itMember = memberNames.find(lineElems.at(1));
			if (itMember == memberNames.end()) {
				cerr << "Unknown member name in function blacklist, line " << dec << (lineCounter + 1)
					<< ": " << lineElems.at(1) << endl;
				continue;
			}
			memberID = std::to_string(itMember->second);
		} else {
			memberID = lineElems.at(1);
		}

		for (auto id : blacklistIDs) {
			fnblacklistOFile << fnBlID << delimiter
				<< id << delimiter
				<< memberID << delimiter
				<< lineElems.at(2) << delimiter
				<< lineElems.at(3) << endl;
			fnBlID++;
		}
	}

	blacklistIDs.clear();
	// Process member blacklist
	for (lineCounter = 0;
		getline(memberBlacklistInfile, inputLine);
		ss.clear(), ss.str(""), lineElems.clear(), blacklistIDs.clear(), lineCounter++) {

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
			cerr << "Ignoring invalid member blacklist entry, line " << dec << (lineCounter + 1)
				<< ": " << inputLine << endl;
			continue;
		}

		string dataTypeBl = lineElems.at(0);
		if (dataTypeBl != "\\N") {
			string subclassName, dataTypeName;
			bool isRealSubclass;
			if (dataTypeBl.find(DELIMITER_SUBCLASS) != string::npos) {
				dataTypeName = dataTypeBl.substr(0, dataTypeBl.find(DELIMITER_SUBCLASS));
				subclassName = dataTypeBl.substr(dataTypeBl.find(DELIMITER_SUBCLASS) + 1);
				isRealSubclass = true;
			} else {
				dataTypeName = subclassName = dataTypeBl;
				isRealSubclass = false;
			}

			if (isRealSubclass) {
				auto itSubclass = subclass2id.find(subclassName);
				if (itSubclass == subclass2id.end()) {
					cerr << "Unknown subclass in member blacklist, line " <<  dec << (lineCounter + 1)
						<< ": " << subclassName << endl;
					continue;
				}
				blacklistIDs.push_back(std::to_string(itSubclass->second));
			} else {
				auto itType = type2id.find(dataTypeName);
				if (itType == type2id.end()) {
					cerr << "Unknown data type in member blacklist, line " << dec << (lineCounter + 1)
						<< ": " << dataTypeName << endl;
					continue;
				}
				blacklistIDs.insert(blacklistIDs.end(), itType->second.begin(), itType->second.end());
			}
		} else {
			blacklistIDs.push_back(dataTypeBl);
		}

		auto itMember = memberNames.find(lineElems.at(1));
		if (itMember == memberNames.end()) {
			cerr << "Unknown member in member blacklist, line " << dec <<  (lineCounter + 1)
				<< ": " << lineElems.at(1) << endl;
			continue;
		}

		for (auto id : blacklistIDs) {
			memberblacklistOFile << id << delimiter << itMember->second << endl;
		}
	}

	cerr << "Finished." << endl;

	return EXIT_SUCCESS;
}
