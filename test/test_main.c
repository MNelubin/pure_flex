#include "protocol.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <memory.h>
#include <inttypes.h>
#include <sys/types.h>

#include <cmocka.h>

#if defined(getc) || defined(putc) || defined(fprintf)
#error "Not handled yet: redefinition of stdio functions"
#endif

#define TEST_BUFFER_SIZE 512
static char buffer_stderr[TEST_BUFFER_SIZE];
static volatile size_t buffer_putc_len;
static uint8_t buffer_putc[TEST_BUFFER_SIZE];
static size_t buffer_getc_len;
static size_t buffer_getc_index;
static uint8_t buffer_getc[TEST_BUFFER_SIZE];

static FILE *message_stream;

static enum {
    UNDEFINED,
    MSB_FIRST,
    LSB_FIRST,
} mode = UNDEFINED;

/**
 * A mock getc function that get character from buffer
 * @param[in] stream
 * @return reads the next character from buffer and returns it
 * as an unsigned char cast to an int, or EOF when nothing is left
 * in the buffer
 */
int __wrap_getc(FILE *stream) {
    assert_true(stream == message_stream);
    return buffer_getc_index == buffer_getc_len
           ? EOF
           : buffer_getc[buffer_getc_index++];
}

/**
 * A mock putc function that put character to buffer
 * @param[in] c
 * @param[in] stream
 * @return the character written as an unsigned char cast to an int or EOF when buffer is full
 */
int __wrap_putc(int c, FILE *stream) {
    assert_true(stream == message_stream);
    if (buffer_putc_len == TEST_BUFFER_SIZE) {
        return EOF;
    }
    buffer_putc[buffer_putc_len++] = (uint8_t) c;
    return c;
}

/**
 * A mock fprintf function that checks the value of strings printed to the
 * standard error stream or output stream.
 */
int __wrap_fprintf(FILE *file, const char *format, ...) {
    int return_value;
    va_list args;
    assert_true(file == stderr);
    va_start(args, format);
    return_value = vsnprintf(buffer_stderr, sizeof(buffer_stderr),
                             format, args);
    va_end(args);
    return return_value;
}

/*
 * Wrap for exit(int) function
 **/
void __wrap_exit(int status) {
    (void)status;
    return;
}

/**
 * Create test stream with tmpfile()
 *
 * Run once before tests.
 * @param state unused (required by cmocka API)
 */
static int test_group_setup(void **state) {
    (void) state;
    message_stream = tmpfile();
    bzero(buffer_putc, TEST_BUFFER_SIZE);
    buffer_putc_len = 0u;
    uint8_t message[] = { // "write_message"
            0x80, // 10000000
    };
    write_message(message_stream, message, sizeof(message));
    if (buffer_putc_len != 3
        || buffer_putc[0] != 0x7E
        || buffer_putc[2] != 0x7E) {
        return 1;
    }
    if (buffer_putc[1] == 0x80) { // 10000000
        mode = MSB_FIRST;
        return 0;
    }
    if (buffer_putc[1] == 0x01) { // 00000001
        mode = LSB_FIRST;
        return 0;
    }
    return 1;
}

/**
 * Close test stream.
 * Run once after tests.
 * @param state unused (required by cmocka API)
 */
static int test_group_teardown(void **state) {
    (void) state;
    fclose(message_stream);
    return 0;
}

/**
 * Free buffers needed by tests.
 * Run before each test.
 * @param state unused (required by cmocka API)
 */
static int test_setup(void **state) {
    (void) state;
    bzero(buffer_stderr, TEST_BUFFER_SIZE);
    bzero(buffer_putc, TEST_BUFFER_SIZE);
    buffer_putc_len = 0u;
    bzero(buffer_getc, TEST_BUFFER_SIZE);
    buffer_getc_len = 0u;
    buffer_getc_index = 0u;
    return 0;
}

static void example_test_write_empty_message(void **state) {
    (void) state;
    const uint8_t message[1] = {0};
    ssize_t message_len = write_message(message_stream, message, 0);

    const uint8_t expected[] = {
            0x7E,
            0x7E,
    };
    // Check buffer
    assert_int_equal(message_len, 0);
    assert_int_equal(buffer_putc_len, 2);
    assert_memory_equal(buffer_putc, expected, 2);
    // Check error string
    assert_string_equal(buffer_stderr, "");
}

