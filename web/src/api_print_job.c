/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Print job endpoint implementations.
 */

#define _GNU_SOURCE
#include "api_print_job.h"
#include "api_multipart.h"
#include "backend_zmq.h"
#include "pending_job_file.h"
#include "pending_job_registration.h"
#include "print_action_dispatch.h"
#include "print_job_file.h"
#include "print_job_summary.h"
#include "print_state_rules.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void log_print_job_state_cmd(const char *cmd, const char *body)
{
    fprintf(stderr, "deneb-api: print_job/state command=%s body=%s\n",
            cmd ? cmd : "(none)", body ? body : "{}");
}

static void set_message_response(http_response_t *resp, int status_code,
                                 const char *message)
{
    char buf[192];

    resp->status_code = status_code;
    snprintf(buf, sizeof(buf), "{\"message\":\"%s\"}",
             message ? message : "");
    api_http_set_body_str(resp, buf);
}

static void write_pending_job_response(http_response_t *resp, const char *job_name, int status_code)
{
    char buf[512];

    deneb_print_job_summary_format_queued_response(
        "Print job already queued", job_name, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
    resp->status_code = status_code;
}

static int dispatch_pause(void *ctx)
{
    (void)ctx;
    return backend_zmq_pause();
}

static int dispatch_resume(void *ctx)
{
    (void)ctx;
    return backend_zmq_resume();
}

static int dispatch_abort(void *ctx)
{
    (void)ctx;
    return backend_zmq_abort();
}

static int dispatch_stop(void *ctx)
{
    (void)ctx;
    return backend_zmq_stop_print();
}

static void dispatch_clear_pending(void *ctx)
{
    (void)ctx;
    deneb_pending_job_file_clear_default();
}

static int registration_start_allowed(void *ctx)
{
    (void)ctx;
    return backend_zmq_print_start_allowed();
}

static int registration_send_job(void *ctx,
                                 const deneb_print_job_start_plan_t *plan)
{
    (void)ctx;
    if (!plan)
        return -1;
    return backend_zmq_send_job(plan->path, plan->source, plan->uuid,
                                plan->cloud_job_id, plan->bed_target,
                                plan->nozzle_target);
}

static int register_native_print(const char *path,
                                 const char *source,
                                 const char *uuid,
                                 const char *cloud_job_id,
                                 char *failure_message,
                                 size_t failure_message_sz,
                                 int *failure_status_code)
{
    deneb_pending_job_registration_t registration;
    deneb_pending_job_registration_dispatch_ops_t ops = {
        NULL,
        registration_start_allowed,
        registration_send_job
    };

    fprintf(stderr, "deneb-api: registering print path natively: %s\n", path);

    if (deneb_pending_job_registration_prepare(
            path, source, uuid, cloud_job_id, (long long)time(NULL),
            &registration) < 0) {
        if (failure_message && failure_message_sz > 0) {
            snprintf(failure_message, failure_message_sz, "%s",
                     registration.failure_message[0] ?
                     registration.failure_message :
                     "Failed to validate print job");
        }
        if (failure_status_code) {
            *failure_status_code = registration.failure_status_code > 0 ?
                registration.failure_status_code : 400;
        }
        fprintf(stderr, "deneb-api: failed to validate print metadata for %s: %s\n",
                path, registration.failure_message[0] ?
                registration.failure_message : "unknown validation error");
        return -1;
    }

    if (deneb_pending_job_registration_write_default(&registration) < 0) {
        fprintf(stderr, "deneb-api: failed to write pending print metadata for %s\n", path);
        return -1;
    }

    if (!registration.should_start_immediately) {
        fprintf(stderr, "deneb-api: pending print waits for user action changes=%d\n",
                registration.change_count);
        return 0;
    }

    if (deneb_pending_job_registration_dispatch_start(
            &registration, &ops) < 0) {
        fprintf(stderr, "deneb-api: failed to send native JOB for %s\n", path);
        return -1;
    }

    fprintf(stderr, "deneb-api: native print registration accepted for %s\n", path);
    return 0;
}

void api_print_job_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;

    backend_zmq_get_job_summary(&summary);
    if (!summary.active) {
        resp->status_code = 404;
        api_http_set_body_str(resp, "{\"message\":\"Not found\"}");
        return;
    }

    char buf[512];
    if (deneb_print_job_summary_format_um_response(&summary, buf,
                                                   sizeof(buf)) < 0) {
        resp->status_code = 500;
        api_http_set_body_str(resp,
                              "{\"message\":\"Print job response too large\"}");
        return;
    }
    api_http_set_body_str(resp, buf);
}

