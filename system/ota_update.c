#define _GNU_SOURCE
#include "ota_update.h"
#include "ota_https.h"
#include "ota_manifest.h"
#include "power_control.h"
#include "sdk_version.h"

#include <errno.h>
#include <fcntl.h>
#include <k230_ota.h>
#include <mbedtls/sha256.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define OTA_URL_PATH "/data/dshanpi_ota_url.txt"
#define OTA_URL_TEMP "/data/dshanpi_ota_url.txt.tmp"
#define OTA_DOWNLOAD_TEMP "/data/update.download"
#define OTA_DEFAULT_URL                                                          \
    "https://dl.100ask.net/Hardware/MPU/DshanPIxCanMV/V3/OTA/latest.json"
#define OTA_ALLOWED_HOST "dl.100ask.net"
#define OTA_ALLOWED_MANIFEST_PATH                                                \
    "/Hardware/MPU/DshanPIxCanMV/V3/OTA/latest.json"
#define OTA_IO_CHUNK (64u * 1024u)
#define OTA_MAX_DOWNLOAD (640u * 1024u * 1024u)
#define OTA_MANIFEST_MAX (16u * 1024u)
#define OTA_SIGNATURE_MAX 80u
#define OTA_MANIFEST_RETRIES 3u

static const unsigned char g_kdimg_magic[] = { 0x93, 0x8f, 0xcb, 0x27 };

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} memory_sink_t;

typedef struct {
    int fd;
    uint64_t expected;
    uint64_t received;
    mbedtls_sha256_context sha256;
    unsigned char magic[sizeof(g_kdimg_magic)];
    size_t magic_size;
    int hash_failed;
    int write_failed;
} package_sink_t;

static pthread_mutex_t g_ota_lock = PTHREAD_MUTEX_INITIALIZER;
static dshanpi_ota_snapshot_t g_ota_snapshot = {
    DSHANPI_OTA_IDLE, 0, 0, "Ready"
};
static int g_ota_busy;
static uint64_t g_ui_heartbeat_sequence;

static void ota_set_state(dshanpi_ota_state_t state, unsigned progress,
                          const char *detail)
{
    pthread_mutex_lock(&g_ota_lock);
    g_ota_snapshot.state = state;
    g_ota_snapshot.progress = progress > 100 ? 100 : progress;
    snprintf(g_ota_snapshot.detail, sizeof(g_ota_snapshot.detail), "%s",
             detail != NULL ? detail : "");
    pthread_mutex_unlock(&g_ota_lock);
}

static void ota_set_failure(const char *prefix, const char *detail)
{
    char message[sizeof(g_ota_snapshot.detail)];

    if (detail == NULL || detail[0] == '\0')
        snprintf(message, sizeof(message), "%s", prefix);
    else
        snprintf(message, sizeof(message), "%s: %s", prefix, detail);
    ota_set_state(DSHANPI_OTA_FAILED, 0, message);
    printf("[ota] %s\n", message);
}

void dshanpi_ota_get_snapshot(dshanpi_ota_snapshot_t *snapshot)
{
    if (snapshot == NULL)
        return;
    pthread_mutex_lock(&g_ota_lock);
    *snapshot = g_ota_snapshot;
    snapshot->busy = g_ota_busy;
    pthread_mutex_unlock(&g_ota_lock);
}

static int manifest_url_allowed(const char *url)
{
    ota_https_url_t parsed;

    return ota_https_parse_url(url, &parsed) == 0 && parsed.port == 443 &&
           strcasecmp(parsed.host, OTA_ALLOWED_HOST) == 0 &&
           strcmp(parsed.path, OTA_ALLOWED_MANIFEST_PATH) == 0;
}

void dshanpi_ota_get_url(char *url, size_t size)
{
    char candidate[OTA_HTTPS_URL_MAX];
    FILE *file;

    if (url == NULL || size == 0)
        return;
    snprintf(url, size, "%s", OTA_DEFAULT_URL);
    file = fopen(OTA_URL_PATH, "r");
    if (file == NULL)
        return;
    candidate[0] = '\0';
    if (fgets(candidate, sizeof(candidate), file) != NULL) {
        candidate[strcspn(candidate, "\r\n")] = '\0';
        if (manifest_url_allowed(candidate))
            snprintf(url, size, "%s", candidate);
    }
    fclose(file);
}

