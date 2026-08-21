#pragma once

#include <stddef.h>
#include <stdint.h>

#define OTA_HTTPS_HOST_MAX 128
#define OTA_HTTPS_PATH_MAX 512
#define OTA_HTTPS_URL_MAX  704

typedef struct {
    char host[OTA_HTTPS_HOST_MAX];
    char path[OTA_HTTPS_PATH_MAX];
    uint16_t port;
} ota_https_url_t;

typedef int (*ota_https_body_cb_t)(const unsigned char *data, size_t size,
                                   void *context);

typedef struct {
    const char *accept;
    uint64_t maximum_body_size;
    uint64_t expected_body_size;
    unsigned total_timeout_seconds;
} ota_https_request_t;

int ota_https_parse_url(const char *url, ota_https_url_t *parsed);
int ota_https_same_origin(const char *first, const char *second);
int ota_https_derive_signature_url(const char *manifest_url, char *signature_url,
                                   size_t signature_url_size);
int ota_https_get(const char *url, const ota_https_request_t *request,
                  ota_https_body_cb_t body_cb, void *body_context,
                  uint64_t *body_size, char *error, size_t error_size);
