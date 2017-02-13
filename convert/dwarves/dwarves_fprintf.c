/*
  Copyright (C) 2006 Mandriva Conectiva S.A.
  Copyright (C) 2006 Arnaldo Carvalho de Melo <acme@mandriva.com>
  Copyright (C) 2007..2009 Red Hat Inc.
  Copyright (C) 2007..2009 Arnaldo Carvalho de Melo <acme@redhat.com>
  Copyright (c) 2016 Alexander Lochmann <alexander.lochmann@tu-dortmund.de>

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This file has been extracted from https://github.com/acmel/dwarves .
  It has been stripped down and/or modified by Alexander Lochmann.
*/

#include <dwarf.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "config.h"
#include "dwarves.h"

static size_t cacheline_size;

static const char *dwarf_tag_names[] = {
	[DW_TAG_array_type]		  = "array_type",
	[DW_TAG_class_type]		  = "class_type",
	[DW_TAG_entry_point]		  = "entry_point",
	[DW_TAG_enumeration_type]	  = "enumeration_type",
	[DW_TAG_formal_parameter]	  = "formal_parameter",
	[DW_TAG_imported_declaration]	  = "imported_declaration",
	[DW_TAG_label]			  = "label",
	[DW_TAG_lexical_block]		  = "lexical_block",
	[DW_TAG_member]			  = "member",
	[DW_TAG_pointer_type]		  = "pointer_type",
	[DW_TAG_reference_type]		  = "reference_type",
	[DW_TAG_compile_unit]		  = "compile_unit",
	[DW_TAG_string_type]		  = "string_type",
	[DW_TAG_structure_type]		  = "structure_type",
	[DW_TAG_subroutine_type]	  = "subroutine_type",
	[DW_TAG_typedef]		  = "typedef",
	[DW_TAG_union_type]		  = "union_type",
	[DW_TAG_unspecified_parameters]	  = "unspecified_parameters",
	[DW_TAG_variant]		  = "variant",
	[DW_TAG_common_block]		  = "common_block",
	[DW_TAG_common_inclusion]	  = "common_inclusion",
	[DW_TAG_inheritance]		  = "inheritance",
	[DW_TAG_inlined_subroutine]	  = "inlined_subroutine",
	[DW_TAG_module]			  = "module",
	[DW_TAG_ptr_to_member_type]	  = "ptr_to_member_type",
	[DW_TAG_set_type]		  = "set_type",
	[DW_TAG_subrange_type]		  = "subrange_type",
	[DW_TAG_with_stmt]		  = "with_stmt",
	[DW_TAG_access_declaration]	  = "access_declaration",
	[DW_TAG_base_type]		  = "base_type",
	[DW_TAG_catch_block]		  = "catch_block",
	[DW_TAG_const_type]		  = "const_type",
	[DW_TAG_constant]		  = "constant",
	[DW_TAG_enumerator]		  = "enumerator",
	[DW_TAG_file_type]		  = "file_type",
	[DW_TAG_friend]			  = "friend",
	[DW_TAG_namelist]		  = "namelist",
	[DW_TAG_namelist_item]		  = "namelist_item",
	[DW_TAG_packed_type]		  = "packed_type",
	[DW_TAG_subprogram]		  = "subprogram",
	[DW_TAG_template_type_parameter]  = "template_type_parameter",
	[DW_TAG_template_value_parameter] = "template_value_parameter",
	[DW_TAG_thrown_type]		  = "thrown_type",
	[DW_TAG_try_block]		  = "try_block",
	[DW_TAG_variant_part]		  = "variant_part",
	[DW_TAG_variable]		  = "variable",
	[DW_TAG_volatile_type]		  = "volatile_type",
	[DW_TAG_dwarf_procedure]	  = "dwarf_procedure",
	[DW_TAG_restrict_type]		  = "restrict_type",
	[DW_TAG_interface_type]		  = "interface_type",
	[DW_TAG_namespace]		  = "namespace",
	[DW_TAG_imported_module]	  = "imported_module",
	[DW_TAG_unspecified_type]	  = "unspecified_type",
	[DW_TAG_partial_unit]		  = "partial_unit",
	[DW_TAG_imported_unit]		  = "imported_unit",
//	[DW_TAG_mutable_type]		  = "mutable_type",
	[DW_TAG_condition]		  = "condition",
	[DW_TAG_shared_type]		  = "shared_type",
#ifdef STB_GNU_UNIQUE
	[DW_TAG_type_unit]		  = "type_unit",
	[DW_TAG_rvalue_reference_type]    = "rvalue_reference_type",
#endif
};

