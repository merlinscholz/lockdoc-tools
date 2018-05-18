#include <map>
#include <vector>
#include <iostream>
#include <bfd.h>
#include <cstring>

#include "binaryread.h"
#include "config.h"
#include "dwarves_api.h"

using namespace std;

/**
 * Passes context information to find_address_in_section
 * which resolves a instruction pointer to a code location.
 * @pc: The instruction pointer
 * @found: Indicates that the locations has already been found.
 * @file: Source file
 * @fn: Function name
 * @line: Line number in @file
 */
struct BfdSearchCtx {
	bfd_vma pc;
	bfd_boolean found;
	const char *file;
	const char *fn;
	unsigned int line;
};
/**
 * Used to pass context information to the dwarves callback.
 * Have a look at convert_cus_iterator().
 */
struct CusIterArgs {
	std::vector<DataType> *types = nullptr;
	expand_type_fn expand_type;
	add_member_name_fn add_member_name;
	FILE *fp = nullptr;
};
/**
 * Passes context information to the findGlobalLockVar function
 * @addr: Address of the lock variable we want to resolve
 * @lockVarname: The variable the lock with @addr resides in
 */
struct lockVarSearch {
	uint64_t addr;
	const char* lockVarName;
};

/**
 * Symbol table used by bfd
 */
static asymbol **bfdSyms;
/**
 * address -> code location cache
 */
static std::map<uint64_t, InstructionPointerInfo> functionAddresses;
/**
 * A dwarves descriptor for the vmlinux
 */
static struct cus *kernelCUs;
/**
 * A bfd descriptor for the vmlinux
 */
static bfd *kernelBfd;

static int findGlobalLockVar(struct cu *cu, void *cookie) {
	uint32_t i;
	struct tag *pos;
	struct lockVarSearch *lockVar = (struct lockVarSearch*)cookie;

	cu__for_each_variable(cu, i, pos) {
		struct variable *var = tag__variable(pos);
		
		// Ensure that this definition has valid location information.
		// The address and the size of a DW_AT_variable definition is valid
		// if DW_AT_location and DW_OP_addr are present.
		// --> var->location is equal LOCATION_GLOBAL.
		// For more information look dwarf__location@dwarf_loader.c:572
		if (var->location != LOCATION_GLOBAL) {
			// No valid location information. Skip this DW_AT_variable.
			continue;
		}
		if (!var->declaration && // Is this a variable definition (--> !declaration)?
			var->name != 0 && // Does this DW_AT_variable have a name?
			lockVar->addr >= var->ip.addr && lockVar->addr < (var->ip.addr + tag__size(pos, cu))) {
			lockVar->lockVarName = variable__name(var, cu);
			PRINT_DEBUG("", hex << showbase << "addr=" << var->ip.addr << ",size=" << dec << tag__size(pos, cu) << " --> " << lockVar->lockVarName);
			return 1;
		}
	}
	return 0;
}

const char* getGlobalLockVar(uint64_t addr) {
	struct lockVarSearch lockVar = { 0 };
	lockVar.addr = addr;

	cus__for_each_cu(kernelCUs, findGlobalLockVar, &lockVar, NULL);

	return lockVar.lockVarName;
}

