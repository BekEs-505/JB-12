#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <asl.h>

#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <sys/mman.h>
#include <mach/vm_map.h>
#include <dlfcn.h>

#include "common.h"

void fail(uint64_t x) {
    *(volatile int*)(0xbad000000000ull + x) = 0xdead;
}

#define ASSERT(x) if (!(x))fail(0xa00000000ull + __LINE__)
#undef MIN
#undef MAX
#define MIN(x,y) ((x)<(y)?(x):(y))
#define MAX(x,y) ((x)>(y)?(x):(y))

typedef void* (*t_dlsym)(void* handle, const char* symbol);
typedef int (*t_asl_log)(void*, void*, int, const char*, ...);

uint64_t memPoolStart = 0;

#define IS_DEBUG 0

void performJITMemcpy(t_dlsym _dlsym, void* dst, void* src, size_t size) {
#if IS_DEBUG
    t_asl_log _asl_log = (t_asl_log)_dlsym(RTLD_DEFAULT, "asl_log");
    _asl_log(NULL, NULL, ASL_LEVEL_ERR, "[stage1] Called performJITMemcpy: 0x%llx 0x%llx 0x%x", dst, src, size);
#endif

    uint64_t _jitWriteSeparateHeapsFunctionAddr = *(int64_t *)_dlsym(RTLD_DEFAULT, "_ZN3JSC29jitWriteSeparateHeapsFunctionE");
    // void *memPoolStartFunc = _dlsym(RTLD_DEFAULT, "_ZN3JSC36startOfFixedExecutableMemoryPoolImplEv");
    // uint64_t memPoolStart = 0;
    // if(memPoolStartFunc != NULL) {
    //     uint64_t (*_startOfFixedExecutableMemoryPoolImplEv)(void) = memPoolStartFunc;
    //     memPoolStart = _startOfFixedExecutableMemoryPoolImplEv();
    // }

 #if IS_DEBUG
    _asl_log(NULL, NULL, ASL_LEVEL_ERR, "[stage1] jitWriteSeparateHeapsFunctionAddr: 0x%llx", _jitWriteSeparateHeapsFunctionAddr);
    _asl_log(NULL, NULL, ASL_LEVEL_ERR, "[stage1] memPoolStart: 0x%llx", memPoolStart);
#endif
    
    void (*_jitWriteSeparateHeapsFunction)(const void *, const void*, size_t) = (void*)_jitWriteSeparateHeapsFunctionAddr;

    _jitWriteSeparateHeapsFunction(dst - memPoolStart, src, size);
    // size_t offset = 0;
    // #define CHUNK_SIZE 0x4000  // 4KB

    // while (size > 0) {
    //     size_t chunkSize = size > CHUNK_SIZE ? CHUNK_SIZE : size;

    //     _asl_log(NULL, NULL, ASL_LEVEL_ERR, "[stage1] _jitWriteSeparateHeapsFunction: 0x%llx", chunkSize);
    //     _jitWriteSeparateHeapsFunction((dst + offset) - memPoolStart, src + offset, chunkSize);

    //     offset += chunkSize;
    //     size -= chunkSize;
    // }

#if IS_DEBUG
    _asl_log(NULL, NULL, ASL_LEVEL_ERR, "[stage1] _jitWriteSeparateHeapsFunction done");
#endif
}

static inline uintptr_t read_uleb128(uint8_t** pp, uint8_t* end)
{
    uint8_t* p = *pp;
    uint64_t result = 0;
    int         bit = 0;
    do {
        ASSERT(p != end);
        uint64_t slice = *p & 0x7f;
        ASSERT(bit <= 63);
        else {
            result |= (slice << bit);
            bit += 7;
        }
    } while (*p++ & 0x80);

    *pp = p;
    return result;
}

static inline uintptr_t read_sleb128(uint8_t** pp, uint8_t* end)
{
    uint8_t* p = *pp;
    int64_t result = 0;
    int bit = 0;
    uint8_t byte;
    do {
        ASSERT(p != end);
        byte = *p++;
        result |= (((int64_t)(byte & 0x7f)) << bit);
        bit += 7;
    } while (byte & 0x80);
    // sign extend negative numbers
    if ( (byte & 0x40) != 0 )
        result |= (-1LL) << bit;
    *pp = p;
    return result;
}