static int write_all(int fd, const void *data, size_t size)
{
    const unsigned char *cursor = data;

    while (size > 0) {
        ssize_t written = write(fd, cursor, size);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return -1;
        cursor += written;
        size -= (size_t)written;
    }
    return 0;
}

static int read_all(int fd, void *data, size_t size)
{
    unsigned char *cursor = data;

    while (size > 0) {
        ssize_t count = read(fd, cursor, size);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        cursor += count;
        size -= (size_t)count;
    }
    return 0;
}

static void sync_data_directory(void)
{
    int fd = open("/data", O_RDONLY | O_DIRECTORY);

    if (fd >= 0) {
        (void)fsync(fd);
        close(fd);
    }
}

int dshanpi_ota_set_url(const char *url)
{
    int fd;
    int failed = 0;
    size_t length;

    if (!manifest_url_allowed(url))
        return -1;
    length = strlen(url);
    unlink(OTA_URL_TEMP);
    fd = open(OTA_URL_TEMP, O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -1;
    if (write_all(fd, url, length) != 0 || write_all(fd, "\n", 1) != 0 ||
        fsync(fd) != 0)
        failed = 1;
    if (close(fd) != 0)
        failed = 1;
    if (!failed && rename(OTA_URL_TEMP, OTA_URL_PATH) != 0)
        failed = 1;
    if (failed) {
        unlink(OTA_URL_TEMP);
        return -1;
    }
    sync_data_directory();
    return 0;
}

static int memory_sink_write(const unsigned char *data, size_t size,
                             void *context)
{
    memory_sink_t *sink = context;

    if (sink == NULL || data == NULL || size > sink->capacity - sink->size)
        return -1;
    memcpy(sink->data + sink->size, data, size);
    sink->size += size;
    return 0;
}

static int fetch_memory(const char *url, const char *accept, size_t maximum,
                        unsigned char **data, size_t *size,
                        char *error, size_t error_size)
{
    ota_https_request_t request;
    memory_sink_t sink;
    uint64_t body_size = 0;
    int result;

    if (data == NULL || size == NULL || maximum == 0)
        return -1;
    *data = NULL;
    *size = 0;
    sink.data = malloc(maximum + 1);
    if (sink.data == NULL) {
        snprintf(error, error_size, "%s", "Cannot allocate OTA metadata buffer");
        return -1;
    }
    sink.size = 0;
    sink.capacity = maximum;
    request.accept = accept;
    request.maximum_body_size = maximum;
    request.expected_body_size = 0;
    request.total_timeout_seconds = 120;
    result = ota_https_get(url, &request, memory_sink_write, &sink, &body_size,
                           error, error_size);
    if (result != 0 || body_size != sink.size) {
        free(sink.data);
        return -1;
    }
    sink.data[sink.size] = '\0';
    *data = sink.data;
    *size = sink.size;
    return 0;
}

static int fetch_signed_manifest(const char *manifest_url,
                                 ota_manifest_t *manifest,
                                 char *error, size_t error_size)
{
    char signature_url[OTA_HTTPS_URL_MAX];
    unsigned attempt;

    if (ota_https_derive_signature_url(manifest_url, signature_url,
                                       sizeof(signature_url)) != 0 ||
        !ota_https_same_origin(manifest_url, signature_url)) {
        snprintf(error, error_size, "%s", "Cannot derive manifest signature URL");
        return OTA_MANIFEST_INVALID;
    }

    for (attempt = 0; attempt < OTA_MANIFEST_RETRIES; ++attempt) {
        unsigned char *json = NULL;
        unsigned char *signature = NULL;
        size_t json_size = 0;
        size_t signature_size = 0;
        int manifest_result = OTA_MANIFEST_INVALID;

        ota_set_state(DSHANPI_OTA_CHECKING, 1 + attempt,
                      attempt == 0 ? "Fetching signed update manifest"
                                   : "Retrying signed update manifest");
        if (fetch_memory(manifest_url, "application/json", OTA_MANIFEST_MAX,
                         &json, &json_size, error, error_size) == 0) {
            ota_set_state(DSHANPI_OTA_CHECKING, 4 + attempt,
                          "Fetching manifest signature");
            if (fetch_memory(signature_url, "application/octet-stream",
                             OTA_SIGNATURE_MAX, &signature, &signature_size,
                             error, error_size) == 0) {
                ota_set_state(DSHANPI_OTA_VERIFYING_MANIFEST, 8,
                              "Verifying update publisher signature");
                manifest_result = ota_manifest_verify_and_parse(
                    json, json_size, signature, signature_size, SYSTEM_VERSION_,
                    manifest, error, error_size);
            }
        }
        free(signature);
        free(json);

        if (manifest_result == OTA_MANIFEST_VALID_UPDATE ||
            manifest_result == OTA_MANIFEST_NOT_NEWER)
            return manifest_result;
        if (attempt + 1 < OTA_MANIFEST_RETRIES)
            sleep(1u << attempt);
    }
    return OTA_MANIFEST_INVALID;
}

static int constant_time_equal(const unsigned char *first,
                               const unsigned char *second, size_t size)
{
    unsigned char difference = 0;
    size_t index;

    for (index = 0; index < size; ++index)
        difference |= first[index] ^ second[index];
    return difference == 0;
}

static int package_sink_write(const unsigned char *data, size_t size,
                              void *context)
{
    package_sink_t *sink = context;
    size_t offset = 0;

    if (sink == NULL || data == NULL || sink->received > sink->expected ||
        size > sink->expected - sink->received)
        return -1;
    while (sink->magic_size < sizeof(sink->magic) && offset < size)
        sink->magic[sink->magic_size++] = data[offset++];
    if (mbedtls_sha256_update(&sink->sha256, data, size) != 0) {
        sink->hash_failed = 1;
        return -1;
    }
    if (write_all(sink->fd, data, size) != 0) {
        sink->write_failed = 1;
        return -1;
    }
    sink->received += size;
    ota_set_state(DSHANPI_OTA_DOWNLOADING,
                  10u + (unsigned)(sink->received * 45u / sink->expected),
                  "Downloading signed update package");
    return 0;
}

static int download_verified_package(const ota_manifest_t *manifest,
                                     int *package_fd,
                                     char *error, size_t error_size)
{
    ota_https_request_t request;
    package_sink_t sink;
    struct stat info;
    uint64_t body_size = 0;
    unsigned char actual_sha256[32];
    int result = -1;

    if (package_fd == NULL)
        return -1;
    *package_fd = -1;
    memset(&sink, 0, sizeof(sink));
    sink.fd = -1;
    sink.expected = manifest->size;
    mbedtls_sha256_init(&sink.sha256);
    if (mbedtls_sha256_starts(&sink.sha256, 0) != 0) {
        snprintf(error, error_size, "%s", "Cannot initialize package SHA-256");
        goto done;
    }

    unlink(OTA_DOWNLOAD_TEMP);
    sink.fd = open(OTA_DOWNLOAD_TEMP,
                   O_CREAT | O_EXCL | O_RDWR | O_NOFOLLOW, 0600);
    if (sink.fd < 0 || fstat(sink.fd, &info) != 0 ||
        !S_ISREG(info.st_mode)) {
        snprintf(error, error_size, "%s", "Cannot create secure OTA temp file");
        goto done;
    }
    /* FatFS rejects unlink of an open file and locks conflicting opens. Keep
     * this exact O_RDWR descriptor open through verification and A/B install;
     * never close and re-open the pathname between those stages. */

    request.accept = "application/octet-stream";
    request.maximum_body_size = OTA_MAX_DOWNLOAD;
    request.expected_body_size = manifest->size;
    request.total_timeout_seconds = 1800;
    ota_set_state(DSHANPI_OTA_DOWNLOADING, 10,
                  "Connecting to signed update package");
    if (ota_https_get(manifest->artifact_url, &request, package_sink_write, &sink,
                      &body_size, error, error_size) != 0) {
        if (sink.hash_failed)
            snprintf(error, error_size, "%s", "Package SHA-256 calculation failed");
        else if (sink.write_failed)
            snprintf(error, error_size, "%s", "Cannot write OTA temp file");
        goto done;
    }
    ota_set_state(DSHANPI_OTA_VERIFYING_PACKAGE, 56,
                  "Verifying package size, magic and SHA-256");
    if (body_size != manifest->size || sink.received != manifest->size ||
        sink.magic_size != sizeof(g_kdimg_magic) ||
        memcmp(sink.magic, g_kdimg_magic, sizeof(g_kdimg_magic)) != 0) {
        snprintf(error, error_size, "%s", "Downloaded KDIMG structure is invalid");
        goto done;
    }
    if (mbedtls_sha256_finish(&sink.sha256, actual_sha256) != 0 ||
        !constant_time_equal(actual_sha256, manifest->sha256,
                             sizeof(actual_sha256))) {
        snprintf(error, error_size, "%s", "Downloaded package SHA-256 mismatch");
        goto done;
    }
    if (fsync(sink.fd) != 0) {
        snprintf(error, error_size, "%s", "Cannot commit verified OTA temp file");
        goto done;
    }
    if (lseek(sink.fd, 0, SEEK_SET) != 0) {
        snprintf(error, error_size, "%s", "Cannot rewind verified OTA package");
        goto done;
    }
    *package_fd = sink.fd;
    sink.fd = -1;
    ota_set_state(DSHANPI_OTA_VERIFYING_PACKAGE, 60,
                  "Signed update package verified");
    result = 0;

done:
    if (sink.fd >= 0)
        close(sink.fd);
    mbedtls_sha256_free(&sink.sha256);
    if (result != 0)
        unlink(OTA_DOWNLOAD_TEMP);
    return result;
}

static int install_package_fd(int fd, uint64_t expected_size,
                              unsigned progress_base)
{
    struct stat info;
    k230_ota_status_t before = { 0 };
    k230_ota_status_t after = { 0 };
    k230_ota_t *ota = NULL;
    unsigned char *buffer = NULL;
    unsigned char magic[sizeof(g_kdimg_magic)];
    off_t completed = 0;
    int result = -1;
    char target_slot;
    uint32_t expected_internal_version;

    if (fd < 0 || expected_size < OTA_IO_CHUNK ||
        expected_size > OTA_MAX_DOWNLOAD || fstat(fd, &info) != 0 ||
        !S_ISREG(info.st_mode) || (uint64_t)info.st_size != expected_size ||
        lseek(fd, 0, SEEK_SET) != 0 ||
        read_all(fd, magic, sizeof(magic)) != 0 ||
        memcmp(magic, g_kdimg_magic, sizeof(magic)) != 0 ||
        lseek(fd, 0, SEEK_SET) != 0) {
        ota_set_failure("Verified KDIMG descriptor is invalid", NULL);
        goto done;
    }
    if (k230_ota_get_status(&before) != 0 ||
        (before.active_slot != 'A' && before.active_slot != 'B')) {
        ota_set_failure("Cannot read A/B boot status", NULL);
        goto done;
    }
    target_slot = before.active_slot == 'A' ? 'B' : 'A';
    expected_internal_version = before.version_a > before.version_b
                                    ? before.version_a
                                    : before.version_b;
    if (expected_internal_version == UINT32_MAX) {
        ota_set_failure("A/B update version counter is exhausted", NULL);
        goto done;
    }
    ++expected_internal_version;
    buffer = malloc(OTA_IO_CHUNK);
    if (buffer == NULL) {
        ota_set_failure("Cannot allocate update buffer", NULL);
        goto done;
    }
    ota = k230_ota_create();
    if (ota == NULL) {
        ota_set_failure("Cannot start A/B update service", NULL);
        goto done;
    }
    ota_set_state(DSHANPI_OTA_INSTALLING, progress_base,
                  "Verifying and writing inactive slot");
    while (completed < info.st_size) {
        size_t wanted = (size_t)(info.st_size - completed);
        ssize_t count;
        unsigned progress;

        if (wanted > OTA_IO_CHUNK)
            wanted = OTA_IO_CHUNK;
        count = read(fd, buffer, wanted);
        if (count == 0)
            goto failed;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            goto failed;
        }
        if (k230_ota_update(ota, buffer, (size_t)count) != 0)
            goto failed;
        completed += count;
        progress = progress_base + (unsigned)(
            (uint64_t)completed * (100u - progress_base) /
            (uint64_t)info.st_size);
        ota_set_state(DSHANPI_OTA_INSTALLING,
                      progress >= 100 ? 99 : progress,
                      "Verifying and writing inactive slot");
    }
    {
        unsigned char extra;
        ssize_t count;
        do {
            count = read(fd, &extra, 1);
        } while (count < 0 && errno == EINTR);
        if (count != 0 || completed != info.st_size)
            goto failed;
    }

    k230_ota_destroy(ota);
    ota = NULL;
    if (k230_ota_get_status(&after) != 0 ||
        after.active_slot != before.active_slot ||
        (target_slot == 'A' && (!after.valid_a || !after.pending_a ||
                                after.attempts_a == 0 ||
                                after.version_a != expected_internal_version)) ||
        (target_slot == 'B' && (!after.valid_b || !after.pending_b ||
                                after.attempts_b == 0 ||
                                after.version_b != expected_internal_version))) {
        ota_set_failure("Inactive slot verification did not complete", NULL);
        goto done;
    }
    ota_set_state(DSHANPI_OTA_READY, 100,
                  "Inactive slot verified; preparing restart");
    printf("[ota] slot %c verified and pending\n", target_slot);
    result = 0;
    goto done;

failed:
    ota_set_failure("Update failed; active slot was not changed", NULL);
done:
    if (ota != NULL)
        k230_ota_destroy(ota);
    free(buffer);
    return result;
}

