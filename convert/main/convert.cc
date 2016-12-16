#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <unistd.h>
#include <map>
#include <string>
#include <vector>
#include <bfd.h>
#include <stack>
#include "dwarves_api.h"
#include "config.h"


#define IS_MULTILVL_LOCK(x)	((x).ptr == 0x42)

/**
 * Author: Alexander Lochmann 2016
 * Attention: This programm has to be compiled with -std=c++11 !
 * This program takes a csv as input, which contains a series of events.
 * Each event might be of the following types: alloc, free, p(acquiere), v (release), read, or write.
 * The programm groups the alloc and free events as well as the p and v events by their pointer, and assigns an unique id to each unique entitiy (allocation or lock).
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
	unsigned long long ptr;										// The pointer to the memory area, where the lock resides
	int held;													// Indicates wether the lock is held or not
	unsigned long long key;										// A unique id which describes a particular lock within our dataset
	string typeStr;												// Describes the argument provided to the lock function
	int datatype_idx;										    // An index into to types array if the lock resides in an allocation. Otherwise, it'll be -1.
	string lockType;											// Describes the lock type
	stack<LockPos> lastNPos;									// Last N takes of this lock, max. one element besides for recursive locks (such as RCU)
};

/**
 * Describes a certain datatype which is observed by our experiment
 */
struct DataType {
	string typeStr;												// Unique to describe a certain datatype, e.g. task_struct
	bool foundInDw;												// True if the struct has been found in the dwarf information. False otherwise.
	unsigned long long id;										// An unique id for a particular datatype
};

/**
 * Describes an instance of a certain datatype, e.g. an instance of task_struct
 *
 */
struct Allocation {
	unsigned long long start;									// Timestamp when it has been allocated
	unsigned long long id;										// A unique id which refers to that particular instance
	int size;													// Size in bytes
	int idx;													// An index into the types array, which desibes the datatype of this allocation
};

/**
 * Represents a memory access
 */
