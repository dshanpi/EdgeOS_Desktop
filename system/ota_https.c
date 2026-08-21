#define _GNU_SOURCE
#include "ota_https.h"
#include "ota_trust_store.h"

#include <canmv_misc.h>
#include <drv_pufs.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <mbedtls/x509_crt.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define OTA_HTTPS_HEADER_MAX       8192u
#define OTA_HTTPS_IO_CHUNK         (64u * 1024u)
#define OTA_HTTPS_CONNECT_TIMEOUT  15u
#define OTA_HTTPS_IO_TIMEOUT_MS    20000u
#define OTA_HTTPS_HANDSHAKE_TIMEOUT 60u
#define OTA_HTTPS_CLOCK_REFRESH_MS  (5u * 60u * 1000u)
#define OTA_VALID_TIME_MIN         ((time_t)1785542400) /* 2026-08-01 */
#define OTA_VALID_TIME_MAX         ((time_t)2240611200) /* 2041-01-01 */

typedef struct {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt ca;
    mbedtls_ctr_drbg_context ctr_drbg;
    drv_pufs_inst rng;
    bool rng_open;
    bool handshake_complete;
} ota_tls_connection_t;

static const int g_ota_tls12_ciphers[] = {
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
    0
};

static const char *g_ota_alpn[] = { "http/1.1", NULL };
static uint64_t g_ota_clock_checked_ms;

static void ota_error(char *error, size_t size, const char *format, ...)
{
    va_list arguments;

    if (error == NULL || size == 0)
        return;
    va_start(arguments, format);
    vsnprintf(error, size, format, arguments);
    va_end(arguments);
    error[size - 1] = '\0';
}

static uint64_t ota_monotonic_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static int ota_deadline_expired(uint64_t deadline_ms)
{
    uint64_t now = ota_monotonic_ms();

    return now == 0 || deadline_ms == 0 || now >= deadline_ms;
}

static int ota_host_character_valid(unsigned char value)
{
    return isalnum(value) || value == '.' || value == '-';
}

int ota_https_parse_url(const char *url, ota_https_url_t *parsed)
{
    const char *authority;
    const char *authority_end;
    const char *path;
    const char *colon = NULL;
    size_t authority_length;
    size_t host_length;
    unsigned long port = 443;
    const char *cursor;
    char *port_end;

    if (url == NULL || parsed == NULL || strncmp(url, "https://", 8) != 0)
        return -1;
    if (strlen(url) >= OTA_HTTPS_URL_MAX)
        return -1;

    authority = url + 8;
    path = strchr(authority, '/');
    authority_end = path != NULL ? path : authority + strlen(authority);
    authority_length = (size_t)(authority_end - authority);
    if (authority_length == 0)
        return -1;

    for (cursor = authority; cursor < authority_end; ++cursor) {
        if (*cursor == ':') {
            if (colon != NULL)
                return -1;
            colon = cursor;
        } else if (!ota_host_character_valid((unsigned char)*cursor)) {
            return -1;
        }
    }

    host_length = (size_t)((colon != NULL ? colon : authority_end) - authority);
    if (host_length == 0 || host_length >= OTA_HTTPS_HOST_MAX ||
        authority[0] == '.' || authority[host_length - 1] == '.' ||
        authority[0] == '-' || authority[host_length - 1] == '-')
        return -1;

    if (colon != NULL) {
        if (colon + 1 == authority_end)
            return -1;
        errno = 0;
        port = strtoul(colon + 1, &port_end, 10);
        if (errno != 0 || port_end != authority_end || port == 0 ||
            port > 65535)
            return -1;
    }

    if (path != NULL) {
        size_t path_length = strlen(path);
        if (path_length == 0 || path_length >= OTA_HTTPS_PATH_MAX)
            return -1;
        for (cursor = path; *cursor != '\0'; ++cursor) {
            unsigned char value = (unsigned char)*cursor;
            if (value < 0x21 || value == 0x7f || value == '\\' ||
                value == '?' || value == '#')
                return -1;
        }
    }

    memset(parsed, 0, sizeof(*parsed));
    memcpy(parsed->host, authority, host_length);
    parsed->host[host_length] = '\0';
    parsed->port = (uint16_t)port;
    snprintf(parsed->path, sizeof(parsed->path), "%s", path != NULL ? path : "/");
    return 0;
}

