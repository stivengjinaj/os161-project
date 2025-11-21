#include <types.h>
#include <kern/errno.h>
#include <kern/limits.h>
#include <lib.h>
#include <copyinout.h>
#include <addrspace.h>
#include <proc.h>
#include <current.h>
#include <syscall.h>
#include "opt-shell.h"

#if OPT_SHELL

/*
 * copy_program_name - Copy program name from user space to kernel space
 *
 * Arguments:
 *   program         - User space pointer to program name string
 *   kernel_progname - Pointer to store allocated kernel copy
 *
 * Returns:
 *   0 on success
 *   ENOMEM - Out of memory
 *   EFAULT - Invalid user pointer
 */
int copy_program_name(const char *program, char **kernel_progname) {
    int result;
    size_t actual;
    
    *kernel_progname = kmalloc(PATH_MAX);
    if (*kernel_progname == NULL) {
        return ENOMEM;
    }
    
    result = copyinstr((const_userptr_t)program, *kernel_progname, PATH_MAX, &actual);
    if (result) {
        kfree(*kernel_progname);
        *kernel_progname = NULL;
        return result;
    }
    
    return 0;
}

/*
 * copy_arguments - Copy argument array from user space to kernel space
 *
 * Arguments:
 *   args        - User space pointer to argument array
 *   kernel_args - Pointer to store allocated kernel copy
 *   argc        - Pointer to store argument count
 *
 * Returns:
 *   0 on success
 *   E2BIG  - Too many arguments
 *   ENOMEM - Out of memory
 *   EFAULT - Invalid user pointer
 */
int copy_arguments(char **args, char ***kernel_args, int *argc) {
    int i, result;
    size_t actual;
    char *user_argptr;
    
    /* Count arguments by reading pointers from user space */
    *argc = 0;
    while (1) {
        /* Read pointer from user space array */
        result = copyin((const_userptr_t)(args + *argc), &user_argptr, sizeof(char *));
        if (result) {
            return result;
        }
        
        /* NULL pointer marks end of array */
        if (user_argptr == NULL) {
            break;
        }
        
        (*argc)++;
        if (*argc > ARG_MAX) {
            return E2BIG;
        }
    }
    
    /* Allocate kernel array for argument pointers */
    *kernel_args = kmalloc((*argc + 1) * sizeof(char *));
    if (*kernel_args == NULL) {
        return ENOMEM;
    }
    
    /* Initialize all pointers to NULL for cleanup safety */
    for (i = 0; i <= *argc; i++) {
        (*kernel_args)[i] = NULL;
    }
    
    /* Copy each argument string */
    for (i = 0; i < *argc; i++) {
        /* Read the pointer to the i-th argument */
        result = copyin((const_userptr_t)(args + i), &user_argptr, sizeof(char *));
        if (result) {
            cleanup_arguments(*kernel_args, *argc);
            *kernel_args = NULL;
            return result;
        }
        
        /* Allocate space for the argument string */
        (*kernel_args)[i] = kmalloc(PATH_MAX);
        if ((*kernel_args)[i] == NULL) {
            cleanup_arguments(*kernel_args, *argc);
            *kernel_args = NULL;
            return ENOMEM;
        }
        
        /* Copy the string from user space */
        result = copyinstr((const_userptr_t)user_argptr, (*kernel_args)[i], PATH_MAX, &actual);
        if (result) {
            cleanup_arguments(*kernel_args, *argc);
            *kernel_args = NULL;
            return result;
        }
    }
    
    return 0;
}

/*
 * cleanup_arguments - Free kernel argument array
 *
 * Arguments:
 *   kernel_args - Kernel array of argument strings to free
 *   argc        - Number of arguments in the array
 */
void cleanup_arguments(char **kernel_args, int argc) {
    if (kernel_args != NULL) {
        for (int i = 0; i < argc; i++) {
            if (kernel_args[i] != NULL) {
                kfree(kernel_args[i]);
            }
        }
        kfree(kernel_args);
    }
}

/*
 * restore_old_address_space - Restore old address space after execv failure
 *
 * Arguments:
 *   old_as - Old address space to restore
 *   new_as - New address space to destroy
 */
void restore_old_address_space(struct addrspace *old_as, struct addrspace *new_as) {
    proc_setas(old_as);
    as_activate();
    as_destroy(new_as);
}

/*
 * copy_args_to_stack - Copy arguments to user stack for execv
 *
 * Copies argument strings and pointers to the user stack in the format
 * expected by main(int argc, char **argv).
 *
 * Stack layout (growing down):
 *   - Argument strings (null-terminated)
 *   - Padding for alignment
 *   - Array of pointers to strings
 *   - NULL pointer (argv[argc])
 *
 * Arguments:
 *   kernel_args - Kernel array of argument strings
 *   argc        - Number of arguments
 *   stackptr    - Pointer to stack pointer, updated to new top of stack
 *
 * Returns:
 *   0 on success
 *   EFAULT - Copy to user space failed
 *   ENOMEM - Out of memory
 */
int copy_args_to_stack(char **kernel_args, int argc, vaddr_t *stackptr) {
    vaddr_t *argv_ptrs;
    int i, result;
    size_t len;
    
    /* Allocate temporary space for argument pointers */
    argv_ptrs = kmalloc((argc + 1) * sizeof(vaddr_t));
    if (argv_ptrs == NULL) {
        return ENOMEM;
    }
    
    /*
     * Copy argument strings to stack (in reverse order)
     * Each string is null-terminated and aligned to 4-byte boundary
     */
    for (i = argc - 1; i >= 0; i--) {
        len = strlen(kernel_args[i]) + 1;
        
        /* Move stack pointer down for string */
        *stackptr -= len;
        
        /* Align to 4-byte boundary */
        *stackptr -= (*stackptr % 4);
        
        /* Copy string to user stack */
        result = copyoutstr(kernel_args[i], (userptr_t)*stackptr, len, NULL);
        if (result) {
            kfree(argv_ptrs);
            return result;
        }
        
        /* Save pointer for argv array */
        argv_ptrs[i] = *stackptr;
    }
    
    /* NULL terminator for argv array */
    argv_ptrs[argc] = 0;
    
    /*
     * Copy argv pointer array to stack
     * Align to 8-byte boundary for MIPS ABI compatibility
     */
    *stackptr -= (argc + 1) * sizeof(vaddr_t);
    *stackptr -= (*stackptr % 8);
    
    result = copyout(argv_ptrs, (userptr_t)*stackptr, (argc + 1) * sizeof(vaddr_t));
    kfree(argv_ptrs);
    
    if (result) {
        return result;
    }
    
    return 0;
}

#endif
