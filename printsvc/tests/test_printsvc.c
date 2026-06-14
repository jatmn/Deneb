/* SPDX-License-Identifier: MPL-2.0 */
#include "buildplate_level.h"
#include "command.h"
#include "command_audit.h"
#include "command_dispatch.h"
#include "command_format.h"
#include "command_reply.h"
#include "config.h"
#include "crc.h"
#include "diagnostics_export.h"
#include "diagnostics_log.h"
#include "error_map.h"
#include "flow_control.h"
#include "frame_light.h"
#include "gcode_control.h"
#include "gcode_command.h"
#include "heater_wait.h"
#include "ipc_zmq.h"
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
#include "motion_send_error.h"
#include "motion_sender.h"
#include "pause_resume.h"
#include "pause_resume_control.h"
#include "pending_job.h"
#include "pending_job_dispatch.h"
#include "pending_job_file.h"
#include "pending_job_registration.h"
#include "print_action_dispatch.h"
#include "printer_identity.h"
#include "printer_status_response.h"
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
#include "status_state.h"
#include "status_parser.h"
#include "system_language.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int buffer_contains_bytes(const uint8_t *buf, size_t len,
                                 const char *needle)
{
    size_t needle_len;

    if (!buf || !needle)
        return 0;
    needle_len = strlen(needle);
    if (needle_len == 0 || needle_len > len)
        return 0;

    for (size_t i = 0; i + needle_len <= len; i++) {
        if (memcmp(buf + i, needle, needle_len) == 0)
            return 1;
    }
    return 0;
}

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

    assert(deneb_command_parse("BOGUS<{}", &cmd) == 0);
    assert(cmd.type == DENEB_COMMAND_UNKNOWN);
    assert(strcmp(cmd.verb, "BOGUS") == 0);
    assert(deneb_command_parse("BOGUS", &cmd) < 0);
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
                                    "cloud-job-1", 60.0f, 210.0f,
                                    frame, sizeof(frame)) > 0);
    assert(deneb_command_parse(frame, &cmd) == 0);
    assert(cmd.type == DENEB_COMMAND_JOB);
    assert(strcmp(cmd.file, "/home/3D/cube.gcode") == 0);
    assert(strcmp(cmd.source, "Cura") == 0);
    assert(strcmp(cmd.uuid, "uuid-1") == 0);
    assert(strcmp(cmd.cloud_job_id, "cloud-job-1") == 0);
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
    {
        deneb_command_frame_plan_t plan;

        assert(deneb_command_plan_frame(DENEB_COMMAND_VERB_ABORT, "{}",
                                        frame, sizeof(frame), &plan) > 0);
        assert(strcmp(frame, "ABORT<{}") == 0);
        assert(plan.len == (int)strlen(frame));
        assert(!plan.has_job_path);

        assert(deneb_command_plan_frame(DENEB_COMMAND_VERB_PAUSE, NULL,
                                        frame, sizeof(frame), NULL) > 0);
        assert(strcmp(frame, "PAUSE<{}") == 0);

        assert(deneb_command_plan_frame(
                   DENEB_COMMAND_VERB_GCODE, "[\"M105\"]", frame,
                   sizeof(frame), &plan) > 0);
        assert(strcmp(frame, "GCODE<[\"M105\"]") == 0);
        assert(!plan.has_job_path);

        assert(deneb_command_plan_frame(
                   DENEB_COMMAND_VERB_JOB,
                   "{\"path\":\"/home/3D/planned.gcode\"}", frame,
                   sizeof(frame), &plan) > 0);
        assert(strcmp(frame, "JOB<{\"path\":\"/home/3D/planned.gcode\"}") == 0);
        assert(plan.has_job_path);
        assert(strcmp(plan.job_path, "/home/3D/planned.gcode") == 0);

        assert(deneb_command_plan_frame(NULL, "{}", frame,
                                        sizeof(frame), &plan) < 0);
        assert(plan.len < 0);
    }
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
    char payload[1536];

    deneb_print_service_init(&svc);
    assert(deneb_command_parse("GCODE<[\"M105\",\"G28 X Y\"]", &cmd) == 0);
    assert(deneb_gcode_control_run(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "\"status\":\"ok\"") != NULL);
    assert(deneb_flow_inflight(&svc.flow) == 2);

    deneb_print_service_init(&svc);
    assert(deneb_command_parse("GCODE<[\"M140 S40\",\"M104 S50\"]", &cmd) == 0);
    assert(deneb_gcode_control_run(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "\"status\":\"ok\"") != NULL);
    assert(svc.status.bed_t_set == 40.0f);
    assert(svc.status.head_t_set == 50.0f);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);
    assert(strcmp(svc.status.req, DENEB_PRINT_REQ_PREHEATING) == 0);
    assert(deneb_status_serialize_payload(&svc.status, payload,
                                          sizeof(payload)) > 0);
    assert(strstr(payload, "\"denebActive\":true") != NULL);
    assert(strstr(payload, "\"denebStopAllowed\":true") != NULL);

    assert(deneb_command_parse("GCODE<[\"M140 S0\",\"M104 S0\"]", &cmd) == 0);
    assert(deneb_gcode_control_run(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(svc.status.bed_t_set == 0.0f);
    assert(svc.status.head_t_set == 0.0f);
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);
    assert(deneb_status_serialize_payload(&svc.status, payload,
                                          sizeof(payload)) > 0);
    assert(strstr(payload, "\"denebActive\":false") != NULL);
    assert(strstr(payload, "\"denebStopAllowed\":false") != NULL);

    svc.status.state = DENEB_PRINT_STATE_PREPARING;
    snprintf(svc.status.req, sizeof(svc.status.req), "%s",
             DENEB_PRINT_REQ_PREHEATING);
    svc.status.bed_t_set = 0.0f;
    svc.status.head_t_set = 0.0f;
    deneb_gcode_control_refresh_manual_status(&svc);
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);
    assert(strcmp(svc.status.req, DENEB_PRINT_REQ_IDLE) == 0);

    svc.status.state = DENEB_PRINT_STATE_PREPARING;
    snprintf(svc.status.req, sizeof(svc.status.req), "%s",
             DENEB_PRINT_REQ_PREHEATING);
    svc.heater_wait.active = 1;
    deneb_gcode_control_refresh_manual_status(&svc);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);
    svc.heater_wait.active = 0;

    svc.status.state = DENEB_PRINT_STATE_COMPLETE;
    snprintf(svc.status.req, sizeof(svc.status.req), "%s",
             DENEB_PRINT_REQ_COMPLETE);
    deneb_gcode_control_refresh_manual_status(&svc);
    assert(svc.status.state == DENEB_PRINT_STATE_COMPLETE);
    assert(strcmp(svc.status.req, DENEB_PRINT_REQ_COMPLETE) == 0);

    deneb_print_service_init(&svc);
    svc.status.state = DENEB_PRINT_STATE_PRINTING;
    assert(deneb_command_parse("GCODE<[\"M140 S0\",\"M104 S0\",\"M105\",\"M105\",\"M105\"]", &cmd) == 0);
    assert(deneb_gcode_control_run(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(svc.gcode_queue_active);
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);
    svc.status.state = DENEB_PRINT_STATE_PRINTING;
    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_gcode_control_poll(&svc) == 1);
    assert(!svc.gcode_queue_active);
    assert(svc.status.bed_t_set == 0.0f);
    assert(svc.status.head_t_set == 0.0f);
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);

    deneb_print_service_init(&svc);
    assert(deneb_command_parse("GCODE<[\"M190 S55\",\"G1 X2\"]", &cmd) == 0);
    assert(deneb_gcode_control_run(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(svc.gcode_queue_active);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);
    assert(svc.status.bed_t_set == 55.0f);
    assert(deneb_command_parse("ABORT<{}", &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply,
                                              sizeof(reply)) == 0);
    assert(!svc.gcode_queue_active);
    assert(svc.gcode_queue_count == 0);
    assert(svc.status.bed_t_set == 0.0f);
    assert(svc.status.head_t_set == 0.0f);
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);

    deneb_print_service_init(&svc);
    assert(deneb_command_parse("GCODE<[\"M117 hello\",\"M190 S55\",\"M109 S205\"]", &cmd) == 0);
    assert(deneb_gcode_control_run(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "\"status\":\"ok\"") != NULL);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    assert(svc.gcode_queue_active);
    assert(svc.status.bed_t_set == 55.0f);
    assert(svc.heater_wait.active);
    assert(svc.heater_wait.wait_bed);
    assert(!svc.heater_wait.wait_head);
    assert(deneb_gcode_control_poll(&svc) == 0);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    svc.status.bed_t_cur = 55.0f;
    assert(deneb_gcode_control_poll(&svc) == 0);
    assert(deneb_flow_inflight(&svc.flow) == 2);
    assert(svc.status.head_t_set == 205.0f);
    assert(svc.heater_wait.active);
    assert(!svc.heater_wait.wait_bed);
    assert(svc.heater_wait.wait_head);
    svc.status.head_t_cur = 205.0f;
    assert(deneb_gcode_control_poll(&svc) == 1);
    assert(!svc.gcode_queue_active);

    deneb_print_service_init(&svc);
    assert(deneb_command_parse("GCODE<[\"M190 S55\",\"G1 X2\"]", &cmd) == 0);
    assert(deneb_gcode_control_run(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(svc.gcode_queue_active);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    assert(svc.gcode_queue_index == 1);
    assert(deneb_gcode_control_poll(&svc) == 0);
    assert(svc.gcode_queue_index == 1);
    svc.status.bed_t_cur = 55.0f;
    assert(deneb_gcode_control_poll(&svc) == 1);
    assert(!svc.gcode_queue_active);
    assert(deneb_flow_inflight(&svc.flow) == 2);

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = DENEB_COMMAND_GCODE;
    cmd.gcode_count = 1;
    assert(deneb_gcode_control_run(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "gcode failed") != NULL);
    assert(svc.status.error.code == DENEB_ERROR_COMMAND);

    deneb_print_service_init(&svc);
    assert(deneb_command_parse("GCODE<[\"M105\"]", &cmd) == 0);
    svc.serial_ready = 1;
    svc.serial.fd = -1;
    assert(deneb_gcode_control_run(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "gcode failed") != NULL);
    assert(svc.status.error.code == DENEB_ERROR_SERIAL);
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
    assert(!svc.pause_policy_pending);

    svc.heater_wait.active = 1;
    assert(deneb_pause_resume_control_resume(&svc, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "resume accepted") != NULL);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);

    assert(deneb_pause_resume_control_resume(&svc, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "print is not paused") != NULL);

    deneb_print_service_init(&svc);
    svc.job_active = 1;
    svc.status.state = DENEB_PRINT_STATE_PRINTING;
    svc.status.position_report_count = 1;
    svc.status.x = 120.0f;
    svc.status.y = 80.0f;
    svc.status.z = 12.0f;
    svc.status.e = 4.2f;
    svc.status.r0 = -3.0f;
    svc.status.head_t_set = 210.0f;
    assert(deneb_pause_resume_control_pause(&svc, reply, sizeof(reply)) == 0);
    assert(svc.status.state == DENEB_PRINT_STATE_PAUSED);
    assert(svc.pause_position_probe_pending);
    assert(!svc.pause_position_probe_sent);
    assert(!svc.pause_policy_pending);
    assert(!svc.paused_position_valid);
    assert(deneb_pause_resume_control_poll(&svc) == 0);
    assert(svc.pause_position_probe_sent);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_pause_resume_control_poll(&svc) == 0);
    assert(svc.pause_position_probe_pending);
    assert(svc.pause_position_probe_sent);
    assert(!svc.pause_policy_pending);
    assert(!svc.paused_position_valid);
    svc.status.head_t_set = 0.0f;
    svc.status.position_report_count = 2;
    svc.status.x = 121.0f;
    svc.status.y = 81.0f;
    assert(deneb_pause_resume_control_poll(&svc) == 0);
    assert(!svc.pause_position_probe_pending);
    assert(svc.pause_policy_pending);
    assert(svc.paused_position_valid);
    assert(svc.paused_x == 121.0f);
    assert(svc.paused_y == 81.0f);
    assert(svc.paused_nozzle_setpoint == 210.0f);
    for (int i = 0; i < 40 && deneb_pause_resume_control_busy(&svc); i++) {
        deneb_flow_clear_inflight(&svc.flow);
        assert(deneb_pause_resume_control_poll(&svc) >= 0);
    }
    assert(!deneb_pause_resume_control_busy(&svc));
    assert(deneb_pause_resume_control_resume(&svc, reply, sizeof(reply)) == 0);
    assert(svc.resume_policy_pending);
    assert(strcmp(svc.resume_policy.commands[0], "M104 S210") == 0);
    assert(strcmp(svc.resume_policy.commands[1], "M109 S210") == 0);
    assert(svc.status.head_t_set == 210.0f);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);
    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_pause_resume_control_poll(&svc) == 0);
    assert(svc.resume_policy_index == 1);
    assert(svc.heater_wait.active);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);
    assert(deneb_pause_resume_control_poll(&svc) == 0);
    assert(svc.resume_policy_index == 1);

    deneb_print_service_init(&svc);
    svc.job_active = 1;
    svc.status.state = DENEB_PRINT_STATE_PAUSED;
    svc.paused_position_valid = 1;
    svc.paused_x = 120.0f;
    svc.paused_y = 80.0f;
    svc.paused_z = 12.0f;
    assert(deneb_pause_resume_control_resume(&svc, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "missing pause nozzle target") != NULL);
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
    status.position_report_count = 8;
    status.finish_drain_ticks = 9;
    status.finish_stable_reports = 3;
    snprintf(status.flow_last_response, sizeof(status.flow_last_response),
             "Resend: \"7\"");
    snprintf(status.file, sizeof(status.file), "cube\"one.gcode");
    snprintf(status.source, sizeof(status.source), "USB\\front");
    snprintf(status.firmware, sizeof(status.firmware), "Apr 30 2020 12:57:04");
    snprintf(status.machine_type, sizeof(status.machine_type), "E2");
    status.pcb_id = 4;
    status.pcb_id_valid = true;
    status.topcap_present = true;
    status.topcap_t_cur = 32.5f;
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
    assert(strstr(frame, "\"flowLastResponse\":\"Resend: \\\"7\\\"\"") != NULL);
    assert(strstr(frame, "\"jobQueueDepth\":1") != NULL);
    assert(strstr(frame, "\"commandLatencyMs\":9") != NULL);
    assert(strstr(frame, "\"plannerStarvationCount\":7") != NULL);
    assert(strstr(frame, "\"positionReportCount\":8") != NULL);
    assert(strstr(frame, "\"finishDrainTicks\":9") != NULL);
    assert(strstr(frame, "\"finishStableReports\":3") != NULL);
    assert(strstr(frame, "\"denebState\":\"idle\"") != NULL);
    assert(strstr(frame, "\"denebActive\":false") != NULL);
    assert(strstr(frame, "\"denebStopAllowed\":false") != NULL);
    assert(strstr(frame, "\"denebErrorKey\":\"thermal_fault\"") != NULL);
    assert(strstr(frame, "\"denebErrorCategory\":\"thermal\"") != NULL);
    assert(strstr(frame, "\"denebErrorDetail\":\"heater \\\"fault\\\"\"") != NULL);
    assert(strstr(frame, "\"firmware\":\"Apr 30 2020 12:57:04\"") != NULL);
    assert(strstr(frame, "\"machineType\":\"E2\"") != NULL);
    assert(strstr(frame, "\"pcbId\":4") != NULL);
    assert(strstr(frame, "\"pcbIdValid\":true") != NULL);
    assert(strstr(frame, "\"topcapIsPresent\":true") != NULL);
    assert(strstr(frame, "\"topcapTemperature\":32.5") != NULL);
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
    assert(deneb_error_line_is_recoverable_serial(
        "Error:Line Number is not Last Line Number+1"));
    assert(deneb_error_line_is_recoverable_serial("Error:Bad checksum"));
    assert(deneb_error_line_is_recoverable_serial(
        "Error:ProtoError:Sequence number is unexpected (received, expected): 0,1"));
    assert(!deneb_error_line_is_recoverable_serial("Error:Printer halted"));

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
                              "cloud-job-1", 60.0f, 205.0f);
    assert(status.state == DENEB_PRINT_STATE_PREPARING);
    assert(strcmp(status.req, DENEB_PRINT_REQ_PREPARE) == 0);
    assert(strcmp(status.file, "/tmp/cube.gcode") == 0);
    assert(strcmp(status.source, DENEB_PRINT_USB_JOB_SOURCE) == 0);
    assert(strcmp(status.uuid, "uuid-1") == 0);
    assert(strcmp(status.cloud_job_id, "cloud-job-1") == 0);
    assert(status.bed_t_set == 60.0f);
    assert(status.head_t_set == 205.0f);

    deneb_job_lifecycle_streaming(&status);
    assert(status.state == DENEB_PRINT_STATE_PRINTING);
    assert(strcmp(status.req, DENEB_COMMAND_VERB_JOB) == 0);

    deneb_job_lifecycle_complete(&status);
    assert(status.state == DENEB_PRINT_STATE_COMPLETE);
    assert(strcmp(status.req, DENEB_PRINT_REQ_COMPLETE) == 0);
    assert(strcmp(status.file, DENEB_PRINT_NONE_VALUE) == 0);
    assert(status.source[0] == '\0');
    assert(status.uuid[0] == '\0');
    assert(status.cloud_job_id[0] == '\0');
    assert(status.bed_t_set == 0.0f);
    assert(status.head_t_set == 0.0f);

    deneb_job_lifecycle_start(&status, "/tmp/cube.gcode", "WEB_API", "",
                              "", 0.0f, 0.0f);
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

static int flow_has_retained_slot_text(const deneb_flow_control_t *flow)
{
    if (!flow)
        return 0;
    for (size_t i = 0; i < DENEB_FLOW_WINDOW; i++) {
        if (flow->slots[i].occupied || flow->slots[i].command[0] != '\0')
            return 1;
    }
    return 0;
}

static const char *flow_command_by_send_order(const deneb_flow_control_t *flow,
                                              unsigned int send_order)
{
    if (!flow)
        return NULL;
    for (size_t i = 0; i < DENEB_FLOW_WINDOW; i++) {
        if (flow->slots[i].occupied &&
            flow->slots[i].send_order == send_order)
            return flow->slots[i].command;
    }
    return NULL;
}

static void drive_service_job_prepare(deneb_print_service_t *svc)
{
    assert(svc != NULL);
    for (int guard = 0; svc->job_prepare_stage != 0 && guard < 128; guard++) {
        if (svc->heater_wait.active) {
            if (svc->heater_wait.wait_bed)
                svc->status.bed_t_cur = svc->heater_wait.bed_target;
            if (svc->heater_wait.wait_head)
                svc->status.head_t_cur = svc->heater_wait.head_target;
        }
        deneb_flow_clear_inflight(&svc->flow);
        assert(deneb_print_service_poll_job(svc) >= 0);
    }
    assert(svc->job_prepare_stage == 0);
    deneb_flow_clear_inflight(&svc->flow);
}

