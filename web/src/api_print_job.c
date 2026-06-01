/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Print job endpoint implementations.
 */

#include "api_print_job.h"
#include "backend_zmq.h"
#include "json_writer.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static int is_printing(const printer_state_t *s)
{
    return s->is_printing || s->is_paused;
}

static const char *get_job_state(const printer_state_t *s)
{
    if (s->has_error) return "error";
    if (s->is_paused) return "paused";
    if (s->is_printing) return "printing";
    return "none";
}

void api_print_job_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();

    if (!is_printing(s)) {
        resp->status_code = 404;
        api_http_set_body_str(resp, "{\"message\":\"Not found\"}");
        return;
    }

    char buf[512];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "name", s->filename);
    json_str(&w, "uuid", s->uuid);
    json_str(&w, "source", s->source);
    json_str(&w, "state", get_job_state(s));
    json_float(&w, "progress", s->progress / 100.0f);
    json_int(&w, "time_elapsed", s->time_total > 0 ? s->time_total - s->time_left : 0);
    json_int(&w, "time_total", s->time_total);
    json_str(&w, "datetime_started", "");
    json_str(&w, "datetime_finished", "");
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_print_job_state_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    if (!is_printing(s)) {
        resp->status_code = 404;
        api_http_set_body_str(resp, "{\"message\":\"Not found\"}");
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "\"%s\"", get_job_state(s));
    api_http_set_body_str(resp, buf);
}

void api_print_job_progress_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", s->progress / 100.0f);
    api_http_set_body_str(resp, buf);
}

void api_print_job_time_elapsed_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    int elapsed = s->time_total > 0 ? s->time_total - s->time_left : 0;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", elapsed);
    api_http_set_body_str(resp, buf);
}

void api_print_job_time_total_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", s->time_total);
    api_http_set_body_str(resp, buf);
}

void api_print_job_name_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[196];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_bare_str(&w, s->filename);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_print_job_uuid_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[96];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_bare_str(&w, s->uuid);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_print_job_source_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    const printer_state_t *s = backend_zmq_get_state();
    char buf[64];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_bare_str(&w, s->source);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

void api_print_job_datetime_started_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "\"\"");
}

void api_print_job_datetime_finished_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    api_http_set_body_str(resp, "\"\"");
}

/* ========== M7 Write Endpoints ========== */

void api_print_job_state_put(const http_request_t *req, http_response_t *resp)
{
    /* Body is a plain string: "pause", "resume", or "abort" */
    /* Body is a plain string: "pause", "print", or "abort" */
    /* Trim whitespace and quotes for robust matching */
    const char *body = req->body;
    while (*body == ' ' || *body == '"' || *body == '\'') body++;
    char cmd[16] = "";
    size_t i = 0;
    while (body[i] && body[i] != '"' && body[i] != '\'' && body[i] != ' ' && i < sizeof(cmd) - 1) {
        cmd[i] = body[i]; i++;
    }
    cmd[i] = '\0';

    if (strcmp(cmd, "pause") == 0) {
        if (backend_zmq_pause() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to pause print\"}");
            return;
        }
        api_http_set_body_str(resp, "{\"message\":\"OK\"}");
    } else if (strcmp(cmd, "resume") == 0 || strcmp(cmd, "print") == 0) {
        /* API spec uses "print" to resume; "resume" is an alias */
        if (backend_zmq_resume() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to resume print\"}");
            return;
        }
        api_http_set_body_str(resp, "{\"message\":\"OK\"}");
    } else if (strcmp(cmd, "abort") == 0) {
        if (backend_zmq_abort() < 0) {
            resp->status_code = 503;
            api_http_set_body_str(resp, "{\"message\":\"Failed to abort print\"}");
            return;
        }
        api_http_set_body_str(resp, "{\"message\":\"OK\"}");
    } else {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Unknown state\"}");
    }
}

