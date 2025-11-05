#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>

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
 * Test 1: Basic getpid functionality
 * Verifies that getpid returns a valid process ID (positive integer)
 */
static void
test_getpid_basic(void)
{
    const char *test_name = "Basic getpid functionality";
    pid_t pid;
    
    pid = getpid();
    
    if (pid < 0) {
        printf("  Error: getpid returned invalid pid %d\n", (int)pid);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Current process PID: %d\n", (int)pid);
    print_result(test_name, 1);
}

/*
 * Test 2: getpid consistency
 * Verifies that multiple calls to getpid return the same value
 */
static void
test_getpid_consistency(void)
{
    const char *test_name = "getpid consistency";
    pid_t pid1, pid2, pid3;
    
    pid1 = getpid();
    pid2 = getpid();
    pid3 = getpid();
    
    if (pid1 != pid2 || pid2 != pid3) {
        printf("  Error: Inconsistent PIDs: %d, %d, %d\n", 
               (int)pid1, (int)pid2, (int)pid3);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Consistent PID across calls: %d\n", (int)pid1);
    print_result(test_name, 1);
}

/*
 * Test 3: getpid returns positive value
 * PIDs should always be positive integers
 */
static void
test_getpid_positive(void)
{
    const char *test_name = "getpid returns positive value";
    pid_t pid;
    
    pid = getpid();
    
    if (pid <= 0) {
        printf("  Error: PID is not positive: %d\n", (int)pid);
        print_result(test_name, 0);
        return;
    }
    
    printf("  PID is positive: %d\n", (int)pid);
    print_result(test_name, 1);
}

int
main(void)
{
    printf("getpid System Call Tests\n");
    
    test_getpid_basic();
    test_getpid_consistency();
    test_getpid_positive();
    //test_getpid_uniqueness();
    
    printf("Test Summary:\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);
    
    return (tests_failed == 0) ? 0 : 1;
}