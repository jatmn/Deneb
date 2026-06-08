/* SPDX-License-Identifier: MPL-2.0 */
#include "buildplate_level.h"
#include "command.h"
#include "command_audit.h"
#include "command_dispatch.h"
#include "command_format.h"
#include "command_reply.h"
#include "config.h"
#include "crc.h"
#include "diagnostics_log.h"
#include "error_map.h"
#include "flow_control.h"
#include "gcode_control.h"
#include "gcode_command.h"
#include "heater_wait.h"
#include "json_field.h"
#include "json_file.h"
#include "json_string.h"
#include "job_control.h"
#include "job_lifecycle.h"
#include "job_streamer.h"
#include "macro_control.h"
#include "macro_registry.h"
#include "macro_runner.h"
#include "manual_motion.h"
#include "material_catalog.h"
#include "material_workflow.h"
#include "marlin_packet.h"
#include "motion_firmware.h"
#include "motion_observer.h"
#include "motion_policy.h"
#include "motion_runtime.h"
#include "motion_sender.h"
#include "pause_resume.h"
#include "pause_resume_control.h"
#include "pending_job.h"
#include "pending_job_file.h"
#include "pending_job_registration.h"
#include "printer_identity.h"
#include "print_history.h"
#include "print_job_file.h"
#include "print_job_summary.h"
#include "print_macros.h"
#include "print_control.h"
#include "print_backend_route.h"
#include "print_profile.h"
#include "print_state_rules.h"
#include "runtime_diagnostics.h"
#include "sha256.h"
#include "service.h"
#include "service_command.h"
#include "service_context.h"
#include "status.h"
#include "status_payload.h"
#include "status_parser.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static void test_command_format_round_trip(void)
{
    deneb_command_t cmd;
    char frame[1024];
    char path[256];
    const char *gcodes[] = {"M105", "G1 X10 Y20"};

    assert(deneb_command_format_gcode(gcodes, 2, frame, sizeof(frame)) > 0);
    assert(strcmp(frame, "GCODE<[\"M105\",\"G1 X10 Y20\"]") == 0);
    assert(deneb_command_parse(frame, &cmd) == 0);
    assert(cmd.type == DENEB_COMMAND_GCODE);
    assert(cmd.gcode_count == 2);
    assert(strcmp(cmd.gcode[0], "M105") == 0);
    assert(strcmp(cmd.gcode[1], "G1 X10 Y20") == 0);

    assert(deneb_command_format_macro(DENEB_PRINT_MACRO_HOME_AND_CENTER_HEAD,
                                      frame, sizeof(frame)) > 0);
    assert(deneb_command_parse(frame, &cmd) == 0);
    assert(cmd.type == DENEB_COMMAND_MACRO);
    assert(strcmp(cmd.macro, DENEB_PRINT_MACRO_HOME_AND_CENTER_HEAD) == 0);

    assert(deneb_command_format_job("/home/3D/cube.gcode", "Cura", "uuid-1",
                                    60.0f, 210.0f, frame, sizeof(frame)) > 0);
    assert(deneb_command_parse(frame, &cmd) == 0);
    assert(cmd.type == DENEB_COMMAND_JOB);
    assert(strcmp(cmd.file, "/home/3D/cube.gcode") == 0);
    assert(strcmp(cmd.source, "Cura") == 0);
    assert(strcmp(cmd.uuid, "uuid-1") == 0);
    assert(cmd.bed_target == 60.0f);
    assert(cmd.head_target == 210.0f);
    assert(deneb_command_extract_job_path("{\"path\":\"/home/3D/path.gcode\"}",
                                          path, sizeof(path)) == 0);
    assert(strcmp(path, "/home/3D/path.gcode") == 0);
    assert(deneb_command_extract_job_path("{\"path\":\"none\",\"file\":\"/home/3D/file.gcode\"}",
                                          path, sizeof(path)) == 0);
    assert(strcmp(path, "/home/3D/file.gcode") == 0);
    assert(deneb_command_extract_job_path("{\"file\":\"none\"}",
                                          path, sizeof(path)) != 0);

    assert(deneb_command_format_action(DENEB_COMMAND_VERB_ABORT, frame, sizeof(frame)) > 0);
    assert(strcmp(frame, "ABORT<{}") == 0);
    assert(deneb_command_format_raw(DENEB_COMMAND_VERB_PAUSE, NULL,
                                    frame, sizeof(frame)) > 0);
    assert(strcmp(frame, "PAUSE<{}") == 0);
    assert(deneb_command_format_raw(DENEB_COMMAND_VERB_JOB,
                                    "{\"file\":\"cube.gcode\"}",
                                    frame, sizeof(frame)) > 0);
    assert(strcmp(frame, "JOB<{\"file\":\"cube.gcode\"}") == 0);
    assert(deneb_command_parse(frame, &cmd) == 0);
    assert(cmd.type == DENEB_COMMAND_JOB);
    assert(strcmp(cmd.file, "cube.gcode") == 0);
    assert(deneb_command_format_action(DENEB_COMMAND_VERB_ABORT, frame, sizeof(frame)) > 0);
    assert(deneb_command_parse(frame, &cmd) == 0);
    assert(cmd.type == DENEB_COMMAND_ABORT);
}

static void test_command_reply_helpers(void)
{
    char reply[256];

    assert(deneb_command_reply_ok(reply, sizeof(reply), "job accepted") == 0);
    assert(strcmp(reply, "{\"status\":\"ok\",\"message\":\"job accepted\"}") == 0);

    assert(deneb_command_reply_error(reply, sizeof(reply),
                                     "bad \"quoted\" path") == 0);
    assert(strcmp(reply,
                  "{\"status\":\"error\",\"message\":\"bad \\\"quoted\\\" path\"}") == 0);

    assert(deneb_command_reply_json(reply, 8, "ok", "too long") < 0);
}

static void test_command_dispatch_policy(void)
{
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char reply[256];

    deneb_print_service_init(&svc);
    assert(deneb_command_dispatch_handle(NULL, &cmd, reply, sizeof(reply)) < 0);

    assert(deneb_command_parse("GCODE<[\"M105\",\"G28 X Y\"]", &cmd) == 0);
    assert(deneb_command_dispatch_handle(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "\"status\":\"ok\"") != NULL);
    assert(deneb_flow_inflight(&svc.flow) == 2);

    assert(deneb_command_parse("PAUSE<{}", &cmd) == 0);
    assert(deneb_command_dispatch_handle(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "no active print to pause") != NULL);

    assert(deneb_command_parse("JOB<{}", &cmd) == 0);
    assert(deneb_command_dispatch_handle(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "missing job file") != NULL);
}

static void test_gcode_control_policy(void)
{
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char reply[256];

    deneb_print_service_init(&svc);
    assert(deneb_command_parse("GCODE<[\"M105\",\"G28 X Y\"]", &cmd) == 0);
    assert(deneb_gcode_control_run(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "\"status\":\"ok\"") != NULL);
    assert(deneb_flow_inflight(&svc.flow) == 2);

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = DENEB_COMMAND_GCODE;
    cmd.gcode_count = 1;
    assert(deneb_gcode_control_run(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "gcode failed") != NULL);
    assert(svc.status.error.code == DENEB_ERROR_COMMAND);
}

static void test_pause_resume_control_policy(void)
{
    deneb_print_service_t svc;
    char reply[256];

    deneb_print_service_init(&svc);
    assert(deneb_pause_resume_control_pause(&svc, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "no active print to pause") != NULL);

    svc.status.state = DENEB_PRINT_STATE_PRINTING;
    assert(deneb_pause_resume_control_pause(&svc, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "pause accepted") != NULL);
    assert(svc.status.state == DENEB_PRINT_STATE_PAUSED);

    svc.heater_wait.active = 1;
    assert(deneb_pause_resume_control_resume(&svc, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "resume accepted") != NULL);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);

    assert(deneb_pause_resume_control_resume(&svc, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "print is not paused") != NULL);
}

static int command_audit_fake_handler(void *ctx, const deneb_command_t *cmd,
                                      char *reply, size_t reply_sz)
{
    deneb_print_service_t *svc = (deneb_print_service_t *)ctx;

    assert(svc != NULL);
    assert(cmd != NULL);
    assert(reply != NULL);
    assert(reply_sz > 0);
    svc->job_active = 1;
    svc->job_stream.line_number = 77;
    snprintf(reply, reply_sz, "{\"status\":\"ok\",\"message\":\"audited\"}");
    return 0;
}

static void test_command_audit_policy(void)
{
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char reply[256];

    assert(deneb_command_audit_elapsed_ms(100, 90) == 0);
    assert(deneb_command_audit_elapsed_ms(100, 60150) == 60000);
    assert(deneb_command_audit_elapsed_ms(100, 250) == 150);

    deneb_print_service_init(&svc);
    assert(deneb_command_parse("PAUSE<{}", &cmd) == 0);
    assert(deneb_command_audit_run(NULL, &cmd, reply, sizeof(reply),
                                   command_audit_fake_handler, &svc) < 0);
    assert(deneb_command_audit_run(&svc, &cmd, reply, sizeof(reply),
                                   NULL, &svc) < 0);
    assert(deneb_command_audit_run(&svc, &cmd, reply, sizeof(reply),
                                   command_audit_fake_handler, &svc) == 0);
    assert(strstr(reply, "audited") != NULL);
    assert(svc.status.job_queue_depth == 1);
    assert(svc.status.job_line_number == 77);
}

static void test_status_frame(void)
{
    deneb_status_t status;
    char frame[1536];

    deneb_status_init(&status);
    status.flow_inflight = 2;
    status.flow_sent = 3;
    status.flow_ack = 1;
    status.flow_resend = 4;
    status.flow_reject = 5;
    status.job_queue_depth = 1;
    status.command_latency_ms = 9;
    status.planner_starvation_count = 7;
    snprintf(status.file, sizeof(status.file), "cube\"one.gcode");
    snprintf(status.source, sizeof(status.source), "USB\\front");
    status.error = deneb_error_make(DENEB_ERROR_THERMAL, "heater \"fault\"");
    assert(deneb_status_serialize_frame(&status, frame, sizeof(frame)) > 0);
    assert(strncmp(frame, "10001<", 6) == 0);
    assert(strstr(frame, "\"file\":\"cube\\\"one.gcode\"") != NULL);
    assert(strstr(frame, "\"source\":\"USB\\\\front\"") != NULL);
    assert(strstr(frame, "\"flowInflight\":2") != NULL);
    assert(strstr(frame, "\"flowSent\":3") != NULL);
    assert(strstr(frame, "\"flowAck\":1") != NULL);
    assert(strstr(frame, "\"flowResend\":4") != NULL);
    assert(strstr(frame, "\"flowReject\":5") != NULL);
    assert(strstr(frame, "\"jobQueueDepth\":1") != NULL);
    assert(strstr(frame, "\"commandLatencyMs\":9") != NULL);
    assert(strstr(frame, "\"plannerStarvationCount\":7") != NULL);
    assert(strstr(frame, "\"denebState\":\"idle\"") != NULL);
    assert(strstr(frame, "\"denebActive\":false") != NULL);
    assert(strstr(frame, "\"denebStopAllowed\":false") != NULL);
    assert(strstr(frame, "\"denebErrorKey\":\"thermal_fault\"") != NULL);
    assert(strstr(frame, "\"denebErrorCategory\":\"thermal\"") != NULL);
    assert(strstr(frame, "\"denebErrorDetail\":\"heater \\\"fault\\\"\"") != NULL);
}

static void test_error_mapping(void)
{
    deneb_error_t error;

    error = deneb_error_from_marlin_line("Error:Thermal runaway, system stopped");
    assert(error.code == DENEB_ERROR_THERMAL);
    assert(strcmp(error.key, "thermal_fault") == 0);
    assert(strcmp(error.category, "thermal") == 0);

    error = deneb_error_from_marlin_line("Error:Homing failed, endstop not hit");
    assert(error.code == DENEB_ERROR_ENDSTOP);
    assert(strcmp(error.category, "motion") == 0);

    error = deneb_error_from_marlin_line("Error:Line Number is not Last Line Number+1");
    assert(error.code == DENEB_ERROR_SERIAL);

    error = deneb_error_from_marlin_line("Error:Unknown command: M9999");
    assert(error.code == DENEB_ERROR_COMMAND);
}

static void test_print_control_contract(void)
{
    assert(deneb_print_control_phase_name(DENEB_PRINT_PHASE_PREPARING) != NULL);
    assert(strcmp(deneb_print_control_phase_name(DENEB_PRINT_PHASE_PREPARING), DENEB_PRINT_PHASE_NAME_PRE_PRINT) == 0);
    assert(strcmp(deneb_print_control_req_for_phase(DENEB_PRINT_PHASE_PREPARING), DENEB_PRINT_REQ_PREPARE) == 0);
    assert(strcmp(deneb_print_control_req_for_phase(DENEB_PRINT_PHASE_PRINTING), DENEB_COMMAND_VERB_JOB) == 0);
    assert(strcmp(deneb_print_control_action_command(DENEB_PRINT_ACTION_ABORT), DENEB_COMMAND_VERB_ABORT) == 0);
    assert(deneb_print_control_state_for_phase(DENEB_PRINT_PHASE_IDLE) == DENEB_PRINT_STATE_IDLE);
    assert(deneb_print_control_state_for_phase(DENEB_PRINT_PHASE_PREPARING) == DENEB_PRINT_STATE_PREPARING);
    assert(deneb_print_control_state_for_phase(DENEB_PRINT_PHASE_PRINTING) == DENEB_PRINT_STATE_PRINTING);
    assert(deneb_print_control_state_for_phase(DENEB_PRINT_PHASE_PAUSED) == DENEB_PRINT_STATE_PAUSED);
    assert(deneb_print_control_state_for_phase(DENEB_PRINT_PHASE_ABORTING) == DENEB_PRINT_STATE_ABORTING);
    assert(deneb_print_control_state_for_phase(DENEB_PRINT_PHASE_COMPLETE) == DENEB_PRINT_STATE_COMPLETE);
    assert(deneb_print_control_state_for_phase(DENEB_PRINT_PHASE_ERROR) == DENEB_PRINT_STATE_ERROR);
    assert(deneb_print_control_phase_active(DENEB_PRINT_PHASE_PREPARING));
    assert(deneb_print_control_phase_stop_allowed(DENEB_PRINT_PHASE_PREPARING));
    assert(deneb_print_control_phase_stop_allowed(DENEB_PRINT_PHASE_PRINTING));
    assert(!deneb_print_control_phase_stop_allowed(DENEB_PRINT_PHASE_IDLE));
    assert(!deneb_print_control_phase_stop_allowed(DENEB_PRINT_PHASE_COMPLETE));
}

