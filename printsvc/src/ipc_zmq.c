/* SPDX-License-Identifier: MPL-2.0 */
#include "config.h"
#include "diagnostics_log.h"
#include "ipc_zmq.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef DENEB_PRINTSVC_HOST_STUB

int deneb_printsvc_ipc_run(deneb_print_service_t *svc)
{
    char frame[1536];
    (void)svc;
    deneb_status_serialize_frame(&svc->status, frame, sizeof(frame));
    printf("%s\n", frame);
    return 0;
}

#else

#include <errno.h>
#include <zmq.h>

#define DENEB_PRINTSVC_IDLE_POLL_MS 250
#define DENEB_PRINTSVC_ACTIVE_POLL_MS 10
#define DENEB_PRINTSVC_JOB_POLL_BURST 8
#define DENEB_PRINTSVC_IDLE_STATUS_PUBLISH_MS 1000
#define DENEB_PRINTSVC_ACTIVE_STATUS_PUBLISH_MS 250
#define DENEB_PRINTSVC_STATUS_HWM 4
#define DENEB_PRINTSVC_ZMQ_MAX_SOCKETS 16
#define DENEB_PRINTSVC_ZMQ_THREAD_STACK_BYTES 65536

static int publish_status(void *pub, const deneb_status_t *status)
{
    char frame[1536];
    int len = deneb_status_serialize_frame(status, frame, sizeof(frame));
    if (len < 0)
        return -1;
    return zmq_send(pub, frame, (size_t)len, ZMQ_DONTWAIT) >= 0 ? 0 : -1;
}

static long long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int service_has_active_work(const deneb_print_service_t *svc)
{
    return svc &&
           (svc->job_active || svc->gcode_queue_active ||
            svc->abort_cleanup_pending || svc->finish_cleanup_pending ||
            svc->pause_policy_pending || svc->pause_position_probe_pending ||
            svc->resume_policy_pending || svc->heater_wait.active);
}

static void configure_status_pub(void *pub)
{
    int hwm = DENEB_PRINTSVC_STATUS_HWM;
    int conflate = 1;

    if (!pub)
        return;

    zmq_setsockopt(pub, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(pub, ZMQ_CONFLATE, &conflate, sizeof(conflate));
}

static void configure_zmq_context(void *ctx)
{
    if (!ctx)
        return;

#ifdef ZMQ_IO_THREADS
    (void)zmq_ctx_set(ctx, ZMQ_IO_THREADS, 1);
#endif
#ifdef ZMQ_MAX_SOCKETS
    (void)zmq_ctx_set(ctx, ZMQ_MAX_SOCKETS,
                      DENEB_PRINTSVC_ZMQ_MAX_SOCKETS);
#endif
#ifdef ZMQ_THREAD_STACK_SIZE
    (void)zmq_ctx_set(ctx, ZMQ_THREAD_STACK_SIZE,
                      DENEB_PRINTSVC_ZMQ_THREAD_STACK_BYTES);
#endif
}

static void poll_job_burst(deneb_print_service_t *svc)
{
    for (int i = 0; i < DENEB_PRINTSVC_JOB_POLL_BURST; i++) {
        int rc = deneb_print_service_poll_job(svc);
        if (rc <= 0)
            return;
    }
}

int deneb_printsvc_ipc_run(deneb_print_service_t *svc)
{
    void *ctx = NULL;
    void *pub = NULL;
    void *rep = NULL;
    int linger = 0;
    long long last_status_publish_ms = 0;
    int rc = -1;

    ctx = zmq_ctx_new();
    if (!ctx)
        return -1;
    configure_zmq_context(ctx);

    pub = zmq_socket(ctx, ZMQ_PUB);
    rep = zmq_socket(ctx, ZMQ_REP);
    if (!pub || !rep)
        goto fail;

    zmq_setsockopt(pub, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(rep, ZMQ_LINGER, &linger, sizeof(linger));
    configure_status_pub(pub);

    if (zmq_bind(pub, DENEB_PRINTSVC_STATUS_ENDPOINT) != 0) {
        fprintf(stderr, "deneb-printsvc: bind %s failed: %s\n",
                DENEB_PRINTSVC_STATUS_ENDPOINT, zmq_strerror(errno));
        goto fail;
    }
    if (zmq_bind(rep, DENEB_PRINTSVC_COMMAND_ENDPOINT) != 0) {
        fprintf(stderr, "deneb-printsvc: bind %s failed: %s\n",
                DENEB_PRINTSVC_COMMAND_ENDPOINT, zmq_strerror(errno));
        goto fail;
    }

    for (;;) {
        zmq_pollitem_t items[] = {{rep, 0, ZMQ_POLLIN, 0}};
        int timeout = service_has_active_work(svc) ?
                          DENEB_PRINTSVC_ACTIVE_POLL_MS :
                          DENEB_PRINTSVC_IDLE_POLL_MS;
        int rc = zmq_poll(items, 1, timeout);

        if (rc > 0 && (items[0].revents & ZMQ_POLLIN)) {
            char buf[1024];
            char reply[256];
            int n = zmq_recv(rep, buf, sizeof(buf) - 1, 0);
            if (n < 0)
                continue;
            buf[n] = '\0';

            deneb_printsvc_ipc_handle_frame(svc, buf, reply, sizeof(reply));
            zmq_send(rep, reply, strlen(reply), 0);
        }

        deneb_print_service_poll_motion(svc);
        poll_job_burst(svc);
        {
            long long now_ms = monotonic_ms();
            int status_publish_ms = service_has_active_work(svc) ?
                                        DENEB_PRINTSVC_ACTIVE_STATUS_PUBLISH_MS :
                                        DENEB_PRINTSVC_IDLE_STATUS_PUBLISH_MS;
            if (last_status_publish_ms == 0 ||
                now_ms - last_status_publish_ms >=
                    status_publish_ms) {
                deneb_print_service_refresh_diagnostics(svc);
                deneb_diagnostics_log_status(&svc->status, 0);
                publish_status(pub, &svc->status);
                last_status_publish_ms = now_ms;
            }
        }
    }

fail:
    if (rep)
        zmq_close(rep);
    if (pub)
        zmq_close(pub);
    if (ctx)
        zmq_ctx_term(ctx);
    return rc;
}

#endif
