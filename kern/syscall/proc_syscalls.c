/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <current.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <proc.h>
#include <kern/errno.h>
#include <machine/trapframe.h>
#include <synch.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <kern/wait.h>
#include <kern/limits.h>

#if OPT_SHELL
pid_t sys_getpid() {
  /* process ID of the current process */
  pid_t result = curproc->p_pid;
  return result;
}

int sys_fork(struct trapframe *tf, pid_t *retval) {
    struct proc *newproc;
    struct trapframe *childtf;
    int result;

    KASSERT(curproc != NULL);

    /* Create a new process structure for the child */
    newproc = proc_create_runprogram(curproc->p_name);
    if (newproc == NULL) {
        return ENOMEM;
    }

    /* Set parent PID */
    newproc->parent_pid = curproc->p_pid;

    /* Copy the address space */
    result = as_copy(curproc->p_addrspace, &(newproc->p_addrspace));
    if (result) {
        proc_destroy(newproc);
        return ENOMEM; 
    }

    /* Copy the trapframe to the heap for the child thread */
    childtf = kmalloc(sizeof(struct trapframe));
    if (childtf == NULL) {
        as_destroy(newproc->p_addrspace);
        newproc->p_addrspace = NULL;
        proc_destroy(newproc);
        return ENOMEM;
    }
    memcpy(childtf, tf, sizeof(struct trapframe));

    if (curproc->fileTable != NULL) {
        for (int i = 3; i < OPEN_MAX; i++) {
            if (curproc->fileTable[i] != NULL) {
                newproc->fileTable[i] = curproc->fileTable[i];
                lock_acquire(curproc->fileTable[i]->lock);
                curproc->fileTable[i]->count++;
                lock_release(curproc->fileTable[i]->lock);
            }
        }
    }

    /* Create a new thread for the child process */
    result = thread_fork(
        curthread->t_name,
        newproc,
        enter_forked_process,
        (void *)childtf,
        0 
    );

    if (result) {
        for (int i = 3; i < OPEN_MAX; i++) {
            if (newproc->fileTable[i] != NULL) {
                lock_acquire(newproc->fileTable[i]->lock);
                newproc->fileTable[i]->count--;
                lock_release(newproc->fileTable[i]->lock);
                newproc->fileTable[i] = NULL;
            }
        }
        
        kfree(childtf);
        as_destroy(newproc->p_addrspace);
        newproc->p_addrspace = NULL;
        proc_destroy(newproc);
        return ENOMEM;
    }

    *retval = newproc->p_pid;

    return 0;
}

int sys_execv(const char *program, char **args) {
    struct addrspace *as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result;
    
    /* Validate program pointer */
    if (program == NULL) {
        return EFAULT;
    }
    
    /* Copy program name from user space */
    char *kprogram = kmalloc(PATH_MAX);
    if (kprogram == NULL) {
        return ENOMEM;
    }
    
    size_t proglen;
    result = copyinstr((const_userptr_t)program, kprogram, PATH_MAX, &proglen);
    if (result) {
        kfree(kprogram);
        return result;
    }
    
    /* Count and validate arguments */
    if (args == NULL) {
        kfree(kprogram);
        return EFAULT;
    }
    
    int argc = 0;
    while (args[argc] != NULL) {
        argc++;
        if (argc > ARG_MAX) {
            kfree(kprogram);
            return E2BIG;
        }
    }
    
    /* Copy arguments from user space to kernel space */
    char **kargs = kmalloc((argc + 1) * sizeof(char *));
    if (kargs == NULL) {
        kfree(kprogram);
        return ENOMEM;
    }
    
    size_t total_arg_len = 0;
    for (int i = 0; i < argc; i++) {
        if (args[i] == NULL) {
            /* Clean up previously allocated args */
            for (int j = 0; j < i; j++) {
                kfree(kargs[j]);
            }
            kfree(kargs);
            kfree(kprogram);
            return EFAULT;
        }
        
        kargs[i] = kmalloc(ARG_MAX);
        if (kargs[i] == NULL) {
            for (int j = 0; j < i; j++) {
                kfree(kargs[j]);
            }
            kfree(kargs);
            kfree(kprogram);
            return ENOMEM;
        }
        
        size_t arglen;
        result = copyinstr((const_userptr_t)args[i], kargs[i], ARG_MAX, &arglen);
        if (result) {
            for (int j = 0; j <= i; j++) {
                kfree(kargs[j]);
            }
            kfree(kargs);
            kfree(kprogram);
            return result;
        }
        
        total_arg_len += arglen;
        if (total_arg_len > ARG_MAX) {
            for (int j = 0; j <= i; j++) {
                kfree(kargs[j]);
            }
            kfree(kargs);
            kfree(kprogram);
            return E2BIG;
        }
    }
    kargs[argc] = NULL;
    
    /* Open the executable */
    result = vfs_open(kprogram, O_RDONLY, 0, &v);
    if (result) {
        for (int i = 0; i < argc; i++) {
            kfree(kargs[i]);
        }
        kfree(kargs);
        kfree(kprogram);
        return result;
    }
    
    /* We no longer need the program name in kernel space */
    kfree(kprogram);
    
    /* Create a new address space */
    as = as_create();
    if (as == NULL) {
        vfs_close(v);
        for (int i = 0; i < argc; i++) {
            kfree(kargs[i]);
        }
        kfree(kargs);
        return ENOMEM;
    }
    
    /* Switch to the new address space and activate it */
    struct addrspace *old_as = proc_setas(as);
    as_activate();
    
    /* Load the executable */
    result = load_elf(v, &entrypoint);
    if (result) {
        /* Restore old address space */
        proc_setas(old_as);
        as_activate();
        as_destroy(as);
        vfs_close(v);
        for (int i = 0; i < argc; i++) {
            kfree(kargs[i]);
        }
        kfree(kargs);
        return result;
    }
    
    /* Done with the file now */
    vfs_close(v);
    
    /* Defining the user stack in the address space */
    result = as_define_stack(as, &stackptr);
    if (result) {
        /* Restore old address space */
        proc_setas(old_as);
        as_activate();
        as_destroy(as);
        for (int i = 0; i < argc; i++) {
            kfree(kargs[i]);
        }
        kfree(kargs);
        return result;
    }
    
    /* Copy arguments to user stack */
    vaddr_t argv_ptrs[argc + 1];
    
    /* Copy argument strings to stack (in reverse order) */
    for (int i = argc - 1; i >= 0; i--) {
        size_t arglen = strlen(kargs[i]) + 1;
        
        /* Align stack pointer */
        stackptr -= arglen;
        
        result = copyout(kargs[i], (userptr_t)stackptr, arglen);
        if (result) {
            /* Restore old address space */
            proc_setas(old_as);
            as_activate();
            as_destroy(as);
            for (int j = 0; j < argc; j++) {
                kfree(kargs[j]);
            }
            kfree(kargs);
            return result;
        }
        
        argv_ptrs[i] = stackptr;
        kfree(kargs[i]);
    }
    argv_ptrs[argc] = 0;
    kfree(kargs);
    
    /* Align stack pointer to 8-byte boundary */
    stackptr -= (stackptr % 8);
    
    /* Copy argv array to stack */
    stackptr -= (argc + 1) * sizeof(vaddr_t);
    result = copyout(argv_ptrs, (userptr_t)stackptr, (argc + 1) * sizeof(vaddr_t));
    if (result) {
        /* Restore old address space */
        proc_setas(old_as);
        as_activate();
        as_destroy(as);
        return result;
    }
    
    /* Destroy the old address space */
    as_destroy(old_as);
    
    /* Warp to user mode */
    enter_new_process(argc, (userptr_t)stackptr, NULL, stackptr, entrypoint);
    
    /* enter_new_process does not return */
    panic("enter_new_process returned\n");
    return EINVAL;
}