static const char *dwarf_gnu_tag_names[] = {
	[DW_TAG_MIPS_loop - DW_TAG_MIPS_loop]			= "MIPS_loop",
	[DW_TAG_format_label - DW_TAG_MIPS_loop]		= "format_label",
	[DW_TAG_function_template - DW_TAG_MIPS_loop]		= "function_template",
	[DW_TAG_class_template - DW_TAG_MIPS_loop]		= "class_template",
#ifdef STB_GNU_UNIQUE
	[DW_TAG_GNU_BINCL - DW_TAG_MIPS_loop]			= "BINCL",
	[DW_TAG_GNU_EINCL - DW_TAG_MIPS_loop]			= "EINCL",
	[DW_TAG_GNU_template_template_param - DW_TAG_MIPS_loop] = "template_template_param",
	[DW_TAG_GNU_template_parameter_pack - DW_TAG_MIPS_loop] = "template_parameter_pack",
	[DW_TAG_GNU_formal_parameter_pack - DW_TAG_MIPS_loop]	= "formal_parameter_pack",
#endif
};

static size_t __tag__id_not_found_snprintf(char *bf, size_t len, uint16_t id,
					   const char *fn, int line)
{
	return snprintf(bf, len, "<ERROR(%s:%d): %#llx not found!>", fn, line,
			(unsigned long long)id);
}

#define tag__id_not_found_snprintf(bf, len, id) \
	__tag__id_not_found_snprintf(bf, len, id, __func__, __LINE__)

static void dwarves_convert_prefix_print(FILE *fp, struct dwarves_convert_ext const *ext)
{
	int i;
	for (i = 0; i < ext->next_prefix_idx; ++i) {
		fputs(ext->name_prefixes[i], fp);
		fputs(".", fp);
	}
}

static bool type__fprintf(struct tag *type, const struct cu *cu,
			    const char *name, FILE *fp, struct dwarves_convert_ext *ext,
				const char *cm_name, uint32_t offset);

static void array_type__fprintf(const struct tag *tag,
				  const struct cu *cu, const char *name,
				  FILE *fp, struct dwarves_convert_ext *ext)
{
	struct array_type *at = tag__array_type(tag);
	struct tag *type = cu__type(cu, tag->type);
	unsigned long long flat_dimensions = 0;
	int i;

	if (type == NULL) {
		tag__id_not_found_fprintf(fp, tag->type);
	}

	// FIXME: expansion of embedded struct arrays does not work yet
	type__fprintf(type, cu, name, fp, ext, NULL, 0);
	for (i = 0; i < at->dimensions; ++i) {
		if (at->is_vector) {
			/*
			 * Seen on the Linux kernel on tun_filter:
			 *
			 * __u8   addr[0][ETH_ALEN];
			 */
			if (at->nr_entries[i] == 0 && i == 0)
				break;
			if (!flat_dimensions) {
				flat_dimensions = at->nr_entries[i];
			} else {
				flat_dimensions *= at->nr_entries[i];
			}
		} else {
			fprintf(fp, "[%u]", at->nr_entries[i]);
		}
	}

	if (at->is_vector) {
		type = tag__follow_typedef(tag, cu);

		if (flat_dimensions == 0) {
			flat_dimensions = 1;
		}
		fprintf(fp, " __attribute__ ((__vector_size__ (%llu)))",
				   flat_dimensions * tag__size(type, cu));
	}
}

static const char *tag__prefix(const struct cu *cu, const uint32_t tag)
{
	switch (tag) {
	case DW_TAG_enumeration_type:	return "enum ";
	case DW_TAG_structure_type:
		return (cu->language == DW_LANG_C_plus_plus) ? "class " : "struct ";
	case DW_TAG_class_type:
		return "class ";
	case DW_TAG_union_type:		return "union ";
	case DW_TAG_pointer_type:	return " *";
	case DW_TAG_reference_type:	return " &";
	}

	return "";
}

