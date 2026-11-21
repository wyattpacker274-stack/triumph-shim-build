// CydiaSubstrate shim for non-jailbroken iOS
// MSHookMessageEx = full (ObjC runtime). MSHookFunction = fishhook-style rebinding.
// Compile:
//   xcrun -sdk iphoneos clang -shared -fobjc-arc -framework Foundation -framework UIKit \
//     -arch arm64 -miphoneos-version-min=13.0 -o CydiaSubstrate CydiaSubstrate.c

#import <objc/runtime.h>
#import <objc/message.h>
#import <dlfcn.h>
#import <stdio.h>
#import <string.h>
#import <stdlib.h>
#import <mach/mach.h>
#import <mach-o/dyld.h>
#import <mach-o/loader.h>
#import <mach-o/nlist.h>
#import <pthread.h>

// Foundation MUST come after objc/runtime.h to avoid forward-decl collisions on newer SDKs
#import <Foundation/Foundation.h>

#ifndef INDIRECT_SYMBOL_LOCAL
#define INDIRECT_SYMBOL_LOCAL   0x80000000
#endif
#ifndef INDIRECT_SYMBOL_ABS
#define INDIRECT_SYMBOL_ABS     0x40000000
#endif

typedef struct { const char *name; void *replacement; void **replaced; } RB;
static RB  _rb_list[512];
static int _rb_count = 0;
static pthread_mutex_t _rb_lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t _linkedit_fileoff(const struct mach_header_64 *hdr) {
    uint8_t *cur = (uint8_t *)(hdr + 1);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)cur;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)cur;
            if (strcmp(seg->segname, "__LINKEDIT") == 0) return seg->fileoff;
        }
        cur += lc->cmdsize;
    }
    return 0;
}

static void rebind_image(const struct mach_header *mh, intptr_t slide) {
    const struct mach_header_64 *hdr = (const struct mach_header_64 *)mh;
    uint64_t le_fileoff = _linkedit_fileoff(hdr);

    uintptr_t linkedit_vm = 0;
    uint64_t symtab_off = 0, strtab_off = 0, indirect_off = 0;
    uint32_t nsyms = 0, nindirect = 0;

    uint8_t *cur = (uint8_t *)(hdr + 1);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)cur;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)cur;
            if (strcmp(seg->segname, "__LINKEDIT") == 0)
                linkedit_vm = (uintptr_t)(seg->vmaddr + slide);
        } else if (lc->cmd == LC_SYMTAB) {
            struct symtab_command *st = (struct symtab_command *)cur;
            symtab_off = st->symoff; strtab_off = st->stroff; nsyms = st->nsyms;
        } else if (lc->cmd == LC_DYSYMTAB) {
            struct dysymtab_command *ds = (struct dysymtab_command *)cur;
            indirect_off = ds->indirectsymoff; nindirect = ds->nindirectsyms;
        }
        cur += lc->cmdsize;
    }
    if (!linkedit_vm || !nsyms || !nindirect) return;

    uintptr_t symtab_addr = linkedit_vm + (symtab_off  - le_fileoff);
    uintptr_t strtab_addr = linkedit_vm + (strtab_off  - le_fileoff);
    uint32_t *ind         = (uint32_t *)(linkedit_vm + (indirect_off - le_fileoff));
    const struct nlist_64 *symtab = (const struct nlist_64 *)symtab_addr;
    const char *strtab = (const char *)strtab_addr;

    cur = (uint8_t *)(hdr + 1);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)cur;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)cur;
            if (strcmp(seg->segname, "__DATA") == 0 || strcmp(seg->segname, "__DATA_CONST") == 0) {
                uint8_t *sec = cur + sizeof(struct segment_command_64);
                for (uint32_t s = 0; s < seg->nsects; s++) {
                    struct section_64 *sect = (struct section_64 *)sec;
                    uint8_t type = sect->flags & SECTION_TYPE;
                    if (type == S_NON_LAZY_SYMBOL_POINTERS || type == S_LAZY_SYMBOL_POINTERS) {
                        uint32_t first = sect->reserved1;
                        if ((uint64_t)first + sect->size/4 > nindirect) { sec += sizeof(*sect); continue; }
                        uintptr_t *slots = (uintptr_t *)(sect->addr + slide);
                        for (uint32_t e = 0; e < sect->size/8; e++) {
                            uint32_t si = ind[first + e];
                            if (si == INDIRECT_SYMBOL_LOCAL || si == INDIRECT_SYMBOL_ABS) continue;
                            if (si >= nsyms) continue;
                            const char *nm = strtab + symtab[si].n_un.n_strx;
                            if (!nm || !*nm) continue;

                            pthread_mutex_lock(&_rb_lock);
                            for (int r = 0; r < _rb_count; r++) {
                                const char *target = _rb_list[r].name;
                                const char *a = nm[0] == '_' ? nm+1 : nm;
                                const char *b = target[0] == '_' ? target+1 : target;
                                if (strcmp(a, b) != 0) continue;
                                uintptr_t *slot = slots + e;
                                vm_address_t page = (vm_address_t)slot & ~0xFFFULL;
                                kern_return_t kr = mach_vm_protect(mach_task_self(), page, 0x1000, false,
                                        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
                                if (kr != KERN_SUCCESS)
                                    mach_vm_protect(mach_task_self(), page, 0x1000, false,
                                        VM_PROT_READ | VM_PROT_WRITE);
                                if (kr == KERN_SUCCESS || true) {
                                    if (_rb_list[r].replaced) *(_rb_list[r].replaced) = (void *)*slot;
                                    *slot = (uintptr_t)_rb_list[r].replacement;
                                }
                            }
                            pthread_mutex_unlock(&_rb_lock);
                        }
                    }
                    sec += sizeof(*sect);
                }
            }
        }
        cur += lc->cmdsize;
    }
}