static int run_network_update(int install)
{
    char manifest_url[OTA_HTTPS_URL_MAX];
    char error[160] = { 0 };
    ota_manifest_t manifest;
    int package_fd = -1;
    int manifest_result;
    int result = -1;

    memset(&manifest, 0, sizeof(manifest));
    dshanpi_ota_get_url(manifest_url, sizeof(manifest_url));
    manifest_result = fetch_signed_manifest(manifest_url, &manifest, error,
                                            sizeof(error));
    if (manifest_result == OTA_MANIFEST_INVALID) {
        ota_set_failure("Signed manifest check failed", error);
        return -1;
    }

    printf("[ota] signed manifest verified: current=%s available=%s key=%s\n",
           SYSTEM_VERSION_, manifest.version, "dshanpi-ota-prod-2026-01");
    if (manifest_result == OTA_MANIFEST_NOT_NEWER) {
        ota_set_state(DSHANPI_OTA_UP_TO_DATE, 100,
                      "System is already up to date");
        return 0;
    }
    if (!install) {
        ota_set_state(DSHANPI_OTA_AVAILABLE, 100,
                      "A newer signed update is available");
        return 0;
    }

    if (download_verified_package(&manifest, &package_fd, error,
                                  sizeof(error)) != 0) {
        ota_set_failure("Signed package download failed", error);
        return -1;
    }
    result = install_package_fd(package_fd, manifest.size, 60);
    close(package_fd);
    if (unlink(OTA_DOWNLOAD_TEMP) != 0)
        printf("[ota] warning: cannot remove verified temp package: %s\n",
               strerror(errno));
    sync_data_directory();
    if (result == 0) {
        ota_set_state(DSHANPI_OTA_REBOOTING, 100,
                      "Update installed; restarting automatically");
        printf("[ota] restarting into verified pending slot in 3 seconds\n");
        sleep(3);
        if (dshanpi_power_reboot() != 0) {
            printf("[ota] automatic restart failed; manual restart required\n");
            ota_set_state(DSHANPI_OTA_READY, 100,
                          "Update ready; restart to try the new slot");
        }
    }
    return result;
}