void api_print_job_state_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    backend_zmq_get_job_summary(&summary);
    if (!summary.active) {
        resp->status_code = 404;
        api_http_set_body_str(resp, "{\"message\":\"Not found\"}");
        return;
    }
    char buf[32];
    deneb_print_job_summary_format_string_field(
        &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_STATE, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_progress_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[16];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_progress_fraction(&summary, buf,
                                                     sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_time_elapsed_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[16];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_time_elapsed(&summary, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_time_total_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[16];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_time_total(&summary, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_name_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[196];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_string_field(
        &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_NAME, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_uuid_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[96];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_string_field(
        &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_UUID, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_source_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[64];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_string_field(
        &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_SOURCE, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_datetime_started_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[4];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_string_field(
        &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_DATETIME_STARTED, buf,
        sizeof(buf));
    api_http_set_body_str(resp, buf);
}

void api_print_job_datetime_finished_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    deneb_print_job_summary_t summary;
    char buf[4];
    backend_zmq_get_job_summary(&summary);
    deneb_print_job_summary_format_string_field(
        &summary, DENEB_PRINT_JOB_SUMMARY_FIELD_DATETIME_FINISHED, buf,
        sizeof(buf));
    api_http_set_body_str(resp, buf);
}

/* ========== M7 Write Endpoints ========== */

int api_print_job_dispatch_plan(const deneb_print_action_plan_t *plan,
                                http_response_t *resp)
{
    deneb_print_action_dispatch_ops_t ops = {
        NULL,
        dispatch_pause,
        dispatch_resume,
        dispatch_abort,
        dispatch_stop,
        dispatch_clear_pending
    };

    if (!plan) {
        resp->status_code = 400;
        api_http_set_body_str(resp, deneb_print_action_unknown_response());
        return -1;
    }

    if (deneb_print_action_dispatch(plan, &ops) < 0) {
        set_message_response(resp, 503, plan->failure_message);
        return -1;
    }

    api_http_set_body_str(resp, "{\"message\":\"OK\"}");
    return 0;
}

int api_print_job_dispatch_action(const char *action, http_response_t *resp,
                                  const char *unknown_message)
{
    deneb_print_action_plan_t plan;

    if (deneb_print_action_plan(action, &plan) != 0) {
        resp->status_code = 400;
        api_http_set_body_str(resp, unknown_message ?
                              unknown_message :
                              deneb_print_action_unknown_response());
        return -1;
    }

    return api_print_job_dispatch_plan(&plan, resp);
}

void api_print_job_state_put(const http_request_t *req, http_response_t *resp)
{
    /* Body is a plain string: "pause", "print", or "abort" */
    char cmd[16];
    const char *action =
        deneb_print_action_parse(req->body, cmd, sizeof(cmd)) == 0 ? cmd : NULL;
    log_print_job_state_cmd(action, req->body);

    api_print_job_dispatch_action(action, resp,
                                  deneb_print_state_unknown_response());
}

void api_print_job_post(const http_request_t *req, http_response_t *resp)
{
    if (req->upload_path[0] == '\0') {
        fprintf(stderr, "deneb-api: print upload rejected: no upload_path\n");
        set_message_response(resp, 400, "No file uploaded");
        return;
    }
    if (!req->multipart_boundary[0]) {
        set_message_response(resp, 400, "No file field in upload");
        return;
    }

    /* The router has already applied the route's authorization policy. */
    resp->prepare_print_upload = 1;
}

static int write_upload_range(int input, const multipart_range_t *file,
                              const char *path, const atomic_bool *cancelled)
{
    int output = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (output < 0)
        return -1;
    int rc = copy_multipart_range(input, output, file, cancelled);
    if (close(output) < 0)
        rc = -1;
    return rc;
}

static void prepare_upload_thumbnail(const char *archive, api_print_upload_t *upload)
{
    char path[] = DENEB_UPLOAD_TEMP_DIR "/deneb-ufp-thumb-upload-XXXXXX";
    int fd = mkostemp(path, O_CLOEXEC);
    if (fd < 0)
        return;
    close(fd);
    if (deneb_print_job_file_extract_ufp_thumbnail(archive, path) == 0)
        snprintf(upload->staged_thumbnail, sizeof(upload->staged_thumbnail), "%s", path);
    else
        unlink(path);
}

int api_print_job_prepare_upload(const char *boundary, const char *upload_path,
                                 api_print_upload_t *upload,
                                 http_response_t *resp,
                                 const atomic_bool *cancelled)
{
    char filename[128];
    char extracted_name[sizeof(filename)];
    char validation_message[256] = "";
    multipart_range_t file;
    multipart_field_t fields[] = {
        { .name = "job_instance_uuid", .value = upload->job_instance_uuid,
          .capacity = sizeof(upload->job_instance_uuid) },
        { .name = "cloud_job_id", .value = upload->cloud_job_id,
          .capacity = sizeof(upload->cloud_job_id) },
        { .name = "owner", .value = upload->owner,
          .capacity = sizeof(upload->owner) },
    };
    int input = open(upload_path, O_RDWR | O_CLOEXEC);
    int result = -1;
    if (input < 0 || parse_multipart_upload(input, boundary, &file,
            filename, sizeof(filename), fields, sizeof(fields) / sizeof(fields[0]),
            cancelled) < 0) {
        fprintf(stderr, "deneb-api: print upload rejected: failed to extract multipart file\n");
        set_message_response(resp, 400, "Failed to extract file from upload");
        goto done;
    }

    bool ufp = deneb_print_job_file_has_extension(filename, ".ufp");
    const char *job_name = filename;
    if (ufp) {
        if (deneb_print_job_file_replace_extension(filename, ".gcode", extracted_name,
                                                    sizeof(extracted_name)) < 0) {
            set_message_response(resp, 400, "Failed to extract UFP model.gcode");
            goto done;
        }
        job_name = extracted_name;
    }
    if (atomic_load(cancelled))
        goto done;

    if (deneb_print_job_file_upload_storage_plan(job_name, &upload->storage) < 0) {
        fprintf(stderr, "deneb-api: failed to prepare print spool path for %s: %s\n",
                job_name, strerror(errno));
        set_message_response(resp, 500, "Failed to prepare print storage");
        goto done;
    }

    /* The private directory keeps publication atomic and bulk scratch off /tmp. */
    char dir[] = DENEB_PRINT_JOB_SPOOL_DIR "/.upload-XXXXXX";
    if ((mkdir(DENEB_PRINT_JOB_SPOOL_DIR, 0755) < 0 && errno != EEXIST) || !mkdtemp(dir)) {
        fprintf(stderr, "deneb-api: failed to stage print file for %s: %s\n",
                upload->storage.dest_path, strerror(errno));
        set_message_response(resp, 500, "Failed to save file");
        goto done;
    }
    snprintf(upload->staged_directory, sizeof(upload->staged_directory), "%s", dir);
    snprintf(upload->staged_gcode, sizeof(upload->staged_gcode), "%s/job", dir);

    if (ufp) {
        /* The worker owns these bytes until join. Move the archive toward
         * offset zero using forward positioned reads before each write. */
        if (lseek(input, 0, SEEK_SET) < 0 ||
            copy_multipart_range(input, input, &file, cancelled) < 0 ||
            ftruncate(input, file.length) < 0) {
            set_message_response(resp, 500, "Failed to save file");
            goto done;
        }
        int extracted = deneb_print_job_file_extract_ufp_model_gcode(
            upload_path, upload->staged_gcode, cancelled);
        if (extracted == 0 && !atomic_load(cancelled))
            prepare_upload_thumbnail(upload_path, upload);
        if (extracted != 0) {
            if (!atomic_load(cancelled))
                fprintf(stderr, "deneb-api: print upload rejected: failed to extract UFP model.gcode from %s\n",
                        filename);
            set_message_response(resp, 400, "Failed to extract UFP model.gcode");
            goto done;
        }
        upload->ufp_normalized = 1;
        fprintf(stderr, "deneb-api: normalized UFP upload to extracted G-code %s\n", job_name);
    } else if (write_upload_range(input, &file, upload->staged_gcode, cancelled) < 0) {
        fprintf(stderr, "deneb-api: failed to stage print file for %s: %s\n",
                upload->storage.dest_path, strerror(errno));
        set_message_response(resp, 500, "Failed to save file");
        goto done;
    }

    if (atomic_load(cancelled))
        goto done;
    if (deneb_print_job_file_validate_build_volume_path(
            upload->staged_gcode, validation_message, sizeof(validation_message)) < 0) {
        fprintf(stderr,
                "deneb-api: print upload build-volume validation failed for %s: %s\n",
                job_name, validation_message[0] ? validation_message : "Invalid print job metadata");
        set_message_response(resp, 422,
                             validation_message[0] ? validation_message : "Invalid print job metadata");
        goto done;
    }

    result = 0;
done:
    if (input >= 0)
        close(input);
    return result;
}

void api_print_job_release_upload(const api_print_upload_t *upload)
{
    if (upload->staged_gcode[0])
        unlink(upload->staged_gcode);
    if (upload->staged_directory[0])
        rmdir(upload->staged_directory);
    if (upload->staged_thumbnail[0])
        unlink(upload->staged_thumbnail);
}

void api_print_job_commit_upload(const api_print_upload_t *upload,
                                  http_response_t *resp)
{
    const deneb_print_job_upload_storage_plan_t *storage = &upload->storage;
    if (!upload->staged_gcode[0]) {
        set_message_response(resp, 500, "Failed to save file");
        return;
    }

    deneb_pending_job_upload_check_t pending_upload;
    if (deneb_pending_job_file_check_upload_default(storage->dest_path,
                                                    storage->filename,
                                                    &pending_upload) == 0 &&
        pending_upload.status != DENEB_PENDING_JOB_UPLOAD_CLEAR) {
        if (pending_upload.status == DENEB_PENDING_JOB_UPLOAD_DUPLICATE) {
            fprintf(stderr, "deneb-api: print upload deduped to existing pending job path=%s\n",
                    pending_upload.path);
            write_pending_job_response(resp, pending_upload.display_name, 200);
            return;
        }

        fprintf(stderr,
                "deneb-api: print upload rejected because another pending print exists: %s\n",
                pending_upload.path);
        set_message_response(resp, 409, "Another print job is already pending");
        return;
    }

    if (rename(upload->staged_gcode, storage->dest_path) < 0) {
        fprintf(stderr, "deneb-api: failed to save print file to %s\n", storage->dest_path);
        set_message_response(resp, 500, "Failed to save file");
        return;
    }

    fprintf(stderr, "deneb-api: print file saved to %s%s\n", storage->dest_path,
            upload->ufp_normalized ? " (extracted from UFP)" : "");
    fprintf(stderr, "deneb-api: registration request sent for %s (%s)\n",
            storage->filename, storage->dest_path);

    char validation_message[256] = "";
    int validation_status_code = 0;
    if (register_native_print(storage->dest_path,
                              upload->owner[0] ? upload->owner : NULL,
                              upload->job_instance_uuid[0] ? upload->job_instance_uuid : NULL,
                              upload->cloud_job_id[0] ? upload->cloud_job_id : NULL,
                              validation_message, sizeof(validation_message),
                              &validation_status_code) < 0) {
        fprintf(stderr, "deneb-api: failed to register print natively for %s\n",
                storage->dest_path);
        deneb_pending_job_file_clear_default();
        unlink(storage->dest_path);
        set_message_response(resp, validation_status_code > 0 ? validation_status_code : 503,
                             validation_message[0] ? validation_message : "Failed to start print");
        return;
    }
    if (upload->staged_thumbnail[0])
        (void)rename(upload->staged_thumbnail, DENEB_ACTIVE_THUMB_PATH);
    fprintf(stderr, "deneb-api: native registration accepted and print metadata prepared for %s\n",
            storage->filename);

    char buf[512];
    deneb_print_job_summary_format_queued_response(
        "Print job accepted", storage->filename, buf, sizeof(buf));
    api_http_set_body_str(resp, buf);
    resp->status_code = 201;
}
