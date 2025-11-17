#include "protocol.h"
#include <stdint.h>

static int write_bit(FILE *stream, int bit, uint8_t *out_byte, int *bit_pos) {
    if (bit) {
        *out_byte |= (uint8_t)(1u << *bit_pos);
    }
    (*bit_pos)--;
    if (*bit_pos < 0) {
        if (putc(*out_byte, stream) == EOF) {
            fprintf(stderr, "I/O error: putc failed\n");
            return -1;
        }
        *out_byte = 0u;
        *bit_pos = 7;
    }
    return 0;
}

static int write_flag(FILE *stream, uint8_t *out_byte, int *bit_pos) {
    const uint8_t FLAG = 0x7E; // 01111110
    for (int i = 7; i >= 0; --i) {
        int bit = (FLAG >> i) & 1u;
        if (write_bit(stream, bit, out_byte, bit_pos) < 0) {
            return -1;
        }
    }
    return 0;
}

static int pad_ones_to_byte(FILE *stream, uint8_t *out_byte, int *bit_pos) {
    if (*bit_pos != 7) {
        while (*bit_pos >= 0) {
            if (write_bit(stream, 1, out_byte, bit_pos) < 0) {
                return -1;
            }
        }
    }
    return 0;
}

/**
 * Encode and write a message using bit stuffing and HDLC-like flags.
 * Returns the number of payload bytes written on success, or -1 on error.
 */
ssize_t write_message(FILE* stream, const void *buf, size_t nbyte) {
    const uint8_t *in = (const uint8_t *)buf;

    uint8_t out_byte = 0u;
    int bit_pos = 7;
    int ones = 0;

    if (write_flag(stream, &out_byte, &bit_pos) < 0) {
        return -1;
    }

    for (size_t i = 0; i < nbyte; ++i) {
        uint8_t b = in[i];
        for (int j = 7; j >= 0; --j) { // MSB first
            int bit = (b >> j) & 1u;
            if (write_bit(stream, bit, &out_byte, &bit_pos) < 0) {
                return -1;
            }
            if (bit) {
                ones++;
                if (ones == 5) {
                    if (write_bit(stream, 0, &out_byte, &bit_pos) < 0) { // stuffed 0
                        return -1;
                    }
                    ones = 0;
                }
            } else {
                ones = 0;
            }
        }
    }

    if (write_flag(stream, &out_byte, &bit_pos) < 0) {
        return -1;
    }

    if (pad_ones_to_byte(stream, &out_byte, &bit_pos) < 0) {
        return -1;
    }

    return (ssize_t)nbyte;
}

static int read_bit(FILE *stream, int *curr_byte, int *bit_pos) {
    if (*bit_pos < 0) {
        int c = getc(stream);
        if (c == EOF) {
            return -1;
        }
        *curr_byte = c & 0xFF;
        *bit_pos = 7;
    }
    int bit = ((*curr_byte) >> (*bit_pos)) & 1;
    (*bit_pos)--;
    return bit;
}

/**
 * Read and decode a message using bit stuffing and HDLC-like flags.
 * Writes the decoded payload into buf and returns its size, or -1 on error.
 */
ssize_t read_message(FILE *stream, void *buf) {
    uint8_t *out = (uint8_t *)buf;

    // Bit buffers sized safely for MAX_MESSAGE_LEN with stuffing and flags
    enum { FRAME_BITS_MAX = MAX_MESSAGE_LEN * 10 + 32 };
    uint8_t frame_bits[FRAME_BITS_MAX];
    size_t frame_len = 0;

    int curr_byte = 0;
    int bit_pos = -1;
    unsigned int window = 0u;

    // Find start flag 0x7E
    for (;;) {
        int bit = read_bit(stream, &curr_byte, &bit_pos);
        if (bit < 0) {
            fprintf(stderr, "Read error: start flag not found\n");
            return -1;
        }
        window = ((window << 1) | (unsigned int)bit) & 0xFFu;
        if (window == 0x7E) {
            break;
        }
    }

    window = 0u;

    // Capture bits until end flag 0x7E
    for (;;) {
        int bit = read_bit(stream, &curr_byte, &bit_pos);
        if (bit < 0) {
            fprintf(stderr, "Read error: unexpected EOF inside frame\n");
            return -1;
        }
        if (frame_len == FRAME_BITS_MAX) {
            fprintf(stderr, "Frame too long\n");
            return -1;
        }
        frame_bits[frame_len++] = (uint8_t)bit;
        window = ((window << 1) | (unsigned int)bit) & 0xFFu;
        if (window == 0x7E) {
            if (frame_len < 8) {
                fprintf(stderr, "Protocol error: malformed frame\n");
                return -1;
            }
            frame_len -= 8; // drop end flag bits
            break;
        }
    }

    // Unstuff payload bits
    uint8_t payload_bits[FRAME_BITS_MAX];
    size_t payload_len = 0;
    int ones = 0;
    for (size_t i = 0; i < frame_len; ++i) {
        int bit = frame_bits[i];
        if (bit) {
            payload_bits[payload_len++] = 1u;
            ones++;
            if (ones > 5) {
                fprintf(stderr, "Protocol error: invalid sequence of ones\n");
                return -1;
            }
        } else {
            if (ones == 5) {
                // stuffed 0, skip
                ones = 0;
                continue;
            }
            payload_bits[payload_len++] = 0u;
            ones = 0;
        }
        if (payload_len > FRAME_BITS_MAX) {
            fprintf(stderr, "Payload too big\n");
            return -1;
        }
    }

    if (payload_len % 8 != 0) {
        fprintf(stderr, "Payload is not a whole number of bytes\n");
        return -1;
    }

    size_t out_bytes = payload_len / 8;
    if (out_bytes > MAX_MESSAGE_LEN) {
        fprintf(stderr, "Payload exceeds MAX_MESSAGE_LEN\n");
        return -1;
    }

    for (size_t i = 0; i < out_bytes; ++i) {
        uint8_t b = 0u;
        for (int j = 0; j < 8; ++j) { // MSB first
            b = (uint8_t)((b << 1) | payload_bits[i * 8 + j]);
        }
        out[i] = b;
    }

    return (ssize_t)out_bytes;
}