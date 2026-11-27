/* SPDX-License-Identifier: MPL-2.0 */
#define main deneb_connector_main
#include "../src/main.c"
#undef main
#include <arpa/inet.h>
#include <assert.h>
#include <sys/wait.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl_ticket.h>
#include <psa/crypto_extra.h>

typedef struct {
    mbedtls_net_context listener;
    const char *cert_file;
    const char *key_file;
    mbedtls_ssl_protocol_version version;
    bool expect_success;
} test_tls_server_t;

static void serve_websocket(test_tls_server_t *server)
{
    mbedtls_net_context peer;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cert;
    mbedtls_pk_context key;
    mbedtls_ssl_ticket_context tickets;
    mbedtls_net_init(&peer);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&cert);
    mbedtls_pk_init(&key);
    mbedtls_ssl_ticket_init(&tickets);

    assert(mbedtls_x509_crt_parse_file(&cert, server->cert_file) == 0);
    assert(mbedtls_pk_parse_keyfile(&key, server->key_file, NULL) == 0);
    assert(mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT) == 0);
    mbedtls_ssl_conf_min_tls_version(&conf, server->version);
    mbedtls_ssl_conf_max_tls_version(&conf, server->version);
    if (server->version == MBEDTLS_SSL_VERSION_TLS1_3) {
        assert(mbedtls_ssl_ticket_setup(&tickets, PSA_ALG_GCM, PSA_KEY_TYPE_AES, 128, 60) == 0);
        mbedtls_ssl_conf_session_tickets_cb(&conf, mbedtls_ssl_ticket_write,
                                            mbedtls_ssl_ticket_parse, &tickets);
    }
    assert(mbedtls_ssl_conf_own_cert(&conf, &cert, &key) == 0);
    assert(mbedtls_ssl_setup(&ssl, &conf) == 0);
    assert(mbedtls_net_accept(&server->listener, &peer, NULL, 0, NULL) == 0);
    mbedtls_ssl_set_bio(&ssl, &peer, mbedtls_net_send, mbedtls_net_recv, NULL);

    int rc = mbedtls_ssl_handshake(&ssl);
    if (server->expect_success) {
        assert(rc == 0);
        char request[768] = "";
        size_t used = 0;
        while (!strstr(request, "\r\n\r\n")) {
            assert(used < sizeof(request) - 1);
            assert(mbedtls_ssl_read(&ssl, (uint8_t *)request + used, 1) == 1);
            request[++used] = '\0';
        }
        const char key_header[] = "Sec-WebSocket-Key: ";
        char *nonce = strstr(request, key_header);
        assert(nonce);
        nonce += strlen(key_header);
        char *end = strstr(nonce, "\r\n");
        assert(end && end - nonce == 24);
        *end = '\0';
        char accept[32];
        assert(ws_expected_accept(nonce, accept) == 0);
        char response[256];
        int len = snprintf(response, sizeof(response),
                           "HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
        assert(len > 0 && (size_t)len < sizeof(response));
        assert(mbedtls_ssl_write(&ssl, (const uint8_t *)response, (size_t)len) == len);
        const uint8_t frame[] = {0x81, 2, 'o', 'k'};
        assert(mbedtls_ssl_write(&ssl, frame, sizeof(frame)) == (int)sizeof(frame));
    } else {
        assert(rc != 0);
    }
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_net_free(&peer);
    mbedtls_pk_free(&key);
    mbedtls_x509_crt_free(&cert);
    mbedtls_ssl_ticket_free(&tickets);
}

static void assert_connection(test_tls_server_t *server, const char *host,
                              const char *ca_directory)
{
    mbedtls_net_init(&server->listener);
    assert(mbedtls_net_bind(&server->listener, "127.0.0.1", "0", MBEDTLS_NET_PROTO_TCP) == 0);
    struct sockaddr_in address;
    socklen_t address_len = sizeof(address);
    assert(getsockname(server->listener.fd, (struct sockaddr *)&address, &address_len) == 0);

    // The default TF-PSA-Crypto build has no locks for its global state.
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        alarm(5);
        mbedtls_psa_crypto_free();
        assert(psa_crypto_init() == PSA_SUCCESS);
        serve_websocket(server);
        _exit(0);
    }
    // mbedtls_net_free would shut down the child's copy of the listener.
    close(server->listener.fd);
    df_url_t url = {.scheme = "wss", .path = "/"};
    assert(snprintf(url.host, sizeof(url.host), "%s", host) > 0);
    assert(snprintf(url.port, sizeof(url.port), "%u", (unsigned)ntohs(address.sin_port)) > 0);
    ws_client_t client;
    int rc = ws_connect(&client, &url, ca_directory);
    bool connected = client.connected;
    if (connected) {
        char message[16];
        assert(ws_read_text(&client, message, sizeof(message)) == 0);
        assert(strcmp(message, "ok") == 0);
    }
    ws_free(&client);
    int status;
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert((rc == 0) == server->expect_success);
    assert(connected == server->expect_success);
}

int main(int argc, char **argv)
{
    assert(argc == 5);
    assert(psa_crypto_init() == PSA_SUCCESS);
    char accept[32];
    assert(ws_expected_accept("dGhlIHNhbXBsZSBub25jZQ==", accept) == 0);
    assert(strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);

    test_tls_server_t server = {
        .cert_file = argv[1],
        .key_file = argv[2],
        .version = MBEDTLS_SSL_VERSION_TLS1_2,
        .expect_success = true,
    };
    assert_connection(&server, "localhost", argv[3]);
    server.expect_success = false;
    assert_connection(&server, "127.0.0.1", argv[3]);
    assert_connection(&server, "localhost", argv[4]);
    server.version = MBEDTLS_SSL_VERSION_TLS1_3;
    server.expect_success = true;
    assert_connection(&server, "localhost", argv[3]);
    return 0;
}