static void test_pause_resume_policy(void)
{
    deneb_status_t status;

    deneb_status_init(&status);
    assert(deneb_pause_resume_pause(&status) < 0);
    assert(deneb_pause_resume_resume(&status, 0) < 0);

    status.state = DENEB_PRINT_STATE_PREPARING;
    snprintf(status.req, sizeof(status.req), "%s",
             deneb_print_control_req_for_phase(DENEB_PRINT_PHASE_PREPARING));
    assert(deneb_pause_resume_pause(&status) == 1);
    assert(status.state == DENEB_PRINT_STATE_PAUSED);
    assert(strcmp(status.req, DENEB_PRINT_REQ_PAUSED) == 0);
    assert(deneb_pause_resume_pause(&status) == 0);
    assert(deneb_pause_resume_resume(&status, 1) == 1);
    assert(status.state == DENEB_PRINT_STATE_PREPARING);
    assert(strcmp(status.req, DENEB_PRINT_REQ_PREPARE) == 0);

    status.state = DENEB_PRINT_STATE_PRINTING;
    snprintf(status.req, sizeof(status.req), "%s",
             deneb_print_control_req_for_phase(DENEB_PRINT_PHASE_PRINTING));
    assert(deneb_pause_resume_pause(&status) == 1);
    assert(status.state == DENEB_PRINT_STATE_PAUSED);
    assert(deneb_pause_resume_resume(&status, 0) == 1);
    assert(status.state == DENEB_PRINT_STATE_PRINTING);
    assert(strcmp(status.req, DENEB_COMMAND_VERB_JOB) == 0);

    status.state = DENEB_PRINT_STATE_COMPLETE;
    assert(deneb_pause_resume_pause(&status) < 0);
    status.state = DENEB_PRINT_STATE_ERROR;
    assert(deneb_pause_resume_pause(&status) < 0);
}

static void test_job_lifecycle_policy(void)
{
    deneb_status_t status;

    deneb_status_init(&status);
    deneb_job_lifecycle_start(&status, "/tmp/cube.gcode", "", "uuid-1",
                              60.0f, 205.0f);
    assert(status.state == DENEB_PRINT_STATE_PREPARING);
    assert(strcmp(status.req, DENEB_PRINT_REQ_PREPARE) == 0);
    assert(strcmp(status.file, "/tmp/cube.gcode") == 0);
    assert(strcmp(status.source, DENEB_PRINT_USB_JOB_SOURCE) == 0);
    assert(strcmp(status.uuid, "uuid-1") == 0);
    assert(status.bed_t_set == 60.0f);
    assert(status.head_t_set == 205.0f);

    deneb_job_lifecycle_streaming(&status);
    assert(status.state == DENEB_PRINT_STATE_PRINTING);
    assert(strcmp(status.req, DENEB_COMMAND_VERB_JOB) == 0);

    deneb_job_lifecycle_complete(&status);
    assert(status.state == DENEB_PRINT_STATE_COMPLETE);
    assert(strcmp(status.req, DENEB_PRINT_REQ_COMPLETE) == 0);

    deneb_job_lifecycle_start(&status, "/tmp/cube.gcode", "WEB_API", "",
                              0.0f, 0.0f);
    status.time_total = 120;
    status.time_left = 90;
    deneb_job_lifecycle_abort(&status);
    assert(status.state == DENEB_PRINT_STATE_IDLE);
    assert(strcmp(status.req, DENEB_PRINT_REQ_IDLE) == 0);
    assert(strcmp(status.file, DENEB_PRINT_NONE_VALUE) == 0);
    assert(status.time_total == 0);
    assert(status.time_left == 0);

    deneb_job_lifecycle_error(&status,
                              deneb_error_make(DENEB_ERROR_STORAGE,
                                               "read failed"));
    assert(status.state == DENEB_PRINT_STATE_ERROR);
    assert(status.fault);
    assert(status.error.code == DENEB_ERROR_STORAGE);
}

static void test_job_control_policy(void)
{
    const char *path = "/tmp/deneb-printsvc-job-control-test.gcode";
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char frame[512];
    char reply[256];
    FILE *f = fopen(path, "wb");

    assert(f != NULL);
    fputs("G1 X1\n", f);
    fclose(f);

    deneb_print_service_init(&svc);
    assert(deneb_command_parse("JOB<{}", &cmd) == 0);
    assert(deneb_job_control_accept(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "missing job file") != NULL);

    snprintf(frame, sizeof(frame),
             "JOB<{\"file\":\"%s\",\"source\":\"Cura\",\"uuid\":\"job-1\","
             "\"bedTset\":60,\"headTset\":200}",
             path);
    assert(deneb_command_parse(frame, &cmd) == 0);
    assert(deneb_job_control_accept(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "job accepted") != NULL);
    assert(svc.job_active);
    assert(!svc.abort_requested);
    assert(svc.heater_wait.active);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);
    assert(strcmp(svc.status.file, path) == 0);
    assert(strcmp(svc.status.source, "Cura") == 0);
    assert(strcmp(svc.status.uuid, "job-1") == 0);

    assert(deneb_job_control_accept(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "job already active") != NULL);

    assert(deneb_job_control_abort(&svc, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "abort accepted") != NULL);
    assert(!svc.job_active);
    assert(svc.abort_requested);
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);
    assert(strcmp(svc.status.file, DENEB_PRINT_NONE_VALUE) == 0);

    remove(path);
}

static void test_job_streamer_policy(void)
{
    const char *path = "/tmp/deneb-printsvc-job-streamer-test.gcode";
    deneb_status_t status;
    deneb_flow_control_t flow;
    deneb_gcode_stream_t stream;
    deneb_heater_wait_t wait;
    deneb_serial_transport_t serial;
    deneb_job_streamer_t streamer;
    int job_active = 1;
    int abort_requested = 0;
    unsigned int starvation = 0;
    FILE *f = fopen(path, "wb");

    assert(f != NULL);
    fputs("G1 X1\n", f);
    fclose(f);

    deneb_status_init(&status);
    deneb_flow_init(&flow);
    deneb_heater_wait_init(&wait);
    memset(&serial, 0, sizeof(serial));
    serial.fd = -1;

    deneb_job_lifecycle_start(&status, path, DENEB_PRINT_USB_JOB_SOURCE,
                              "streamer-uuid", 60.0f, 200.0f);
    deneb_heater_wait_start(&wait, status.bed_t_set, status.head_t_set, 1.0f);
    assert(deneb_gcode_stream_open(&stream, path) == 0);

    streamer.status = &status;
    streamer.flow = &flow;
    streamer.stream = &stream;
    streamer.heater_wait = &wait;
    streamer.serial = &serial;
    streamer.serial_ready = 0;
    streamer.job_active = &job_active;
    streamer.abort_requested = &abort_requested;
    streamer.planner_starvation_count = &starvation;

    assert(deneb_job_streamer_poll(NULL) < 0);
    assert(deneb_job_streamer_poll(&streamer) == 0);
    assert(status.state == DENEB_PRINT_STATE_PREPARING);
    assert(stream.line_number == 0);

    status.bed_t_cur = 60.0f;
    status.head_t_cur = 200.0f;
    assert(deneb_job_streamer_poll(&streamer) == 1);
    assert(status.state == DENEB_PRINT_STATE_PRINTING);
    assert(stream.line_number == 1);
    assert(starvation == 1);
    assert(deneb_flow_inflight(&flow) == 1);

    assert(deneb_job_streamer_poll(&streamer) == 1);
    assert(!job_active);
    assert(status.state == DENEB_PRINT_STATE_COMPLETE);

    deneb_flow_init(&flow);
    assert(deneb_gcode_stream_open(&stream, path) == 0);
    job_active = 1;
    abort_requested = 1;
    assert(deneb_job_streamer_poll(&streamer) == -2);
    assert(!job_active);

    remove(path);
}

static void test_runtime_diagnostics_policy(void)
{
    deneb_status_t status;
    deneb_flow_control_t flow;
    uint8_t packet[DENEB_PRINTSVC_MAX_GCODE_LINE + 64];
    size_t written = 0;
    uint8_t seq = 0;
    uint8_t resend = 0;

    deneb_status_init(&status);
    deneb_flow_init(&flow);
    assert(deneb_flow_prepare_packet(&flow, "M105", packet, sizeof(packet),
                                     &written, &seq) == 0);
    assert(deneb_flow_handle_response(&flow, "Resend: 0", &resend) == 2);
    assert(deneb_flow_handle_response(&flow, "Error:Bad checksum", &resend) == -1);

    deneb_runtime_diagnostics_refresh(&status, &flow, 1, 42, 3);
    assert(status.flow_inflight == 1);
    assert(status.flow_sent == 1);
    assert(status.flow_ack == 0);
    assert(status.flow_resend == 1);
    assert(status.flow_reject == 1);
    assert(status.job_queue_depth == 1);
    assert(status.job_line_number == 42);
    assert(status.planner_starvation_count == 3);

    deneb_runtime_diagnostics_refresh(&status, &flow, 0, 42, 4);
    assert(status.job_queue_depth == 0);
    assert(status.job_line_number == 0);
    assert(status.planner_starvation_count == 4);
}

static void test_motion_sender_policy(void)
{
    deneb_flow_control_t flow;
    deneb_serial_transport_t serial;
    deneb_motion_policy_t policy;

    deneb_flow_init(&flow);
    memset(&serial, 0, sizeof(serial));
    serial.fd = -1;

    assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, NULL) < 0);
    assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, "") < 0);
    assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, "M105") == 0);
    assert(deneb_flow_inflight(&flow) == 1);
    assert(flow.sent_count == 1);
    assert(deneb_motion_sender_resend_sequence(&flow, &serial, 0, 0) == 0);
    assert(deneb_motion_sender_resend_sequence(&flow, &serial, 0, 7) < 0);

    deneb_flow_init(&flow);
    deneb_motion_policy_abort(&policy);
    assert(deneb_motion_sender_apply_policy(&flow, &serial, 0, &policy) == 0);
    assert(flow.sent_count == policy.count);
    assert(deneb_flow_inflight(&flow) == policy.count);
    assert(deneb_motion_sender_apply_policy(&flow, &serial, 0, NULL) < 0);
}

typedef struct {
    int abort_after_sends;
    int fail_wait;
    int fail_send;
    int sends;
    int polls;
    char sent[3][DENEB_PRINTSVC_MAX_GCODE_LINE];
} macro_runner_fake_t;

static int macro_runner_fake_abort(void *ctx)
{
    macro_runner_fake_t *fake = (macro_runner_fake_t *)ctx;
    return fake->abort_after_sends >= 0 &&
           fake->sends >= fake->abort_after_sends;
}

static int macro_runner_fake_wait(void *ctx, long long timeout_ms)
{
    macro_runner_fake_t *fake = (macro_runner_fake_t *)ctx;
    assert(timeout_ms == 5000);
    return fake->fail_wait ? -1 : 0;
}

static int macro_runner_fake_send(void *ctx, const char *line)
{
    macro_runner_fake_t *fake = (macro_runner_fake_t *)ctx;
    if (fake->fail_send)
        return -1;
    if (fake->sends < 3)
        snprintf(fake->sent[fake->sends], sizeof(fake->sent[fake->sends]),
                 "%s", line);
    fake->sends++;
    return 0;
}

static int macro_runner_fake_poll(void *ctx)
{
    macro_runner_fake_t *fake = (macro_runner_fake_t *)ctx;
    fake->polls++;
    return 0;
}

static void test_motion_observer_policy(void)
{
    deneb_status_t status;
    deneb_heater_wait_t wait;
    deneb_flow_control_t flow;
    uint8_t packet[DENEB_PRINTSVC_MAX_GCODE_LINE + 64];
    size_t written = 0;
    uint8_t seq = 0;
    uint8_t resend = 0;

    deneb_status_init(&status);
    deneb_heater_wait_init(&wait);
    deneb_flow_init(&flow);
    deneb_heater_wait_start(&wait, 60.0f, 200.0f, 1.0f);

    assert(deneb_motion_observer_handle_line(NULL, &wait, &flow, "ok",
                                             &resend) < 0);
    assert(deneb_motion_observer_handle_line(&status, &wait, &flow,
                                             "ok T:150.0 /200.0 B:55.0 /60.0",
                                             &resend) == 1);
    assert(wait.active);
    assert(status.head_t_cur == 150.0f);
    assert(status.head_t_set == 200.0f);
    assert(status.bed_t_cur == 55.0f);
    assert(status.bed_t_set == 60.0f);

    assert(deneb_motion_observer_handle_line(&status, &wait, &flow,
                                             "ok T:200.0 /200.0 B:60.0 /60.0",
                                             &resend) == 1);
    assert(!wait.active);

    assert(deneb_flow_prepare_packet(&flow, "M105", packet, sizeof(packet),
                                     &written, &seq) == 0);
    assert(deneb_motion_observer_handle_line(&status, &wait, &flow,
                                             "Resend: 0", &resend) == 2);
    assert(resend == 0);
}

