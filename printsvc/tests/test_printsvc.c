/* SPDX-License-Identifier: MPL-2.0 */
#include "command.h"
#include "config.h"
#include "crc.h"
#include "flow_control.h"
#include "heater_wait.h"
#include "macro_registry.h"
#include "marlin_packet.h"
#include "motion_firmware.h"
#include "motion_policy.h"
#include "sha256.h"
#include "service.h"
#include "status.h"
#include "status_parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_command_parse(void)
{
    deneb_command_t cmd;

    assert(deneb_command_parse("JOB<{\"file\":\"/mnt/sda1/cube.gcode\",\"source\":\"USB\",\"uuid\":\"abc\"}", &cmd) == 0);
    assert(cmd.type == DENEB_COMMAND_JOB);
    assert(strcmp(cmd.file, "/mnt/sda1/cube.gcode") == 0);
    assert(strcmp(cmd.source, "USB") == 0);
    assert(strcmp(cmd.uuid, "abc") == 0);
    assert(deneb_command_parse("JOB<{\"file\":\"/tmp/a.gcode\",\"bedTset\":60,\"headTset\":210}", &cmd) == 0);
    assert(cmd.bed_target == 60.0f);
    assert(cmd.head_target == 210.0f);

    assert(deneb_command_parse("GCODE<[\"M105\",\"G28 X Y\"]", &cmd) == 0);
    assert(cmd.type == DENEB_COMMAND_GCODE);
    assert(cmd.gcode_count == 2);
    assert(strcmp(cmd.gcode[1], "G28 X Y") == 0);
}

static void test_status_frame(void)
{
    deneb_status_t status;
    char frame[1024];

    deneb_status_init(&status);
    assert(deneb_status_serialize_frame(&status, frame, sizeof(frame)) > 0);
    assert(strncmp(frame, "10001<", 6) == 0);
    assert(strstr(frame, "\"file\":\"none\"") != NULL);
}

static void test_crc_and_packet(void)
{
    uint8_t packet[256];
    size_t written = 0;

    assert(deneb_crc16_ccitt((const uint8_t *)"123456789", 9) == 0x29b1);
    assert(deneb_marlin_packet_encode(7, "M105", packet, sizeof(packet), &written) == 0);
    assert(written > 0);
    assert(strstr((const char *)packet, "N7 M105*") != NULL);
}

static void test_macro_safety(void)
{
    char path[256];

    assert(deneb_macro_resolve("init.gcode", path, sizeof(path)) == 0);
    assert(strstr(path, DENEB_PRINTSVC_MACRO_DIR) == path);
    assert(deneb_macro_resolve("../init.gcode", path, sizeof(path)) != 0);
    assert(deneb_macro_resolve("/tmp/init.gcode", path, sizeof(path)) != 0);
}

