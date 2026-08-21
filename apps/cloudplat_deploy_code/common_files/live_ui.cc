#include "live_ui.h"

#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

namespace {

constexpr uint8_t kTouchDown = 2;
constexpr int kBackTouchExtent = 104;

struct TouchPoint {
    uint8_t event;
    uint8_t track_id;
    uint8_t width;
    uint16_t x_coordinate;
    uint16_t y_coordinate;
    uint32_t timestamp;
};

bool back_button_pressed(const TouchPoint &point)
{
    /* Native touch coordinates are 480x640; the panel is landscape. */
    const int x = 639 - static_cast<int>(point.y_coordinate);
    const int y = 479 - static_cast<int>(point.x_coordinate);
    return point.event == kTouchDown && x >= 0 && x < kBackTouchExtent &&
           y >= 0 && y < kBackTouchExtent;
}

} // namespace

void cloud_live_wait_for_exit(std::atomic<bool> &stop)
{
    const int touch_fd = open("/dev/touch0", O_RDWR | O_NONBLOCK);
    if (touch_fd < 0) {
        printf("[cloud-live] touch unavailable; press q on serial to return\n");
    } else {
        printf("[cloud-live] tap the top-left back button to return\n");
    }

    while (!stop.load()) {
        pollfd stdin_fd = {STDIN_FILENO, POLLIN, 0};
        const int ready = poll(&stdin_fd, 1, 10);

        if (ready > 0 && (stdin_fd.revents & POLLIN) != 0) {
            char input[16];
            const ssize_t size = read(STDIN_FILENO, input, sizeof(input));
            for (ssize_t i = 0; i < size; ++i) {
                if (input[i] == 'q' || input[i] == 'Q') {
                    stop = true;
                    break;
                }
            }
        }

        /* RT-Smart's touch device supports non-blocking read but does not
         * reliably advertise POLLIN. Poll it directly, as the other working
         * camera applications do. */
        if (touch_fd >= 0) {
            TouchPoint points[10];
            const ssize_t size = read(touch_fd, points, sizeof(points));
            const ssize_t point_count = size > 0 ? size / sizeof(points[0]) : 0;
            for (ssize_t i = 0; i < point_count; ++i) {
                const int x = 639 - static_cast<int>(points[i].y_coordinate);
                const int y = 479 - static_cast<int>(points[i].x_coordinate);
                if (points[i].event == kTouchDown) {
                    printf("[cloud-live-touch] point=(%d,%d)\n", x, y);
                }
                if (back_button_pressed(points[i])) {
                    printf("[cloud-live] back button pressed\n");
                    stop = true;
                    break;
                }
            }
        }
    }

    if (touch_fd >= 0) {
        close(touch_fd);
    }
}