static void rebind_all(void) {
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        rebind_image(_dyld_get_image_header(i), _dyld_get_image_vmaddr_slide(i));
    }
}

// --- public API -------------------------------------------------------------
__attribute__((visibility("default")))
void MSHookMessageEx(Class cls, SEL sel, IMP hook, IMP *old) {
    if (!cls || !sel) return;
    Method m = class_getInstanceMethod(cls, sel);
    if (!m) m = class_getClassMethod(cls, sel);
    if (!m) {
        NSLog(@"[SubstrateShim] no method %@ on %s", NSStringFromSelector(sel), class_getName(cls));
        return;
    }
    IMP orig = method_setImplementation(m, hook);
    if (old) *old = orig;
    NSLog(@"[SubstrateShim] hooked -[%@ %@]", class_getName(cls), NSStringFromSelector(sel));
}

__attribute__((visibility("default")))
void *MSHookFunction(void *symbol, void *replace, void **result) {
    if (!symbol) return NULL;
    pthread_mutex_lock(&_rb_lock);
    Dl_info di;
    if (_rb_count < 512 && dladdr(symbol, &di) && di.dli_sname) {
        _rb_list[_rb_count].name = strdup(di.dli_sname);
        _rb_list[_rb_count].replacement = replace;
        _rb_list[_rb_count].replaced = result;
        NSLog(@"[SubstrateShim] MSHookFunction: %s", di.dli_sname);
        _rb_count++;
    } else {
        NSLog(@"[SubstrateShim] MSHookFunction: cannot resolve %p", symbol);
    }
    pthread_mutex_unlock(&_rb_lock);
    rebind_all();
    if (result) *result = symbol;
    return symbol;
}

__attribute__((visibility("default")))
void MSHookFlush(void) { rebind_all(); }

__attribute__((visibility("default")))
void MSHookProcess(pid_t pid, const char *lib) { (void)pid; (void)lib; }

__attribute__((constructor))
static void shim_init(void) { NSLog(@"[SubstrateShim] loaded"); }