static void test_abort_clears_status(void)
{
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char reply[256];

    deneb_print_service_init(&svc);
    svc.status.state = DENEB_PRINT_STATE_PRINTING;
    snprintf(svc.status.file, sizeof(svc.status.file), "cube.gcode");
    assert(deneb_command_parse("ABORT<{}", &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);
    assert(strcmp(svc.status.file, "none") == 0);
}

static void test_status_parser(void)
{
    deneb_status_t status;

    deneb_status_init(&status);
    assert(deneb_status_parse_marlin_line(&status, "ok T:201.5 /210.0 B:59.8 /60.0") == DENEB_PARSE_TEMPERATURE);
    assert(status.head_t_cur > 201.0f && status.head_t_set > 209.0f);
    assert(status.bed_t_cur > 59.0f && status.bed_t_set > 59.0f);

    assert(deneb_status_parse_marlin_line(&status, "X:1.00 Y:2.00 Z:3.00 E:4.00 Count X:80 Y:160 Z:1200") == DENEB_PARSE_POSITION);
    assert(status.x == 1.0f && status.y == 2.0f && status.z == 3.0f && status.e == 4.0f);

    assert(deneb_status_parse_marlin_line(&status, "FIRMWARE_NAME:Marlin 1.1.0") == DENEB_PARSE_VERSION);
    assert(deneb_status_parse_marlin_line(&status, "Error:Printer halted") == DENEB_PARSE_FAULT);
    assert(status.state == DENEB_PRINT_STATE_ERROR);
}

static void test_flow_control(void)
{
    deneb_flow_control_t flow;
    uint8_t packet[256];
    uint8_t seq = 0;
    uint8_t resend = 0;
    size_t written = 0;

    deneb_flow_init(&flow);
    assert(deneb_flow_prepare_packet(&flow, "M105", packet, sizeof(packet), &written, &seq) == 0);
    assert(seq == 0);
    assert(deneb_flow_inflight(&flow) == 1);
    assert(deneb_flow_handle_response(&flow, "ok N0", &resend) == 1);
    assert(deneb_flow_inflight(&flow) == 0);

    assert(deneb_flow_prepare_packet(&flow, "G28 X Y", packet, sizeof(packet), &written, &seq) == 0);
    assert(seq == 1);
    assert(deneb_flow_handle_response(&flow, "Resend: 1", &resend) == 2);
    assert(resend == 1);
    assert(deneb_flow_get_resend_packet(&flow, resend, packet, sizeof(packet), &written) == 0);
    assert(strstr((const char *)packet, "N1 G28 X Y*") != NULL);
}

static void test_service_poll_motion_stub(void)
{
    deneb_print_service_t svc;

    deneb_print_service_init(&svc);
    assert(deneb_print_service_poll_motion(&svc) == 0);
}

static void test_heater_wait(void)
{
    deneb_heater_wait_t wait;
    deneb_status_t status;

    deneb_heater_wait_init(&wait);
    deneb_status_init(&status);
    deneb_heater_wait_start(&wait, 60.0f, 210.0f, 1.0f);
    deneb_heater_wait_apply_status(&wait, &status);
    assert(status.state == DENEB_PRINT_STATE_PREPARING);
    assert(status.bed_t_set == 60.0f);
    assert(status.head_t_set == 210.0f);
    status.bed_t_cur = 59.1f;
    status.head_t_cur = 209.2f;
    assert(deneb_heater_wait_ready(&wait, &status));
    status.head_t_cur = 200.0f;
    assert(!deneb_heater_wait_ready(&wait, &status));
}

static void test_sha256(void)
{
    deneb_sha256_t sha;
    uint8_t digest[32];
    char hex[65];

    deneb_sha256_init(&sha);
    deneb_sha256_update(&sha, (const uint8_t *)"abc", 3);
    deneb_sha256_final(&sha, digest);
    deneb_sha256_hex(digest, hex);
    assert(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
}

static void test_motion_policy(void)
{
    deneb_motion_policy_t policy;

    deneb_motion_policy_abort(&policy);
    assert(policy.count > 0);
    assert(!deneb_motion_policy_contains_xy_home(&policy));

    deneb_motion_policy_finish(&policy);
    assert(policy.count > 0);
    assert(!deneb_motion_policy_contains_xy_home(&policy));
}

static void test_motion_firmware_cache(void)
{
    const char *hex_path = "/tmp/deneb-motion-fw-test.hex";
    const char *cache_path = "/tmp/deneb-motion-fw-test.sha256";
    char hash[65];
    FILE *f = fopen(hex_path, "wb");
    assert(f != NULL);
    fputs(":00000001FF\n", f);
    fclose(f);

    assert(deneb_motion_firmware_hash_file(hex_path, hash) == 0);
    assert(strlen(hash) == 64);
    assert(deneb_motion_firmware_check_cache(hex_path, cache_path, hash) == DENEB_MOTION_FW_PROGRAM_REQUIRED);

    f = fopen(cache_path, "wb");
    assert(f != NULL);
    fprintf(f, "%s\n", hash);
    fclose(f);
    assert(deneb_motion_firmware_check_cache(hex_path, cache_path, hash) == DENEB_MOTION_FW_CACHE_MATCH);
    remove(hex_path);
    remove(cache_path);
}

int main(void)
{
    test_command_parse();
    test_status_frame();
    test_crc_and_packet();
    test_macro_safety();
    test_abort_clears_status();
    test_status_parser();
    test_flow_control();
    test_service_poll_motion_stub();
    test_heater_wait();
    test_sha256();
    test_motion_policy();
    test_motion_firmware_cache();
    puts("deneb-printsvc tests passed");
    return 0;
}