static void test_job_control_policy(void)
{
    const char *path = "/tmp/deneb-printsvc-job-control-test.gcode";
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char frame[512];
    char reply[256];
    uint8_t packet[DENEB_PRINTSVC_MAX_GCODE_LINE + 64];
    size_t written = 0;
    uint8_t seq = 0;
    int pipefd[2] = {-1, -1};
    FILE *f = fopen(path, "wb");

    assert(f != NULL);
    fputs("G1 X1\n", f);
    fclose(f);

    deneb_print_service_init(&svc);
    assert(deneb_command_parse("JOB<{}", &cmd) == 0);
    assert(deneb_job_control_accept(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "missing job file") != NULL);

    svc.abort_requested = 1;
    assert(deneb_job_control_abort(&svc, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "no active print to abort") != NULL);
    assert(!svc.abort_requested);
    assert(deneb_flow_inflight(&svc.flow) == 0);

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs("M190 S60\nG280\nG1 X10\n", f);
    fclose(f);
    snprintf(frame, sizeof(frame), "JOB<{\"file\":\"%s\"}", path);
    assert(deneb_command_parse(frame, &cmd) == 0);
    assert(deneb_job_control_accept(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "job accepted") != NULL);
    assert(svc.job_active);
    assert(svc.job_stream.has_prime_cmd);
    assert(svc.job_prepare_stage == 1);
    assert(deneb_job_control_abort(&svc, reply, sizeof(reply)) == 0);

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs("G1 X1\n", f);
    fclose(f);
    deneb_print_service_init(&svc);
    snprintf(frame, sizeof(frame),
             "JOB<{\"file\":\"%s\",\"source\":\"Cura\",\"uuid\":\"job-1\","
             "\"bedTset\":60,\"headTset\":200}",
             path);
    assert(deneb_command_parse(frame, &cmd) == 0);
    svc.status.bed_t_cur = 29.0f;
    svc.status.head_t_cur = 31.7f;
    assert(deneb_job_control_accept(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "job accepted") != NULL);
    assert(svc.job_active);
    assert(!svc.abort_requested);
    assert(svc.heater_wait.active);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);
    assert(!svc.status.fault);
    assert(svc.status.bed_t_cur == 29.0f);
    assert(svc.status.head_t_cur == 31.7f);
    assert(strcmp(svc.status.file, path) == 0);
    assert(strcmp(svc.status.source, "Cura") == 0);
    assert(strcmp(svc.status.uuid, "job-1") == 0);

    assert(deneb_job_control_accept(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "job already active") != NULL);

    assert(deneb_job_control_abort(&svc, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "abort accepted") != NULL);
    assert(!svc.job_active);
    assert(!svc.abort_requested);
    assert(!svc.heater_wait.active);
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);
    assert(!svc.status.fault);
    assert(strcmp(svc.status.file, DENEB_PRINT_NONE_VALUE) == 0);

    deneb_flow_init(&svc.flow);
    for (int i = 0; i < 7; i++) {
        assert(deneb_flow_prepare_packet(&svc.flow, "M105", packet,
                                         sizeof(packet), &written, &seq) == 0);
    }
    assert(deneb_flow_inflight(&svc.flow) == 7);
    assert(seq == 6);
    svc.status.fault = true;
    svc.status.error = deneb_error_make(DENEB_ERROR_MARLIN_FAULT, "stale fault");
    assert(deneb_job_control_accept(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(deneb_flow_inflight(&svc.flow) == 0);
    assert(deneb_flow_prepare_packet(&svc.flow, "M105", packet,
                                     sizeof(packet), &written, &seq) == 0);
    assert(seq == 7);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    assert(!svc.status.fault);
    assert(svc.status.error.code == DENEB_ERROR_NONE);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);
    assert(deneb_job_control_abort(&svc, reply, sizeof(reply)) == 0);

    deneb_flow_init(&svc.flow);
    for (int i = 0; i < DENEB_FLOW_WINDOW; i++) {
        assert(deneb_flow_prepare_packet(&svc.flow, "M105", packet,
                                         sizeof(packet), &written, &seq) == 0);
    }
    assert(deneb_flow_inflight(&svc.flow) == DENEB_FLOW_WINDOW);
    assert(seq == DENEB_FLOW_WINDOW - 1);
    svc.status.state = DENEB_PRINT_STATE_PRINTING;
    assert(deneb_job_control_abort(&svc, reply, sizeof(reply)) == 0);
    assert(deneb_flow_inflight(&svc.flow) == 0);
    assert(!flow_has_retained_slot_text(&svc.flow));
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);
    assert(deneb_flow_prepare_packet(&svc.flow, "M105", packet,
                                     sizeof(packet), &written, &seq) == 0);
    assert(seq == DENEB_FLOW_WINDOW);

    svc.status.state = DENEB_PRINT_STATE_PRINTING;
    snprintf(svc.status.file, sizeof(svc.status.file), "serial.gcode");
    assert(pipe(pipefd) == 0);
    svc.serial.fd = pipefd[1];
    svc.serial_ready = 1;
    deneb_flow_init(&svc.flow);
    assert(deneb_job_control_abort(&svc, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "abort accepted") != NULL);
    assert(svc.abort_cleanup_pending);
    assert(!svc.abort_requested);
    assert(svc.status.state == DENEB_PRINT_STATE_ABORTING);
    assert(strcmp(svc.status.file, "serial.gcode") == 0);
    assert(deneb_job_control_poll_abort_cleanup(&svc) == 0);
    assert(svc.status.state == DENEB_PRINT_STATE_ABORTING);
    for (size_t i = 0;
         i < svc.abort_cleanup_policy.count + 2 && svc.abort_cleanup_pending;
         i++) {
        deneb_flow_clear_inflight(&svc.flow);
        if (deneb_job_control_poll_abort_cleanup(&svc) == 1)
            break;
    }
    assert(!svc.abort_cleanup_pending);
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);
    assert(strcmp(svc.status.file, DENEB_PRINT_NONE_VALUE) == 0);
    assert(!flow_has_retained_slot_text(&svc.flow));
    assert(svc.job_stream.line_number == 0);
    close(pipefd[0]);
    close(pipefd[1]);
    svc.serial.fd = -1;
    svc.serial_ready = 0;

    deneb_print_service_init(&svc);
    svc.serial_ready = 0;
    svc.finish_cleanup_pending = 1;
    svc.job_active = 1;
    deneb_motion_policy_finish(&svc.finish_cleanup_policy);
    svc.finish_cleanup_index = 0;
    svc.flow.controller_free_space_known = 1;
    svc.flow.controller_free_space = 0;
    svc.status.state = DENEB_PRINT_STATE_PRINTING;
    snprintf(svc.status.file, sizeof(svc.status.file), "finish.gcode");
    assert(deneb_job_control_poll_finish_cleanup(&svc) == 0);
    assert(svc.finish_cleanup_pending);
    assert(svc.finish_cleanup_index == 0);
    assert(svc.status.state == DENEB_PRINT_STATE_PRINTING);
    svc.flow.controller_free_space = 4;
    assert(deneb_job_control_poll_finish_cleanup(&svc) == 0);
    assert(svc.finish_cleanup_index > 0);
    while (svc.finish_cleanup_index < svc.finish_cleanup_policy.count) {
        deneb_flow_clear_inflight(&svc.flow);
        assert(deneb_job_control_poll_finish_cleanup(&svc) == 0);
    }
    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_job_control_poll_finish_cleanup(&svc) == 1);
    assert(!svc.finish_cleanup_pending);
    assert(!svc.job_active);
    assert(svc.status.state == DENEB_PRINT_STATE_COMPLETE);
    assert(strcmp(svc.status.file, DENEB_PRINT_NONE_VALUE) == 0);
    assert(!flow_has_retained_slot_text(&svc.flow));
    assert(svc.job_stream.line_number == 0);

    svc.status.state = DENEB_PRINT_STATE_PRINTING;
    snprintf(svc.status.file, sizeof(svc.status.file), "stale.gcode");
    deneb_flow_init(&svc.flow);
    svc.serial_ready = 1;
    assert(deneb_job_control_abort(&svc, reply, sizeof(reply)) == 0);
    assert(strstr(reply, "abort accepted") != NULL);
    assert(deneb_job_control_poll_abort_cleanup(&svc) < 0);
    assert(!svc.abort_requested);
    assert(!svc.heater_wait.active);
    assert(svc.status.state == DENEB_PRINT_STATE_ERROR);
    assert(svc.status.error.code == DENEB_ERROR_SERIAL);

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
    int job_prepare_stage = 1;
    size_t job_prepare_index = 0;
    size_t job_startup_index = 0;
    int abort_requested = 0;
    int finish_cleanup_pending = 0;
    deneb_motion_policy_t finish_cleanup_policy;
    size_t finish_cleanup_index = 0;
    int serial_ready = 0;
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
                              "streamer-uuid", "", 60.0f, 200.0f);
    deneb_heater_wait_start(&wait, status.bed_t_set, status.head_t_set, 1.0f);
    assert(deneb_gcode_stream_open(&stream, path) == 0);

    streamer.status = &status;
    streamer.flow = &flow;
    streamer.stream = &stream;
    streamer.heater_wait = &wait;
    streamer.serial = &serial;
    streamer.serial_ready = &serial_ready;
    streamer.job_active = &job_active;
    streamer.job_prepare_stage = &job_prepare_stage;
    streamer.job_prepare_index = &job_prepare_index;
    streamer.job_startup_index = &job_startup_index;
    streamer.abort_requested = &abort_requested;
    streamer.finish_cleanup_pending = &finish_cleanup_pending;
    streamer.finish_cleanup_policy = &finish_cleanup_policy;
    streamer.finish_cleanup_index = &finish_cleanup_index;
    streamer.planner_starvation_count = &starvation;

    assert(deneb_job_streamer_poll(NULL) < 0);
    job_active = 0;
    abort_requested = 1;
    wait.active = 1;
    assert(deneb_job_streamer_poll(&streamer) == 0);
    assert(!abort_requested);
    assert(!wait.active);

    job_active = 1;
    job_prepare_stage = 1;
    job_prepare_index = 0;
    job_startup_index = 0;
    abort_requested = 0;
    deneb_heater_wait_start(&wait, status.bed_t_set, status.head_t_set, 1.0f);
    assert(deneb_job_streamer_poll(&streamer) == 1);
    assert(status.state == DENEB_PRINT_STATE_PREPARING);
    assert(stream.line_number == 0);
    assert(deneb_flow_inflight(&flow) == 1);

    deneb_flow_clear_inflight(&flow);
    assert(deneb_job_streamer_poll(&streamer) == 1);
    deneb_flow_clear_inflight(&flow);
    assert(deneb_job_streamer_poll(&streamer) == 0);
    status.bed_t_cur = 60.0f;
    status.head_t_cur = 200.0f;
    assert(deneb_job_streamer_poll(&streamer) == 0);
    assert(deneb_job_streamer_poll(&streamer) == 1);
    assert(deneb_flow_inflight(&flow) == 3);
    for (int guard = 0; job_prepare_stage != 0 && guard < 32; guard++) {
        deneb_flow_clear_inflight(&flow);
        assert(deneb_job_streamer_poll(&streamer) >= 0);
    }
    assert(job_prepare_stage == 0);
    deneb_flow_clear_inflight(&flow);
    assert(deneb_job_streamer_poll(&streamer) == 1);
    assert(status.state == DENEB_PRINT_STATE_PRINTING);
    assert(stream.line_number == 1);
    assert(starvation == 1);
    assert(deneb_flow_inflight(&flow) == 1);

    assert(deneb_job_streamer_poll(&streamer) == 0);
    assert(job_active);
    assert(!finish_cleanup_pending);
    assert(finish_cleanup_index == 0);
    assert(status.state == DENEB_PRINT_STATE_PRINTING);
    deneb_flow_clear_inflight(&flow);
    assert(deneb_flow_inflight(&flow) == 0);
    assert(deneb_job_streamer_poll(&streamer) == 1);
    assert(job_active);
    assert(finish_cleanup_pending);
    assert(finish_cleanup_policy.count > 0);
    assert(finish_cleanup_index == 0);
    assert(status.state == DENEB_PRINT_STATE_PRINTING);

    deneb_status_init(&status);
    deneb_flow_init(&flow);
    flow.controller_free_space_known = 1;
    flow.controller_free_space = 0;
    deneb_heater_wait_init(&wait);
    assert(deneb_gcode_stream_open(&stream, path) == 0);
    deneb_job_lifecycle_start(&status, path, DENEB_PRINT_USB_JOB_SOURCE,
                              "stream-backpressure", "", 0.0f, 0.0f);
    job_active = 1;
    job_prepare_stage = 0;
    job_prepare_index = 0;
    job_startup_index = 0;
    abort_requested = 0;
    finish_cleanup_pending = 0;
    serial_ready = 0;
    assert(deneb_job_streamer_poll(&streamer) == 0);
    assert(job_active);
    assert(!finish_cleanup_pending);
    assert(status.state != DENEB_PRINT_STATE_ERROR);
    assert(stream.line_number == 0);
    assert(deneb_flow_inflight(&flow) == 0);

    deneb_status_init(&status);
    deneb_flow_init(&flow);
    deneb_heater_wait_init(&wait);
    assert(deneb_gcode_stream_open(&stream, path) == 0);
    deneb_job_lifecycle_start(&status, path, DENEB_PRINT_USB_JOB_SOURCE,
                              "stream-send-fault", "", 0.0f, 0.0f);
    job_active = 1;
    abort_requested = 0;
    finish_cleanup_pending = 0;
    wait.active = 1;
    serial_ready = 1;
    assert(deneb_job_streamer_poll(&streamer) == -1);
    assert(!job_active);
    assert(!finish_cleanup_pending);
    assert(!wait.active);
    assert(status.state == DENEB_PRINT_STATE_ERROR);
    assert(status.error.code == DENEB_ERROR_SERIAL);
    assert(strstr(status.error.detail,
                  "job stream send failed: serial_write") != NULL);

    deneb_flow_init(&flow);
    assert(deneb_gcode_stream_open(&stream, path) == 0);
    deneb_job_lifecycle_start(&status, path, DENEB_PRINT_USB_JOB_SOURCE,
                              "stream-abort", "", 0.0f, 0.0f);
    status.time_total = 120;
    status.time_left = 90;
    job_active = 1;
    abort_requested = 1;
    finish_cleanup_pending = 0;
    wait.active = 1;
    assert(deneb_job_streamer_poll(&streamer) == -2);
    assert(!job_active);
    assert(!abort_requested);
    assert(!finish_cleanup_pending);
    assert(!wait.active);
    assert(status.state == DENEB_PRINT_STATE_IDLE);
    assert(strcmp(status.file, DENEB_PRINT_NONE_VALUE) == 0);
    assert(status.time_total == 0);
    assert(status.time_left == 0);

    f = fopen(path, "wb");
    assert(f != NULL);
    fclose(f);
    deneb_status_init(&status);
    deneb_flow_init(&flow);
    deneb_heater_wait_init(&wait);
    assert(deneb_gcode_stream_open(&stream, path) == 0);
    deneb_job_lifecycle_start(&status, path, DENEB_PRINT_USB_JOB_SOURCE,
                              "finish-fault", "", 0.0f, 0.0f);
    job_active = 1;
    abort_requested = 0;
    finish_cleanup_pending = 0;
    wait.active = 1;
    serial_ready = 1;
    assert(deneb_job_streamer_poll(&streamer) == 1);
    assert(job_active);
    assert(finish_cleanup_pending);
    assert(finish_cleanup_policy.count > 0);
    assert(finish_cleanup_index == 0);
    assert(!wait.active);
    assert(status.state == DENEB_PRINT_STATE_PRINTING);

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs("M190 S55\n", f);
    fputs("G1 X2\n", f);
    fclose(f);
    deneb_status_init(&status);
    deneb_flow_init(&flow);
    deneb_heater_wait_init(&wait);
    assert(deneb_gcode_stream_open(&stream, path) == 0);
    deneb_job_lifecycle_start(&status, path, DENEB_PRINT_USB_JOB_SOURCE,
                              "stream-wait", "", 0.0f, 0.0f);
    status.bed_t_cur = 25.0f;
    job_active = 1;
    job_prepare_stage = 0;
    job_prepare_index = 0;
    job_startup_index = 0;
    abort_requested = 0;
    finish_cleanup_pending = 0;
    serial_ready = 0;
    assert(deneb_job_streamer_poll(&streamer) == 1);
    assert(wait.active);
    assert(wait.wait_bed);
    assert(!wait.wait_head);
    assert(status.state == DENEB_PRINT_STATE_PREPARING);
    assert(status.bed_t_set == 55.0f);
    assert(stream.line_number == 1);
    assert(deneb_flow_inflight(&flow) == 1);
    assert(deneb_job_streamer_poll(&streamer) == 0);
    assert(stream.line_number == 1);
    status.bed_t_cur = 55.0f;
    assert(deneb_job_streamer_poll(&streamer) == 1);
    assert(!wait.active);
    assert(status.state == DENEB_PRINT_STATE_PRINTING);
    assert(stream.line_number == 2);

    remove(path);
}

static void test_gcode_stream_rewrite_policy(void)
{
    const char *path = "/tmp/deneb-printsvc-gcode-rewrite-test.gcode";
    deneb_gcode_stream_t stream;
    char line[DENEB_PRINTSVC_MAX_GCODE_LINE];
    int wait_bed = 0;
    int wait_nozzle = 0;
    float target = 0.0f;
    FILE *f = fopen(path, "wb");

    assert(f != NULL);
    fputs("M117 printing\n", f);
    fputs("M109 S205\n", f);
    fputs("G280 S1\n", f);
    fclose(f);

    assert(deneb_gcode_stream_open(&stream, path) == 0);
    assert(deneb_gcode_stream_next(&stream, line, sizeof(line)) == 1);
    assert(strcmp(line, "M104 S205") == 0);
    assert(deneb_gcode_stream_last_wait(&stream, &wait_bed, &wait_nozzle,
                                        &target) == 1);
    assert(!wait_bed);
    assert(wait_nozzle);
    assert(target == 205.0f);
    assert(deneb_gcode_stream_next(&stream, line, sizeof(line)) == 1);
    assert(strcmp(line, "G92 E-16.5") == 0);
    assert(deneb_gcode_stream_last_wait(&stream, &wait_bed, &wait_nozzle,
                                        &target) == 0);
    assert(!wait_bed);
    assert(!wait_nozzle);
    assert(deneb_gcode_stream_next(&stream, line, sizeof(line)) == 0);
    deneb_gcode_stream_close(&stream);

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs("G280\n", f);
    fclose(f);
    assert(deneb_gcode_stream_open(&stream, path) == 0);
    assert(deneb_gcode_stream_next(&stream, line, sizeof(line)) == 1);
    assert(strcmp(line, "G0 Z2 F9000") == 0);
    assert(deneb_gcode_stream_next(&stream, line, sizeof(line)) == 1);
    assert(strcmp(line, "G10 S-6.5 F1500") == 0);
    assert(stream.line_number == 1);
    deneb_gcode_stream_close(&stream);
    assert(stream.fd == -1);
    assert(stream.path[0] == '\0');
    assert(stream.line_number == 1);
    assert(stream.pending_count == 0);
    assert(stream.pending_index == 0);
    assert(stream.read_len == 0);
    assert(stream.read_pos == 0);
    assert(stream.read_buf[0] == '\0');

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
    assert(status.flow_reject == 2);
    assert(status.job_queue_depth == 1);
    assert(status.job_line_number == 42);
    assert(status.planner_starvation_count == 3);

    deneb_runtime_diagnostics_refresh(&status, &flow, 0, 42, 4);
    assert(status.job_queue_depth == 0);
    assert(status.job_line_number == 0);
    assert(status.planner_starvation_count == 4);
}

static void format_sync_packet(char code, uint8_t sequence, uint8_t free_space,
                               char out[8])
{
    uint8_t crc;

    snprintf(out, 8, "%c%02X%02X00", code, sequence, free_space);
    crc = deneb_crc8((const uint8_t *)out, 5);
    snprintf(out + 5, 3, "%02X", crc);
}

static void test_motion_sender_policy(void)
{
    deneb_flow_control_t flow;
    deneb_serial_transport_t serial;
    deneb_motion_policy_t policy;
    uint8_t buf[256];
    int pipefd[2] = {-1, -1};
    ssize_t n;

    deneb_flow_init(&flow);
    memset(&serial, 0, sizeof(serial));
    serial.fd = -1;

    assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, NULL) < 0);
    assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, "") < 0);
    assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, "M105") == 0);
    assert(deneb_flow_inflight(&flow) == 1);
    assert(flow.sent_count == 1);
    assert(deneb_motion_sender_resend_sequence(
               &flow, &serial, 0, DENEB_FLOW_INITIAL_SEQUENCE) == 0);
    assert(deneb_motion_sender_resend_sequence(&flow, &serial, 0, 7) ==
           DENEB_MOTION_SEND_FLOW_FULL);
    assert(deneb_motion_sender_send_gcode(&flow, &serial, 1, "M104 S0") ==
           DENEB_MOTION_SEND_SERIAL);
    assert(deneb_motion_send_error_code(DENEB_MOTION_SEND_SERIAL) ==
           DENEB_ERROR_SERIAL);
    assert(deneb_motion_send_error_code(DENEB_MOTION_SEND_FLOW_FULL) ==
           DENEB_ERROR_COMMAND);

    deneb_flow_init(&flow);
    deneb_motion_policy_abort(&policy);
    assert(deneb_motion_sender_apply_policy(&flow, &serial, 0, &policy) == 0);
    assert(flow.sent_count == policy.count);
    assert(deneb_flow_inflight(&flow) == policy.count);
    assert(deneb_motion_sender_apply_policy(&flow, &serial, 0, NULL) ==
           DENEB_MOTION_SEND_INVALID);

    deneb_flow_init(&flow);
    assert(deneb_motion_sender_apply_policy(&flow, &serial, 1, &policy) ==
           DENEB_MOTION_SEND_SERIAL);

    deneb_flow_init(&flow);
    for (size_t i = 0; i < DENEB_FLOW_WINDOW; i++)
        assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, "M105") ==
               0);
    assert(deneb_motion_sender_apply_policy(&flow, &serial, 0, &policy) ==
           DENEB_MOTION_SEND_FLOW_FULL);

    deneb_flow_init(&flow);
    assert(pipe(pipefd) == 0);
    serial.fd = pipefd[1];
    assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, "M105") == 0);
    assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, "M114") == 0);
    assert(deneb_motion_sender_resend_pending(&flow, &serial, 1) == 0);
    n = read(pipefd[0], buf, sizeof(buf));
    assert(n > 16);
    assert(buf[0] == 0xff && buf[1] == 0xff);
    close(pipefd[0]);
    close(pipefd[1]);
    serial.fd = -1;
}

typedef struct {
    int abort_after_sends;
    int fail_wait;
    int fail_send;
    int fail_send_rc;
    int fail_poll;
    int fail_poll_rc;
    int fail_heater_wait;
    int sends;
    int polls;
    int heater_waits;
    int last_wait_bed;
    int last_wait_nozzle;
    float last_wait_target;
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
    assert(timeout_ms == DENEB_MACRO_RUNNER_WINDOW_TIMEOUT_MS);
    return fake->fail_wait ? -1 : 0;
}

static int macro_runner_fake_send(void *ctx, const char *line)
{
    macro_runner_fake_t *fake = (macro_runner_fake_t *)ctx;
    if (fake->fail_send)
        return fake->fail_send_rc ? fake->fail_send_rc : -1;
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
    if (fake->fail_poll)
        return fake->fail_poll_rc ? fake->fail_poll_rc : -1;
    return 0;
}

static int macro_runner_fake_heater_wait(void *ctx, int wait_bed,
                                         int wait_nozzle, float target,
                                         long long timeout_ms)
{
    macro_runner_fake_t *fake = (macro_runner_fake_t *)ctx;
    assert(timeout_ms == DENEB_MACRO_RUNNER_HEATER_TIMEOUT_MS);
    fake->heater_waits++;
    fake->last_wait_bed = wait_bed;
    fake->last_wait_nozzle = wait_nozzle;
    fake->last_wait_target = target;
    return fake->fail_heater_wait ? -1 : 0;
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
    assert(strcmp(status.flow_last_response, "Resend: 0") == 0);

    assert(deneb_motion_observer_handle_line(
               &status, &wait, &flow,
               "Error:ProtoError:Sequence number is unexpected (received, expected): 0,7",
               &resend) == 3);
    assert(strcmp(status.flow_last_response,
                  "Error:ProtoError:Sequence number is unexpected (received, expected): 0,7") == 0);
}

static void test_serial_transport_buffers_partial_lines(void)
{
    deneb_serial_transport_t serial;
    int pipefd[2];
    char line[DENEB_PRINTSVC_SERIAL_LINE];

    memset(&serial, 0, sizeof(serial));
    serial.fd = -1;
    assert(pipe(pipefd) == 0);
    assert(fcntl(pipefd[0], F_SETFL, O_NONBLOCK) == 0);
    assert(write(pipefd[1], "o00", 3) == 3);
    serial.fd = pipefd[0];
    assert(deneb_serial_read_line(&serial, line, sizeof(line)) == 0);
    assert(write(pipefd[1], "05", 2) == 2);
    assert(deneb_serial_read_line(&serial, line, sizeof(line)) == 0);
    assert(write(pipefd[1], "A6\n", 3) == 3);
    assert(deneb_serial_read_line(&serial, line, sizeof(line)) == 7);
    assert(strcmp(line, "o0005A6") == 0);
    close(pipefd[0]);
    close(pipefd[1]);
    serial.fd = -1;
}

static void test_serial_transport_retries_nonblocking_writes(void)
{
    deneb_serial_transport_t serial;
    int pipefd[2];
    uint8_t fill[4096];
    uint8_t payload[512];
    pid_t child;

    memset(&serial, 0, sizeof(serial));
    serial.fd = -1;
    memset(fill, 0x5a, sizeof(fill));
    memset(payload, 0xa5, sizeof(payload));

    assert(pipe(pipefd) == 0);
    assert(fcntl(pipefd[1], F_SETFL, O_NONBLOCK) == 0);
    while (write(pipefd[1], fill, sizeof(fill)) > 0) {
    }

    child = fork();
    assert(child >= 0);
    if (child == 0) {
        uint8_t drain[8192];
        close(pipefd[1]);
        assert(fcntl(pipefd[0], F_SETFL, O_NONBLOCK) == 0);
        usleep(50000);
        while (read(pipefd[0], drain, sizeof(drain)) > 0) {
        }
        close(pipefd[0]);
        _exit(0);
    }

    serial.fd = pipefd[1];
    assert(deneb_serial_write_all(&serial, payload, sizeof(payload)) == 0);
    close(pipefd[0]);
    close(pipefd[1]);
    serial.fd = -1;
    assert(waitpid(child, NULL, 0) == child);
}

