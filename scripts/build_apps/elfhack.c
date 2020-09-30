/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <errno.h>

#include <elf.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define USE_ELF32
#include <tilck/common/basic_defs.h>
#include <tilck/common/elf_types.h>
#include <tilck/common/utils.h>

struct elf_file_info {

   const char *path;
   size_t mmap_size;
   void *vaddr;
   int fd;
};

struct elfhack_cmd {

   const char *opt;
   const char *help;
   int nargs;
   int (*func)(struct elf_file_info *, const char *, const char *);
};

int show_help(struct elf_file_info *nfo, const char *, const char *);

/* --- Low-level ELF utility functions --- */

Elf_Shdr *
get_section(Elf_Ehdr *h, const char *section_name)
{
   Elf_Shdr *sections = (Elf_Shdr *) ((char *)h + h->e_shoff);
   Elf_Shdr *section_header_strtab = sections + h->e_shstrndx;

   for (uint32_t i = 0; i < h->e_shnum; i++) {

      Elf_Shdr *s = sections + i;
      char *name = (char *)h + section_header_strtab->sh_offset + s->sh_name;

      if (!strcmp(name, section_name)) {
         return s;
      }
   }

   fprintf(stderr, "No section '%s'\n", section_name);
   exit(1);
}

Elf_Phdr *
get_phdr_for_section(Elf_Ehdr *h, Elf_Shdr *section)
{
   Elf_Phdr *phdrs = (Elf_Phdr *)((char*)h + h->e_phoff);
   Elf_Addr sh_begin = section->sh_addr;
   Elf_Addr sh_end = section->sh_addr + section->sh_size;

   for (uint32_t i = 0; i < h->e_phnum; i++) {

      Elf_Phdr *p = phdrs + i;
      Elf_Addr pend = p->p_vaddr + p->p_memsz;

      if (p->p_vaddr <= sh_begin && sh_end <= pend)
         return p;
   }

   return NULL;
}

Elf_Sym *
get_symbol(Elf_Ehdr *h, const char *sym_name)
{
   Elf_Shdr *symtab;
   Elf_Shdr *strtab;
   Elf_Sym *syms;
   unsigned sym_count;

   symtab = get_section(h, ".symtab");
   strtab = get_section(h, ".strtab");

   if (!symtab || !strtab)
      return NULL;

   syms = (Elf_Sym *)((char *)h + symtab->sh_offset);
   sym_count = symtab->sh_size / sizeof(Elf_Sym);

   for (unsigned i = 0; i < sym_count; i++) {

      Elf_Sym *s = syms + i;
      const char *s_name = (char *)h + strtab->sh_offset + s->st_name;

      if (!strcmp(s_name, sym_name))
         return s;
   }

   return NULL;
}

/* --- Actual commands --- */

int
section_dump(struct elf_file_info *nfo, const char *section_name, const char *u)
{
   Elf_Ehdr *h = (Elf_Ehdr*)nfo->vaddr;
   Elf_Shdr *s = get_section(nfo->vaddr, section_name);
   fwrite((char*)h + s->sh_offset, 1, s->sh_size, stdout);
   return 0;
}

int
copy_section(struct elf_file_info *nfo, const char *src, const char *dst)
{
   Elf_Ehdr *h = (Elf_Ehdr*)nfo->vaddr;
   Elf_Shdr *s_src = get_section(nfo->vaddr, src);
   Elf_Shdr *s_dst = get_section(nfo->vaddr, dst);

   if (!dst) {
      fprintf(stderr, "Missing <dest section> argument\n");
      return 1;
   }

   if (s_src->sh_size > s_dst->sh_size) {
      fprintf(stderr, "The source section '%s' is too big "
              "[%u bytes] to fit in the dest section '%s' [%u bytes]\n",
              src, s_src->sh_size, dst, s_dst->sh_size);
      return 1;
   }

   memcpy((char*)h + s_dst->sh_offset,
          (char*)h + s_src->sh_offset,
          s_src->sh_size);

   s_dst->sh_info = s_src->sh_info;
   s_dst->sh_flags = s_src->sh_flags;
   s_dst->sh_type = s_src->sh_type;
   s_dst->sh_entsize = s_src->sh_entsize;
   s_dst->sh_size = s_src->sh_size;
   return 0;
}

