#include "power_control.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define DSHANPI_PMU_DEVICE "/dev/pmu_pwrkey"
#define DSHANPI_PMU_IOCTL_POWER_OFF _IO('P', 0x06)
#define DSHANPI_PMU_IOCTL_REBOOT    _IO('P', 0x07)
#define DSHANPI_PMU_IOCTL_REBOOT_TO_UPGRADE _IO('P', 0x08)

static int power_command(unsigned long command)
{
    int fd = open(DSHANPI_PMU_DEVICE, O_RDWR);
    int result;

    if (fd < 0) {
        perror("[power] open " DSHANPI_PMU_DEVICE);
        return -1;
    }
    result = ioctl(fd, command);
    if (result != 0) {
        perror("[power] ioctl");
    }
    close(fd);
    return result;
}

int dshanpi_power_off(void)
{
    return power_command(DSHANPI_PMU_IOCTL_POWER_OFF);
}

int dshanpi_power_reboot(void)
{
    return power_command(DSHANPI_PMU_IOCTL_REBOOT);
}

int dshanpi_power_reboot_to_upgrade(void)
{
    return power_command(DSHANPI_PMU_IOCTL_REBOOT_TO_UPGRADE);
}