static void example_test_write_message(void **state) {
    (void) state;
    const uint8_t message[] = { // "write_message"
            0x77, // 01110111
            0x72, // 01110010
            0x69, // 01101001
            0x74, // 01110100
            0x65, // 01100101
            0x5F, // 01011111
            0x74, // 01110100
            0x65, // 01100101
            0x73, // 01110011
            0x74, // 01110100
    };
    ssize_t message_len = write_message(message_stream, message, sizeof(message));
    const uint8_t expected_msb[] = {
            0x7E, // 01111110
            0x77, // 01110111
            0x72, // 01110010
            0x69, // 01101001
            0x74, // 01110100
            0x65, // 01100101
            0x5F, // 01011111
            0x3A, // 00111010
            0x32, // 00110010
            0xB9, // 10111001
            0xBA, // 10111010
            0x3F, // 00111111
            0x7F, // 01111111
    };
    const uint8_t expected_lsb[] = {
            0x7E, // 01111110
            0xEE, // 11101110
            0x4E, // 01001110
            0x96, // 10010110
            0x2E, // 00101110
            0xA6, // 10100110
            0xFA, // 11111010
            0x5C, // 01011100
            0x4C, // 01001100
            0x9D, // 10011101
            0x5D, // 01011101
            0xFC, // 11111100
            0xFE, // 11111110
    };
    // Check buffer
    assert_int_equal(message_len, sizeof(message));
    assert_int_equal(buffer_putc_len, 13);
    if (mode == MSB_FIRST) {
        assert_memory_equal(buffer_putc, expected_msb, buffer_putc_len);
    } else {
        assert_memory_equal(buffer_putc, expected_lsb, buffer_putc_len);
    }
    // Check error string
    assert_string_equal(buffer_stderr, "");
}

static void example_test_read_empty_message(void **state) {
    (void) state;
    uint8_t message[MAX_MESSAGE_LEN] = {0};
    buffer_getc[0] = 0x7E;
    buffer_getc[1] = 0x7E;
    buffer_getc_len = 2;
    ssize_t message_len = read_message(message_stream, message);
    assert_int_equal(message_len, 0);
    assert_string_equal(buffer_stderr, "");
}

static void example_test_read_message(void **state) {
    (void) state;
    uint8_t message[MAX_MESSAGE_LEN] = {0}; // "read_test"
    if (mode == MSB_FIRST) {
        buffer_getc[0] = 0x7E; // 01111110
        buffer_getc[1] = 0x72; // 01110010
        buffer_getc[2] = 0x65; // 01100101
        buffer_getc[3] = 0x61; // 01100001
        buffer_getc[4] = 0x64; // 01100100
        buffer_getc[5] = 0x5F; // 01011111
        buffer_getc[6] = 0x3A; // 00111010
        buffer_getc[7] = 0x32; // 00110010
        buffer_getc[8] = 0xB9; // 10111001
        buffer_getc[9] = 0xBA; // 10111010
        buffer_getc[10] = 0x3F; // 00111111
        buffer_getc[11] = 0x7F; // 01111111
    } else {
        buffer_getc[0] = 0x7E; // 01111110
        buffer_getc[1] = 0x4E; // 01001110
        buffer_getc[2] = 0xA6; // 10100110
        buffer_getc[3] = 0x86; // 10000110
        buffer_getc[4] = 0x26; // 00100110
        buffer_getc[5] = 0xFA; // 11111010
        buffer_getc[6] = 0x5C; // 01011100
        buffer_getc[7] = 0x4C; // 01001100
        buffer_getc[8] = 0x9D; // 10011101
        buffer_getc[9] = 0x5D; // 01011101
        buffer_getc[10] = 0xFC; // 11111100
        buffer_getc[11] = 0xFE; // 11111110
    }
    buffer_getc_len = 12;
    ssize_t message_len = read_message(message_stream, message);
    const uint8_t expected[] = { // "read_test"
            0x72, // 01110010
            0x65, // 01100101
            0x61, // 01100001
            0x64, // 01100100
            0x5F, // 01011111
            0x74, // 01110100
            0x65, // 01100101
            0x73, // 01110011
            0x74, // 01110100
    };
    assert_int_equal(message_len, 9);
    assert_memory_equal(message, expected, message_len);
    assert_string_equal(buffer_stderr, "");
}