static void test_motion_runtime_policy(void)
{
    deneb_status_t status;
    deneb_heater_wait_t wait;
    deneb_flow_control_t flow;
    deneb_serial_transport_t serial;
    deneb_motion_runtime_t runtime;
    int pipefd[2];
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
    runtime.allow_sequence_resync = 0;

    assert(deneb_motion_runtime_poll(NULL) < 0);
    assert(deneb_motion_runtime_poll(&runtime) == 0);
    serial_ready = 1;
    deneb_motion_runtime_close(&runtime);
    assert(serial_ready == 0);

    deneb_status_init(&status);
    status.state = DENEB_PRINT_STATE_PRINTING;
    deneb_flow_init(&flow);
    assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, "M105") == 0);
    assert(pipe(pipefd) == 0);
    assert(write(pipefd[1], "Resend: 0\n", 10) == 10);
    close(pipefd[1]);
    serial.fd = pipefd[0];
    serial_ready = 1;
    assert(deneb_motion_runtime_poll(&runtime) < 0);
    assert(status.state == DENEB_PRINT_STATE_ERROR);
    assert(status.error.code == DENEB_ERROR_SERIAL);
    close(pipefd[0]);
    serial.fd = -1;
    serial_ready = 0;

    deneb_status_init(&status);
    deneb_flow_init(&flow);
    assert(pipe(pipefd) == 0);
    assert(write(pipefd[1], "Resend: 7\n", 10) == 10);
    close(pipefd[1]);
    serial.fd = pipefd[0];
    serial_ready = 1;
    assert(deneb_motion_runtime_poll(&runtime) == 1);
    assert(status.state == DENEB_PRINT_STATE_IDLE);
    assert(!status.fault);
    close(pipefd[0]);
    serial.fd = -1;
    serial_ready = 0;

    deneb_status_init(&status);
    status.state = DENEB_PRINT_STATE_COMPLETE;
    snprintf(status.req, sizeof(status.req), "%s", DENEB_PRINT_REQ_COMPLETE);
    deneb_flow_init(&flow);
    assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, "M105") == 0);
    assert(deneb_flow_inflight(&flow) == 1);
    {
        const char *proto_error =
            "Error:ProtoError:Sequence number is unexpected (received, expected): 0,7\n";
        assert(pipe(pipefd) == 0);
        assert(write(pipefd[1], proto_error, strlen(proto_error)) ==
               (ssize_t)strlen(proto_error));
        close(pipefd[1]);
        serial.fd = pipefd[0];
        serial_ready = 1;
        runtime.allow_sequence_resync = 0;
        assert(deneb_motion_runtime_poll(&runtime) == 1);
        assert(status.state == DENEB_PRINT_STATE_COMPLETE);
        assert(status.error.code == DENEB_ERROR_NONE);
        assert(flow.next_sequence == 7);
        assert(deneb_flow_inflight(&flow) == 0);
        close(pipefd[0]);
        serial.fd = -1;
        serial_ready = 0;
    }

    deneb_status_init(&status);
    deneb_flow_init(&flow);
    assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, "M105") == 0);
    assert(deneb_flow_inflight(&flow) == 1);
    {
        const char *proto_error =
            "Error:ProtoError:Sequence number is unexpected (received, expected): 0,7\n";
        assert(pipe(pipefd) == 0);
        assert(write(pipefd[1], proto_error, strlen(proto_error)) ==
               (ssize_t)strlen(proto_error));
        close(pipefd[1]);
        serial.fd = pipefd[0];
        serial_ready = 1;
        runtime.allow_sequence_resync = 0;
        assert(deneb_motion_runtime_poll(&runtime) == 1);
        assert(status.state == DENEB_PRINT_STATE_IDLE);
        assert(flow.next_sequence == 7);
        assert(deneb_flow_inflight(&flow) == 0);
        close(pipefd[0]);
        serial.fd = -1;
        serial_ready = 0;
    }

    deneb_status_init(&status);
    status.state = DENEB_PRINT_STATE_PRINTING;
    deneb_flow_init(&flow);
    assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, "M105") == 0);
    assert(deneb_flow_inflight(&flow) == 1);
    {
        const char *proto_error =
            "Error:ProtoError:Sequence number is unexpected (received, expected): 0,7\n";
        assert(pipe(pipefd) == 0);
        assert(write(pipefd[1], proto_error, strlen(proto_error)) ==
               (ssize_t)strlen(proto_error));
        close(pipefd[1]);
        serial.fd = pipefd[0];
        serial_ready = 1;
        runtime.allow_sequence_resync = 0;
        assert(deneb_motion_runtime_poll(&runtime) < 0);
        assert(status.state == DENEB_PRINT_STATE_ERROR);
        assert(status.error.code == DENEB_ERROR_SERIAL);
        assert(flow.next_sequence == 7);
        assert(deneb_flow_inflight(&flow) == 0);
        close(pipefd[0]);
        serial.fd = -1;
        serial_ready = 0;
    }

    deneb_status_init(&status);
    status.state = DENEB_PRINT_STATE_ABORTING;
    deneb_flow_init(&flow);
    assert(deneb_motion_sender_send_gcode(&flow, &serial, 0, "M105") == 0);
    assert(deneb_flow_inflight(&flow) == 1);
    {
        const char *proto_error =
            "Error:ProtoError:Sequence number is unexpected (received, expected): 0,7\n";
        assert(pipe(pipefd) == 0);
        assert(write(pipefd[1], proto_error, strlen(proto_error)) ==
               (ssize_t)strlen(proto_error));
        close(pipefd[1]);
        serial.fd = pipefd[0];
        serial_ready = 1;
        runtime.allow_sequence_resync = 1;
        assert(deneb_motion_runtime_poll(&runtime) == 1);
        assert(status.state == DENEB_PRINT_STATE_ABORTING);
        assert(flow.next_sequence == 7);
        assert(deneb_flow_inflight(&flow) == 0);
        close(pipefd[0]);
        serial.fd = -1;
        serial_ready = 0;
        runtime.allow_sequence_resync = 0;
    }
}

static void test_service_proto_desync_resyncs_active_job(void)
{
    const char *proto_error =
        "Error:ProtoError:Sequence number is unexpected (received, expected): 0,7\n";
    const char *job_path = "/tmp/deneb-printsvc-desync-abort-test.gcode";
    deneb_print_service_t svc;
    FILE *serial_file;
    int serial_fd;

    deneb_print_service_init(&svc);
    svc.status.state = DENEB_PRINT_STATE_PRINTING;
    snprintf(svc.status.file, sizeof(svc.status.file), "%s", job_path);
    svc.job_active = 1;
    svc.serial_ready = 1;
    assert(deneb_motion_sender_send_gcode(&svc.flow, &svc.serial, 0,
                                          "G1 X1") == 0);
    assert(deneb_flow_inflight(&svc.flow) == 1);

    serial_file = tmpfile();
    assert(serial_file != NULL);
    assert(fputs(proto_error, serial_file) >= 0);
    assert(fflush(serial_file) == 0);
    assert(lseek(fileno(serial_file), 0, SEEK_SET) == 0);
    serial_fd = dup(fileno(serial_file));
    assert(serial_fd >= 0);
    svc.serial.fd = serial_fd;

    assert(deneb_print_service_poll_motion(&svc) == 1);
    assert(svc.job_active);
    assert(!svc.abort_cleanup_pending);
    assert(svc.status.state == DENEB_PRINT_STATE_PRINTING);
    assert(svc.flow.next_sequence == 7);
    assert(deneb_flow_inflight(&svc.flow) == 0);

    deneb_print_service_close(&svc);
    fclose(serial_file);
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
    io.wait_for_heater = macro_runner_fake_heater_wait;
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
    assert(deneb_macro_runner_run_file(path, &io) == -1);

    memset(&fake, 0, sizeof(fake));
    fake.abort_after_sends = -1;
    fake.fail_send = 1;
    fake.fail_send_rc = DENEB_MOTION_SEND_SERIAL;
    io.ctx = &fake;
    assert(deneb_macro_runner_run_file(path, &io) ==
           DENEB_MOTION_SEND_SERIAL);

    memset(&fake, 0, sizeof(fake));
    fake.abort_after_sends = -1;
    fake.fail_poll = 1;
    io.ctx = &fake;
    assert(deneb_macro_runner_run_file(path, &io) == -1);
    assert(fake.sends == 1);
    assert(fake.polls == 1);

    memset(&fake, 0, sizeof(fake));
    fake.abort_after_sends = -1;
    fake.fail_poll = 1;
    fake.fail_poll_rc = DENEB_MOTION_SEND_SERIAL;
    io.ctx = &fake;
    assert(deneb_macro_runner_run_file(path, &io) ==
           DENEB_MOTION_SEND_SERIAL);
    assert(fake.sends == 1);
    assert(fake.polls == 1);

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs("M109 S205\nG1 X3\n", f);
    fclose(f);
    memset(&fake, 0, sizeof(fake));
    fake.abort_after_sends = -1;
    io.ctx = &fake;
    assert(deneb_macro_runner_run_file(path, &io) == 0);
    assert(fake.sends == 2);
    assert(fake.heater_waits == 1);
    assert(!fake.last_wait_bed);
    assert(fake.last_wait_nozzle);
    assert(fake.last_wait_target == 205.0f);
    assert(strcmp(fake.sent[0], "M104 S205") == 0);
    assert(strcmp(fake.sent[1], "G1 X3") == 0);

    memset(&fake, 0, sizeof(fake));
    fake.abort_after_sends = -1;
    fake.fail_heater_wait = 1;
    io.ctx = &fake;
    assert(deneb_macro_runner_run_file(path, &io) == -1);
    assert(fake.sends == 1);
    assert(fake.heater_waits == 1);
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

typedef struct {
    int pause_count;
    int resume_count;
    int abort_count;
    int stop_count;
    int clear_count;
    int fail_kind;
} fake_print_action_dispatch_t;

static int fake_dispatch_pause(void *ctx)
{
    fake_print_action_dispatch_t *fake = ctx;
    fake->pause_count++;
    return fake->fail_kind == DENEB_PRINT_ACTION_PLAN_PAUSE ? -1 : 0;
}

static int fake_dispatch_resume(void *ctx)
{
    fake_print_action_dispatch_t *fake = ctx;
    fake->resume_count++;
    return fake->fail_kind == DENEB_PRINT_ACTION_PLAN_RESUME ? -1 : 0;
}

static int fake_dispatch_abort(void *ctx)
{
    fake_print_action_dispatch_t *fake = ctx;
    fake->abort_count++;
    return fake->fail_kind == DENEB_PRINT_ACTION_PLAN_ABORT ? -1 : 0;
}

static int fake_dispatch_stop(void *ctx)
{
    fake_print_action_dispatch_t *fake = ctx;
    fake->stop_count++;
    return fake->fail_kind == DENEB_PRINT_ACTION_PLAN_STOP ? -1 : 0;
}

static void fake_dispatch_clear_pending(void *ctx)
{
    fake_print_action_dispatch_t *fake = ctx;
    fake->clear_count++;
}

static void test_print_state_rules(void)
{
    deneb_print_observation_t obs = {0};
    deneb_print_stop_guard_t stop_guard;
    deneb_print_preheat_tracker_t preheat_tracker;
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
    deneb_print_preheat_tracker_init(&preheat_tracker);
    assert(!preheat_tracker.targets_seen);
    assert(!preheat_tracker.targets_ready_seen);
    assert(deneb_print_preheat_tracker_update(&preheat_tracker,
                                              0.0f, 0.0f, 0.0f, 0.0f) ==
           DENEB_PRINT_PREHEAT_EVENT_NONE);
    assert(deneb_print_preheat_tracker_update(&preheat_tracker,
                                              20.0f, 60.0f, 30.0f, 210.0f) ==
           DENEB_PRINT_PREHEAT_EVENT_TARGETS_ACTIVE);
    assert(preheat_tracker.targets_seen);
    assert(!preheat_tracker.targets_ready_seen);
    assert(deneb_print_preheat_tracker_update(&preheat_tracker,
                                              40.0f, 60.0f, 150.0f, 210.0f) ==
           DENEB_PRINT_PREHEAT_EVENT_NONE);
    assert(deneb_print_preheat_tracker_update(&preheat_tracker,
                                              59.0f, 60.0f, 209.0f, 210.0f) ==
           DENEB_PRINT_PREHEAT_EVENT_TARGETS_READY);
    assert(preheat_tracker.targets_ready_seen);
    assert(deneb_print_preheat_tracker_update(&preheat_tracker,
                                              60.0f, 60.0f, 210.0f, 210.0f) ==
           DENEB_PRINT_PREHEAT_EVENT_NONE);
    assert(deneb_print_preheat_tracker_update(&preheat_tracker,
                                              30.0f, 0.0f, 40.0f, 0.0f) ==
           DENEB_PRINT_PREHEAT_EVENT_RESET);
    assert(!preheat_tracker.targets_seen);
    assert(!preheat_tracker.targets_ready_seen);
    assert(deneb_print_preheat_tracker_update(&preheat_tracker,
                                              60.0f, 60.0f, 210.0f, 210.0f) ==
           (DENEB_PRINT_PREHEAT_EVENT_TARGETS_ACTIVE |
            DENEB_PRINT_PREHEAT_EVENT_TARGETS_READY));
    assert(DENEB_PRINT_MATERIAL_MIN_MOVE_TEMP_C == 170.0f);
    assert(DENEB_PRINT_MATERIAL_READY_TOLERANCE_C == 2.0f);
    assert(!deneb_print_material_move_ready(210.0f, 160.0f));
    assert(!deneb_print_material_move_ready(207.0f, 210.0f));
    assert(deneb_print_material_move_ready(208.0f, 210.0f));
    assert(deneb_print_is_cluster_guid("506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9"));
    assert(deneb_print_is_cluster_guid("506C9F0D-E3AA-4BD4-B2D2-23E2425B1AA9"));
    assert(strcmp(deneb_print_cluster_job_uuid_or_default(
                      "506C9F0D-E3AA-4BD4-B2D2-23E2425B1AA9"),
                  "506C9F0D-E3AA-4BD4-B2D2-23E2425B1AA9") == 0);
    assert(!deneb_print_is_cluster_guid("target-guid"));
    assert(strcmp(deneb_print_cluster_job_uuid_or_default("target-guid"),
                  DENEB_PRINT_DEFAULT_CLUSTER_JOB_UUID) == 0);

    deneb_print_observation_init(&obs, DENEB_PRINT_REQ_PREHEATING, NULL,
                                 0, 0, 60.0f, 210.0f);
    assert(obs.req == DENEB_PRINT_REQ_PREHEATING);
    assert(obs.bed_target == 60.0f);
    assert(obs.nozzle_target == 210.0f);
    assert(deneb_print_observation_has_context(&obs));
    assert(!deneb_print_has_stoppable_context(&obs, 0, 0, 0));
    assert(deneb_print_has_preparing_context(&obs, 1));
    assert(deneb_print_has_stoppable_context(&obs, 0, 0, 1));
    {
        deneb_print_context_flags_t flags;

        deneb_print_context_flags_from_observation(&flags, &obs, 0, 0, 1);
        assert(flags.has_active_context);
        assert(flags.has_preparing_context);
        assert(flags.has_stoppable_context);

        deneb_print_context_flags_from_observation(&flags, &obs, 0, 0, 0);
        assert(flags.has_active_context);
        assert(!flags.has_preparing_context);
        assert(!flags.has_stoppable_context);

        deneb_print_context_flags_from_observation(&flags, NULL, 1, 1, 1);
        assert(!flags.has_active_context);
        assert(!flags.has_preparing_context);
        assert(!flags.has_stoppable_context);

        deneb_print_context_flags_from_fields(
            &flags, DENEB_PRINT_REQ_PREHEATING, "/home/3D/cube.gcode",
            0, 0, 60.0f, 210.0f, 0, 0, 1);
        assert(flags.has_active_context);
        assert(flags.has_preparing_context);
        assert(flags.has_stoppable_context);

        deneb_print_context_flags_from_fields(
            &flags, DENEB_PRINT_REQ_PREHEATING, NULL, 0, 0,
            60.0f, 210.0f, 0, 0, 0);
        assert(flags.has_active_context);
        assert(!flags.has_preparing_context);
        assert(!flags.has_stoppable_context);

        assert(deneb_print_fields_have_active_context(
            "", "/home/3D/cube.gcode", 0, 0, 0.0f, 0.0f, 0, 0, 1));
    }

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
    {
        deneb_print_context_flags_t flags;

        deneb_print_context_flags_from_observation(&flags, &obs, 1, 1, 1);
        assert(!flags.has_active_context);
        assert(!flags.has_preparing_context);
        assert(!flags.has_stoppable_context);

        deneb_print_context_flags_from_fields(
            &flags, DENEB_COMMAND_VERB_ABORT, "/home/3D/cube.gcode",
            120, 100, 60.0f, 210.0f, 1, 1, 1);
        assert(!flags.has_active_context);
        assert(!flags.has_preparing_context);
        assert(!flags.has_stoppable_context);
        assert(!deneb_print_fields_have_active_context(
            DENEB_COMMAND_VERB_ABORT, "/home/3D/cube.gcode",
            120, 100, 60.0f, 210.0f, 1, 1, 1));
    }

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
    assert(strcmp(deneb_print_status_label_with_req(
                      1, 0, 0, 1, DENEB_COMMAND_VERB_ABORT, 1),
                  "aborting") == 0);
    assert(strcmp(deneb_print_status_label_with_req(
                      1, 0, 0, 0, DENEB_COMMAND_VERB_ABORT, 1),
                  "aborting") == 0);
    assert(strcmp(deneb_print_status_label_with_req(
                      1, 0, 0, 0, DENEB_COMMAND_VERB_ABORT, 0),
                  "idle") == 0);
    assert(strcmp(deneb_print_status_label_with_req(
                      1, 0, 0, 0, DENEB_PRINT_REQ_PREPARE, 1),
                  DENEB_PRINT_PHASE_NAME_PRE_PRINT) == 0);
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
    assert(strcmp(deneb_print_completion_state_label_with_req(
                      0, 0, 0, DENEB_PRINT_REQ_COMPLETE),
                  "completed") == 0);
    assert(strcmp(deneb_print_completion_state_label_with_req(
                      0, 0, 0, DENEB_PRINT_REQ_IDLE),
                  "stopped") == 0);
    assert(strcmp(deneb_print_completion_state_label_with_req(
                      1, 0, 0, DENEB_PRINT_REQ_COMPLETE),
                  "error") == 0);
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
                   "pending-uuid", "cloud-job-2", 55.0f, 205.0f,
                   &plan) == 0);
        assert(strcmp(plan.path, "/home/3D/pending.gcode") == 0);
        assert(strcmp(plan.source, DENEB_PRINT_WEB_API_JOB_SOURCE) == 0);
        assert(strcmp(plan.uuid, "pending-uuid") == 0);
        assert(strcmp(plan.cloud_job_id, "cloud-job-2") == 0);
        assert(plan.bed_target == 55.0f);
        assert(plan.nozzle_target == 205.0f);
        assert(deneb_print_job_start_plan_file("", DENEB_PRINT_USB_JOB_SOURCE,
                                               &plan) < 0);
    }
    {
        char action[16];
        deneb_print_action_route_t action_route;
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
        assert(deneb_print_action_parse_or_pending_default(
                   "{\"action\":\"abort\"}", 1, action, sizeof(action)) == 0);
        assert(strcmp(action, DENEB_PRINT_ACTION_ABORT_TEXT) == 0);
        assert(deneb_print_action_parse_or_pending_default(
                   "{}", 1, action, sizeof(action)) == 0);
        assert(strcmp(action, DENEB_PRINT_ACTION_PRINT_TEXT) == 0);
        assert(deneb_print_action_parse_or_pending_default(
                   "{}", 0, action, sizeof(action)) != 0);
        assert(deneb_print_action_parse_or_pending_default(
                   "{}", 1, action, 3) != 0);
        assert(strcmp(deneb_print_action_parse_error_response(),
                      "{\"message\":\"Expected {\\\"action\\\":\\\"pause|print|abort\\\"}\"}") == 0);
        assert(strcmp(deneb_print_action_unknown_response(),
                      "{\"message\":\"Unknown print job action\"}") == 0);
        assert(strcmp(deneb_print_state_unknown_response(),
                      "{\"message\":\"Unknown state\"}") == 0);
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
        {
            fake_print_action_dispatch_t fake = {0};
            deneb_print_action_dispatch_ops_t ops = {
                &fake,
                fake_dispatch_pause,
                fake_dispatch_resume,
                fake_dispatch_abort,
                fake_dispatch_stop,
                fake_dispatch_clear_pending
            };
            assert(deneb_print_action_dispatch(&action_plan, &ops) == 0);
            assert(fake.pause_count == 1);
            assert(fake.resume_count == 0);
            assert(fake.abort_count == 0);
            assert(fake.stop_count == 0);
            assert(fake.clear_count == 0);
        }
        assert(deneb_print_action_plan(DENEB_PRINT_ACTION_CONTINUE_TEXT,
                                       &action_plan) == 0);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_RESUME);
        assert(strcmp(action_plan.command, DENEB_COMMAND_VERB_RESUME) == 0);
        {
            fake_print_action_dispatch_t fake = {0};
            deneb_print_action_dispatch_ops_t ops = {
                &fake,
                fake_dispatch_pause,
                fake_dispatch_resume,
                fake_dispatch_abort,
                fake_dispatch_stop,
                fake_dispatch_clear_pending
            };
            assert(deneb_print_action_dispatch(&action_plan, &ops) == 0);
            assert(fake.resume_count == 1);
            assert(fake.clear_count == 0);
        }
        assert(deneb_print_action_plan(DENEB_PRINT_ACTION_CANCEL_TEXT,
                                       &action_plan) == 0);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_ABORT);
        assert(strcmp(action_plan.command, DENEB_COMMAND_VERB_ABORT) == 0);
        assert(action_plan.clear_pending_after_success);
        {
            fake_print_action_dispatch_t fake = {0};
            deneb_print_action_dispatch_ops_t ops = {
                &fake,
                fake_dispatch_pause,
                fake_dispatch_resume,
                fake_dispatch_abort,
                fake_dispatch_stop,
                fake_dispatch_clear_pending
            };
            assert(deneb_print_action_dispatch(&action_plan, &ops) == 0);
            assert(fake.abort_count == 1);
            assert(fake.clear_count == 1);
        }
        assert(deneb_print_action_plan(DENEB_PRINT_ACTION_STOP_TEXT,
                                       &action_plan) == 0);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_STOP);
        assert(strcmp(action_plan.command, DENEB_COMMAND_VERB_ABORT) == 0);
        assert(action_plan.clear_pending_after_success);
        {
            fake_print_action_dispatch_t fake = {0};
            deneb_print_action_dispatch_ops_t ops = {
                &fake,
                fake_dispatch_pause,
                fake_dispatch_resume,
                fake_dispatch_abort,
                fake_dispatch_stop,
                fake_dispatch_clear_pending
            };
            assert(deneb_print_action_dispatch(&action_plan, &ops) == 0);
            assert(fake.stop_count == 1);
            assert(fake.clear_count == 1);
            fake.fail_kind = DENEB_PRINT_ACTION_PLAN_STOP;
            assert(deneb_print_action_dispatch(&action_plan, &ops) != 0);
            assert(fake.clear_count == 1);
        }
        assert(deneb_print_action_plan("bogus", &action_plan) != 0);
        assert(deneb_print_action_dispatch(NULL, NULL) != 0);
        assert(deneb_print_pending_action_plan(
                   DENEB_PRINT_ACTION_PRINT_TEXT, &action_plan) == 0);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_RESUME);
        assert(strcmp(action_plan.command, DENEB_PRINT_REQ_PREPARE) == 0);
        assert(strcmp(action_plan.failure_message,
                      "Failed to continue print") == 0);
        assert(!action_plan.clear_pending_after_success);
        assert(deneb_print_pending_action_plan(
                   DENEB_PRINT_ACTION_CANCEL_TEXT, &action_plan) == 0);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_ABORT);
        assert(strcmp(action_plan.command, DENEB_COMMAND_VERB_ABORT) == 0);
        assert(strcmp(action_plan.failure_message,
                      "Failed to cancel print") == 0);
        assert(action_plan.clear_pending_after_success);
        assert(deneb_print_pending_action_plan("pause", &action_plan) != 0);
        assert(deneb_print_pending_action_plan(
                   DENEB_PRINT_ACTION_PRINT_TEXT, NULL) != 0);
        assert(deneb_print_cluster_action_plan(
                   DENEB_PRINT_ACTION_PRINT_TEXT, 1, &action_plan,
                   &action_route) == 0);
        assert(action_route == DENEB_PRINT_ACTION_ROUTE_PENDING);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_RESUME);
        assert(strcmp(action_plan.command, DENEB_PRINT_REQ_PREPARE) == 0);
        assert(deneb_print_cluster_action_plan(
                   DENEB_PRINT_ACTION_CANCEL_TEXT, 1, &action_plan,
                   &action_route) == 0);
        assert(action_route == DENEB_PRINT_ACTION_ROUTE_PENDING);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_ABORT);
        assert(strcmp(action_plan.command, DENEB_COMMAND_VERB_ABORT) == 0);
        assert(deneb_print_cluster_action_plan(
                   DENEB_PRINT_ACTION_PRINT_TEXT, 0, &action_plan,
                   &action_route) == 0);
        assert(action_route == DENEB_PRINT_ACTION_ROUTE_NORMAL);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_RESUME);
        assert(strcmp(action_plan.command, DENEB_COMMAND_VERB_RESUME) == 0);
        assert(deneb_print_cluster_action_plan(
                   DENEB_PRINT_ACTION_CANCEL_TEXT, 0, &action_plan,
                   &action_route) == 0);
        assert(action_route == DENEB_PRINT_ACTION_ROUTE_NORMAL);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_ABORT);
        assert(deneb_print_cluster_action_plan("bogus", 1, &action_plan,
                                               &action_route) != 0);
        assert(deneb_print_cluster_action_plan(
                   DENEB_PRINT_ACTION_PRINT_TEXT, 1, NULL,
                   &action_route) != 0);
        assert(deneb_print_cluster_action_plan(
                   DENEB_PRINT_ACTION_PRINT_TEXT, 1, &action_plan,
                   NULL) != 0);
        assert(deneb_print_delete_action_plan(1, &action_plan) == 0);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_ABORT);
        assert(strcmp(action_plan.command, DENEB_COMMAND_VERB_ABORT) == 0);
        assert(strcmp(action_plan.failure_message,
                      "Failed to abort print") == 0);
        assert(!action_plan.clear_pending_after_success);
        assert(deneb_print_delete_action_plan(0, &action_plan) == 0);
        assert(action_plan.kind == DENEB_PRINT_ACTION_PLAN_CLEAR_PENDING);
        assert(strcmp(action_plan.failure_message,
                      "Failed to clear pending print") == 0);
        assert(action_plan.clear_pending_after_success);
        {
            fake_print_action_dispatch_t fake = {0};
            deneb_print_action_dispatch_ops_t ops = {
                &fake,
                fake_dispatch_pause,
                fake_dispatch_resume,
                fake_dispatch_abort,
                fake_dispatch_stop,
                fake_dispatch_clear_pending
            };
            assert(deneb_print_action_dispatch(&action_plan, &ops) == 0);
            assert(fake.pause_count == 0);
            assert(fake.resume_count == 0);
            assert(fake.abort_count == 0);
            assert(fake.stop_count == 0);
            assert(fake.clear_count == 1);
        }
        assert(deneb_print_delete_action_plan(0, NULL) != 0);
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

    deneb_print_job_summary_init(&summary, "cube\"one.gcode", "uuid-1",
                                 "Cura", 0, 0, 1, 120, 90, 25.0f);
    {
        char json[2048];

        assert(deneb_print_job_summary_format_um_response(
                   &summary, json, sizeof(json)) > 0);
        assert(strstr(json, "\"name\":\"cube\\\"one.gcode\"") != NULL);
        assert(strstr(json, "\"uuid\":\"uuid-1\"") != NULL);
        assert(strstr(json, "\"source\":\"Cura\"") != NULL);
        assert(strstr(json, "\"state\":\"printing\"") != NULL);
        assert(strstr(json, "\"progress\":0.2") != NULL);
        assert(strstr(json, "\"time_elapsed\":30") != NULL);
        assert(strstr(json, "\"time_total\":120") != NULL);
        assert(strstr(json, "\"datetime_started\":\"\"") != NULL);
        assert(strstr(json, "\"datetime_finished\":\"\"") != NULL);

        assert(deneb_print_job_summary_format_string_field(
                   &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_NAME,
                   json, sizeof(json)) > 0);
        assert(strcmp(json, "\"cube\\\"one.gcode\"") == 0);
        assert(deneb_print_job_summary_format_string_field(
                   &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_UUID,
                   json, sizeof(json)) > 0);
        assert(strcmp(json, "\"uuid-1\"") == 0);
        assert(deneb_print_job_summary_format_string_field(
                   &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_SOURCE,
                   json, sizeof(json)) > 0);
        assert(strcmp(json, "\"Cura\"") == 0);
        assert(deneb_print_job_summary_format_string_field(
                   &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_STATE,
                   json, sizeof(json)) > 0);
        assert(strcmp(json, "\"printing\"") == 0);
        assert(deneb_print_job_summary_format_string_field(
                   &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_DATETIME_STARTED,
                   json, sizeof(json)) > 0);
        assert(strcmp(json, "\"\"") == 0);
        assert(deneb_print_job_summary_format_progress_fraction(
                   &summary, json, sizeof(json)) > 0);
        assert(strcmp(json, "0.2") == 0);
        assert(deneb_print_job_summary_format_time_elapsed(
                   &summary, json, sizeof(json)) > 0);
        assert(strcmp(json, "30") == 0);
        assert(deneb_print_job_summary_format_time_total(
                   &summary, json, sizeof(json)) > 0);
        assert(strcmp(json, "120") == 0);
        assert(deneb_print_job_summary_format_string_field(
                   &summary, (deneb_print_job_summary_string_field_t)99,
                   json, sizeof(json)) != 0);

        assert(deneb_print_job_summary_format_deneb_current_response(
                   &summary, json, sizeof(json)) > 0);
        assert(strstr(json, "\"name\":\"cube\\\"one.gcode\"") != NULL);
        assert(strstr(json, "\"uuid\":\"uuid-1\"") != NULL);
        assert(strstr(json, "\"source\":\"Cura\"") != NULL);
        assert(strstr(json, "\"state\":\"printing\"") != NULL);
        assert(strstr(json, "\"progress\":25.0") != NULL);
        assert(strstr(json, "\"time_total\":120") != NULL);
        assert(strstr(json, "\"time_elapsed\":30") != NULL);
        assert(strstr(json, "\"time_left\":90") != NULL);

        assert(deneb_print_job_summary_format_cluster_active_response(
                   &summary, "printer-1", "12345", json,
                   sizeof(json)) > 0);
        assert(strstr(json, "\"created_at\":\"12345\"") != NULL);
        assert(strstr(json, "\"force\":false") != NULL);
        assert(strstr(json, "\"name\":\"cube\\\"one.gcode\"") != NULL);
        assert(strstr(json, "\"started\":true") != NULL);
        assert(strstr(json, "\"status\":\"printing\"") != NULL);
        assert(strstr(json, "\"time_total\":120") != NULL);
        assert(strstr(json, "\"time_elapsed\":30") != NULL);
        assert(strstr(json, "\"cloud_job_id\"") == NULL);
        assert(strstr(json, "\"configuration\":[{\"extruder_index\":0") != NULL);
        assert(strstr(json, "\"print_core_id\":\"0.4 mm\"") != NULL);
        assert(strstr(json, "\"material\":{\"guid\":\"506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9\"") != NULL);
        assert(strstr(json, "\"printer_uuid\":\"printer-1\"") != NULL);
        assert(strstr(json, "\"assigned_to\":\"printer-1\"") != NULL);
        assert(strstr(json, "\"owner\"") == NULL);
        assert(strstr(json, "\"build_plate\"") == NULL);
        assert(strstr(json, "\"compatible_machine_families\"") == NULL);
        assert(strstr(json, "\"impediments_to_printing\"") == NULL);
        summary.cloud_job_id = "cloud-job-123";
        assert(deneb_print_job_summary_format_cluster_active_response(
                   &summary, "printer-1", "12345", json,
                   sizeof(json)) > 0);
        assert(strstr(json, "\"cloud_job_id\":\"cloud-job-123\"") != NULL);

        summary.active = 0;
        assert(deneb_print_job_summary_format_um_response(
                   &summary, json, sizeof(json)) != 0);
        assert(deneb_print_job_summary_format_deneb_current_response(
                   &summary, json, sizeof(json)) != 0);
        assert(deneb_print_job_summary_format_cluster_active_response(
                   &summary, "printer-1", "12345", json,
                   sizeof(json)) != 0);
    }
}