int ota_https_same_origin(const char *first, const char *second)
{
    ota_https_url_t first_url;
    ota_https_url_t second_url;

    if (ota_https_parse_url(first, &first_url) != 0 ||
        ota_https_parse_url(second, &second_url) != 0)
        return 0;
    return first_url.port == second_url.port &&
           strcasecmp(first_url.host, second_url.host) == 0;
}

int ota_https_derive_signature_url(const char *manifest_url, char *signature_url,
                                   size_t signature_url_size)
{
    ota_https_url_t parsed;
    int length;

    if (signature_url == NULL || signature_url_size == 0 ||
        ota_https_parse_url(manifest_url, &parsed) != 0)
        return -1;
    length = snprintf(signature_url, signature_url_size, "%s.sig", manifest_url);
    if (length <= 0 || (size_t)length >= signature_url_size)
        return -1;
    return 0;
}

static int ota_prepare_clock(char *error, size_t error_size)
{
    time_t timestamp = 0;
    uint64_t now = ota_monotonic_ms();
    int rtc_valid;

    if (now == 0) {
        ota_error(error, error_size, "Monotonic clock is unavailable");
        return -1;
    }
    rtc_valid = canmv_misc_get_utc_timestamp(&timestamp) == 0 &&
                timestamp >= OTA_VALID_TIME_MIN &&
                timestamp < OTA_VALID_TIME_MAX;
    if (g_ota_clock_checked_ms != 0 && now >= g_ota_clock_checked_ms &&
        now - g_ota_clock_checked_ms < OTA_HTTPS_CLOCK_REFRESH_MS && rtc_valid)
        return 0;

    g_ota_clock_checked_ms = now;
    if (canmv_misc_ntp_sync() > 0) {
        sleep(1);
        timestamp = 0;
        if (canmv_misc_get_utc_timestamp(&timestamp) == 0 &&
            timestamp >= OTA_VALID_TIME_MIN && timestamp < OTA_VALID_TIME_MAX)
            return 0;
    }

    /* NTP availability must not become a downgrade switch.  A plausible RTC
     * may still be used, but TLS certificate validation remains mandatory. */
    if (rtc_valid)
        return 0;

    ota_error(error, error_size, "RTC/NTP time is invalid");
    return -1;
}

static int ota_hardware_random(void *context, unsigned char *output, size_t size)
{
    drv_pufs_inst *rng = context;

    if (rng == NULL || output == NULL)
        return -1;
    while (size > 0) {
        uint32_t chunk = size > 4096u ? 4096u : (uint32_t)size;
        if (drv_pufs_rng_read(rng, output, chunk) != 0)
            return -1;
        output += chunk;
        size -= chunk;
    }
    return 0;
}

static int ota_wait_socket(int fd, int want_read, uint64_t deadline_ms,
                           uint64_t maximum_wait_ms)
{
    uint64_t start = ota_monotonic_ms();
    uint64_t wait_deadline;

    if (start == 0 || deadline_ms == 0 || start >= deadline_ms)
        return -1;
    wait_deadline = start + maximum_wait_ms;
    if (wait_deadline < start || deadline_ms < wait_deadline)
        wait_deadline = deadline_ms;
    for (;;) {
        fd_set read_set;
        fd_set write_set;
        struct timeval timeout;
        uint64_t now = ota_monotonic_ms();
        uint64_t remaining;
        int selected;

        if (fd < 0 || fd >= FD_SETSIZE || now == 0 || deadline_ms == 0 ||
            now >= deadline_ms || now >= wait_deadline)
            return -1;
        remaining = wait_deadline - now;
        timeout.tv_sec = (time_t)(remaining / 1000u);
        timeout.tv_usec = (suseconds_t)((remaining % 1000u) * 1000u);
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        if (want_read)
            FD_SET(fd, &read_set);
        else
            FD_SET(fd, &write_set);
        selected = select(fd + 1, want_read ? &read_set : NULL,
                          want_read ? NULL : &write_set, NULL, &timeout);
        if (selected > 0)
            return 0;
        if (selected == 0)
            return -1;
        if (errno != EINTR)
            return -1;
    }
}

