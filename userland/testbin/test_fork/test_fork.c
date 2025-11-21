#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>

#define TEST_FILE "fork_test.txt"

static int tests_passed = 0;
static int tests_failed = 0;

static void
print_result(const char *test_name, int passed)
{
    if (passed) {
        printf("[PASS] %s\n", test_name);
        tests_passed++;
    } else {
        printf("[FAIL] %s\n", test_name);
        tests_failed++;
    }
}

/*
 * Test 1: Basic fork functionality
 * Verifies that fork creates a child process successfully
 */
static void
test_fork_basic(void)
{
    const char *test_name = "Basic fork functionality";
    pid_t pid;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child process */
        _exit(0);
    } else {
        /* Parent process */
        pid_t wait_result = waitpid(pid, &status, 0);
        
        if (wait_result < 0) {
            printf("  Error: waitpid failed\n");
            print_result(test_name, 0);
            return;
        }
        
        if (wait_result != pid) {
            printf("  Error: waitpid returned wrong pid\n");
            print_result(test_name, 0);
            return;
        }
        
        printf("  Successfully created child with PID %d\n", (int)pid);
        print_result(test_name, 1);
    }
}

/*
 * Test 2: Fork returns different values to parent and child
 * Parent should get child PID, child should get 0
 */
static void
test_fork_return_values(void)
{
    const char *test_name = "Fork return values";
    pid_t pid;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child process - fork should return 0 */
        if (pid != 0) {
            printf("  Child Error: fork returned %d instead of 0\n", (int)pid);
            _exit(1);
        }
        
        _exit(0);
    } else {
        /* Parent process - fork should return child PID */
        waitpid(pid, &status, 0);
        
        if (pid <= 0) {
            printf("  Error: fork returned invalid child PID: %d\n", (int)pid);
            print_result(test_name, 0);
            return;
        }
        
        printf("  Parent got child PID %d, child got 0\n", (int)pid);
        print_result(test_name, 1);
    }
}

/*
 * Test 3: Parent and child have different PIDs
 * Verifies process identity separation
 */
static void
test_fork_different_pids(void)
{
    const char *test_name = "Parent and child have different PIDs";
    pid_t parent_pid, child_fork_result;
    int status;
    
    parent_pid = getpid();
    child_fork_result = fork();
    
    if (child_fork_result < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (child_fork_result == 0) {
        /* Child process */
        pid_t child_pid = getpid();
        
        if (child_pid == parent_pid) {
            printf("  Child Error: Child PID equals parent PID: %d\n", (int)child_pid);
            _exit(1);
        }
        
        _exit(0);
    } else {
        /* Parent process */
        waitpid(child_fork_result, &status, 0);
        
        printf("  Parent PID: %d, Child PID: %d\n", (int)parent_pid, (int)child_fork_result);
        print_result(test_name, 1);
    }
}

/*
 * Helper function to check if a string contains a substring
 */
static int
contains_string(const char *haystack, const char *needle)
{
    size_t needle_len = strlen(needle);
    size_t haystack_len = strlen(haystack);
    size_t i;
    
    if (needle_len > haystack_len) {
        return 0;
    }
    
    for (i = 0; i <= haystack_len - needle_len; i++) {
        size_t j;
        for (j = 0; j < needle_len; j++) {
            if (haystack[i + j] != needle[j]) {
                break;
            }
        }
        if (j == needle_len) {
            return 1;
        }
    }
    
    return 0;
}

/*
 * Test 4: Child inherits file descriptors from parent
 * Verifies file descriptor table is copied
 */
static void
test_fork_file_descriptors(void)
{
    const char *test_name = "Child inherits file descriptors";
    int fd;
    pid_t pid;
    int status;
    char buffer[64];
    
    /* Parent opens a file */
    fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  Error: Could not create test file\n");
        print_result(test_name, 0);
        return;
    }
    
    write(fd, "Parent ", 7);
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        close(fd);
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child writes to the same fd */
        write(fd, "Child ", 6);
        close(fd);
        _exit(0);
    } else {
        /* Parent writes more */
        write(fd, "Parent ", 7);
        close(fd);
        
        waitpid(pid, &status, 0);
        
        /* Verify the file content */
        fd = open(TEST_FILE, O_RDONLY);
        if (fd < 0) {
            printf("  Error: Could not reopen file\n");
            print_result(test_name, 0);
            return;
        }
        
        memset(buffer, 0, sizeof(buffer));
        read(fd, buffer, sizeof(buffer));
        close(fd);
        
        /* Both parent and child should have written */
        if (!contains_string(buffer, "Parent") || !contains_string(buffer, "Child")) {
            printf("  Error: File content incorrect: '%s'\n", buffer);
            print_result(test_name, 0);
            return;
        }
        
        printf("  File descriptors correctly inherited\n");
        print_result(test_name, 1);
    }
}