static void test_printer_status_response(void)
{
    deneb_printer_status_response_t status;
    char json[4096];

    deneb_printer_status_response_init(&status);
    status.nozzle_temp_cur = 201.25f;
    status.nozzle_temp_set = 210.0f;
    status.bed_temp_cur = 57.75f;
    status.bed_temp_set = 60.0f;
    status.pos_x = 12.0f;
    status.pos_y = 34.0f;
    status.pos_z = 5.5f;
    status.connected = 1;
    status.is_printing = 1;
    status.is_paused = 0;
    status.has_error = 0;
    status.native_active = 1;
    status.native_stop_allowed = 1;
    status.has_native_active = 1;
    status.has_native_stop_allowed = 1;
    status.topcap_present = 1;
    status.topcap_temp_cur = 33.25f;
    status.firmware = "Apr 30 2020 12:57:04";
    status.machine_type = "E2";
    status.pcb_id = 4;
    status.pcb_id_valid = 1;
    status.progress = 42.5f;
    status.time_total = 1200;
    status.time_left = 690;
    status.filename = "cube\"one.gcode";
    status.status_label = "printing";
    status.error_key = "serial_fault";
    status.error_category = "serial";
    status.error_detail = "flow \"desync\"";
    status.flow_last_response = "Error:ProtoError";
    status.flow_inflight = 3;
    status.flow_sent = 55;
    status.flow_ack = 52;
    status.flow_resend = 1;
    status.flow_reject = 2;
    status.job_line_number = 54;

    assert(deneb_printer_status_response_format_status(
               &status, json, sizeof(json)) > 0);
    assert(strcmp(json, "\"printing\"") == 0);

    assert(deneb_printer_status_response_format_um_root(
               &status, json, sizeof(json)) > 0);
    assert(strstr(json, "\"bed\":{\"temperature\":{\"current\":57.8") != NULL);
    assert(strstr(json, "\"target\":60.0") != NULL);
    assert(strstr(json, "\"pre_heat\":{\"active\":true}") != NULL);
    assert(strstr(json, "\"topcap\":{\"present\":true") != NULL);
    assert(strstr(json, "\"temperature\":{\"current\":33.2}") != NULL);
    assert(strstr(json, "\"firmware\":\"Apr 30 2020 12:57:04\"") != NULL);
    assert(strstr(json, "\"machine_type\":\"E2\"") != NULL);
    assert(strstr(json, "\"pcb_id\":4") != NULL);
    assert(strstr(json, "\"pcb_id_valid\":true") != NULL);
    assert(strstr(json, "\"heads\":[{\"acceleration\":3000") != NULL);
    assert(strstr(json, "\"hotend\":{\"id\":\"0.4 mm\"") != NULL);
    assert(strstr(json, "\"current\":201.2") != NULL);
    assert(strstr(json, "\"position\":{\"x\":12.0,\"y\":34.0,\"z\":5.5}") != NULL);
    assert(strstr(json, "\"status\":\"printing\"") != NULL);
    assert(strstr(json, "\"connected\":true") != NULL);
    assert(strstr(json, "\"is_printing\":true") != NULL);
    assert(strstr(json, "\"is_paused\":false") != NULL);
    assert(strstr(json, "\"has_error\":false") != NULL);
    assert(strstr(json, "\"native_active\":true") != NULL);
    assert(strstr(json, "\"native_stop_allowed\":true") != NULL);
    assert(strstr(json, "\"progress\":42.5") != NULL);
    assert(strstr(json, "\"time_total\":1200") != NULL);
    assert(strstr(json, "\"time_left\":690") != NULL);
    assert(strstr(json, "\"filename\":\"cube\\\"one.gcode\"") != NULL);
    assert(strstr(json, "\"diagnostics\":{\"error_key\":\"serial_fault\"") != NULL);
    assert(strstr(json, "\"error_detail\":\"flow \\\"desync\\\"\"") != NULL);
    assert(strstr(json, "\"flow_last_response\":\"Error:ProtoError\"") != NULL);
    assert(strstr(json, "\"flow_inflight\":3") != NULL);
    assert(strstr(json, "\"flow_sent\":55") != NULL);
    assert(strstr(json, "\"flow_ack\":52") != NULL);
    assert(strstr(json, "\"flow_resend\":1") != NULL);
    assert(strstr(json, "\"flow_reject\":2") != NULL);
    assert(strstr(json, "\"job_line_number\":54") != NULL);

    assert(deneb_printer_status_response_format_um_bed(
               &status, json, sizeof(json)) > 0);
    assert(strstr(json, "\"temperature\":{\"current\":57.8") != NULL);
    assert(strstr(json, "\"type\":\"glass\"") != NULL);
    assert(strstr(json, "\"pre_heat\":{\"active\":true}") != NULL);
    assert(deneb_printer_status_response_format_um_bed_preheat(
               &status, json, sizeof(json)) > 0);
    assert(strcmp(json, "{\"active\":true}") == 0);
    status.bed_temp_set = 0.0f;
    assert(deneb_printer_status_response_format_um_bed(
               &status, json, sizeof(json)) > 0);
    assert(strstr(json, "\"pre_heat\":{\"active\":false}") != NULL);
    assert(deneb_printer_status_response_format_um_bed_preheat(
               &status, json, sizeof(json)) > 0);
    assert(strcmp(json, "{\"active\":false}") == 0);
    status.bed_temp_set = 60.0f;
    assert(deneb_printer_status_response_format_um_temperature(
               status.bed_temp_cur, status.bed_temp_set,
               json, sizeof(json)) > 0);
    assert(strcmp(json, "{\"current\":57.8,\"target\":60.0}") == 0);
    assert(deneb_printer_status_response_format_um_position(
               &status, json, sizeof(json)) > 0);
    assert(strcmp(json, "{\"x\":12.0,\"y\":34.0,\"z\":5.5}") == 0);
    assert(deneb_printer_status_response_format_um_head(
               &status, json, sizeof(json)) > 0);
    assert(strstr(json, "\"acceleration\":3000") != NULL);
    assert(strstr(json, "\"jerk\":{\"x\":20.0,\"y\":20.0,\"z\":1.0}") != NULL);
    assert(deneb_printer_status_response_format_um_heads(
               &status, json, sizeof(json)) > 0);
    assert(strstr(json, "[{\"acceleration\":3000") == json);
    assert(deneb_printer_status_response_format_um_feeder(
               json, sizeof(json)) > 0);
    assert(strcmp(json, "{\"acceleration\":3000.0,\"jerk\":5.0,"
                        "\"max_speed\":45.0}") == 0);
    assert(deneb_printer_status_response_format_um_hotend(
               &status, 1, json, sizeof(json)) > 0);
    assert(strstr(json, "\"id\":\"0.4 mm\"") != NULL);
    assert(strstr(json, "\"offset\":{\"x\":0.0,\"y\":0.0,\"z\":0.0,"
                        "\"state\":\"valid\"}") != NULL);
    assert(deneb_printer_status_response_format_um_hotend(
               &status, 0, json, sizeof(json)) > 0);
    assert(strstr(json, "\"offset\"") == NULL);
    assert(deneb_printer_status_response_format_um_extruder(
               &status, json, sizeof(json)) > 0);
    assert(strstr(json, "\"active_material\"") != NULL);
    assert(strstr(json, "\"hotend\":{\"id\":\"0.4 mm\"") != NULL);
    assert(deneb_printer_status_response_format_um_extruders(
               &status, json, sizeof(json)) > 0);
    assert(strstr(json, "[{\"active_material\"") == json);
    assert(deneb_printer_status_response_format_um_material(
               json, sizeof(json)) > 0);
    assert(strcmp(json, "{\"GUID\":\"\",\"guid\":\"\","
                        "\"length_remaining\":-1}") == 0);
    assert(deneb_printer_status_response_format_um_led(
               json, sizeof(json)) > 0);
    assert(strcmp(json, "{\"hue\":0.0,\"saturation\":0.0,"
                        "\"brightness\":100.0}") == 0);
    assert(deneb_printer_status_response_format_um_led_brightness(
               json, sizeof(json)) > 0);
    assert(strcmp(json, "100") == 0);
    assert(deneb_printer_status_response_format_um_led_hue(
               json, sizeof(json)) > 0);
    assert(strcmp(json, "0") == 0);
    assert(deneb_printer_status_response_format_um_led_saturation(
               json, sizeof(json)) > 0);
    assert(strcmp(json, "0") == 0);
    assert(deneb_printer_status_response_format_um_ambient(
               json, sizeof(json)) > 0);
    assert(strcmp(json, "{\"current\":0.0}") == 0);
    status.topcap_present = 1;
    assert(deneb_printer_status_response_format_um_airmanager(
               &status, json, sizeof(json)) > 0);
    assert(strstr(json, "\"status\":\"connected\"") != NULL);
    assert(strstr(json, "\"temperature\":{\"current\":33.2}") != NULL);
    status.topcap_present = 0;
    assert(deneb_printer_status_response_format_um_airmanager(
               &status, json, sizeof(json)) > 0);
    assert(strstr(json, "\"status\":\"not_connected\"") != NULL);

    status.filename = "";
    status.status_label = "idle\"quoted";
    assert(deneb_printer_status_response_format_status(
               &status, json, sizeof(json)) > 0);
    assert(strcmp(json, "\"idle\\\"quoted\"") == 0);
    assert(deneb_printer_status_response_format_um_root(
               &status, json, sizeof(json)) > 0);
    assert(strstr(json, "\"status\":\"idle\\\"quoted\"") != NULL);

    assert(deneb_printer_status_response_format_um_root(
               NULL, json, sizeof(json)) != 0);
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
    curr = deneb_status_filename_context_from_fields(
        payload.req, "", payload.uuid, payload.time_total, payload.time_left,
        payload.bed_temp_set, payload.nozzle_temp_set, payload.is_printing,
        payload.is_paused);
    assert(curr.req == payload.req);
    assert(curr.filename && curr.filename[0] == '\0');
    assert(curr.time_total == 120);
    assert(curr.time_left == 90);
    assert(deneb_status_filename_context_from_fields(
               NULL, NULL, NULL, 0, 0, 0.0f, 0.0f, 0, 0).req == NULL);
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
    curr = deneb_status_filename_context_from_fields(
        payload.req, "", payload.uuid, payload.time_total, payload.time_left,
        payload.bed_temp_set, payload.nozzle_temp_set, payload.is_printing,
        payload.is_paused);
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
    curr = deneb_status_filename_context_from_fields(
        payload.req, "cube.gcode", "", 120, 90, 0.0f, 0.0f, 0, 0);
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
    {
        deneb_gcode_motion_plan_t plan;

        assert(deneb_gcode_plan_motion_from_json(
                   "{\"axis\":\"x\",\"distance\":5}", &plan) ==
               DENEB_GCODE_MOTION_PLAN_OK);
        assert(plan.kind == DENEB_GCODE_MOTION_PLAN_JOG);
        assert(strcmp(plan.jog.lines[0], DENEB_GCODE_RELATIVE_MODE) == 0);
        assert(strcmp(plan.jog.lines[1], "G1 X5 F3000") == 0);
        assert(strcmp(plan.jog.lines[2], DENEB_GCODE_ABSOLUTE_MODE) == 0);

        assert(deneb_gcode_plan_motion_from_json(
                   "{\"x\":12,\"y\":34.5,\"speed\":150}", &plan) ==
               DENEB_GCODE_MOTION_PLAN_OK);
        assert(plan.kind == DENEB_GCODE_MOTION_PLAN_ABSOLUTE_POSITION);
        assert(strcmp(plan.absolute_lines[0], DENEB_GCODE_ABSOLUTE_MODE) == 0);
        assert(strcmp(plan.absolute_lines[1], "G1 X12 Y34.5 F9e+03") == 0);

        assert(deneb_gcode_plan_motion_from_json(
                   "{\"axis\":\"E\",\"distance\":5}", &plan) ==
               DENEB_GCODE_MOTION_PLAN_ERR_JOG_SHAPE);
        assert(deneb_gcode_plan_motion_from_json(
                   "{\"axis\":\"X\",\"distance\":1.5}", &plan) ==
               DENEB_GCODE_MOTION_PLAN_ERR_JOG_DISTANCE);
        assert(deneb_gcode_plan_motion_from_json(
                   "{\"x\":\"bad\"}", &plan) ==
               DENEB_GCODE_MOTION_PLAN_ERR_X);
        assert(deneb_gcode_plan_motion_from_json(
                   "{\"x\":999}", &plan) ==
               DENEB_GCODE_MOTION_PLAN_ERR_VOLUME);
        assert(deneb_gcode_plan_motion_from_json(
                   "{\"x\":1,\"speed\":0}", &plan) ==
               DENEB_GCODE_MOTION_PLAN_ERR_SPEED);
        assert(deneb_gcode_plan_motion_from_json("{}", &plan) ==
               DENEB_GCODE_MOTION_PLAN_ERR_EXPECTED);
        assert(deneb_gcode_plan_motion_from_json("{\"x\":1}", NULL) ==
               DENEB_GCODE_MOTION_PLAN_ERR_EXPECTED);
        assert(strcmp(deneb_gcode_motion_plan_error_response(
                          DENEB_GCODE_MOTION_PLAN_ERR_JOG_SHAPE),
                      "{\"message\":\"Expected {\\\"axis\\\":\\\"X|Y|Z\\\",\\\"distance\\\":number}\"}") == 0);
        assert(strcmp(deneb_gcode_motion_plan_error_response(
                          DENEB_GCODE_MOTION_PLAN_ERR_JOG_DISTANCE),
                      "{\"message\":\"Distance must be a whole number from 1 to 50 mm\"}") == 0);
        assert(strcmp(deneb_gcode_motion_plan_error_response(
                          DENEB_GCODE_MOTION_PLAN_ERR_X),
                      "{\"message\":\"Invalid x position\"}") == 0);
        assert(strcmp(deneb_gcode_motion_plan_error_response(
                          DENEB_GCODE_MOTION_PLAN_ERR_Y),
                      "{\"message\":\"Invalid y position\"}") == 0);
        assert(strcmp(deneb_gcode_motion_plan_error_response(
                          DENEB_GCODE_MOTION_PLAN_ERR_Z),
                      "{\"message\":\"Invalid z position\"}") == 0);
        assert(strcmp(deneb_gcode_motion_plan_error_response(
                          DENEB_GCODE_MOTION_PLAN_ERR_VOLUME),
                      "{\"message\":\"Position is outside the printable volume\"}") == 0);
        assert(strcmp(deneb_gcode_motion_plan_error_response(
                          DENEB_GCODE_MOTION_PLAN_ERR_SPEED),
                      "{\"message\":\"Invalid movement speed\"}") == 0);
        assert(strcmp(deneb_gcode_motion_plan_error_response(
                          DENEB_GCODE_MOTION_PLAN_ERR_EXPECTED),
                      "{\"message\":\"Expected jog {\\\"axis\\\":\\\"X|Y|Z\\\",\\\"distance\\\":number} or position {\\\"x\\\":number,\\\"y\\\":number,\\\"z\\\":number}\"}") == 0);
    }

    assert(deneb_gcode_format_nozzle_target(210.0f, value, sizeof(value)) == 0);
    assert(strcmp(value, "M104 S210") == 0);
    assert(deneb_gcode_format_bed_target(60.0f, value, sizeof(value)) == 0);
    assert(strcmp(value, "M140 S60") == 0);
    assert(deneb_gcode_format_heater_target(DENEB_GCODE_HEATER_NOZZLE,
                                            205.0f, value,
                                            sizeof(value)) == 0);
    assert(strcmp(value, "M104 S205") == 0);
    assert(deneb_gcode_format_heater_target(DENEB_GCODE_HEATER_BED,
                                            55.0f, value,
                                            sizeof(value)) == 0);
    assert(strcmp(value, "M140 S55") == 0);
    assert(deneb_gcode_format_heater_target((deneb_gcode_heater_t)99,
                                            55.0f, value,
                                            sizeof(value)) != 0);
    {
        float temp = -1.0f;

        assert(deneb_gcode_plan_temperature_target_from_json(
                   DENEB_GCODE_HEATER_NOZZLE, "{\"temperature\":215}",
                   &temp, value, sizeof(value)) == 0);
        assert(temp == 215.0f);
        assert(strcmp(value, "M104 S215") == 0);

        assert(deneb_gcode_plan_temperature_target_from_json(
                   DENEB_GCODE_HEATER_BED, "{\"target\":40}",
                   &temp, value, sizeof(value)) == 0);
        assert(temp == 40.0f);
        assert(strcmp(value, "M140 S40") == 0);

        assert(deneb_gcode_plan_temperature_target_from_json(
                   DENEB_GCODE_HEATER_NOZZLE, "{\"temperature\":999}",
                   &temp, value, sizeof(value)) == 0);
        assert(temp == DENEB_GCODE_MAX_NOZZLE_TEMP_C);
        assert(strcmp(value, "M104 S260") == 0);

        assert(deneb_gcode_plan_temperature_target_from_json(
                   DENEB_GCODE_HEATER_BED, "{\"target\":999}",
                   &temp, value, sizeof(value)) == 0);
        assert(temp == DENEB_GCODE_MAX_BED_TEMP_C);
        assert(strcmp(value, "M140 S110") == 0);

        assert(deneb_gcode_plan_temperature_target_from_json(
                   DENEB_GCODE_HEATER_BED, "{\"temperature\":-10}",
                   &temp, value, sizeof(value)) == 0);
        assert(temp == 0.0f);
        assert(strcmp(value, "M140 S0") == 0);

        assert(deneb_gcode_plan_temperature_target_from_json(
                   DENEB_GCODE_HEATER_BED, "{}", &temp, value,
                   sizeof(value)) == 0);
        assert(temp == 0.0f);
        assert(strcmp(value, "M140 S0") == 0);

        assert(deneb_gcode_plan_temperature_target_from_json(
                   DENEB_GCODE_HEATER_BED, "{\"temperature\":\"hot\"}",
                   &temp, value, sizeof(value)) != 0);
        assert(deneb_gcode_plan_temperature_target_from_json(
                   DENEB_GCODE_HEATER_BED, "{\"target\":\"hot\"}",
                   &temp, value, sizeof(value)) != 0);
        assert(deneb_gcode_plan_temperature_target_from_json(
                   (deneb_gcode_heater_t)99, "{\"temperature\":1}",
                   &temp, value, sizeof(value)) != 0);
        assert(deneb_gcode_plan_temperature_target_from_json(
                   DENEB_GCODE_HEATER_NOZZLE, "{\"temperature\":1}",
                   NULL, value, sizeof(value)) != 0);
        assert(strcmp(deneb_gcode_temperature_target_error_response(),
                      "{\"message\":\"Invalid temperature\"}") == 0);
    }
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

static void test_frame_light_helpers(void)
{
    deneb_frame_light_state_t state;
    char command[320];

    assert(DENEB_FRAME_LIGHT_DEFAULT_BRIGHTNESS == 100);
    assert(strcmp(DENEB_FRAME_LIGHT_LEGACY_UCI_KEY,
                  "ultimaker.option.framelight") == 0);
    assert(strcmp(DENEB_FRAME_LIGHT_ENABLED_UCI_KEY,
                  "deneb.frame_light.enabled") == 0);
    assert(strcmp(DENEB_FRAME_LIGHT_BRIGHTNESS_UCI_KEY,
                  "deneb.frame_light.brightness") == 0);

    assert(deneb_frame_light_clamp_brightness(-1) == 0);
    assert(deneb_frame_light_clamp_brightness(101) == 100);
    assert(deneb_frame_light_clamp_brightness(42) == 42);

    deneb_frame_light_state_init(&state);
    assert(!state.enabled);
    assert(state.brightness == DENEB_FRAME_LIGHT_DEFAULT_BRIGHTNESS);
    assert(deneb_frame_light_output_brightness(&state) == 0);

    deneb_frame_light_state_from_values(75, -1, -1, &state);
    assert(state.enabled);
    assert(state.brightness == 75);
    assert(deneb_frame_light_output_brightness(&state) == 75);

    deneb_frame_light_state_from_values(75, 40, 0, &state);
    assert(!state.enabled);
    assert(state.brightness == 40);
    assert(deneb_frame_light_output_brightness(&state) == 0);

    deneb_frame_light_state_from_values(-1, 0, 1, &state);
    assert(state.enabled);
    assert(state.brightness == DENEB_FRAME_LIGHT_DEFAULT_BRIGHTNESS);
    assert(deneb_frame_light_format_save_command(&state, command,
                                                 sizeof(command)) == 0);
    assert(strstr(command, "deneb.frame_light.enabled='1'") != NULL);
    assert(strstr(command, "deneb.frame_light.brightness='100'") != NULL);
    assert(strstr(command, "ultimaker.option.framelight='100'") != NULL);

    state.enabled = 0;
    state.brightness = 37;
    assert(deneb_frame_light_format_save_command(&state, command,
                                                 sizeof(command)) == 0);
    assert(strstr(command, "deneb.frame_light.enabled='0'") != NULL);
    assert(strstr(command, "deneb.frame_light.brightness='37'") != NULL);
    assert(strstr(command, "ultimaker.option.framelight='0'") != NULL);
    assert(deneb_frame_light_format_save_command(&state, command, 8) != 0);
}

static void test_diagnostics_export_helpers(void)
{
    const char *probe = deneb_diagnostics_export_usb_available_command();
    char command[2048];

    assert(strcmp(DENEB_DIAGNOSTICS_EXPORT_TMP_DIR,
                  "/tmp/deneb-log-export") == 0);
    assert(strcmp(DENEB_DIAGNOSTICS_EXPORT_LOG_PATH,
                  "/tmp/deneb-log-export.log") == 0);
    assert(strstr(probe, "/mnt/sda1") != NULL);
    assert(strstr(probe, "/mnt/usb") != NULL);
    assert(strstr(probe, "/media/usb") != NULL);
    assert(strstr(probe, "[ -w \"$USB\" ]") != NULL);

    assert(deneb_diagnostics_export_format_command(command,
                                                   sizeof(command)) == 0);
    assert(strstr(command, "No writable USB mount") != NULL);
    assert(strstr(command, "UM2C_${NAME}_v${VER}_${STAMP}") != NULL);
    assert(strstr(command, "logread.txt") != NULL);
    assert(strstr(command, "digitalfactory_service_status.txt") != NULL);
    assert(strstr(command, "cp /tmp/deneb*.log") != NULL);
    assert(strstr(command, "grep -v ssid") != NULL);
    assert(strstr(command, "grep -v key") != NULL);
    assert(strstr(command, "grep -v encryption") != NULL);
    assert(strstr(command, "tar -czf \"$OUT.tar.gz\"") != NULL);
    assert(strstr(command, DENEB_DIAGNOSTICS_EXPORT_LOG_PATH) != NULL);
    assert(deneb_diagnostics_export_format_command(command, 16) != 0);
    assert(deneb_diagnostics_export_format_command(NULL, sizeof(command)) != 0);
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

    assert(deneb_manual_motion_plan_request(
               "{\"action\":\"z_home\"}", &plan) ==
           DENEB_MANUAL_MOTION_PLAN_OK);
    assert(plan.kind == DENEB_MANUAL_MOTION_GCODE);
    assert(strcmp(plan.command, DENEB_GCODE_HOME_Z) == 0);

    assert(deneb_manual_motion_plan_request(
               "{\"home\":true}", &plan) == DENEB_MANUAL_MOTION_PLAN_OK);
    assert(plan.kind == DENEB_MANUAL_MOTION_MACRO);
    assert(strcmp(plan.command, DENEB_PRINT_MACRO_HOME_AND_CENTER_HEAD) == 0);

    assert(deneb_manual_motion_plan_request(
               "{\"action\":\"bogus\"}", &plan) ==
           DENEB_MANUAL_MOTION_PLAN_UNKNOWN_ACTION);
    assert(deneb_manual_motion_plan_request("{}", &plan) ==
           DENEB_MANUAL_MOTION_PLAN_BAD_REQUEST);
    assert(deneb_manual_motion_plan_request(
               "{\"action\":\"home\"}", NULL) ==
           DENEB_MANUAL_MOTION_PLAN_BAD_REQUEST);
    assert(strcmp(deneb_manual_motion_plan_error_response(
                      DENEB_MANUAL_MOTION_PLAN_BAD_REQUEST),
                  "{\"message\":\"Expected {\\\"action\\\":\\\"home|z_home|bed_up|bed_down\\\"}\"}") == 0);
    assert(strcmp(deneb_manual_motion_plan_error_response(
                      DENEB_MANUAL_MOTION_PLAN_UNKNOWN_ACTION),
                  "{\"message\":\"Unknown motion action\"}") == 0);
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
               "\"topcapIsPresent\":\"yes\",\"firmware\":\"Apr 30 2020\","
               "\"machineType\":\"E2\",\"pcbId\":4,\"pcbIdValid\":true,"
               "\"denebErrorKey\":\"serial_fault\","
               "\"denebErrorCategory\":\"serial\","
               "\"denebErrorDetail\":\"flow control sequence desync\","
               "\"flowLastResponse\":\"Error:ProtoError\","
               "\"flowInflight\":3,\"flowSent\":55,\"flowAck\":52,"
               "\"flowResend\":1,\"flowReject\":2,\"jobLineNumber\":54,"
               "\"received_faults\":1}",
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
    assert(strcmp(payload.firmware, "Apr 30 2020") == 0);
    assert(strcmp(payload.machine_type, "E2") == 0);
    assert(payload.pcb_id == 4);
    assert(payload.pcb_id_valid);
    assert(strcmp(payload.error_key, "serial_fault") == 0);
    assert(strcmp(payload.error_category, "serial") == 0);
    assert(strcmp(payload.error_detail,
                  "flow control sequence desync") == 0);
    assert(strcmp(payload.flow_last_response, "Error:ProtoError") == 0);
    assert(payload.flow_inflight == 3);
    assert(payload.flow_sent == 55);
    assert(payload.flow_ack == 52);
    assert(payload.flow_resend == 1);
    assert(payload.flow_reject == 2);
    assert(payload.job_line_number == 54);
    assert(payload.progress > 49.9f && payload.progress < 50.1f);
    assert(payload.nozzle_temp_cur > 199.4f);
    assert(payload.observation.file == payload.file);
    assert(!payload.has_native_active);
    assert(!payload.has_native_stop_allowed);

    assert(deneb_status_payload_parse(
               "{\"file\":\"/home/3D/cube.gcode\",\"req\":\"JOB\","
               "\"denebState\":\"error\",\"denebErrorKey\":\"command_fault\","
               "\"received_faults\":[1]}",
               &payload) == 0);
    assert(payload.has_error);

    assert(deneb_status_payload_parse(
               "{\"file\":\"/home/3D/cube.gcode\",\"req\":\"JOB\","
               "\"denebState\":\"idle\",\"denebErrorKey\":\"none\","
               "\"received_faults\":[]}",
               &payload) == 0);
    assert(!payload.has_error);

    assert(deneb_status_payload_parse(
               "{\"file\":\"/home/3D/cube.gcode\",\"req\":\"JOB\","
               "\"denebState\":\"error\",\"denebErrorKey\":\"none\","
               "\"received_faults\":[]}",
               &payload) == 0);
    assert(payload.has_error);

    assert(deneb_status_payload_parse(
               "{\"file\":\"/home/3D/cube.gcode\",\"req\":\"JOB\","
               "\"denebState\":\"idle\",\"denebErrorKey\":\"serial_fault\","
               "\"received_faults\":[]}",
               &payload) == 0);
    assert(payload.has_error);

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

    assert(deneb_status_payload_parse(
               "{\"file\":\"/home/3D/cube.gcode\",\"req\":\"PREHEATING\","
               "\"headTset\":210,\"bedTset\":60,"
               "\"denebActive\":true,\"denebStopAllowed\":true}",
               &payload) == 0);
    assert(payload.has_native_active);
    assert(payload.native_active);
    assert(payload.has_native_stop_allowed);
    assert(payload.native_stop_allowed);
    assert(payload.is_printing);

    assert(deneb_status_payload_parse(
               "{\"file\":\"/home/3D/cube.gcode\",\"req\":\"ABORT\","
               "\"denebActive\":false,\"denebStopAllowed\":false}",
               &payload) == 0);
    assert(payload.has_native_active);
    assert(!payload.native_active);
    assert(payload.has_native_stop_allowed);
    assert(!payload.native_stop_allowed);
    assert(!payload.is_printing);

    assert(deneb_status_payload_parse(
               "{\"file\":\"/home/3D/cube.gcode\",\"req\":\"ABORT\","
               "\"denebActive\":true,\"denebStopAllowed\":false}",
               &payload) == 0);
    assert(payload.has_native_active);
    assert(payload.native_active);
    assert(payload.has_native_stop_allowed);
    assert(!payload.native_stop_allowed);
    assert(payload.is_printing);
}

static void test_status_state_helpers(void)
{
    deneb_backend_status_state_t state;
    deneb_backend_status_state_t prev;
    deneb_status_filename_context_t ctx;
    deneb_print_context_flags_t flags;
    deneb_print_stop_guard_t stop_guard;
    deneb_status_transition_t transition;
    deneb_print_preheat_tracker_t preheat_tracker;
    char retained[128] = "";

    deneb_status_state_init(&state);
    deneb_print_stop_guard_init(&stop_guard, 3000);
    assert(deneb_print_stop_guard_begin(&stop_guard, 1000));
    assert(stop_guard.in_flight);

    assert(deneb_status_state_apply_json(
               &state, NULL,
               "{\"file\":\"/home/3D/cube.gcode\",\"source\":\"Cura\","
               "\"uuid\":\"job-1\",\"req\":\"PREHEATING\","
               "\"Ttot\":120,\"Tleft\":120,\"headTset\":210,\"bedTset\":60,"
               "\"headTcur\":31.5,\"bedTcur\":28.0,"
               "\"denebActive\":true,\"denebStopAllowed\":true,"
               "\"topcapTemperature\":33.0,\"topcapIsPresent\":true,"
               "\"firmware\":\"Apr 30 2020\",\"machineType\":\"E2\","
               "\"pcbId\":4,\"pcbIdValid\":true}",
               retained, sizeof(retained), &stop_guard, 1234) == 0);
    assert(state.connected);
    assert(state.last_update_ms == 1234);
    assert(state.is_printing);
    assert(!state.is_paused);
    assert(state.native_active);
    assert(state.native_stop_allowed);
    assert(state.has_native_active);
    assert(state.has_native_stop_allowed);
    assert(strcmp(state.filename, "cube.gcode") == 0);
    assert(strcmp(retained, "cube.gcode") == 0);
    assert(strcmp(state.source, "Cura") == 0);
    assert(strcmp(state.uuid, "job-1") == 0);
    assert(strcmp(state.firmware, "Apr 30 2020") == 0);
    assert(strcmp(state.machine_type, "E2") == 0);
    assert(state.pcb_id == 4);
    assert(state.pcb_id_valid);
    assert(state.topcap_present);
    assert(state.topcap_temp_cur > 32.9f);
    assert(stop_guard.in_flight);

    ctx = deneb_status_state_filename_context(&state);
    assert(strcmp(ctx.req, DENEB_PRINT_REQ_PREHEATING) == 0);
    assert(strcmp(ctx.filename, "cube.gcode") == 0);
    assert(deneb_status_state_has_print_name(&state, NULL));
    assert(deneb_status_state_has_print_context(&state));
    assert(!deneb_status_state_has_abort_context(&state, 0));
    assert(deneb_status_state_has_abort_context(&state, 1));
    flags = deneb_status_state_context_flags(&state, 1);
    assert(flags.has_active_context);
    assert(flags.has_preparing_context);
    assert(flags.has_stoppable_context);

    prev = state;
    assert(deneb_status_state_apply_json(
               &state, &prev,
               "{\"file\":\"/home/cygnus/marlindriver/gcode/"
               DENEB_PRINT_MACRO_HOME_AND_CENTER_HEAD
               "\",\"req\":\"PREHEATING\",\"headTset\":210,"
               "\"bedTset\":60,\"denebActive\":true,"
               "\"denebStopAllowed\":true}",
               retained, sizeof(retained), &stop_guard, 2234) == 0);
    assert(strcmp(state.filename, "cube.gcode") == 0);
    assert(strcmp(retained, "cube.gcode") == 0);
    assert(stop_guard.in_flight);

    prev = state;
    assert(deneb_status_state_apply_json(
               &state, &prev,
               "{\"file\":\"none\",\"req\":\"\",\"Ttot\":0,\"Tleft\":0,"
               "\"headTset\":0,\"bedTset\":0,\"denebActive\":false,"
               "\"denebStopAllowed\":false}",
               retained, sizeof(retained), &stop_guard, 3234) == 0);
    assert(!state.is_printing);
    assert(!state.is_paused);
    assert(!state.native_active);
    assert(!state.native_stop_allowed);
    assert(state.filename[0] == '\0');
    assert(!deneb_status_state_has_print_context(&state));
    assert(!deneb_status_state_has_abort_context(&state, 0));
    assert(!stop_guard.in_flight);

    prev = state;
    assert(deneb_status_state_apply_json(
               &state, &prev,
               "{\"file\":\"cube.gcode\",\"req\":\"Aborting\","
               "\"denebActive\":true,\"denebStopAllowed\":false}",
               retained, sizeof(retained), NULL, 4234) == 0);
    assert(deneb_status_state_has_abort_context(&state, 0));
    assert(deneb_status_state_has_abort_context(&state, 1));

    deneb_status_state_init(&prev);
    deneb_status_state_init(&state);
    snprintf(prev.current_req, sizeof(prev.current_req), "%s",
             DENEB_PRINT_REQ_PAUSED);
    snprintf(prev.filename, sizeof(prev.filename), "%s", "cube.gcode");
    prev.is_printing = 1;
    prev.is_paused = 1;
    prev.time_total = 100;
    prev.time_left = 25;
    snprintf(state.current_req, sizeof(state.current_req), "%s",
             DENEB_PRINT_REQ_PRINTING);
    snprintf(state.filename, sizeof(state.filename), "%s", "cube.gcode");
    state.is_printing = 1;
    assert(deneb_status_state_transition_from_pair(
               &transition, &prev, &state) == 0);
    assert(transition.req_changed);
    assert(transition.print_resumed);
    assert(!transition.print_paused);
    assert(!transition.print_started);
    assert(!transition.print_ended);

    state.time_total = 100;
    state.time_left = 0;
    prev = state;
    deneb_status_state_init(&state);
    snprintf(state.current_req, sizeof(state.current_req), "%s",
             DENEB_PRINT_REQ_IDLE);
    assert(deneb_status_state_transition_from_pair(
               &transition, &prev, &state) == 0);
    assert(transition.print_ended);
    assert(!transition.print_started);
    assert(strcmp(transition.completion_label, "completed") == 0);

    deneb_print_preheat_tracker_init(&preheat_tracker);
    state.bed_temp_set = 60.0f;
    state.nozzle_temp_set = 210.0f;
    state.bed_temp_cur = 28.0f;
    state.nozzle_temp_cur = 31.0f;
    assert(deneb_status_state_preheat_events(
               &state, &preheat_tracker) ==
           DENEB_PRINT_PREHEAT_EVENT_TARGETS_ACTIVE);
    state.bed_temp_cur = 60.0f;
    state.nozzle_temp_cur = 210.0f;
    assert(deneb_status_state_preheat_events(
               &state, &preheat_tracker) ==
           DENEB_PRINT_PREHEAT_EVENT_TARGETS_READY);

    assert(deneb_status_state_apply_json(NULL, &prev, "{}", retained,
                                         sizeof(retained), &stop_guard,
                                         1) < 0);
    assert(deneb_status_state_apply_json(&state, &prev, NULL, retained,
                                         sizeof(retained), &stop_guard,
                                         1) < 0);
}

static void test_print_profile_helpers(void)
{
    char value[64];
    char command[256];
    const deneb_print_profile_material_choice_t *material;
    const deneb_print_profile_nozzle_choice_t *nozzle;

    assert(strcmp(DENEB_PRINT_PROFILE_MACHINE_FAMILY,
                  "ultimaker2_plus_connect") == 0);
    assert(strcmp(DENEB_PRINT_PROFILE_MACHINE_VARIANT,
                  "Ultimaker 2+ Connect") == 0);

    assert(deneb_print_profile_material_choice_count() == 10);
    material = deneb_print_profile_material_choice(0);
    assert(material != NULL);
    assert(strcmp(material->label, "Generic PLA") == 0);
    assert(strcmp(material->guid, DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_GUID) == 0);
    assert(deneb_print_profile_material_choice(10) == NULL);
    assert(strcmp(deneb_print_profile_material_label_from_guid(material->guid),
                  "Generic PLA") == 0);
    assert(deneb_print_profile_material_label_from_guid("unknown") == NULL);
    assert(deneb_print_profile_format_set_material_command(
               material->guid, command, sizeof(command)) == 0);
    assert(strstr(command, "material_guid='506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9'") != NULL);

    assert(deneb_print_profile_nozzle_choice_count() == 4);
    nozzle = deneb_print_profile_nozzle_choice(1);
    assert(nozzle != NULL);
    assert(strcmp(nozzle->size, DENEB_PRINT_PROFILE_DEFAULT_NOZZLE_SIZE) == 0);
    assert(strcmp(nozzle->label, "0.4 mm") == 0);
    assert(deneb_print_profile_nozzle_choice(4) == NULL);
    assert(strcmp(deneb_print_profile_nozzle_label_from_size("0.6"),
                  "0.6 mm") == 0);
    assert(strcmp(deneb_print_profile_nozzle_label_from_size("bad"),
                  DENEB_PRINT_PROFILE_DEFAULT_NOZZLE_ID) == 0);
    assert(deneb_print_profile_format_set_nozzle_command("0.60", command,
                                                         sizeof(command)) == 0);
    assert(strstr(command, "nozzle_size=0.6") != NULL);
    assert(deneb_print_profile_format_set_nozzle_command("1.2", command,
                                                         sizeof(command)) != 0);

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

    assert(strcmp(DENEB_PRINTER_UNAVAILABLE_ID, "Unavailable") == 0);
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
    deneb_print_backend_route_t route;
    char fields[256];

    assert(deneb_print_backend_is_native(DENEB_PRINT_BACKEND_NATIVE));

    route = deneb_print_backend_route(DENEB_PRINT_BACKEND_NATIVE);
    assert(route.backend == DENEB_PRINT_BACKEND_NATIVE);
    assert(strcmp(route.status_url, DENEB_PRINTSVC_STATUS_URL) == 0);
    assert(strcmp(route.command_url, DENEB_PRINTSVC_COMMAND_URL) == 0);
    assert(strcmp(deneb_print_backend_name(route.backend), "native") == 0);
    assert(deneb_print_backend_route_json_fields(&route, fields, sizeof(fields)) > 0);
    assert(strstr(fields, "\"print_backend\":\"native\"") != NULL);
    assert(strstr(fields, DENEB_PRINTSVC_STATUS_URL) != NULL);
    assert(strstr(fields, DENEB_PRINTSVC_COMMAND_URL) != NULL);
    assert(strstr(fields, "\"native_only_route\":true") != NULL);

    assert(deneb_print_backend_route_json_fields(NULL, fields, sizeof(fields)) > 0);
    assert(strstr(fields, "\"print_backend\":\"native\"") != NULL);
    assert(strstr(fields, "\"native_only_route\":true") != NULL);
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
    assert(strstr(json, "\"cloud_job_id\"") == NULL);
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
    snprintf(job.cloud_job_id, sizeof(job.cloud_job_id), "cloud-job-456");
    assert(deneb_pending_job_change_count(&job) == 2);
    assert(deneb_pending_job_serialize(&job, json, sizeof(json)) > 0);
    assert(strstr(json, "\"cloud_job_id\":\"cloud-job-456\"") != NULL);
    assert(strstr(json, "\"status\":\"wait_user_action\"") != NULL);
    assert(strstr(json, "\"started\":false") != NULL);
    assert(strstr(json, "\"printer_uuid\"") == NULL);
    assert(strstr(json, "\"assigned_to\"") != NULL);
    assert(strstr(json, "\"guid\":\"target-guid\"") == NULL);
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
    deneb_pending_job_conflict_prompt_t prompt;
    char pending_json[8192];

    deneb_pending_job_init(&job, "/home/3D/deneb-uploads/write-test.gcode");
    job.tracker = 77;
    assert(deneb_pending_job_write_file(&job, path) == 0);
    assert(deneb_pending_job_file_load(path, &loaded) == 0);
    assert(loaded.tracker == 77);
    assert(strcmp(loaded.path, "/home/3D/deneb-uploads/write-test.gcode") == 0);
    assert(strcmp(loaded.name, "write-test.gcode") == 0);
    assert(!deneb_pending_job_file_has_conflict(&loaded));
    deneb_pending_job_conflict_prompt_init(&prompt);
    assert(strcmp(prompt.job_name, "network print") == 0);
    assert(strcmp(prompt.loaded_name, "loaded material") == 0);
    assert(strcmp(prompt.target_name, "sliced material") == 0);
    assert(!prompt.is_pending);
    assert(!prompt.has_conflict);
    assert(deneb_pending_job_file_conflict_prompt(&loaded, &prompt) != 0);
    assert(strcmp(prompt.job_name, "write-test.gcode") == 0);
    assert(prompt.is_pending);
    assert(!prompt.has_conflict);
    loaded.has_configuration_changes = 1;
    loaded.has_material_change = 1;
    snprintf(loaded.origin_name, sizeof(loaded.origin_name), "%s",
             "Loaded PLA");
    snprintf(loaded.target_name, sizeof(loaded.target_name), "%s",
             "Target PETG");
    assert(deneb_pending_job_file_conflict_prompt(&loaded, &prompt) == 0);
    assert(strcmp(prompt.job_name, "write-test.gcode") == 0);
    assert(strcmp(prompt.loaded_name, "Loaded PLA") == 0);
    assert(strcmp(prompt.target_name, "Target PETG") == 0);
    assert(prompt.is_pending);
    assert(prompt.has_conflict);
    assert(deneb_pending_job_file_conflict_prompt(NULL, &prompt) != 0);
    assert(deneb_pending_job_file_conflict_prompt(&loaded, NULL) != 0);
    assert(deneb_pending_job_file_clear(path) == 0);

    assert(deneb_pending_job_file_clear_default() == 0);
    deneb_pending_job_file_read_default_array_or_empty(pending_json,
                                                       sizeof(pending_json));
    assert(strcmp(pending_json, "[]") == 0);
    job.tracker = 78;
    assert(deneb_pending_job_write_default(&job) == 0);
    assert(deneb_pending_job_file_read_default_raw_array(
               pending_json, sizeof(pending_json), NULL) == 0);
    assert(strstr(pending_json, "\"deneb_tracker\":78") != NULL);
    deneb_pending_job_file_read_default_array_or_empty(pending_json,
                                                       sizeof(pending_json));
    assert(strstr(pending_json, "\"deneb_tracker\":78") != NULL);
    assert(deneb_pending_job_file_clear_default() == 0);
}

typedef struct {
    int start_allowed;
    int fail_abort;
    int fail_job;
    int start_checks;
    int abort_calls;
    int job_calls;
    char job_path[256];
    char job_source[32];
    char job_uuid[64];
    char job_cloud_job_id[96];
    float bed_target;
    float nozzle_target;
} pending_dispatch_fake_t;

static int pending_dispatch_fake_start_allowed(void *ctx)
{
    pending_dispatch_fake_t *fake = (pending_dispatch_fake_t *)ctx;
    if (fake)
        fake->start_checks++;
    return fake && fake->start_allowed;
}

static int pending_dispatch_fake_abort(void *ctx)
{
    pending_dispatch_fake_t *fake = (pending_dispatch_fake_t *)ctx;
    if (!fake || fake->fail_abort)
        return -1;
    fake->abort_calls++;
    return 0;
}

static int pending_dispatch_fake_job(void *ctx,
                                     const deneb_print_job_start_plan_t *plan)
{
    pending_dispatch_fake_t *fake = (pending_dispatch_fake_t *)ctx;
    if (!fake || !plan)
        return -1;
    fake->job_calls++;
    if (fake->fail_job)
        return -1;
    snprintf(fake->job_path, sizeof(fake->job_path), "%s", plan->path);
    snprintf(fake->job_source, sizeof(fake->job_source), "%s", plan->source);
    snprintf(fake->job_uuid, sizeof(fake->job_uuid), "%s", plan->uuid);
    snprintf(fake->job_cloud_job_id, sizeof(fake->job_cloud_job_id), "%s",
             plan->cloud_job_id);
    fake->bed_target = plan->bed_target;
    fake->nozzle_target = plan->nozzle_target;
    return 0;
}

static void write_pending_dispatch_fixture(const char *path,
                                           const char *status)
{
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    fprintf(f, "[{\"name\":\"Cube\",\"path\":\"/home/3D/cube.gcode\","
            "\"status\":\"%s\",\"deneb_tracker\":42,"
            "\"configuration_changes_required\":["
            "{\"type_of_change\":\"material_change\","
            "\"origin_name\":\"PLA\",\"target_name\":\"PETG\"}]}]\n",
            status);
    fclose(f);
}

static void test_pending_job_dispatch_helpers(void)
{
    const char *path = "/tmp/deneb-pending-dispatch-test.json";
    pending_dispatch_fake_t fake;
    deneb_pending_job_dispatch_ops_t ops;
    deneb_pending_job_file_t loaded;

    memset(&fake, 0, sizeof(fake));
    fake.start_allowed = 1;
    ops.ctx = &fake;
    ops.start_allowed = pending_dispatch_fake_start_allowed;
    ops.send_abort = pending_dispatch_fake_abort;
    ops.send_job = pending_dispatch_fake_job;

    write_pending_dispatch_fixture(path, "wait_user_action");
    assert(deneb_pending_job_dispatch_from_path(path, DENEB_PRINT_REQ_PREPARE,
                                                &ops) == 0);
    assert(fake.job_calls == 1);
    assert(fake.abort_calls == 0);
    assert(strcmp(fake.job_path, "/home/3D/cube.gcode") == 0);
    assert(strcmp(fake.job_source, DENEB_PRINT_DEFAULT_JOB_SOURCE) == 0);
    assert(strcmp(fake.job_uuid, DENEB_PRINT_DEFAULT_JOB_UUID) == 0);
    assert(fake.job_cloud_job_id[0] == '\0');
    assert(deneb_pending_job_file_load(path, &loaded) == 0);
    assert(!deneb_pending_job_file_has_conflict(&loaded));

    assert(deneb_pending_job_dispatch_from_path(path, DENEB_COMMAND_VERB_ABORT,
                                                &ops) == 0);
    assert(fake.abort_calls == 1);
    assert(deneb_pending_job_file_load(path, &loaded) != 0);

    write_pending_dispatch_fixture(path, "wait_user_action");
    fake.start_allowed = 0;
    fake.job_calls = 0;
    assert(deneb_pending_job_dispatch_from_path(path, DENEB_PRINT_REQ_PREPARE,
                                                &ops) != 0);
    assert(fake.job_calls == 0);
    assert(deneb_pending_job_file_load(path, &loaded) == 0);
    assert(deneb_pending_job_file_has_conflict(&loaded));

    fake.start_allowed = 1;
    fake.fail_job = 1;
    assert(deneb_pending_job_dispatch_from_path(path, DENEB_PRINT_REQ_PREPARE,
                                                &ops) != 0);
    assert(deneb_pending_job_file_load(path, &loaded) == 0);
    assert(deneb_pending_job_file_has_conflict(&loaded));

    fake.fail_job = 0;
    fake.fail_abort = 1;
    assert(deneb_pending_job_dispatch_from_path(path, DENEB_COMMAND_VERB_ABORT,
                                                &ops) != 0);
    assert(deneb_pending_job_file_load(path, &loaded) == 0);

    assert(deneb_pending_job_dispatch_from_path(path, "PAUSE", &ops) != 0);
    assert(deneb_pending_job_dispatch_from_path(path, DENEB_PRINT_REQ_PREPARE,
                                                NULL) != 0);
    deneb_pending_job_file_clear(path);
}

static void test_pending_job_upload_default_check(void)
{
    deneb_pending_job_t job;
    deneb_pending_job_upload_check_t check;

    deneb_pending_job_file_clear_default();
    assert(!deneb_pending_job_file_has_pending_default());
    assert(!deneb_pending_job_file_has_conflict_default());
    assert(deneb_pending_job_file_check_upload_default(
               "/home/3D/deneb-uploads/cube.gcode", "cube.gcode",
               &check) == 0);
    assert(check.status == DENEB_PENDING_JOB_UPLOAD_CLEAR);

    deneb_pending_job_init(&job, "/home/3D/deneb-uploads/cube.gcode");
    job.tracker = 88;
    assert(deneb_pending_job_write_default(&job) == 0);
    assert(deneb_pending_job_file_has_pending_default());
    assert(!deneb_pending_job_file_has_conflict_default());

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

    job.material_change_required = 1;
    assert(deneb_pending_job_write_default(&job) == 0);
    assert(deneb_pending_job_file_has_pending_default());
    assert(deneb_pending_job_file_has_conflict_default());

    deneb_pending_job_file_clear_default();
}

static void test_pending_job_registration_policy(void)
{
    const char *plain_path = "/tmp/deneb-registration-plain.gcode";
    const char *conflict_path = "/tmp/deneb-registration-conflict.gcode";
    deneb_pending_job_registration_t registration;
    deneb_pending_job_registration_dispatch_ops_t ops;
    pending_dispatch_fake_t fake;
    FILE *f;

    f = fopen(plain_path, "wb");
    assert(f != NULL);
    fputs("G28\n", f);
    fclose(f);

    assert(deneb_pending_job_registration_prepare(
               plain_path, NULL, NULL, NULL, 123, &registration) == 0);
    assert(registration.job.tracker == 123);
    assert(registration.change_count == 0);
    assert(registration.should_start_immediately);
    assert(strcmp(registration.job.path, plain_path) == 0);
    assert(strcmp(registration.job.source, DENEB_PRINT_DEFAULT_JOB_SOURCE) == 0);
    assert(registration.job.cloud_job_id[0] == '\0');

    memset(&fake, 0, sizeof(fake));
    fake.start_allowed = 1;
    ops.ctx = &fake;
    ops.start_allowed = pending_dispatch_fake_start_allowed;
    ops.send_job = pending_dispatch_fake_job;

    assert(deneb_pending_job_registration_dispatch_start(
               &registration, &ops) == 0);
    assert(fake.start_checks == 1);
    assert(fake.job_calls == 1);
    assert(strcmp(fake.job_path, plain_path) == 0);
    assert(strcmp(fake.job_source, DENEB_PRINT_DEFAULT_JOB_SOURCE) == 0);
    assert(fake.job_uuid[0] != '\0');
    assert(fake.job_cloud_job_id[0] == '\0');
    assert(fake.bed_target == 0.0f);
    assert(fake.nozzle_target == 0.0f);

    memset(&fake, 0, sizeof(fake));
    fake.start_allowed = 0;
    ops.ctx = &fake;
    assert(deneb_pending_job_registration_dispatch_start(
               &registration, &ops) != 0);
    assert(fake.start_checks == 1);
    assert(fake.job_calls == 0);

    memset(&fake, 0, sizeof(fake));
    fake.start_allowed = 1;
    fake.fail_job = 1;
    ops.ctx = &fake;
    assert(deneb_pending_job_registration_dispatch_start(
               &registration, &ops) != 0);
    assert(fake.start_checks == 1);
    assert(fake.job_calls == 1);

    assert(deneb_pending_job_registration_dispatch_start(NULL, &ops) != 0);
    assert(deneb_pending_job_registration_dispatch_start(
               &registration, NULL) != 0);

    f = fopen(conflict_path, "wb");
    assert(f != NULL);
    fputs("; material_guid='material-123'\n; nozzle_size=0.8\n", f);
    fclose(f);

    assert(deneb_pending_job_registration_prepare(
               conflict_path, "Digital Factory",
               "01234567-89ab-cdef-0123-456789abcdef",
               "cloud-job-456", 456, &registration) == 0);
    assert(registration.job.tracker == 456);
    assert(strcmp(registration.job.material_guid, "material-123") == 0);
    assert(strcmp(registration.job.nozzle_id, "0.8 mm") == 0);
    assert(strcmp(registration.job.source, "Digital Factory") == 0);
    assert(strcmp(registration.job.uuid,
                  "01234567-89ab-cdef-0123-456789abcdef") == 0);
    assert(strcmp(registration.job.cloud_job_id, "cloud-job-456") == 0);
    assert(registration.change_count >= 1);
    assert(!registration.should_start_immediately);

    memset(&fake, 0, sizeof(fake));
    fake.start_allowed = 1;
    ops.ctx = &fake;
    assert(deneb_pending_job_registration_dispatch_start(
               &registration, &ops) == 0);
    assert(fake.start_checks == 0);
    assert(fake.job_calls == 0);
    assert(deneb_pending_job_registration_dispatch_start(
               &registration, NULL) == 0);

    remove(plain_path);
    remove(conflict_path);
}

static void test_print_job_file_metadata(void)
{
    const char *path = "/tmp/deneb-print-job-metadata-test.gcode";
    deneb_print_job_file_metadata_t meta;
    deneb_print_job_upload_storage_plan_t upload_plan;
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
    assert(deneb_print_job_file_upload_storage_plan(
               "..\\nested\\cura cube.gcode", &upload_plan) == 0);
    assert(strcmp(upload_plan.filename, "cura cube.gcode") == 0);
    assert(strcmp(upload_plan.dest_path,
                  DENEB_PRINT_JOB_SPOOL_DIR "/cura cube.gcode") == 0);
    assert(deneb_print_job_file_upload_storage_plan(
               "", &upload_plan) == 0);
    assert(strcmp(upload_plan.filename, "upload.gcode") == 0);
    assert(strcmp(upload_plan.dest_path,
                  DENEB_PRINT_JOB_SPOOL_DIR "/upload.gcode") == 0);
    assert(deneb_print_job_file_upload_storage_plan(
               "cube.gcode", NULL) != 0);
    assert(deneb_print_job_file_has_extension("print.UFP", ".ufp"));
    assert(!deneb_print_job_file_has_extension("print.gcode", ".ufp"));
    assert(deneb_print_job_file_replace_extension(
               "print.UFP", ".gcode", safe, sizeof(safe)) == 0);
    assert(strcmp(safe, "print.gcode") == 0);
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

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs(";START_OF_HEADER\n"
          ";EXTRUDER_TRAIN.0.MATERIAL.GUID:"
          "506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9\n"
          ";EXTRUDER_TRAIN.0.NOZZLE.DIAMETER:0.4\n"
          ";PRINT.TIME:1334\n"
          ";END_OF_HEADER\n", f);
    fclose(f);
    deneb_print_job_file_metadata_init(&meta);
    assert(deneb_print_job_file_metadata_load(path, &meta) == 0);
    assert(strcmp(meta.material_guid,
                  "506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9") == 0);
    assert(strcmp(meta.nozzle_size, "0.4") == 0);
    assert(meta.print_time_seconds == 1334);

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs(";TIME:42\n;TIME_ELAPSED:1.0\n", f);
    fclose(f);
    deneb_print_job_file_metadata_init(&meta);
    assert(deneb_print_job_file_metadata_load(path, &meta) == 0);
    assert(meta.print_time_seconds == 42);

    f = fopen(path, "wb");
    assert(f != NULL);
    fputs(";TIME_ELAPSED:99.0\n", f);
    fclose(f);
    deneb_print_job_file_metadata_init(&meta);
    assert(deneb_print_job_file_metadata_load(path, &meta) != 0);
    remove(path);
}

static void test_crc_and_packet(void)
{
    uint8_t packet[256];
    size_t written = 0;

    assert(deneb_crc16_ccitt((const uint8_t *)"123456789", 9) == 0x29b1);
    assert(deneb_marlin_packet_encode(7, "M105", packet, sizeof(packet), &written) == 0);
    assert(written == 10);
    assert(packet[0] == 0xff);
    assert(packet[1] == 0xff);
    assert(packet[2] == 7);
    assert(packet[3] == 4);
    assert(memcmp(packet + 4, "M105", 4) == 0);
    assert(((uint16_t)packet[8] << 8 | packet[9]) ==
           deneb_crc16_ccitt(packet + 2, 6));
}

static void test_material_catalog_helpers(void)
{
    char material_path[256] = "/tmp/deneb-material-catalog-test.xml";
    const char *catalog_dir = "/tmp/deneb-material-catalog";
    const char *import_dir = "/tmp/deneb-material-import";
    const char *import_nested_dir = "/tmp/deneb-material-import/nested";
    const char *import_path = "/tmp/deneb-material-import/nested/profile.material";
    const char *ignored_path = "/tmp/deneb-material-import/nested/readme.txt";
    const char *guid = "506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9";
    char parsed_guid[64];
    char tag_value[64];
    char *body = NULL;
    size_t body_len = 0;
    int imported = 0;
    int version = -1;
    FILE *f;

    assert(strcmp(DENEB_MATERIAL_CATALOG_DIR, "/home/3D/deneb-materials") == 0);
    assert(strcmp(DENEB_MATERIAL_IMPORT_USB_ROOT, "/mnt/sda1") == 0);
    assert(DENEB_MATERIAL_IMPORT_MAX_DEPTH == 4);
    assert(deneb_material_catalog_file_is_candidate("profile.xml"));
    assert(deneb_material_catalog_file_is_candidate("profile.fdm_material"));
    assert(deneb_material_catalog_file_is_candidate("profile.material"));
    assert(!deneb_material_catalog_file_is_candidate("profile.txt"));
    assert(!deneb_material_catalog_file_is_candidate("profile"));
    assert(!deneb_material_catalog_file_is_candidate(""));
    assert(!deneb_material_catalog_file_is_candidate(NULL));
    assert(deneb_material_catalog_copy_tag_value("<GUID> abc </GUID>",
                                                 "GUID", tag_value,
                                                 sizeof(tag_value)) == 0);
    assert(strcmp(tag_value, "abc") == 0);
    assert(deneb_material_catalog_guid_is_safe(guid));
    assert(!deneb_material_catalog_guid_is_safe("../bad-guid"));

    remove("/tmp/deneb-material-catalog/506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9.json");
    rmdir(catalog_dir);
    remove(import_path);
    remove(ignored_path);
    rmdir(import_nested_dir);
    rmdir(import_dir);
    assert(mkdir(import_dir, 0755) == 0);
    assert(mkdir(import_nested_dir, 0755) == 0);
    f = fopen(import_path, "wb");
    assert(f != NULL);
    fprintf(f, "<fdmmaterial><metadata><GUID>%s</GUID><version>7</version>"
            "</metadata></fdmmaterial>", guid);
    fclose(f);
    f = fopen(ignored_path, "wb");
    assert(f != NULL);
    fputs("not a material profile", f);
    fclose(f);
    assert(deneb_material_catalog_import_tree(import_dir, catalog_dir,
                                              DENEB_MATERIAL_IMPORT_MAX_DEPTH,
                                              &imported) == 0);
    assert(imported == 1);
    remove("/tmp/deneb-material-catalog/506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9.json");
    rmdir(catalog_dir);
    remove(import_path);
    remove(ignored_path);
    rmdir(import_nested_dir);
    rmdir(import_dir);

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

    snprintf(material_path, sizeof(material_path), "%s/uploaded.material",
             import_dir);
    assert(mkdir(import_dir, 0755) == 0);
    f = fopen(material_path, "wb");
    assert(f != NULL);
    fputs("<GUID>abcdefabcdefabcdefabcdefabcdefab</GUID>"
          "<version>7</version>", f);
    fclose(f);
    assert(deneb_material_catalog_store_uploaded_file_to_dir(
               material_path, catalog_dir, parsed_guid, sizeof(parsed_guid),
               &version) == 0);
    assert(strcmp(parsed_guid, "abcdefabcdefabcdefabcdefabcdefab") == 0);
    assert(version == 7);
    assert(access(material_path, F_OK) != 0);

    snprintf(material_path, sizeof(material_path), "%s/bad-upload.material",
             import_dir);
    f = fopen(material_path, "wb");
    assert(f != NULL);
    fputs("<GUID>bad</GUID><version>1</version>", f);
    fclose(f);
    assert(deneb_material_catalog_store_uploaded_file_to_dir(
               material_path, catalog_dir, parsed_guid, sizeof(parsed_guid),
               &version) != 0);
    assert(access(material_path, F_OK) != 0);
    remove("/tmp/deneb-material-catalog/abcdefabcdefabcdefabcdefabcdefab.json");
    rmdir(catalog_dir);
    rmdir(import_dir);

    snprintf(material_path, sizeof(material_path),
             "/tmp/deneb-material-catalog-test.xml");
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
    const char *macro_dir = "/tmp/deneb-macro-override";
    const char *macro_path = "/tmp/deneb-macro-override/init.gcode";
    FILE *f;

    assert(strcmp(DENEB_PRINTSVC_MACRO_DIR,
                  "/etc/deneb/marlindriver/gcode") == 0);
    assert(deneb_macro_resolve("missing-test-only.gcode",
                               path, sizeof(path)) != 0);
    assert(deneb_macro_name_is_safe("init.gcode"));
    assert(!deneb_macro_name_is_safe("init.gcode.bak"));
    assert(!deneb_macro_name_is_safe("gcode"));
    assert(!deneb_macro_name_is_safe("init.GCODE"));
    assert(deneb_macro_resolve("../init.gcode", path, sizeof(path)) != 0);
    assert(deneb_macro_resolve("/tmp/init.gcode", path, sizeof(path)) != 0);
    assert(deneb_macro_resolve("dir\\init.gcode", path, sizeof(path)) != 0);

    remove(macro_path);
    rmdir(macro_dir);
    assert(mkdir(macro_dir, 0755) == 0);
    assert(deneb_macro_resolve_from_dir("init.gcode", macro_dir,
                                        path, sizeof(path)) != 0);

    f = fopen(macro_path, "wb");
    assert(f != NULL);
    fputs("M105\n", f);
    fclose(f);
    assert(deneb_macro_resolve_from_dir("init.gcode", macro_dir,
                                        path, sizeof(path)) == 0);
    assert(strcmp(path, macro_path) == 0);
    assert(deneb_macro_resolve_from_dir("init.gcode", macro_dir,
                                        path, 8) != 0);
    assert(deneb_macro_resolve_from_dir("../init.gcode", macro_dir,
                                        path, sizeof(path)) != 0);
    remove(macro_path);
    assert(deneb_macro_resolve_from_dir("init.gcode", macro_dir,
                                        path, sizeof(path)) != 0);
    remove(macro_path);
    rmdir(macro_dir);
}

static void test_system_language_helpers(void)
{
    char value[32];
    char cmd[160];
    const deneb_system_language_choice_t *choice;

    assert(strcmp(DENEB_SYSTEM_LANGUAGE_DEFAULT, "en") == 0);
    assert(strcmp(DENEB_SYSTEM_LANGUAGE_READ_COMMAND,
                  "uci -q get deneb.system.language 2>/dev/null") == 0);
    assert(deneb_system_language_choice_count() == 7);
    choice = deneb_system_language_choice(4);
    assert(choice != NULL);
    assert(strcmp(choice->code, "zh-Hans") == 0);
    assert(strcmp(choice->label_key, "language.zh_Hans") == 0);
    assert(deneb_system_language_choice(99) == NULL);
    assert(deneb_system_language_code_is_valid("en"));
    assert(deneb_system_language_code_is_valid("en-1337"));
    assert(!deneb_system_language_code_is_valid(""));
    assert(!deneb_system_language_code_is_valid("en;reboot"));
    deneb_system_language_copy_or_default("de\n", value, sizeof(value));
    assert(strcmp(value, "de") == 0);
    deneb_system_language_copy_or_default("missing", value, sizeof(value));
    assert(strcmp(value, DENEB_SYSTEM_LANGUAGE_DEFAULT) == 0);
    assert(deneb_system_language_format_save_command("fr", cmd,
                                                     sizeof(cmd)) == 0);
    assert(strstr(cmd, "deneb.system.language='fr'") != NULL);
    assert(deneb_system_language_format_save_command("fr", cmd, 12) != 0);
    assert(deneb_system_language_format_save_command("bad", cmd,
                                                     sizeof(cmd)) != 0);
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
    status.position_report_count = 5;
    status.finish_drain_ticks = 6;
    status.finish_stable_reports = 2;
    snprintf(status.firmware, sizeof(status.firmware), "Apr 30 2020");
    snprintf(status.machine_type, sizeof(status.machine_type), "E2");
    status.pcb_id = 4;
    status.pcb_id_valid = true;
    snprintf(status.flow_last_response, sizeof(status.flow_last_response),
             "r000000");

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
    assert(strstr(buf, "stock.firmware=\"Apr 30 2020\"") != NULL);
    assert(strstr(buf, "stock.machineType=\"E2\"") != NULL);
    assert(strstr(buf, "stock.pcbId=4") != NULL);
    assert(strstr(buf, "stock.pcbIdValid=1") != NULL);
    assert(strstr(buf, "native.phase=printing") != NULL);
    assert(strstr(buf, "serial.ack=12") != NULL);
    assert(strstr(buf, "serial.resend=1") != NULL);
    assert(strstr(buf, "serial.reject=2") != NULL);
    assert(strstr(buf, "serial.lastFlowResponse=\"r000000\"") != NULL);
    assert(strstr(buf, "native.queueDepth=1") != NULL);
    assert(strstr(buf, "native.jobLine=42") != NULL);
    assert(strstr(buf, "native.commandLatencyMs=7") != NULL);
    assert(strstr(buf, "native.plannerStarvation=4") != NULL);
    assert(strstr(buf, "native.positionReports=5") != NULL);
    assert(strstr(buf, "native.finishDrainTicks=6") != NULL);
    assert(strstr(buf, "native.finishStableReports=2") != NULL);
    assert(strstr(buf, "event=command") != NULL);
    assert(strstr(buf, "command.verb=\"PAUSE\"") != NULL);
    remove(path);
}

static int count_substrings(const char *text, const char *needle)
{
    int count = 0;
    const char *p = text;

    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += strlen(needle);
    }

    return count;
}