static int ota_tcp_connect(const ota_https_url_t *url, uint64_t deadline_ms)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *item;
    char port_text[8];
    int result = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)url->port);
    if (getaddrinfo(url->host, port_text, &hints, &addresses) != 0)
        return -1;

    for (item = addresses; item != NULL; item = item->ai_next) {
        int fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        int flags;
        int connected = 0;

        if (fd < 0)
            continue;
        if (fd >= FD_SETSIZE) {
            close(fd);
            continue;
        }
        flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
            close(fd);
            continue;
        }

        if (connect(fd, item->ai_addr, item->ai_addrlen) == 0) {
            connected = 1;
        } else if (errno == EINPROGRESS) {
            int socket_error = 0;
            socklen_t socket_error_size = sizeof(socket_error);

            if (ota_wait_socket(fd, 0, deadline_ms,
                                OTA_HTTPS_CONNECT_TIMEOUT * 1000u) == 0 &&
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                           &socket_error_size) == 0 && socket_error == 0)
                connected = 1;
        }

        if (!connected) {
            close(fd);
            continue;
        }
        /* Keep the descriptor nonblocking. TLS WANT_READ/WANT_WRITE is driven
         * by ota_wait_socket() against an absolute operation deadline. */
        result = fd;
        break;
    }

    freeaddrinfo(addresses);
    return result;
}

static void ota_tls_init(ota_tls_connection_t *connection)
{
    memset(connection, 0, sizeof(*connection));
    mbedtls_net_init(&connection->net);
    mbedtls_ssl_init(&connection->ssl);
    mbedtls_ssl_config_init(&connection->config);
    mbedtls_x509_crt_init(&connection->ca);
    mbedtls_ctr_drbg_init(&connection->ctr_drbg);
}

static void ota_tls_free(ota_tls_connection_t *connection)
{
    if (connection->handshake_complete)
        (void)mbedtls_ssl_close_notify(&connection->ssl);
    mbedtls_net_free(&connection->net);
    mbedtls_ssl_free(&connection->ssl);
    mbedtls_ssl_config_free(&connection->config);
    mbedtls_x509_crt_free(&connection->ca);
    mbedtls_ctr_drbg_free(&connection->ctr_drbg);
    if (connection->rng_open)
        (void)drv_pufs_close(&connection->rng);
    memset(connection, 0, sizeof(*connection));
}