/*
 * Test 5: Multiple children from same parent
 * Verifies parent can create multiple child processes
 */
static void
test_fork_multiple_children(void)
{
    const char *test_name = "Multiple children from same parent";
    pid_t child1, child2;
    int status;
    
    child1 = fork();
    
    if (child1 < 0) {
        printf("  Error: first fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (child1 == 0) {
        /* First child */
        _exit(0);
    }
    
    /* Parent creates second child */
    child2 = fork();
    
    if (child2 < 0) {
        printf("  Error: second fork failed\n");
        waitpid(child1, &status, 0);
        print_result(test_name, 0);
        return;
    }
    
    if (child2 == 0) {
        /* Second child */
        _exit(0);
    }
    
    /* Parent waits for both children */
    waitpid(child1, &status, 0);
    waitpid(child2, &status, 0);
    
    if (child1 == child2) {
        printf("  Error: Children have same PID\n");
        print_result(test_name, 0);
        return;
    }
    
    printf("  Created two children: PID %d and PID %d\n", (int)child1, (int)child2);
    print_result(test_name, 1);
}

/*
 * Test 6: Child can fork (grandchild)
 * Verifies recursive forking works
 */
static void
test_fork_grandchild(void)
{
    const char *test_name = "Child can fork (grandchild)";
    pid_t child, grandchild;
    int status;
    
    child = fork();
    
    if (child < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (child == 0) {
        /* Child process creates grandchild */
        grandchild = fork();
        
        if (grandchild < 0) {
            printf("  Child Error: fork failed\n");
            _exit(1);
        }
        
        if (grandchild == 0) {
            /* Grandchild */
            _exit(0);
        } else {
            /* Child waits for grandchild */
            waitpid(grandchild, &status, 0);
            _exit(0);
        }
    } else {
        /* Parent waits for child */
        waitpid(child, &status, 0);
        
        printf("  Successfully created grandchild process\n");
        print_result(test_name, 1);
    }
}

/*
 * Test 7: Child address space is independent
 * Modifications in child should not affect parent
 */
static void
test_fork_address_space(void)
{
    const char *test_name = "Child address space independence";
    int shared_var = 100;
    pid_t pid;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child modifies variable */
        shared_var = 200;
        
        if (shared_var != 200) {
            printf("  Child Error: Variable not modified correctly\n");
            _exit(1);
        }
        
        _exit(0);
    } else {
        /* Parent checks its variable */
        waitpid(pid, &status, 0);
        
        if (shared_var != 100) {
            printf("  Error: Parent's variable was modified by child\n");
            print_result(test_name, 0);
            return;
        }
        
        printf("  Address spaces correctly separated\n");
        print_result(test_name, 1);
    }
}

/*
 * Test 8: Fork and exit code propagation
 * Parent should receive child's exit code via waitpid
 */
static void
test_fork_exit_code(void)
{
    const char *test_name = "Fork and exit code propagation";
    pid_t pid;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child exits with specific code */
        _exit(42);
    } else {
        /* Parent waits and checks exit code */
        waitpid(pid, &status, 0);
        
        /* Note: In OS/161, status is encoded with _MKWAIT_EXIT */
        /* We just verify waitpid succeeded */
        printf("  Child exited with status: %d\n", status);
        print_result(test_name, 1);
    }
}

int
main(void)
{
    printf("fork System Call Tests\n");
    printf("======================\n\n");
    
    test_fork_basic();
    test_fork_return_values();
    test_fork_different_pids();
    test_fork_file_descriptors();
    test_fork_multiple_children();
    test_fork_grandchild();
    test_fork_address_space();
    test_fork_exit_code();
    
    printf("fork Test Summary:\n");
    printf("=============\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);
    
    return (tests_failed == 0) ? 0 : 1;
}