static const char *__tag__name(const struct tag *tag, const struct cu *cu,
			       char *bf, size_t len);

static const char *tag__ptr_name(const struct tag *tag, const struct cu *cu,
				 char *bf, size_t len, const char *ptr_suffix)
{
	if (tag->type == 0) /* No type == void */
		snprintf(bf, len, "void %s", ptr_suffix);
	else {
		const struct tag *type = cu__type(cu, tag->type);

		if (type == NULL) {
			size_t l = tag__id_not_found_snprintf(bf, len, tag->type);
			snprintf(bf + l, len - l, " %s", ptr_suffix);
		} else if (!tag__has_type_loop(tag, type, bf, len, NULL)) {
			char tmpbf[1024];

			snprintf(bf, len, "%s %s",
				 __tag__name(type, cu,
					     tmpbf, sizeof(tmpbf)),
				 ptr_suffix);
		}
	}

	return bf;
}

const char *dwarf_tag_name(const uint32_t tag)
{
	if (tag >= DW_TAG_array_type && tag <=
#ifdef STB_GNU_UNIQUE
		DW_TAG_rvalue_reference_type
#else
		DW_TAG_shared_type
#endif
	    )
		return dwarf_tag_names[tag];
	else if (tag >= DW_TAG_MIPS_loop && tag <=
#ifdef STB_GNU_UNIQUE
		 DW_TAG_GNU_formal_parameter_pack
#else
		 DW_TAG_class_template
#endif
		)
		return dwarf_gnu_tag_names[tag - DW_TAG_MIPS_loop];
	return "INVALID";
}

static const char *__tag__name(const struct tag *tag, const struct cu *cu,
			       char *bf, size_t len)
{
	struct tag *type;

	if (tag == NULL)
		strncpy(bf, "void", len);
	else switch (tag->tag) {
	case DW_TAG_base_type: {
		const struct base_type *bt = tag__base_type(tag);
		const char *name = "nameless base type!";
		char bf2[64];

		if (bt->name)
			name = base_type__name(tag__base_type(tag), cu,
					       bf2, sizeof(bf2));

		strncpy(bf, name, len);
	}
		break;
	case DW_TAG_subprogram:
		strncpy(bf, function__name(tag__function(tag), cu), len);
		break;
	case DW_TAG_pointer_type:
		return tag__ptr_name(tag, cu, bf, len, "*");
	case DW_TAG_reference_type:
		return tag__ptr_name(tag, cu, bf, len, "&");
	case DW_TAG_ptr_to_member_type: {
		char suffix[512];
		uint16_t id = tag__ptr_to_member_type(tag)->containing_type;

		type = cu__type(cu, id);
		if (type != NULL)
			snprintf(suffix, sizeof(suffix), "%s::*",
				 class__name(tag__class(type), cu));
		else {
			size_t l = tag__id_not_found_snprintf(suffix,
							      sizeof(suffix),
							      id);
			snprintf(suffix + l, sizeof(suffix) - l, "::*");
		}

		return tag__ptr_name(tag, cu, bf, len, suffix);
	}
	case DW_TAG_volatile_type:
	case DW_TAG_const_type:
		type = cu__type(cu, tag->type);
		if (type == NULL && tag->type != 0)
			tag__id_not_found_snprintf(bf, len, tag->type);
		else if (!tag__has_type_loop(tag, type, bf, len, NULL)) {
			char tmpbf[128];
			const char *prefix = "const",
				   *type_str = __tag__name(type, cu, tmpbf,
							   sizeof(tmpbf));
			if (tag->tag == DW_TAG_volatile_type)
				prefix = "volatile";
			snprintf(bf, len, "%s %s ", prefix, type_str);
		}
		break;
	case DW_TAG_array_type:
		type = cu__type(cu, tag->type);
		if (type == NULL)
			tag__id_not_found_snprintf(bf, len, tag->type);
		else if (!tag__has_type_loop(tag, type, bf, len, NULL))
			return __tag__name(type, cu, bf, len);
		break;
	case DW_TAG_subroutine_type: {
		FILE *bfp = fmemopen(bf, len, "w");

		if (bfp != NULL) {
			ftype__fprintf(tag__ftype(tag), cu, NULL, 0, 0, bfp);
			fclose(bfp);
		} else
			snprintf(bf, len, "<ERROR(%s): fmemopen failed!>",
				 __func__);
	}
		break;
	case DW_TAG_member:
		snprintf(bf, len, "%s", class_member__name(tag__class_member(tag), cu));
		break;
	case DW_TAG_variable:
		snprintf(bf, len, "%s", variable__name(tag__variable(tag), cu));
		break;
	default:
		snprintf(bf, len, "%s%s", tag__prefix(cu, tag->tag),
			 type__name(tag__type(tag), cu) ?: "");
		break;
	}

	return bf;
}

