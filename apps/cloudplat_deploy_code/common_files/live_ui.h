#pragma once

#include <atomic>

/* Wait for the on-screen back button or the legacy serial 'q' command. */
void cloud_live_wait_for_exit(std::atomic<bool> &stop);
