#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *id;
    const char *name;
    const char *category;
    const char *launch_script;
    uint32_t color;
} dshanpi_ai_app_t;

typedef struct {
    const char *id;
    const char *name;
    const char *description;
    const char *symbol;
    uint32_t color;
    const char *const *app_ids;
    size_t app_count;
} dshanpi_ai_scene_t;

const dshanpi_ai_app_t *dshanpi_ai_apps(size_t *count);
const dshanpi_ai_app_t *dshanpi_ai_app_find(const char *id);
const dshanpi_ai_scene_t *dshanpi_ai_scenes(size_t *count);