const char *tag__name(const struct tag *tag, const struct cu *cu,
		      char *bf, size_t len)
{
	bool starts_with_const = false;

	if (tag == NULL) {
		strncpy(bf, "void", len);
		return bf;
	}

	if (tag->tag == DW_TAG_const_type) {
		starts_with_const = true;
		tag = cu__type(cu, tag->type);
	}

	__tag__name(tag, cu, bf, len);

	if (starts_with_const)
		strncat(bf, "const", len);

	return bf;
}

// returns true if it recursed into a known struct
static bool type__fprintf(struct tag *type, const struct cu *cu,
	const char *name, FILE *fp, struct dwarves_convert_ext *ext,
	const char *cm_name, uint32_t offset)
{
	char tbf[128];
	//char namebf[256];
	struct type *ctype;
	struct tag *orig_type = type;

	if (type == NULL) {
		goto out_error;
	}

	// Try to resolve typedefs
	// All output-related code has been removed. For those who are intereseted, have a look at type__fprintf():586-612 in the original dwarves_fprintf.c.
	while (tag__is_typedef(orig_type)) {
		struct tag *type_type;

		ctype = tag__type(orig_type);	
		type_type = cu__type(cu, orig_type->type);
		
		if (type_type == NULL || tag__has_type_loop(orig_type, type_type, NULL, 0, fp)) {
			// Cannot resolve type definition or definition contains a loop
			// Do not resolve the typedef, and fallback to the actual definition
			orig_type = NULL;
			break;
		}
		orig_type = type_type;
	}
	// Have we resolved the type and does the definition have a name?
	// If not, use the typedef. This might be the case if the typedef is an anonymous struct/union for example.
	if (orig_type != NULL && type__name(tag__type(orig_type), cu) != NULL) {
		type = orig_type;
	}

	if (type->tag != DW_TAG_class_type && type->tag != DW_TAG_structure_type && type->tag != DW_TAG_array_type) {
		fprintf(fp, "%llu" DELIMITER_STRING, ext->type_id);
	}

	switch (type->tag) {
		case DW_TAG_pointer_type:
			if (type->type != 0) {
				int n;
				struct tag *ptype = cu__type(cu, type->type);
				if (ptype == NULL) {
					goto out_error;
				}
				n = tag__has_type_loop(type, ptype, NULL, 0, fp);
				if (n) {
					return false;
				}
				if (ptype->tag == DW_TAG_subroutine_type) {
					ftype__fprintf(tag__ftype(ptype),
								  cu, name, 0, 1, fp);
					break;
				}
			}
			/* Fall Thru */
		default:
			fprintf(fp, "%s",
					   tag__name(type, cu, tbf, sizeof(tbf)));
			break;
		case DW_TAG_subroutine_type:
			ftype__fprintf(tag__ftype(type), cu, name, 0, 0, fp);
			break;
		case DW_TAG_array_type:
			array_type__fprintf(type, cu, name, fp, ext);
			break;
		case DW_TAG_class_type:
		case DW_TAG_structure_type:
			ctype = tag__type(type);

			if (type__name(ctype, cu) != NULL && !ext->expand_type(type__name(ctype, cu))) {
				fprintf(fp, "%llu" DELIMITER_STRING, ext->type_id);
				fprintf(fp, "%s %s",
						   (type->tag == DW_TAG_class_type) ? "class" : "struct",
						   type__name(ctype, cu));
			} else {
				if (ext->next_prefix_idx >= sizeof(ext->name_prefixes)/sizeof(*ext->name_prefixes)) {
					fprintf(fp, "<ERROR RECURSION MAXDEPTH REACHED>");
					return false;
				}

				// recurse
				ext->name_prefixes[ext->next_prefix_idx++] = cm_name;
				ext->offset += offset;
				class__fprintf(tag__class(type), cu, fp, ext);
				ext->offset -= offset;
				ext->next_prefix_idx--;
				return true;
			}
			break;
		case DW_TAG_union_type:
			ctype = tag__type(type);

			fprintf(fp,"union ");
			if (type__name(ctype, cu) != NULL) {
				fprintf(fp, "%s",
						   type__name(ctype, cu));
			}
			break;
		case DW_TAG_enumeration_type:
			ctype = tag__type(type);

			fprintf(fp,"enum ");
			if (type__name(ctype, cu) != NULL) {
				fprintf(fp, "%s",
						   type__name(ctype, cu));
			}
			break;
		}
	return false;

out_error:
	fprintf(fp, "%s","<ERROR>");
	return false;
}