static void test_motion_runtime_policy(void)
{
    deneb_status_t status;
    deneb_heater_wait_t wait;
    deneb_flow_control_t flow;
    deneb_serial_transport_t serial;
    deneb_motion_runtime_t runtime;
    int serial_ready = 0;

    deneb_status_init(&status);
    deneb_heater_wait_init(&wait);
    deneb_flow_init(&flow);
    memset(&serial, 0, sizeof(serial));
    serial.fd = -1;

    runtime.status = &status;
    runtime.heater_wait = &wait;
    runtime.flow = &flow;
    runtime.serial = &serial;
    runtime.serial_ready = &serial_ready;

    assert(deneb_motion_runtime_poll(NULL) < 0);
    assert(deneb_motion_runtime_poll(&runtime) == 0);
    serial_ready = 1;
    deneb_motion_runtime_close(&runtime);
    assert(serial_ready == 0);
}

static void test_macro_runner_policy(void)
{
    const char *path = "/tmp/deneb-printsvc-macro-runner-test.gcode";
    deneb_macro_runner_io_t io;
    macro_runner_fake_t fake;
    FILE *f = fopen(path, "wb");

    assert(f != NULL);
    fputs(";comment\nG1 X1\n\nG1 X2\n", f);
    fclose(f);

    memset(&fake, 0, sizeof(fake));
    fake.abort_after_sends = -1;
    io.ctx = &fake;
    io.abort_requested = macro_runner_fake_abort;
    io.wait_for_window = macro_runner_fake_wait;
    io.send_gcode = macro_runner_fake_send;
    io.poll_motion = macro_runner_fake_poll;
    assert(deneb_macro_runner_run_file(path, &io) == 0);
    assert(fake.sends == 2);
    assert(fake.polls == 2);
    assert(strcmp(fake.sent[0], "G1 X1") == 0);
    assert(strcmp(fake.sent[1], "G1 X2") == 0);

    memset(&fake, 0, sizeof(fake));
    fake.abort_after_sends = 1;
    io.ctx = &fake;
    assert(deneb_macro_runner_run_file(path, &io) == -2);
    assert(fake.sends == 1);

    memset(&fake, 0, sizeof(fake));
    fake.abort_after_sends = -1;
    fake.fail_wait = 1;
    io.ctx = &fake;
    assert(deneb_macro_runner_run_file(path, &io) < 0);
    assert(fake.sends == 0);

    memset(&fake, 0, sizeof(fake));
    fake.abort_after_sends = -1;
    fake.fail_send = 1;
    io.ctx = &fake;
    assert(deneb_macro_runner_run_file(path, &io) < 0);
    assert(deneb_macro_runner_run_file(path, NULL) < 0);
    assert(deneb_macro_runner_run_macro("../bad.gcode", &io) < 0);
    remove(path);
}

static void test_macro_control_policy(void)
{
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char reply[256];

    deneb_print_service_init(&svc);
    memset(&cmd, 0, sizeof(cmd));
    snprintf(cmd.macro, sizeof(cmd.macro), "../bad.gcode");
    assert(deneb_macro_control_run(NULL, &cmd, reply, sizeof(reply)) < 0);
    assert(deneb_macro_control_run(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "macro failed") != NULL);
    assert(svc.status.error.code == DENEB_ERROR_COMMAND);

    snprintf(cmd.macro, sizeof(cmd.macro), "missing_macro.gcode");
    assert(deneb_macro_control_run(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "macro failed") != NULL);
}

static void test_print_state_rules(void)
{
    deneb_print_observation_t obs = {0};
    deneb_print_stop_guard_t stop_guard;
    deneb_print_action_plan_t action_plan;

    assert(deneb_print_req_is_print(DENEB_COMMAND_VERB_JOB));
    assert(deneb_print_req_is_print(DENEB_PRINT_REQ_PRINTING));
    assert(deneb_print_req_is_paused(DENEB_PRINT_REQ_PAUSED));
    assert(deneb_print_req_is_lifecycle(DENEB_PRINT_REQ_PREHEATING));
    assert(deneb_print_req_is_abort("BUSY_ABORTING"));
    assert(deneb_print_file_is_candidate("/home/3D/cube.gcode"));
    assert(deneb_print_file_is_candidate("/home/3D/job.ufp"));
    assert(deneb_print_file_is_candidate("LOCAL_JOB.GCODE"));
    assert(!deneb_print_file_is_candidate("/home/cygnus/marlindriver/gcode/"
                                          DENEB_PRINT_MACRO_HOME_AND_CENTER_HEAD));
    assert(!deneb_print_file_is_candidate("readme.txt"));
    assert(deneb_print_file_is_transient(DENEB_PRINT_MACRO_MOVE_BUILDPLATE_UP));
    assert(deneb_print_active_time(120, 60));
    assert(!deneb_print_active_time(120, 0));
    assert(deneb_print_temp_target_ready(0.0f, 0.0f, 1.0f));
    assert(deneb_print_temp_target_ready(198.2f, 200.0f, 2.0f));
    assert(!deneb_print_temp_target_ready(197.0f, 200.0f, 2.0f));
    assert(deneb_print_temp_targets_ready(59.2f, 60.0f, 199.3f, 200.0f));
    assert(deneb_print_temp_targets_ready(0.0f, 0.0f, 199.3f, 200.0f));
    assert(deneb_print_temp_targets_ready(59.2f, 60.0f, 0.0f, 0.0f));
    assert(!deneb_print_temp_targets_ready(0.0f, 0.0f, 0.0f, 0.0f));
    assert(!deneb_print_temp_targets_ready(59.2f, 60.0f, 190.0f, 200.0f));
    assert(DENEB_PRINT_MATERIAL_MIN_MOVE_TEMP_C == 170.0f);
    assert(DENEB_PRINT_MATERIAL_READY_TOLERANCE_C == 2.0f);
    assert(!deneb_print_material_move_ready(210.0f, 160.0f));
    assert(!deneb_print_material_move_ready(207.0f, 210.0f));
    assert(deneb_print_material_move_ready(208.0f, 210.0f));

    deneb_print_observation_init(&obs, DENEB_PRINT_REQ_PREHEATING, NULL,
                                 0, 0, 60.0f, 210.0f);
    assert(obs.req == DENEB_PRINT_REQ_PREHEATING);
    assert(obs.bed_target == 60.0f);
    assert(obs.nozzle_target == 210.0f);
    assert(deneb_print_observation_has_context(&obs));
    assert(!deneb_print_has_stoppable_context(&obs, 0, 0, 0));
    assert(deneb_print_has_preparing_context(&obs, 1));
    assert(deneb_print_has_stoppable_context(&obs, 0, 0, 1));

    deneb_print_observation_init(&obs, "HOME", NULL, 0, 0, 0.0f, 0.0f);
    assert(!deneb_print_observation_has_context(&obs));
    assert(!deneb_print_has_stoppable_context(&obs, 0, 0, 0));
    assert(deneb_print_has_stoppable_context(&obs, 0, 0, 1));

    deneb_print_observation_init(&obs, "", NULL, 0, 0, 0.0f, 0.0f);
    obs.time_left = 0;
    assert(!deneb_print_has_active_context(&obs, 0, 0, 0));
    assert(deneb_print_has_active_context(&obs, 0, 0, 1));
    assert(!deneb_print_has_preparing_context(&obs, 0));

    obs.req = DENEB_COMMAND_VERB_ABORT;
    obs.file = "/home/3D/cube.gcode";
    obs.time_total = 120;
    obs.time_left = 100;
    assert(!deneb_print_observation_has_context(&obs));
    assert(!deneb_print_has_active_context(&obs, 1, 1, 1));
    assert(!deneb_print_has_preparing_context(&obs, 1));
    assert(!deneb_print_has_stoppable_context(&obs, 1, 1, 1));

    obs.req = DENEB_COMMAND_VERB_JOB;
    obs.time_total = 120;
    obs.time_left = 80;
    assert(deneb_print_has_stoppable_context(&obs, 1, 0, 1));
    assert(deneb_print_has_stoppable_context(&obs, 0, 1, 0));

    assert(strcmp(deneb_print_status_label(0, 0, 0, 0), "offline") == 0);
    assert(strcmp(deneb_print_status_label(1, 0, 0, 0), "idle") == 0);
    assert(strcmp(deneb_print_status_label(1, 0, 0, 1), "printing") == 0);
    assert(strcmp(deneb_print_status_label(1, 0, 1, 1), "paused") == 0);
    assert(strcmp(deneb_print_status_label(1, 1, 1, 1), "error") == 0);
    assert(strcmp(deneb_print_job_status_label(0, 0, 0), "finished") == 0);
    assert(strcmp(deneb_print_job_status_label(0, 1, 1), "paused") == 0);
    assert(strcmp(deneb_print_job_status_label(1, 1, 1), "error") == 0);
    assert(strcmp(deneb_print_job_state_or_none(0, 0, 0),
                  DENEB_PRINT_NONE_VALUE) == 0);
    assert(strcmp(deneb_print_job_state_or_none(0, 0, 1), "printing") == 0);
    assert(strcmp(deneb_print_job_state_or_none(0, 1, 1), "paused") == 0);
    assert(strcmp(deneb_print_job_state_or_none(1, 1, 1), "error") == 0);
    assert(strcmp(deneb_print_completion_state_label(0, 120, 0), "completed") == 0);
    assert(strcmp(deneb_print_completion_state_label(0, 120, -1), "completed") == 0);
    assert(strcmp(deneb_print_completion_state_label(0, 120, 30), "stopped") == 0);
    assert(strcmp(deneb_print_completion_state_label(1, 120, 0), "error") == 0);
    assert(strcmp(deneb_print_completion_state_label(0, 0, 0), "stopped") == 0);
    {
        int total = 120;
        int left = 180;
        float progress = 99.0f;
        deneb_print_normalize_timing(1, 0, &total, &left, &progress);
        assert(total == 120);
        assert(left == 120);
        assert(progress == 0.0f);

        left = 60;
        deneb_print_normalize_timing(1, 0, &total, &left, &progress);
        assert(progress == 50.0f);

        deneb_print_normalize_timing(0, 1, &total, &left, &progress);
        assert(total == 120);
        assert(left == 60);
        assert(progress == 50.0f);

        deneb_print_normalize_timing(0, 0, &total, &left, &progress);
        assert(total == 0);
        assert(left == 0);
        assert(progress == 0.0f);
    }
    assert(strcmp(deneb_print_job_name_or_default(""), DENEB_PRINT_DEFAULT_JOB_NAME) == 0);
    assert(strcmp(deneb_print_job_name_or_default(DENEB_PRINT_NONE_VALUE),
                  DENEB_PRINT_DEFAULT_JOB_NAME) == 0);
    assert(strcmp(deneb_print_job_name_or_default("cube.gcode"), "cube.gcode") == 0);
    assert(strcmp(deneb_print_job_uuid_or_default(""), DENEB_PRINT_DEFAULT_JOB_UUID) == 0);
    assert(strcmp(deneb_print_job_uuid_or_default("job-1"), "job-1") == 0);
    assert(strcmp(deneb_print_job_source_or_default(""), DENEB_PRINT_DEFAULT_JOB_SOURCE) == 0);
    assert(strcmp(deneb_print_job_source_or_default("USB"), "USB") == 0);
    assert(strcmp(DENEB_PRINT_USB_JOB_SOURCE, "USB") == 0);
    {
        deneb_print_job_start_plan_t plan;

        deneb_print_job_start_plan_init(&plan);
        assert(plan.path == NULL);
        assert(strcmp(plan.source, DENEB_PRINT_DEFAULT_JOB_SOURCE) == 0);
        assert(strcmp(plan.uuid, DENEB_PRINT_DEFAULT_JOB_UUID) == 0);
        assert(plan.bed_target == 0.0f);
        assert(plan.nozzle_target == 0.0f);

        assert(deneb_print_job_start_plan_file(
                   "/home/3D/cube.gcode", DENEB_PRINT_USB_JOB_SOURCE,
                   &plan) == 0);
        assert(strcmp(plan.path, "/home/3D/cube.gcode") == 0);
        assert(strcmp(plan.source, DENEB_PRINT_USB_JOB_SOURCE) == 0);
        assert(strcmp(plan.uuid, DENEB_PRINT_DEFAULT_JOB_UUID) == 0);
        assert(plan.bed_target == 0.0f);
        assert(plan.nozzle_target == 0.0f);

        assert(deneb_print_job_start_plan_file(
                   "/home/3D/cura.gcode", "", &plan) == 0);
        assert(strcmp(plan.source, DENEB_PRINT_DEFAULT_JOB_SOURCE) == 0);
        assert(deneb_print_job_start_plan_prepare(
                   "/home/3D/pending.gcode", DENEB_PRINT_WEB_API_JOB_SOURCE,
                   "pending-uuid", 55.0f, 205.0f, &plan) == 0);
        assert(strcmp(plan.path, "/home/3D/pending.gcode") == 0);
        assert(strcmp(plan.source, DENEB_PRINT_WEB_API_JOB_SOURCE) == 0);
        assert(strcmp(plan.uuid, "pending-uuid") == 0);
        assert(plan.bed_target == 55.0f);
        assert(plan.nozzle_target == 205.0f);
        assert(deneb_print_job_start_plan_file("", DENEB_PRINT_USB_JOB_SOURCE,
                                               &plan) < 0);
    }
    {
        char action[16];
        assert(deneb_print_action_parse("\"Pause\"", action, sizeof(action)) == 0);
        assert(strcmp(action, DENEB_PRINT_ACTION_PAUSE_TEXT) == 0);
        assert(deneb_print_action_is_pause(action));
        assert(deneb_print_action_parse("{\"action\":\"Force\"}", action, sizeof(action)) == 0);
        assert(strcmp(action, DENEB_PRINT_ACTION_FORCE_TEXT) == 0);
        assert(deneb_print_action_is_resume_or_start(action));
        assert(deneb_print_action_is_force(action));
        assert(deneb_print_action_parse(" cancel ", action, sizeof(action)) == 0);
        assert(strcmp(action, DENEB_PRINT_ACTION_CANCEL_TEXT) == 0);
        assert(deneb_print_action_is_abort(action));
        assert(deneb_print_action_parse("{\"action\":\"stop\"}", action, sizeof(action)) == 0);
        assert(strcmp(action, DENEB_PRINT_ACTION_STOP_TEXT) == 0);
        assert(deneb_print_action_is_stop(action));
        assert(deneb_print_action_is_resume_or_start(DENEB_PRINT_ACTION_PRINT_TEXT));
        assert(deneb_print_action_is_resume_or_start(DENEB_PRINT_ACTION_RESUME_TEXT));
        assert(deneb_print_action_is_resume_or_start(DENEB_PRINT_ACTION_CONTINUE_TEXT));
        assert(deneb_print_action_is_resume_or_start(DENEB_PRINT_ACTION_START_TEXT));
        assert(deneb_print_action_is_abort(DENEB_PRINT_ACTION_ABORT_TEXT));
        assert(deneb_print_action_plan(DENEB_PRINT_ACTION_PAUSE_TEXT,
                                       &action_plan) == 0);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_PAUSE);
        assert(strcmp(action_plan.command, DENEB_COMMAND_VERB_PAUSE) == 0);
        assert(!action_plan.clear_pending_after_success);
        assert(deneb_print_action_plan(DENEB_PRINT_ACTION_CONTINUE_TEXT,
                                       &action_plan) == 0);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_RESUME);
        assert(strcmp(action_plan.command, DENEB_COMMAND_VERB_RESUME) == 0);
        assert(deneb_print_action_plan(DENEB_PRINT_ACTION_CANCEL_TEXT,
                                       &action_plan) == 0);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_ABORT);
        assert(strcmp(action_plan.command, DENEB_COMMAND_VERB_ABORT) == 0);
        assert(action_plan.clear_pending_after_success);
        assert(deneb_print_action_plan(DENEB_PRINT_ACTION_STOP_TEXT,
                                       &action_plan) == 0);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_STOP);
        assert(strcmp(action_plan.command, DENEB_COMMAND_VERB_ABORT) == 0);
        assert(action_plan.clear_pending_after_success);
        assert(deneb_print_action_plan("bogus", &action_plan) != 0);
        assert(deneb_print_action_parse("{}", action, sizeof(action)) != 0);
    }
    assert(deneb_print_job_is_active(1, 0, 0));
    assert(deneb_print_job_is_active(0, 1, 0));
    assert(deneb_print_job_is_active(0, 0, 1));
    assert(!deneb_print_job_is_active(0, 0, 0));
    assert(deneb_print_manual_action_allowed(1, 0, 0, 0));
    assert(!deneb_print_manual_action_allowed(0, 0, 0, 0));
    assert(!deneb_print_manual_action_allowed(1, 1, 0, 0));
    assert(!deneb_print_manual_action_allowed(1, 0, 1, 0));
    assert(!deneb_print_manual_action_allowed(1, 0, 0, 1));
    assert(deneb_print_start_allowed(1, 0, 0, 0));
    assert(!deneb_print_start_allowed(0, 0, 0, 0));
    assert(!deneb_print_start_allowed(1, 1, 0, 0));
    assert(!deneb_print_start_allowed(1, 0, 1, 0));
    assert(!deneb_print_start_allowed(1, 0, 0, 1));
    assert(deneb_print_display_state(0, 0, 0, 0, 0, 0, 0) ==
           DENEB_PRINT_DISPLAY_STATE_PREPARING);
    assert(deneb_print_display_state(1, 1, 0, 0, 0, 0, 0) ==
           DENEB_PRINT_DISPLAY_STATE_ERROR);
    assert(deneb_print_display_state(1, 0, 0, 0, 1, 0, 0) ==
           DENEB_PRINT_DISPLAY_STATE_COOLING);
    assert(deneb_print_display_state(1, 0, 1, 1, 0, 0, 0) ==
           DENEB_PRINT_DISPLAY_STATE_PAUSED);
    assert(deneb_print_display_state(1, 0, 0, 0, 0, 1, 0) ==
           DENEB_PRINT_DISPLAY_STATE_PREPARING);
    assert(deneb_print_display_state(1, 0, 0, 1, 0, 1, 120) ==
           DENEB_PRINT_DISPLAY_STATE_PRINTING);
    assert(deneb_print_display_state(1, 0, 0, 0, 0, 0, 0) ==
           DENEB_PRINT_DISPLAY_STATE_IDLE);
    assert(deneb_print_elapsed_seconds(120, 60) == 60);
    assert(deneb_print_elapsed_seconds(120, 0) == 120);
    assert(deneb_print_elapsed_seconds(120, -1) == 120);
    assert(deneb_print_elapsed_seconds(120, 121) == 0);
    assert(deneb_print_elapsed_seconds(0, 10) == 0);
    assert(deneb_print_progress_percent(120, 60) > 49.9f);
    assert(deneb_print_progress_percent(120, 60) < 50.1f);
    assert(deneb_print_progress_percent(120, 0) == 100.0f);
    assert(deneb_print_progress_percent(120, -1) == 100.0f);
    assert(deneb_print_progress_percent(120, 121) == 0.0f);
    assert(deneb_print_progress_percent(0, 10) == 0.0f);
    assert(deneb_print_progress_fraction(50.0f) > 0.49f);
    assert(deneb_print_progress_fraction(50.0f) < 0.51f);
    assert(deneb_print_progress_fraction(-1.0f) == 0.0f);
    assert(deneb_print_progress_fraction(101.0f) == 1.0f);

    deneb_print_stop_guard_init(&stop_guard, 3000);
    assert(deneb_print_stop_guard_begin(&stop_guard, 1000));
    assert(!deneb_print_stop_guard_begin(&stop_guard, 1500));
    assert(deneb_print_stop_guard_inflight(&stop_guard, 2000, 0));
    assert(deneb_print_stop_guard_inflight(&stop_guard, 5000, 1));
    assert(!deneb_print_stop_guard_begin(&stop_guard, 5001));
    assert(!deneb_print_stop_guard_inflight(&stop_guard, 5001, 0));
    assert(deneb_print_stop_guard_begin(&stop_guard, 5002));
    deneb_print_stop_guard_clear(&stop_guard);
    assert(!deneb_print_stop_guard_inflight(&stop_guard, 5003, 1));
}