static void *ota_worker(void *argument)
{
    (void)argument;
    (void)run_network_update(1);

    pthread_mutex_lock(&g_ota_lock);
    g_ota_busy = 0;
    pthread_mutex_unlock(&g_ota_lock);
    return NULL;
}

static int ota_start(void)
{
    pthread_t thread;

    pthread_mutex_lock(&g_ota_lock);
    if (g_ota_busy) {
        pthread_mutex_unlock(&g_ota_lock);
        return -1;
    }
    g_ota_busy = 1;
    g_ota_snapshot.state = DSHANPI_OTA_CHECKING;
    g_ota_snapshot.progress = 0;
    snprintf(g_ota_snapshot.detail, sizeof(g_ota_snapshot.detail), "%s",
             "Checking signed update manifest");
    pthread_mutex_unlock(&g_ota_lock);
    if (pthread_create(&thread, NULL, ota_worker, NULL) != 0) {
        pthread_mutex_lock(&g_ota_lock);
        g_ota_busy = 0;
        g_ota_snapshot.state = DSHANPI_OTA_FAILED;
        g_ota_snapshot.progress = 0;
        snprintf(g_ota_snapshot.detail, sizeof(g_ota_snapshot.detail), "%s",
                 "Cannot create update worker");
        pthread_mutex_unlock(&g_ota_lock);
        return -2;
    }
    pthread_detach(thread);
    return 0;
}

