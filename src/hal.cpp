#include "hal.h"
#include "serial.h"
#include "lora.h"

extern da_config CFG;

int64_t my_millis(void)
{
    return (int64_t) esp_timer_get_time() / MILLISECONDS_TO_MICROSECONDS;
}

void cpu_fast(void)
{
    setCpuFrequencyMhz(240);
    return;
}

void cpu_slow(void)
{
    if (!CFG.bt_enabled) setCpuFrequencyMhz(80);
    return;
}

/* EOF */