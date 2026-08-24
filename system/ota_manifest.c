#include "ota_manifest.h"
#include "ota_https.h"
#include "ota_trust_store.h"

#include <cJSON.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define OTA_BOARD_NAME "k230_canmv_dongshanpi"
#define OTA_CHANNEL_NAME "stable"
#define OTA_DOWNLOAD_HOST "dl.100ask.net"
#define OTA_DOWNLOAD_PREFIX "/Hardware/MPU/DshanPIxCanMV/V3/OTA/"
#define OTA_PACKAGE_MIN_SIZE (64u * 1024u)

typedef struct {
    uint16_t part[4];
} ota_version_t;

static int json_contains_decoded_nul(const unsigned char *json, size_t size)
{
    size_t index;

    for (index = 0; index + 5 < size; ++index) {
        size_t preceding = 0;
        size_t cursor;

        if (json[index] != '\\' || json[index + 1] != 'u' ||
            json[index + 2] != '0' || json[index + 3] != '0' ||
            json[index + 4] != '0' || json[index + 5] != '0')
            continue;
        cursor = index;
        while (cursor > 0 && json[cursor - 1] == '\\') {
            ++preceding;
            --cursor;
        }
        /* An even number of preceding slashes leaves this slash active; cJSON
         * would decode it to an embedded NUL and strlen/strcmp could then
         * mistake a longer key or value for an exact match. */
        if ((preceding & 1u) == 0)
            return 1;
    }
    return 0;
}

static void manifest_error(char *error, size_t size, const char *format, ...)
{
    va_list arguments;

    if (error == NULL || size == 0)
        return;
    va_start(arguments, format);
    vsnprintf(error, size, format, arguments);
    va_end(arguments);
    error[size - 1] = '\0';
}

static int utf8_valid(const char *text)
{
    const unsigned char *bytes = (const unsigned char *)text;
    size_t length = strlen(text);
    size_t index = 0;

    while (index < length) {
        const unsigned char *cursor = bytes + index;
        size_t remaining = length - index;

        if (*cursor <= 0x7f) {
            ++index;
        } else if (*cursor >= 0xc2 && *cursor <= 0xdf) {
            if (remaining < 2 || cursor[1] < 0x80 || cursor[1] > 0xbf)
                return 0;
            index += 2;
        } else if (*cursor == 0xe0) {
            if (remaining < 3 || cursor[1] < 0xa0 || cursor[1] > 0xbf ||
                cursor[2] < 0x80 || cursor[2] > 0xbf)
                return 0;
            index += 3;
        } else if ((*cursor >= 0xe1 && *cursor <= 0xec) ||
                   (*cursor >= 0xee && *cursor <= 0xef)) {
            if (remaining < 3 || cursor[1] < 0x80 || cursor[1] > 0xbf ||
                cursor[2] < 0x80 || cursor[2] > 0xbf)
                return 0;
            index += 3;
        } else if (*cursor == 0xed) {
            if (remaining < 3 || cursor[1] < 0x80 || cursor[1] > 0x9f ||
                cursor[2] < 0x80 || cursor[2] > 0xbf)
                return 0;
            index += 3;
        } else if (*cursor == 0xf0) {
            if (remaining < 4 || cursor[1] < 0x90 || cursor[1] > 0xbf ||
                cursor[2] < 0x80 || cursor[2] > 0xbf ||
                cursor[3] < 0x80 || cursor[3] > 0xbf)
                return 0;
            index += 4;
        } else if (*cursor >= 0xf1 && *cursor <= 0xf3) {
            if (remaining < 4 || cursor[1] < 0x80 || cursor[1] > 0xbf ||
                cursor[2] < 0x80 || cursor[2] > 0xbf ||
                cursor[3] < 0x80 || cursor[3] > 0xbf)
                return 0;
            index += 4;
        } else if (*cursor == 0xf4) {
            if (remaining < 4 || cursor[1] < 0x80 || cursor[1] > 0x8f ||
                cursor[2] < 0x80 || cursor[2] > 0xbf ||
                cursor[3] < 0x80 || cursor[3] > 0xbf)
                return 0;
            index += 4;
        } else {
            return 0;
        }
    }
    return 1;
}

