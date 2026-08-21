#ifndef DSHANPI_MODE_PERSISTENCE_H
#define DSHANPI_MODE_PERSISTENCE_H

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace dshanpi_mode_state {

inline bool make_path(const char *app_id, char *path, size_t path_size)
{
    if (app_id == nullptr || app_id[0] == '\0') {
        return false;
    }
    for (const char *p = app_id; *p != '\0'; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
              *p == '_')) {
            return false;
        }
    }
    const int written = snprintf(path, path_size,
                                 "/data/dshanpi_ai_mode_%s.conf", app_id);
    return written > 0 && static_cast<size_t>(written) < path_size;
}

inline int load(const char *app_id, int fallback, int mode_count)
{
    char path[128];
    int value = fallback;
    if (mode_count <= 0 || fallback < 0 || fallback >= mode_count ||
        !make_path(app_id, path, sizeof(path))) {
        return fallback;
    }

    FILE *file = fopen(path, "r");
    if (file == nullptr) {
        printf("[mode-state] %s using default mode %d\n", app_id, fallback);
        return fallback;
    }
    if (fscanf(file, "mode=%d", &value) != 1 ||
        value < 0 || value >= mode_count) {
        printf("[mode-state] %s ignored invalid saved mode\n", app_id);
        value = fallback;
    }
    fclose(file);
    printf("[mode-state] %s restored mode %d\n", app_id, value);
    return value;
}

inline int save(const char *app_id, int value, int mode_count)
{
    char path[128];
    char temporary[136];
    if (value < 0 || value >= mode_count ||
        !make_path(app_id, path, sizeof(path))) {
        return -1;
    }
    const int written = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(temporary)) {
        return -1;
    }

    FILE *file = fopen(temporary, "w");
    if (file == nullptr) {
        printf("[mode-state] %s open failed: %s\n", app_id,
               strerror(errno));
        return -1;
    }
    int failed = fprintf(file, "mode=%d\n", value) < 0;
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) {
        failed = 1;
    }
    if (fclose(file) != 0) {
        failed = 1;
    }
    if (failed) {
        unlink(temporary);
        return -1;
    }

    /* RT-Smart /data rename does not replace an existing target. */
    if (unlink(path) != 0 && errno != ENOENT) {
        printf("[mode-state] %s remove old state failed: %s\n", app_id,
               strerror(errno));
        unlink(temporary);
        return -1;
    }
    if (rename(temporary, path) != 0) {
        printf("[mode-state] %s commit failed: %s\n", app_id,
               strerror(errno));
        unlink(temporary);
        return -1;
    }
    printf("[mode-state] %s saved mode %d\n", app_id, value);
    return 0;
}

}  // namespace dshanpi_mode_state

#endif
