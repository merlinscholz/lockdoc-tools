#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <unistd.h>
#include <map>
#include <string>
#include <vector>

/**
 * Author: Alexander Lochmann 2016
 * Attention: This programm has to be compiled with -std=c++11 !
 * This program takes a csv as input, which contains a series of events.
 * Each event might be of the following types: alloc, free, p(acquiere), v (release), read, or write.
 * The programm groups the alloc and free events as well as the p and v events by their pointer, and assigns an unique id to each unique entitiy (allocation or lock).
 * For each read or write access it generates the 
 * OUTPUT: TODO
 */


/*
 * bss start: nm /media/playground/kernel/linux-4.3/vmlinux | grep __bss_start | cut -f1 -d' '
 * bss size: size /media/playground/kernel/linux-4.3/vmlinux | tail -n 1| tr '\t' ',' | cut -f3 -d,
 * data start: nm /media/playground/kernel/linux-4.3/vmlinux | grep __data_start | cut -f1 -d' '
 * data size: size /media/playground/kernel/linux-4.3/vmlinux | tail -n 1| tr '\t' ',' | cut -f2 -d,
 * 
 * both bss and data: size -A /path/to/vmlinux
 */

#define MAX_OBSERVED_TYPES 3
#define DELIMITER ';'
#define NEED_BSS_DATA
#define MAX_COLUMNS 13
//#define VERBOSE
#define PRINT_KONTEXT " (action=" << action << ",type=" << typeStr << ",ts=" << dec << ts << ")"

using namespace std;

enum AccessTypes {
	ACCESS = 0,
	READ,
	WRITE,
	TYPES_END
};

/**
 * Describes an instance of a lock
 */
struct Lock {
	unsigned long long ptr;										// The pointer to the memory area, where the lock resides
	int held;													// Indicates wether the lock is held or not
	unsigned long long key;										// A unique id which describes a particular lock within our dataset
	string typeStr;												// Describes the argument provided to the lock function
	unsigned long long start;									// Timestamp when the lock has been acquired
	int lastLine;												// Position within the file where the lock has been acquired for the last time
	string lastFile;											// Last file from where the lock has been acquired
	string lastFn;												// Last caller
	string lastLockFn;											// Lock function used the last time
	int datatype_idx;										// An index into to types array if the lock resides in an allocation. Otherwise, it'll be -1.
	string lockType;
};

/**
 * Describes a certain datatype which is observed by our experiment
 */
