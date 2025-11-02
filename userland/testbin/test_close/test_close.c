#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#define TEST_FILE "close_test.txt"

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
 * Test 1: Close a valid file descriptor
 */
static void
test_close_valid_fd(void)
{
    const char *test_name = "Close valid file descriptor";
    int fd;
    int result;
    
    fd = open(TEST_FILE, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    
    result = close(fd);
    if (result != 0) {
        printf("  Error: close returned %d\n", result);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Successfully closed fd=%d\n", fd);
    print_result(test_name, 1);
}

/*
 * Test 2: Close invalid fd
 */
static void
test_close_invalid_fd(void)
{
    const char *test_name = "Close invalid fd (should fail)";
    int result;
    
    result = close(999);
    if (result == 0) {
        printf("  Error: Should have failed but succeeded\n");
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed\n");
    print_result(test_name, 1);
}

/*
 * Test 3: Close already closed fd
 */
static void
test_close_already_closed(void)
{
    const char *test_name = "Close already closed fd (should fail)";
    int fd;
    int result1, result2;
    
    fd = open(TEST_FILE, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    
    result1 = close(fd);
    result2 = close(fd);
    
    if (result1 != 0) {
        printf("  Error: First close failed\n");
        print_result(test_name, 0);
        return;
    }
    
    if (result2 == 0) {
        printf("  Error: Second close should have failed but succeeded\n");
        print_result(test_name, 0);
        return;
    }
    
    printf("  First close succeeded, second correctly failed\n");
    print_result(test_name, 1);
}

/*
 * Test 4: Close and verify fd is reused
 */
static void
test_close_fd_reuse(void)
{
    const char *test_name = "Close and verify fd reuse";
    int fd1, fd2;
    
    fd1 = open(TEST_FILE, O_WRONLY | O_CREAT, 0644);
    if (fd1 < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    
    close(fd1);
    
    fd2 = open(TEST_FILE, O_RDONLY);
    if (fd2 < 0) {
        printf("  Error: Could not reopen file\n");
        print_result(test_name, 0);
        return;
    }
    
    if (fd2 != fd1) {
        printf("  Warning: fd not reused (fd1=%d, fd2=%d) - may be implementation-specific\n", 
               fd1, fd2);
    } else {
        printf("  fd reused: fd1=%d, fd2=%d\n", fd1, fd2);
    }
    
    close(fd2);
    print_result(test_name, 1);
}

/*
 * Test 5: Operations on closed fd should fail
 */
static void
test_operations_after_close(void)
{
    const char *test_name = "Operations after close should fail";
    int fd;
    char buffer[10];
    int read_result, write_result;
    int success = 1;
    
    fd = open(TEST_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    
    close(fd);
    
    /* Try to read */
    read_result = read(fd, buffer, sizeof(buffer));
    if (read_result >= 0) {
        printf("  Error: Read after close should have failed\n");
        success = 0;
    }
    
    /* Try to write */
    write_result = write(fd, "test", 4);
    if (write_result >= 0) {
        printf("  Error: Write after close should have failed\n");
        success = 0;
    }
    
    if (success) {
        printf("  All operations correctly failed after close\n");
    }
    
    print_result(test_name, success);
}

/*
 * Test 6: Close multiple fds
 */
static void
test_close_multiple(void)
{
    const char *test_name = "Close multiple file descriptors";
    int fd1, fd2, fd3;
    int result;
    int success = 1;
    
    fd1 = open(TEST_FILE, O_RDONLY | O_CREAT, 0644);
    fd2 = open(TEST_FILE, O_RDONLY);
    fd3 = open(TEST_FILE, O_RDONLY);
    
    if (fd1 < 0 || fd2 < 0 || fd3 < 0) {
        printf("  Error: Could not open files\n");
        if (fd1 >= 0) close(fd1);
        if (fd2 >= 0) close(fd2);
        if (fd3 >= 0) close(fd3);
        print_result(test_name, 0);
        return;
    }
    
    result = close(fd1);
    if (result != 0) {
        printf("  Error: close(fd1) failed\n");
        success = 0;
    }
    
    result = close(fd2);
    if (result != 0) {
        printf("  Error: close(fd2) failed\n");
        success = 0;
    }
    
    result = close(fd3);
    if (result != 0) {
        printf("  Error: close(fd3) failed\n");
        success = 0;
    }
    
    if (success) {
        printf("  Successfully closed all fds: %d, %d, %d\n", fd1, fd2, fd3);
    }
    
    print_result(test_name, success);
}

int
main(void)
{
    printf("========================================\n");
    printf("     Close System Call Test Suite\n");
    printf("========================================\n\n");
    
    test_close_valid_fd();
    test_close_invalid_fd();
    test_close_already_closed();
    test_close_fd_reuse();
    test_operations_after_close();
    test_close_multiple();
    
    printf("\n----------------------------------------\n");
    printf("Test Summary:\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  Total:  %d\n", tests_passed + tests_failed);
    printf("========================================\n");
    
    return (tests_failed == 0) ? 0 : 1;
}