int dshanpi_ota_start_network(void)
{
    return ota_start();
}

int dshanpi_ota_check_network(void)
{
    int result;

    pthread_mutex_lock(&g_ota_lock);
    if (g_ota_busy) {
        pthread_mutex_unlock(&g_ota_lock);
        return -1;
    }
    g_ota_busy = 1;
    pthread_mutex_unlock(&g_ota_lock);
    result = run_network_update(0);
    pthread_mutex_lock(&g_ota_lock);
    g_ota_busy = 0;
    pthread_mutex_unlock(&g_ota_lock);
    return result;
}

int dshanpi_ota_boot_is_pending(void)
{
    k230_ota_status_t status = { 0 };

    if (k230_ota_get_status(&status) != 0)
        return 0;
    return (status.active_slot == 'A' && status.valid_a && status.pending_a) ||
           (status.active_slot == 'B' && status.valid_b && status.pending_b);
}

void dshanpi_ota_report_ui_heartbeat(void)
{
    pthread_mutex_lock(&g_ota_lock);
    ++g_ui_heartbeat_sequence;
    pthread_mutex_unlock(&g_ota_lock);
}

static int ota_data_health_check(char active_slot, uint64_t sequence)
{
    char path[80];
    char expected[48];
    char actual[48];
    int fd = -1;
    int result = -1;
    int created = 0;
    int expected_size;

    if (snprintf(path, sizeof(path), "/data/.ota-health-%ld.tmp",
                 (long)getpid()) <= 0)
        return -1;
    expected_size = snprintf(expected, sizeof(expected), "slot=%c heartbeat=%llu",
                             active_slot, (unsigned long long)sequence);
    if (expected_size <= 0 || (size_t)expected_size >= sizeof(expected))
        return -1;
    fd = open(path, O_CREAT | O_EXCL | O_RDWR | O_NOFOLLOW, 0600);
    if (fd < 0)
        goto done;
    created = 1;
    if (write_all(fd, expected, (size_t)expected_size) != 0 || fsync(fd) != 0 ||
        lseek(fd, 0, SEEK_SET) != 0 ||
        read_all(fd, actual, (size_t)expected_size) != 0 ||
        memcmp(actual, expected, (size_t)expected_size) != 0)
        goto done;
    {
        unsigned char extra;
        ssize_t count;

        do {
            count = read(fd, &extra, 1);
        } while (count < 0 && errno == EINTR);
        if (count != 0)
            goto done;
    }
    result = 0;

done:
    if (fd >= 0)
        close(fd);
    if (created && unlink(path) == 0)
        sync_data_directory();
    return result;
}