static void test_print_job_summary(void)
{
    deneb_print_job_summary_t summary;

    deneb_print_job_summary_init(&summary, "cube.gcode", "uuid-1", "Cura",
                                 0, 0, 1, 120, 90, 25.0f);
    assert(summary.active);
    assert(summary.started);
    assert(strcmp(summary.name, "cube.gcode") == 0);
    assert(strcmp(summary.uuid, "uuid-1") == 0);
    assert(strcmp(summary.source, "Cura") == 0);
    assert(strcmp(summary.state, "printing") == 0);
    assert(summary.time_total == 120);
    assert(summary.time_left == 90);
    assert(summary.time_elapsed == 30);
    assert(summary.progress_percent == 25.0f);
    assert(summary.progress_fraction == 0.25f);

    deneb_print_job_summary_init(&summary, "", "", "", 0, 0, 0, 0, 0, 0.0f);
    assert(!summary.active);
    assert(!summary.started);
    assert(strcmp(summary.name, DENEB_PRINT_DEFAULT_JOB_NAME) == 0);
    assert(strcmp(summary.uuid, DENEB_PRINT_DEFAULT_JOB_UUID) == 0);
    assert(strcmp(summary.source, DENEB_PRINT_DEFAULT_JOB_SOURCE) == 0);
    assert(strcmp(summary.state, DENEB_PRINT_NONE_VALUE) == 0);

    deneb_print_job_summary_init_queued(&summary, "queued.gcode");
    assert(summary.active);
    assert(!summary.started);
    assert(strcmp(summary.name, "queued.gcode") == 0);
    assert(strcmp(summary.uuid, DENEB_PRINT_STOCK_API_JOB_UUID) == 0);
    assert(strcmp(summary.source, DENEB_PRINT_WEB_API_JOB_SOURCE) == 0);
    assert(strcmp(summary.state, DENEB_PRINT_PHASE_NAME_PRE_PRINT) == 0);
    assert(summary.time_elapsed == 0);
    assert(summary.progress_fraction == 0.0f);

    {
        char json[512];
        assert(deneb_print_job_summary_format_queued_response(
                   "Print job accepted", "queued\"job.gcode",
                   json, sizeof(json)) > 0);
        assert(strstr(json, "\"message\":\"Print job accepted\"") != NULL);
        assert(strstr(json, "\"name\":\"queued\\\"job.gcode\"") != NULL);
        assert(strstr(json, "\"uuid\":\"0\"") != NULL);
        assert(strstr(json, "\"source\":\"WEB_API\"") != NULL);
        assert(strstr(json, "\"state\":\"pre_print\"") != NULL);
        assert(strstr(json, "\"progress\":0.0") != NULL);
        assert(strstr(json, "\"time_elapsed\":0") != NULL);
        assert(strstr(json, "\"time_total\":0") != NULL);
    }
}

static void test_json_field_helpers(void)
{
    const char *json =
        "{\"file\":\"/home/3D/cube.gcode\",\"req\":\"JOB\","
        "\"Ttot\":120,\"headTset\":210.5,\"escaped\":\"cube\\\"name\"}";
    char value[64];

    assert(deneb_json_get_value(json, "file", value, sizeof(value)) == 0);
    assert(strcmp(value, "/home/3D/cube.gcode") == 0);
    assert(deneb_json_get_value(json, "escaped", value, sizeof(value)) == 0);
    assert(strcmp(value, "cube\"name") == 0);
    assert(deneb_json_get_int(json, "Ttot", 0) == 120);
    assert(deneb_json_get_float(json, "headTset", 0.0f) > 210.4f);
    assert(deneb_json_field_present(json, "req"));
    assert(!deneb_json_field_present(json, "missing"));
    {
        float parsed = 0.0f;
        int flag = 0;
        assert(deneb_json_get_float_value(json, "headTset", &parsed) == 0);
        assert(parsed > 210.4f);
        assert(deneb_json_get_float_value("{\"x\":\"bad\"}", "x", &parsed) != 0);
        assert(deneb_json_get_int_value("{\"deneb_tracker\":42}",
                                        "deneb_tracker", &flag) == 0);
        assert(flag == 42);
        assert(deneb_json_get_int_value("{\"deneb_tracker\":\"42x\"}",
                                        "deneb_tracker", &flag) != 0);
        assert(deneb_json_get_bool_value("{\"auth_required\":true}",
                                         "auth_required", &flag) == 0);
        assert(flag == 1);
        assert(deneb_json_get_bool_value("{\"auth_required\":false}",
                                         "auth_required", &flag) == 0);
        assert(flag == 0);
        assert(deneb_json_get_bool_value("{\"auth_required\":\"later\"}",
                                         "auth_required", &flag) != 0);
        assert(deneb_json_get_truthy_value("{\"topcapIsPresent\":\"yes\"}",
                                           "topcapIsPresent", &flag) == 0);
        assert(flag == 1);
        assert(deneb_json_get_truthy_value("{\"topcapIsPresent\":\"t\"}",
                                           "topcapIsPresent", &flag) == 0);
        assert(flag == 1);
        assert(deneb_json_get_truthy_value("{\"topcapIsPresent\":1}",
                                           "topcapIsPresent", &flag) == 0);
        assert(flag == 1);
        assert(deneb_json_get_truthy_value("{\"topcapIsPresent\":\"no\"}",
                                           "topcapIsPresent", &flag) == 0);
        assert(flag == 0);
    }
    assert(deneb_json_get_value(json, "missing", value, sizeof(value)) != 0);
    assert(deneb_json_get_int(json, "missing", 7) == 7);
}

static void test_status_payload_filename_resolution(void)
{
    deneb_status_payload_t payload;
    deneb_status_filename_context_t curr = {0};
    deneb_status_filename_context_t prev = {0};
    char retained[128] = "";
    char out[128] = "";

    assert(deneb_status_payload_parse(
        "{\"file\":\"/home/3D/cube.gcode\",\"req\":\"JOB\","
        "\"Ttot\":120,\"Tleft\":90}",
        &payload) == 0);
    deneb_status_filename_context_init(
        &curr, payload.req, "", payload.uuid, payload.time_total,
        payload.time_left, payload.bed_temp_set, payload.nozzle_temp_set,
        payload.is_printing, payload.is_paused);
    assert(curr.req == payload.req);
    assert(curr.filename && curr.filename[0] == '\0');
    assert(curr.time_total == 120);
    assert(curr.time_left == 90);
    deneb_status_payload_resolve_filename(&payload, &curr, &prev,
                                          retained, sizeof(retained),
                                          out, sizeof(out));
    assert(strcmp(out, "cube.gcode") == 0);
    assert(strcmp(retained, "cube.gcode") == 0);

    assert(deneb_status_payload_parse(
        "{\"file\":\"/home/cygnus/marlindriver/gcode/"
        DENEB_PRINT_MACRO_HOME_AND_CENTER_HEAD
        "\",\"req\":\"PREHEATING\",\"headTset\":200,\"bedTset\":60}",
        &payload) == 0);
    prev.req = DENEB_COMMAND_VERB_JOB;
    prev.filename = "cube.gcode";
    prev.uuid = "";
    prev.time_total = 120;
    prev.time_left = 90;
    prev.is_printing = 1;
    deneb_status_filename_context_init(
        &curr, payload.req, "", payload.uuid, payload.time_total,
        payload.time_left, payload.bed_temp_set, payload.nozzle_temp_set,
        payload.is_printing, payload.is_paused);
    deneb_status_payload_resolve_filename(&payload, &curr, &prev,
                                          retained, sizeof(retained),
                                          out, sizeof(out));
    assert(strcmp(out, "cube.gcode") == 0);

    out[0] = '\0';
    deneb_status_payload_resolve_filename_value(
        "/home/cygnus/marlindriver/gcode/"
        DENEB_PRINT_MACRO_HOME_AND_CENTER_HEAD,
        1, &curr, &prev, retained, sizeof(retained), out, sizeof(out));
    assert(strcmp(out, "cube.gcode") == 0);

    assert(deneb_status_payload_parse(
        "{\"file\":\"/home/3D/cube.gcode\",\"req\":\"ABORT\"}",
        &payload) == 0);
    deneb_status_filename_context_init(
        &curr, payload.req, "cube.gcode", "", 120, 90, 0.0f, 0.0f, 0, 0);
    deneb_status_payload_resolve_filename(&payload, &curr, &prev,
                                          retained, sizeof(retained),
                                          out, sizeof(out));
    assert(out[0] == '\0');
    assert(retained[0] == '\0');
}

