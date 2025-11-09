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

/* ---------------------------- Small helpers -------------------------------- */

/* Copy program path from user to a fixed-size kernel buffer. */
static int
copyin_program_path(const char *u_program, char out_path[PATH_MAX])
{
    size_t got = 0;
    if (u_program == NULL) return EFAULT;

    int r = copyinstr((const_userptr_t)u_program, out_path, PATH_MAX, &got);
    if (r) return r;
    if (out_path[0] == '\0') return EINVAL;  // empty path not allowed
    return 0;
}

/* Safely count argc by reading user pointers until NULL. */
static int
count_user_args(char **u_args, int *out_argc)
{
    if (u_args == NULL) return EFAULT;

    int argc = 0;
    while (1) {
        char *u_ptr = NULL;
        int r = copyin((const_userptr_t)&u_args[argc], &u_ptr, sizeof(u_ptr));
        if (r) return r;
        if (u_ptr == NULL) break;
        argc++;

        /* Soft guard: avoid unbounded scans if user passes a bad vector. */
        if (argc > (ARG_MAX / (int)sizeof(char *))) return E2BIG;
    }
    *out_argc = argc;
    return 0;
}

/* Free a kargv created by copyin_args_vector. */
static void
free_kargv(char **kargv, int argc)
{
    if (kargv == NULL) return;
    for (int i = 0; i < argc; i++) {
        if (kargv[i] != NULL) kfree(kargv[i]);
    }
    kfree(kargv);
}

/*
 * Copy argv[] strings from user into freshly allocated kernel copies.
 * Also computes the total bytes the strings will consume on the user stack
 * (including per-string alignment to 4 bytes).
 */
static int
copyin_args_vector(char **u_args, int argc, char ***out_kargv, size_t *out_data_bytes)
{
    char **kargv = kmalloc(sizeof(char *) * (argc + 1));
    if (kargv == NULL) return ENOMEM;

    size_t data_bytes = 0;

    for (int i = 0; i < argc; i++) {
        char *u_ptr = NULL;
        int r = copyin((const_userptr_t)&u_args[i], &u_ptr, sizeof(u_ptr));
        if (r) { free_kargv(kargv, i); return r; }

        /* Two-step copy for clarity: bounded temp to get exact length, then allocate exact and re-copy. */
        char temp[PATH_MAX];
        size_t got = 0;
        r = copyinstr((const_userptr_t)u_ptr, temp, sizeof(temp), &got);
        if (r) { free_kargv(kargv, i); return r; }   /* ENAMETOOLONG if arg > PATH_MAX */

        kargv[i] = kmalloc(got);
        if (kargv[i] == NULL) { free_kargv(kargv, i); return ENOMEM; }

        r = copyinstr((const_userptr_t)u_ptr, kargv[i], got, NULL);
        if (r) { free_kargv(kargv, i + 1); return r; }

        data_bytes += got;                   /* include '\0' */
        data_bytes = ROUNDUP(data_bytes, 4); /* maintain 4-byte stack alignment per-arg */
    }
    kargv[argc] = NULL;

    *out_kargv = kargv;
    *out_data_bytes = data_bytes;
    return 0;
}

/* Switch to a fresh address space; return old as for rollback. */
static int
create_and_switch_as(struct addrspace **out_newas, struct addrspace **out_oldas)
{
    struct addrspace *newas = as_create();
    if (newas == NULL) return ENOMEM;

    struct addrspace *oldas = proc_setas(newas);
    as_activate();

    *out_newas = newas;
    *out_oldas = oldas;
    return 0;
}

/* Roll back to the old address space and destroy the newly created one. */
static void
rollback_as(struct addrspace *newas, struct addrspace *oldas)
{
    proc_setas(oldas);
    as_activate();
    as_destroy(newas);
}

/*
 * Marshal argv on the user stack in the current address space.
 * Layout (low -> high): [strings ...][padding][argv[0]..argv[argc-1]][NULL]
 */
