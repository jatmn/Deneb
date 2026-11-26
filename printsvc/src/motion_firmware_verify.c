/* SPDX-License-Identifier: MPL-2.0 */
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum {
    /* ATmega2560 flash minus the largest possible bootloader section. */
    APPLICATION_BYTES = 0x3e000,
    HEX_RECORD_BYTES = 5 + UINT8_MAX,
    FLASH_READ_BYTES = 256,
    FRAME_HEADER_BYTES = 5,
    REQUEST_BYTES = 6,
    RESPONSE_BYTES = FLASH_READ_BYTES + 3,
    COMMAND_TIMEOUT_MS = 1000,
    FLASH_READ_TIMEOUT_MS = 5000,
    STK_START = 0x1b,
    STK_TOKEN = 0x0e,
    STK_OK = 0,
    STK_LOAD_ADDRESS = 0x06,
    STK_READ_FLASH = 0x14,
    STK_READ_SIGNATURE = 0x1b
};

typedef struct {
    uint8_t bytes[APPLICATION_BYTES];
    uint8_t present[APPLICATION_BYTES / 8];
} firmware_image_t;

typedef enum {
    VERIFY_MATCH = 0,
    VERIFY_USAGE = 2,
    VERIFY_IMAGE,
    VERIFY_PEER,
    VERIFY_IDENTITY,
    VERIFY_TIMEOUT,
    VERIFY_DISCONNECT,
    VERIFY_TRUNCATED,
    VERIFY_PROTOCOL,
    VERIFY_MISMATCH
} verify_result_t;

static int nibble(int ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static bool decode_record(const char *line, size_t len, uint8_t *record)
{
    if (len < 11 || len % 2 == 0 || line[0] != ':')
        return false;

    unsigned sum = 0;
    for (size_t i = 0; i < len / 2; ++i) {
        int hi = nibble(line[1 + 2 * i]);
        int lo = nibble(line[2 + 2 * i]);
        if (hi < 0 || lo < 0)
            return false;
        record[i] = (uint8_t)((hi << 4) | lo);
        sum += record[i];
    }
    return len == 1 + 2 * (5 + (size_t)record[0]) && (sum & 0xff) == 0;
}

static bool image_has_byte(const firmware_image_t *image, uint32_t address)
{
    return (image->present[address / 8] & (1u << (address % 8))) != 0;
}

static bool read_image(FILE *file, firmware_image_t *image)
{
    char line[1 + 2 * HEX_RECORD_BYTES + 1];  /* Optional CR before LF. */
    uint8_t record[HEX_RECORD_BYTES];
    uint32_t base = 0;
    bool have_data = false;
    bool have_eof = false;

    for (;;) {
        size_t len = 0;
        int ch;
        while ((ch = fgetc(file)) != EOF && ch != '\n') {
            if (len == sizeof(line))
                return false;
            line[len++] = (char)ch;
        }
        if (ch == EOF && len == 0)
            return !ferror(file) && have_eof && have_data;
        if (ch == '\n' && len > 0 && line[len - 1] == '\r')
            --len;
        if (have_eof || len > 1 + 2 * HEX_RECORD_BYTES ||
            !decode_record(line, len, record))
            return false;

        uint32_t count = record[0];
        uint32_t offset = ((uint32_t)record[1] << 8) | record[2];
        switch (record[3]) {
        case 0x00: {
            uint32_t address = base + offset;
            if (count == 0 || count > 0x10000u - offset ||
                address >= APPLICATION_BYTES || count > APPLICATION_BYTES - address)
                return false;
            for (uint32_t i = 0; i < count; ++i, ++address) {
                if (image_has_byte(image, address))
                    return false;
                image->bytes[address] = record[4 + i];
                image->present[address / 8] |= (uint8_t)(1u << (address % 8));
            }
            have_data = true;
            break;
        }
        case 0x01:
            if (count != 0 || offset != 0)
                return false;
            have_eof = true;
            break;
        case 0x02:
        case 0x04:
            if (count != 2 || offset != 0)
                return false;
            base = ((uint32_t)record[4] << 8) | record[5];
            base <<= record[3] == 0x02 ? 4 : 16;
            if (base >= APPLICATION_BYTES)
                return false;
            break;
        default:
            return false;
        }
    }
}

static bool connected_local_stream(int fd)
{
    struct sockaddr_storage address;
    socklen_t address_size = sizeof(address);
    int type;
    socklen_t type_size = sizeof(type);

    return getpeername(fd, (struct sockaddr *)&address, &address_size) == 0 &&
           address.ss_family == AF_UNIX &&
           getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_size) == 0 &&
           type == SOCK_STREAM;
}