static void test_diagnostics_log_throttles_hot_path_counters(void)
{
    const char *path = "/tmp/deneb-printsvc-diagnostics-throttle-test.log";
    deneb_status_t status;
    char buf[8192];
    FILE *f;
    size_t n;

    remove(path);
    deneb_status_init(&status);
    status.state = DENEB_PRINT_STATE_PRINTING;
    snprintf(status.req, sizeof(status.req), "%s", DENEB_COMMAND_VERB_JOB);
    snprintf(status.file, sizeof(status.file), "long-print.gcode");
    status.flow_ack = 1;
    status.flow_inflight = 1;
    status.job_line_number = 1;

    assert(deneb_diagnostics_log_open(path) == 0);
    deneb_diagnostics_log_status(&status, 1);

    status.flow_ack = 200;
    status.flow_inflight = 3;
    snprintf(status.flow_last_response, sizeof(status.flow_last_response),
             "o010AAA");
    status.job_line_number = 200;
    status.command_latency_ms = 18;
    deneb_diagnostics_log_status(&status, 0);

    status.flow_ack = 201;
    status.flow_inflight = 2;
    snprintf(status.flow_last_response, sizeof(status.flow_last_response),
             "o020BBB");
    status.job_line_number = 201;
    status.command_latency_ms = 19;
    deneb_diagnostics_log_status(&status, 0);

    status.flow_resend = 1;
    deneb_diagnostics_log_status(&status, 0);
    deneb_diagnostics_log_close();

    f = fopen(path, "rb");
    assert(f != NULL);
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    assert(count_substrings(buf, "event=status") == 2);
    assert(strstr(buf, "serial.ack=1") != NULL);
    assert(strstr(buf, "serial.ack=201") != NULL);
    assert(strstr(buf, "serial.resend=1") != NULL);
    remove(path);
}

