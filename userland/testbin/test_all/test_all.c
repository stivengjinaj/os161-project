#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

/* List of all tests to run - UPDATE THIS when adding new tests */
static const char *tests[] = {
    "test_read",
    "test_write",
    "test_open",
    "test_close",
    "test_fork",
    "test_execv",
    "test_waitpid",
    /* Add more tests here as you create them */
    NULL  /* Sentinel */
};

static int total_passed = 0;
static int total_failed = 0;

static void
run_test(const char *test_name)
{
    pid_t pid;
    int status;
    char path[256];
    
    printf("\n========================================\n");
    printf("Running %s...\n", test_name);
    printf("========================================\n");
    
    /* Build the path to the test binary */
    snprintf(path, sizeof(path), "/testbin/%s/%s", test_name, test_name);
    
    pid = fork();
    if (pid < 0) {
        printf("ERROR: Failed to fork for %s\n", test_name);
        total_failed++;
        return;
    }
    
    if (pid == 0) {
        /* Child process - execute the test */
        char *args[2];
        args[0] = (char *)test_name;
        args[1] = NULL;
        
        execv(path, args);
        
        /* If execv returns, it failed */
        printf("ERROR: Failed to execute %s\n", test_name);
        _exit(1);
    }
    
    /* Parent process - wait for test to complete */
    if (waitpid(pid, &status, 0) < 0) {
        printf("ERROR: Failed to wait for %s\n", test_name);
        total_failed++;
        return;
    }
    
    /* Check exit status */
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf("[SUCCESS] %s passed\n", test_name);
        total_passed++;
    } else {
        printf("[FAILURE] %s failed (exit code: %d)\n", 
               test_name, WEXITSTATUS(status));
        total_failed++;
    }
}

int
main(void)
{
    int i;
    
    printf("========================================\n");
    printf("OS/161 System Call Test Suite\n");
    printf("========================================\n");
    
    /* Run all tests */
    for (i = 0; tests[i] != NULL; i++) {
        run_test(tests[i]);
    }
    
    /* Print final summary */
    printf("\n========================================\n");
    printf("Overall Test Summary\n");
    printf("========================================\n");
    printf("Test suites passed: %d\n", total_passed);
    printf("Test suites failed: %d\n", total_failed);
    printf("Total test suites:  %d\n", total_passed + total_failed);
    printf("========================================\n");
    
    if (total_failed == 0) {
        printf("ALL TESTS PASSED!\n");
        return 0;
    } else {
        printf("SOME TESTS FAILED!\n");
        return 1;
    }
}