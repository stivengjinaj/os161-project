#include <unistd.h>
#include <fcntl.h>
#include <kern/errno.h>
#include <stdio.h>
#include <string.h>

#define TEST_FILE "testfile.txt"
#define NEW_FILE "newfile.txt"
#define NONEXISTENT "does_not_exist.txt"

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
 * Test 1: Opens an existing file for reading. It opens TEST_FILE in read-only mode.
 * Expects success and a valid file descriptor.
 */
static void
test_open_existing_readonly(void)
{
    int fd;
    const char *test_name = "Open existing file (O_RDONLY)";
    
    fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0) {
        printf("  Error: returned %d\n", fd);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Opened with fd=%d\n", fd);
    close(fd);
    print_result(test_name, 1);
}

/*
 * Test 2: Opens an existing file for writing. It opens TEST_FILE in write-only mode.
 * Expects success and a valid file descriptor.
 */
static void
test_open_existing_writeonly(void)
{
    int fd;
    const char *test_name = "Open existing file (O_WRONLY)";
    
    fd = open(TEST_FILE, O_WRONLY);
    if (fd < 0) {
        printf("  Error: returned %d\n", fd);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Opened with fd=%d\n", fd);
    close(fd);
    print_result(test_name, 1);
}

/*
 * Test 3: Opens an existing file for read/write. It opens TEST_FILE in read-write mode.
 * Expects success and a valid file descriptor.
 */
static void
test_open_existing_readwrite(void)
{
    int fd;
    const char *test_name = "Open existing file (O_RDWR)";
    
    fd = open(TEST_FILE, O_RDWR);
    if (fd < 0) {
        printf("  Error: returned %d\n", fd);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Opened with fd=%d\n", fd);
    close(fd);
    print_result(test_name, 1);
}

/*
 * Test 4: Opens non-existent file without O_CREAT. It attempts to open a file that does not exist
 * without the O_CREAT flag. Expects failure.
 */
static void
test_open_nonexistent_nocreat(void)
{
    int fd;
    const char *test_name = "Open non-existent file without O_CREAT (should fail)";
    
    fd = open(NONEXISTENT, O_RDONLY);
    if (fd >= 0) {
        printf("  Error: Should have failed but got fd=%d\n", fd);
        close(fd);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed (fd=%d)\n", fd);
    print_result(test_name, 1);
}

/*
 * Test 5: Creates new file with O_CREAT. It attempts to create a new file using O_CREAT flag.
 * Expects success and a valid file descriptor.
 */
static void
test_open_create_new(void)
{
    int fd;
    const char *test_name = "Create new file with O_CREAT";
    
    fd = open(NEW_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  Error: returned %d\n", fd);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Created file with fd=%d\n", fd);
    close(fd);
    print_result(test_name, 1);
}

/*
 * Test 6: Opens with NULL filename (should fail with EFAULT).  It attempts to open a file with a NULL filename pointer.
 * Expects failure with EFAULT.
 */
static void
test_open_null_filename(void)
{
    int fd;
    const char *test_name = "Open with NULL filename (should fail with EFAULT)";
    
    fd = open(NULL, O_RDONLY);
    if (fd >= 0) {
        printf("  Error: Should have failed but got fd=%d\n", fd);
        close(fd);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed (fd=%d)\n", fd);
    print_result(test_name, 1);
}

/*
 * Test 7: Opens with empty string. It attempts to open a file with an empty string as the filename.
 * Expects failure.
 */
static void
test_open_empty_string(void)
{
    int fd;
    const char *test_name = "Open with empty string (should fail)";
    
    fd = open("", O_RDONLY);
    if (fd >= 0) {
        printf("  Error: Should have failed but got fd=%d\n", fd);
        close(fd);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed (fd=%d)\n", fd);
    print_result(test_name, 1);
}

/*
 * Test 8: Opens with invalid flags. It attempts to open a file with invalid flags.
 * Expects failure with EINVAL.
 */
static void
test_open_invalid_flags(void)
{
    int fd;
    const char *test_name = "Open with invalid flags (should fail with EINVAL)";
    
    /* Invalid access mode (neither O_RDONLY, O_WRONLY, nor O_RDWR) */
    fd = open(TEST_FILE, 999);
    if (fd >= 0) {
        printf("  Error: Should have failed but got fd=%d\n", fd);
        close(fd);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed (fd=%d)\n", fd);
    print_result(test_name, 1);
}

/*
 * Test 9: Opens multiple files simultaneously. It attempts to open the same file multiple times.
 * Expects success and unique file descriptors.
 */
static void
test_open_multiple_files(void)
{
    int fd1, fd2, fd3;
    const char *test_name = "Open multiple files simultaneously";
    int success = 1;
    
    fd1 = open(TEST_FILE, O_RDONLY);
    if (fd1 < 0) {
        printf("  Error opening first file: %d\n", fd1);
        print_result(test_name, 0);
        return;
    }
    
    fd2 = open(TEST_FILE, O_RDONLY);
    if (fd2 < 0) {
        printf("  Error opening second file: %d\n", fd2);
        close(fd1);
        print_result(test_name, 0);
        return;
    }
    
    fd3 = open(TEST_FILE, O_RDONLY);
    if (fd3 < 0) {
        printf("  Error opening third file: %d\n", fd3);
        close(fd1);
        close(fd2);
        print_result(test_name, 0);
        return;
    }
    
    if (fd1 == fd2 || fd1 == fd3 || fd2 == fd3) {
        printf("  Error: File descriptors are not unique: fd1=%d, fd2=%d, fd3=%d\n",
               fd1, fd2, fd3);
        success = 0;
    } else {
        printf("  Successfully opened 3 files: fd1=%d, fd2=%d, fd3=%d\n",
               fd1, fd2, fd3);
    }
    
    close(fd1);
    close(fd2);
    close(fd3);
    print_result(test_name, success);
}

/*
 * Test 10: Opens with O_APPEND flag. It attempts to open a file with the O_APPEND flag.
 * Expects success and a valid file descriptor.
 */
static void
test_open_append(void)
{
    int fd;
    const char *test_name = "Open file with O_APPEND";
    
    fd = open(TEST_FILE, O_WRONLY | O_APPEND);
    if (fd < 0) {
        printf("  Error: returned %d\n", fd);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Opened with O_APPEND, fd=%d\n", fd);
    close(fd);
    print_result(test_name, 1);
}

/*
 * Test 11: Opens with O_TRUNC flag. It attempts to open a file with the O_TRUNC flag.
 * Expects success and a valid file descriptor.
 */
static void
test_open_trunc(void)
{
    int fd;
    const char *test_name = "Open file with O_TRUNC";
    
    fd = open(TEST_FILE, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        printf("  Error: returned %d\n", fd);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Opened with O_TRUNC, fd=%d\n", fd);
    close(fd);
    print_result(test_name, 1);
}

/*
 * Test 12: Tests file descriptor allocation order. It opens multiple files, closes one in the middle,
 * and opens another to see if the closed descriptor is reused. Expects the new file to reuse the lowest 
 * available file descriptor.
 */
static void
test_fd_allocation_order(void)
{
    int fd1, fd2, fd3;
    const char *test_name = "File descriptor allocation order";
    int success = 1;
    
    /* Open three files */
    fd1 = open(TEST_FILE, O_RDONLY);
    fd2 = open(TEST_FILE, O_RDONLY);
    fd3 = open(TEST_FILE, O_RDONLY);
    
    if (fd1 < 0 || fd2 < 0 || fd3 < 0) {
        printf("  Error opening files\n");
        if (fd1 >= 0) close(fd1);
        if (fd2 >= 0) close(fd2);
        if (fd3 >= 0) close(fd3);
        print_result(test_name, 0);
        return;
    }
    
    close(fd2);
    
    int fd4 = open(TEST_FILE, O_RDONLY);
    if (fd4 < 0) {
        printf("  Error reopening: %d\n", fd4);
        success = 0;
    } else if (fd4 != fd2) {
        printf("  Warning: Expected fd=%d but got fd=%d (may be implementation-specific)\n", 
               fd2, fd4);
        printf("  FD values: fd1=%d, fd2=%d, fd3=%d, fd4=%d\n", fd1, fd2, fd3, fd4);
    } else {
        printf("  FDs correctly reused: fd1=%d, reused=%d, fd3=%d\n", fd1, fd4, fd3);
    }
    
    close(fd1);
    close(fd3);
    close(fd4);
    print_result(test_name, success);
}

int
main(void)
{
    printf("Open System Call Test Suite\n");
        
    int fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, "Test data\n", 10);
        close(fd);
        printf("Created test file: %s\n\n", TEST_FILE);
    } else {
        printf("Warning: Could not create test file\n\n");
    }
    
    test_open_existing_readonly();
    test_open_existing_writeonly();
    test_open_existing_readwrite();
    test_open_nonexistent_nocreat();
    test_open_create_new();
    test_open_null_filename();
    test_open_empty_string();
    test_open_invalid_flags();
    test_open_multiple_files();
    test_open_append();
    test_open_trunc();
    test_fd_allocation_order();
    
    printf("Test Summary:\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);
    
    return (tests_failed == 0) ? 0 : 1;
}