static int convert_cus_iterator(struct cu *cu, void *cookie) {
	uint16_t class_id;
	struct tag *ret;
	CusIterArgs *cusIterArgs = (CusIterArgs*)cookie;
	struct dwarves_convert_ext dwarvesconfig = { 0 }; // initializes all members

	// Setup callback
	dwarvesconfig.expand_type = cusIterArgs->expand_type;
	dwarvesconfig.add_member_name = cusIterArgs->add_member_name;

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

int extractStructDefs(const char *outFname, char delimiter, std::vector<DataType> *types, expand_type_fn expand_type, add_member_name_fn add_member_name) {
	CusIterArgs cusIterArgs;
	FILE *structsLayoutOFile;

	// Open the output file and add the header
	structsLayoutOFile = fopen(outFname, "w+");
	if (structsLayoutOFile == NULL) {
		perror("fopen structs_layout.csv");
		return 1;
	}
	fprintf(structsLayoutOFile,
		"type_id%ctype%cmember%coffset%csize\n",delimiter,delimiter,delimiter,delimiter);

	// Pass the context information to the callback: types array and the outputfile
	cusIterArgs.types = types;
	cusIterArgs.fp = structsLayoutOFile;
	cusIterArgs.expand_type = expand_type;
	cusIterArgs.add_member_name = add_member_name;
	// Iterate through every compilation unit, and look for information about the datatypes of interest
	cus__for_each_cu(kernelCUs, convert_cus_iterator, &cusIterArgs, NULL);

	fclose(structsLayoutOFile);

	return 0;
}

/* Copied from binutils-2.28/addr2line.c */
static int slurp_symtab (bfd *abfd)
{ 
	long storage;
	long symcount;
	bfd_boolean dynamic = FALSE;

	if ((bfd_get_file_flags (abfd) & HAS_SYMS) == 0) {
		return 1;
	}

	storage = bfd_get_symtab_upper_bound (abfd);
	if (storage == 0) { 
		storage = bfd_get_dynamic_symtab_upper_bound (abfd);
		dynamic = TRUE;
	}
	if (storage < 0) {
		return 1;
	}

	bfdSyms = (asymbol **) malloc (storage);
	if (dynamic) {
		symcount = bfd_canonicalize_dynamic_symtab (abfd, bfdSyms);
	} else {
		symcount = bfd_canonicalize_symtab (abfd, bfdSyms);
	}
	if (symcount < 0) {
		return 1;
	}

	/* If there are no symbols left after canonicalization and
	 we have not tried the dynamic symbols then give them a go.  */
	if (symcount == 0
	  && ! dynamic
	  && (storage = bfd_get_dynamic_symtab_upper_bound (abfd)) > 0) { 
		free (bfdSyms);
		bfdSyms = (asymbol**)malloc (storage);
		symcount = bfd_canonicalize_dynamic_symtab (abfd, bfdSyms);
	}

	/* PR 17512: file: 2a1d3b5b.
	 Do not pretend that we have some symbols when we don't.  */
	if (symcount <= 0) {
		free (bfdSyms);
		bfdSyms = NULL;
		return 1;
	}
	return 0;
}

/* Copied from binutils-2.28/addr2line.c */
static void find_address_in_section (bfd *kernelBfd, asection *section, void *data)
{
	bfd_vma vma;
	bfd_size_type size;
	BfdSearchCtx *bfdSearchCtx = (BfdSearchCtx*)data;

	if (bfdSearchCtx->found) {
		return;
	}

	if ((bfd_get_section_flags (kernelBfd, section) & SEC_ALLOC) == 0) {
		return;
	}

	vma = bfd_get_section_vma (kernelBfd, section);
	if (bfdSearchCtx->pc < vma) {
		return;
	}

	size = bfd_get_section_size (section);
	if (bfdSearchCtx->pc >= vma + size) {
		return;
	}

	bfdSearchCtx->found = bfd_find_nearest_line_discriminator (kernelBfd, section, bfdSyms, bfdSearchCtx->pc - vma,
															   &bfdSearchCtx->file, &bfdSearchCtx->fn,
															   &bfdSearchCtx->line, NULL);
}

// caching wrapper around cus__get_function_at_addr
const struct InstructionPointerInfo& get_function_at_addr(const char *compDir, uint64_t addr)
{
	auto it = functionAddresses.find(addr);
	if (it == functionAddresses.end()) {
		BfdSearchCtx bfdSearchCtx;
		memset(&bfdSearchCtx, 0, sizeof(bfdSearchCtx));
		
		bfdSearchCtx.pc = addr;
		bfd_map_over_sections (kernelBfd, find_address_in_section, &bfdSearchCtx);

		if (bfdSearchCtx.found) {
			functionAddresses[addr].line = bfdSearchCtx.line;
			if (bfdSearchCtx.file) {
				const char *tmp = strstr(bfdSearchCtx.file, compDir);
				if (tmp) {
					functionAddresses[addr].file = bfdSearchCtx.file + strlen(compDir);
				} else {
					functionAddresses[addr].file = bfdSearchCtx.file;
				}
			} else {
				functionAddresses[addr].file = "unknown";
			}
			if (bfdSearchCtx.fn) {
				functionAddresses[addr].fn = bfdSearchCtx.fn;
			} else {
				functionAddresses[addr].fn = "unknown";
			}
		} else {
			functionAddresses[addr].fn = "unknown";
			functionAddresses[addr].file = "unknown";
			functionAddresses[addr].line = 0;
		}
		return functionAddresses[addr];
	}
	return it->second;
} 

// find .bss and .data sections
int readSections(uint64_t& bssStart, uint64_t& bssSize, uint64_t& dataStart, uint64_t& dataSize) {
	asection *bsSection, *dataSection;

	bsSection = bfd_get_section_by_name(kernelBfd,".bss");
	if (bsSection == NULL) {
		bfd_perror("Cannot find section '.bss'");
		bfd_close(kernelBfd);
		return 1;
	}
	bssStart = bfd_section_vma(kernelBfd, bsSection);
	bssSize = bfd_section_size(kernelBfd, bsSection);
	cout << bfd_section_name(kernelBFD,bsSection) << ": " << bssSize << " bytes @ " << hex << showbase << bssStart << dec << noshowbase << endl;

	dataSection = bfd_get_section_by_name(kernelBfd,".data");
	if (bsSection == NULL) {
		bfd_perror("Cannot find section '.bss'");
		bfd_close(kernelBfd);
		return 1;
	}
	dataStart = bfd_section_vma(kernelBfd, dataSection);
	dataSize = bfd_section_size(kernelBfd,dataSection);
	cout << bfd_section_name(kernelBFD,dataSection) << ": " << dataSize << " bytes @ " << hex << showbase << dataStart << dec << noshowbase << endl;

	return 0;
}


int binaryread_init(const char *vmlinuxName) {
	// Init bfd
	bfd_init();
	kernelBfd = bfd_openr(vmlinuxName,"elf32-i386");
	if (kernelBfd == NULL) {
		bfd_perror("open vmlinux");
		return 1;
	}
	// This check is not only a sanity check. Moreover, it is necessary
	// to allow looking up of sections.
	if (!bfd_check_format (kernelBfd, bfd_object)) {
		cerr << "bfd: unknown format" << endl;
		bfd_close(kernelBfd);
		return 1;
	}
	if (slurp_symtab(kernelBfd)) {
		fprintf(stderr, "slurp_symtab(..) failed!\n");
		return 1;
	}

	// Init dwarves
	dwarves__init(0);
	kernelCUs = cus__new();
	if (kernelCUs == NULL) {
		cerr << "Insufficient memory" << endl;
		return 1;
	}
	// Load the dwarf information of every compilation unit
	struct conf_load confLoad;

	memset(&confLoad, 0, sizeof(confLoad));
	confLoad.get_addr_info = true;
	confLoad.extra_dbg_info = true;

	if (cus__load_file(kernelCUs, &confLoad, vmlinuxName) != 0) {
		cerr << "No debug information found in " << vmlinuxName << endl;
		return 1;
	}
	
	return 0;
}

void binaryread_destroy(void) {
	if (bfdSyms != NULL) {
		free(bfdSyms);
	}
	bfd_close(kernelBfd);

	cus__delete(kernelCUs);
	dwarves__exit();
}
