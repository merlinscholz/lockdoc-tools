#include "binaryread.h"
#include <bfd.h>

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
		bfdSyms = nullptr;
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
															   &bfdSearchCtx->line, nullptr);
}

int binaryread_init(const char *filename)
{
	// Init bfd
	bfd_init();
	// Use NULL as target name for libbfd
	// Libbfd tries to determine to correct target.
	kernelBfd = bfd_openr(filename, nullptr);
	if (kernelBfd == nullptr) {
		bfd_perror("open vmlinux");
		return 1;
	}
	// This check is not only a sanity check. Moreover, it is necessary
	// to allow looking up of sections.
	if (!bfd_check_format (kernelBfd, bfd_object)) {
		std::cerr << "bfd: unknown format" << std::endl;
		bfd_close(kernelBfd);
		return 1;
	}
	if (slurp_symtab(kernelBfd)) {
		fprintf(stderr, "slurp_symtab(..) failed!\n");
		return 1;
	}
	return 0;
}

BfdSearchCtx addr_to_line(unsigned long addr)
{
	BfdSearchCtx bfdSearchCtx;
	memset(&bfdSearchCtx, 0, sizeof(bfdSearchCtx));

	bfdSearchCtx.pc = addr;
	bfd_map_over_sections (kernelBfd, find_address_in_section, &bfdSearchCtx);
	return bfdSearchCtx;
}