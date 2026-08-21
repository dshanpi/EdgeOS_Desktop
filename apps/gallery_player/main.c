#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "kplayer.h"
#include "kd_display.h"

static sem_t g_finished;
static volatile sig_atomic_t g_interrupted;
static uint64_t g_last_pts_ms;
static uint64_t g_clock_pts_ms;
static uint64_t g_clock_wall_us;
static uint64_t g_last_wake_us;
static uint64_t g_decode_cost_us = 3000;

static uint64_t monotonic_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000ULL +
           (uint64_t)now.tv_nsec / 1000ULL;
}

static void reset_playback_clock(void)
{
    g_last_pts_ms = 0;
    g_clock_pts_ms = 0;
    g_clock_wall_us = 0;
    g_last_wake_us = 0;
    g_decode_cost_us = 3000;
}

static void signal_handler(int signal_number)
{
    (void)signal_number;
    g_interrupted = 1;
    sem_post(&g_finished);
}

static k_s32 player_event(K_PLAYER_EVENT_E event, void *data)
{
    if (event == K_PLAYER_EVENT_PROGRESS && data != NULL) {
        K_PLAYER_PROGRESS_INFO *progress = data;
        uint64_t pts = progress->cur_time;
        uint64_t now_us = monotonic_us();
        /*
         * Pace to the file PTS without adding demux/decode time to every
         * frame interval.
         */
        if (g_last_pts_ms != 0 && pts > g_last_pts_ms) {
            uint64_t delta = pts - g_last_pts_ms;
            if (delta >= 2 && delta <= 1000) {
                if (g_last_wake_us != 0 && now_us > g_last_wake_us) {
                    uint64_t cost = now_us - g_last_wake_us;
                    if (cost <= 100000)
                        g_decode_cost_us = (g_decode_cost_us * 3 + cost) / 4;
                }
                if (g_clock_wall_us == 0) {
                    g_clock_wall_us = now_us;
                    g_clock_pts_ms = pts;
                }
                uint64_t target_us = g_clock_wall_us +
                    (pts + delta - g_clock_pts_ms) * 1000ULL;
                uint64_t wake_us = target_us > g_decode_cost_us
                                       ? target_us - g_decode_cost_us
                                       : target_us;
                if (now_us > target_us + 250000ULL) {
                    g_clock_wall_us = now_us;
                    g_clock_pts_ms = pts;
                    target_us = now_us + delta * 1000ULL;
                    wake_us = target_us > g_decode_cost_us
                                  ? target_us - g_decode_cost_us
                                  : target_us;
                }
                if (wake_us > now_us)
                    usleep((useconds_t)(wake_us - now_us));
            } else {
                usleep(17000);
                g_clock_wall_us = 0;
            }
        } else {
            usleep(17000);
            g_clock_wall_us = 0;
        }
        g_last_wake_us = monotonic_us();
        g_last_pts_ms = pts;
    } else if (event == K_PLAYER_EVENT_EOF) {
        sem_post(&g_finished);
    }
    return K_SUCCESS;
}

int main(int argc, char **argv)
{
    int result = 0;
    int repeat_test;

    if (argc < 2) {
        printf("[gallery-player] missing MP4 path\n");
        return 1;
    }
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    sem_init(&g_finished, 0, 0);
    repeat_test = argc >= 3 && strcmp(argv[2], "--repeat-test") == 0;

    /*
     * kplayer's vo_init() is a no-op in this SDK.  Initialize VB through the
     * player first, then select and initialize the real panel through the
     * display framework before kplayer opens its VIDEO1 layer.
     */
    if (kd_player_init(K_FALSE) != K_SUCCESS) {
        printf("[gallery-player] media initialization failed\n");
        return 1;
    }
    if (kd_display_init(ST7701_480_640_DSI_V1) != K_SUCCESS) {
        printf("[gallery-player] ST7701 initialization failed\n");
        kd_player_deinit(K_FALSE);
        return 1;
    }
    kd_player_set_connector_type(ST7701_480_640_DSI_V1);
    kd_player_regcallback(player_event, NULL);

    for (int run = 0; run < (repeat_test ? 2 : 1); ++run) {
        while (sem_trywait(&g_finished) == 0)
            ;
        reset_playback_clock();
        printf("[gallery-player] open run %d/%d\n", run + 1,
               repeat_test ? 2 : 1);
        if (kd_player_setdatasource(argv[1]) != K_SUCCESS) {
            printf("[gallery-player] cannot open %s\n", argv[1]);
            result = 1;
            break;
        }

        kd_display_layer_disable(K_VO_LAYER_VIDEO1);
        if (kd_display_layer_configure(
                K_VO_LAYER_VIDEO1, PIXEL_FORMAT_YUV_SEMIPLANAR_420,
                640, 480, 0, 0, 255, GDMA_ROTATE_DEGREE_270, 2, 2) !=
                K_SUCCESS ||
            kd_display_layer_enable(K_VO_LAYER_VIDEO1) != K_SUCCESS) {
            printf("[gallery-player] VIDEO1 configuration failed\n");
            kd_player_stop();
            result = 1;
            break;
        }
        printf("[gallery-player] playing %s\n", argv[1]);
        if (kd_player_start() != K_SUCCESS) {
            kd_player_stop();
            result = 1;
            break;
        }

        if (repeat_test && run == 0) {
            /* Reproduce Gallery's hardest transition: leave while paused,
             * then open the same decoder again in this process. */
            usleep(700000);
            printf("[gallery-player] pausing first run before close\n");
            kd_player_pause();
            usleep(150000);
            kd_player_stop();
            printf("[gallery-player] first run closed\n");
            continue;
        }

        sem_wait(&g_finished);
        if (!g_interrupted)
            usleep(500000);
        kd_player_stop();
        if (repeat_test && !g_interrupted)
            printf("[gallery-player] repeat playback PASS\n");
        if (g_interrupted)
            break;
    }

    kd_display_deinit();
    kd_player_deinit(K_FALSE);
    sem_destroy(&g_finished);
    return result;
}