// <3 qwerty
void rebase(struct dyld_info_command* dyld_info,
            uint8_t* map,
            uintptr_t* segstart,
            uintptr_t linkedit_base,
            uintptr_t reloc_slide) {
    uint8_t* start = map + dyld_info->rebase_off + linkedit_base;
    uint8_t* end = start + dyld_info->rebase_size;
    uintptr_t address = (uintptr_t)map;
    uintptr_t count = 0, skip = 0;
    char done = 0;
    uint8_t* p = start;
    while (!done && (p < end)) {
        uint8_t immediate = *p & REBASE_IMMEDIATE_MASK;
        uint8_t opcode = *p & REBASE_OPCODE_MASK;
        ++p;

        switch (opcode) {
            case REBASE_OPCODE_DONE:
                done = 1;
                break;
            case REBASE_OPCODE_SET_TYPE_IMM:
                ASSERT(immediate == REBASE_TYPE_POINTER);
                break;
            case REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
                address = (uintptr_t)(map + segstart[immediate] + read_uleb128(&p, end));
                break;
            case REBASE_OPCODE_ADD_ADDR_ULEB:
                address += read_uleb128(&p, end);
                break;
            case REBASE_OPCODE_ADD_ADDR_IMM_SCALED:
                address += immediate * sizeof(uintptr_t);
                break;
            case REBASE_OPCODE_DO_REBASE_IMM_TIMES:
                for (int i=0; i < immediate; ++i) {
                    *(uintptr_t*)(address) += reloc_slide;
                    address += sizeof(uintptr_t);
                }
                break;
            case REBASE_OPCODE_DO_REBASE_ULEB_TIMES:
                count = read_uleb128(&p, end);
                for (int i = 0; i < count; ++i) {
                    *(uintptr_t*)(address) += reloc_slide;
                    address += sizeof(uintptr_t);
                }
                break;
            case REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB:
                *(uintptr_t*)(address) += reloc_slide;
                address += read_uleb128(&p, end) + sizeof(uintptr_t);

                break;
            case REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB:
                count = read_uleb128(&p, end);
                skip = read_uleb128(&p, end);
                for (int i = 0; i < count; ++i) {
                    *(uintptr_t*)address += reloc_slide;
                    address += skip + sizeof(uintptr_t);
                }
                break;

            default:
                ASSERT(0);
                break;
        }
    }
}

void bindit(struct dyld_info_command* dyld_info,
            uint8_t* map,
            uintptr_t* segstart,
            uintptr_t linkedit_base,
            t_dlsym _dlsym) {
    uint8_t* start = map + dyld_info->bind_off + linkedit_base;
    uint8_t* end = start + dyld_info->bind_size;
    uintptr_t address = (uintptr_t)map;
    uintptr_t count = 0, skip = 0;
    char done = 0;
    unsigned char type = 0;
    uint8_t* p = start;
    char* symbolName=0;

    while (!done && (p < end)) {
        uint8_t immediate = *p & BIND_IMMEDIATE_MASK;
        uint8_t opcode = *p & BIND_OPCODE_MASK;
        ++p;
        switch (opcode) {
            case BIND_OPCODE_DONE:
                done = 1;
                break;
            case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
                break;
            case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
                read_uleb128(&p, end);
                break;
            case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
                break;
            case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
                symbolName = (char*)p;
                while (*p != '\0')
                    ++p;
                ++p;
                break;
            case BIND_OPCODE_SET_TYPE_IMM:
                break;
            case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
                address = (uintptr_t)(map + segstart[immediate] + read_uleb128(&p, end));
                break;
            case BIND_OPCODE_SET_ADDEND_SLEB:
                ASSERT(0);
                break;
            case BIND_OPCODE_ADD_ADDR_ULEB:
                address += read_uleb128(&p, end);
                break;
            case BIND_OPCODE_DO_BIND:
                *(uintptr_t*)address = (uintptr_t)_dlsym(RTLD_DEFAULT, symbolName+1);
                address += sizeof(uintptr_t);
                break;
            case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
                *(uintptr_t*)address = (uintptr_t)_dlsym(RTLD_DEFAULT, symbolName+1);
                address += read_uleb128(&p, end) + sizeof(uintptr_t);
                break;
            case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
                *(uintptr_t*)address = (uintptr_t)_dlsym(RTLD_DEFAULT, symbolName+1);
                address += (immediate + 1) * sizeof(uintptr_t);
                break;
            case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
                count = read_uleb128(&p, end);
                skip = read_uleb128(&p, end);
                for (uint32_t i = 0; i < count; ++i) {
                    *(uintptr_t*)address = (uintptr_t)_dlsym(RTLD_DEFAULT, symbolName+1);
                    address += skip + sizeof(uintptr_t);
                }
                break;
            default:
                ASSERT(0);
        }
    }
}

