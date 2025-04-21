#include "rdcp-callbacks.h"
#include "hal.h"
#include "rdcp-common.h"
#include "serial.h"
#include "lora.h"
#include "rdcp-memory.h"
#include "persistence.h"
#include "rdcp-scheduler.h"
#include "Base64ren.h"

callback_chain CC[NUM_TX_CALLBACKS];
extern da_config CFG;
extern rdcp_memory_table mem;
extern int64_t last_periodic_chain_finish;

/**
 * Send a "delivery receipt" RDCP Message to the given destination. 
 * @param destination RDCP Address of recipient 
 */
void rdcp_send_delivery_receipt(uint16_t destination)
{
    /* Prepare Delivery Receipt */
    rdcp_message r;
    r.header.sender = CFG.rdcp_address;
    r.header.origin = CFG.rdcp_address;
    r.header.sequence_number = get_next_rdcp_sequence_number(CFG.rdcp_address);
    r.header.destination = destination;
    r.header.message_type = RDCP_MSGTYPE_DELIVERY_RECEIPT;
    r.header.rdcp_payload_length = 0;
    r.header.counter = rdcp_get_default_retransmission_counter_for_messagetype(r.header.message_type);
    r.header.relay1 = 0xEE;
    r.header.relay2 = 0xEE;
    r.header.relay3 = 0xEE;

    /* Update CRC header field */
    uint8_t data_for_crc[256];
    memcpy(&data_for_crc, &r.header, RDCP_HEADER_SIZE - 2);
    for (int i=0; i < r.header.rdcp_payload_length; i++) data_for_crc[i + RDCP_HEADER_SIZE - 2] = r.payload.data[i];
    uint16_t actual_crc = crc16(data_for_crc, RDCP_HEADER_SIZE - 2 + r.header.rdcp_payload_length);
    r.header.checksum = actual_crc;

    /* Schedule Delivery Receipt for transmission on CHANNEL433 */
    uint8_t data_for_scheduler[256];
    memcpy(&data_for_scheduler, &r.header, RDCP_HEADER_SIZE);
    for (int i=0; i < r.header.rdcp_payload_length; i++) 
        data_for_scheduler[i + RDCP_HEADER_SIZE] = r.payload.data[i];

    rdcp_txqueue_add(CHANNEL433, data_for_scheduler, RDCP_HEADER_SIZE + r.header.rdcp_payload_length,
      NOTIMPORTANT, NOFORCEDTX, TX_CALLBACK_NONE, 0); // end of chain, no callback needed

    return;
}

/**
 * Sends a memory. 
 * @param memidx Index of the memory to send 
 * @param callback Callback number to use, e.g., TX_CALLBACK_FETCH_SINGLE 
 * @param channel Either CHANNEL433 or CHANNEL868
 */
void rdcp_send_memory(int memidx, uint8_t callback, uint8_t channel)
{ 
    char info[256];
    snprintf(info, 256, "INFO: Scheduling memory %d with callback %d", memidx, callback);
    serial_writeln(info);

    if (memidx == -1) return;

    /* Fetch original memory */
    rdcp_message r;
    memcpy(&r.header, mem.entries[memidx].payload, RDCP_HEADER_SIZE);
    for (int i=0; i<r.header.rdcp_payload_length; i++) r.payload.data[i] = mem.entries[memidx].payload[RDCP_HEADER_SIZE+i];

    /* Adjust the header fields of the outgoing message */
    r.header.sender = CFG.rdcp_address;
    r.header.counter = CFG.memory_retransmissions;
    r.header.relay1 = 0xEE;
    r.header.relay2 = 0xEE;
    r.header.relay3 = 0xEE;

    /* Update CRC header field */
    uint8_t data_for_crc[256];
    memcpy(&data_for_crc, &r.header, RDCP_HEADER_SIZE - 2);
    for (int i=0; i < r.header.rdcp_payload_length; i++) data_for_crc[i + RDCP_HEADER_SIZE - 2] = r.payload.data[i];
    uint16_t actual_crc = crc16(data_for_crc, RDCP_HEADER_SIZE - 2 + r.header.rdcp_payload_length);
    r.header.checksum = actual_crc;

    /* Schedule for transmission on given channel */
    uint8_t data_for_scheduler[256];
    memcpy(&data_for_scheduler, &r.header, RDCP_HEADER_SIZE);
    for (int i=0; i < r.header.rdcp_payload_length; i++) 
        data_for_scheduler[i + RDCP_HEADER_SIZE] = r.payload.data[i];

    rdcp_txqueue_add(channel, data_for_scheduler, RDCP_HEADER_SIZE + r.header.rdcp_payload_length,
      NOTIMPORTANT, NOFORCEDTX, callback, 0);

    return;
}

