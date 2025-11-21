#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#define TEST_FILE "write_test.txt"
#define TEST_DATA "Hello, OS/161 write test!\n"
#define TEST_DATA_LEN 27

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
 * Test 1: Writes to stdout. It writes a known string to stdout and checks the return value.
 */
static void
test_write_stdout(void)
{
    const char *test_name = "Write to stdout";
    const char *msg = "Test output to stdout\n";
    int result;
    
    result = write(STDOUT_FILENO, msg, strlen(msg));
    print_result(test_name, result == (int)strlen(msg));
}

/*
 * Test 2: Writes to a file. It creates a file, writes known data, and checks the return value.
 */
static void
test_write_file(void)
{
    const char *test_name = "Write to file";
    int fd;
    int result;
    
    fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    
    result = write(fd, TEST_DATA, TEST_DATA_LEN);
    close(fd);
    
    if (result != TEST_DATA_LEN) {
        printf("  Error: Expected %d bytes, wrote %d\n", TEST_DATA_LEN, result);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Wrote %d bytes to file\n", result);
    print_result(test_name, 1);
}

/*
 * Test 3: Writes with invalid fd. It calls write on an invalid descriptor 999 and expects it to fail.
 */
static void
test_write_invalid_fd(void)
{
    const char *test_name = "Write with invalid fd (should fail)";
    int result;
    
    result = write(999, "test", 4);
    if (result >= 0) {
        printf("  Error: Should have failed but returned %d\n", result);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed (result=%d)\n", result);
    print_result(test_name, 1);
}

/*
 * Test 4: Write to closed fd. It opens a file, closes it, then attempts to write
 * to the closed descriptor, expecting failure.
 */
static void
test_write_closed_fd(void)
{
    const char *test_name = "Write to closed fd (should fail)";
    int fd;
    int result;
    
    fd = open(TEST_FILE, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    
    close(fd);
    
    result = write(fd, "test", 4);
    if (result >= 0) {
        printf("  Error: Should have failed but returned %d\n", result);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Correctly failed (result=%d)\n", result);
    print_result(test_name, 1);
}

/*
 * Test 5: Write to read-only file. It opens a file in read-only mode and attempts to write,
 * expecting failure.
 */
static void
test_write_readonly_file(void)
{
    const char *test_name = "Write to read-only file (should fail)";
    int fd;
    int result;
    
    fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, "data", 4);
        close(fd);
    }
    
    fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    
    result = write(fd, "test", 4);
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
 * Test 6: Multiple writes to same file. It opens a file and performs multiple write calls,
 * verifying the total bytes written.
 */
static void
test_multiple_writes(void)
{
    const char *test_name = "Multiple writes to same file";
    int fd;
    int result1, result2;
    
    fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  Error: Could not open file\n");
        print_result(test_name, 0);
        return;
    }
    
    result1 = write(fd, "First ", 6);
    result2 = write(fd, "Second\n", 7);
    close(fd);
    
    if (result1 != 6 || result2 != 7) {
        printf("  Error: Write failed (result1=%d, result2=%d)\n", result1, result2);
        print_result(test_name, 0);
        return;
    }
    
    printf("  Wrote %d + %d = %d bytes\n", result1, result2, result1 + result2);
    print_result(test_name, 1);
}

int
main(void)
{
    printf("write System Call Test Suite\n");
    
    test_write_stdout();
    test_write_file();
    test_write_invalid_fd();
    test_write_closed_fd();
    test_write_readonly_file();
    test_multiple_writes();
    
    printf("write Test Summary:\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("Total:  %d\n", tests_passed + tests_failed);
    
    return (tests_failed == 0) ? 0 : 1;
}