static void test_gcode_command_helpers(void)
{
    char value[96];

    assert(strcmp(DENEB_GCODE_RELATIVE_MODE, "G91") == 0);
    assert(strcmp(DENEB_GCODE_ABSOLUTE_MODE, "G90") == 0);
    assert(strcmp(DENEB_GCODE_HOME_Z, "G28 Z") == 0);
    assert(strcmp(DENEB_GCODE_FAN_OFF, "M106 S0") == 0);
    assert(strcmp(DENEB_GCODE_RESET_EXTRUDER, "G92 E0") == 0);
    assert(strcmp(DENEB_GCODE_STOP_MATERIAL, "M401") == 0);
    assert(DENEB_GCODE_MAX_JOG_DISTANCE_MM == 50.0f);
    assert(deneb_gcode_normalize_motion_axis('x') == 'X');
    assert(deneb_gcode_axis_is_motion_axis('z'));
    assert(!deneb_gcode_axis_is_motion_axis('e'));
    assert(deneb_gcode_valid_jog_distance(1.0f));
    assert(deneb_gcode_valid_jog_distance(-50.0f));
    assert(!deneb_gcode_valid_jog_distance(0.5f));
    assert(!deneb_gcode_valid_jog_distance(51.0f));
    assert(!deneb_gcode_valid_jog_distance(1.5f));
    assert(deneb_gcode_valid_position_value('x', DENEB_GCODE_MAX_POSITION_X_MM));
    assert(!deneb_gcode_valid_position_value('X', DENEB_GCODE_MAX_POSITION_X_MM + 1.0f));
    assert(deneb_gcode_valid_position_value('Y', DENEB_GCODE_MAX_POSITION_Y_MM));
    assert(deneb_gcode_valid_position_value('Z', DENEB_GCODE_MAX_POSITION_Z_MM));
    assert(!deneb_gcode_valid_position_value('E', 1.0f));
    assert(deneb_gcode_valid_move_speed(DENEB_GCODE_DEFAULT_MOVE_SPEED_MM_S));
    assert(!deneb_gcode_valid_move_speed(0.0f));
    assert(!deneb_gcode_valid_move_speed(DENEB_GCODE_MAX_MOVE_SPEED_MM_S + 1.0f));
    assert(DENEB_GCODE_MAX_NOZZLE_TEMP_C == 260.0f);
    assert(DENEB_GCODE_MAX_BED_TEMP_C == 110.0f);

    assert(deneb_gcode_format_jog('X', 10.0f, value, sizeof(value)) == 0);
    assert(strcmp(value, "G1 X10 F3000") == 0);
    assert(deneb_gcode_format_jog('Y', -1.0f, value, sizeof(value)) == 0);
    assert(strcmp(value, "G1 Y-1 F3000") == 0);
    assert(deneb_gcode_format_jog('E', 1.0f, value, sizeof(value)) != 0);
    {
        deneb_gcode_jog_sequence_t seq;
        assert(deneb_gcode_build_jog_sequence('x', 5.0f, &seq) == 0);
        assert(strcmp(seq.lines[0], DENEB_GCODE_RELATIVE_MODE) == 0);
        assert(strcmp(seq.lines[1], "G1 X5 F3000") == 0);
        assert(strcmp(seq.lines[2], DENEB_GCODE_ABSOLUTE_MODE) == 0);
        assert(deneb_gcode_build_jog_sequence('E', 5.0f, &seq) != 0);
        assert(deneb_gcode_build_jog_sequence('X', 5.0f, NULL) != 0);
    }
    assert(deneb_gcode_format_extrude(360.0f, 60.0f, value,
                                      sizeof(value)) == 0);
    assert(strcmp(value, "G1 E360 F60") == 0);
    assert(deneb_gcode_format_extrude(-360.0f, 300.0f, value,
                                      sizeof(value)) == 0);
    assert(strcmp(value, "G1 E-360 F300") == 0);
    {
        deneb_gcode_material_move_sequence_t seq;
        assert(DENEB_GCODE_MATERIAL_MOVE_DISTANCE_MM == 360.0f);
        assert(DENEB_GCODE_MATERIAL_LOAD_FEEDRATE_MM_MIN == 60.0f);
        assert(DENEB_GCODE_MATERIAL_UNLOAD_FEEDRATE_MM_MIN == 300.0f);
        assert(DENEB_GCODE_MATERIAL_MOVE_MARGIN_MS == 2000U);
        assert(deneb_gcode_build_material_move_sequence(0, &seq) == 0);
        assert(strcmp(seq.lines[0], DENEB_GCODE_RESET_EXTRUDER) == 0);
        assert(strcmp(seq.lines[1], "G1 E360 F60") == 0);
        assert(seq.duration_ms == 362000U);
        assert(deneb_gcode_build_material_move_sequence(1, &seq) == 0);
        assert(strcmp(seq.lines[0], DENEB_GCODE_RESET_EXTRUDER) == 0);
        assert(strcmp(seq.lines[1], "G1 E-360 F300") == 0);
        assert(seq.duration_ms == 74000U);
        assert(deneb_gcode_build_material_move_sequence(0, NULL) != 0);
    }

    assert(deneb_gcode_format_absolute_position(1, 12.0f, 1, 34.5f,
                                                0, 0.0f, 150.0f,
                                                value, sizeof(value)) == 0);
    assert(strcmp(value, "G1 X12 Y34.5 F9e+03") == 0);
    assert(deneb_gcode_format_absolute_position(0, 0.0f, 0, 0.0f,
                                                0, 0.0f, 150.0f,
                                                value, sizeof(value)) != 0);

    assert(deneb_gcode_format_nozzle_target(210.0f, value, sizeof(value)) == 0);
    assert(strcmp(value, "M104 S210") == 0);
    assert(deneb_gcode_format_bed_target(60.0f, value, sizeof(value)) == 0);
    assert(strcmp(value, "M140 S60") == 0);
    assert(deneb_gcode_format_nozzle_off(value, sizeof(value)) == 0);
    assert(strcmp(value, "M104 S0") == 0);
    assert(deneb_gcode_format_bed_off(value, sizeof(value)) == 0);
    assert(strcmp(value, "M140 S0") == 0);
    {
        deneb_gcode_cooldown_sequence_t seq;
        assert(deneb_gcode_build_cooldown_sequence(&seq) == 0);
        assert(strcmp(seq.lines[0], "M104 S0") == 0);
        assert(strcmp(seq.lines[1], "M140 S0") == 0);
        assert(strcmp(seq.lines[2], DENEB_GCODE_FAN_OFF) == 0);
        assert(deneb_gcode_build_cooldown_sequence(NULL) != 0);
    }
    assert(deneb_gcode_frame_light_brightness_to_pwm(-5) == 0);
    assert(deneb_gcode_frame_light_brightness_to_pwm(100) == 255);
    assert(deneb_gcode_frame_light_brightness_to_pwm(50) == 128);
    assert(deneb_gcode_format_frame_light(50, value, sizeof(value)) == 0);
    assert(strcmp(value, "M142 w128") == 0);
    assert(deneb_gcode_format_frame_light(150, value, sizeof(value)) == 0);
    assert(strcmp(value, "M142 w255") == 0);
    assert(DENEB_GCODE_AIR_MANAGER_FAN_MAX_PWM == 255);
    assert(deneb_gcode_format_air_manager_fan(1, value, sizeof(value)) == 0);
    assert(strcmp(value, "M12030 S255") == 0);
    assert(deneb_gcode_format_air_manager_fan(0, value, sizeof(value)) == 0);
    assert(strcmp(value, "M12030 S0") == 0);
    assert(deneb_gcode_format_air_manager_fan(1, NULL, 0) != 0);
}

static void test_manual_motion_helpers(void)
{
    deneb_manual_motion_plan_t plan;

    deneb_manual_motion_plan_init(&plan);
    assert(plan.kind == DENEB_MANUAL_MOTION_NONE);
    assert(plan.command == NULL);

    assert(deneb_manual_motion_plan_action(
               DENEB_MANUAL_MOTION_ACTION_HOME, &plan) == 0);
    assert(plan.kind == DENEB_MANUAL_MOTION_MACRO);
    assert(strcmp(plan.command, DENEB_PRINT_MACRO_HOME_AND_CENTER_HEAD) == 0);

    assert(deneb_manual_motion_plan_action(
               DENEB_MANUAL_MOTION_ACTION_Z_HOME, &plan) == 0);
    assert(plan.kind == DENEB_MANUAL_MOTION_GCODE);
    assert(strcmp(plan.command, DENEB_GCODE_HOME_Z) == 0);

    assert(deneb_manual_motion_plan_action(
               DENEB_MANUAL_MOTION_ACTION_BED_UP, &plan) == 0);
    assert(plan.kind == DENEB_MANUAL_MOTION_MACRO);
    assert(strcmp(plan.command, DENEB_PRINT_MACRO_MOVE_BUILDPLATE_UP) == 0);

    assert(deneb_manual_motion_plan_action(
               DENEB_MANUAL_MOTION_ACTION_BED_DOWN, &plan) == 0);
    assert(plan.kind == DENEB_MANUAL_MOTION_MACRO);
    assert(strcmp(plan.command, DENEB_PRINT_MACRO_MOVE_BUILDPLATE_DOWN) == 0);

    assert(deneb_manual_motion_plan_action("unknown", &plan) != 0);
    assert(deneb_manual_motion_plan_action(
               DENEB_MANUAL_MOTION_ACTION_HOME, NULL) != 0);
}

static void test_buildplate_level_helpers(void)
{
    deneb_buildplate_level_plan_t plan;

    deneb_buildplate_level_plan_init(&plan);
    assert(plan.macro == NULL);

    assert(deneb_buildplate_level_plan_step(
               DENEB_BUILDPLATE_LEVEL_STEP_1, &plan) == 0);
    assert(strcmp(plan.macro,
                  DENEB_PRINT_MACRO_BUILDPLATE_LEVEL_STEP1) == 0);
    assert(deneb_buildplate_level_plan_step(
               DENEB_BUILDPLATE_LEVEL_STEP_2, &plan) == 0);
    assert(strcmp(plan.macro,
                  DENEB_PRINT_MACRO_BUILDPLATE_LEVEL_STEP2) == 0);
    assert(deneb_buildplate_level_plan_step(
               DENEB_BUILDPLATE_LEVEL_STEP_3, &plan) == 0);
    assert(strcmp(plan.macro,
                  DENEB_PRINT_MACRO_BUILDPLATE_LEVEL_STEP3) == 0);
    assert(deneb_buildplate_level_plan_step(
               DENEB_BUILDPLATE_LEVEL_STEP_4, &plan) == 0);
    assert(strcmp(plan.macro,
                  DENEB_PRINT_MACRO_BUILDPLATE_LEVEL_STEP4) == 0);
    assert(deneb_buildplate_level_plan_step(
               DENEB_BUILDPLATE_LEVEL_STEP_FINISH, &plan) == 0);
    assert(strcmp(plan.macro,
                  DENEB_PRINT_MACRO_BUILDPLATE_LEVEL_FINISH) == 0);
    assert(deneb_buildplate_level_plan_step(
               (deneb_buildplate_level_step_t)99, &plan) != 0);
    assert(deneb_buildplate_level_plan_step(
               DENEB_BUILDPLATE_LEVEL_STEP_1, NULL) != 0);
}

static void test_material_workflow_helpers(void)
{
    deneb_material_workflow_stop_plan_t plan;

    assert(DENEB_MATERIAL_WORKFLOW_DEFAULT_TEMP_C == 210);
    deneb_material_workflow_stop_plan_init(&plan);
    assert(plan.stop_gcode == NULL);
    assert(plan.cooldown_gcode == NULL);
    assert(plan.nozzle_off[0] == '\0');

    assert(deneb_material_workflow_stop_plan(0, &plan) == 0);
    assert(plan.stop_gcode == NULL);
    assert(strcmp(plan.cooldown_gcode, "M104 S0") == 0);
    assert(strcmp(plan.nozzle_off, "M104 S0") == 0);

    assert(deneb_material_workflow_stop_plan(1, &plan) == 0);
    assert(strcmp(plan.stop_gcode, DENEB_GCODE_STOP_MATERIAL) == 0);
    assert(strcmp(plan.cooldown_gcode, "M104 S0") == 0);
    assert(deneb_material_workflow_stop_plan(1, NULL) != 0);

    assert(deneb_material_workflow_status(0, 0, 0, 210, 0) ==
           DENEB_MATERIAL_WORKFLOW_STATUS_BUSY);
    assert(deneb_material_workflow_status(1, 1, 0, 210, 0) ==
           DENEB_MATERIAL_WORKFLOW_STATUS_MOVING);
    assert(deneb_material_workflow_status(1, 0, 0, 210, 0) ==
           DENEB_MATERIAL_WORKFLOW_STATUS_SET_TARGET);
    assert(deneb_material_workflow_status(1, 0, 1, 0, 0) ==
           DENEB_MATERIAL_WORKFLOW_STATUS_COOLING);
    assert(deneb_material_workflow_status(1, 0, 1, 160, 0) ==
           DENEB_MATERIAL_WORKFLOW_STATUS_TARGET_TOO_LOW);
    assert(deneb_material_workflow_status(1, 0, 1, 210, 1) ==
           DENEB_MATERIAL_WORKFLOW_STATUS_READY_TO_MOVE);
    assert(deneb_material_workflow_status(1, 0, 1, 210, 0) ==
           DENEB_MATERIAL_WORKFLOW_STATUS_HEATING);
}