static void example_test_read_with_prefix_bits_1(void **state) {
    (void) state;
    uint8_t message[MAX_MESSAGE_LEN] = {0}; // "read_test"
    if (mode == MSB_FIRST) {
        buffer_getc[0] = 0xBF; // 10111111
        buffer_getc[1] = 0x39; // 00111001
        buffer_getc[2] = 0x32; // 00110010
        buffer_getc[3] = 0xB0; // 10110000
        buffer_getc[4] = 0xB2; // 10110010
        buffer_getc[5] = 0x2F; // 00101111
        buffer_getc[6] = 0x9D; // 10011101
        buffer_getc[7] = 0x19; // 00011001
        buffer_getc[8] = 0x5C; // 01011100
        buffer_getc[9] = 0xDD; // 11011101
        buffer_getc[10] = 0x1F; // 00011111
        buffer_getc[11] = 0xBF; // 10111111
    } else {
        buffer_getc[0] = 0xFD; // 11111101
        buffer_getc[1] = 0x9C; // 10011100
        buffer_getc[2] = 0x4C; // 01001100
        buffer_getc[3] = 0x0D; // 00001101
        buffer_getc[4] = 0x4D; // 01001101
        buffer_getc[5] = 0xF4; // 11110100
        buffer_getc[6] = 0xB9; // 10111001
        buffer_getc[7] = 0x98; // 10011000
        buffer_getc[8] = 0x3A; // 00111010
        buffer_getc[9] = 0xBB; // 10111011
        buffer_getc[10] = 0xF8; // 11111000
        buffer_getc[11] = 0xFD; // 11111101
    }
    buffer_getc_len = 12;
    ssize_t message_len = read_message(message_stream, message);
    const uint8_t expected[] = { // "read_test"
            0x72, // 01110010
            0x65, // 01100101
            0x61, // 01100001
            0x64, // 01100100
            0x5F, // 01011111
            0x74, // 01110100
            0x65, // 01100101
            0x73, // 01110011
            0x74, // 01110100
    };
    assert_int_equal(message_len, 9);
    assert_memory_equal(message, expected, message_len);
    assert_string_equal(buffer_stderr, "");
}

static void example_test_write_read_empty_message(void **state) {
    (void) state;
    const char *message_written = "";
    size_t len = strlen(message_written);
    write_message(message_stream, message_written, len);
    memcpy(buffer_getc, buffer_putc, buffer_putc_len);
    buffer_getc_len = buffer_putc_len;
    char message_read[MAX_MESSAGE_LEN] = {0};
    read_message(message_stream, message_read);

    assert_int_equal(strlen(message_read), len);
    assert_string_equal(message_read, message_written);
    assert_string_equal(buffer_stderr, "");
}

static void example_test_write_read_message(void **state) {
    (void) state;
    const char *message_written = "write_read_test";
    size_t len = strlen(message_written);
    ssize_t message_written_len = write_message(message_stream, message_written, len);
    memcpy(buffer_getc, buffer_putc, buffer_putc_len);
    buffer_getc_len = buffer_putc_len;
    char message_read[MAX_MESSAGE_LEN] = {0};
    ssize_t message_read_len = read_message(message_stream, message_read);

    assert_int_equal(message_written_len, len);
    assert_int_equal(message_read_len, len);
    assert_int_equal(strlen(message_read), len);
    assert_string_equal(message_read, message_written);
    assert_string_equal(buffer_stderr, "");
}

int main() {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test_setup(example_test_write_empty_message, test_setup),
            cmocka_unit_test_setup(example_test_write_message, test_setup),
            cmocka_unit_test_setup(example_test_read_empty_message, test_setup),
            cmocka_unit_test_setup(example_test_read_message, test_setup),
            cmocka_unit_test_setup(example_test_read_with_prefix_bits_1, test_setup),
            cmocka_unit_test_setup(example_test_write_read_empty_message, test_setup),
            cmocka_unit_test_setup(example_test_write_read_message, test_setup),
    };

    return cmocka_run_group_tests(tests, test_group_setup, test_group_teardown);
}
