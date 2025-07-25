#include "krw.h"
#include "kutils.h"
#include <stdint.h>
#include <mach/mach.h> 
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

extern mach_port_t g_hsp4;

#ifndef MIN
#    define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

kern_return_t
kreadbuf(uint64_t kaddr, void *buf, size_t sz) {
    mach_vm_address_t p = (mach_vm_address_t)buf;
    mach_vm_size_t read_sz, out_sz = 0;

    while(sz != 0) {
        read_sz = MIN(sz, vm_kernel_page_size - (kaddr & vm_kernel_page_mask));
        if(mach_vm_read_overwrite(g_hsp4, kaddr, read_sz, p, &out_sz) != KERN_SUCCESS || out_sz != read_sz) {
            return KERN_FAILURE;
        }
        p += read_sz;
        sz -= read_sz;
        kaddr += read_sz;
    }
    return KERN_SUCCESS;
}

kern_return_t
kwritebuf(uint64_t kaddr, const void *buf, size_t sz) {
    vm_machine_attribute_val_t mattr_val = MATTR_VAL_CACHE_FLUSH;
    mach_vm_address_t p = (mach_vm_address_t)buf;
    mach_msg_type_number_t write_sz;

    while(sz != 0) {
        write_sz = (mach_msg_type_number_t)MIN(sz, vm_kernel_page_size - (kaddr & vm_kernel_page_mask));
        if(mach_vm_write(g_hsp4, kaddr, p, write_sz) != KERN_SUCCESS || mach_vm_machine_attribute(g_hsp4, kaddr, write_sz, MATTR_CACHE, &mattr_val) != KERN_SUCCESS) {
            return KERN_FAILURE;
        }
        p += write_sz;
        sz -= write_sz;
        kaddr += write_sz;
    }
    return KERN_SUCCESS;
}

uint32_t kread32(uint64_t where) {
    uint32_t out;
    kreadbuf(where, &out, sizeof(uint32_t));
    return out;
}

uint64_t kread64(uint64_t where) {
    uint64_t out;
    kreadbuf(where, &out, sizeof(uint64_t));
    return out;
}

void kwrite32(uint64_t where, uint32_t what) {
    uint32_t _what = what;
    kwritebuf(where, &_what, sizeof(uint32_t));
}

void kwrite64(uint64_t where, uint64_t what) {
    uint64_t _what = what;
    kwritebuf(where, &_what, sizeof(uint64_t));
}

uint64_t kalloc(size_t sz) {
    mach_vm_address_t va = 0;
    kern_return_t ret = mach_vm_allocate(g_hsp4, &va, sz, VM_FLAGS_ANYWHERE);
    if(ret == KERN_SUCCESS) {
        return va;
    }
    return -1;
}

uint64_t kalloc_wired(uint64_t size) {
    kern_return_t err;
    mach_vm_address_t addr = 0;
    mach_vm_size_t ksize = round_page_kernel(size);
    
    printf("vm_kernel_page_size: %lx\n", vm_kernel_page_size);
    
    err = mach_vm_allocate(g_hsp4, &addr, ksize+0x4000, VM_FLAGS_ANYWHERE);
    if (err != KERN_SUCCESS) {
        printf("unable to allocate kernel memory via tfp0: %s %x\n", mach_error_string(err), err);
        sleep(3);
        return 0;
    }
    
    printf("allocated address: %llx\n", addr);
    
    addr += 0x3fff;
    addr &= ~0x3fffull;
    
    printf("address to wire: %llx\n", addr);
    
    err = mach_vm_wire(fake_host_priv(), g_hsp4, addr, ksize, VM_PROT_READ|VM_PROT_WRITE);
    if (err != KERN_SUCCESS) {
        printf("unable to wire kernel memory via tfp0: %s %x\n", mach_error_string(err), err);
        sleep(3);
        return 0;
    }
    return addr;
}

void kfree(uint64_t kaddr, size_t sz) {
    kern_return_t ret = mach_vm_deallocate(g_hsp4, kaddr, sz);
    if(ret == KERN_SUCCESS)
    {
        return;
    }
    printf("kfree failed\n");
    exit(1);
}

void rkbuffer(uint64_t kaddr, void* buffer, uint32_t length) {
    kern_return_t err;
    mach_vm_size_t outsize = 0;
    err = mach_vm_read_overwrite(g_hsp4,
                                 (mach_vm_address_t)kaddr,
                                 (mach_vm_size_t)length,
                                 (mach_vm_address_t)buffer,
                                 &outsize);
    if (err != KERN_SUCCESS){
      printf("hsp4 read failed %s addr: 0x%llx err:%x port:%x\n", mach_error_string(err), kaddr, err, g_hsp4);
      sleep(3);
      return;
    }
    
    if (outsize != length){
      printf("hsp4 read was short (expected %lx, got %llx\n", sizeof(uint32_t), outsize);
      sleep(3);
      return;
    }
  }

// copy a NULL terminated string from the kernel to the userspace buffer, up to a max of length bytes
void rkstring(uint64_t kaddr, void* buffer, uint32_t length) {
    uint8_t ch;
    size_t offset = 0;
    uint8_t* output_string = buffer;
    do {
      ch = kread32(kaddr + offset) & 0xff;
      output_string[offset++] = ch;
    } while (ch && offset < length);
  }

const uint64_t kernel_address_space_base = 0xffff000000000000;
void kmemcpy(uint64_t dest, uint64_t src, uint32_t length) {
    if (dest >= kernel_address_space_base) {
      // copy to kernel:
      kwritebuf(dest, (void*) src, length);
    } else {
      // copy from kernel
      kreadbuf(src, (void*)dest, length);
    }
}

void khexdump(uint64_t addr, size_t size) {
    void *data = malloc(size);
    kreadbuf(addr, data, size);
    char ascii[17];
    size_t i, j;
    ascii[16] = '\0';
    for (i = 0; i < size; ++i) {
        if ((i % 16) == 0)
        {
            printf("[0x%016llx+0x%03zx] ", addr, i);
//            printf("[0x%016llx] ", i + addr);
        }
        
        printf("%02X ", ((unsigned char*)data)[i]);
        if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
            ascii[i % 16] = ((unsigned char*)data)[i];
        } else {
            ascii[i % 16] = '.';
        }
        if ((i+1) % 8 == 0 || i+1 == size) {
            printf(" ");
            if ((i+1) % 16 == 0) {
                printf("|  %s \n", ascii);
            } else if (i+1 == size) {
                ascii[(i+1) % 16] = '\0';
                if ((i+1) % 16 <= 8) {
                    printf(" ");
                }
                for (j = (i+1) % 16; j < 16; ++j) {
                    printf("   ");
                }
                printf("|  %s \n", ascii);
            }
        }
    }
    free(data);
}