static void test_json_string_helpers(void)
{
    char value[64];

    deneb_json_escape_string("cube\"one\\two.gcode", value, sizeof(value));
    assert(strcmp(value, "cube\\\"one\\\\two.gcode") == 0);

    deneb_json_escape_string("line\nbreak", value, sizeof(value));
    assert(strcmp(value, "linebreak") == 0);

    deneb_json_escape_string(NULL, value, sizeof(value));
    assert(strcmp(value, "") == 0);
}

static void test_status_payload_helpers(void)
{
    deneb_status_payload_t payload;

    assert(deneb_status_payload_parse(
               "{\"name\":\"cube.gcode\",\"file\":\"none\",\"source\":\"Cura\","
               "\"uuid\":\"job-1\",\"req\":\"JOB\",\"Ttot\":120,\"Tleft\":60,"
               "\"headTcur\":199.5,\"headTset\":210,\"bedTcur\":58,"
               "\"bedTset\":60,\"topcapTemperature\":32.5,"
               "\"topcapIsPresent\":\"yes\",\"received_faults\":1}",
               &payload) == 0);
    assert(strcmp(payload.file, "cube.gcode") == 0);
    assert(strcmp(payload.source, "Cura") == 0);
    assert(strcmp(payload.uuid, "job-1") == 0);
    assert(strcmp(payload.req, DENEB_COMMAND_VERB_JOB) == 0);
    assert(payload.has_file);
    assert(payload.is_printing);
    assert(!payload.is_paused);
    assert(payload.has_error);
    assert(payload.topcap_present);
    assert(payload.progress > 49.9f && payload.progress < 50.1f);
    assert(payload.nozzle_temp_cur > 199.4f);
    assert(payload.observation.file == payload.file);

    assert(deneb_status_payload_parse(
               "{\"file\":\"/home/3D/cube.gcode\",\"req\":\"PAUSE\","
               "\"Ttot\":120,\"Tleft\":80,\"topcapIsPresent\":\"no\"}",
               &payload) == 0);
    assert(payload.is_paused);
    assert(payload.is_printing);
    assert(!payload.topcap_present);

    assert(deneb_status_payload_parse(
               "{\"file\":\"/home/3D/cube.gcode\",\"req\":\"ABORT\","
               "\"Ttot\":120,\"Tleft\":80}",
               &payload) == 0);
    assert(!payload.is_printing);
}

static void test_print_profile_helpers(void)
{
    char value[64];

    assert(strcmp(DENEB_PRINT_PROFILE_MACHINE_FAMILY,
                  "ultimaker2_plus_connect") == 0);
    assert(strcmp(DENEB_PRINT_PROFILE_MACHINE_VARIANT,
                  "Ultimaker 2+ Connect") == 0);

    deneb_print_profile_normalize_nozzle_id("0.6", value, sizeof(value));
    assert(strcmp(value, "0.6 mm") == 0);
    deneb_print_profile_normalize_nozzle_id("0.8 mm", value, sizeof(value));
    assert(strcmp(value, "0.8 mm") == 0);
    deneb_print_profile_normalize_nozzle_id("", value, sizeof(value));
    assert(strcmp(value, DENEB_PRINT_PROFILE_DEFAULT_NOZZLE_ID) == 0);
    assert(deneb_print_profile_normalize_nozzle_size("0.60", value,
                                                     sizeof(value)) == 0);
    assert(strcmp(value, "0.6") == 0);
    assert(deneb_print_profile_normalize_nozzle_size(" 0.80 ", value,
                                                     sizeof(value)) == 0);
    assert(strcmp(value, "0.8") == 0);
    assert(deneb_print_profile_normalize_nozzle_size("1.2", value,
                                                     sizeof(value)) != 0);
    assert(strcmp(value, DENEB_PRINT_PROFILE_DEFAULT_NOZZLE_SIZE) == 0);

    deneb_print_profile_material_name_from_guid(
        DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_GUID, value, sizeof(value));
    assert(strcmp(value, DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_TYPE) == 0);
    deneb_print_profile_material_name_from_guid("custom-guid", value, sizeof(value));
    assert(strcmp(value, "custom-guid") == 0);
}

static void test_printer_identity_helpers(void)
{
    char value[64];

    deneb_printer_identity_copy_line_or_default("um2c-lab\n",
                                                DENEB_PRINTER_DEFAULT_HOSTNAME,
                                                value, sizeof(value));
    assert(strcmp(value, "um2c-lab") == 0);
    deneb_printer_identity_copy_line_or_default("  \t",
                                                DENEB_PRINTER_DEFAULT_HOSTNAME,
                                                value, sizeof(value));
    assert(strcmp(value, DENEB_PRINTER_DEFAULT_HOSTNAME) == 0);
    deneb_printer_identity_copy_line_or_default(NULL,
                                                DENEB_PRINTER_DEFAULT_GUID,
                                                value, sizeof(value));
    assert(strcmp(value, DENEB_PRINTER_DEFAULT_GUID) == 0);
}

static void test_print_backend_route_contract(void)
{
    deneb_print_backend_t backend;
    deneb_print_backend_route_t route;
    char fields[256];

    assert(deneb_print_backend_parse_override("native", &backend) == 0);
    assert(backend == DENEB_PRINT_BACKEND_NATIVE);
    assert(deneb_print_backend_parse_override("1", &backend) == 0);
    assert(backend == DENEB_PRINT_BACKEND_NATIVE);
    assert(deneb_print_backend_parse_override("coordinator", &backend) == 0);
    assert(backend == DENEB_PRINT_BACKEND_COORDINATOR);
    assert(deneb_print_backend_parse_override("stock", &backend) == 0);
    assert(backend == DENEB_PRINT_BACKEND_COORDINATOR);
    assert(deneb_print_backend_parse_override("bad", &backend) != 0);
    assert(deneb_print_backend_is_native(DENEB_PRINT_BACKEND_NATIVE));
    assert(!deneb_print_backend_is_native(DENEB_PRINT_BACKEND_COORDINATOR));

    assert(deneb_print_backend_from_flag_text("1\n") == DENEB_PRINT_BACKEND_NATIVE);
    assert(deneb_print_backend_from_flag_text("0\n") == DENEB_PRINT_BACKEND_COORDINATOR);
    assert(deneb_print_backend_from_flag_text(NULL) == DENEB_PRINT_BACKEND_COORDINATOR);

    route = deneb_print_backend_route(DENEB_PRINT_BACKEND_COORDINATOR);
    assert(route.backend == DENEB_PRINT_BACKEND_COORDINATOR);
    assert(strcmp(route.status_url, DENEB_COORDINATOR_STATUS_URL) == 0);
    assert(strcmp(route.command_url, DENEB_COORDINATOR_COMMAND_URL) == 0);
    assert(deneb_print_backend_route_json_fields(&route, fields, sizeof(fields)) > 0);
    assert(strstr(fields, "\"print_backend\":\"coordinator\"") != NULL);
    assert(strstr(fields, DENEB_COORDINATOR_STATUS_URL) != NULL);
    assert(strstr(fields, DENEB_COORDINATOR_COMMAND_URL) != NULL);

    route = deneb_print_backend_route(DENEB_PRINT_BACKEND_NATIVE);
    assert(route.backend == DENEB_PRINT_BACKEND_NATIVE);
    assert(strcmp(route.status_url, DENEB_PRINTSVC_STATUS_URL) == 0);
    assert(strcmp(route.command_url, DENEB_PRINTSVC_COMMAND_URL) == 0);
    assert(strcmp(deneb_print_backend_name(route.backend), "native") == 0);
    assert(deneb_print_backend_route_json_fields(&route, fields, sizeof(fields)) > 0);
    assert(strstr(fields, "\"print_backend\":\"native\"") != NULL);
    assert(strstr(fields, DENEB_PRINTSVC_STATUS_URL) != NULL);
    assert(strstr(fields, DENEB_PRINTSVC_COMMAND_URL) != NULL);
}

static void test_pending_job_metadata(void)
{
    deneb_pending_job_t job;
    char json[4096];

    deneb_pending_job_init(&job, "/home/3D/deneb-uploads/cube.gcode");
    job.tracker = 42;
    assert(deneb_pending_job_change_count(&job) == 0);
    assert(deneb_pending_job_serialize(&job, json, sizeof(json)) > 0);
    assert(strstr(json, "\"name\":\"cube.gcode\"") != NULL);
    assert(strstr(json, "\"path\":\"/home/3D/deneb-uploads/cube.gcode\"") != NULL);
    assert(strstr(json, "\"status\":\"pre_print\"") != NULL);
    assert(strstr(json, "\"owner\":\"Cura\"") != NULL);
    assert(strstr(json, "\"deneb_tracker\":42") != NULL);
    assert(strstr(json, "\"configuration_changes_required\"") == NULL);

    job.material_change_required = 1;
    job.print_core_change_required = 1;
    snprintf(job.origin_material_guid, sizeof(job.origin_material_guid), "loaded-guid");
    snprintf(job.origin_material_name, sizeof(job.origin_material_name), "Loaded PLA");
    snprintf(job.target_material_name, sizeof(job.target_material_name), "Target PETG");
    snprintf(job.material_guid, sizeof(job.material_guid), "target-guid");
    snprintf(job.origin_nozzle_id, sizeof(job.origin_nozzle_id), "0.4 mm");
    snprintf(job.nozzle_id, sizeof(job.nozzle_id), "0.6 mm");
    assert(deneb_pending_job_change_count(&job) == 2);
    assert(deneb_pending_job_serialize(&job, json, sizeof(json)) > 0);
    assert(strstr(json, "\"status\":\"wait_user_action\"") != NULL);
    assert(strstr(json, "\"material_change\"") != NULL);
    assert(strstr(json, "\"print_core_change\"") != NULL);
    assert(strstr(json, "\"origin_id\":\"loaded-guid\"") != NULL);
    assert(strstr(json, "\"origin_name\":\"Loaded PLA\"") != NULL);
    assert(strstr(json, "\"target_id\":\"target-guid\"") != NULL);
    assert(strstr(json, "\"target_name\":\"Target PETG\"") != NULL);
    assert(strstr(json, "\"origin_id\":\"0.4 mm\"") != NULL);
    assert(strstr(json, "\"target_id\":\"0.6 mm\"") != NULL);
}

static void test_pending_job_metadata_write_file(void)
{
    const char *path = "/tmp/deneb-pending-job-write-test.json";
    deneb_pending_job_t job;
    deneb_pending_job_file_t loaded;

    deneb_pending_job_init(&job, "/home/3D/deneb-uploads/write-test.gcode");
    job.tracker = 77;
    assert(deneb_pending_job_write_file(&job, path) == 0);
    assert(deneb_pending_job_file_load(path, &loaded) == 0);
    assert(loaded.tracker == 77);
    assert(strcmp(loaded.path, "/home/3D/deneb-uploads/write-test.gcode") == 0);
    assert(strcmp(loaded.name, "write-test.gcode") == 0);
    assert(!deneb_pending_job_file_has_conflict(&loaded));
    assert(deneb_pending_job_file_clear(path) == 0);
}

static void test_pending_job_upload_default_check(void)
{
    deneb_pending_job_t job;
    deneb_pending_job_upload_check_t check;

    deneb_pending_job_file_clear_default();
    assert(deneb_pending_job_file_check_upload_default(
               "/home/3D/deneb-uploads/cube.gcode", "cube.gcode",
               &check) == 0);
    assert(check.status == DENEB_PENDING_JOB_UPLOAD_CLEAR);

    deneb_pending_job_init(&job, "/home/3D/deneb-uploads/cube.gcode");
    job.tracker = 88;
    assert(deneb_pending_job_write_default(&job) == 0);

    assert(deneb_pending_job_file_check_upload_default(
               "/home/3D/deneb-uploads/cube.gcode", "cube.gcode",
               &check) == 0);
    assert(check.status == DENEB_PENDING_JOB_UPLOAD_DUPLICATE);
    assert(check.tracker == 88);
    assert(strcmp(check.display_name, "cube.gcode") == 0);

    assert(deneb_pending_job_file_check_upload_default(
               "/home/3D/deneb-uploads/other.gcode", "other.gcode",
               &check) == 0);
    assert(check.status == DENEB_PENDING_JOB_UPLOAD_BLOCKED);

    deneb_pending_job_file_clear_default();
}

static void test_pending_job_registration_policy(void)
{
    const char *plain_path = "/tmp/deneb-registration-plain.gcode";
    const char *conflict_path = "/tmp/deneb-registration-conflict.gcode";
    deneb_pending_job_registration_t registration;
    FILE *f;

    f = fopen(plain_path, "wb");
    assert(f != NULL);
    fputs("G28\n", f);
    fclose(f);

    assert(deneb_pending_job_registration_prepare(
               plain_path, 123, &registration) == 0);
    assert(registration.job.tracker == 123);
    assert(registration.change_count == 0);
    assert(registration.should_start_immediately);
    assert(strcmp(registration.job.path, plain_path) == 0);
    assert(strcmp(registration.job.source, DENEB_PRINT_DEFAULT_JOB_SOURCE) == 0);

    f = fopen(conflict_path, "wb");
    assert(f != NULL);
    fputs("; material_guid='material-123'\n; nozzle_size=0.8\n", f);
    fclose(f);

    assert(deneb_pending_job_registration_prepare(
               conflict_path, 456, &registration) == 0);
    assert(registration.job.tracker == 456);
    assert(strcmp(registration.job.material_guid, "material-123") == 0);
    assert(strcmp(registration.job.nozzle_id, "0.8 mm") == 0);
    assert(registration.change_count >= 1);
    assert(!registration.should_start_immediately);

    remove(plain_path);
    remove(conflict_path);
}

