/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Print job endpoint implementations.
 */

#include "api_print_job.h"
#include "backend_zmq.h"
#include "json_writer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define DENEB_PRINT_SPOOL_DIR "/home/3D/deneb-uploads"
#define DENEB_CLUSTER_PENDING_JOB "/tmp/deneb-cluster-print-job.json"

static int register_coordinator_print(const char *path)
{
    static const char script[] =
        "import datetime, json, os, sys\n"
        "sys.path.insert(0, '/home')\n"
        "sys.path.insert(0, '/home/lib')\n"
        "from sponge.spinner import spinner\n"
        "from sponge.ipc import zmqipc\n"
        "import gershwin.constructs as gershwin\n"
        "import gershwin.node as node\n"
        "from gershwin.manager import Manager\n"
        "from cygnus.util import configuration, ufp_format\n"
        "from cygnus.marshal.types.printing import PrintHandlingRequest, PrintHandlingInstruction\n"
        "try:\n"
        "    from cygnus_materials.material_db import get_material_string\n"
        "except Exception:\n"
        "    get_material_string = None\n"
        "PENDING = '" DENEB_CLUSTER_PENDING_JOB "'\n"
        "def mat_name(guid):\n"
        "    if get_material_string:\n"
        "        try: return get_material_string(guid)\n"
        "        except Exception: pass\n"
        "    return guid or 'Unknown'\n"
        "def material_type(name):\n"
        "    return 'PLA' if 'PLA' in name else (name or 'Unknown')\n"
        "def write_pending(path, tracker=None):\n"
        "    loaded_guid = configuration.get_option('material_guid', '')\n"
        "    loaded_name = mat_name(loaded_guid)\n"
        "    nozzle = configuration.get_option('nozzle_size', '0.4')\n"
        "    meta = {}\n"
        "    try: meta = ufp_format.get_meta_data(path)\n"
        "    except Exception: pass\n"
        "    target_guid = meta.get('material_guid') or loaded_guid\n"
        "    target_name = mat_name(target_guid)\n"
        "    target_nozzle = str(meta.get('nozzle_size') or nozzle)\n"
        "    changes = []\n"
        "    if loaded_guid and target_guid and loaded_guid != target_guid:\n"
        "        changes.append({'type_of_change':'material_change','index':0,'origin_id':loaded_guid,'origin_name':loaded_name,'target_id':target_guid,'target_name':target_name})\n"
        "    if nozzle and target_nozzle and nozzle != target_nozzle:\n"
        "        changes.append({'type_of_change':'print_core_change','index':0,'origin_id':nozzle + ' mm','origin_name':nozzle + ' mm','target_id':target_nozzle + ' mm','target_name':target_nozzle + ' mm'})\n"
        "    job = {'uuid':'deneb-current-job','created_at':datetime.datetime.utcnow().isoformat(timespec='milliseconds') + 'Z','name':os.path.basename(path),'status':'wait_user_action' if changes else 'pre_print','time_total':0,'time_elapsed':0,'started':False,'force':False,'machine_variant':'Ultimaker 2+ Connect','owner':'Cura','assigned_to':'00000000-0000-0000-0000-000000000000','build_plate':{'type':'glass'},'configuration':[{'extruder_index':0,'print_core_id':target_nozzle + ' mm','material':{'guid':target_guid,'brand':'Generic','material':material_type(target_name),'color':'#ffc924'}}],'compatible_machine_families':['ultimaker2_plus_connect','Ultimaker 2+ Connect'],'impediments_to_printing':[]}\n"
        "    if tracker is not None: job['deneb_tracker'] = tracker\n"
        "    if changes: job['configuration_changes_required'] = changes\n"
        "    with open(PENDING, 'w') as f: json.dump([job], f, separators=(',', ':'))\n"
        "    return len(changes)\n"
        "print('registering print with stock coordinator: %s' % sys.argv[1])\n"
        "result = {'done': False, 'reply': None, 'change_count': -1, 'prepare_reply': None}\n"
        "@gershwin.node('client')\n"
        "class ClientNode(node.Node):\n"
        "    def plan(self):\n"
        "        self.doOption(gershwin.DO_OPTIONS.DO_IMMEDIATELY)\n"
        "        self.addStep(goal='register print', func=self._run)\n"
        "    def _run(self):\n"
        "        req = PrintHandlingRequest.create(tracker=0, instruction=PrintHandlingInstruction.REGISTER, options={'path': sys.argv[1]})\n"
        "        result['reply'] = yield from self.call('coordinator::print::handling', req)\n"
        "        reply = result['reply'] or {}\n"
        "        print_tracker = reply.get('tracker')\n"
        "        if reply.get('accepted') and print_tracker is not None:\n"
        "            result['change_count'] = write_pending(sys.argv[1], print_tracker)\n"
        "            if result['change_count'] == 0:\n"
        "                req = PrintHandlingRequest.create(tracker=print_tracker, instruction=PrintHandlingInstruction.PREPARE, options={})\n"
        "                result['prepare_reply'] = yield from self.call('coordinator::print::handling', req)\n"
        "        result['done'] = True\n"
        "m = Manager('deneb-api-register', spinner.Spinner, zmqipc.ZMQIPC, ip='tcp://127.0.0.1:', pubbase=5546, pubinstance=3)\n"
        "m.addNode(ClientNode.id, ClientNode)\n"
        "for _ in range(30):\n"
        "    m.spinner.spin(100)\n"
        "    m.run_time = m.spinner.getElapsed()\n"
        "    m.spin()\n"
        "    if result['done']:\n"
        "        break\n"
        "if not result['done']:\n"
        "    print('timed out waiting for coordinator tracker')\n"
        "    sys.exit(2)\n"
        "reply = result['reply'] or {}\n"
        "print('coordinator reply: %r' % (reply,))\n"
        "if not reply.get('accepted'):\n"
        "    sys.exit(3)\n"
        "print_tracker = reply.get('tracker')\n"
        "if print_tracker is None:\n"
        "    sys.exit(4)\n"
        "change_count = result.get('change_count', -1)\n"
        "if change_count < 0:\n"
        "    sys.exit(5)\n"
        "if change_count == 0:\n"
        "    prepare_reply = result.get('prepare_reply') or {}\n"
        "    print('prepare reply: %r' % (prepare_reply,))\n"
        "    if not prepare_reply.get('accepted'):\n"
        "        sys.exit(6)\n"
        "    try: os.unlink(PENDING)\n"
        "    except Exception: pass\n"
        "sys.exit(0)\n";

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int log_fd = open("/tmp/deneb-register-print.log",
                          O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }
        setenv("PYTHONPATH", "/home", 1);
        execlp("python3", "python3", "-c", script, path, (char *)NULL);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

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
     * to internal printer storage, and send a ZMQ JOB command.
     */
    if (req->upload_path[0] == '\0') {
        fprintf(stderr, "deneb-api: print upload rejected: no upload_path\n");
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
        fprintf(stderr, "deneb-api: print upload rejected: failed to extract multipart file\n");
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Failed to extract file from upload\"}");
        return;
    }

    if (gcode_path[0] == '\0') {
        fprintf(stderr, "deneb-api: print upload rejected: no file field\n");
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

    if (mkdir(DENEB_PRINT_SPOOL_DIR, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "deneb-api: failed to create print spool %s: %s\n",
                DENEB_PRINT_SPOOL_DIR, strerror(errno));
        unlink(gcode_path);
        resp->status_code = 500;
        api_http_set_body_str(resp, "{\"message\":\"Failed to prepare print storage\"}");
        return;
    }

    /* Move file to persistent storage where the print service can find it. */
    char dest_path[256];
    snprintf(dest_path, sizeof(dest_path), "%s/%s", DENEB_PRINT_SPOOL_DIR, filename);

    if (rename(gcode_path, dest_path) < 0) {
        int rename_errno = errno;
        /* If rename fails (cross-device), copy instead */
        int src_fd = open(gcode_path, O_RDONLY);
        int dst_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (src_fd < 0 || dst_fd < 0) {
            fprintf(stderr,
                    "deneb-api: failed to save print file to %s after rename error %s: src=%s dst=%s\n",
                    dest_path, strerror(rename_errno),
                    src_fd < 0 ? strerror(errno) : "ok",
                    dst_fd < 0 ? strerror(errno) : "ok");
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
        while ((nr = read(src_fd, buf, sizeof(buf))) >= 0) {
            if (nr == 0) break;
            if (write(dst_fd, buf, (size_t)nr) != nr) {
                copy_ok = 0;
                break;
            }
        }
        if (nr < 0) {
            copy_ok = 0;
        }
        close(src_fd);
        close(dst_fd);
        if (!copy_ok) {
            fprintf(stderr, "deneb-api: failed while copying print file to %s\n", dest_path);
            unlink(dest_path);
            unlink(gcode_path);
            resp->status_code = 500;
            api_http_set_body_str(resp, "{\"message\":\"Failed to save file\"}");
            return;
        }
        unlink(gcode_path);
    }

    fprintf(stderr, "deneb-api: print file saved to %s\n", dest_path);

    if (register_coordinator_print(dest_path) < 0) {
        fprintf(stderr, "deneb-api: failed to register print with coordinator for %s\n", dest_path);
        resp->status_code = 503;
        api_http_set_body_str(resp, "{\"message\":\"Failed to start print\"}");
        return;
    }

    /* Return print job info */
    char buf[512];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "message", "Print job accepted");
    json_str(&w, "name", filename);
    json_str(&w, "uuid", "0");
    json_str(&w, "source", "WEB_API");
    json_str(&w, "state", "pre_print");
    json_float(&w, "progress", 0.0);
    json_int(&w, "time_elapsed", 0);
    json_int(&w, "time_total", 0);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
    resp->status_code = 201;
}