void load(void* buffer, t_dlsym _dlsym, void* jitend, uint64_t jitStart) {
    memPoolStart = jitStart;

    t_asl_log _asl_log = (t_asl_log)_dlsym(RTLD_DEFAULT, "asl_log");
    _asl_log(NULL, NULL, ASL_LEVEL_ERR, "[stage1] loaded");
    
#   define FOR_COMMAND \
        lc = (void*)(header + 1); \
        for (int i = 0; i < header->ncmds; ++i, lc = (void*)((char*)lc + lc->cmdsize)) { \

#   define FOR_SEGMENT_64 \
        FOR_COMMAND \
            if (lc->cmd != LC_SEGMENT_64) \
                continue; \
            struct segment_command_64* sc = (void*)lc; \
            if (!_strcmp(sc->segname, "__PAGEZERO")) \
                continue;

    void* (*_mmap)(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
    void* (*_memcpy)(void *restrict dst, const void *restrict src, size_t n);
    int (*_strcmp)(const char *s1, const char *s2);
    int (*_mlock)(const void *addr, size_t len);

    _mlock = _dlsym(RTLD_DEFAULT, "mlock");
    _mmap = _dlsym(RTLD_DEFAULT, "mmap");
    _memcpy = _dlsym(RTLD_DEFAULT, "memcpy");
    _strcmp = _dlsym(RTLD_DEFAULT, "strcmp");

    uintptr_t exec_base = -1, exec_end = 0,
             write_base = -1, write_end = 0,
             base = -1, end = 0;

    uint32_t* x = (uint32_t*)buffer;
    while (*x != 0xfeedfacf)
        x--;
    
    // _asl_log(NULL, NULL, ASL_LEVEL_ERR, "[stage1] Found macho: %p", x);

    struct mach_header_64* header = (struct mach_header_64*)x;
    struct load_command* lc;
    uintptr_t linkedit_base = 0;
    uintptr_t segstart[32];
    int segcnt = 0;

    FOR_SEGMENT_64
        uintptr_t from = sc->vmaddr, to = from + sc->vmsize;
        segstart[segcnt++] = from;
        if (!_strcmp(sc->segname, "__LINKEDIT"))
            linkedit_base = sc->vmaddr - sc->fileoff;
        if (sc->initprot & VM_PROT_EXECUTE) {
            exec_base = MIN(exec_base, from);
            exec_end = MAX(exec_end, to);
        }
        if (sc->initprot & VM_PROT_WRITE) {
            write_base = MIN(write_base, from);
            write_end = MAX(write_end, to);
        }
        base = MIN(base, from);
        end = MAX(end, to);
    }

    uint8_t* tmpmap = _mmap(0, end - base, PROT_WRITE | PROT_READ,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT(tmpmap);

    FOR_SEGMENT_64
        _memcpy(tmpmap + sc->vmaddr, (char*)header + sc->fileoff, sc->filesize);
    }

    ASSERT(write_base >= exec_end);
    void* rw = _mmap(jitend, end - write_base, PROT_READ|PROT_WRITE,
                    MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    ASSERT(rw == jitend);

    uint8_t* finalmap = jitend - write_base + base;

    uintptr_t reloc_slide = (uintptr_t)finalmap;

    FOR_COMMAND
        if (lc->cmd == LC_DYLD_INFO_ONLY || lc->cmd == LC_DYLD_INFO) {
            rebase((void*)lc, tmpmap, segstart, linkedit_base, reloc_slide);
            bindit((void*)lc, tmpmap, segstart, linkedit_base, _dlsym);
        }
    }

    performJITMemcpy(_dlsym, finalmap, tmpmap, write_base - base);
    _memcpy(rw, tmpmap + write_base - base, end - write_base);

    void (*entrypoint)();
    FOR_SEGMENT_64
        uint64_t* x = (void*)(finalmap + sc->vmaddr);
        while ((char*)x != (char*)(finalmap + sc->vmaddr + sc->vmsize)) {
            if (*x == MAGIC) {
                entrypoint = (void*)*(x+1);
            }
            x++;
        }
    }

    entrypoint();
}
