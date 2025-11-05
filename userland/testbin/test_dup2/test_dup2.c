#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

#define TEST_FILE "dup2_test.txt"
#define TEST_DATA "Hello, dup2 test!\n"

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
 * Test 1: Basic dup2 functionality
 * Duplicates a valid file descriptor to another valid descriptor number
 */
static void
test_dup2_basic(void)
{
    const char *test_name = "Basic dup2 functionality";
    int fd, new_fd;
    char buffer[64];
    int result;
    
    fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  Error: Could not create test file\n");
        print_result(test_name, 0);
        return;
    }
    
    new_fd = 10;
    result = dup2(fd, new_fd);
    
    if (result != new_fd) {
        printf("  Error: dup2 returned %d, expected %d\n", result, new_fd);
        close(fd);
        print_result(test_name, 0);
        return;
    }
    
    result = write(new_fd, TEST_DATA, strlen(TEST_DATA));
    close(fd);
    close(new_fd);
    
    fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0) {
        printf("  Error: Could not reopen file\n");
        print_result(test_name, 0);
        return;
    }
    
    memset(buffer, 0, sizeof(buffer));
    read(fd, buffer, sizeof(buffer));
    close(fd);
    
    if (strcmp(buffer, TEST_DATA) != 0) {
        printf("  Error: Data mismatch\n");
        print_result(test_name, 0);
        return;
    }
    
    printf("  Successfully duplicated fd %d to fd %d\n", fd, new_fd);
    print_result(test_name, 1);
}

/*
 * Test 2: dup2 with same oldfd and newfd
 * According to POSIX, dup2(fd, fd) should succeed and return fd
 */
static void
test_dup2_same_fd(void)
{
    const char *test_name = "dup2 with oldfd == newfd";
    int fd, result;
    
    fd = open(TEST_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    
    result = dup2(fd, fd);
    close(fd);
    
    if (result != fd) {
        printf("  Error: dup2(fd, fd) returned %d, expected %d\n", result, fd);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly returned fd %d\n", fd);
    print_result(test_name, 1);
}

/*
 * Test 3: dup2 with invalid oldfd
 * Should fail when oldfd is not a valid open file descriptor
 */
static void
test_dup2_invalid_oldfd(void)
{
    const char *test_name = "dup2 with invalid oldfd (should fail)";
    int result;
    
    result = dup2(999, 5);
    
    if (result >= 0) {
        printf("  Error: Should have failed but returned %d\n", result);
        close(result);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed (result=%d)\n", result);
    print_result(test_name, 1);
}

/*
 * Test 4: dup2 with invalid newfd
 * Should fail when newfd is negative or exceeds system limits
 */
static void
test_dup2_invalid_newfd(void)
{
    const char *test_name = "dup2 with invalid newfd (should fail)";
    int fd, result;
    
    fd = open(TEST_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    
    result = dup2(fd, -1);
    close(fd);
    
    if (result >= 0) {
        printf("  Error: Should have failed but returned %d\n", result);
        close(result);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed with negative newfd (result=%d)\n", result);
    print_result(test_name, 1);
}

/*
 * Test 5: dup2 closes newfd if already open
 * If newfd is already open, dup2 should close it first
 */
static void
test_dup2_closes_newfd(void)
{
    const char *test_name = "dup2 closes newfd if already open";
    int fd1, fd2;
    char buffer[64];
    int result;
    
    /* Open two different files */
    fd1 = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    fd2 = open("dup2_temp.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    
    if (fd1 < 0 || fd2 < 0) {
        printf("  Error: Could not create test files\n");
        if (fd1 >= 0) close(fd1);
        if (fd2 >= 0) close(fd2);
        print_result(test_name, 0);
        return;
    }
    
    write(fd1, "File1", 5);
    write(fd2, "File2", 5);
    
    /* dup2 should close fd2 and make it point to fd1 */
    result = dup2(fd1, fd2);
    
    if (result != fd2) {
        printf("  Error: dup2 returned %d, expected %d\n", result, fd2);
        close(fd1);
        close(fd2);
        print_result(test_name, 0);
        return;
    }
    
    /* Write more data using fd2 (now points to fd1's file) */
    write(fd2, " More", 5);
    close(fd1);
    close(fd2);
    
    /* Verify the content */
    fd1 = open(TEST_FILE, O_RDONLY);
    memset(buffer, 0, sizeof(buffer));
    read(fd1, buffer, sizeof(buffer));
    close(fd1);
    
    if (strcmp(buffer, "File1 More") != 0) {
        printf("  Error: Data mismatch, got '%s'\n", buffer);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Successfully closed and duplicated fd\n");
    print_result(test_name, 1);
}

/*
 * Test 6: dup2 with standard file descriptors
 * Test duplicating to/from stdin, stdout, stderr
 */
static void
test_dup2_stdio(void)
{
    const char *test_name = "dup2 with stdout";
    int fd, old_stdout, result;
    
    fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  Error: Could not create test file\n");
        print_result(test_name, 0);
        return;
    }
    
    /* Save stdout */
    old_stdout = dup2(STDOUT_FILENO, 20);
    if (old_stdout < 0) {
        printf("  Error: Could not save stdout\n");
        close(fd);
        print_result(test_name, 0);
        return;
    }
    
    /* Redirect stdout to file */
    result = dup2(fd, STDOUT_FILENO);
    if (result != STDOUT_FILENO) {
        dup2(old_stdout, STDOUT_FILENO);
        close(old_stdout);
        close(fd);
        printf("  Error: Could not redirect stdout\n");
        print_result(test_name, 0);
        return;
    }
    
    printf("Redirected output\n");
    
    /* Restore stdout */
    dup2(old_stdout, STDOUT_FILENO);
    close(old_stdout);
    close(fd);
    
    printf("  Successfully redirected stdout\n");
    print_result(test_name, 1);
}

int
main(void)
{
    printf("dup2 System Call Tests\n");
    
    test_dup2_basic();
    test_dup2_same_fd();
    test_dup2_invalid_oldfd();
    test_dup2_invalid_newfd();
    test_dup2_closes_newfd();
    test_dup2_stdio();
    
    printf("Test Summary:\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);
    
    return (tests_failed == 0) ? 0 : 1;
}