static void struct_member__fprintf(struct class_member *member,
	struct tag *type, const struct cu *cu, FILE *fp,
	struct dwarves_convert_ext *ext)
{
	struct class_member *pos;
	const int size = member->byte_size;
	uint32_t offset = member->byte_offset;
	char *cm_name_temp = NULL;
	const char *cm_name = class_member__name(member, cu), *temp, *name = cm_name;

	if (member->tag.tag == DW_TAG_inheritance) {
		name = "<ancestor>";
	}

	if (member->is_static) { //TODO: skip
		//fprintf(fp, "static ");

		// skip
		return;
	}

#if 0
	if (member->is_static) {
		if (member->const_value != 0) {
			fprintf(fp, " = %lu;", member->const_value);
		}
	} else if (member->bitfield_size != 0) {
		fprintf(fp, ":%u;", member->bitfield_size);
	}
#endif
	if (tag__is_union(type)) {
		int len = 0, curLen = 0;
		struct type *ctype = tag__type(type);

		type__for_each_member(ctype, pos) {
			struct tag *type = cu__type(cu, pos->tag.type);

			if (type == NULL) {
				continue;
			}
			temp = class_member__name(pos, cu);
			if (temp == NULL) {
				continue;
			}
			curLen = strlen(temp) + 2;
			len += curLen;
			cm_name_temp = realloc(cm_name_temp,len);
			if (len == curLen) {
					memset(cm_name_temp,0,strlen(temp) + 2);
			}
			if (cm_name_temp == NULL) {
				exit(0);
			}
			strcat(cm_name_temp,temp);
			if (!list_is_last(&pos->tag.node,&ctype->namespace.tags)) {
				strcat(cm_name_temp,"-");
			}
		}
		if (cm_name_temp != NULL) {
			cm_name = cm_name_temp;
		}
	}
	if (cm_name == NULL) {
		cm_name_temp = malloc(strlen("anonymous") + 10);
		if (cm_name_temp == NULL) {
			exit(0);
		}
		sprintf(cm_name_temp,"anonymous_%d",offset);
		cm_name = cm_name_temp;
	}
	
	if (!type__fprintf(type, cu, name, fp, ext, cm_name, offset)) {
		fprintf(fp, DELIMITER_STRING);
		dwarves_convert_prefix_print(fp, ext);
		fprintf(fp,
			"%s" DELIMITER_STRING "%d" DELIMITER_STRING "%d",
			cm_name, offset + ext->offset, size);
		fprintf(fp, "\n");
	}
	if (cm_name_temp != NULL) {
		free(cm_name_temp);
	}
}

const char *function__prototype(const struct function *func,
				const struct cu *cu, char *bf, size_t len)
{
	FILE *bfp = fmemopen(bf, len, "w");

	if (bfp != NULL) {
		ftype__fprintf(&func->proto, cu, NULL, 0, 0, bfp);
		fclose(bfp);
	} else
		snprintf(bf, len, "<ERROR(%s): fmemopen failed!>", __func__);

	return bf;
}

