/* SPDX-License-Identifier: MPL-2.0 */
#include "motion_firmware.h"

#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static const char valid_image[] =
    ":020000040003F7\r\n"    /* Extended linear address 0x30000. */
    ":01DFFF0079A8\r\n"      /* Last comparable byte. */
    ":020000040002F8\n"
    ":0100010056a8\n"        /* An odd byte above 128 KiB. */
    ":020000020100FB\n"      /* Extended segment address 0x1000. */
    ":01002000558A\n"
    ":020000040000FA\n"
    ":0101000042BC\n"
    ":0101FF00F00F\n"        /* Last byte in the same page. */
    ":0200040000FFFB\n"      /* Explicit zero and 0xff. */
    ":01000100AA54\n"
    ":00000001FF";           /* EOF with no final newline. */

typedef enum {
    PEER_UNUSED,
    PEER_MATCH,
    PEER_IDENTITY,
    PEER_TIMEOUT,
    PEER_DISCONNECT,
    PEER_TRUNCATED,
    PEER_CHECKSUM,
    PEER_MISMATCH
} peer_case_t;

static uint8_t checksum(const uint8_t *bytes, size_t len)
{
    uint8_t result = 0;
    for (size_t i = 0; i < len; ++i)
        result ^= bytes[i];
    return result;
}

static void expect_request(int fd, uint8_t sequence, const uint8_t *payload, size_t len)
{
    uint8_t frame[12];
    size_t frame_size = 6 + len;
    assert(frame_size <= sizeof(frame));
    for (size_t pos = 0; pos < frame_size;) {
        ssize_t n = read(fd, frame + pos, frame_size - pos);
        assert(n > 0);
        pos += (size_t)n;
    }
    assert(frame[0] == 0x1b && frame[1] == sequence &&
           frame[2] == 0 && frame[3] == len && frame[4] == 0x0e);
    assert(memcmp(frame + 5, payload, len) == 0);
    assert(checksum(frame, frame_size) == 0);
}

static void reply(int fd, uint8_t sequence, const uint8_t *data, size_t len,
                  peer_case_t scenario)
{
    uint8_t frame[6 + 256 + 3] = {
        0x1b, sequence, (uint8_t)(len >> 8), (uint8_t)len, 0x0e
    };
    assert(len + 6 <= sizeof(frame));
    memcpy(frame + 5, data, len);
    frame[5 + len] = checksum(frame, 5 + len);
    if (scenario == PEER_CHECKSUM)
        frame[5 + len] ^= 1;
    size_t send_size = scenario == PEER_TRUNCATED ? 6 : 6 + len;
    assert(write(fd, frame, send_size) == (ssize_t)send_size);
}

static void expect_flash_read(int fd, uint8_t sequence, uint32_t word,
                              const uint8_t *payload, size_t count)
{
    const uint8_t address[] = { 0x06, 0x80, (uint8_t)(word >> 16),
                                (uint8_t)(word >> 8), (uint8_t)word };
    expect_request(fd, sequence, address, sizeof(address));
    reply(fd, sequence, (uint8_t[]){ 0x06, 0 }, 2, PEER_MATCH);

    const uint8_t read_flash[] = { 0x14, (uint8_t)(count >> 8), (uint8_t)count, 0x20 };
    expect_request(fd, sequence + 1, read_flash, sizeof(read_flash));
    uint8_t response[256 + 3] = { 0x14, 0 };
    assert(count <= 256);
    memcpy(response + 2, payload, count);
    reply(fd, sequence + 1, response, count + 3, PEER_MATCH);
}

static void simulate_controller(int fd, peer_case_t scenario)
{
    if (scenario == PEER_UNUSED)
        return;
    const uint8_t identity[] = { 0x1e, 0x98, 0x01 };
    for (uint8_t index = 0; index < sizeof(identity); ++index) {
        uint8_t sequence = index + 1;
        expect_request(fd, sequence, (uint8_t[]){ 0x1b, 4, 0x30, 0, index, 0 }, 6);
        if (scenario == PEER_TIMEOUT)
            return;
        if (scenario != PEER_DISCONNECT) {
            uint8_t value = scenario == PEER_IDENTITY ? 0xff : identity[index];
            reply(fd, sequence, (uint8_t[]){ 0x1b, 0, value, 0 }, 4, scenario);
        }
        if (scenario == PEER_DISCONNECT || scenario == PEER_TRUNCATED)
            assert(shutdown(fd, SHUT_WR) == 0);
        if (scenario != PEER_MATCH && scenario != PEER_MISMATCH)
            return;
    }

    /* Include arbitrary bytes outside the records in the transported words. */
    uint8_t low[] = { 0x95, 0xaa, 0x11, 0x22, 0x00, 0xff };
    expect_flash_read(fd, 4, 0, low, sizeof(low));
    uint8_t page[256] = { [0] = 0x42, [255] = 0xf0 };
    expect_flash_read(fd, 6, 0x0080, page, sizeof(page));
    expect_flash_read(fd, 8, 0x0810, (uint8_t[]){ 0x55, 0x31 }, 2);
    expect_flash_read(fd, 10, 0x10000, (uint8_t[]){ 0x81, 0x56 }, 2);
    uint8_t last_byte = scenario == PEER_MISMATCH ? 0x7a : 0x79;
    expect_flash_read(fd, 12, 0x1efff, (uint8_t[]){ 0x0c, last_byte }, 2);
}

