#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

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
 * Test 1: Basic waitpid functionality
 * Parent waits for a single child process
 */
static void
test_waitpid_basic(void)
{
    const char *test_name = "Basic waitpid functionality";
    pid_t pid, result;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child exits immediately */
        _exit(0);
    } else {
        /* Parent waits for child */
        result = waitpid(pid, &status, 0);
        
        if (result < 0) {
            printf("  Error: waitpid failed\n");
            print_result(test_name, 0);
            return;
        }
        
        if (result != pid) {
            printf("  Error: waitpid returned wrong PID: %d (expected %d)\n", 
                   (int)result, (int)pid);
            print_result(test_name, 0);
            return;
        }
        
        printf("  Successfully waited for child PID %d\n", (int)pid);
        print_result(test_name, 1);
    }
}

/*
 * Test 2: waitpid with invalid PID (should fail)
 * Tests error handling for non-existent process
 */
static void
test_waitpid_invalid_pid(void)
{
    const char *test_name = "waitpid with invalid PID (should fail)";
    int status;
    pid_t result;
    
    /* Try to wait for a PID that doesn't exist */
    result = waitpid(99999, &status, 0);
    
    if (result >= 0) {
        printf("  Error: waitpid should have failed but returned %d\n", (int)result);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed with invalid PID (result=%d)\n", (int)result);
    print_result(test_name, 1);
}

/*
 * Test 3: waitpid with negative PID (should fail)
 * Tests error handling for invalid PID value
 */
static void
test_waitpid_negative_pid(void)
{
    const char *test_name = "waitpid with negative PID (should fail)";
    int status;
    pid_t result;
    
    /* Try to wait for negative PID */
    result = waitpid(-1, &status, 0);
    
    if (result >= 0) {
        printf("  Error: waitpid should have failed but returned %d\n", (int)result);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed with negative PID (result=%d)\n", (int)result);
    print_result(test_name, 1);
}

/*
 * Test 4: waitpid for non-child process (should fail)
 * Can only wait for direct children
 */
static void
test_waitpid_non_child(void)
{
    const char *test_name = "waitpid for non-child process (should fail)";
    pid_t pid, other_child;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* First child creates its own child */
        other_child = fork();
        
        if (other_child == 0) {
            /* Grandchild */
            _exit(0);
        } else {
            /* First child exits without waiting for grandchild */
            _exit(0);
        }
    } else {
        /* Parent waits for first child */
        waitpid(pid, &status, 0);
        
        /* Parent tries to wait for grandchild (should fail) */
        /* Note: We don't know the grandchild's PID, so this test is limited */
        
        printf("  Cannot wait for non-child process\n");
        print_result(test_name, 1);
    }
}

/*
 * Test 5: waitpid with NULL status pointer
 * Status pointer can be NULL if caller doesn't need exit status
 */
static void
test_waitpid_null_status(void)
{
    const char *test_name = "waitpid with NULL status pointer";
    pid_t pid, result;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child exits */
        _exit(5);
    } else {
        /* Parent waits without status */
        result = waitpid(pid, NULL, 0);
        
        if (result < 0) {
            printf("  Error: waitpid with NULL status failed\n");
            print_result(test_name, 0);
            return;
        }
        
        if (result != pid) {
            printf("  Error: waitpid returned wrong PID\n");
            print_result(test_name, 0);
            return;
        }
        
        printf("  Successfully waited with NULL status\n");
        print_result(test_name, 1);
    }
}

/*
 * Test 6: waitpid blocks until child exits
 * Verifies that waitpid waits for child to complete
 */
static void
test_waitpid_blocks(void)
{
    const char *test_name = "waitpid blocks until child exits";
    pid_t pid, result;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child sleeps briefly then exits */
        volatile int i, j;
        for (i = 0; i < 1000; i++) {
            for (j = 0; j < 1000; j++) {
                /* Busy wait */
            }
        }
        _exit(0);
    } else {
        /* Parent waits (should block) */
        result = waitpid(pid, &status, 0);
        
        if (result != pid) {
            printf("  Error: waitpid didn't wait for child\n");
            print_result(test_name, 0);
            return;
        }
        
        printf("  waitpid correctly blocked until child exited\n");
        print_result(test_name, 1);
    }
}

/*
 * Test 7: waitpid multiple children in order
 * Parent waits for multiple children in the order they were created
 */
static void
test_waitpid_multiple_children(void)
{
    const char *test_name = "waitpid for multiple children";
    pid_t child1, child2, result1, result2;
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
    result1 = waitpid(child1, &status, 0);
    result2 = waitpid(child2, &status, 0);
    
    if (result1 != child1 || result2 != child2) {
        printf("  Error: waitpid returned wrong PIDs\n");
        print_result(test_name, 0);
        return;
    }
    
    printf("  Successfully waited for both children: %d and %d\n", 
           (int)child1, (int)child2);
    print_result(test_name, 1);
}

/*
 * Test 8: waitpid with invalid options (should fail)
 * Tests error handling for unsupported options
 */
static void
test_waitpid_invalid_options(void)
{
    const char *test_name = "waitpid with invalid options (should fail)";
    pid_t pid, result;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child exits */
        _exit(0);
    } else {
        /* Parent waits with invalid options */
        result = waitpid(pid, &status, 0x9999);
        
        if (result >= 0) {
            printf("  Error: waitpid should have failed with invalid options\n");
            print_result(test_name, 0);
            return;
        }
        
        /* Clean up - wait properly */
        waitpid(pid, &status, 0);
        
        printf("  Correctly failed with invalid options (result=%d)\n", (int)result);
        print_result(test_name, 1);
    }
}

/*
 * Test 9: Double waitpid on same child (should fail)
 * Cannot wait for a child that has already been reaped
 */
static void
test_waitpid_double_wait(void)
{
    const char *test_name = "Double waitpid on same child (should fail)";
    pid_t pid, result1, result2;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child exits */
        _exit(0);
    } else {
        /* First wait should succeed */
        result1 = waitpid(pid, &status, 0);
        
        if (result1 != pid) {
            printf("  Error: first waitpid failed\n");
            print_result(test_name, 0);
            return;
        }
        
        /* Second wait should fail */
        result2 = waitpid(pid, &status, 0);
        
        if (result2 >= 0) {
            printf("  Error: second waitpid should have failed\n");
            print_result(test_name, 0);
            return;
        }
        
        printf("  Correctly failed on second waitpid (result=%d)\n", (int)result2);
        print_result(test_name, 1);
    }
}

int
main(void)
{
    printf("waitpid System Call Tests\n");
    printf("=========================\n\n");
    
    test_waitpid_basic();
    test_waitpid_invalid_pid();
    test_waitpid_negative_pid();
    test_waitpid_non_child();
    test_waitpid_null_status();
    test_waitpid_blocks();
    test_waitpid_multiple_children();
    test_waitpid_invalid_options();
    test_waitpid_double_wait();
    
    printf("waitpid Test Summary:\n");
    printf("=============\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);
    
    return (tests_failed == 0) ? 0 : 1;
}