static int object_has_exact_members(const cJSON *object,
                                    const char *const *members,
                                    size_t member_count)
{
    const cJSON *item;
    size_t actual_count = 0;

    if (!cJSON_IsObject(object))
        return 0;
    for (item = object->child; item != NULL; item = item->next) {
        const cJSON *other;
        size_t index;
        int known = 0;

        if (item->string == NULL)
            return 0;
        for (other = item->next; other != NULL; other = other->next) {
            if (other->string != NULL && strcmp(item->string, other->string) == 0)
                return 0;
        }
        for (index = 0; index < member_count; ++index) {
            if (strcmp(item->string, members[index]) == 0) {
                known = 1;
                break;
            }
        }
        if (!known)
            return 0;
        ++actual_count;
    }
    return actual_count == member_count;
}

static const cJSON *required_item(const cJSON *object, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

static const char *required_string(const cJSON *object, const char *name,
                                   size_t maximum_size)
{
    const cJSON *item = required_item(object, name);

    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        strlen(item->valuestring) >= maximum_size ||
        !utf8_valid(item->valuestring))
        return NULL;
    return item->valuestring;
}

static int parse_version(const char *text, ota_version_t *version)
{
    const char *cursor;
    unsigned index;

    if (text == NULL || version == NULL || text[0] != 'v')
        return -1;
    cursor = text + 1;
    for (index = 0; index < 3; ++index) {
        unsigned value = 0;
        unsigned digits = 0;

        if (!isdigit((unsigned char)*cursor))
            return -1;
        if (*cursor == '0' && isdigit((unsigned char)cursor[1]))
            return -1;
        while (isdigit((unsigned char)*cursor)) {
            value = value * 10u + (unsigned)(*cursor - '0');
            if (value > UINT16_MAX)
                return -1;
            ++digits;
            ++cursor;
        }
        if (digits == 0)
            return -1;
        version->part[index] = (uint16_t)value;
        if (index < 2) {
            if (*cursor != '.')
                return -1;
            ++cursor;
        }
    }
    version->part[3] = 0;
    return *cursor == '\0' ? 0 : -1;
}

static int compare_version(const ota_version_t *first,
                           const ota_version_t *second)
{
    unsigned index;

    for (index = 0; index < 4; ++index) {
        if (first->part[index] < second->part[index])
            return -1;
        if (first->part[index] > second->part[index])
            return 1;
    }
    return 0;
}

static int timestamp_valid(const char *timestamp)
{
    static const unsigned separators[] = { 4, 7, 10, 13, 16, 19 };
    static const char values[] = { '-', '-', 'T', ':', ':', 'Z' };
    unsigned index;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;

    if (timestamp == NULL || strlen(timestamp) != 20)
        return 0;
    for (index = 0; index < 6; ++index) {
        if (timestamp[separators[index]] != values[index])
            return 0;
    }
    for (index = 0; index < 20; ++index) {
        if (index == 4 || index == 7 || index == 10 || index == 13 ||
            index == 16 || index == 19)
            continue;
        if (!isdigit((unsigned char)timestamp[index]))
            return 0;
    }
    if (sscanf(timestamp, "%4d-%2d-%2dT%2d:%2d:%2dZ", &year, &month, &day,
               &hour, &minute, &second) != 6)
        return 0;
    if (year < 2025 || year > 2099 || month < 1 || month > 12 || day < 1 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
        second > 60)
        return 0;
    {
        static const unsigned char days_per_month[] = {
            31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
        };
        int maximum_day = days_per_month[month - 1];

        if (month == 2 &&
            ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
            maximum_day = 29;
        return day <= maximum_day;
    }
}

static int parse_sha256(const char *text, unsigned char digest[32])
{
    size_t index;

    if (text == NULL || strlen(text) != 64)
        return -1;
    for (index = 0; index < 32; ++index) {
        unsigned high;
        unsigned low;
        char first = text[index * 2];
        char second = text[index * 2 + 1];

        if (!((first >= '0' && first <= '9') ||
              (first >= 'a' && first <= 'f')) ||
            !((second >= '0' && second <= '9') ||
              (second >= 'a' && second <= 'f')))
            return -1;
        high = first <= '9' ? (unsigned)(first - '0')
                            : (unsigned)(first - 'a' + 10);
        low = second <= '9' ? (unsigned)(second - '0')
                            : (unsigned)(second - 'a' + 10);
        digest[index] = (unsigned char)((high << 4) | low);
    }
    return 0;
}

static int verify_signature(const unsigned char *json, size_t json_size,
                            const unsigned char *signature,
                            size_t signature_size, char *error,
                            size_t error_size)
{
    unsigned char digest[32];
    mbedtls_pk_context key;
    int result = -1;

    if (signature_size < 8 || signature_size > 80) {
        manifest_error(error, error_size, "Manifest signature size is invalid");
        return -1;
    }
    if (mbedtls_sha256(json, json_size, digest, 0) != 0) {
        manifest_error(error, error_size, "Cannot hash OTA manifest");
        return -1;
    }
    mbedtls_pk_init(&key);
    if (mbedtls_pk_parse_public_key(&key, dshanpi_ota_manifest_key_2026_01,
                                    dshanpi_ota_manifest_key_2026_01_size) != 0 ||
        !mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA) ||
        mbedtls_pk_get_bitlen(&key) != 256) {
        manifest_error(error, error_size, "Embedded OTA public key is invalid");
        goto done;
    }
    if (mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                          signature, signature_size) != 0) {
        manifest_error(error, error_size, "OTA manifest signature is invalid");
        goto done;
    }
    result = 0;

