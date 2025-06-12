#include "hal.h"
#include "serial.h"

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
    setCpuFrequencyMhz(80);
    return;
}

/* EOF */