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

static int publish_status(void *pub, const deneb_status_t *status)
{
    char frame[1536];
    int len = deneb_status_serialize_frame(status, frame, sizeof(frame));
    if (len < 0)
        return -1;
    return zmq_send(pub, frame, (size_t)len, ZMQ_DONTWAIT) >= 0 ? 0 : -1;
}

int deneb_printsvc_ipc_run(deneb_print_service_t *svc)
{
    void *ctx;
    void *pub;
    void *rep;
    int linger = 0;

    ctx = zmq_ctx_new();
    if (!ctx)
        return -1;

    pub = zmq_socket(ctx, ZMQ_PUB);
    rep = zmq_socket(ctx, ZMQ_REP);
    if (!pub || !rep)
        return -1;

    zmq_setsockopt(pub, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(rep, ZMQ_LINGER, &linger, sizeof(linger));

    if (zmq_bind(pub, DENEB_PRINTSVC_STATUS_ENDPOINT) != 0) {
        fprintf(stderr, "deneb-printsvc: bind %s failed: %s\n",
                DENEB_PRINTSVC_STATUS_ENDPOINT, zmq_strerror(errno));
        return -1;
    }
    if (zmq_bind(rep, DENEB_PRINTSVC_COMMAND_ENDPOINT) != 0) {
        fprintf(stderr, "deneb-printsvc: bind %s failed: %s\n",
                DENEB_PRINTSVC_COMMAND_ENDPOINT, zmq_strerror(errno));
        return -1;
    }

    for (;;) {
        zmq_pollitem_t items[] = {{rep, 0, ZMQ_POLLIN, 0}};
        int rc = zmq_poll(items, 1, 250);

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
        deneb_print_service_poll_job(svc);
        deneb_print_service_refresh_diagnostics(svc);
        deneb_diagnostics_log_status(&svc->status, 0);
        publish_status(pub, &svc->status);
    }
}

#endif
