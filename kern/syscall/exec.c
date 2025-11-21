#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>      // PATH_MAX, ARG_MAX
#include <lib.h>              // kmalloc, kfree, strlen, ROUNDUP
#include <copyinout.h>        // copyin, copyout, copyinstr
#include <vfs.h>              // vfs_open, vfs_close
#include <vnode.h>
#include <proc.h>             // curproc_setas
#include <addrspace.h>        // as_create, as_destroy, as_define_stack, as_activate
#include <elf.h>              // load_elf
#include <current.h>
#include <thread.h>           // enter_new_process
#include <syscall.h>

#define ALIGN4(x)   ((x) & ~(vaddr_t)3)