static void test_diagnostics_log_truncates_oversized_file(void)
{
    const char *path = "/tmp/deneb-printsvc-diagnostics-truncate-test.log";
    FILE *f;
    long size;

    remove(path);
    f = fopen(path, "wb");
    assert(f != NULL);
    assert(fseek(f, 1024L * 1024L + 16L, SEEK_SET) == 0);
    assert(fputc('\n', f) != EOF);
    fclose(f);

    assert(deneb_diagnostics_log_open(path) == 0);
    deneb_diagnostics_log_close();

    f = fopen(path, "rb");
    assert(f != NULL);
    assert(fseek(f, 0, SEEK_END) == 0);
    size = ftell(f);
    fclose(f);

    assert(size > 0);
    assert(size < 1024L * 1024L);
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
    svc.abort_requested = 1;
    assert(deneb_command_parse("ABORT<{}", &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);
    assert(strcmp(svc.status.file, DENEB_PRINT_NONE_VALUE) == 0);
    assert(!svc.abort_requested);

    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "no active print to abort") != NULL);
}

static void test_latched_abort_uses_service_cleanup(void)
{
    deneb_print_service_t svc;
    FILE *serial_file;
    int serial_fd;

    deneb_print_service_init(&svc);
    serial_file = tmpfile();
    assert(serial_file != NULL);
    serial_fd = dup(fileno(serial_file));
    assert(serial_fd >= 0);
    svc.serial.fd = serial_fd;
    svc.status.state = DENEB_PRINT_STATE_PRINTING;
    snprintf(svc.status.file, sizeof(svc.status.file), "latched.gcode");
    svc.serial_ready = 1;
    svc.abort_requested = 1;

    assert(deneb_print_service_poll_job(&svc) == 0);
    assert(!svc.abort_requested);
    assert(svc.abort_cleanup_pending);
    assert(svc.status.state == DENEB_PRINT_STATE_ABORTING);
    assert(strcmp(svc.status.file, "latched.gcode") == 0);

    while (svc.abort_cleanup_pending) {
        deneb_flow_clear_inflight(&svc.flow);
        assert(deneb_job_control_poll_abort_cleanup(&svc) >= 0);
    }
    assert(svc.status.state == DENEB_PRINT_STATE_IDLE);
    assert(strcmp(svc.status.file, DENEB_PRINT_NONE_VALUE) == 0);
    deneb_print_service_close(&svc);
    fclose(serial_file);
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
    assert(!runtime.allow_sequence_resync);

    svc.job_active = 1;
    assert(deneb_service_context_motion_runtime(&svc, &runtime) == 0);
    assert(runtime.allow_sequence_resync);
    svc.job_active = 0;

    svc.gcode_queue_active = 1;
    assert(deneb_service_context_motion_runtime(&svc, &runtime) == 0);
    assert(runtime.allow_sequence_resync);
    svc.gcode_queue_active = 0;

    svc.abort_cleanup_pending = 1;
    assert(deneb_service_context_motion_runtime(&svc, &runtime) == 0);
    assert(runtime.allow_sequence_resync);
    svc.abort_cleanup_pending = 0;

    assert(deneb_service_context_job_streamer(NULL, &streamer) < 0);
    assert(deneb_service_context_job_streamer(&svc, NULL) < 0);
    assert(deneb_service_context_job_streamer(&svc, &streamer) == 0);
    assert(streamer.status == &svc.status);
    assert(streamer.flow == &svc.flow);
    assert(streamer.stream == &svc.job_stream);
    assert(streamer.heater_wait == &svc.heater_wait);
    assert(streamer.serial == &svc.serial);
    assert(streamer.serial_ready == &svc.serial_ready);
    assert(streamer.job_active == &svc.job_active);
    assert(streamer.job_prepare_stage == &svc.job_prepare_stage);
    assert(streamer.job_prepare_index == &svc.job_prepare_index);
    assert(streamer.job_startup_index == &svc.job_startup_index);
    assert(streamer.abort_requested == &svc.abort_requested);
    assert(streamer.finish_cleanup_pending == &svc.finish_cleanup_pending);
    assert(streamer.finish_cleanup_policy == &svc.finish_cleanup_policy);
    assert(streamer.finish_cleanup_index == &svc.finish_cleanup_index);
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

    svc.job_active = 0;
    svc.finish_cleanup_pending = 1;
    svc.job_stream.line_number = 19;
    deneb_service_context_refresh_diagnostics(&svc);
    assert(svc.status.job_queue_depth == 1);
    assert(svc.status.job_line_number == 19);

    svc.serial_ready = 1;
    svc.abort_requested = 1;
    svc.heater_wait.active = 1;
    deneb_service_context_close(&svc);
    assert(!svc.job_active);
    assert(!svc.abort_requested);
    assert(!svc.heater_wait.active);
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

    assert(deneb_command_parse("BOGUS<{}", &cmd) == 0);
    assert(deneb_service_command_handle(&svc, &cmd, reply, sizeof(reply)) < 0);
    assert(strstr(reply, "unknown command") != NULL);
    assert(svc.status.command_latency_ms <= 60000);
}

