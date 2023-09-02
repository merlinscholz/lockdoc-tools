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
 * Size of symbol table @bfdSyms
 */
static long bfdSymcount;
/**
 * A cache for global symbols: addr --> symbol name
 */
static map<uint64_t, const char*> addrToSym;
/**
 * address -> code location cache
 */
static std::map<uint64_t, ResolvedInstructionPtr> functionAddresses;
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

	if (lockVar.lockVarName == NULL) {
		auto it = addrToSym.find(addr);
		if (it != addrToSym.end()) {
			lockVar.lockVarName = it->second;
		}
	}
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

	// smmescho: I have no clue why this has to be commented out,
	// as this codepath should be the very first one to write to the csv
	
	//fprintf(structsLayoutOFile,
	//	"data_type_id%cdata_type_name%cmember_name_id%cbyte_offset%csize\n",delimiter,delimiter,delimiter,delimiter);

	// Pass the context information to the callback: types array and the outputfile
	cusIterArgs.types = types;
	cusIterArgs.fp = structsLayoutOFile;
	cusIterArgs.expand_type = expand_type;
	cusIterArgs.add_member_name = add_member_name;
	// Iterate through every compilation unit, and look for information about the datatypes of interest
	cus__for_each_cu(kernelCUs, convert_cus_iterator, &cusIterArgs, NULL);

	//fclose(structsLayoutOFile);

	return 0;
}

/* Copied from binutils-2.28/addr2line.c */
static int slurp_symtab (bfd *abfd)
{ 
	long storage;
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
		bfdSymcount = bfd_canonicalize_dynamic_symtab (abfd, bfdSyms);
	} else {
		bfdSymcount = bfd_canonicalize_symtab (abfd, bfdSyms);
	}
	if (bfdSymcount < 0) {
		return 1;
	}

	/* If there are no symbols left after canonicalization and
	 we have not tried the dynamic symbols then give them a go.  */
	if (bfdSymcount == 0
	  && ! dynamic
	  && (storage = bfd_get_dynamic_symtab_upper_bound (abfd)) > 0) { 
		free (bfdSyms);
		bfdSyms = (asymbol**)malloc (storage);
		bfdSymcount = bfd_canonicalize_dynamic_symtab (abfd, bfdSyms);
	}

	/* PR 17512: file: 2a1d3b5b.
	 Do not pretend that we have some symbols when we don't.  */
	if (bfdSymcount <= 0) {
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

	if ((bfd_section_flags (section) & SEC_ALLOC) == 0) {
		return;
	}

	vma = bfd_section_vma (section);
	if (bfdSearchCtx->pc < vma) {
		return;
	}

	size = bfd_section_size (section);
	if (bfdSearchCtx->pc >= vma + size) {
		return;
	}

	bfdSearchCtx->found = bfd_find_nearest_line_discriminator (kernelBfd, section, bfdSyms, bfdSearchCtx->pc - vma,
															   &bfdSearchCtx->file, &bfdSearchCtx->fn,
															   &bfdSearchCtx->line, NULL);
}

// caching wrapper around cus__get_function_at_addr
const struct ResolvedInstructionPtr& get_function_at_addr(const char *compDir, uint64_t addr)
{
	auto it = functionAddresses.find(addr);
	if (it == functionAddresses.end()) {
		BfdSearchCtx bfdSearchCtx;
		memset(&bfdSearchCtx, 0, sizeof(bfdSearchCtx));
		
		bfdSearchCtx.pc = addr;
		bfd_map_over_sections (kernelBfd, find_address_in_section, &bfdSearchCtx);

		if (bfdSearchCtx.found) {
			vector<struct CodeLocation>& inlinedBy = functionAddresses[addr].inlinedBy;
			bfd_boolean foundInline = FALSE;
			int i = 0;

			functionAddresses[addr].codeLocation.line = bfdSearchCtx.line;
			if (bfdSearchCtx.file) {
				const char *tmp = strstr(bfdSearchCtx.file, compDir);
				size_t len = strlen(compDir);
				if (tmp) {
					// If compDir does *not* end with a slash, remove one more char from the filename.
					// Otherwise, the resulting filename will start with a slash.
					if (compDir[len - 1] != '/') {
						functionAddresses[addr].codeLocation.file = bfdSearchCtx.file + len + 1;
					} else {
						functionAddresses[addr].codeLocation.file = bfdSearchCtx.file + len;
					}
				} else {
					functionAddresses[addr].codeLocation.file = bfdSearchCtx.file;
				}
			} else {
				functionAddresses[addr].codeLocation.file = "unknown";
			}
			if (bfdSearchCtx.fn) {
				functionAddresses[addr].codeLocation.fn = bfdSearchCtx.fn;
			} else {
				functionAddresses[addr].codeLocation.fn = "unknown";
			}
			while (1) {
				// Re-use bfdSearchCtx
				foundInline = bfd_find_inliner_info(kernelBfd, &bfdSearchCtx.file, &bfdSearchCtx.fn, &bfdSearchCtx.line);
				if (foundInline == TRUE) {
					inlinedBy.push_back(CodeLocation());

					const char *tmp = strstr(bfdSearchCtx.file, compDir);
					if (tmp) {
						inlinedBy[i].file = bfdSearchCtx.file + strlen(compDir);
					} else {
						inlinedBy[i].file = bfdSearchCtx.file;
					}
					if (bfdSearchCtx.fn) {
						inlinedBy[i].fn = bfdSearchCtx.fn;
					} else {
						inlinedBy[i].fn = "unknown";
					}
					inlinedBy[i].line = bfdSearchCtx.line;
					i++;
				} else {
					break;
				}
			}
		} else {
			functionAddresses[addr].codeLocation.fn = "unknown";
			functionAddresses[addr].codeLocation.file = "unknown";
			functionAddresses[addr].codeLocation.line = 0;
		}
		return functionAddresses[addr];
	}
	return it->second;
} 

// find in ELF_SECTIONS specified sections in the kernel
void readSections(map<string, pair<uint64_t, uint64_t>>& dataSections) {
	asection *curSection;
	vector<string> sections = ELF_SECTIONS;

	for (const string &section : sections) {
		curSection = bfd_get_section_by_name(kernelBfd, section.c_str());
		if (curSection == NULL) {
			cout << "Cannot find section '" << section << "'. Ignoring." << endl;
		} else {
			dataSections[bfd_section_name(curSection)] = make_pair(
				bfd_section_vma(curSection),
				bfd_section_size(curSection));

			cout << bfd_section_name(curSection) << ": " << dataSections[bfd_section_name(curSection)].second << " bytes @ " << hex << showbase << dataSections[bfd_section_name(curSection)].first << dec << noshowbase << endl;
		}
	}
}


int binaryread_init(const char *vmlinuxName) {
	int i;
	symbol_info syminfo;

	// Init bfd
	bfd_init();
	// Use NULL as target name for libbfd
	// Libbfd tries to determine to correct target.
	kernelBfd = bfd_openr(vmlinuxName, NULL);
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

	for (i = 0; i < bfdSymcount; i++) {
		if (bfdSyms[i]->flags & BSF_GLOBAL) {
			bfd_symbol_info(bfdSyms[i], &syminfo);
			addrToSym.emplace(syminfo.value, bfdSyms[i]->name);
		}
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
