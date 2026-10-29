#include "battery.h"
#include "sdkconfig.h"

battery_status_t battery_read(void)
{
#if CONFIG_FLOW_BATTERY_MOCK
    return (battery_status_t) {
        .available = true,
        .mock = true,
        .percent = CONFIG_FLOW_BATTERY_MOCK_PERCENT,
        .voltage = CONFIG_FLOW_BATTERY_MOCK_MV / 1000.0f,
    };
#else
    // TODO: define the divider/monitor, ADC pin, calibration and battery curve.
    // Do not report a plausible-looking voltage from unfinished hardware.
    return (battery_status_t) {0};
#endif
}