void rdcp_chain_starter(uint8_t callback_to_use, int starter, uint16_t destination, uint16_t refnr)
{
    char info[256];

    if (callback_to_use == TX_CALLBACK_FETCH_SINGLE)
    {
        if (starter == -1)
        { // nothing to send but delivery receipt
            rdcp_send_delivery_receipt(destination);
        }
        else 
        {
            if (CC[TX_CALLBACK_FETCH_SINGLE].in_use)
            {
                serial_writeln("WARNING: Chain Fetch Single already in use, cannot comply with request");
                return;
            }
            CC[TX_CALLBACK_FETCH_SINGLE].in_use = true;
            int64_t now = my_millis();
            CC[TX_CALLBACK_FETCH_SINGLE].activity = now;
            CC[TX_CALLBACK_FETCH_SINGLE].timeout = now + 10 * 60 * 1000; // 10 minute timeout
            CC[TX_CALLBACK_FETCH_SINGLE].refnr = refnr;
            CC[TX_CALLBACK_FETCH_SINGLE].destination = destination;
            CC[TX_CALLBACK_FETCH_SINGLE].last_mem_idx = starter;

            mem.entries[starter].used_in_fetch_single = true;

            rdcp_send_memory(starter, TX_CALLBACK_FETCH_SINGLE, CHANNEL433);
        }
    }

    if (callback_to_use == TX_CALLBACK_FETCH_ALL)
    {
        if (starter == -1)
        { // nothing to send but delivery receipt
            rdcp_send_delivery_receipt(destination);
        }
        else 
        {
            if (CC[TX_CALLBACK_FETCH_ALL].in_use)
            {
                serial_writeln("WARNING: Chain Fetch All already in use, cannot comply with request");
                return;
            }
            CC[TX_CALLBACK_FETCH_ALL].in_use = true;
            int64_t now = my_millis();
            CC[TX_CALLBACK_FETCH_ALL].activity = now;
            CC[TX_CALLBACK_FETCH_ALL].timeout = now + 10 * 60 * 1000; // 10 minute timeout
            CC[TX_CALLBACK_FETCH_ALL].refnr = refnr;
            CC[TX_CALLBACK_FETCH_ALL].destination = destination;
            CC[TX_CALLBACK_FETCH_ALL].last_mem_idx = starter;

            mem.entries[starter].used_in_fetch_all = true;

            rdcp_send_memory(starter, TX_CALLBACK_FETCH_ALL, CHANNEL433);
        }
    }

    if (callback_to_use == TX_CALLBACK_PERIODIC868)
    {
        if (starter == -1)
        { // nothing to send
            last_periodic_chain_finish = my_millis();
            return;
        }
        else 
        {
            if (CC[TX_CALLBACK_PERIODIC868].in_use)
            {
                serial_writeln("WARNING: Chain Periodic 868 already in use, cannot comply with request");
                return;
            }
            CC[TX_CALLBACK_PERIODIC868].in_use = true;
            int64_t now = my_millis();
            CC[TX_CALLBACK_PERIODIC868].activity = now;
            CC[TX_CALLBACK_PERIODIC868].timeout = now + 10 * 60 * 1000; // 10 minute timeout
            CC[TX_CALLBACK_PERIODIC868].refnr = refnr;
            CC[TX_CALLBACK_PERIODIC868].destination = destination;
            CC[TX_CALLBACK_PERIODIC868].last_mem_idx = starter;

            mem.entries[starter].used_in_periodic868 = true;

            rdcp_send_memory(starter, TX_CALLBACK_PERIODIC868, CHANNEL868);
        }
    }

    return;
}