struct DataType {
	string typeStr;												// Unique to describe a certain datatype, e.g. task_struct
	map<unsigned long long,int> histogram[TYPES_END];			// Count which lock has been held for each type of memory access. (--> enum AccessTypes)
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
 * Contains all known locks. The ptr of a lock is used as an index.
 */
static map<int,Lock> lockPrimKey;
/**
 * Contains all active allocations. The ptr to the memory area is used as an index.
 */
static map<unsigned long long,Allocation> activeAllocs;
/**
 * An array of all observed datatypes . For now, we observe three datatypes.
 */
static DataType types[MAX_OBSERVED_TYPES];

//
static unsigned long long curLockKey = 1;
static unsigned long long curAllocKey = 1;
static unsigned long long curAccessKey = 1;

static void printUsageAndExit(const char *elf) {
	cerr << "usage: " << elf;
#ifdef NEED_BSS_DATA
	cerr << " -b <bss_start>:<bss_size> -d <data_start>:<data_size>";
#endif
	cerr << " inputfile" << endl;
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
	stringstream ss;
	string inputLine, token, typeStr, file, fn, lockfn, lockType;
	vector<string> lineElems;
	Allocation tempAlloc;
	Lock tempLock;
	pair<map<unsigned long long,Allocation>::iterator,bool> retAlloc;
	map<unsigned long long,Allocation>::iterator itAlloc;
	pair<map<int,Lock>::iterator,bool> retLock;
	map<int,Lock>::iterator itLock, itTemp;
	unsigned long long ts, ptr, size = 0, line = 0, address, stackPtr, instrPtr;
#ifdef NEED_BSS_DATA
	unsigned long long bssStart = 0, bssSize = 0, dataStart = 0, dataSize = 0;
#endif
	int lineCounter = 0, i;
	char action, param;
	
	if (argc < 2) {
		cerr << "Need at least an input file!" << endl;
		return EXIT_FAILURE;
	}

#ifdef NEED_BSS_DATA
	while ((param = getopt(argc,argv,"b:d:")) != -1) {
		switch (param) {
			case 'b':
				token.clear();
				token.append(optarg);
				if (token.find(":") == string::npos) {
					printUsageAndExit(argv[0]);
				}
				bssStart = std::stoull(token.substr(0,token.find(":")),NULL,10);
				bssSize = std::stoull(token.substr(token.find(":") + 1),NULL,10);
				break;
		
			case 'd':
				token.clear();
				token.append(optarg);
				if (token.find(":") == string::npos) {
					printUsageAndExit(argv[0]);
				}
				dataStart = std::stoull(token.substr(0,token.find(":")),NULL,10);
				dataSize = std::stoull(token.substr(token.find(":") + 1),NULL,10);				
				break;
		}
	}
	
	if (bssStart == 0 || bssSize == 0 || dataStart == 0 || dataSize == 0 ) {
		cerr << "Invalid values for bss start, bss size, data start or data size!" << endl;
		printUsageAndExit(argv[0]);
	}
	if (optind == argc) {
		printUsageAndExit(argv[0]);
	}
#endif	
	
	types[0].typeStr = "task_struct";
	types[1].typeStr = "inode";
	types[2].typeStr = "super_block";
	
#ifdef NEED_BSS_DATA
	ifstream infile(argv[optind]);
#else
	ifstream infile(argv[0]);
#endif
	if (!infile.is_open()) {
		cerr << "Cannot open file: " << argv[1] << endl;
		return EXIT_FAILURE;
	}
	
	// Create the outputfiles. One for each table.
	ofstream datatypesOFile("data_types.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream allocOFile("allocations.csv",std::ofstream::out | std::ofstream::trunc);	
	ofstream accessOFile("accesses.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream locksOFile("locks.csv",std::ofstream::out | std::ofstream::trunc);
	ofstream locksHeldOFile("locks_held.csv",std::ofstream::out | std::ofstream::trunc);
	// Add the header. Hallo, Horst. :)
	datatypesOFile << "id" << DELIMITER << "name" << endl;
	allocOFile << "id" << DELIMITER << "type_id" << DELIMITER << "ptr" << DELIMITER << "size" << DELIMITER << "start" << DELIMITER << "end" << endl;
	accessOFile << "id" << DELIMITER << "alloc_id" << DELIMITER << "type" << DELIMITER << "address" << DELIMITER << "stackptr" << DELIMITER << "instrptr" << endl;
	locksOFile << "id" << DELIMITER << "ptr" << DELIMITER << "var" << DELIMITER << "embedded" << DELIMITER << "locktype" << endl;
	locksHeldOFile << "lock_id" << DELIMITER << "access_id" << DELIMITER << "start" << endl;

	for (i = 0; i < MAX_OBSERVED_TYPES; i++) {
		// The unique id for each datatype will be its index + 1
		datatypesOFile << i + 1 << DELIMITER << types[i].typeStr << endl;
	}

	// Start reading the inputfile
	for (;getline(infile,inputLine); ss.clear(), lineElems.clear(), lineCounter++) {
			// Skip the header
			if (lineCounter == 0) {
				continue;
			}

			ss << inputLine;
			// Tokenize each line by DELIMITER, and store each element in a vector
			while (getline(ss,token,DELIMITER)) {
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
					address = std::stoull(lineElems.at(10),NULL,16);
				} else {
					address = 0x4711;
				}
				if (lineElems.at(11).compare("NULL") != 0) {
					instrPtr = std::stoull(lineElems.at(11),NULL,16);
				} else {
					instrPtr = 0x1337;
				}
				if (lineElems.at(12).compare("NULL") != 0) {
					stackPtr = std::stoull(lineElems.at(12),NULL,16);
				} else {
					stackPtr = 0xc0ffee;
				}
				
			} catch (exception &e) {
				cerr << "Exception occured (ts="<< ts << "): " << e.what() << endl;
			}
				
			switch(action) {
					case 'a':
							if (activeAllocs.find(ptr) != activeAllocs.end()) {
								cerr << "Found active allocation at address " << showbase << hex << ptr << noshowbase << PRINT_KONTEXT << endl;
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
							tempAlloc.id = curAllocKey++;
							tempAlloc.start = ts;
							tempAlloc.idx = i;
							tempAlloc.size = size;
							retAlloc = activeAllocs.insert(pair<unsigned long long,Allocation>(ptr,tempAlloc));
							if (!retAlloc.second) {
								cerr << "Cannot insert allocation into map: " << showbase << hex << ptr << noshowbase << PRINT_KONTEXT << endl;
							}
							break;

					case 'f':
							itAlloc = activeAllocs.find(ptr);
							if (itAlloc == activeAllocs.end()) {
								cerr << "Didn't find active allocation for address " << showbase << hex << ptr << noshowbase << PRINT_KONTEXT << endl;
								continue;
							}
							tempAlloc = itAlloc->second;
							// An allocations datatype is 
							allocOFile << tempAlloc.id << DELIMITER << tempAlloc.idx + 1 << DELIMITER << ptr << DELIMITER << dec << size << DELIMITER << dec << tempAlloc.start << DELIMITER << ts << endl;
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

					case 'v':
					case 'p':
							if ((ptr >= bssStart && ptr <= bssStart + bssSize) || ( ptr >= dataStart && ptr <= dataStart) || (typeStr.compare("static") && ptr == 0x42)) {
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
								for (itAlloc = activeAllocs.begin(); itAlloc != activeAllocs.end(); itAlloc++) {
									if (ptr >= itAlloc->first && ptr <= itAlloc->first + itAlloc->second.size) {
										i = itAlloc->second.idx;
										break;
									}
								}
								if (i < 0) {
#ifdef VERBOSE
									cerr << "Lock at address " << showbase << hex << ptr << noshowbase << " does not belong to any of the observed memory regions. Ignoring it." << PRINT_KONTEXT << endl;
#endif
									continue;
								}
							}
							itLock = lockPrimKey.find(ptr);
							if (itLock != lockPrimKey.end()) {
								if (action == 'p') {
									// Found a known lock. Just update its metainformation
#ifdef VERBOSE
									cerr << "Found existing lock at address " << showbase << hex << ptr << noshowbase << ". Just updating the meta information." << endl;
#endif
									itLock->second.typeStr = typeStr;
									itLock->second.start = ts;
									itLock->second.lastLine = line;
									itLock->second.lastFile = file;
									itLock->second.lastFn = fn;
									itLock->second.lastLockFn = lockfn;
									// Has it already been acquired?
									if (itLock->second.held != 0) {
										cerr << "Lock at address " << showbase << hex << ptr << noshowbase << " is already held!" << PRINT_KONTEXT << endl;
									}
									itLock->second.held = 1;
								} else if (action == 'v') {
									// Has it already been released?
									if (itLock->second.held != 1) {
										cerr << "Lock at address " << showbase << hex << ptr << noshowbase << " has already been released." << PRINT_KONTEXT << endl;
									}
									itLock->second.held = 0;
								}
								// Since the lock alreadys exists, and the metainformation has been updated, no further action is required
								continue;
							} else if (action == 'v') {
								cerr << "Cannot find a lock at address " << showbase << hex << ptr << noshowbase << PRINT_KONTEXT << endl;
								continue;
							}
							tempLock.ptr = ptr;
							tempLock.held = 1;
							tempLock.key = curLockKey++;
							tempLock.typeStr = typeStr;
							tempLock.start = ts;
							tempLock.lastLine = line;
							tempLock.lastFile = file;
							tempLock.lastFn = fn;
							tempLock.lastLockFn = lockfn;
							tempLock.datatype_idx = i;
							tempLock.lockType = lockType;
							// Insert lock into map, and write entry to file
							retLock = lockPrimKey.insert(pair<unsigned long long,Lock>(ptr,tempLock));
							if (!retLock.second) {
								cerr << "Cannot insert lock into map: " << showbase << hex << ptr << noshowbase << PRINT_KONTEXT << endl;
							}
							locksOFile << dec << tempLock.key << DELIMITER << tempLock.typeStr << DELIMITER << tempLock.ptr << DELIMITER;
							if (tempLock.datatype_idx == -1) {
								locksOFile << "NULL";
							} else {
								// datatype_idx is an index into to datatypes array. Since the idx should be an id for the database, it is incremented by one.
								// Thus, index 0 will be 1 and so on.
								locksOFile <<  tempLock.datatype_idx + 1;
							}
							locksOFile << DELIMITER << tempLock.lockType << endl;
							break;

					case 'w':
					case 'r':
							itAlloc = activeAllocs.find(ptr);
							if (itAlloc == activeAllocs.end()) {
								cerr << "Didn't find active allocation for address " << showbase << hex << ptr << noshowbase << PRINT_KONTEXT << endl;
								continue;
							}
							i = curAccessKey++;
							accessOFile << dec << i << DELIMITER << itAlloc->second.id << DELIMITER << action << DELIMITER << dec << size << DELIMITER << address << DELIMITER << stackPtr << DELIMITER << instrPtr << endl;	
							// Create an entry for each held lock
							for (itLock = lockPrimKey.begin(); itLock != lockPrimKey.end(); itLock++) {
								if (itLock->second.held == 1) {
									locksHeldOFile << dec << itLock->second.key << DELIMITER  << i << DELIMITER  << itLock->second.start << endl;
								}
							}
							break;
			}
	}
	// Due to the fact that we abort the expriment as soon as the benchmark has finished, some allocations may not have been freed.
	// Hence, print every allocation, which is still stored in the map, and set the freed timestamp to NULL.
	for (itAlloc = activeAllocs.begin(); itAlloc != activeAllocs.end(); itAlloc++) {
		tempAlloc = itAlloc->second;
		allocOFile << tempAlloc.id << DELIMITER << types[tempAlloc.idx].typeStr << DELIMITER << itAlloc->first << DELIMITER;
		allocOFile << dec << tempAlloc.size << DELIMITER << dec << tempAlloc.start << DELIMITER << "NULL" << endl;
	}
	
	infile.close();
	datatypesOFile.close();
	allocOFile.close();
	accessOFile.close();
	locksOFile.close();
	locksHeldOFile.close();
	
	return EXIT_SUCCESS;
}
