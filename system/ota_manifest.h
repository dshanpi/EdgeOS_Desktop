#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_MANIFEST_VERSION_MAX       24
#define OTA_MANIFEST_FILENAME_MAX      128
#define OTA_MANIFEST_ARTIFACT_URL_MAX  704
#define OTA_MANIFEST_RELEASE_NOTES_MAX 256
#define DSHANPI_OTA_PRODUCT_NAME        "DshanPI_EdgeOS_Desktop"
#define DSHANPI_OTA_PACKAGE_MAX_SIZE    (1280ULL * 1024ULL * 1024ULL)

typedef struct {
    char version[OTA_MANIFEST_VERSION_MAX];
    char filename[OTA_MANIFEST_FILENAME_MAX];
    char artifact_url[OTA_MANIFEST_ARTIFACT_URL_MAX];
    char release_notes[OTA_MANIFEST_RELEASE_NOTES_MAX];
    uint64_t size;
    unsigned char sha256[32];
    bool mandatory;
} ota_manifest_t;

enum {
    OTA_MANIFEST_VALID_UPDATE = 0,
    OTA_MANIFEST_NOT_NEWER = 1,
    OTA_MANIFEST_INVALID = -1,
};

int ota_manifest_verify_and_parse(const unsigned char *json, size_t json_size,
                                  const unsigned char *signature,
                                  size_t signature_size,
                                  const char *current_version,
                                  ota_manifest_t *manifest,
                                  char *error, size_t error_size);