struct MemAccess {
	unsigned long long id;										// Unique of a certain memory access
	unsigned long long ts;										// Timestamp
	unsigned long long alloc_id;								// Id of the memory allocation which has been accessed
	char action;												// Access type: r or w
	int size;													// Size of memory access
	unsigned long long address;									// Accessed address
	unsigned long long stackPtr;								// Stack pointer
	unsigned long long instrPtr;								// Instruction pointer
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
 * An array of all observed datatypes . For now, we observe three datatypes.
 */
static DataType types[MAX_OBSERVED_TYPES];
/**
 * The list of the LOOK_BEHIND_WINDOW last memory accesses.
 */
static vector<MemAccess> lastMemAccesses;
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
static unsigned long long curLockKey = 1;
/**
 * The next id for a new allocation.
 */
static unsigned long long curAllocKey = 1;
/**
 * The next id for a new memory access.
 */
static unsigned long long curAccessKey = 1;

static struct cus *cus;

static void printUsageAndExit(const char *elf) {
	cerr << "usage: " << elf;
	cerr << " -k <path/to/vmlinux";
	cerr << " inputfile" << endl;
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

static void writeMemAccesses(char pAction, unsigned long long pAddress, ofstream *pMemAccessOFile, vector<MemAccess> *pMemAccesses, ofstream *pLocksHeldOFile, map<unsigned long long,Lock> *pLockPrimKey) {
	map<unsigned long long,Lock>::iterator itLock;
	vector<MemAccess>::iterator itAccess;
	MemAccess window[LOOK_BEHIND_WINDOW];
	int size;

	// Since we want to build up a history of the n last memory accesses, we do nothing if a r or w event is imminent.
	if (pAction == 'r' || pAction == 'w') {
		return;
	}

	if ((pAction == 'p' || pAction == 'v') && pMemAccesses->size() > 0 && pMemAccesses->size() >= LOOK_BEHIND_WINDOW) {
		size = pMemAccesses->size();
		// Have a look at the two last events
		window[0] = pMemAccesses->at(size - 2);
		window[1] = pMemAccesses->at(size - 1);
		// If they have the same timestamp, access the same address, access the same amount of memory, and one is a read and the other event is a write,
		// they'll propably belong to the upcoming acquire or release events.
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
			cout << "Discarding event r+w " << dec << window[0].ts << " reading and writing " << window[0].size  << " bytes at address " << showbase << hex << window[0].address << noshowbase << endl;
#endif
			// Remove the two last memory accesses from the list, because they belong to the upcoming acquire or release on a spinlock.
			// We do *not* want them to be in our dataset.
			pMemAccesses->pop_back();
			pMemAccesses->pop_back();
		}
	}

	for (itAccess = pMemAccesses->begin(); itAccess != pMemAccesses->end(); itAccess++) {
		MemAccess& tempAccess = *itAccess;
		*pMemAccessOFile << dec << tempAccess.id << DELIMITER_CHAR << tempAccess.alloc_id << DELIMITER_CHAR << tempAccess.ts;
		*pMemAccessOFile << DELIMITER_CHAR << tempAccess.action << DELIMITER_CHAR << dec << tempAccess.size;
		*pMemAccessOFile << DELIMITER_CHAR << tempAccess.address << DELIMITER_CHAR << tempAccess.stackPtr << DELIMITER_CHAR << tempAccess.instrPtr;
		*pMemAccessOFile << DELIMITER_CHAR << get_function_at_addr(cus, tempAccess.instrPtr) << "\n";
		// Create an entry for each lock being held
		for (itLock = pLockPrimKey->begin(); itLock != pLockPrimKey->end(); itLock++) {
			Lock& tempLock = itLock->second;
			if ((IS_MULTILVL_LOCK(tempLock) && tempLock.held >= 1) || (!IS_MULTILVL_LOCK(tempLock) && tempLock.held == 1)) {
				LockPos& tempLockPos = itLock->second.lastNPos.top();
				*pLocksHeldOFile << dec << itLock->second.key << DELIMITER_CHAR  << tempAccess.id << DELIMITER_CHAR;
				*pLocksHeldOFile << tempLockPos.start << DELIMITER_CHAR << tempLockPos.lastFile << DELIMITER_CHAR;
				*pLocksHeldOFile << tempLockPos.lastLine << DELIMITER_CHAR << tempLockPos.lastFn << DELIMITER_CHAR;
				*pLocksHeldOFile << tempLockPos.lastPreemptCount << DELIMITER_CHAR << tempLockPos.lastLockFn << "\n";
			}
		}
	}
	// Disabled flush of output files for performance reasons
//	pMemAccessOFile->flush();
//	pLocksHeldOFile->flush();
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
		// Do this compilation unit contain information of at datatype at index i?
		ret = cu__find_struct_by_name(cu, cusIterArgs->types[i].typeStr.c_str(),1,&class_id);
		if (ret == NULL) {
			continue;
		}

		// Is it really a class or a struct?
		if (ret->tag == DW_TAG_class_type ||
			ret->tag == DW_TAG_interface_type ||
			ret->tag == DW_TAG_structure_type) {
			class__fprintf(ret, cu, cusIterArgs->fp, cusIterArgs->types[i].id);
			cusIterArgs->types[i].foundInDw = 1;
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

int main(int argc, char *argv[]) {
	stringstream ss;
	string inputLine, token, typeStr, file, fn, lockfn, lockType;
	vector<string> lineElems; // input CSV columns
	map<unsigned long long,Allocation>::iterator itAlloc;
	map<unsigned long long,Lock>::iterator itLock, itTemp;
	unsigned long long ts, ptr, size = 0, line = 0, address = 0x4711, stackPtr = 0x1337, instrPtr = 0xc0ffee, preemptCount = 0xaa;
	int lineCounter = 0, i;
	char action, param, *vmlinuxName = NULL;

	if (argc < 2) {
		cerr << "Need at least an input file!" << endl;
		return EXIT_FAILURE;
	}

	while ((param = getopt(argc,argv,"k:")) != -1) {
		switch (param) {
		case 'k':
			vmlinuxName = optarg;
			break;
		}
	}
	if (vmlinuxName == NULL || optind == argc) {
		printUsageAndExit(argv[0]);
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

	if (readSections(vmlinuxName)) {
		return EXIT_FAILURE;
	}

	dwarves__init(0);
	cus = cus__new();
	if (cus == NULL) {
		cerr << "Insufficient memory" << endl;
		return -1;
	}

	if (extractStructDefs(cus,vmlinuxName)) {
		return EXIT_FAILURE;
	}

	if (bssStart == 0 || bssSize == 0 || dataStart == 0 || dataSize == 0 ) {
		cerr << "Invalid values for bss start, bss size, data start or data size!" << endl;
		printUsageAndExit(argv[0]);
	}

	ifstream infile(argv[optind]);
	if (!infile.is_open()) {
		cerr << "Cannot open file: " << argv[optind] << endl;
		return EXIT_FAILURE;
	}

	// Create the outputfiles. One for each table.
	ofstream datatypesOFile("data_types.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream allocOFile("allocations.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream accessOFile("accesses.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream locksOFile("locks.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream locksHeldOFile("locks_held.csv",std::ofstream::out | std::ofstream::trunc);
	// Add the header. Hallo, Horst. :)
	datatypesOFile << "id" << DELIMITER_CHAR << "name" << endl;

	allocOFile << "id" << DELIMITER_CHAR << "type_id" << DELIMITER_CHAR << "ptr" << DELIMITER_CHAR;
	allocOFile << "size" << DELIMITER_CHAR << "start" << DELIMITER_CHAR << "end" << endl;

	accessOFile << "id" << DELIMITER_CHAR << "alloc_id" << DELIMITER_CHAR << "ts" << DELIMITER_CHAR;
	accessOFile << "type" << DELIMITER_CHAR << "size" << DELIMITER_CHAR << "address" << DELIMITER_CHAR;
	accessOFile << "stackptr" << DELIMITER_CHAR << "instrptr" << DELIMITER_CHAR << "fn" << endl;

	locksOFile << "id" << DELIMITER_CHAR << "ptr" << DELIMITER_CHAR << "var" << DELIMITER_CHAR;
	locksOFile << "embedded" << DELIMITER_CHAR << "locktype" << endl;

	locksHeldOFile << "lock_id" << DELIMITER_CHAR << "access_id" << DELIMITER_CHAR << "start" << DELIMITER_CHAR;
	locksHeldOFile << "lastFile" << DELIMITER_CHAR << "lastLine" << DELIMITER_CHAR << "lastFn" << DELIMITER_CHAR;
	locksHeldOFile << "lastPreemptCount" << DELIMITER_CHAR << "lastLockFn" << endl;

	for (i = 0; i < MAX_OBSERVED_TYPES; i++) {
		// The unique id for each datatype will be its index + 1
		datatypesOFile << types[i].id << DELIMITER_CHAR << types[i].typeStr << endl;
	}

	// Start reading the inputfile
	for (;getline(infile,inputLine); ss.clear(), ss.str(""), lineElems.clear(), lineCounter++) {
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
		// Skip the header
		if (lineCounter == 0) {
			continue;
		}
		if (lineCounter % 100 == 0) {
			allocOFile.flush();
			accessOFile.flush();
			locksOFile.flush();
			locksHeldOFile.flush();
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

		writeMemAccesses(action, address, &accessOFile, &lastMemAccesses, &locksHeldOFile, &lockPrimKey);
		switch (action) {
		case 'a':
				{
				if (activeAllocs.find(ptr) != activeAllocs.end()) {
					cerr << "Found active allocation at address " << showbase << hex << ptr << noshowbase << PRINT_CONTEXT << endl;
					continue;
				}
				// Do we know that datatype?
				for (i = 0; i < MAX_OBSERVED_TYPES; i++) {
					if (types[i].typeStr.compare(typeStr) == 0) {
						break;
					}
				}
				if (i >= MAX_OBSERVED_TYPES) {
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
				tempAlloc.id = curAllocKey++;
				tempAlloc.start = ts;
				tempAlloc.idx = i;
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
					if (itLock->second.ptr >= itAlloc->first && itLock->second.ptr <= (itAlloc->first + tempAlloc.size)) {
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
						itLock->second.typeStr = typeStr;
						if (ptr == 0x42) {
							itLock->second.held++;
						} else {
							// Has it already been acquired?
							if (itLock->second.held != 0) {
								cerr << "Lock at address " << showbase << hex << ptr << noshowbase << " is already being held!" << PRINT_CONTEXT << endl;
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
					} else if (action == 'v') {
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
						if (!itLock->second.lastNPos.empty()) {
							itLock->second.lastNPos.pop();
						} else {
							cerr << "No last locking position known for lock at address " << showbase << hex << ptr << noshowbase << ", cannot pop." << PRINT_CONTEXT << endl;
						}
					}
					// Since the lock alreadys exists, and the metainformation has been updated, no further action is required
					continue;
				}

				// categorize currently unknown lock
				if ((ptr >= bssStart && ptr <= bssStart + bssSize) || ( ptr >= dataStart && ptr <= dataStart + dataSize) || (typeStr.compare("static") == 0 && ptr == 0x42)) {
					// static lock which resides either in the bss segment or in the data segment
					// or global static lock aka rcu lock
#ifdef VERBOSE
					cout << "Found static lock: " << showbase << hex << ptr << noshowbase << endl;
#endif
					// -1 indicates an static lock
					i = -1;
				} else {
					// A lock which probably resides in one of the observed allocations. If not, we don't care!
					i = -1; // Use variable i as an indicator if an allocation has been found
					itAlloc = activeAllocs.upper_bound(ptr);
					if (itAlloc != activeAllocs.begin()) {
						itAlloc--;
					    if (ptr <= itAlloc->first + itAlloc->second.size) {
							i = itAlloc->second.id;
						}
					}
					if (i == -1) {
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
				tempLock.key = curLockKey++;
				tempLock.datatype_idx = i;
				tempLock.lockType = lockType;
				tempLock.typeStr = typeStr;
				tempLock.lastNPos.push(LockPos());
				LockPos& tempLockPos = tempLock.lastNPos.top();
				tempLockPos.start = ts;
				tempLockPos.lastLine = line;
				tempLockPos.lastFile = file;
				tempLockPos.lastFn = fn;
				tempLockPos.lastLockFn = lockfn;
				tempLockPos.lastPreemptCount = preemptCount;

				locksOFile << dec << tempLock.key << DELIMITER_CHAR << tempLock.typeStr << DELIMITER_CHAR << tempLock.ptr << DELIMITER_CHAR;
				if (tempLock.datatype_idx == -1) {
					locksOFile << "-1";
				} else {
					locksOFile << tempLock.datatype_idx;
				}
				locksOFile << DELIMITER_CHAR << tempLock.lockType << "\n";
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
					continue;
					return EXIT_FAILURE;
				}

				lastMemAccesses.push_back(MemAccess());
				MemAccess& tempAccess = lastMemAccesses.back();
				tempAccess.id = curAccessKey++;
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
	// Due to the fact that we abort the expriment as soon as the benchmark has finished, some allocations may not have been freed.
	// Hence, print every allocation, which is still stored in the map, and set the freed timestamp to NULL.
	for (itAlloc = activeAllocs.begin(); itAlloc != activeAllocs.end(); itAlloc++) {
		Allocation& tempAlloc = itAlloc->second;
		allocOFile << tempAlloc.id << DELIMITER_CHAR << types[tempAlloc.idx].id << DELIMITER_CHAR << itAlloc->first << DELIMITER_CHAR;
		allocOFile << dec << tempAlloc.size << DELIMITER_CHAR << dec << tempAlloc.start << DELIMITER_CHAR << "-1" << "\n";
	}

	infile.close();
	datatypesOFile.close();
	allocOFile.close();
	accessOFile.close();
	locksOFile.close();
	locksHeldOFile.close();

	cus__delete(cus);
	dwarves__exit();

	cerr << "Finished." << endl;

	return EXIT_SUCCESS;
}