static int ota_tls_connect(ota_tls_connection_t *connection,
                           const ota_https_url_t *url, uint64_t deadline_ms,
                           char *error, size_t error_size)
{
    static const unsigned char personalization[] = "DshanPI-CanMV-V3-OTA-TLS";
    int result;
    uint64_t handshake_deadline;

    if (ota_prepare_clock(error, error_size) != 0)
        return -1;
    if (drv_pufs_open(&connection->rng) != 0) {
        ota_error(error, error_size, "PUFS hardware RNG is unavailable");
        return -1;
    }
    connection->rng_open = true;
    result = mbedtls_ctr_drbg_seed(&connection->ctr_drbg, ota_hardware_random,
                                   &connection->rng, personalization,
                                   sizeof(personalization) - 1);
    if (result != 0) {
        ota_error(error, error_size, "Cannot seed TLS from PUFS RNG (%d)", result);
        return -1;
    }
    result = mbedtls_x509_crt_parse(&connection->ca, dshanpi_ota_tls_ca_bundle,
                                    dshanpi_ota_tls_ca_bundle_size);
    if (result != 0) {
        ota_error(error, error_size, "Embedded TLS CA bundle is invalid (%d)", result);
        return -1;
    }
    result = mbedtls_ssl_config_defaults(&connection->config,
                                         MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT);
    if (result != 0) {
        ota_error(error, error_size, "Cannot initialize TLS configuration (%d)",
                  result);
        return -1;
    }

    mbedtls_ssl_conf_authmode(&connection->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&connection->config, &connection->ca, NULL);
    mbedtls_ssl_conf_rng(&connection->config, mbedtls_ctr_drbg_random,
                         &connection->ctr_drbg);
    mbedtls_ssl_conf_read_timeout(&connection->config, OTA_HTTPS_IO_TIMEOUT_MS);
    mbedtls_ssl_conf_min_tls_version(&connection->config,
                                     MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&connection->config,
                                     MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_ciphersuites(&connection->config, g_ota_tls12_ciphers);
    result = mbedtls_ssl_conf_alpn_protocols(&connection->config, g_ota_alpn);
    if (result != 0) {
        ota_error(error, error_size, "Cannot configure HTTP/1.1 ALPN (%d)", result);
        return -1;
    }
    result = mbedtls_ssl_setup(&connection->ssl, &connection->config);
    if (result != 0 || mbedtls_ssl_set_hostname(&connection->ssl, url->host) != 0) {
        ota_error(error, error_size, "Cannot configure TLS hostname");
        return -1;
    }

    connection->net.fd = ota_tcp_connect(url, deadline_ms);
    if (connection->net.fd < 0) {
        ota_error(error, error_size, "Cannot connect to OTA server");
        return -1;
    }
    mbedtls_ssl_set_bio(&connection->ssl, &connection->net, mbedtls_net_send,
                        mbedtls_net_recv, NULL);

    handshake_deadline = ota_monotonic_ms();
    if (handshake_deadline != 0)
        handshake_deadline += OTA_HTTPS_HANDSHAKE_TIMEOUT * 1000u;
    if (deadline_ms != 0 &&
        (handshake_deadline == 0 || deadline_ms < handshake_deadline))
        handshake_deadline = deadline_ms;

    do {
        if (ota_deadline_expired(handshake_deadline)) {
            ota_error(error, error_size, "TLS handshake timed out");
            return -1;
        }
        result = mbedtls_ssl_handshake(&connection->ssl);
        if ((result == MBEDTLS_ERR_SSL_WANT_READ ||
             result == MBEDTLS_ERR_SSL_WANT_WRITE) &&
            ota_wait_socket(connection->net.fd,
                            result == MBEDTLS_ERR_SSL_WANT_READ,
                            handshake_deadline,
                            OTA_HTTPS_IO_TIMEOUT_MS) != 0) {
            ota_error(error, error_size, "TLS handshake timed out");
            return -1;
        }
    } while (result == MBEDTLS_ERR_SSL_WANT_READ ||
             result == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (result != 0) {
        ota_error(error, error_size, "TLS handshake/certificate failed (%d)", result);
        return -1;
    }
    if (mbedtls_ssl_get_verify_result(&connection->ssl) != 0 ||
        mbedtls_ssl_get_peer_cert(&connection->ssl) == NULL) {
        ota_error(error, error_size, "OTA server certificate is not trusted");
        return -1;
    }
    connection->handshake_complete = true;
    return 0;
}

static int ota_tls_write_all(ota_tls_connection_t *connection,
                             const unsigned char *data, size_t size,
                             uint64_t deadline_ms)
{
    while (size > 0) {
        int result;
        if (ota_deadline_expired(deadline_ms))
            return -1;
        result = mbedtls_ssl_write(&connection->ssl, data, size);
        if (result == MBEDTLS_ERR_SSL_WANT_READ ||
            result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (ota_wait_socket(connection->net.fd,
                                result == MBEDTLS_ERR_SSL_WANT_READ,
                                deadline_ms, OTA_HTTPS_IO_TIMEOUT_MS) != 0)
                return -1;
            continue;
        }
        if (result <= 0)
            return -1;
        data += (size_t)result;
        size -= (size_t)result;
    }
    return 0;
}

static int ota_tls_read(ota_tls_connection_t *connection, unsigned char *data,
                        size_t size, uint64_t deadline_ms)
{
    int result;

    do {
        if (ota_deadline_expired(deadline_ms))
            return -1;
        result = mbedtls_ssl_read(&connection->ssl, data, size);
        if ((result == MBEDTLS_ERR_SSL_WANT_READ ||
             result == MBEDTLS_ERR_SSL_WANT_WRITE) &&
            ota_wait_socket(connection->net.fd,
                            result == MBEDTLS_ERR_SSL_WANT_READ,
                            deadline_ms, OTA_HTTPS_IO_TIMEOUT_MS) != 0)
            return -1;
    } while (result == MBEDTLS_ERR_SSL_WANT_READ ||
             result == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        return 0;
    return result;
}

static int ota_header_name_equal(const char *name, size_t name_size,
                                 const char *expected)
{
    return strlen(expected) == name_size &&
           strncasecmp(name, expected, name_size) == 0;
}

static int ota_http_field_value_valid(const char *value)
{
    size_t length = 0;

    if (value == NULL)
        return 0;
    while (value[length] != '\0') {
        unsigned char character = (unsigned char)value[length];

        if (character < 0x20 || character > 0x7e || ++length > 128)
            return 0;
    }
    return length > 0;
}

static int ota_parse_content_length(const char *value, size_t value_size,
                                    uint64_t *length)
{
    uint64_t parsed = 0;
    size_t index = 0;

    while (index < value_size && (value[index] == ' ' || value[index] == '\t'))
        ++index;
    if (index == value_size)
        return -1;
    for (; index < value_size && isdigit((unsigned char)value[index]); ++index) {
        unsigned digit = (unsigned)(value[index] - '0');
        if (parsed > (UINT64_MAX - digit) / 10u)
            return -1;
        parsed = parsed * 10u + digit;
    }
    while (index < value_size && (value[index] == ' ' || value[index] == '\t'))
        ++index;
    if (index != value_size)
        return -1;
    *length = parsed;
    return 0;
}

static int ota_parse_http_header(char *header, uint64_t maximum_body_size,
                                 uint64_t expected_body_size,
                                 uint64_t *content_length,
                                 char *error, size_t error_size)
{
    char *line_end;
    char *cursor;
    bool length_seen = false;

    line_end = strstr(header, "\r\n");
    if (line_end == NULL || strncmp(header, "HTTP/1.1 200 ", 13) != 0) {
        ota_error(error, error_size, "OTA server did not return HTTP/1.1 200");
        return -1;
    }
    cursor = line_end + 2;
    while (*cursor != '\0') {
        char *colon;
        char *value;
        size_t name_size;
        size_t value_size;

        line_end = strstr(cursor, "\r\n");
        if (line_end == NULL)
            break;
        if (line_end == cursor)
            break;
        if (*cursor == ' ' || *cursor == '\t') {
            ota_error(error, error_size, "Folded HTTP headers are not accepted");
            return -1;
        }
        colon = memchr(cursor, ':', (size_t)(line_end - cursor));
        if (colon == NULL) {
            ota_error(error, error_size, "Malformed HTTP response header");
            return -1;
        }
        name_size = (size_t)(colon - cursor);
        value = colon + 1;
        value_size = (size_t)(line_end - value);

        if (ota_header_name_equal(cursor, name_size, "Content-Length")) {
            if (length_seen ||
                ota_parse_content_length(value, value_size, content_length) != 0) {
                ota_error(error, error_size, "Invalid or duplicate Content-Length");
                return -1;
            }
            length_seen = true;
        } else if (ota_header_name_equal(cursor, name_size,
                                         "Transfer-Encoding")) {
            ota_error(error, error_size, "Transfer-Encoding is not accepted");
            return -1;
        } else if (ota_header_name_equal(cursor, name_size,
                                         "Content-Encoding")) {
            while (value_size > 0 && (*value == ' ' || *value == '\t')) {
                ++value;
                --value_size;
            }
            while (value_size > 0 &&
                   (value[value_size - 1] == ' ' ||
                    value[value_size - 1] == '\t'))
                --value_size;
            if (!(value_size == 8 && strncasecmp(value, "identity", 8) == 0)) {
                ota_error(error, error_size, "Compressed HTTP bodies are not accepted");
                return -1;
            }
        }
        cursor = line_end + 2;
    }

    if (!length_seen || *content_length == 0 ||
        *content_length > maximum_body_size) {
        ota_error(error, error_size, "Missing or out-of-range Content-Length");
        return -1;
    }
    if (expected_body_size != 0 && *content_length != expected_body_size) {
        ota_error(error, error_size, "OTA body size differs from signed manifest");
        return -1;
    }
    return 0;
}

int ota_https_get(const char *url_text, const ota_https_request_t *request,
                  ota_https_body_cb_t body_cb, void *body_context,
                  uint64_t *body_size, char *error, size_t error_size)
{
    ota_https_url_t url;
    ota_tls_connection_t connection;
    unsigned char *io_buffer = NULL;
    char http_request[1024];
    char header[OTA_HTTPS_HEADER_MAX + 1];
    size_t header_size = 0;
    char *header_end;
    uint64_t content_length = 0;
    uint64_t received = 0;
    uint64_t deadline_ms;
    int result = -1;
    int request_size;

    if (body_size != NULL)
        *body_size = 0;
    if (error != NULL && error_size > 0)
        error[0] = '\0';
    if (request == NULL || body_cb == NULL ||
        !ota_http_field_value_valid(request->accept) ||
        request->maximum_body_size == 0 ||
        request->total_timeout_seconds == 0 ||
        ota_https_parse_url(url_text, &url) != 0) {
        ota_error(error, error_size, "Invalid HTTPS OTA URL or request");
        return -1;
    }
    /* The RT-Smart MbedTLS socket port writes directly to the TCP fd.  A
     * peer reset must become an ordinary I/O error instead of terminating the
     * complete launcher process with SIGPIPE. */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        ota_error(error, error_size, "Cannot configure safe TLS socket writes");
        return -1;
    }

    deadline_ms = ota_monotonic_ms();
    if (deadline_ms == 0) {
        ota_error(error, error_size, "Monotonic clock is unavailable");
        return -1;
    }
    deadline_ms += (uint64_t)request->total_timeout_seconds * 1000u;

    ota_tls_init(&connection);
    if (ota_tls_connect(&connection, &url, deadline_ms, error, error_size) != 0)
        goto done;

    if (url.port == 443) {
        request_size = snprintf(
            http_request, sizeof(http_request),
            "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
            "User-Agent: DshanPI-CanMV-V3-OTA/2.0\r\nAccept: %s\r\n"
            "Accept-Encoding: identity\r\nCache-Control: no-cache\r\n\r\n",
            url.path, url.host, request->accept);
    } else {
        request_size = snprintf(
            http_request, sizeof(http_request),
            "GET %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n"
            "User-Agent: DshanPI-CanMV-V3-OTA/2.0\r\nAccept: %s\r\n"
            "Accept-Encoding: identity\r\nCache-Control: no-cache\r\n\r\n",
            url.path, url.host, (unsigned)url.port, request->accept);
    }
    if (request_size <= 0 || (size_t)request_size >= sizeof(http_request) ||
        ota_tls_write_all(&connection, (const unsigned char *)http_request,
                          (size_t)request_size, deadline_ms) != 0) {
        ota_error(error, error_size, "Cannot send HTTPS OTA request");
        goto done;
    }

    header[0] = '\0';
    header_end = NULL;
    while (header_size < OTA_HTTPS_HEADER_MAX) {
        int count = ota_tls_read(&connection,
                                 (unsigned char *)header + header_size,
                                 OTA_HTTPS_HEADER_MAX - header_size,
                                 deadline_ms);
        if (count <= 0) {
            ota_error(error, error_size, "HTTPS response ended before headers");
            goto done;
        }
        header_size += (size_t)count;
        header[header_size] = '\0';
        header_end = strstr(header, "\r\n\r\n");
        if (header_end != NULL)
            break;
    }
    if (header_end == NULL) {
        ota_error(error, error_size, "HTTPS response headers are too large");
        goto done;
    }

    {
        size_t header_bytes = (size_t)(header_end + 4 - header);
        size_t payload_size = header_size - header_bytes;
        /* Keep the last header's CRLF visible to the parser, and terminate at
         * the empty line's CR.  Truncating at *header_end would silently skip
         * the final header field. */
        header_end[2] = '\0';
        if (ota_parse_http_header(header, request->maximum_body_size,
                                  request->expected_body_size, &content_length,
                                  error, error_size) != 0)
            goto done;
        if ((uint64_t)payload_size > content_length) {
            ota_error(error, error_size, "HTTPS response exceeds Content-Length");
            goto done;
        }
        if (payload_size > 0) {
            if (body_cb((const unsigned char *)header + header_bytes,
                        payload_size, body_context) != 0) {
                ota_error(error, error_size, "Cannot process HTTPS OTA body");
                goto done;
            }
            if (ota_deadline_expired(deadline_ms)) {
                ota_error(error, error_size, "HTTPS OTA operation timed out");
                goto done;
            }
            received = payload_size;
        }
    }

    if (received < content_length) {
        io_buffer = malloc(OTA_HTTPS_IO_CHUNK);
        if (io_buffer == NULL) {
            ota_error(error, error_size, "Cannot allocate HTTPS receive buffer");
            goto done;
        }
    }
    while (received < content_length) {
        uint64_t remaining = content_length - received;
        size_t wanted = remaining > OTA_HTTPS_IO_CHUNK
                            ? OTA_HTTPS_IO_CHUNK
                            : (size_t)remaining;
        int count = ota_tls_read(&connection, io_buffer, wanted, deadline_ms);
        if (count <= 0) {
            ota_error(error, error_size, "HTTPS OTA download was interrupted");
            goto done;
        }
        if (body_cb(io_buffer, (size_t)count, body_context) != 0) {
            ota_error(error, error_size, "Cannot process HTTPS OTA body");
            goto done;
        }
        if (ota_deadline_expired(deadline_ms)) {
            ota_error(error, error_size, "HTTPS OTA operation timed out");
            goto done;
        }
        received += (uint64_t)count;
    }

    if (received != content_length) {
        ota_error(error, error_size, "HTTPS OTA body length mismatch");
        goto done;
    }
    if (ota_deadline_expired(deadline_ms)) {
        ota_error(error, error_size, "HTTPS OTA operation timed out");
        goto done;
    }
    if (body_size != NULL)
        *body_size = received;
    result = 0;

done:
    free(io_buffer);
    ota_tls_free(&connection);
    return result;
}
