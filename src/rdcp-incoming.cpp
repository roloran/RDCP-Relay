#include "rdcp-incoming.h"
#include "lora.h"
#include "serial.h"
#include "persistence.h"
#include "rdcp-common.h"
#include "rdcp-relay.h"
#include "rdcp-entrypoint.h"
#include "rdcp-blockdevice.h"
#include "rdcp-scheduler.h"
#include "rdcp-forward.h"
#include "rdcp-neighbors.h"
#include "rdcp-memory.h"
#include "rdcp-commands.h"

lora_message current_lora_message;
extern rdcp_message rdcp_msg_in;
extern da_config CFG;
extern runtime_da_data DART;

int32_t bad_crc_counter;
uint16_t last_origin = 0x0000;
uint16_t last_seqnr = 0x0000;
bool currently_in_fetch_mode = false;

void rdcp_handle_incoming_lora_message(void)
{
    /* Check whether it could be an RDCP message at all */
    if (current_lora_message.payload_length < RDCP_HEADER_SIZE)
    {
        serial_writeln("INFO: LoRa packet too small - not an RDCP message, not processing");
        // NB: Received non-RDCP LoRa packets have no influence on CFEst, so there is nothing else to do.
        return;
    }

    /* Copy the message to process into the rdcp_msg_in data structure */
    memcpy(&rdcp_msg_in.header, &current_lora_message.payload, RDCP_HEADER_SIZE);
    for (int i=RDCP_HEADER_SIZE; i<current_lora_message.payload_length; i++) rdcp_msg_in.payload.data[i-RDCP_HEADER_SIZE] = current_lora_message.payload[i];
    
    /* Verify the CRC-16 checksum */
    if (!rdcp_check_crc_in(current_lora_message.payload_length))
    {
        serial_writeln("INFO: RDCP checksum mismatch - not processing");
        // NB: Any RDCP Header or RDCP Payload field may have been corrupted,
        //     so we do not process anything further, including updates to CFEst.
        bad_crc_counter++;
        if (bad_crc_counter % 100 == 0)
        {
            /* 
                Bad CRC usually is the result of poor reception or other devices sending 
                non-RDCP LoRA packets on the same channel. However, we re-initialize our 
                LoRa radios every now and then in case it might be hardware-related. 
            */
            serial_writeln("WARNING: Bad CRC counter exceeded threshold - consider additional countermeasures!");
            setup_radio();
        }
        return;
    }

    /* Update the CFEst since we received an RDCP Message */
    rdcp_update_cfest_in();

    /* Stop any TX events on the current channel as long as it is busy */
    if ((rdcp_msg_in.header.origin != last_origin) && (rdcp_msg_in.header.sequence_number != last_seqnr))
    {
        last_origin = rdcp_msg_in.header.origin;
        last_seqnr = rdcp_msg_in.header.sequence_number;
        rdcp_reschedule_on_busy_channel(current_lora_message.channel);
    }

    /* Check the RDCP Message duplicate status */
    bool duplicate = rdcp_check_duplicate_message(rdcp_msg_in.header.origin, rdcp_msg_in.header.sequence_number);

    /* On the 433 MHz channel, we may be a designated relay even if it is a duplicate */
    if (current_lora_message.channel == CHANNEL433)
    {
        /* If it is a duplicate, we need to remember whether we already relayed the 
           message. We relay it at most once. */
        int relay_delay = rdcp_check_relay_designation();
        if (relay_delay > -1)
        {
            /* 
                We could add further checks here whether the message should really be relayed, 
                e.g., based on Message Type (e.g., Fetch) and Origin (e.g., blocked device). 
                However, for now we assume that we are only assigned as relay if we should do so.
            */
            if (!rdcp_check_has_already_relayed()) 
            {
                rdcp_schedule_relayed_message(relay_delay);
                DART.num_rdcp_tx++;
            }
        }
    }

    /* On the 868 MHz channel, we have to process MG HEARTBEAT messages (always dupes). */
    /* We also register Sender-based RSSI and SNR values for any RDCP Messages. */
    bool heartbeat = false;
    if ((current_lora_message.channel == CHANNEL868) && 
        (rdcp_msg_in.header.message_type == RDCP_MSGTYPE_HEARTBEAT)) heartbeat = true;
    rdcp_neighbor_register_rx(current_lora_message.channel, rdcp_msg_in.header.sender, 
                              current_lora_message.rssi, current_lora_message.snr, 
                              current_lora_message.timestamp, heartbeat);

    /* Only perform the following actions if the message is not a duplicate */
    if (!duplicate)
    {
        DART.num_rdcp_rx++;

        if (current_lora_message.channel == CHANNEL433)
        {
            if (rdcp_check_forward_868_relevance()) rdcp_forward_schedule(false); // do not add a delay
            if (rdcp_check_forward_da_relevance()) rdcp_msg_to_da_via_serial();
        }
        else /* RDCP Message received on CHANNEL868 */
        {
            /* Forward the RDCP Message on 433 MHz if we are the Entry Point
               unless there are reasons not to forward it. */
            if (rdcp_check_entrypoint_designation())
            {
                if (rdcp_check_entrypoint_messagetype_valid() && 
                    rdcp_relay_allowed_for_device(rdcp_msg_in.header.origin))
                {
                    /* If it is a CIRE, we also have to send an ACK back to the MG */
                    if (rdcp_msg_in.header.message_type == RDCP_MSGTYPE_CITIZEN_REPORT)
                    {
                           rdcp_send_ack_unsigned(CFG.rdcp_address, rdcp_msg_in.header.origin, 
                                                  rdcp_msg_in.header.sequence_number);
                    }

                    /* Forward the message on the 433 MHz channel unless we are the destination */
                    if (rdcp_msg_in.header.destination != CFG.rdcp_address)
                    { 
                        rdcp_entrypoint_schedule();
                        DART.num_rdcp_tx++;
                    
                        /* 
                            If the HQ used us as entry point, send the message also on the 868 MHz channel 
                            so everyone in our area gets it even if they do not hear the HQ device directly.
                        */
                        if (rdcp_msg_in.header.sender < 0x0100)
                        {
                            if (rdcp_check_forward_868_relevance()) rdcp_forward_schedule(true); // add a delay
                        }
                    }

                    /* Forward relevant messages to the DA even if we did not broadcast it. */
                    if (rdcp_check_forward_da_relevance()) rdcp_msg_to_da_via_serial();
                }
            }
            else 
            {
                /*
                    We are not the designated Entry Point but we got a new (not duplicate) message 
                    on 868 MHz first. If we receive it later on 433 MHz, we will consider it a 
                    duplicate. While we still may relay it then, we would not forward it on 868 MHz. 
                    Thus, we have to forward in on 868 MHz and to our DA here. 
                */
                if (rdcp_check_forward_868_relevance()) rdcp_forward_schedule(true); // add a delay
                if (rdcp_check_forward_da_relevance()) rdcp_msg_to_da_via_serial();
            }
        }

        /* If we are the destination of a message, fulfill relevant requests */
        if (rdcp_matches_any_of_my_addresses(rdcp_msg_in.header.destination))
        {
           rdcp_handle_command();
        }

        /* Store relevant RDCP Messages for periodic transmission on 868 MHz */
        if ((rdcp_msg_in.header.message_type == RDCP_MSGTYPE_OFFICIAL_ANNOUNCEMENT) || 
            (rdcp_msg_in.header.message_type == RDCP_MSGTYPE_SIGNATURE))
        {
            if ((rdcp_msg_in.header.destination == RDCP_BROADCAST_ADDRESS) || 
                ((rdcp_msg_in.header.destination >= 0xB000) && (rdcp_msg_in.header.destination <= 0xBFFF))) 
            {
                rdcp_memory_remember();
            }
        }
    }
    else
    { // RDCP Message is a duplicate, but certain messages must be processed even then.
        if (
            (rdcp_msg_in.header.message_type == RDCP_MSGTYPE_DEVICE_RESET) ||
            (rdcp_msg_in.header.message_type == RDCP_MSGTYPE_DEVICE_REBOOT) ||
            (rdcp_msg_in.header.message_type == RDCP_MSGTYPE_INFRASTRUCTURE_RESET)
        )
        {
            if (rdcp_matches_any_of_my_addresses(rdcp_msg_in.header.destination))
            {
                serial_writeln("INFO: Fulfilling request despite sent via duplicate RDCP message");
                rdcp_handle_command();
            }    
        }
        else if (currently_in_fetch_mode)
        {
            if ((rdcp_msg_in.header.message_type == RDCP_MSGTYPE_OFFICIAL_ANNOUNCEMENT) || 
                (rdcp_msg_in.header.message_type == RDCP_MSGTYPE_SIGNATURE))
            {
                rdcp_memory_remember();
                rdcp_msg_to_da_via_serial();
            }    
        }
        else 
        {
            serial_writeln("INFO: Ignoring duplicate");
        }
    }

    return;
}

/* EOF */