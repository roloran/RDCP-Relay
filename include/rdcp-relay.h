#ifndef _RDCP_RELAY 
#define _RDCP_RELAY 

#include <Arduino.h> 

/**
 * Checks whether we are a designated Relay for the current RDCP Message. 
 * @return number of timeslots to delay for relaying, or -1 if not designated as Relay
 */
int rdcp_check_relay_designation(void);

/**
 * Check whether the same message has already been relayed by us. 
 * @return true if already relayed, false if not relayed yet 
 */
bool rdcp_check_has_already_relayed(void);

/**
 * Schedule relayed message transmission. 
 * @param relay_delay Number of timeslots to delay when relaying
 */
void rdcp_schedule_relayed_message(int relay_delay);

struct relay_memory_entry {
    uint16_t origin = 0x0000;
    uint16_t seqnr  = 0x0000;
};

#endif 
/* EOF */