int
rename_section(struct elf_file_info *nfo, const char *sec, const char *new_name)
{
   Elf_Ehdr *h = (Elf_Ehdr*)nfo->vaddr;
   char *hc = (char *)h;
   Elf_Shdr *sections = (Elf_Shdr *)(hc + h->e_shoff);
   Elf_Shdr *shstrtab = sections + h->e_shstrndx;

   if (!new_name) {
      fprintf(stderr, "Missing <new_name> argument\n");
      return 1;
   }

   if (strlen(new_name) > strlen(sec)) {
      fprintf(stderr, "Section rename with length > old one NOT supported.\n");
      return 1;
   }

   Elf_Shdr *s = get_section(nfo->vaddr, sec);
   strcpy(hc + shstrtab->sh_offset + s->sh_name, new_name);
   return 0;
}

int
link_sections(struct elf_file_info *nfo, const char *sec, const char *linked)
{
   Elf_Ehdr *h = (Elf_Ehdr*)nfo->vaddr;
   char *hc = (char *)h;
   Elf_Shdr *sections = (Elf_Shdr *)(hc + h->e_shoff);

   if (!linked) {
      fprintf(stderr, "Missing <linked section> argument\n");
      return 1;
   }

   Elf_Shdr *a = get_section(nfo->vaddr, sec);
   Elf_Shdr *b = get_section(nfo->vaddr, linked);

   unsigned bidx = (b - sections);
   a->sh_link = bidx;
   return 0;
}

int
move_metadata(struct elf_file_info *nfo, const char *u1, const char *u2)
{
   Elf_Ehdr *h = (Elf_Ehdr*)nfo->vaddr;
   char *hc = (char *)h;

   size_t off = h->e_ehsize;

   memcpy(hc + off, hc + h->e_phoff, h->e_phentsize*h->e_phnum);
   h->e_phoff = off;
   off += h->e_phentsize*h->e_phnum;

   memcpy(hc + off, hc + h->e_shoff, h->e_shentsize*h->e_shnum);
   h->e_shoff = off;
   off += h->e_shentsize*h->e_shnum;

   Elf_Shdr *sections = (Elf_Shdr *) (hc + h->e_shoff);
   Elf_Shdr *shstrtab = sections + h->e_shstrndx;

   memcpy(hc + off, hc + shstrtab->sh_offset, shstrtab->sh_size);
   shstrtab->sh_offset = off;

   Elf_Phdr *phdrs = (Elf_Phdr *)(hc + h->e_phoff);
   shstrtab->sh_addr = phdrs[0].p_vaddr + shstrtab->sh_offset;
   shstrtab->sh_flags |= SHF_ALLOC;

   for (uint32_t i = 0; i < h->e_shnum; i++) {
      Elf_Shdr *s = sections + i;

      /* Make sure that all the sections with a vaddr != 0 are 'alloc' */
      if (s->sh_addr)
         s->sh_flags |= SHF_ALLOC;
   }

   return 0;
}