void ftype__fprintf_parms(const struct ftype *ftype,
			    const struct cu *cu, FILE *fp)
{
	struct parameter *pos;
	char sbf[128];
	struct tag *type;
	const char *name, *stype;
	int first_parm = 1;
	fprintf(fp, "(");

	ftype__for_each_parameter(ftype, pos) {
		if (first_parm)  {
			first_parm = 0;
		}
		name = parameter__name(pos, cu);
		type = cu__type(cu, pos->tag.type);
		if (type == NULL) {
			snprintf(sbf, sizeof(sbf),
				 "<ERROR: type %d not found>", pos->tag.type);
			stype = sbf;
			goto print_it;
		}
		if (type->tag == DW_TAG_pointer_type) {
			if (type->type != 0) {
				int n;
				struct tag *ptype = cu__type(cu, type->type);
				if (ptype == NULL) {
					tag__id_not_found_fprintf(fp, type->type);
					continue;
				}
				n = tag__has_type_loop(type, ptype, NULL, 0, fp);
				if (n) {
					return;
				}
				if (ptype->tag == DW_TAG_subroutine_type) {
					ftype__fprintf(tag__ftype(ptype),
							    cu, name, 0, 1, fp);
					continue;
				}
			}
		} else if (type->tag == DW_TAG_subroutine_type) {
			ftype__fprintf(tag__ftype(type), cu, name,
						  0, 0, fp);
			continue;
		}
		stype = tag__name(type, cu, sbf, sizeof(sbf));
print_it:
		fprintf(fp, "%s%s%s", stype, name ? " " : "",
				   name ?: "");
	}

	/* No parameters? */
	if (first_parm) {
		fprintf(fp, "void)");
	} else if (ftype->unspec_parms) {
		fprintf(fp, ", ...)");
	} else {
		fprintf(fp, ")");
	}
}

void ftype__fprintf(const struct ftype *ftype, const struct cu *cu,
		      const char *name, const int inlined,
		      const int is_pointer, FILE *fp)
{
	struct tag *type = cu__type(cu, ftype->tag.type);
	char sbf[128];
	const char *stype = tag__name(type, cu, sbf, sizeof(sbf));
	fprintf(fp, "%s%s %s%s%s%s",
				 inlined ? "inline " : "",
				 stype,
				 ftype->tag.tag == DW_TAG_subroutine_type ?
					"(" : "",
				 is_pointer ? "*" : "", name ?: "",
				 ftype->tag.tag == DW_TAG_subroutine_type ?
					")" : "");

	ftype__fprintf_parms(ftype, cu, fp);
}


int class__fprintf(void *class_, const struct cu *cu,FILE *out,
	struct dwarves_convert_ext *ext)
{
	struct class *class = (struct class*)class_;
	struct type *type = &class->type;
	struct class_member *pos;
	struct tag *tag_pos;
	int ret = 0;

	type__for_each_tag(type, tag_pos) {
		struct tag *type;
	
		if (tag_pos->tag != DW_TAG_member &&
		    tag_pos->tag != DW_TAG_inheritance) {
			continue;
		}

		ret = 1;
		pos = tag__class_member(tag_pos);

		type = cu__type(cu, pos->tag.type);
		if (type == NULL) {
			tag__id_not_found_fprintf(stderr, pos->tag.type);
			continue;
		}

		struct_member__fprintf(pos, type, cu, out, ext);
	}
	return ret;
}

void cus__print_error_msg(const char *progname, const struct cus *cus,
			  const char *filename, const int err)
{
	if (err == -EINVAL || (cus != NULL && list_empty(&cus->cus)))
		fprintf(stderr, "%s: couldn't load debugging info from %s\n",
		       progname, filename);
	else
		fprintf(stderr, "%s: %s\n", progname, strerror(err));
}

void dwarves__fprintf_init(uint16_t user_cacheline_size)
{
	if (user_cacheline_size == 0) {
		long sys_cacheline_size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);

		if (sys_cacheline_size > 0)
			cacheline_size = sys_cacheline_size;
		else
			cacheline_size = 64; /* Fall back to a sane value */
	} else
		cacheline_size = user_cacheline_size;
}