void rdcp_chain_callback(uint8_t callback_type, bool has_timeout)
{
    char info[256];
    snprintf(info, 256, "INFO: Callback %d triggered (%s)", callback_type, has_timeout ? "timeout" : "regular");
    serial_writeln(info);

    if (callback_type == TX_CALLBACK_FETCH_SINGLE)
    {
        if (has_timeout)
        { // Request could not be fulfilled due to busy channel. Try to send receipt at least.
            rdcp_send_delivery_receipt(CC[TX_CALLBACK_FETCH_SINGLE].destination);
            CC[TX_CALLBACK_FETCH_SINGLE].in_use = false;
            for (int i=1; i < MAX_STORED_MSGS; i++) mem.entries[i].used_in_fetch_single = false;
            return;
        }
        /* 
            We need to determine whether there is at least one more related memory to send. 
            It may be a multi-fragment OA or the corresponding Signature. 
        */
        bool has_more = false;
        int memidx = -1;

        for (int i=1; i < MAX_STORED_MSGS; i++) // Start with i=1 to skip last sent memory
        {
            if (
                (mem.entries[(i + CC[TX_CALLBACK_FETCH_SINGLE].last_mem_idx) % MAX_STORED_MSGS].reference_number == CC[TX_CALLBACK_FETCH_SINGLE].refnr) &&
                (mem.entries[(i + CC[TX_CALLBACK_FETCH_SINGLE].last_mem_idx) % MAX_STORED_MSGS].used_in_fetch_single == false)
               )
               {              
                   has_more = true;
                   memidx = (i + CC[TX_CALLBACK_FETCH_SINGLE].last_mem_idx) % MAX_STORED_MSGS;
                   break;
               }
        }

        if (has_more)
        {
            int64_t now = my_millis();
            CC[TX_CALLBACK_FETCH_SINGLE].activity = now;
            CC[TX_CALLBACK_FETCH_SINGLE].timeout = now + 10 * 60 * 1000; // 10 minute timeout
            CC[TX_CALLBACK_FETCH_SINGLE].last_mem_idx = memidx;
            mem.entries[memidx].used_in_fetch_single = true;
            rdcp_send_memory(memidx, TX_CALLBACK_FETCH_SINGLE, CHANNEL433);
        }
        else
        { // no more memories to send -> conclude the chain
            rdcp_send_delivery_receipt(CC[TX_CALLBACK_FETCH_SINGLE].destination);
            CC[TX_CALLBACK_FETCH_SINGLE].in_use = false;
            for (int i=1; i < MAX_STORED_MSGS; i++) mem.entries[i].used_in_fetch_single = false;
        }
    }

    if (callback_type == TX_CALLBACK_FETCH_ALL)
    {
        if (has_timeout)
        { // Request could not be fulfilled due to busy channel. Try to send receipt at least.
            rdcp_send_delivery_receipt(CC[TX_CALLBACK_FETCH_ALL].destination);
            CC[TX_CALLBACK_FETCH_ALL].in_use = false;
            for (int i=1; i < MAX_STORED_MSGS; i++) mem.entries[i].used_in_fetch_all = false;
            return;
        }
        /* 
            We need to determine whether there is at least one more related memory to send. 
        */
        bool has_more = false;
        int memidx = -1;

        for (int i=1; i < MAX_STORED_MSGS; i++) // Start with i=1 to skip last sent memory
        {
            if (
                (mem.entries[(i + CC[TX_CALLBACK_FETCH_ALL].last_mem_idx) % MAX_STORED_MSGS].reference_number >= CC[TX_CALLBACK_FETCH_ALL].refnr) &&
                (mem.entries[(i + CC[TX_CALLBACK_FETCH_ALL].last_mem_idx) % MAX_STORED_MSGS].used_in_fetch_all == false)
               )
               {              
                   has_more = true;
                   memidx = (i + CC[TX_CALLBACK_FETCH_ALL].last_mem_idx) % MAX_STORED_MSGS;
                   break;
               }
        }

        if (has_more)
        {
            int64_t now = my_millis();
            CC[TX_CALLBACK_FETCH_ALL].activity = now;
            CC[TX_CALLBACK_FETCH_ALL].timeout = now + 10 * 60 * 1000; // 10 minute timeout
            CC[TX_CALLBACK_FETCH_ALL].last_mem_idx = memidx;
            mem.entries[memidx].used_in_fetch_all = true;
            rdcp_send_memory(memidx, TX_CALLBACK_FETCH_ALL, CHANNEL433);
        }
        else
        { // no more memories to send -> conclude the chain
            rdcp_send_delivery_receipt(CC[TX_CALLBACK_FETCH_ALL].destination);
            CC[TX_CALLBACK_FETCH_ALL].in_use = false;
            for (int i=1; i < MAX_STORED_MSGS; i++) mem.entries[i].used_in_fetch_all = false;
        }        
    }

    if (callback_type == TX_CALLBACK_PERIODIC868)
    { 
        if (has_timeout)
        { // Chain cannot be completed due to busy channel
            CC[TX_CALLBACK_PERIODIC868].in_use = false;
            for (int i=1; i < MAX_STORED_MSGS; i++) mem.entries[i].used_in_periodic868 = false;
            last_periodic_chain_finish = my_millis();
            return;
        }
        /* 
            We need to determine whether there is at least one more related memory to send. 
        */
        bool has_more = false;
        int memidx = -1;

        for (int i=1; i < MAX_STORED_MSGS; i++) // Start with i=1 to skip last sent memory
        {
            if (
                (mem.entries[(i + CC[TX_CALLBACK_PERIODIC868].last_mem_idx) % MAX_STORED_MSGS].timestamp_added >= my_millis() - CFG.max_periodic868_age) &&
                (mem.entries[(i + CC[TX_CALLBACK_PERIODIC868].last_mem_idx) % MAX_STORED_MSGS].reference_number > 0) &&
                (mem.entries[(i + CC[TX_CALLBACK_PERIODIC868].last_mem_idx) % MAX_STORED_MSGS].used_in_periodic868 == false)
               )
               {              
                   has_more = true;
                   memidx = (i + CC[TX_CALLBACK_PERIODIC868].last_mem_idx) % MAX_STORED_MSGS;
                   break;
               }
        }

        if (has_more)
        {
            int64_t now = my_millis();
            CC[TX_CALLBACK_PERIODIC868].activity = now;
            CC[TX_CALLBACK_PERIODIC868].timeout = now + 10 * 60 * 1000; // 10 minute timeout
            CC[TX_CALLBACK_PERIODIC868].last_mem_idx = memidx;
            mem.entries[memidx].used_in_periodic868 = true;
            rdcp_send_memory(memidx, TX_CALLBACK_PERIODIC868, CHANNEL868);
        }
        else
        { // no more memories to send -> conclude the chain
            CC[TX_CALLBACK_PERIODIC868].in_use = false;
            for (int i=1; i < MAX_STORED_MSGS; i++) mem.entries[i].used_in_periodic868 = false;
            last_periodic_chain_finish = my_millis();
        }        
    }

    return;
}

void rdcp_periodic_kickstart(void)
{
    if ((CFG.periodic_enabled) && 
        (!CC[TX_CALLBACK_PERIODIC868].in_use) && 
        (my_millis() > last_periodic_chain_finish + CFG.periodic_interval))
    {
        int first = mem.idx_first;
        int starter = -1;
        if (first != -1)
        {
            for (int i=0; i < MAX_STORED_MSGS; i++)
            {
                if (mem.entries[(i + first) % MAX_STORED_MSGS].timestamp_added >= my_millis() - CFG.max_periodic868_age)
                {
                    starter = i;
                    break; // find first only
                }
            }
        }
        serial_writeln("WARNING: Starting periodic868 chain"); // DEBUG
        /* Destination and refnr parameters are not relevant for this chain */
        rdcp_chain_starter(TX_CALLBACK_PERIODIC868, starter, 0xFFFF, 0xFFFF);
    }
    return;
}

/* EOF */