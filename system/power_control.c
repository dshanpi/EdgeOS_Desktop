#include "power_control.h"
#include "drv_pmu.h"

#include <stdio.h>

static int power_command(int (*command)(drv_pmu_inst_t *), const char *name)
{
    drv_pmu_inst_t *pmu = NULL;
    int result;

    if (command == NULL || drv_pmu_inst_create(&pmu) != 0) {
        fprintf(stderr, "[power] cannot open PMU for %s\n", name);
        return -1;
    }
    result = command(pmu);
    if (result != 0)
        fprintf(stderr, "[power] PMU command %s failed\n", name);
    drv_pmu_inst_destroy(&pmu);
    return result;
}

int dshanpi_power_off(void)
{
    return power_command(drv_pmu_shutdown_now, "power-off");
}

int dshanpi_power_reboot(void)
{
    return power_command(drv_pmu_reboot, "reboot");
}

int dshanpi_power_reboot_to_upgrade(void)
{
    return power_command(drv_pmu_reboot_to_upgrade, "reboot-to-upgrade");
}
