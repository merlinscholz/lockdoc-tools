#ifndef __BINARYREAD_H__
#define __BINARYREAD_H__

#include "dwarves_api.h"
#include <vector>

using std::vector;

struct CodeLocation {
	const char *fn;
	const char *file;
	int line;
};

struct ResolvedInstructionPtr {
	struct CodeLocation codeLocation;
	std::vector<struct CodeLocation> inlinedBy;
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

int binaryread_init(const char *vmlinuxName);
void binaryread_destroy(void);
const struct ResolvedInstructionPtr& get_function_at_addr(const char *compDir, uint64_t addr);
int readSections(uint64_t& bssStart, uint64_t& bssSize, uint64_t& dataStart, uint64_t& dataSize);
const char* getGlobalLockVar(uint64_t addr);
int extractStructDefs(const char *outFname, char delimiter, std::vector<DataType> *types, expand_type_fn expand_type, add_member_name_fn add_member_name);
#endif // __BINARYREAD_H__