static void test_ipc_frame_policy(void)
{
    deneb_print_service_t svc;
    char reply[256];

    deneb_print_service_init(&svc);
    assert(deneb_printsvc_ipc_handle_frame(&svc, "GCODE<[\"M105\"]",
                                           reply, sizeof(reply)) == 0);
    assert(strstr(reply, "gcode accepted") != NULL);
    assert(deneb_flow_inflight(&svc.flow) == 1);

    assert(deneb_printsvc_ipc_handle_frame(&svc, "BOGUS<{}",
                                           reply, sizeof(reply)) < 0);
    assert(strstr(reply, "unknown command") != NULL);

    assert(deneb_printsvc_ipc_handle_frame(&svc, "BOGUS",
                                           reply, sizeof(reply)) < 0);
    assert(strstr(reply, "bad command") != NULL);
    assert(deneb_printsvc_ipc_handle_frame(NULL, "GCODE<[\"M105\"]",
                                           reply, sizeof(reply)) < 0);
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
    assert(deneb_print_service_poll_job(&svc) == 1);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    assert(strcmp(flow_command_by_send_order(&svc.flow, 0), "G28") == 0);

    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_print_service_poll_job(&svc) == 1);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    assert(strcmp(flow_command_by_send_order(&svc.flow, 1),
                  "G0 X105 Y0 F9000") == 0);

    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_print_service_poll_job(&svc) == 0);
    svc.status.bed_t_cur = 60.0f;
    svc.status.head_t_cur = 200.0f;
    assert(deneb_print_service_poll_job(&svc) == 0);
    assert(deneb_print_service_poll_job(&svc) == 1);
    assert(deneb_flow_inflight(&svc.flow) == 3);
    assert(strcmp(flow_command_by_send_order(&svc.flow, 2),
                  "G10 S-6.5 F1500") == 0);
    assert(strcmp(flow_command_by_send_order(&svc.flow, 3), "G10 S0 F300") == 0);
    assert(strcmp(flow_command_by_send_order(&svc.flow, 4), "G90") == 0);

    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_print_service_poll_job(&svc) == 1);
    assert(strcmp(flow_command_by_send_order(&svc.flow, 5), "M82") == 0);

    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_print_service_poll_job(&svc) == 1);
    assert(strcmp(flow_command_by_send_order(&svc.flow, 6), "G92 E0") == 0);

    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_print_service_poll_job(&svc) == 1);
    assert(strcmp(flow_command_by_send_order(&svc.flow, 7), "G0 F9000") == 0);

    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_print_service_poll_job(&svc) == 0);
    assert(svc.job_prepare_stage == 0);
    assert(deneb_print_service_poll_job(&svc) == 1);
    deneb_print_service_refresh_diagnostics(&svc);
    assert(svc.status.state == DENEB_PRINT_STATE_PRINTING);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    assert(svc.status.flow_inflight == 1);
    assert(svc.status.flow_sent == 9);
    assert(svc.status.job_queue_depth == 1);
    assert(svc.status.job_line_number == 2);
    assert(svc.status.planner_starvation_count == 1);
    remove(job_path);
}

static void test_job_completion_waits_for_finish_cleanup(void)
{
    const char *job_path = "/tmp/deneb-printsvc-job-finish-test.gcode";
    FILE *f = fopen(job_path, "wb");
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char frame[512];
    char reply[256];
    int pipefd[2] = {-1, -1};

    assert(f != NULL);
    fputs("G1 X1 Y1\n", f);
    fclose(f);

    deneb_print_service_init(&svc);
    snprintf(frame, sizeof(frame), "JOB<{\"file\":\"%s\"}", job_path);
    assert(deneb_command_parse(frame, &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply,
                                              sizeof(reply)) == 0);
    drive_service_job_prepare(&svc);
    assert(deneb_print_service_poll_job(&svc) == 1);
    assert(svc.status.state == DENEB_PRINT_STATE_PRINTING);
    assert(svc.job_active);

    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_print_service_poll_job(&svc) == 1);
    assert(svc.job_active);
    assert(svc.finish_cleanup_pending);
    assert(svc.status.state == DENEB_PRINT_STATE_PRINTING);
    assert(deneb_status_has_active_print(&svc.status));

    assert(deneb_print_service_poll_job(&svc) == 0);
    assert(svc.job_active);
    assert(svc.finish_cleanup_pending);
    assert(svc.status.state == DENEB_PRINT_STATE_PRINTING);

    assert(deneb_print_service_handle_command(&svc, &cmd, reply,
                                              sizeof(reply)) < 0);
    assert(strstr(reply, "job already active") != NULL);

    deneb_print_service_refresh_diagnostics(&svc);
    assert(svc.status.job_queue_depth == 1);
    assert(svc.status.job_line_number == 1);

    assert(pipe(pipefd) == 0);
    svc.serial.fd = pipefd[1];
    svc.serial_ready = 1;

    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_job_control_poll_finish_cleanup(&svc) == 0);
    assert(svc.finish_cleanup_pending);
    assert(svc.status.state == DENEB_PRINT_STATE_PRINTING);
    assert(svc.finish_cleanup_index > 0);
    assert(svc.status.finish_drain_ticks == 0);

    while (svc.finish_cleanup_index < svc.finish_cleanup_policy.count) {
        deneb_flow_clear_inflight(&svc.flow);
        assert(deneb_job_control_poll_finish_cleanup(&svc) == 0);
        assert(svc.finish_cleanup_pending);
        assert(svc.status.state == DENEB_PRINT_STATE_PRINTING);
    }

    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_job_control_poll_finish_cleanup(&svc) == 1);
    assert(!svc.finish_cleanup_pending);
    assert(!svc.job_active);
    assert(svc.status.state == DENEB_PRINT_STATE_COMPLETE);
    assert(strcmp(svc.status.file, DENEB_PRINT_NONE_VALUE) == 0);
    assert(svc.status.source[0] == '\0');
    assert(svc.status.uuid[0] == '\0');
    assert(svc.status.bed_t_set == 0.0f);
    assert(svc.status.head_t_set == 0.0f);
    assert(svc.job_stream.line_number == 0);
    svc.serial_ready = 0;
    svc.serial.fd = -1;
    close(pipefd[0]);
    close(pipefd[1]);

    remove(job_path);
}