static int
push_args_to_stack(vaddr_t *io_stackptr, char **kargv, int argc, userptr_t *out_uargv)
{
    /* Pre-check: compute total size and compare against ARG_MAX. */
    size_t data_bytes = 0;
    for (int i = 0; i < argc; i++) {
        data_bytes += (strlen(kargv[i]) + 1);
        data_bytes = ROUNDUP(data_bytes, 4);
    }
    size_t ptr_bytes = ROUNDUP((argc + 1) * sizeof(userptr_t), 4);
    if (ptr_bytes + data_bytes > ARG_MAX) return E2BIG;

    userptr_t *uargv_ptrs = kmalloc(sizeof(userptr_t) * (argc + 1));
    if (uargv_ptrs == NULL) return ENOMEM;

    vaddr_t sp = *io_stackptr;
    int r = 0;

    /* Copy strings from high to low, saving their user addresses. */
    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(kargv[i]) + 1;     /* include terminator */
        sp -= len;
        sp = ALIGN4(sp);                       /* 4-byte alignment */
        r = copyout(kargv[i], (userptr_t)sp, len);
        if (r) { kfree(uargv_ptrs); return r; }
        uargv_ptrs[i] = (userptr_t)sp;
    }
    uargv_ptrs[argc] = NULL;

    /* Copy the argv[] pointer array. */
    sp -= (argc + 1) * sizeof(userptr_t);
    sp = ALIGN4(sp);
    r = copyout(uargv_ptrs, (userptr_t)sp, (argc + 1) * sizeof(userptr_t));
    kfree(uargv_ptrs);
    if (r) return r;

    *io_stackptr = sp;
    *out_uargv = (userptr_t)sp;
    return 0;
}

/* ------------------------------ sys_execv ---------------------------------- */

int
exec_sys_execv(const char *program, char **args)
{
    /* 0) Cheap pointer sanity: fail fast on NULLs. */
    if (program == NULL || args == NULL) return EFAULT;

    /* 1) Copy program path. */
    char kprog[PATH_MAX];
    int r = copyin_program_path(program, kprog);
    if (r) return r;

    /* 2) Count argc. */
    int argc = 0;
    r = count_user_args(args, &argc);
    if (r) return r;

    /* 3) Copy argv[] into kernel and estimate stack footprint. */
    char **kargv = NULL;
    size_t _unused_data_bytes = 0; /* kept for clarity, checked again inside push_args_to_stack */
    r = copyin_args_vector(args, argc, &kargv, &_unused_data_bytes);
    if (r) return r;

    /* 4) Open executable. */
    struct vnode *v = NULL;
    r = vfs_open(kprog, O_RDONLY, 0, &v);
    if (r) { free_kargv(kargv, argc); return r; }

    /* 5) Create and switch to a fresh address space (keep old for rollback). */
    struct addrspace *newas = NULL, *oldas = NULL;
    r = create_and_switch_as(&newas, &oldas);
    if (r) { vfs_close(v); free_kargv(kargv, argc); return r; }

    /* 6) Load ELF into the current (new) address space. */
    vaddr_t entry = 0;
    r = load_elf(v, &entry);
    vfs_close(v);
    if (r) { rollback_as(newas, oldas); free_kargv(kargv, argc); return r; }

    /* 7) Define user stack. */
    vaddr_t stackptr = 0;
    r = as_define_stack(newas, &stackptr);
    if (r) { rollback_as(newas, oldas); free_kargv(kargv, argc); return r; }

    /* 8) Push argv onto the user stack. */
    userptr_t uargv = NULL;
    r = push_args_to_stack(&stackptr, kargv, argc, &uargv);
    free_kargv(kargv, argc);
    if (r) { rollback_as(newas, oldas); return r; }

    /* 9) Commit: destroy the old address space and jump to user mode. */
    as_destroy(oldas);

    enter_new_process(
        /* argc   */ argc,
        /* argv   */ uargv,
        /* envp   */ NULL,      /* no environment in OS/161 */
        /* stack  */ stackptr,
        /* entry  */ entry
    );

    /* Not reached on success. */
    panic("enter_new_process returned in sys_execv\n");
    return EINVAL;
}