static void *ota_health_worker(void *argument)
{
    k230_ota_status_t before = { 0 };
    k230_ota_status_t after = { 0 };
    char active_slot;
    uint64_t heartbeat;
    unsigned check;

    (void)argument;
    if (k230_ota_get_status(&before) != 0 ||
        !((before.active_slot == 'A' && before.valid_a && before.pending_a) ||
          (before.active_slot == 'B' && before.valid_b && before.pending_b)))
        return NULL;
    active_slot = before.active_slot;
    pthread_mutex_lock(&g_ota_lock);
    heartbeat = g_ui_heartbeat_sequence;
    pthread_mutex_unlock(&g_ota_lock);
    for (check = 0; check < 4; ++check) {
        uint64_t current;

        sleep(2);
        pthread_mutex_lock(&g_ota_lock);
        current = g_ui_heartbeat_sequence;
        pthread_mutex_unlock(&g_ota_lock);
        if (current <= heartbeat) {
            printf("[ota] UI heartbeat stalled; pending slot remains unconfirmed\n");
            return NULL;
        }
        heartbeat = current;
    }
    if (access("/data", R_OK | W_OK) != 0 ||
        access("/sdcard/app/dshanpi_aimodel", R_OK | X_OK) != 0 ||
        ota_data_health_check(active_slot, heartbeat) != 0 ||
        k230_ota_get_status(&after) != 0 || after.active_slot != active_slot ||
        !((active_slot == 'A' && after.valid_a && after.pending_a) ||
          (active_slot == 'B' && after.valid_b && after.pending_b))) {
        printf("[ota] boot health gate failed; pending slot remains unconfirmed\n");
        return NULL;
    }
    if (k230_ota_confirm_boot() == 0)
        printf("[ota] UI-ready slot %c confirmed healthy\n", active_slot);
    else
        printf("[ota] unable to confirm pending slot %c\n", active_slot);
    return NULL;
}

void dshanpi_ota_confirm_boot_after_health_delay(void)
{
    pthread_t thread;

    if (!dshanpi_ota_boot_is_pending())
        return;
    if (pthread_create(&thread, NULL, ota_health_worker, NULL) == 0)
        pthread_detach(thread);
}