static void test_print_job_file_metadata(void)
{
    const char *path = "/tmp/deneb-print-job-metadata-test.gcode";
    deneb_print_job_file_metadata_t meta;
    char value[64];
    char safe[128];
    char spool_path[256];
    FILE *f;

    assert(strcmp(DENEB_PRINT_JOB_USB_SCAN_DIR, "/mnt/sda1") == 0);
    assert(strcmp(DENEB_PRINT_JOB_LOCAL_SCAN_DIR, "/home/3D") == 0);
    assert(strcmp(DENEB_PRINT_JOB_SPOOL_DIR, "/home/3D/deneb-uploads") == 0);
    assert(deneb_print_job_file_sanitize_name("../../cube.gcode",
                                              safe, sizeof(safe)) == 0);
    assert(strcmp(safe, "cube.gcode") == 0);
    assert(deneb_print_job_file_sanitize_name("..", safe, sizeof(safe)) == 0);
    assert(strcmp(safe, "upload.gcode") == 0);
    snprintf(safe, sizeof(safe), "\\usb\\job.gcode");
    assert(deneb_print_job_file_sanitize_name(safe, safe, sizeof(safe)) == 0);
    assert(strcmp(safe, "job.gcode") == 0);
    assert(deneb_print_job_file_spool_path("../cube.gcode", spool_path,
                                           sizeof(spool_path)) == 0);
    assert(strcmp(spool_path, DENEB_PRINT_JOB_SPOOL_DIR "/cube.gcode") == 0);
    assert(deneb_print_job_file_metadata_extract_value(
               "; material_guid = target-guid\n", "material_guid",
               value, sizeof(value)) == 0);
    assert(strcmp(value, "target-guid") == 0);
    assert(deneb_print_job_file_metadata_extract_value(
               "; print_core_id:\"0.6\"", "print_core_id",
               value, sizeof(value)) == 0);
    assert(strcmp(value, "0.6") == 0);
    assert(deneb_print_job_file_metadata_extract_value(
               "; material_guid='material-123'\n; nozzle_size=0.8\n",
               "nozzle_size", value, sizeof(value)) == 0);
    assert(strcmp(value, "0.8") == 0);

    deneb_print_job_file_metadata_init(&meta);
    f = fopen(path, "wb");
    assert(f != NULL);
    fputs("; material_guid='material-123'\n; nozzle_size=0.8\n", f);
    fclose(f);
    assert(deneb_print_job_file_metadata_load(path, &meta) == 0);
    assert(strcmp(meta.material_guid, "material-123") == 0);
    assert(strcmp(meta.nozzle_size, "0.8") == 0);

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs("; print_core_id = 0.6\n", f);
    fclose(f);
    deneb_print_job_file_metadata_init(&meta);
    assert(deneb_print_job_file_metadata_load(path, &meta) == 0);
    assert(strcmp(meta.nozzle_size, "0.6") == 0);
    remove(path);
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

static void test_material_catalog_helpers(void)
{
    const char *material_path = "/tmp/deneb-material-catalog-test.xml";
    const char *catalog_dir = "/tmp/deneb-material-catalog";
    const char *guid = "506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9";
    char parsed_guid[64];
    char tag_value[64];
    char *body = NULL;
    size_t body_len = 0;
    int version = -1;
    FILE *f;

    assert(strcmp(DENEB_MATERIAL_CATALOG_DIR, "/home/3D/deneb-materials") == 0);
    assert(deneb_material_catalog_copy_tag_value("<GUID> abc </GUID>",
                                                 "GUID", tag_value,
                                                 sizeof(tag_value)) == 0);
    assert(strcmp(tag_value, "abc") == 0);
    assert(deneb_material_catalog_guid_is_safe(guid));
    assert(!deneb_material_catalog_guid_is_safe("../bad-guid"));

    remove("/tmp/deneb-material-catalog/506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9.json");
    rmdir(catalog_dir);
    f = fopen(material_path, "wb");
    assert(f != NULL);
    fprintf(f, "<fdmmaterial><metadata><GUID>%s</GUID><version>7</version>"
            "</metadata></fdmmaterial>", guid);
    fclose(f);

    assert(deneb_material_catalog_parse_file(material_path, parsed_guid,
                                             sizeof(parsed_guid),
                                             &version) == 0);
    assert(strcmp(parsed_guid, guid) == 0);
    assert(version == 7);
    assert(deneb_material_catalog_store_file(material_path, catalog_dir,
                                             parsed_guid, sizeof(parsed_guid),
                                             &version) == 0);
    assert(deneb_material_catalog_build_response("[{\"guid\":\"stock\"}]",
                                                 catalog_dir, &body,
                                                 &body_len) == 0);
    assert(body_len == strlen(body));
    assert(strstr(body, "\"guid\":\"stock\"") != NULL);
    assert(strstr(body, guid) != NULL);
    free(body);
    f = fopen(material_path, "wb");
    assert(f != NULL);
    fprintf(f, "<fdmmaterial><metadata><GUID>%s</GUID><version>7x</version>"
            "</metadata></fdmmaterial>", guid);
    fclose(f);
    assert(deneb_material_catalog_parse_file(material_path, parsed_guid,
                                             sizeof(parsed_guid),
                                             &version) != 0);
    remove("/tmp/deneb-material-catalog/506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9.json");
    rmdir(catalog_dir);
    remove(material_path);
}

static void test_macro_safety(void)
{
    char path[256];

    assert(deneb_macro_resolve(DENEB_PRINT_MACRO_HOME_AND_CENTER_HEAD,
                               path, sizeof(path)) == 0);
    assert(strstr(path, DENEB_PRINT_MACRO_HOME_AND_CENTER_HEAD) != NULL);
    assert(deneb_macro_resolve("init.gcode", path, sizeof(path)) == 0);
    assert(strstr(path, DENEB_PRINTSVC_MACRO_DIR) == path);
    assert(deneb_macro_resolve("../init.gcode", path, sizeof(path)) != 0);
    assert(deneb_macro_resolve("/tmp/init.gcode", path, sizeof(path)) != 0);
}

static void test_diagnostics_log_side_by_side_fields(void)
{
    const char *path = "/tmp/deneb-printsvc-diagnostics-test.log";
    deneb_status_t status;
    deneb_command_t cmd;
    char buf[4096];
    FILE *f;
    size_t n;

    remove(path);
    deneb_status_init(&status);
    status.state = DENEB_PRINT_STATE_PRINTING;
    snprintf(status.req, sizeof(status.req), "%s", DENEB_COMMAND_VERB_JOB);
    snprintf(status.file, sizeof(status.file), "cube.gcode");
    status.head_t_cur = 201.0f;
    status.head_t_set = 210.0f;
    status.bed_t_cur = 59.0f;
    status.bed_t_set = 60.0f;
    status.flow_ack = 12;
    status.flow_resend = 1;
    status.flow_reject = 2;
    status.flow_inflight = 3;
    status.job_queue_depth = 1;
    status.job_line_number = 42;
    status.command_latency_ms = 7;
    status.planner_starvation_count = 4;

    assert(deneb_command_parse("PAUSE<{}", &cmd) == 0);
    assert(deneb_diagnostics_log_open(path) == 0);
    deneb_diagnostics_log_status(&status, 1);
    deneb_diagnostics_log_command(&cmd, 0, 7);
    deneb_diagnostics_log_close();

    f = fopen(path, "rb");
    assert(f != NULL);
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    assert(strstr(buf, "event=status") != NULL);
    assert(strstr(buf, "stock.req=\"JOB\"") != NULL);
    assert(strstr(buf, "stock.file=\"cube.gcode\"") != NULL);
    assert(strstr(buf, "native.phase=printing") != NULL);
    assert(strstr(buf, "serial.ack=12") != NULL);
    assert(strstr(buf, "serial.resend=1") != NULL);
    assert(strstr(buf, "serial.reject=2") != NULL);
    assert(strstr(buf, "native.queueDepth=1") != NULL);
    assert(strstr(buf, "native.jobLine=42") != NULL);
    assert(strstr(buf, "native.commandLatencyMs=7") != NULL);
    assert(strstr(buf, "native.plannerStarvation=4") != NULL);
    assert(strstr(buf, "event=command") != NULL);
    assert(strstr(buf, "command.verb=\"PAUSE\"") != NULL);
    remove(path);
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
    assert(strcmp(svc.status.file, DENEB_PRINT_NONE_VALUE) == 0);
}

static void test_job_accepts_without_blocking(void)
{
    const char *job_path = "/tmp/deneb-printsvc-job-test.gcode";
    FILE *f = fopen(job_path, "wb");
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char frame[512];
    char reply[256];

    assert(f != NULL);
    fputs("M104 S200\nM140 S60\nG1 X1 Y1\n", f);
    fclose(f);

    deneb_print_service_init(&svc);
    snprintf(frame, sizeof(frame),
             "JOB<{\"file\":\"%s\",\"bedTset\":60,\"headTset\":200}",
             job_path);
    assert(deneb_command_parse(frame, &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "job accepted") != NULL);
    assert(svc.job_active);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);

    assert(deneb_command_parse("ABORT<{}", &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(!svc.job_active);
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);
    remove(job_path);
}

static void test_service_context_policy(void)
{
    deneb_print_service_t svc;
    deneb_motion_runtime_t runtime;
    deneb_job_streamer_t streamer;
    uint8_t packet[128];
    size_t written = 0;
    uint8_t sequence = 0;

    deneb_service_context_init(&svc);
    assert(svc.serial.fd == -1);
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);

    assert(deneb_service_context_motion_runtime(NULL, &runtime) < 0);
    assert(deneb_service_context_motion_runtime(&svc, NULL) < 0);
    assert(deneb_service_context_motion_runtime(&svc, &runtime) == 0);
    assert(runtime.status == &svc.status);
    assert(runtime.heater_wait == &svc.heater_wait);
    assert(runtime.flow == &svc.flow);
    assert(runtime.serial == &svc.serial);
    assert(runtime.serial_ready == &svc.serial_ready);

    assert(deneb_service_context_job_streamer(NULL, &streamer) < 0);
    assert(deneb_service_context_job_streamer(&svc, NULL) < 0);
    assert(deneb_service_context_job_streamer(&svc, &streamer) == 0);
    assert(streamer.status == &svc.status);
    assert(streamer.flow == &svc.flow);
    assert(streamer.stream == &svc.job_stream);
    assert(streamer.heater_wait == &svc.heater_wait);
    assert(streamer.serial == &svc.serial);
    assert(streamer.job_active == &svc.job_active);
    assert(streamer.abort_requested == &svc.abort_requested);
    assert(streamer.planner_starvation_count == &svc.planner_starvation_count);

    svc.job_active = 1;
    svc.job_stream.line_number = 17;
    svc.planner_starvation_count = 3;
    assert(deneb_flow_prepare_packet(&svc.flow, "M105", packet,
                                     sizeof(packet), &written,
                                     &sequence) == 0);
    deneb_service_context_refresh_diagnostics(&svc);
    assert(svc.status.job_queue_depth == 1);
    assert(svc.status.job_line_number == 17);
    assert(svc.status.planner_starvation_count == 3);

    svc.serial_ready = 1;
    deneb_service_context_close(&svc);
    assert(!svc.job_active);
    assert(!svc.serial_ready);
}

static void test_service_command_policy(void)
{
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char reply[256];

    deneb_print_service_init(&svc);
    assert(deneb_command_parse("GCODE<[\"M105\"]", &cmd) == 0);
    assert(deneb_service_command_handle(NULL, &cmd, reply, sizeof(reply)) < 0);
    assert(deneb_service_command_handle(&svc, &cmd, NULL, sizeof(reply)) < 0);
    assert(deneb_service_command_handle(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "gcode accepted") != NULL);
    assert(deneb_flow_inflight(&svc.flow) == 1);

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = DENEB_COMMAND_UNKNOWN;
    assert(deneb_service_command_handle(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "unknown command") != NULL);
}