static void verify_case(const char *binary, const char *image_path,
                        peer_case_t scenario, const char *expected, int exit_code)
{
    int peer[2];
    int output[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, peer) == 0);
    assert(pipe(output) == 0);

    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(peer[0]);
        close(output[0]);
        if (dup2(peer[1], STDIN_FILENO) < 0 ||
            dup2(output[1], STDOUT_FILENO) < 0 ||
            dup2(output[1], STDERR_FILENO) < 0)
            _exit(126);
        if (peer[1] != STDIN_FILENO)
            close(peer[1]);
        close(output[1]);
        execl(binary, binary, image_path, (char *)NULL);
        _exit(127);
    }
    close(peer[1]);
    close(output[1]);

    simulate_controller(peer[0], scenario);
    char diagnostic[256];
    /* A different or unexpected extra command fails, including on bad HEX. */
    assert(read(peer[0], diagnostic, 1) == 0);
    assert(close(peer[0]) == 0);

    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    size_t got = 0;
    while (got < sizeof(diagnostic) - 1) {
        ssize_t n = read(output[0], diagnostic + got, sizeof(diagnostic) - 1 - got);
        assert(n >= 0);
        if (n == 0)
            break;
        got += (size_t)n;
    }
    diagnostic[got] = '\0';
    assert(close(output[0]) == 0);
    if (strcmp(diagnostic, expected) != 0 || WEXITSTATUS(status) != exit_code) {
        fprintf(stderr, "Wanted exit %d and %sGot exit %d and %s",
                exit_code, expected, WEXITSTATUS(status), diagnostic);
        abort();
    }
}

static void write_image(const char *path, const char *image, size_t size)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(image, 1, size, file) == size);
    assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp)
        tmp = "/tmp";
    char image_path[4096];
    int len = snprintf(image_path, sizeof(image_path), "%s/deneb-readback-XXXXXX", tmp);
    assert(len > 0 && (size_t)len < sizeof(image_path));
    int fd = mkstemp(image_path);
    assert(fd >= 0 && close(fd) == 0);

    static const char *const invalid_images[] = {
        ":01000100xA54\n:00000001FF\n",                         /* Non-hex. */
        ":01000100AA55\n:00000001FF\n",                         /* Checksum. */
        ":01000100AA54\n",                                      /* Missing EOF. */
        ":01000100AA54\n:00000001F",                             /* Partial EOF. */
        ":020000040003F7\n:01E0000079A6\n:00000001FF\n",      /* Bootloader. */
        ":02FFFF000001FF\n:00000001FF\n",                       /* Offset wrap. */
        ":01000100AA54\n:01000100AA54\n:00000001FF\n",         /* Duplicate. */
        ":01000100AA54\n:00000001FF\n:01000100AA54\n",         /* After EOF. */
        ":01000100AA54\n:0400000500000000F7\n:00000001FF\n",  /* Start address. */
        ":00000001FF\n"                                        /* No data. */
    };
    for (size_t i = 0; i < sizeof(invalid_images) / sizeof(invalid_images[0]); ++i) {
        write_image(image_path, invalid_images[i], strlen(invalid_images[i]));
        verify_case(argv[1], image_path, PEER_UNUSED, "Invalid Intel HEX application image\n", 3);
    }
    const char nul_image[] = ":01000100AA54\n:00000001FF\0";
    write_image(image_path, nul_image, sizeof(nul_image) - 1);
    verify_case(argv[1], image_path, PEER_UNUSED, "Invalid Intel HEX application image\n", 3);

    write_image(image_path, valid_image, sizeof(valid_image) - 1);
    char cache_path[sizeof(image_path) + sizeof(".sha256")];
    assert(snprintf(cache_path, sizeof(cache_path), "%s.sha256", image_path) > 0);
    char hash[65];
    assert(deneb_motion_firmware_hash_file(image_path, hash) == 0);
    write_image(cache_path, hash, strlen(hash));
    assert(deneb_motion_firmware_check_cache(image_path, cache_path, hash) ==
           DENEB_MOTION_FW_CACHE_MATCH);
    verify_case(argv[1], image_path, PEER_DISCONNECT, "Controller disconnected\n", 7);

    verify_case(argv[1], image_path, PEER_MATCH,
                "ATmega2560 application image bytes verified by readback\n", 0);
    verify_case(argv[1], image_path, PEER_IDENTITY, "Unexpected controller signature\n", 5);
    verify_case(argv[1], image_path, PEER_TIMEOUT, "Controller reply timed out\n", 6);
    verify_case(argv[1], image_path, PEER_TRUNCATED, "Truncated controller reply\n", 8);
    verify_case(argv[1], image_path, PEER_CHECKSUM, "Invalid STK500v2 controller reply\n", 9);
    verify_case(argv[1], image_path, PEER_MISMATCH, "Flash mismatch at byte 0x3dfff\n", 10);
    assert(unlink(cache_path) == 0);
    assert(unlink(image_path) == 0);
    return 0;
}