int sys_waitpid(pid_t pid, int *status, int options, int *retval) {
    int result;
    
    /* Validate options - only WNOHANG and WUNTRACED are valid */
    if (options != 0 && options != WNOHANG && options != WUNTRACED && 
        options != (WNOHANG | WUNTRACED)) {
        return EINVAL;
    }
    
    /* WUNTRACED is not supported in OS161 */
    if (options & WUNTRACED) {
        return EINVAL;
    }
    
    /* Validate status pointer if provided */
    if (status != NULL) {
        /* Check if the status pointer is valid user space address */
        if ((vaddr_t)status >= USERSPACETOP || (vaddr_t)status < (vaddr_t)0x400000) {
            return EFAULT;
        }
    }
    
    /* Check if pid is valid */
    if (pid < 0 || pid > PROC_MAX) {
        return ESRCH;  /* No such process */
    }
    
    /* Get the process with the given PID */
    struct proc *child = proc_search(pid);
    if (child == NULL) {
        return ESRCH;  /* No such process */
    }
    
    /* Check if the process is a child of the current process */
    if (child->parent_pid != curproc->p_pid) {
        return ECHILD;  /* Not a child of calling process */
    }
    
    /* If WNOHANG is set and child hasn't exited yet, return immediately */
    if (options & WNOHANG) {
        lock_acquire(child->p_locklock);
        if (!child->p_exited) {
            lock_release(child->p_locklock);
            *retval = 0;  /* No child has exited */
            return 0;
        }
        lock_release(child->p_locklock);
    }
    
    /* Wait for the child to exit */
    lock_acquire(child->p_locklock);
    while (!child->p_exited) {
        cv_wait(child->p_cv, child->p_locklock);
    }
    
    /* Get the exit status */
    int exit_status = child->p_exitcode;
    lock_release(child->p_locklock);
    
    /* Copy exit status to user space if status pointer is provided */
    if (status != NULL) {
        result = copyout(&exit_status, (userptr_t)status, sizeof(int));
        if (result) {
            return EFAULT;
        }
    }
    
    /* Clean up the child process - remove from process table */
    proc_remove(pid);
    proc_destroy(child);
    
    /* Return the PID of the child that exited */
    *retval = pid;
    
    return 0;
}

void sys__exit(int exitcode) {
    struct proc *p = curproc;
    
    KASSERT(p != NULL);
    
    /* Store exit code */
    lock_acquire(p->p_locklock);
    p->p_exitcode = _MKWAIT_EXIT(exitcode);
    p->p_exited = true;
    
    /* Wake up parent if it's waiting */
    cv_broadcast(p->p_cv, p->p_locklock);
    lock_release(p->p_locklock);
    
    /* Close all open file descriptors */
    if (p->fileTable != NULL) {
        for (int i = 0; i < OPEN_MAX; i++) {
            if (p->fileTable[i] != NULL) {
                sys_close(i);
            }
        }
    }
    
    /* Detach from address space */
    struct addrspace *as = proc_getas();
    if (as != NULL) {
        proc_setas(NULL);
        as_activate();
        as_destroy(as);
    }
    
    /* Thread exits */
    thread_exit();
    
    panic("thread_exit returned (should not happen)\n");
}

#endif