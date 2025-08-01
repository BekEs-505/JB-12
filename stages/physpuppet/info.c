#include "info.h"
#include "libprejailbreak.h"

#include <stdio.h>

uint64_t get_kernel_slide(void) {
    if (!proc_self()) return 0;
    uint64_t crLabel = kread_ptr(ucred_self() + koffsetof(ucred, label));
    uint64_t entitlements = kread_ptr(crLabel + 0x8);
    uint64_t vtable = kread_ptr(entitlements);
    uint64_t kernelPage = vtable & ~(uint64_t)0xFFF;
    
    while (kernelPage > 0xFFFFFFF007004000) {
        uint32_t header = kread32(kernelPage);
        if (header == 0xFEEDFACF) {
            return kernelPage - (uint64_t)0xFFFFFFF007004000;
        }
        kernelPage -= 0x1000;
    }
    return 0;
}

int info_init(uint64_t task) {
    pinfo(task) = task;
    if (!pinfo(task)) {
        return -1;
    }
    pinfo(proc) = kread_ptr(task + koffsetof(task, bsd_info));
    if (!pinfo(proc)) {
        return -1;
    }
    pinfo(ucred) = kread_ptr(proc_self() + koffsetof(proc, ucred));
    if (!pinfo(ucred)) {
        return -1;
    }
    pinfo(vm_map) = kread_ptr(task_self() + koffsetof(task, vm_map));
    if (!pinfo(vm_map)) {
        return -1;
    }
    pinfo(pmap) = kread_ptr(vm_map_self() + koffsetof(vm_map, pmap));
    if (!pinfo(pmap)) {
        return -1;
    }

    kinfo(slide) = get_kernel_slide();
    if (!kinfo(slide)) {
        return -1;
    }

    uint64_t curProc = proc_self();
    while (curProc >= 0xffffffe000000000) {
      pid_t curPid = kread64(curProc + koffsetof(proc, pid));
      if (curPid == 0) kinfo(proc) = curProc;
      curProc = kread64(curProc + koffsetof(proc, next));
    }
    if (!kinfo(proc)) {
        return -1;
    }

    kinfo(task) = kread_ptr(kinfo(proc) + koffsetof(proc, task));
    if (!kinfo(task)) {
        return -1;
    }
    kinfo(ucred) = kread_ptr(kinfo(proc) + koffsetof(proc, ucred));
    if (!kinfo(ucred)) {
        return -1;
    }
    kinfo(vm_map) = kread_ptr(kinfo(task) + koffsetof(task, vm_map));
    if (!kinfo(vm_map)) {
        return -1;
    }
    kinfo(pmap) = kread_ptr(kinfo(vm_map) + koffsetof(vm_map, pmap));
    if (!kinfo(pmap)) {
        return -1;
    }

    return 0;
}