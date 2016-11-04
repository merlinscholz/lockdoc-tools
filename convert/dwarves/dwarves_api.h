#ifndef __DWARVES_API_H__
#define __DWARVES_API_H__


#ifdef __cplusplus
extern "C" {
#endif

#include <obstack.h>
#include <dwarf.h>
#include <elfutils/libdwfl.h>
#include "list.h"
#include "strings.h"
#include "rbtree.h"

struct cu;

enum load_steal_kind {
	LSK__KEEPIT,
	LSK__DELETE,
	LSK__STOP_LOADING,
};

/** struct conf_load - load configuration
 * @extra_dbg_info - keep original debugging format extra info
 *		     (e.g. DWARF's decl_{line,file}, id, etc)
 * @fixup_silly_bitfields - Fixup silly things such as "int foo:32;"
 * @get_addr_info - wheter to load DW_AT_location and other addr info
 */
struct conf_load {
	enum load_steal_kind	(*steal)(struct cu *self,
					 struct conf_load *conf);
	void			*cookie;
	char			*format_path;
	bool			extra_dbg_info;
	bool			fixup_silly_bitfields;
	bool			get_addr_info;
};

struct cus {
	struct list_head      cus;
};

struct ptr_table {
	void	 **entries;
	uint32_t nr_entries;
	uint32_t allocated_entries;
};

struct function;
struct tag;
struct cu;
struct variable;

/* Same as DW_LANG, so that we don't have to include dwarf.h in CTF */
enum dwarf_languages {
    LANG_C89		= 0x01,	/* ISO C:1989 */
    LANG_C		= 0x02,	/* C */
    LANG_Ada83		= 0x03,	/* ISO Ada:1983 */
    LANG_C_plus_plus	= 0x04,	/* ISO C++:1998 */
    LANG_Cobol74	= 0x05,	/* ISO Cobol:1974 */
    LANG_Cobol85	= 0x06,	/* ISO Cobol:1985 */
    LANG_Fortran77	= 0x07,	/* ISO FORTRAN 77 */
    LANG_Fortran90	= 0x08,	/* ISO Fortran 90 */
    LANG_Pascal83	= 0x09,	/* ISO Pascal:1983 */
    LANG_Modula2	= 0x0a,	/* ISO Modula-2:1996 */
    LANG_Java		= 0x0b,	/* Java */
    LANG_C99		= 0x0c,	/* ISO C:1999 */
    LANG_Ada95		= 0x0d,	/* ISO Ada:1995 */
    LANG_Fortran95	= 0x0e,	/* ISO Fortran 95 */
    LANG_PL1		= 0x0f,	/* ISO PL/1:1976 */
    LANG_Objc		= 0x10,	/* Objective-C */
    LANG_ObjC_plus_plus	= 0x11,	/* Objective-C++ */
    LANG_UPC		= 0x12,	/* Unified Parallel C */
    LANG_D		= 0x13,	/* D */
};

/** struct debug_fmt_ops - specific to the underlying debug file format
 *
 * @function__name - will be called by function__name(), giving a chance to
 *		     formats such as CTF to get this from some other place
 *		     than the global strings table. CTF does this by storing
 * 		     GElf_Sym->st_name in function->name, and by using
 *		     function->name as an index into the .strtab ELF section.
 * @variable__name - will be called by variable__name(), see @function_name
 * cu__delete - called at cu__delete(), to give a chance to formats such as
 *		CTF to keep the .strstab ELF section available till the cu is
 *		deleted. See @function__name
 */
struct debug_fmt_ops {
	const char	   *name;
	int		   (*init)(void);
	void		   (*exit)(void);
	int		   (*load_file)(struct cus *self,
				       struct conf_load *conf,
				       const char *filename);
	const char	   *(*tag__decl_file)(const struct tag *self,
					      const struct cu *cu);
	uint32_t	   (*tag__decl_line)(const struct tag *self,
					     const struct cu *cu);
	unsigned long long (*tag__orig_id)(const struct tag *self,
					   const struct cu *cu);
	void		   (*tag__free_orig_info)(struct tag *self,
						  struct cu *cu);
	const char	   *(*function__name)(struct function *self,
					      const struct cu *cu);
	const char	   *(*variable__name)(const struct variable *self,
					      const struct cu *cu);
	const char	   *(*strings__ptr)(const struct cu *self, strings_t s);
	void		   (*cu__delete)(struct cu *self);
};

struct cu {
	struct list_head node;
	struct list_head tags;
	struct list_head tool_list;	/* To be used by tools such as ctracer */
	struct ptr_table types_table;
	struct ptr_table functions_table;
	struct ptr_table tags_table;
	struct rb_root	 functions;
	char		 *name;
	char		 *filename;
	void 		 *priv;
	struct obstack	 obstack;
	struct debug_fmt_ops *dfops;
	Elf		 *elf;
	Dwfl_Module	 *dwfl;
	uint32_t	 cached_symtab_nr_entries;
	uint8_t		 addr_size;
	uint8_t		 extra_dbg_info:1;
	uint8_t		 has_addr_info:1;
	uint8_t		 uses_global_strings:1;
	uint16_t	 language;
	unsigned long	 nr_inline_expansions;
	size_t		 size_inline_expansions;
	uint32_t	 nr_functions_changed;
	uint32_t	 nr_structures_changed;
	size_t		 max_len_changed_item;
	size_t		 function_bytes_added;
	size_t		 function_bytes_removed;
	int		 build_id_len;
	unsigned char	 build_id[0];
};

/** struct tag - basic representation of a debug info element
 * @priv - extra data, for instance, DWARF offset, id, decl_{file,line}
 * @top_level -
 */
struct tag {
	struct list_head node;
	uint16_t	 type;
	uint16_t	 tag;
	bool		 visited;
	bool		 top_level;
	uint16_t	 recursivity_level;
	void		 *priv;
};

/** struct conf_fprintf - hints to the __fprintf routines
 *
 * @flat_arrays - a->foo[10][2] becomes a->foo[20]
 * @classes_as_structs - class f becomes struct f, CTF doesn't have a "class"
 */
struct conf_fprintf {
	const char *prefix;
	const char *suffix;
	int32_t	   type_spacing;
	int32_t	   name_spacing;
	uint32_t   base_offset;
	uint8_t	   indent;
	uint8_t	   expand_types:1;
	uint8_t	   expand_pointers:1;
	uint8_t    rel_offset:1;
	uint8_t	   emit_stats:1;
	uint8_t	   suppress_comments:1;
	uint8_t	   suppress_offset_comment:1;
	uint8_t	   show_decl_info:1;
	uint8_t	   show_only_data_members:1;
	uint8_t	   no_semicolon:1;
	uint8_t	   show_first_biggest_size_base_type_member:1;
	uint8_t	   flat_arrays:1;
	uint8_t	   no_parm_names:1;
	uint8_t	   classes_as_structs:1;
	uint8_t	   hex_fmt:1;
};


struct cus *cus__new(void);
void cus__delete(struct cus *self);

int cus__load_file(struct cus *self, struct conf_load *conf,
		   const char *filename);
int cus__load_files(struct cus *self, struct conf_load *conf,
		    char *filenames[]);
int cus__load_dir(struct cus *self, struct conf_load *conf,
		  const char *dirname, const char *filename_mask,
		  const int recursive);
void cus__add(struct cus *self, struct cu *cu);
void cus__print_error_msg(const char *progname, const struct cus *cus,
			  const char *filename, const int err);
struct cu *cus__find_cu_by_name(const struct cus *self, const char *name);
struct tag *cus__find_struct_by_name(const struct cus *self, struct cu **cu,
				     const char *name, const int include_decls,
				     uint16_t *id);
struct function *cus__find_function_at_addr(const struct cus *self,
					    uint64_t addr, struct cu **cu);
void cus__for_each_cu(struct cus *self, int (*iterator)(struct cu *cu,
							void *cookie),
		      void *cookie,
		      struct cu *(*filter)(struct cu *cu));

void class__fprintf(void *class_, const struct cu *cu,FILE *out, unsigned long long id);
struct tag *cu__find_struct_by_name(const struct cu *cu, const char *name,
				    const int include_decls, uint16_t *id);

int dwarves__init(uint16_t user_cacheline_size);
void dwarves__exit(void);

#ifdef __cplusplus
}
#endif

#endif // __DWARVES_API_H__