int
drop_last_section(struct elf_file_info *nfo, const char *u1, const char *u2)
{
   Elf_Ehdr *h = (Elf_Ehdr*)nfo->vaddr;
   char *hc = (char *)h;
   Elf_Shdr *sections = (Elf_Shdr *)(hc + h->e_shoff);
   Elf_Shdr *shstrtab = sections + h->e_shstrndx;

   Elf_Shdr *last_section = sections;
   int last_section_index = 0;
   off_t last_offset = 0;

   if (!h->e_shnum) {
      fprintf(stderr, "ERROR: the ELF file has no sections!\n");
      return 1;
   }

   for (uint32_t i = 0; i < h->e_shnum; i++) {

      Elf_Shdr *s = sections + i;

      if (s->sh_offset > last_offset) {
         last_section = s;
         last_offset = s->sh_offset;
         last_section_index = i;
      }
   }

   if (last_section == shstrtab) {
      fprintf(stderr,
              "The last section is .shstrtab and it cannot be removed!\n");
      return 1;
   }

   if (last_section_index != h->e_shnum - 1) {

      /*
       * If the last section physically on file is not the last section in
       * the table, we cannot just decrease h->e_shnum, otherwise we'll remove
       * from the table an useful section. Therefore, in that case we just
       * use the slot of the last_section to store the section metainfo of the
       * section with the biggest index in the section table (last section in
       * another sense).
       */

      *last_section = sections[h->e_shnum - 1];

      /*
       * If we're so unlucky that the section with the biggest index in the
       * section table is also the special .shstrtab, we have to update its
       * index in the ELF header as well.
       */
      if (h->e_shstrndx == h->e_shnum - 1) {
         h->e_shstrndx = last_section_index;
      }
   }

   /* Drop the last section from the section table */
   h->e_shnum--;

   /*
    * Unlink all the sections depending on this one. Yes, this is rough,
    * but it's fine. Users of this script MUST know exactly what they're doing.
    * In particular, for the main use of this feature (drop of the old symtab
    * and strtab), it is expected this function to be just used twice.
    */
   for (uint32_t i = 0; i < h->e_shnum; i++)
      if (sections[i].sh_link == last_section_index)
         sections[i].sh_link = 0;

   /*
    * Unfortunately, the "bash for Windows" subsystem does not support
    * ftruncate on memory-mapped files. Even if having the Tilck to work there
    * is _not_ a must (users are supposed to use Linux), it is a nice-to-have
    * feature. Therefore, here we first unmap the memory-mapped ELF file and
    * then we truncate it.
    */
   if (munmap(nfo->vaddr, nfo->mmap_size) < 0) {
      perror("munmap() failed");
      return 1;
   }

   nfo->vaddr = NULL;

   /* Physically remove the last section from the file, by truncating it */
   if (ftruncate(nfo->fd, last_offset) < 0) {

      fprintf(stderr, "ftruncate(%i, %li) failed with '%s'\n",
              nfo->fd, last_offset, strerror(errno));

      return 1;
   }

   return 0;
}

int
set_phdr_rwx_flags(struct elf_file_info *nfo,
                   const char *phdr_index,
                   const char *flags)
{
   Elf_Ehdr *h = (Elf_Ehdr*)nfo->vaddr;
   errno = 0;

   char *endptr = NULL;
   unsigned long phindex = strtoul(phdr_index, &endptr, 10);

   if (errno || *endptr != '\0') {
      fprintf(stderr, "Invalid phdr index '%s'\n", phdr_index);
      return 1;
   }

   if (phindex >= h->e_phnum) {
      fprintf(stderr, "Phdr index %lu out-of-range [0, %u].\n",
              phindex, h->e_phnum - 1);
      return 1;
   }

   if (!flags) {
      fprintf(stderr, "Missing <rwx flags> argument.\n");
      return 1;
   }

   char *hc = (char *)h;
   Elf_Phdr *phdrs = (Elf_Phdr *)(hc + h->e_phoff);
   Elf_Phdr *phdr = phdrs + phindex;

   unsigned f = 0;

   while (*flags) {
      switch (*flags) {
         case 'r':
            f |= PF_R;
            break;
         case 'w':
            f |= PF_W;
            break;
         case 'x':
            f |= PF_X;
            break;
         default:
            fprintf(stderr, "Invalid flag '%c'. Allowed: r,w,x.\n", *flags);
            return 1;
      }
      flags++;
   }

   // First, clear the already set RWX flags (be keep the others!)
   phdr->p_flags &= ~(PF_R | PF_W | PF_X);

   // Then, set the new RWX flags.
   phdr->p_flags |= f;
   return 0;
}

