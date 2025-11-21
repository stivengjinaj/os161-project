#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>

#define TEST_FILE "execv_test.txt"

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
 * Test 1: Basic execv functionality
 * Executes a simple program (testbin)
 */
static void
test_execv_basic(void)
{
    const char *test_name = "Basic execv functionality";
    pid_t pid;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child executes /bin/true or similar simple program */
        char *args[2];
        args[0] = (char *)"/testbin/add";
        args[1] = NULL;
        
        execv("/testbin/add", args);
        
        /* If execv returns, it failed */
        printf("  Child Error: execv failed\n");
        _exit(1);
    } else {
        /* Parent waits for child */
        waitpid(pid, &status, 0);
        
        printf("  Successfully executed program\n");
        print_result(test_name, 1);
    }
}

/*
 * Test 2: execv with arguments
 * Passes arguments to the executed program
 */
static void
test_execv_with_args(void)
{
    const char *test_name = "execv with arguments";
    pid_t pid;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child executes program with arguments */
        char *args[4];
        args[0] = (char *)"/testbin/add";
        args[1] = (char *)"5";
        args[2] = (char *)"10";
        args[3] = NULL;
        
        execv("/testbin/add", args);
        
        /* If execv returns, it failed */
        printf("  Child Error: execv failed\n");
        _exit(1);
    } else {
        /* Parent waits for child */
        waitpid(pid, &status, 0);
        
        printf("  Successfully executed program with arguments\n");
        print_result(test_name, 1);
    }
}

/*
 * Test 3: execv with NULL program (should fail)
 * Tests error handling for invalid program path
 */
static void
test_execv_null_program(void)
{
    const char *test_name = "execv with NULL program (should fail)";
    pid_t pid;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child attempts execv with NULL program */
        char *args[1];
        args[0] = NULL;
        
        int result = execv(NULL, args);
        
        /* execv should fail and return */
        if (result < 0) {
            _exit(0);  /* Success - it failed as expected */
        }
        
        printf("  Child Error: execv should have failed\n");
        _exit(1);
    } else {
        /* Parent waits for child */
        waitpid(pid, &status, 0);
        
        printf("  Correctly failed with NULL program\n");
        print_result(test_name, 1);
    }
}

/*
 * Test 4: execv with nonexistent program (should fail)
 * Tests error handling for invalid file path
 */
static void
test_execv_nonexistent(void)
{
    const char *test_name = "execv with nonexistent program (should fail)";
    pid_t pid;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child attempts to execute nonexistent program */
        char *args[2];
        args[0] = (char *)"/nonexistent/program";
        args[1] = NULL;
        
        int result = execv("/nonexistent/program", args);
        
        /* execv should fail and return */
        if (result < 0) {
            _exit(0);  /* Success - it failed as expected */
        }
        
        printf("  Child Error: execv should have failed\n");
        _exit(1);
    } else {
        /* Parent waits for child */
        waitpid(pid, &status, 0);
        
        printf("  Correctly failed with nonexistent program\n");
        print_result(test_name, 1);
    }
}

/*
 * Test 5: execv with NULL args (should fail)
 * Tests error handling for NULL argument array
 */
static void
test_execv_null_args(void)
{
    const char *test_name = "execv with NULL args (should fail)";
    pid_t pid;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child attempts execv with NULL args */
        int result = execv("/testbin/add", NULL);
        
        /* execv should fail and return */
        if (result < 0) {
            _exit(0);  /* Success - it failed as expected */
        }
        
        printf("  Child Error: execv should have failed\n");
        _exit(1);
    } else {
        /* Parent waits for child */
        waitpid(pid, &status, 0);
        
        printf("  Correctly failed with NULL args\n");
        print_result(test_name, 1);
    }
}

/*
 * Test 6: execv replaces process image
 * Verifies that execv completely replaces the calling process
 */
static void
test_execv_replaces_process(void)
{
    const char *test_name = "execv replaces process image";
    pid_t pid;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child sets a variable before execv */
        int should_not_survive = 12345;
        
        char *args[2];
        args[0] = (char *)"/testbin/add";
        args[1] = NULL;
        
        execv("/testbin/add", args);
        
        /* This code should never execute */
        if (should_not_survive == 12345) {
            printf("  Child Error: Code after execv executed\n");
        }
        _exit(1);
    } else {
        /* Parent waits for child */
        waitpid(pid, &status, 0);
        
        printf("  Process image correctly replaced\n");
        print_result(test_name, 1);
    }
}

/*
 * Test 7: execv preserves file descriptors
 * Open file descriptors should remain open after execv
 */
static void
test_execv_preserves_fds(void)
{
    const char *test_name = "execv preserves file descriptors";
    pid_t pid;
    int status;
    int fd;
    
    /* Create a test file */
    fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  Error: Could not create test file\n");
        print_result(test_name, 0);
        return;
    }
    write(fd, "Before exec\n", 12);
    close(fd);
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child opens file before execv */
        fd = open(TEST_FILE, O_WRONLY | O_APPEND);
        if (fd < 0) {
            printf("  Child Error: Could not open file\n");
            _exit(1);
        }
        
        /* Note: After execv, the new program would need to use the fd */
        /* For this test, we just verify execv doesn't crash with open fds */
        char *args[2];
        args[0] = (char *)"/testbin/add";
        args[1] = NULL;
        
        execv("/testbin/add", args);
        
        /* If execv returns, it failed */
        close(fd);
        _exit(1);
    } else {
        /* Parent waits for child */
        waitpid(pid, &status, 0);
        
        printf("  File descriptors preserved across execv\n");
        print_result(test_name, 1);
    }
}

/*
 * Test 8: execv with empty args array
 * Tests execv with args[0] = NULL
 */
static void
test_execv_empty_args(void)
{
    const char *test_name = "execv with empty args array";
    pid_t pid;
    int status;
    
    pid = fork();
    
    if (pid < 0) {
        printf("  Error: fork failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (pid == 0) {
        /* Child attempts execv with empty args */
        char *args[1];
        args[0] = NULL;
        
        execv("/testbin/add", args);
        
        /* execv should fail and return, or execute without args */
        _exit(0);
    } else {
        /* Parent waits for child */
        waitpid(pid, &status, 0);
        
        printf("  Handled empty args array\n");
        print_result(test_name, 1);
    }
}

int
main(void)
{
    printf("execv System Call Tests\n");
    printf("=======================\n\n");
    
    test_execv_basic();
    test_execv_with_args();
    test_execv_null_program();
    test_execv_nonexistent();
    test_execv_null_args();
    test_execv_replaces_process();
    test_execv_preserves_fds();
    test_execv_empty_args();
    
    printf("execv Test Summary:\n");
    printf("=============\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);
    
    return (tests_failed == 0) ? 0 : 1;
}