static int64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static verify_result_t transfer(int fd, bool input, uint8_t *bytes, size_t len,
                                int64_t deadline, bool receiving_frame)
{
    struct pollfd pfd = { .fd = fd, .events = input ? POLLIN : POLLOUT };
    size_t done = 0;
    while (done < len) {
        bool partial_reply = input && (receiving_frame || done != 0);
        int64_t now = monotonic_ms();
        if (now < 0)
            return VERIFY_PEER;
        if (now >= deadline)
            return partial_reply ? VERIFY_TRUNCATED : VERIFY_TIMEOUT;

        int ready = poll(&pfd, 1, (int)(deadline - now));
        if (ready == 0 || (ready < 0 && errno == EINTR))
            continue;
        if (ready < 0 || (pfd.revents & POLLNVAL))
            return VERIFY_PEER;

        ssize_t n = input
            ? recv(fd, bytes + done, len - done, MSG_DONTWAIT)
            : send(fd, bytes + done, len - done, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (n == 0 || (n < 0 && (errno == ECONNRESET || errno == EPIPE)))
            return partial_reply ? VERIFY_TRUNCATED : VERIFY_DISCONNECT;
        if (n < 0)
            return VERIFY_PEER;
        done += (size_t)n;
    }
    return VERIFY_MATCH;
}

static uint8_t frame_xor(const uint8_t *p, size_t len)
{
    uint8_t checksum = 0;
    for (size_t i = 0; i < len; ++i)
        checksum ^= p[i];
    return checksum;
}

static verify_result_t exchange(int fd, uint8_t *sequence,
                                const uint8_t *request, size_t request_size,
                                uint8_t *reply, size_t reply_size)
{
    uint8_t frame[FRAME_HEADER_BYTES + REQUEST_BYTES + 1] = {
        STK_START, *sequence, 0, (uint8_t)request_size, STK_TOKEN
    };
    size_t body_end = FRAME_HEADER_BYTES + request_size;
    memcpy(frame + FRAME_HEADER_BYTES, request, request_size);
    frame[body_end] = frame_xor(frame, body_end);

    int64_t start = monotonic_ms();
    if (start < 0)
        return VERIFY_PEER;
    verify_result_t result = transfer(fd, false, frame, body_end + 1,
                                      start + COMMAND_TIMEOUT_MS, false);
    if (result != VERIFY_MATCH)
        return result;

    /* AVR068 counts the time to receive the complete reply after sending. */
    start = monotonic_ms();
    if (start < 0)
        return VERIFY_PEER;
    int timeout_ms = request[0] == STK_READ_FLASH ? FLASH_READ_TIMEOUT_MS : COMMAND_TIMEOUT_MS;
    int64_t deadline = start + timeout_ms;
    result = transfer(fd, true, frame, FRAME_HEADER_BYTES, deadline, false);
    if (result != VERIFY_MATCH)
        return result;

    size_t claimed_size = ((size_t)frame[2] << 8) | frame[3];
    if (frame[0] != STK_START || frame[1] != *sequence || frame[4] != STK_TOKEN ||
        claimed_size < 2 || claimed_size > reply_size)
        return VERIFY_PROTOCOL;
    result = transfer(fd, true, reply, claimed_size + 1, deadline, true);
    if (result != VERIFY_MATCH)
        return result;
    if ((frame_xor(frame, FRAME_HEADER_BYTES) ^ frame_xor(reply, claimed_size + 1)) != 0 ||
        reply[0] != request[0] || reply[1] != STK_OK)
        return VERIFY_PROTOCOL;
    if (claimed_size < reply_size)
        return VERIFY_TRUNCATED;
    if (reply[reply_size - 1] != STK_OK)
        return VERIFY_PROTOCOL;

    ++*sequence;
    return VERIFY_MATCH;
}

static verify_result_t compare_flash(int fd, const firmware_image_t *image)
{
    static const uint8_t signature[] = { 0x1e, 0x98, 0x01 };
    uint8_t sequence = 1;
    uint8_t reply[RESPONSE_BYTES + 1];

    for (size_t i = 0; i < sizeof(signature); ++i) {
        const uint8_t request[] = { STK_READ_SIGNATURE, 4, 0x30, 0, (uint8_t)i, 0 };
        verify_result_t result = exchange(fd, &sequence, request, sizeof(request), reply, 4);
        if (result != VERIFY_MATCH)
            return result;
        if (reply[2] != signature[i])
            return VERIFY_IDENTITY;
    }

    for (uint32_t page = 0; page < APPLICATION_BYTES; page += FLASH_READ_BYTES) {
        uint32_t start = page;
        uint32_t end = page + FLASH_READ_BYTES;
        while (start < end && !image_has_byte(image, start))
            ++start;
        if (start == end)
            continue;
        while (!image_has_byte(image, end - 1))
            --end;

        /* The bootloader reads complete words. Compare only bytes in the HEX. */
        start &= ~1u;
        end = (end + 1) & ~1u;
        uint32_t word = start / 2;
        const uint8_t address[] = {
            STK_LOAD_ADDRESS, 0x80, (uint8_t)(word >> 16),
            (uint8_t)(word >> 8), (uint8_t)word
        };
        verify_result_t result = exchange(fd, &sequence, address, sizeof(address), reply, 2);
        if (result != VERIFY_MATCH)
            return result;
        uint32_t count = end - start;
        const uint8_t read_flash[] = {
            STK_READ_FLASH, (uint8_t)(count >> 8), (uint8_t)count, 0x20
        };
        result = exchange(fd, &sequence, read_flash, sizeof(read_flash), reply, count + 3);
        if (result != VERIFY_MATCH)
            return result;
        for (uint32_t a = start; a < end; ++a) {
            if (image_has_byte(image, a) && reply[2 + a - start] != image->bytes[a]) {
                fprintf(stderr, "Flash mismatch at byte 0x%05lx\n", (unsigned long)a);
                return VERIFY_MISMATCH;
            }
        }
    }
    return VERIFY_MATCH;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s IMAGE.hex (connected local stream socket on stdin)\n", argv[0]);
        return VERIFY_USAGE;
    }
    firmware_image_t *image = calloc(1, sizeof(*image));
    if (!image) {
        perror("Allocating firmware image");
        return VERIFY_IMAGE;
    }
    FILE *hex = fopen(argv[1], "rb");
    bool valid = hex && read_image(hex, image);
    if (hex)
        fclose(hex);

    verify_result_t result;
    if (!valid)
        result = VERIFY_IMAGE;
    else if (!connected_local_stream(STDIN_FILENO))
        result = VERIFY_PEER;
    else
        result = compare_flash(STDIN_FILENO, image);
    free(image);

    static const char *const errors[] = {
        [VERIFY_IMAGE] = "Invalid Intel HEX application image",
        [VERIFY_PEER] = "Controller peer I/O unavailable",
        [VERIFY_IDENTITY] = "Unexpected controller signature",
        [VERIFY_TIMEOUT] = "Controller reply timed out",
        [VERIFY_DISCONNECT] = "Controller disconnected",
        [VERIFY_TRUNCATED] = "Truncated controller reply",
        [VERIFY_PROTOCOL] = "Invalid STK500v2 controller reply"
    };
    if (result == VERIFY_MATCH)
        puts("ATmega2560 application image bytes verified by readback");
    else if (result != VERIFY_MISMATCH)
        fprintf(stderr, "%s\n", errors[result]);
    return result;
}