int
verify_flat_elf_file(struct elf_file_info *nfo, const char *u1, const char *u2)
{
   Elf_Ehdr *h = (Elf_Ehdr*)nfo->vaddr;
   Elf_Shdr *sections = (Elf_Shdr *)((char*)h + h->e_shoff);
   Elf_Shdr *shstrtab = sections + h->e_shstrndx;
   Elf_Addr lowest_addr = (Elf_Addr) -1;
   Elf_Addr base_addr = lowest_addr;
   bool failed = false;

   if (!h->e_shnum) {
      fprintf(stderr, "ERROR: the ELF file has no sections!\n");
      return 1;
   }

   for (uint32_t i = 0; i < h->e_shnum; i++) {

      Elf_Shdr *s = sections + i;
      Elf_Phdr *phdr = get_phdr_for_section(h, s);

      if (!phdr || phdr->p_type != PT_LOAD)
         continue;

      if (s->sh_addr < lowest_addr) {
         base_addr = s->sh_addr - s->sh_offset;
         lowest_addr = s->sh_addr;
      }
   }

   for (uint32_t i = 0; i < h->e_shnum; i++) {

      Elf_Shdr *s = sections + i;
      Elf_Phdr *phdr = get_phdr_for_section(h, s);
      char *name = (char *)h + shstrtab->sh_offset + s->sh_name;

      if (!phdr || phdr->p_type != PT_LOAD)
         continue;

      Elf_Addr mem_offset = s->sh_addr - base_addr;

      if (mem_offset != s->sh_offset) {

         fprintf(stderr, "ERROR: section[%d] '%s' has "
                 "memory_offset (%p) != file_offset (%p)\n", i,
                 name, (void *)(size_t)mem_offset,
                 (void *)(size_t)s->sh_offset);

         failed = true;
      }
   }

   if (h->e_entry != lowest_addr) {
      fprintf(stderr, "ERROR: entry point (%p) != lowest load addr (%p)\n",
              (void *)(size_t)h->e_entry, (void *)(size_t)lowest_addr);
      failed = true;
   }

   if (failed) {
      fprintf(stderr, "ERROR: flat ELF check FAILED for file: %s\n", nfo->path);
      return 1;
   }

   return 0;
}

int
check_entry_point(struct elf_file_info *nfo, const char *exp, const char *u1)
{
   char buf[64];
   Elf_Ehdr *h = (Elf_Ehdr*)nfo->vaddr;

   if (!exp) {
      printf("%p\n", (void *)(size_t)h->e_entry);
      return 0;
   }

   sprintf(buf, "%p", (void *)(size_t)h->e_entry);

   if (strcmp(buf, exp)) {

      fprintf(stderr,
              "ERROR: entry point (%s) != expected (%s) for file %s\n",
              buf, exp, nfo->path);

      return 1;
   }

   return 0;
}

int
set_sym_strval(struct elf_file_info *nfo, const char *sym_name, const char *val)
{
   Elf_Ehdr *h = (Elf_Ehdr*)nfo->vaddr;
   Elf_Shdr *section;
   Elf_Sym *sym;
   size_t len;

   if (!sym_name || !val) {
      fprintf(stderr, "Missing arguments\n");
      return 1;
   }

   section = get_section(h, ".rodata");

   if (!section) {
      fprintf(stderr, "Unable to find the .rodata section\n");
      return 1;
   }

   sym = get_symbol(h, sym_name);

   if (!sym) {
      fprintf(stderr, "Unable to find the symbol '%s'\n", sym_name);
      return 1;
   }

   if (sym->st_value < section->sh_addr ||
       sym->st_value + sym->st_size > section->sh_addr + section->sh_size)
   {
      fprintf(stderr, "Symbol '%s' not in .rodata\n", sym_name);
      return 1;
   }

   len = strlen(val) + 1;

   if (sym->st_size < len) {
      fprintf(stderr, "Symbol '%s' [%u bytes] not big enough for value\n",
              sym_name, (unsigned)sym->st_size);
      return 1;
   }

   const long sym_sec_off = sym->st_value - section->sh_addr;
   const long sym_file_off = section->sh_offset + sym_sec_off;
   memcpy((char *)h + sym_file_off, val, len);
   return 0;
}

static struct elfhack_cmd cmds_list[] =
{
   {
      .opt = "--help",
      .help = "",
      .nargs = 0,
      .func = &show_help,
   },

   {
      .opt = "--dump",
      .help = "<section name>",
      .nargs = 1,
      .func = &section_dump,
   },

   {
      .opt = "--move-metadata",
      .help = "",
      .nargs = 0,
      .func = &move_metadata,
   },

   {
      .opt = "--copy",
      .help = "<src section> <dest section>",
      .nargs = 2,
      .func = &copy_section,
   },

