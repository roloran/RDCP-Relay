#include "rdcp-neighbors.h"
#include "serial.h"
#include "hal.h"

neighbor_table_entry neighbors[MAX_NEIGHBORS];

void rdcp_neighbor_register_rx(uint8_t channel, uint16_t sender, double rssi, double snr, int64_t timestamp, bool heartbeat, bool explicit_refnr, uint16_t latest_refnr, uint16_t roamingrec)
{
    int index = -1;
    for (int i=0; i<MAX_NEIGHBORS; i++)
    {
        if ((neighbors[i].sender == 0x0000) || (neighbors[i].sender == sender))
        {
            index = i;
            break;
        } 
    }

    if (index == -1)
    {
        serial_writeln("WARNING: Neighbor table overflow - increase size!");
        return;
    }

    neighbors[index].channel   = channel;
    neighbors[index].sender    = sender;
    neighbors[index].rssi      = rssi;
    neighbors[index].snr       = snr;
    neighbors[index].timestamp = timestamp;
    neighbors[index].heartbeat = heartbeat;
    neighbors[index].counted   = false;
    neighbors[index].explicit_refnr   = explicit_refnr;
    neighbors[index].latest_refnr     = latest_refnr;
    neighbors[index].roamingrec       = roamingrec;

    return;
}

void rdcp_neighbor_dump(void)
{
    char info[256];
    int64_t now = my_millis();
    serial_writeln("INFO: Begin of neighbor table dump");
    for (int i=0; i<MAX_NEIGHBORS; i++)
    {
        if (neighbors[i].sender != 0x0000)
        {
            int mytime = (int) ((now - neighbors[i].timestamp) / (1000 * 60));
            snprintf(info, 256, "INFO: Neighbor %d,%04X,%d,%.0f,%.0f,%c,%c,%04X,%04X,%dm",
                     i, neighbors[i].sender, neighbors[i].channel == CHANNEL433 ? 433:868,
                     neighbors[i].rssi, neighbors[i].snr, 
                     neighbors[i].heartbeat ? 'H':'-', 
                     neighbors[i].explicit_refnr ? 'X':'-',
                     neighbors[i].latest_refnr,
                     neighbors[i].roamingrec,                     
                     mytime);
            serial_writeln(info);
        }
    }
    serial_writeln("INFO: End of neighbor table dump");
    return;
}

/* EOF */