void api_print_job_post(const http_request_t *req, http_response_t *resp)
{
    /*
     * POST /api/v1/print_job
     * Cura uploads a gcode file via multipart/form-data with fields:
     *   - file: the gcode/ufp file
     *   - jobname: display name (optional)
     *   - owner: who started the print (optional)
     *
     * The main.c stream_upload() has already saved the full multipart body
     * to req->upload_path. We need to extract the file content, move it
     * to the USB mount, and send a ZMQ JOB command.
     */
    if (req->upload_path[0] == '\0') {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"No file uploaded\"}");
        return;
    }

    /* Extract file from multipart body */
    char gcode_path[256] = "";
    char filename[128] = "upload.gcode";

    if (req->multipart_boundary[0] &&
        extract_multipart_file(req->multipart_boundary, req->upload_path,
                               gcode_path, sizeof(gcode_path),
                               filename, sizeof(filename)) < 0) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Failed to extract file from upload\"}");
        return;
    }

    if (gcode_path[0] == '\0') {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"No file field in upload\"}");
        return;
    }

    /* Sanitize filename: strip path components to prevent traversal */
    char *slash = strrchr(filename, '/');
    if (!slash) slash = strrchr(filename, '\\');
    if (slash) {
        /* Move past the last path separator */
        char safe[128];
        snprintf(safe, sizeof(safe), "%s", slash + 1);
        snprintf(filename, sizeof(filename), "%s", safe);
    }
    /* Reject empty or dot-only filenames */
    if (filename[0] == '\0' || strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
        snprintf(filename, 128, "upload.gcode");
    }

    /* Move file to the USB mount where the print service can find it */
    char dest_path[256];
    snprintf(dest_path, sizeof(dest_path), "/mnt/sda1/%s", filename);

    if (rename(gcode_path, dest_path) < 0) {
        /* If rename fails (cross-device), copy instead */
        int src_fd = open(gcode_path, O_RDONLY);
        int dst_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (src_fd < 0 || dst_fd < 0) {
            if (src_fd >= 0) close(src_fd);
            if (dst_fd >= 0) close(dst_fd);
            unlink(gcode_path);
            resp->status_code = 500;
            api_http_set_body_str(resp, "{\"message\":\"Failed to save file\"}");
            return;
        }
        char buf[65536];
        ssize_t nr;
        int copy_ok = 1;
        while ((nr = read(src_fd, buf, sizeof(buf))) > 0) {
            if (write(dst_fd, buf, (size_t)nr) != nr) {
                copy_ok = 0;
                break;
            }
        }
        close(src_fd);
        close(dst_fd);
        if (!copy_ok) {
            unlink(dest_path);
            unlink(gcode_path);
            resp->status_code = 500;
            api_http_set_body_str(resp, "{\"message\":\"Failed to save file\"}");
            return;
        }
        unlink(gcode_path);
    }

    fprintf(stderr, "deneb-api: print file saved to %s\n", dest_path);

    /* Send JOB command to coordinator (escape path for JSON) */
    char cmd_args[512];
    json_writer_t cmd_w;
    json_init(&cmd_w, cmd_args, sizeof(cmd_args));
    json_obj_open(&cmd_w);
    json_str(&cmd_w, "file", dest_path);
    json_str(&cmd_w, "source", "Web");
    json_str(&cmd_w, "uuid", "0");
    json_obj_close(&cmd_w);
    json_len(&cmd_w);
    if (backend_zmq_send_command("JOB", cmd_args) < 0) {
        resp->status_code = 503;
        api_http_set_body_str(resp, "{\"message\":\"Failed to start print\"}");
        return;
    }

    /* Return print job info */
    char buf[512];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "name", filename);
    json_str(&w, "uuid", "0");
    json_str(&w, "source", "Web");
    json_str(&w, "state", "printing");
    json_float(&w, "progress", 0.0);
    json_int(&w, "time_elapsed", 0);
    json_int(&w, "time_total", 0);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
    resp->status_code = 201;
}