   {
      .opt = "--rename",
      .help = "<section> <new_name>",
      .nargs = 2,
      .func = &rename_section,
   },

   {
      .opt = "--link",
      .help = "<section> <linked_section>",
      .nargs = 2,
      .func = &link_sections,
   },

   {
      .opt = "--drop-last-section",
      .help = "",
      .nargs = 0,
      .func = &drop_last_section,
   },

   {
      .opt = "--set-phdr-rwx-flags",
      .help = "<phdr index> <rwx flags>",
      .nargs = 2,
      .func = &set_phdr_rwx_flags,
   },

   {
      .opt = "--verify-flat-elf",
      .help = "",
      .nargs = 0,
      .func = &verify_flat_elf_file,
   },

   {
      .opt = "--check-entry-point",
      .help = "[<expected>]",
      .nargs = 0, /* note: the `expected` param is optional */
      .func = &check_entry_point,
   },

   {
      .opt = "--set-sym-strval",
      .help = "<sym> <string value>",
      .nargs = 2,
      .func = &set_sym_strval,
   },
};

#define printerr(...) fprintf(stderr, __VA_ARGS__)
#define print_help_line(...) printerr("    elfhack <file> " __VA_ARGS__)

int
show_help(struct elf_file_info *nfo, const char *u1, const char *u2)
{
   printerr("Usage:\n");

   for (int i = 0; i < ARRAY_SIZE(cmds_list); i++) {
      struct elfhack_cmd *c = &cmds_list[i];
      fprintf(stderr, "    elfhack <file> %s %s\n", c->opt, c->help);
   }

   return 0;
}

int
main(int argc, char **argv)
{
   struct elf_file_info nfo = {0};
   struct stat statbuf;
   size_t page_size;
   const char *opt = NULL;
   const char *opt_arg1 = NULL;
   const char *opt_arg2 = NULL;
   struct elfhack_cmd *cmd = NULL;
   int rc;

   if (argc > 2) {

      nfo.path = argv[1];
      opt = argv[2];

      if (argc > 3) {

         opt_arg1 = argv[3];

         if (argc > 4)
            opt_arg2 = argv[4];
      }

   } else {

      show_help(NULL, NULL, NULL);
      return 1;
   }

   nfo.fd = open(nfo.path, O_RDWR);

   if (nfo.fd < 0) {
      perror("open failed");
      return 1;
   }

   if (fstat(nfo.fd, &statbuf) < 0) {
      perror("fstat failed");
      close(nfo.fd);
      return 1;
   }

   page_size = sysconf(_SC_PAGESIZE);

   if (page_size <= 0) {
      fprintf(stderr, "Unable to get page size. Got: %ld\n", (long)page_size);
      close(nfo.fd);
      return 1;
   }

   nfo.mmap_size = pow2_round_up_at((size_t)statbuf.st_size, page_size);

   errno = 0;
   nfo.vaddr = mmap(NULL,                   /* addr */
                    nfo.mmap_size,          /* length */
                    PROT_READ | PROT_WRITE, /* prot */
                    MAP_SHARED,             /* flags */
                    nfo.fd,                 /* fd */
                    0);                     /* offset */

   if (errno) {
      perror(NULL);
      return 1;
   }

   for (int i = 0; i < ARRAY_SIZE(cmds_list); i++) {
      if (!strcmp(opt, cmds_list[i].opt)) {
         cmd = &cmds_list[i];
         break;
      }
   }

   if (cmd && argc-3 < cmd->nargs) {
      fprintf(stderr, "ERROR: Invalid number of arguments for %s.\n\n", opt);
      cmd = NULL;
   }

   if (!cmd)
      cmd = &cmds_list[0];    /* help */

   rc = cmd->func(&nfo, opt_arg1, opt_arg2);

   /*
    * Do munmap() only if vaddr != NULL.
    * Reason: some functions (at the moment only drop_last_section()) may
    * have their reasons for calling munmap() earlier. Do avoid double-calling
    * it and getting an error, such functions will just set vaddr to NULL.
    */
   if (nfo.vaddr && munmap(nfo.vaddr, nfo.mmap_size) < 0) {
      perror("munmap() failed");
   }

   close(nfo.fd);
   return rc;
}