static void test_repeated_jobs_release_terminal_state(void)
{
    const char *job_path = "/tmp/deneb-printsvc-repeated-job-cleanup.gcode";
    FILE *f = fopen(job_path, "wb");
    deneb_print_service_t svc;
    deneb_command_t cmd;
    char frame[512];
    char reply[256];
    int null_fd;

    assert(f != NULL);
    fputs("G1 X1 Y1\nG1 X2 Y2\nG1 X3 Y3\n", f);
    fclose(f);

    deneb_print_service_init(&svc);
    null_fd = open("/dev/null", O_WRONLY);
    assert(null_fd >= 0);
    svc.serial.fd = null_fd;
    svc.serial_ready = 1;

    snprintf(frame, sizeof(frame), "JOB<{\"file\":\"%s\"}", job_path);
    assert(deneb_command_parse(frame, &cmd) == 0);

    for (int cycle = 0; cycle < 64; cycle++) {
        int guard = 0;

        assert(deneb_print_service_handle_command(&svc, &cmd, reply,
                                                  sizeof(reply)) == 0);
        assert(svc.job_active);
        assert(svc.job_stream.fd >= 0);
        assert(svc.job_stream.path[0] != '\0');

        while ((svc.job_active || svc.finish_cleanup_pending) &&
               guard++ < 256) {
            deneb_flow_clear_inflight(&svc.flow);
            assert(deneb_print_service_poll_job(&svc) >= 0);
            deneb_flow_clear_inflight(&svc.flow);
            assert(deneb_job_control_poll_finish_cleanup(&svc) >= 0);
        }

        assert(guard < 256);
        assert(!svc.job_active);
        assert(!svc.finish_cleanup_pending);
        assert(!svc.abort_cleanup_pending);
        assert(svc.job_stream.fd == -1);
        assert(svc.job_stream.path[0] == '\0');
        assert(svc.job_stream.read_len == 0);
        assert(svc.job_stream.read_pos == 0);
        assert(svc.job_stream.pending_count == 0);
        assert(svc.job_stream.pending_index == 0);
        assert(svc.job_stream.line_number == 0);
        assert(!flow_has_retained_slot_text(&svc.flow));

        deneb_print_service_refresh_diagnostics(&svc);
        assert(svc.status.job_queue_depth == 0);
        assert(svc.status.job_line_number == 0);
        assert(strcmp(svc.status.file, DENEB_PRINT_NONE_VALUE) == 0);
        assert(svc.status.source[0] == '\0');
        assert(svc.status.uuid[0] == '\0');
    }

    svc.serial_ready = 0;
    svc.serial.fd = -1;
    close(null_fd);
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
    drive_service_job_prepare(&svc);
    assert(deneb_print_service_poll_job(&svc) == 1);
    assert(deneb_flow_inflight(&svc.flow) == 1);

    assert(deneb_command_parse("PAUSE<{}", &cmd) == 0);
    svc.status.position_report_count = 1;
    svc.status.x = 120.0f;
    svc.status.y = 80.0f;
    svc.status.z = 12.0f;
    svc.status.e = 4.2f;
    svc.status.r0 = -3.0f;
    svc.status.head_t_set = 210.0f;
    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(svc.status.state == DENEB_PRINT_STATE_PAUSED);
    assert(svc.pause_position_probe_pending);
    assert(!svc.pause_policy_pending);
    assert(deneb_print_service_poll_job(&svc) == 0);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    assert(svc.job_stream.line_number == 1);
    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_pause_resume_control_poll(&svc) == 0);
    assert(svc.pause_position_probe_sent);
    assert(svc.pause_position_probe_pending);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_pause_resume_control_poll(&svc) == 0);
    assert(svc.pause_position_probe_pending);
    assert(svc.pause_position_probe_sent);
    assert(!svc.pause_policy_pending);
    svc.status.head_t_set = 0.0f;
    svc.status.position_report_count = 2;
    svc.status.x = 121.0f;
    assert(deneb_pause_resume_control_poll(&svc) == 0);
    assert(!svc.pause_position_probe_pending);
    assert(svc.pause_policy_pending);
    assert(svc.paused_x == 121.0f);
    assert(svc.paused_nozzle_setpoint == 210.0f);
    assert(deneb_print_service_poll_job(&svc) == 0);
    assert(svc.job_stream.line_number == 1);
    for (int i = 0; i < 40 && deneb_pause_resume_control_busy(&svc); i++) {
        deneb_flow_clear_inflight(&svc.flow);
        assert(deneb_pause_resume_control_poll(&svc) >= 0);
    }
    assert(!deneb_pause_resume_control_busy(&svc));

    assert(deneb_command_parse("RESUME<{}", &cmd) == 0);
    assert(deneb_print_service_handle_command(&svc, &cmd, reply, sizeof(reply)) == 0);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);
    assert(svc.resume_policy_pending);
    assert(strcmp(svc.resume_policy.commands[0], "M104 S210") == 0);
    assert(strcmp(svc.resume_policy.commands[1], "M109 S210") == 0);
    assert(svc.status.head_t_set == 210.0f);
    assert(deneb_print_service_poll_job(&svc) == 0);
    assert(svc.job_stream.line_number == 1);
    assert(deneb_pause_resume_control_poll(&svc) >= 0);
    assert(svc.resume_policy_index == 1);
    assert(svc.heater_wait.active);
    assert(svc.status.state == DENEB_PRINT_STATE_PREPARING);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_pause_resume_control_poll(&svc) == 0);
    assert(svc.resume_policy_index == 1);
    svc.status.head_t_cur = 210.0f;
    for (int i = 0; i < 40 && deneb_pause_resume_control_busy(&svc); i++) {
        deneb_flow_clear_inflight(&svc.flow);
        assert(deneb_pause_resume_control_poll(&svc) >= 0);
    }
    assert(!deneb_pause_resume_control_busy(&svc));
    deneb_flow_clear_inflight(&svc.flow);
    assert(deneb_print_service_poll_job(&svc) == 1);
    assert(deneb_flow_inflight(&svc.flow) == 1);
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

    deneb_status_init(&status);
    assert(deneb_status_parse_marlin_line(&status, "ok T0:23.4 /0.0 B:24.1 /0.0") == DENEB_PARSE_TEMPERATURE);
    assert(status.head_t_cur > 23.0f && status.head_t_cur < 24.0f);
    assert(status.head_t_set == 0.0f);
    assert(status.bed_t_cur > 24.0f && status.bed_t_cur < 25.0f);
    assert(status.bed_t_set == 0.0f);

    deneb_status_init(&status);
    assert(deneb_status_parse_marlin_line(&status, "T0:26.8/0.0@0f65535/0 B25.1/0.0@0 t1/33.2") == DENEB_PARSE_TEMPERATURE);
    assert(status.head_t_cur > 26.0f && status.head_t_cur < 27.0f);
    assert(status.head_t_set == 0.0f);
    assert(status.bed_t_cur > 25.0f && status.bed_t_cur < 26.0f);
    assert(status.bed_t_set == 0.0f);
    assert(status.topcap_present);
    assert(status.topcap_t_cur > 33.0f && status.topcap_t_cur < 34.0f);

    assert(deneb_status_parse_marlin_line(&status, "ok B0") == DENEB_PARSE_NO_MATCH);
    assert(status.bed_t_cur > 25.0f && status.bed_t_cur < 26.0f);

    assert(deneb_status_parse_marlin_line(&status, "X:1.00 Y:2.00 Z:3.00 E:4.00 R0:-3.50 Count X:80 Y:160 Z:1200") == DENEB_PARSE_POSITION);
    assert(status.x == 1.0f && status.y == 2.0f && status.z == 3.0f && status.e == 4.0f);
    assert(status.r0 == -3.5f);
    assert(status.position_report_count == 1);

    assert(deneb_status_parse_marlin_line(&status, "FIRMWARE_NAME:Marlin 1.1.0 SOURCE_CODE_URL:https://example.invalid") == DENEB_PARSE_VERSION);
    assert(strcmp(status.firmware, "Marlin 1.1.0") == 0);

    assert(deneb_status_parse_marlin_line(&status, "MACHINE_TYPE:E2 PCB_ID:4 BUILD:\"Apr 30 2020 12:57:04\"") == DENEB_PARSE_VERSION);
    assert(strcmp(status.machine_type, "E2") == 0);
    assert(status.pcb_id == 4);
    assert(status.pcb_id_valid);
    assert(strcmp(status.firmware, "Apr 30 2020 12:57:04") == 0);
    assert(deneb_status_parse_marlin_line(&status, "Error:Line Number is not Last Line Number+1") == DENEB_PARSE_NO_MATCH);
    assert(status.state != DENEB_PRINT_STATE_ERROR);
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
    char sync[8];
    uint8_t seq = 0;
    uint8_t resend = 0;
    size_t written = 0;

    deneb_flow_init(&flow);
    assert(deneb_flow_prepare_packet(&flow, "M105", packet, sizeof(packet), &written, &seq) == 0);
    assert(seq == DENEB_FLOW_INITIAL_SEQUENCE);
    assert(deneb_flow_inflight(&flow) == 1);
    assert(!deneb_flow_has_pending_barrier(&flow));
    format_sync_packet('o', DENEB_FLOW_INITIAL_SEQUENCE, 5, sync);
    assert(deneb_flow_handle_response(&flow, sync, &resend) == 1);
    assert(deneb_flow_inflight(&flow) == 0);

    assert(deneb_flow_prepare_packet(&flow, "G28 X Y", packet, sizeof(packet), &written, &seq) == 0);
    assert(seq == DENEB_FLOW_INITIAL_SEQUENCE + 1);
    assert(deneb_flow_has_pending_barrier(&flow));
    format_sync_packet('r', DENEB_FLOW_INITIAL_SEQUENCE + 1, 5, sync);
    assert(deneb_flow_handle_response(&flow, sync, &resend) == 2);
    assert(resend == DENEB_FLOW_INITIAL_SEQUENCE + 1);
    assert(flow.handling_resend);
    seq = resend;
    assert(deneb_flow_handle_response(&flow, sync, &resend) == 0);
    assert(deneb_flow_get_resend_packet(&flow, seq, packet, sizeof(packet), &written) == 0);
    assert(written == 13);
    assert(packet[0] == 0xff);
    assert(packet[1] == 0xff);
    assert(packet[2] == DENEB_FLOW_INITIAL_SEQUENCE + 1);
    assert(packet[3] == 7);
    assert(memcmp(packet + 4, "G28 X Y", 7) == 0);
    format_sync_packet('o', DENEB_FLOW_INITIAL_SEQUENCE + 1, 5, sync);
    assert(deneb_flow_handle_response(&flow, sync, &resend) == 1);
    assert(!flow.handling_resend);
    assert(!deneb_flow_has_pending_barrier(&flow));

    deneb_flow_init(&flow);
    assert(deneb_flow_prepare_packet(&flow, "M105", packet, sizeof(packet),
                                     &written, &seq) == 0);
    assert(deneb_flow_prepare_packet(&flow, "M105", packet, sizeof(packet),
                                     &written, &seq) == 0);
    assert(deneb_flow_handle_response(
               &flow,
               "Error:ProtoError:Sequence number is unexpected (received, expected): 0,1",
               &resend) == 3);
    assert(resend == 0);
    assert(flow.next_sequence == 2);
    assert(flow.last_proto_expected_sequence == 1);
    assert(deneb_flow_handle_response(
               &flow,
               "Error:ProtoError:Sequence number is unexpected (received, expected): 0,1",
               &resend) == 3);
    assert(deneb_flow_inflight(&flow) == 2);
    assert(flow.reject_count == 2);

    deneb_flow_init(&flow);
    assert(deneb_flow_prepare_packet(&flow, "M105", packet, sizeof(packet),
                                     &written, &seq) == 0);
    assert(deneb_flow_handle_response(
               &flow,
               "Error:ProtoError:Sequence number is unexpected (received, expected): 0,7",
               &resend) == 3);
    assert(flow.next_sequence == 1);
    assert(flow.last_proto_expected_sequence == 7);
    deneb_flow_resync_to_expected(&flow);
    assert(flow.next_sequence == 7);
    assert(deneb_flow_inflight(&flow) == 0);

    deneb_flow_init(&flow);
    for (int i = 0; i < 10; i++) {
        assert(deneb_flow_prepare_packet(&flow, "M105", packet,
                                         sizeof(packet), &written, &seq) == 0);
        format_sync_packet('o', seq, 5, sync);
        assert(deneb_flow_handle_response(&flow, sync, &resend) == 1);
    }
    assert(deneb_flow_inflight(&flow) == 0);
    assert(deneb_flow_handle_response(&flow, "Resend: 4", &resend) == 0);
    assert(resend == 0);

    assert(deneb_flow_handle_response(&flow, "Resend: 220", &resend) == 0);
    assert(flow.next_sequence == 10);
    assert(deneb_flow_inflight(&flow) == 0);

    deneb_flow_init(&flow);
    for (int i = 0; i < 10; i++) {
        assert(deneb_flow_prepare_packet(&flow, "M105", packet,
                                         sizeof(packet), &written, &seq) == 0);
        format_sync_packet('o', seq, 5, sync);
        assert(deneb_flow_handle_response(&flow, sync, &resend) == 1);
    }
    format_sync_packet('r', 4, 5, sync);
    assert(deneb_flow_handle_response(&flow, sync, &resend) == 0);
    assert(resend == 0);

    format_sync_packet('r', 220, 5, sync);
    assert(deneb_flow_handle_response(&flow, sync, &resend) == 0);
    assert(flow.next_sequence == 10);
    assert(deneb_flow_inflight(&flow) == 0);

    deneb_flow_init(&flow);
    for (int i = 0; i < 4; i++) {
        assert(deneb_flow_prepare_packet(&flow, "G1 Z0.20 F60", packet,
                                         sizeof(packet), &written, &seq) == 0);
    }
    format_sync_packet('o', 200, 0, sync);
    assert(deneb_flow_handle_response(&flow, sync, &resend) == 0);
    assert(deneb_flow_inflight(&flow) == 4);
    assert(flow.ack_count == 0);
    format_sync_packet('o', 1, 0, sync);
    assert(deneb_flow_handle_response(&flow, sync, &resend) == 1);
    assert(deneb_flow_inflight(&flow) == 2);
    assert(deneb_flow_prepare_packet(&flow, "G1 Z0.20 F60", packet,
                                     sizeof(packet), &written, &seq) == -2);
    format_sync_packet('q', 1, 2, sync);
    assert(deneb_flow_handle_response(&flow, sync, &resend) == 0);
    assert(deneb_flow_prepare_packet(&flow, "G1 Z0.20 F60", packet,
                                     sizeof(packet), &written, &seq) == 0);
    assert(deneb_flow_prepare_packet(&flow, "G1 Z-0.20 F60", packet,
                                     sizeof(packet), &written, &seq) == 0);
    assert(deneb_flow_prepare_packet(&flow, "G1 Z0.20 F60", packet,
                                     sizeof(packet), &written, &seq) == -2);

    deneb_flow_clear_inflight(&flow);
    assert(deneb_flow_prepare_packet(&flow, "G1 Z0.20 F60", packet,
                                     sizeof(packet), &written, &seq) == 0);

    deneb_flow_init(&flow);
    for (int i = 0; i < 260; i++) {
        assert(deneb_flow_prepare_packet(&flow, "M105", packet,
                                         sizeof(packet), &written, &seq) == 0);
        assert(seq != 255);
        format_sync_packet('o', seq, 5, sync);
        assert(deneb_flow_handle_response(&flow, sync, &resend) == 1);
    }
    assert(flow.next_sequence == 5);
    assert(deneb_flow_inflight(&flow) == 0);

    deneb_flow_init(&flow);
    flow.next_sequence = 253;
    assert(deneb_flow_prepare_packet(&flow, "G1 Z-0.20 F60", packet,
                                     sizeof(packet), &written, &seq) == 0);
    assert(seq == 253);
    assert(deneb_flow_prepare_packet(&flow, "G1 Z-0.20 F60", packet,
                                     sizeof(packet), &written, &seq) == 0);
    assert(seq == 254);
    assert(deneb_flow_prepare_packet(&flow, "G1 Z-0.20 F60", packet,
                                     sizeof(packet), &written, &seq) == 0);
    assert(seq == 0);
    assert(deneb_flow_prepare_packet(&flow, "G1 Z-0.20 F60", packet,
                                     sizeof(packet), &written, &seq) == 0);
    assert(seq == 1);
    assert(deneb_flow_inflight(&flow) == 4);
    format_sync_packet('o', 0, 5, sync);
    assert(deneb_flow_handle_response(&flow, sync, &resend) == 1);
    assert(deneb_flow_inflight(&flow) == 1);
    assert(flow.ack_count == 3);
}

static void test_service_poll_motion_stub(void)
{
    deneb_print_service_t svc;

    deneb_print_service_init(&svc);
    assert(deneb_print_service_poll_motion(&svc) == 0);
}

static void test_service_startup_status_probe(void)
{
    deneb_print_service_t svc;
    FILE *serial_file;
    int serial_fd;
    uint8_t buf[512];
    ssize_t n;

    deneb_print_service_init(&svc);
    serial_file = tmpfile();
    assert(serial_file != NULL);
    serial_fd = dup(fileno(serial_file));
    assert(serial_fd >= 0);
    svc.serial.fd = serial_fd;
    svc.serial_ready = 1;
    svc.startup_status_probe_pending = 1;
    svc.firmware_probe_pending = 1;

    assert(deneb_print_service_poll_motion(&svc) == 0);
    assert(!svc.startup_status_probe_pending);
    assert(svc.firmware_probe_pending);
    assert(svc.firmware_probe_attempts == 1);
    assert(svc.temperature_poll_ticks == 0);
    assert(deneb_flow_inflight(&svc.flow) == 3);
    assert(lseek(serial_fd, 0, SEEK_SET) == 0);
    n = read(serial_fd, buf, sizeof(buf));
    assert(n > 0);
    assert(buffer_contains_bytes(buf, (size_t)n, "M115"));
    assert(buffer_contains_bytes(buf, (size_t)n, "M105"));
    assert(buffer_contains_bytes(buf, (size_t)n, "M114"));

    deneb_print_service_close(&svc);
    fclose(serial_file);
}

static void test_service_firmware_probe_retries_until_bounded(void)
{
    deneb_print_service_t svc;
    FILE *serial_file;
    int serial_fd;
    uint8_t buf[512];
    ssize_t n;

    deneb_print_service_init(&svc);
    serial_file = tmpfile();
    assert(serial_file != NULL);
    serial_fd = dup(fileno(serial_file));
    assert(serial_fd >= 0);
    svc.serial.fd = serial_fd;
    svc.serial_ready = 1;
    svc.firmware_probe_pending = 1;
    svc.firmware_probe_attempts = 1;
    svc.firmware_probe_ticks = 19;

    assert(deneb_print_service_poll_motion(&svc) == 0);
    assert(svc.firmware_probe_pending);
    assert(svc.firmware_probe_attempts == 2);
    assert(svc.firmware_probe_ticks == 0);
    assert(deneb_flow_inflight(&svc.flow) == 1);
    assert(lseek(serial_fd, 0, SEEK_SET) == 0);
    n = read(serial_fd, buf, sizeof(buf));
    assert(n > 0);
    assert(buffer_contains_bytes(buf, (size_t)n, "M115"));
    assert(!buffer_contains_bytes(buf, (size_t)n, "M105"));
    assert(!buffer_contains_bytes(buf, (size_t)n, "M114"));

    deneb_print_service_close(&svc);
    fclose(serial_file);

    deneb_print_service_init(&svc);
    serial_file = tmpfile();
    assert(serial_file != NULL);
    serial_fd = dup(fileno(serial_file));
    assert(serial_fd >= 0);
    svc.serial.fd = serial_fd;
    svc.serial_ready = 1;
    svc.firmware_probe_pending = 1;
    snprintf(svc.status.firmware, sizeof(svc.status.firmware), "Apr 30 2020");

    assert(deneb_print_service_poll_motion(&svc) == 0);
    assert(!svc.firmware_probe_pending);
    assert(deneb_flow_inflight(&svc.flow) == 0);

    deneb_print_service_close(&svc);
    fclose(serial_file);

    deneb_print_service_init(&svc);
    serial_file = tmpfile();
    assert(serial_file != NULL);
    serial_fd = dup(fileno(serial_file));
    assert(serial_fd >= 0);
    svc.serial.fd = serial_fd;
    svc.serial_ready = 1;
    svc.firmware_probe_pending = 1;
    svc.firmware_probe_attempts = 6;
    svc.firmware_probe_ticks = 19;

    assert(deneb_print_service_poll_motion(&svc) == 0);
    assert(!svc.firmware_probe_pending);
    assert(deneb_flow_inflight(&svc.flow) == 0);

    deneb_print_service_close(&svc);
    fclose(serial_file);
}

static void test_service_periodic_status_poll_omits_version_probe(void)
{
    deneb_print_service_t svc;
    FILE *serial_file;
    int serial_fd;
    uint8_t buf[512];
    ssize_t n;

    deneb_print_service_init(&svc);
    serial_file = tmpfile();
    assert(serial_file != NULL);
    serial_fd = dup(fileno(serial_file));
    assert(serial_fd >= 0);
    svc.serial.fd = serial_fd;
    svc.serial_ready = 1;
    svc.startup_status_probe_pending = 0;
    svc.temperature_poll_ticks = 3;

    assert(deneb_print_service_poll_motion(&svc) == 0);
    assert(!svc.startup_status_probe_pending);
    assert(svc.temperature_poll_ticks == 0);
    assert(deneb_flow_inflight(&svc.flow) == 2);
    assert(lseek(serial_fd, 0, SEEK_SET) == 0);
    n = read(serial_fd, buf, sizeof(buf));
    assert(n > 0);
    assert(!buffer_contains_bytes(buf, (size_t)n, "M115"));
    assert(buffer_contains_bytes(buf, (size_t)n, "M105"));
    assert(buffer_contains_bytes(buf, (size_t)n, "M114"));

    deneb_print_service_close(&svc);
    fclose(serial_file);
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
    assert(policy.count == 12);
    assert(strcmp(policy.commands[0], DENEB_GCODE_RELATIVE_MODE) == 0);
    assert(strcmp(policy.commands[1], "G1 X20 Y20 E-6.5 F9000") == 0);
    assert(strcmp(policy.commands[2], "G1 Z3") == 0);
    assert(strcmp(policy.commands[3], DENEB_GCODE_ABSOLUTE_MODE) == 0);
    assert(strcmp(policy.commands[4], "G10 S-16.5") == 0);
    assert(strcmp(policy.commands[5], "G28 X Y") == 0);
    assert(strcmp(policy.commands[6], DENEB_GCODE_HOME_Z) == 0);
    assert(strcmp(policy.commands[7], "M104 S0") == 0);
    assert(strcmp(policy.commands[8], "M140 S0") == 0);
    assert(strcmp(policy.commands[9], DENEB_GCODE_FAN_OFF) == 0);
    assert(strcmp(policy.commands[10], DENEB_GCODE_WAIT_FOR_MOVES) == 0);
    assert(strcmp(policy.commands[11], DENEB_GCODE_DISABLE_ALL_STEPPERS) == 0);
    assert(deneb_motion_policy_contains_xy_home(&policy));

    deneb_motion_policy_finish(&policy);
    assert(policy.count == 11);
    assert(strcmp(policy.commands[0], DENEB_GCODE_WAIT_FOR_MOVES) == 0);
    assert(strcmp(policy.commands[1], DENEB_GCODE_RELATIVE_MODE) == 0);
    assert(strcmp(policy.commands[2], "G1 Z3") == 0);
    assert(strcmp(policy.commands[3], DENEB_GCODE_ABSOLUTE_MODE) == 0);
    assert(strcmp(policy.commands[4], "G28 X Y") == 0);
    assert(strcmp(policy.commands[5], DENEB_GCODE_HOME_Z) == 0);
    assert(strcmp(policy.commands[6], "M104 S0") == 0);
    assert(strcmp(policy.commands[7], "M140 S0") == 0);
    assert(strcmp(policy.commands[8], DENEB_GCODE_FAN_OFF) == 0);
    assert(strcmp(policy.commands[9], DENEB_GCODE_WAIT_FOR_MOVES) == 0);
    assert(strcmp(policy.commands[10], DENEB_GCODE_DISABLE_ALL_STEPPERS) == 0);
    assert(deneb_motion_policy_contains_xy_home(&policy));

    deneb_motion_policy_pause(&policy, 120.0f, 80.0f, 12.0f);
    assert(policy.count == 10);
    assert(strcmp(policy.commands[0], "M83") == 0);
    assert(strcmp(policy.commands[2], "G1 E-6.5 X70") == 0);
    assert(strcmp(policy.commands[4], "G0 X5 Y10") == 0);
    assert(strcmp(policy.commands[7], "G0 Z205 F9000") == 0);
    assert(strcmp(policy.commands[9], "M104 S0") == 0);

    deneb_motion_policy_resume(&policy, 120.0f, 80.0f, 12.0f, 4.2f,
                               -3.0f, 210.0f);
    assert(policy.count == 12);
    assert(strcmp(policy.commands[0], "M104 S210") == 0);
    assert(strcmp(policy.commands[1], "M109 S210") == 0);
    assert(strcmp(policy.commands[5], "G0 X120 Y80") == 0);
    assert(strcmp(policy.commands[6], "G0 Z12") == 0);
    assert(strcmp(policy.commands[9], "G10 S-3") == 0);
    assert(strcmp(policy.commands[10], "G92 E4.2") == 0);
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
    test_gcode_stream_rewrite_policy();
    test_runtime_diagnostics_policy();
    test_motion_sender_policy();
    test_motion_observer_policy();
    test_serial_transport_buffers_partial_lines();
    test_serial_transport_retries_nonblocking_writes();
    test_motion_runtime_policy();
    test_macro_runner_policy();
    test_macro_control_policy();
    test_print_state_rules();
    test_print_job_summary();
    test_printer_status_response();
    test_json_field_helpers();
    test_status_payload_filename_resolution();
    test_gcode_command_helpers();
    test_frame_light_helpers();
    test_diagnostics_export_helpers();
    test_manual_motion_helpers();
    test_buildplate_level_helpers();
    test_material_workflow_helpers();
    test_json_string_helpers();
    test_status_payload_helpers();
    test_status_state_helpers();
    test_print_profile_helpers();
    test_printer_identity_helpers();
    test_print_backend_route_contract();
    test_pending_job_metadata();
    test_pending_job_metadata_write_file();
    test_pending_job_dispatch_helpers();
    test_pending_job_upload_default_check();
    test_pending_job_registration_policy();
    test_print_job_file_metadata();
    test_material_catalog_helpers();
    test_status_frame();
    test_crc_and_packet();
    test_macro_safety();
    test_system_language_helpers();
    test_diagnostics_log_side_by_side_fields();
    test_diagnostics_log_throttles_hot_path_counters();
    test_diagnostics_log_truncates_oversized_file();
    test_abort_clears_status();
    test_latched_abort_uses_service_cleanup();
    test_job_accepts_without_blocking();
    test_service_context_policy();
    test_service_command_policy();
    test_ipc_frame_policy();
    test_job_poll_streams_after_preheat();
    test_job_completion_waits_for_finish_cleanup();
    test_repeated_jobs_release_terminal_state();
    test_pause_gates_active_job_streaming();
    test_pause_during_preheat_resumes_to_preparing();
    test_status_parser();
    test_home_distance_parser();
    test_flow_control();
    test_service_poll_motion_stub();
    test_service_startup_status_probe();
    test_service_firmware_probe_retries_until_bounded();
    test_service_periodic_status_poll_omits_version_probe();
    test_service_proto_desync_resyncs_active_job();
    test_heater_wait();
    test_sha256();
    test_motion_policy();
    test_motion_firmware_cache();
    test_pending_job_file_contract();
    test_print_history_array_reader();
    puts("deneb-printsvc tests passed");
    return 0;
}