done:
    mbedtls_pk_free(&key);
    return result;
}

int ota_manifest_verify_and_parse(const unsigned char *json, size_t json_size,
                                  const unsigned char *signature,
                                  size_t signature_size,
                                  const char *current_version,
                                  ota_manifest_t *manifest,
                                  char *error, size_t error_size)
{
    static const char *const root_members[] = {
        "schema", "product", "board", "channel", "version",
        "published_at", "release_notes", "mandatory", "artifact", "signature"
    };
    static const char *const artifact_members[] = {
        "filename", "url", "compression", "size", "sha256"
    };
    static const char *const signature_members[] = {
        "algorithm", "format", "key_id", "url"
    };
    unsigned char *json_copy = NULL;
    const char *parse_end = NULL;
    cJSON *root = NULL;
    const cJSON *artifact;
    const cJSON *signature_object;
    const cJSON *schema;
    const cJSON *size_item;
    const cJSON *mandatory;
    const char *product;
    const char *board;
    const char *channel;
    const char *version_text;
    const char *published_at;
    const char *release_notes;
    const char *filename;
    const char *artifact_url;
    const char *compression;
    const char *sha256_text;
    const char *algorithm;
    const char *format;
    const char *key_id;
    const char *signature_url;
    ota_https_url_t parsed_artifact_url;
    ota_version_t available_version;
    ota_version_t installed_version;
    char expected_filename[OTA_MANIFEST_FILENAME_MAX];
    char expected_path[OTA_HTTPS_PATH_MAX];
    double size_value;
    uint64_t parsed_size;
    unsigned char parsed_sha256[32];
    int result = OTA_MANIFEST_INVALID;

    if (error != NULL && error_size > 0)
        error[0] = '\0';
    if (json == NULL || json_size == 0 || signature == NULL ||
        current_version == NULL || manifest == NULL ||
        memchr(json, '\0', json_size) != NULL ||
        json_contains_decoded_nul(json, json_size)) {
        manifest_error(error, error_size, "OTA manifest input is invalid");
        return OTA_MANIFEST_INVALID;
    }
    if (verify_signature(json, json_size, signature, signature_size, error,
                         error_size) != 0)
        return OTA_MANIFEST_INVALID;

    json_copy = malloc(json_size + 1);
    if (json_copy == NULL) {
        manifest_error(error, error_size, "Cannot allocate OTA manifest parser");
        return OTA_MANIFEST_INVALID;
    }
    memcpy(json_copy, json, json_size);
    json_copy[json_size] = '\0';
    root = cJSON_ParseWithLengthOpts((const char *)json_copy, json_size + 1,
                                     &parse_end, 1);
    if (root == NULL || parse_end != (const char *)json_copy + json_size ||
        !object_has_exact_members(root, root_members,
                                  sizeof(root_members) / sizeof(root_members[0]))) {
        manifest_error(error, error_size, "OTA manifest JSON/schema is invalid");
        goto done;
    }

    schema = required_item(root, "schema");
    product = required_string(root, "product", 64);
    board = required_string(root, "board", 64);
    channel = required_string(root, "channel", 32);
    version_text = required_string(root, "version", OTA_MANIFEST_VERSION_MAX);
    published_at = required_string(root, "published_at", 32);
    release_notes = required_string(root, "release_notes",
                                    OTA_MANIFEST_RELEASE_NOTES_MAX);
    mandatory = required_item(root, "mandatory");
    artifact = required_item(root, "artifact");
    signature_object = required_item(root, "signature");

    if (!cJSON_IsNumber(schema) || schema->valuedouble != 1.0 ||
        product == NULL || strcmp(product, DSHANPI_OTA_PRODUCT_NAME) != 0 ||
        board == NULL || strcmp(board, OTA_BOARD_NAME) != 0 ||
        channel == NULL || strcmp(channel, OTA_CHANNEL_NAME) != 0 ||
        version_text == NULL || published_at == NULL ||
        !timestamp_valid(published_at) || release_notes == NULL ||
        !cJSON_IsBool(mandatory) ||
        !object_has_exact_members(artifact, artifact_members,
                                  sizeof(artifact_members) /
                                      sizeof(artifact_members[0])) ||
        !object_has_exact_members(signature_object, signature_members,
                                  sizeof(signature_members) /
                                      sizeof(signature_members[0]))) {
        manifest_error(error, error_size, "OTA manifest fields are invalid");
        goto done;
    }

    filename = required_string(artifact, "filename",
                               OTA_MANIFEST_FILENAME_MAX);
    artifact_url = required_string(artifact, "url",
                                   OTA_MANIFEST_ARTIFACT_URL_MAX);
    compression = required_string(artifact, "compression", 16);
    size_item = required_item(artifact, "size");
    sha256_text = required_string(artifact, "sha256", 65);
    algorithm = required_string(signature_object, "algorithm", 32);
    format = required_string(signature_object, "format", 16);
    key_id = required_string(signature_object, "key_id", 64);
    signature_url = required_string(signature_object, "url", 64);
    if (filename == NULL || artifact_url == NULL || compression == NULL ||
        strcmp(compression, "none") != 0 || !cJSON_IsNumber(size_item) ||
        sha256_text == NULL || algorithm == NULL ||
        strcmp(algorithm, "ecdsa-p256-sha256") != 0 || format == NULL ||
        strcmp(format, "asn1-der") != 0 || key_id == NULL ||
        strcmp(key_id, dshanpi_ota_manifest_key_id) != 0 ||
        signature_url == NULL || strcmp(signature_url, "latest.json.sig") != 0) {
        manifest_error(error, error_size, "OTA artifact/signature metadata is invalid");
        goto done;
    }

    if (parse_version(version_text, &available_version) != 0 ||
        parse_version(current_version, &installed_version) != 0) {
        manifest_error(error, error_size, "OTA version format is invalid");
        goto done;
    }
    if (snprintf(expected_filename, sizeof(expected_filename), "%s_%s_ota.kdimg",
                 DSHANPI_OTA_PRODUCT_NAME, version_text) <= 0 ||
        strcmp(filename, expected_filename) != 0) {
        manifest_error(error, error_size, "OTA filename does not match version");
        goto done;
    }
    if (ota_https_parse_url(artifact_url, &parsed_artifact_url) != 0 ||
        parsed_artifact_url.port != 443 ||
        strcasecmp(parsed_artifact_url.host, OTA_DOWNLOAD_HOST) != 0 ||
        snprintf(expected_path, sizeof(expected_path), "%s%s",
                 OTA_DOWNLOAD_PREFIX, expected_filename) <= 0 ||
        strcmp(parsed_artifact_url.path, expected_path) != 0) {
        manifest_error(error, error_size, "OTA artifact URL is not allowed");
        goto done;
    }

    size_value = size_item->valuedouble;
    if (size_value < (double)OTA_PACKAGE_MIN_SIZE ||
        size_value > (double)DSHANPI_OTA_PACKAGE_MAX_SIZE) {
        manifest_error(error, error_size, "OTA artifact size is out of range");
        goto done;
    }
    parsed_size = (uint64_t)size_value;
    if ((double)parsed_size != size_value ||
        parse_sha256(sha256_text, parsed_sha256) != 0) {
        manifest_error(error, error_size, "OTA size or SHA-256 is invalid");
        goto done;
    }

    memset(manifest, 0, sizeof(*manifest));
    snprintf(manifest->version, sizeof(manifest->version), "%s", version_text);
    snprintf(manifest->filename, sizeof(manifest->filename), "%s", filename);
    snprintf(manifest->artifact_url, sizeof(manifest->artifact_url), "%s",
             artifact_url);
    snprintf(manifest->release_notes, sizeof(manifest->release_notes), "%s",
             release_notes);
    manifest->size = parsed_size;
    memcpy(manifest->sha256, parsed_sha256, sizeof(manifest->sha256));
    manifest->mandatory = cJSON_IsTrue(mandatory);

    if (compare_version(&available_version, &installed_version) <= 0) {
        result = OTA_MANIFEST_NOT_NEWER;
    } else {
        result = OTA_MANIFEST_VALID_UPDATE;
    }

done:
    cJSON_Delete(root);
    free(json_copy);
    return result;
}