static void test_job_poll_streams_after_preheat(void)
{
    const char *job_path = "/tmp/deneb-printsvc-job-poll-test.gcode";
    FILE *f = fopen(job_path, "wb");
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char frame[512];
    char reply[256];

    assert(f != NULL);
    fputs("; ignored comment\nG1 X1 Y1\nG1 X2 Y2\n", f);
    fclose(f);

    deneb_print_service_init(&svc);
    snprintf(frame, sizeof(frame),
             "JOB<{\"file\":\"%s\",\"bedTset\":60,\"headTset\":200}",
             job_path);
    assert(deneb_command_parse(frame, &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(deneb_print_service_poll_job(&svc) == 0);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);

    svc.status.bed_t_cur = 60.0f;
    svc.status.head_t_cur = 200.0f;
    assert(deneb_print_service_poll_job(&svc) == 1);
    deneb_print_service_refresh_diagnostics(&svc);
    assert(svc.status.state == DENEB_PRINT_STATE_PRINTING);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    assert(svc.status.flow_inflight == 1);
    assert(svc.status.flow_sent == 1);
    assert(svc.status.job_queue_depth == 1);
    assert(svc.status.job_line_number == 2);
    assert(svc.status.planner_starvation_count == 1);
    remove(job_path);
}

static void test_pause_gates_active_job_streaming(void)
{
    const char *job_path = "/tmp/deneb-printsvc-job-pause-test.gcode";
    FILE *f = fopen(job_path, "wb");
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char frame[512];
    char reply[256];

    assert(f != NULL);
    fputs("G1 X1 Y1\nG1 X2 Y2\n", f);
    fclose(f);

    deneb_print_service_init(&svc);
    snprintf(frame, sizeof(frame), "JOB<{\"file\":\"%s\"}", job_path);
    assert(deneb_command_parse(frame, &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(deneb_print_service_poll_job(&svc) == 1);
    assert(deneb_flow_inflight(&svc.flow) == 1);

    assert(deneb_command_parse("PAUSE<{}", &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(svc.status.state == DENEB_PRINT_STATE_PAUSED);
    assert(deneb_print_service_poll_job(&svc) == 0);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    assert(svc.job_stream.line_number == 1);

    assert(deneb_command_parse("RESUME<{}", &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(svc.status.state == DENEB_PRINT_STATE_PRINTING);
    assert(deneb_print_service_poll_job(&svc) == 1);
    assert(deneb_flow_inflight(&svc.flow) == 2);
    assert(svc.job_stream.line_number == 2);
    remove(job_path);
}

static void test_pause_during_preheat_resumes_to_preparing(void)
{
    const char *job_path = "/tmp/deneb-printsvc-job-preheat-pause-test.gcode";
    FILE *f = fopen(job_path, "wb");
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char frame[512];
    char reply[256];

    assert(f != NULL);
    fputs("G1 X1 Y1\n", f);
    fclose(f);

    deneb_print_service_init(&svc);
    snprintf(frame, sizeof(frame),
             "JOB<{\"file\":\"%s\",\"bedTset\":60,\"headTset\":200}",
             job_path);
    assert(deneb_command_parse(frame, &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);

    assert(deneb_command_parse("PAUSE<{}", &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(svc.status.state == DENEB_PRINT_STATE_PAUSED);
    assert(deneb_print_service_poll_job(&svc) == 0);
    assert(svc.job_stream.line_number == 0);

    assert(deneb_command_parse("RESUME<{}", &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);
    assert(deneb_print_service_poll_job(&svc) == 0);
    assert(svc.job_stream.line_number == 0);
    remove(job_path);
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

static void test_home_distance_parser(void)
{
    deneb_status_t status;
    char frame[1536];

    deneb_status_init(&status);
    assert(deneb_status_parse_marlin_line(&status, "G28 home-distance X:0.12 Y:-0.25 Z:1.50") == DENEB_PARSE_HOME_DISTANCE);
    assert(status.home_distance_valid);
    assert(status.home_x > 0.11f && status.home_x < 0.13f);
    assert(status.home_y < -0.24f && status.home_y > -0.26f);
    assert(status.home_z > 1.49f && status.home_z < 1.51f);
    assert(deneb_status_serialize_frame(&status, frame, sizeof(frame)) > 0);
    assert(strstr(frame, "\"homeDistanceValid\":true") != NULL);
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

    deneb_heater_wait_start(&wait, 0.0f, 200.0f, 1.0f);
    deneb_status_init(&status);
    status.head_t_cur = 199.2f;
    assert(deneb_heater_wait_ready(&wait, &status));
    status.head_t_cur = 198.0f;
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
    assert(policy.count == 5);
    assert(strcmp(policy.commands[0], DENEB_GCODE_WAIT_FOR_MOVES) == 0);
    assert(strcmp(policy.commands[1], "M104 S0") == 0);
    assert(strcmp(policy.commands[2], "M140 S0") == 0);
    assert(strcmp(policy.commands[3], DENEB_GCODE_FAN_OFF) == 0);
    assert(strcmp(policy.commands[4], DENEB_GCODE_DISABLE_EXTRUDER_STEPPER) == 0);
    assert(!deneb_motion_policy_contains_xy_home(&policy));

    deneb_motion_policy_finish(&policy);
    assert(policy.count == 5);
    assert(strcmp(policy.commands[0], DENEB_GCODE_WAIT_FOR_MOVES) == 0);
    assert(strcmp(policy.commands[1], "M104 S0") == 0);
    assert(strcmp(policy.commands[2], "M140 S0") == 0);
    assert(strcmp(policy.commands[3], DENEB_GCODE_FAN_OFF) == 0);
    assert(strcmp(policy.commands[4], DENEB_GCODE_DISABLE_ALL_STEPPERS) == 0);
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

static void test_pending_job_file_contract(void)
{
    const char *path = "/tmp/deneb-pending-job-file-test.json";
    deneb_pending_job_file_t job;
    deneb_pending_job_action_plan_t plan;
    deneb_pending_job_upload_check_t upload;
    char raw[1024];
    char display[128];
    size_t raw_len = 0;
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    fputs("[{\"name\":\"Cube\",\"path\":\"/home/3D/cube.gcode\","
          "\"status\":\"wait_user_action\",\"deneb_tracker\":42,"
          "\"configuration_changes_required\":["
          "{\"type_of_change\":\"material_change\","
          "\"origin_name\":\"PLA\",\"target_name\":\"PETG\"}]}]\n", f);
    fclose(f);

    assert(deneb_pending_job_file_read_raw_array(path, raw, sizeof(raw), &raw_len) == 0);
    assert(raw_len > 0);
    assert(deneb_pending_job_file_load(path, &job) == 0);
    assert(job.tracker == 42);
    assert(strcmp(job.name, "Cube") == 0);
    assert(strcmp(job.path, "/home/3D/cube.gcode") == 0);
    assert(strcmp(job.origin_name, "PLA") == 0);
    assert(strcmp(job.target_name, "PETG") == 0);
    assert(deneb_pending_job_file_is_pending(&job));
    assert(deneb_pending_job_file_has_path(&job));
    assert(deneb_pending_job_file_has_conflict(&job));
    assert(deneb_pending_job_file_plan_action(&job, DENEB_PRINT_REQ_PREPARE,
                                              &plan) == 0);
    assert(plan.kind == DENEB_PENDING_JOB_ACTION_START);
    assert(strcmp(plan.command, DENEB_COMMAND_VERB_JOB) == 0);
    assert(strcmp(plan.path, "/home/3D/cube.gcode") == 0);
    assert(strcmp(plan.source, DENEB_PRINT_DEFAULT_JOB_SOURCE) == 0);
    assert(strcmp(plan.uuid, DENEB_PRINT_DEFAULT_JOB_UUID) == 0);
    assert(plan.tracker == 42);
    assert(plan.mark_handled_after_success);
    assert(!plan.clear_after_success);
    assert(deneb_pending_job_file_finish_action(path, &plan) == 0);
    assert(deneb_pending_job_file_load(path, &job) == 0);
    assert(deneb_pending_job_file_is_pending(&job));
    assert(deneb_pending_job_file_has_path(&job));
    assert(!deneb_pending_job_file_has_conflict(&job));

    assert(deneb_pending_job_file_plan_action(&job, DENEB_COMMAND_VERB_ABORT,
                                              &plan) == 0);
    assert(plan.kind == DENEB_PENDING_JOB_ACTION_ABORT);
    assert(strcmp(plan.command, DENEB_COMMAND_VERB_ABORT) == 0);
    assert(plan.tracker == 42);
    assert(!plan.mark_handled_after_success);
    assert(plan.clear_after_success);
    assert(deneb_pending_job_file_finish_action(path, &plan) == 0);
    assert(deneb_pending_job_file_load(path, &job) != 0);

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs("[{\"deneb_tracker\":42,\"name\":\"Cube\","
          "\"path\":\"/home/3D/cube.gcode\","
          "\"status\":\"wait_user_action\","
          "\"configuration_changes_required\":["
          "{\"type_of_change\":\"material_change\","
          "\"origin_name\":\"PLA\",\"target_name\":\"PETG\"}]}]\n", f);
    fclose(f);
    assert(deneb_pending_job_file_load(path, &job) == 0);
    assert(deneb_pending_job_file_plan_action(&job, "PAUSE", &plan) != 0);
    assert(deneb_pending_job_file_check_upload(&job, "/tmp/cube.gcode",
                                               "fallback.gcode",
                                               &upload) == 0);
    assert(upload.status == DENEB_PENDING_JOB_UPLOAD_DUPLICATE);
    assert(strcmp(upload.path, "/home/3D/cube.gcode") == 0);
    assert(strcmp(upload.display_name, "Cube") == 0);
    assert(upload.tracker == 42);
    assert(deneb_pending_job_file_check_upload(&job, "/tmp/other.gcode",
                                               "fallback.gcode",
                                               &upload) == 0);
    assert(upload.status == DENEB_PENDING_JOB_UPLOAD_BLOCKED);
    assert(strcmp(upload.path, "/home/3D/cube.gcode") == 0);
    assert(deneb_pending_job_file_same_path(job.path, "/tmp/cube.gcode"));
    assert(deneb_pending_job_file_display_name(&job, display, sizeof(display)) == 0);
    assert(strcmp(display, "Cube") == 0);
    assert(deneb_pending_job_file_display_value("/home/3D/cube.gcode",
                                                display, sizeof(display)) == 0);
    assert(strcmp(display, "cube.gcode") == 0);
    assert(deneb_pending_job_file_display_value("C:\\spool\\win.gcode",
                                                display, sizeof(display)) == 0);
    assert(strcmp(display, "win.gcode") == 0);
    assert(deneb_pending_job_file_display_value(DENEB_PRINT_NONE_VALUE,
                                                display, sizeof(display)) != 0);
    assert(display[0] == '\0');
    assert(deneb_pending_job_file_display_value("", display, sizeof(display)) != 0);
    assert(display[0] == '\0');

    assert(deneb_pending_job_file_clear(path) == 0);
    assert(deneb_pending_job_file_load(path, &job) != 0);

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs("[{\"name\":\"none\",\"path\":\"/home/3D/fallback.gcode\","
          "\"status\":\"pre_print\",\"deneb_tracker\":77}]\n", f);
    fclose(f);
    assert(deneb_pending_job_file_load(path, &job) == 0);
    assert(deneb_pending_job_file_display_name(&job, display, sizeof(display)) == 0);
    assert(strcmp(display, "fallback.gcode") == 0);

    job.tracker = -1;
    assert(!deneb_pending_job_file_is_pending(&job));
    assert(!deneb_pending_job_file_has_path(&job));
    assert(!deneb_pending_job_file_has_conflict(&job));
    assert(deneb_pending_job_file_check_upload(&job, "/tmp/fallback.gcode",
                                               "fallback.gcode",
                                               &upload) == 0);
    assert(upload.status == DENEB_PENDING_JOB_UPLOAD_CLEAR);

    assert(deneb_pending_job_file_clear(path) == 0);
}

static void test_print_history_array_reader(void)
{
    const char *path = "/tmp/deneb-print-history-test.json";
    char buf[2048];
    deneb_print_history_entry_t entry;
    FILE *f;

    remove(path);
    deneb_json_file_read_array_or_empty(path, buf, sizeof(buf));
    assert(strcmp(buf, "[]") == 0);

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs(" \n [{\"name\":\"cube.gcode\"}]\n", f);
    fclose(f);
    deneb_json_file_read_array_or_empty(path, buf, sizeof(buf));
    assert(strstr(buf, "\"cube.gcode\"") != NULL);

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs("{\"not\":\"array\"}", f);
    fclose(f);
    deneb_json_file_read_array_or_empty(path, buf, sizeof(buf));
    assert(strcmp(buf, "[]") == 0);

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs("[{\"name\":\"too-long-for-buffer\"}]", f);
    fclose(f);
    deneb_json_file_read_array_or_empty(path, buf, 8);
    assert(strcmp(buf, "[]") == 0);

    remove(path);
    memset(&entry, 0, sizeof(entry));
    entry.name = "cube\"one.gcode";
    entry.uuid = "uuid\\one";
    entry.source = "Cura";
    entry.state = "completed";
    entry.time_total = 120;
    entry.time_elapsed = 115;
    entry.progress = 99.5f;
    entry.started_at = 1700000000LL;
    entry.finished_at = 1700000120LL;
    assert(deneb_print_history_append_entry(path, &entry) == 0);
    deneb_json_file_read_array_or_empty(path, buf, sizeof(buf));
    assert(strstr(buf, "\"name\":\"cube\\\"one.gcode\"") != NULL);
    assert(strstr(buf, "\"uuid\":\"uuid\\\\one\"") != NULL);
    assert(strstr(buf, "\"state\":\"completed\"") != NULL);
    assert(strstr(buf, "\"time_total\":120") != NULL);
    assert(strstr(buf, "\"time_elapsed\":115") != NULL);
    assert(strstr(buf, "\"progress\":99.5") != NULL);
    assert(strstr(buf, "\"started_at\":\"2023-11-14T22:13:20Z\"") != NULL);
    assert(strstr(buf, "\"finished_at\":\"2023-11-14T22:15:20Z\"") != NULL);

    entry.name = "second.gcode";
    entry.uuid = "uuid-two";
    entry.state = "stopped";
    assert(deneb_print_history_append_entry(path, &entry) == 0);
    deneb_json_file_read_array_or_empty(path, buf, sizeof(buf));
    assert(strstr(buf, "\"second.gcode\"") != NULL);
    assert(strstr(buf, "\"cube\\\"one.gcode\"") != NULL);
    remove(path);
}

int main(void)
{
    test_command_parse();
    test_command_format_round_trip();
    test_command_reply_helpers();
    test_command_dispatch_policy();
    test_gcode_control_policy();
    test_pause_resume_control_policy();
    test_command_audit_policy();
    test_error_mapping();
    test_print_control_contract();
    test_pause_resume_policy();
    test_job_lifecycle_policy();
    test_job_control_policy();
    test_job_streamer_policy();
    test_runtime_diagnostics_policy();
    test_motion_sender_policy();
    test_motion_observer_policy();
    test_motion_runtime_policy();
    test_macro_runner_policy();
    test_macro_control_policy();
    test_print_state_rules();
    test_print_job_summary();
    test_json_field_helpers();
    test_status_payload_filename_resolution();
    test_gcode_command_helpers();
    test_manual_motion_helpers();
    test_buildplate_level_helpers();
    test_material_workflow_helpers();
    test_json_string_helpers();
    test_status_payload_helpers();
    test_print_profile_helpers();
    test_printer_identity_helpers();
    test_print_backend_route_contract();
    test_pending_job_metadata();
    test_pending_job_metadata_write_file();
    test_pending_job_upload_default_check();
    test_pending_job_registration_policy();
    test_print_job_file_metadata();
    test_material_catalog_helpers();
    test_status_frame();
    test_crc_and_packet();
    test_macro_safety();
    test_diagnostics_log_side_by_side_fields();
    test_abort_clears_status();
    test_job_accepts_without_blocking();
    test_service_context_policy();
    test_service_command_policy();
    test_job_poll_streams_after_preheat();
    test_pause_gates_active_job_streaming();
    test_pause_during_preheat_resumes_to_preparing();
    test_status_parser();
    test_home_distance_parser();
    test_flow_control();
    test_service_poll_motion_stub();
    test_heater_wait();
    test_sha256();
    test_motion_policy();
    test_motion_firmware_cache();
    test_pending_job_file_contract();
    test_print_history_array_reader();
    puts("deneb-printsvc tests passed");
    return 0;
}
