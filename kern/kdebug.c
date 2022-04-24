#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/dwarf.h>
#include <inc/elf.h>
#include <inc/x86.h>

#include <kern/kdebug.h>
#include <kern/pmap.h>
#include <kern/env.h>
#include <inc/uefi.h>

void
load_kernel_dwarf_info(struct Dwarf_Addrs *addrs) {
    addrs->aranges_begin = (uint8_t *)(uefi_lp->DebugArangesStart);
    addrs->aranges_end = (uint8_t *)(uefi_lp->DebugArangesEnd);
    addrs->abbrev_begin = (uint8_t *)(uefi_lp->DebugAbbrevStart);
    addrs->abbrev_end = (uint8_t *)(uefi_lp->DebugAbbrevEnd);
    addrs->info_begin = (uint8_t *)(uefi_lp->DebugInfoStart);
    addrs->info_end = (uint8_t *)(uefi_lp->DebugInfoEnd);
    addrs->line_begin = (uint8_t *)(uefi_lp->DebugLineStart);
    addrs->line_end = (uint8_t *)(uefi_lp->DebugLineEnd);
    addrs->str_begin = (uint8_t *)(uefi_lp->DebugStrStart);
    addrs->str_end = (uint8_t *)(uefi_lp->DebugStrEnd);
    addrs->pubnames_begin = (uint8_t *)(uefi_lp->DebugPubnamesStart);
    addrs->pubnames_end = (uint8_t *)(uefi_lp->DebugPubnamesEnd);
    addrs->pubtypes_begin = (uint8_t *)(uefi_lp->DebugPubtypesStart);
    addrs->pubtypes_end = (uint8_t *)(uefi_lp->DebugPubtypesEnd);
}

#define UNKNOWN       "<unknown>"
#define CALL_INSN_LEN 5

/* debuginfo_rip(addr, info)
 * Fill in the 'info' structure with information about the specified
 * instruction address, 'addr'.  Returns 0 if information was found, and
 * negative if not.  But even if it returns negative it has stored some
 * information into '*info'
 */
int
debuginfo_rip(uintptr_t addr, struct Ripdebuginfo *info) {

    /* Initialize *info */
    strcpy(info->rip_file, UNKNOWN);
    strcpy(info->rip_fn_name, UNKNOWN);
    info->rip_fn_namelen = sizeof UNKNOWN - 1;
    info->rip_line = 0;
    info->rip_fn_addr = addr;
    info->rip_fn_narg = 0;

    if (!addr) return 0;

    struct Dwarf_Addrs addrs;
    assert(addr >= MAX_USER_READABLE);
    load_kernel_dwarf_info(&addrs);

    Dwarf_Off offset = 0, line_offset = 0;
    int res = info_by_address(&addrs, addr, &offset);
    if (res < 0) goto error;

    char *tmp_buf = NULL;
    res = file_name_by_info(&addrs, offset, &tmp_buf, &line_offset);
    if (res < 0) goto error;
    strncpy(info->rip_file, tmp_buf, sizeof(info->rip_file));
    info->rip_file[RIPDEBUG_BUFSIZ - 1] = 0;

    /* Find line number corresponding to given address.
    * Hint: note that we need the address of `call` instruction, but rip holds
    * address of the next instruction, so we should substract 5 from it.
    * Hint: use line_for_address from kern/dwarf_lines.c */

    // LAB 2: Your res here:
    res = line_for_address(&addrs, addr - 5, line_offset, &(info->rip_line));
    if (res < 0) goto error;

    /* Find function name corresponding to given address.
    * Hint: note that we need the address of `call` instruction, but rip holds
    * address of the next instruction, so we should substract 5 from it.
    * Hint: use function_by_info from kern/dwarf.c
    * Hint: info->rip_fn_name can be not NULL-terminated,
    * string returned by function_by_info will always be */

    // LAB 2: Your res here:
    res = function_by_info(&addrs, addr - 5, offset, &tmp_buf, &(info->rip_fn_addr));
    if (res < 0) goto error;
    info->rip_fn_namelen = strnlen(tmp_buf, RIPDEBUG_BUFSIZ - 1);
    strncpy(info->rip_fn_name, tmp_buf, info->rip_fn_namelen);
    info->rip_fn_name[info->rip_fn_namelen] = 0;

error:
    return res;
}

uintptr_t
find_function(const char *const fname) {
    /* There are two functions for function name lookup.
     * address_by_fname, which looks for function name in section .debug_pubnames
     * and naive_address_by_fname which performs full traversal of DIE tree.
     * It may also be useful to look to kernel symbol table for symbols defined
     * in assembly. */

    // LAB 3: Your code here:

    struct Dwarf_Addrs addrs;
    load_kernel_dwarf_info(&addrs);
    uintptr_t offset = 0;

    int res = address_by_fname(&addrs, fname, &offset);
    if (res == 0 && offset)
        return offset;

    res = naive_address_by_fname(&addrs, fname, &offset);
    if (res == 0 && offset)
        return offset;

    struct Elf64_Sym * symtab = (struct Elf64_Sym *)uefi_lp->SymbolTableStart;
    struct Elf64_Sym * symtab_end = (struct Elf64_Sym *)uefi_lp->SymbolTableEnd;
    char * strtab = (char *)uefi_lp->StringTableStart;
    //char * strtab_end = (char *)uefi_lp->StringTableEnd;

    for (; symtab != symtab_end; ++symtab)
    {
        if (ELF64_ST_BIND(symtab->st_info) != STB_GLOBAL)
            continue;
        if (ELF64_ST_TYPE(symtab->st_info) != STT_FUNC)
            continue;

        const char * symname = strtab + symtab->st_name;
        //cprintf("find_function: found %s\n", symname);
        if (strcmp(fname, symname) != 0)
            continue;

        return symtab->st_value;
    }

    return 0;
}

void
print_backtrace() {

    cprintf("Stack backtrace:\n");

    uint64_t *rbp = (uint64_t *)read_rbp();
    while (rbp)
    {
        uint64_t rip = rbp[1];
        cprintf("  rbp %016lx  rip %016lx\n", (uint64_t)rbp, rip);

        struct Ripdebuginfo info;
        debuginfo_rip(rip, &info);
        cprintf("    %s:%d: %s+%lu\n", info.rip_file, info.rip_line, info.rip_fn_name, rip - info.rip_fn_addr);

        rbp = (uint64_t *)rbp[0];
    }
}
