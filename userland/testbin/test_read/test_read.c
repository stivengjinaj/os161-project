#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#define TEST_FILE "read_test.txt"
#define TEST_DATA "Hello, OS/161 read test!\n"
#define TEST_DATA_LEN 26

static int tests_passed = 0;
static int tests_failed = 0;

/* Helper function to compare memory */
static int
memcmp_local(const char *s1, const char *s2, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return s1[i] - s2[i];
        }
    }
    return 0;
}

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
 * Test 1: Read from a file
 */
static void
test_read_file(void)
{
    const char *test_name = "Read from file";
    int fd;
    char buffer[128];
    int result;
    
    /* Create test file first */
    fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  Error: Could not create test file\n");
        print_result(test_name, 0);
        return;
    }
    write(fd, TEST_DATA, TEST_DATA_LEN);
    close(fd);
    
    /* Now read it */
    fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0) {
        printf("  Error: Could not open file for reading\n");
        print_result(test_name, 0);
        return;
    }
    
    memset(buffer, 0, sizeof(buffer));
    result = read(fd, buffer, TEST_DATA_LEN);
    close(fd);
    
    if (result != TEST_DATA_LEN) {
        printf("  Error: Expected %d bytes, read %d\n", TEST_DATA_LEN, result);
        print_result(test_name, 0);
        return;
    }
    
    if (strcmp(buffer, TEST_DATA) != 0) {
        printf("  Error: Data mismatch\n");
        printf("  Expected: '%s'\n", TEST_DATA);
        printf("  Got:      '%s'\n", buffer);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Read %d bytes successfully\n", result);
    print_result(test_name, 1);
}

/*
 * Test 2: Read with invalid fd
 */
static void
test_read_invalid_fd(void)
{
    const char *test_name = "Read with invalid fd (should fail)";
    char buffer[10];
    int result;
    
    result = read(999, buffer, sizeof(buffer));
    if (result >= 0) {
        printf("  Error: Should have failed but returned %d\n", result);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed (result=%d)\n", result);
    print_result(test_name, 1);
}

/*
 * Test 3: Read from closed fd
 */
static void
test_read_closed_fd(void)
{
    const char *test_name = "Read from closed fd (should fail)";
    int fd;
    char buffer[10];
    int result;
    
    /* Create and close a file */
    fd = open(TEST_FILE, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    close(fd);
    
    result = read(fd, buffer, sizeof(buffer));
    if (result >= 0) {
        printf("  Error: Should have failed but returned %d\n", result);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed (result=%d)\n", result);
    print_result(test_name, 1);
}

/*
 * Test 4: Read from write-only file
 */
static void
test_read_writeonly_file(void)
{
    const char *test_name = "Read from write-only file (should fail)";
    int fd;
    char buffer[10];
    int result;
    
    fd = open(TEST_FILE, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    
    result = read(fd, buffer, sizeof(buffer));
    close(fd);
    
    if (result >= 0) {
        printf("  Error: Should have failed but returned %d\n", result);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed (result=%d)\n", result);
    print_result(test_name, 1);
}

/*
 * Test 5: Multiple reads from same file
 */
static void
test_multiple_reads(void)
{
    const char *test_name = "Multiple reads from same file";
    int fd;
    char buffer1[10];
    char buffer2[10];
    int result1, result2;
    
    /* Create test file */
    fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, "0123456789ABCDEFGHIJ", 20);
        close(fd);
    }
    
    /* Read in chunks */
    fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    
    memset(buffer1, 0, sizeof(buffer1));
    memset(buffer2, 0, sizeof(buffer2));
    
    result1 = read(fd, buffer1, 10);
    result2 = read(fd, buffer2, 10);
    close(fd);
    
    if (result1 != 10 || result2 != 10) {
        printf("  Error: Read failed (result1=%d, result2=%d)\n", result1, result2);
        print_result(test_name, 0);
        return;
    }
    
    if (memcmp_local(buffer1, "0123456789", 10) != 0 || 
        memcmp_local(buffer2, "ABCDEFGHIJ", 10) != 0) {
        printf("  Error: Data mismatch\n");
        print_result(test_name, 0);
        return;
    }
    
    printf("  Read %d + %d = %d bytes correctly\n", result1, result2, result1 + result2);
    print_result(test_name, 1);
}

/*
 * Test 6: Read beyond EOF
 */
static void
test_read_eof(void)
{
    const char *test_name = "Read beyond EOF";
    int fd;
    char buffer[128];
    int result;
    
    /* Create small file */
    fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, "Small", 5);
        close(fd);
    }
    
    /* Try to read more than exists */
    fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    
    memset(buffer, 0, sizeof(buffer));
    result = read(fd, buffer, 100);
    close(fd);
    
    if (result != 5) {
        printf("  Error: Expected 5 bytes, read %d\n", result);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Read %d bytes (file size) correctly\n", result);
    print_result(test_name, 1);
}

int
main(void)
{
    printf("========================================\n");
    printf("      Read System Call Tests\n");
    printf("========================================\n\n");
    
    test_read_file();
    test_read_invalid_fd();
    test_read_closed_fd();
    test_read_writeonly_file();
    test_multiple_reads();
    test_read_eof();
    
    printf("\n----------------------------------------\n");
    printf("Test Summary:\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  Total:  %d\n", tests_passed + tests_failed);
    printf("========================================\n");
    
    return (tests_failed == 0) ? 0 : 1;
}