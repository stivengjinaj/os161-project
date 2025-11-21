#include <unistd.h>
#include <fcntl.h>
#include <kern/seek.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#define TEST_FILE "lseek_test.txt"
#define TEST_DATA "Hello, OS/161 lseek test!\n"
#define TEST_DATA_LEN 27

int 
main(void) 
{
    int fd;
    off_t pos;
    int result;

    printf("lseek Test Summary\n");

    /* Create and write to test file */
    fd = open(TEST_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("[FAIL] Could not create test file (fd=%d)\n", fd);
        return 1;
    }
    
    result = write(fd, TEST_DATA, TEST_DATA_LEN);
    if (result != TEST_DATA_LEN) {
        printf("[FAIL] Write failed (wrote %d bytes, expected %d)\n", result, TEST_DATA_LEN);
        close(fd);
        return 1;
    }
    printf("Wrote %d bytes to file\n", result);

    /* Test 1: Seek to beginning */
    pos = lseek(fd, 0, SEEK_SET);
    if (pos < 0) {
        printf("[FAIL] lseek to beginning failed (pos=%ld)\n", (long)pos);
        close(fd);
        return 1;
    }
    if (pos != 0) {
        printf("[FAIL] lseek to beginning: expected pos=0, got %ld\n", (long)pos);
        close(fd);
        return 1;
    }
    printf("[PASS] Seek to beginning successful (position=%ld)\n", (long)pos);

    /* Test 2: Seek to end */
    pos = lseek(fd, 0, SEEK_END);
    if (pos < 0) {
        printf("[FAIL] lseek to end failed (pos=%ld)\n", (long)pos);
        close(fd);
        return 1;
    }
    if (pos != TEST_DATA_LEN) {
        printf("[FAIL] lseek to end: expected pos=%d, got %ld\n", TEST_DATA_LEN, (long)pos);
        close(fd);
        return 1;
    }
    printf("[PASS] Seek to end successful (position=%ld)\n", (long)pos);

    /* Test 3: Seek to middle using SEEK_SET */
    pos = lseek(fd, 10, SEEK_SET);
    if (pos < 0) {
        printf("[FAIL] lseek to position 10 failed (pos=%ld)\n", (long)pos);
        close(fd);
        return 1;
    }
    if (pos != 10) {
        printf("[FAIL] lseek to position 10: expected pos=10, got %ld\n", (long)pos);
        close(fd);
        return 1;
    }
    printf("[PASS] Seek to position 10 successful (position=%ld)\n", (long)pos);

    /* Test 4: Seek relative to current position */
    pos = lseek(fd, 5, SEEK_CUR);
    if (pos < 0) {
        printf("[FAIL] lseek SEEK_CUR +5 failed (pos=%ld)\n", (long)pos);
        close(fd);
        return 1;
    }
    if (pos != 15) {
        printf("[FAIL] lseek SEEK_CUR: expected pos=15, got %ld\n", (long)pos);
        close(fd);
        return 1;
    }
    printf("[PASS] Seek relative to current (+5) successful (position=%ld)\n", (long)pos);

    close(fd);
    printf("\nAll lseek tests passed!\n");
    return 0;
}