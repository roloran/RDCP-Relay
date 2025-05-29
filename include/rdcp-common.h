#ifndef _RDCP_COMMON 
#define _RDCP_COMMON

#include <Arduino.h>

/// RDCP v0.4 fixed header size
#define RDCP_HEADER_SIZE 16
/// RDCP v0.4 fixed cryptographic signature size
#define RDCP_SIGNATURE_LENGTH 65

/// RDCP v0.4 fixed broadcast address
#define RDCP_BROADCAST_ADDRESS    0xFFFF
/// RDCP v0.4 fixed HQ multicast address
#define RDCP_HQ_MULTICAST_ADDRESS 0x00FF

/// RDCP v0.4 fixed maximum LoRa Payload size
#define RDCP_MAX_PAYLOAD_SIZE 200

/// Magic values for TX scheduling; > 0 means delay in milliseconds
#define TX_IMMEDIATELY -1
#define TX_WHEN_CF 0

/// Buffer time within a timeslot between retransmissions according to specs
#define RDCP_TIMESLOT_BUFFERTIME 1000

/// How long does it take to schedule and pre-process any retransmission on this device (in ms)?
#define RETRANSMISSION_PROCESSING_TIME 200
// History: 38

/// How long does it take to schedule and pre-process a relay transmission (with initial/max counter value) on this device (in ms)?
#define TRANSMISSION_PROCESSING_TIME 200

/**
  * Data structure for an RDCP v0.4 Header
  */
struct rdcp_header {
    uint16_t sender;              //< current sender of the message (may be a relay)
    uint16_t origin;              //< original sender of the message (who created it)
    uint16_t sequence_number;     //< for detecting duplicates (specific for "origin")
    uint16_t destination;         //< intended destination (may be the broadcast address)
    uint8_t  message_type;        //< type of RDCP message according to specification
    uint8_t  rdcp_payload_length; //< the length of the inner RDCP payload
    uint8_t  counter;             //< retransmission counter
    uint8_t  relay1;              //< relay 1 designation, delay 1 assignment
    uint8_t  relay2;              //< relay 2 designation, delay 2 assignment
    uint8_t  relay3;              //< relay 3 designation, delay 3 assignment
    uint16_t checksum;            //< CRC-16 (CCITT) checksum
};
  
/**
  * Data structure for storing the RDCP v0.4 Payload of an RDCP Message
  */
struct rdcp_payload {
  uint8_t data[RDCP_MAX_PAYLOAD_SIZE]; // RDCP payload must not exceed 200 bytes
};
  
/**
  * Data structure for an RDCP v0.4 Message, consisting of RDCP Header and RDCP Payload
  */
struct rdcp_message {
  struct rdcp_header  header;
  struct rdcp_payload payload;
};

/**
  * Calculate the CRC-16 (CCITT) checksum of a byte array with given length
  * @param data The data to calculate the checksum for 
  * @param len length of the data 
  * @return uint16_t CRC-16 checksum
  */
uint16_t crc16(uint8_t *data, uint16_t len);
 
/**
  * Verify that the CRC-16 checksum of the most recently received RDCP Message is correct.
  * Needs the actual received packet length as parameter because the corresponding RDCP Header field might be corrupted.
  * @param real_packet_length Received LoRa packet payload length; may differ from RDCP header size + RDCP payload size
  * @return true if the CRC-16 in the RDCP Message is valid; false otherwise
  */
bool rdcp_check_crc_in(uint8_t real_packet_length);

/**
  * Data structure for Duplicate Table entries
  */
struct rdcp_dup_table_entry {
  uint16_t origin = 0x0000;          //< Origin from the RDCP Header
  uint16_t sequence_number = 0x0000; //< SequenceNumber from the RDCP Header
  int64_t last_seen = 0;             //< Timestamp of when the entry was last updated
};
  
/**
  * Data structure for the overall Duplicate Table
  */
struct rdcp_dup_table {
  unsigned short num_entries = 0;              //< Number of currently stored entries
  struct rdcp_dup_table_entry tableentry[256]; //< Array of Duplicate Table entries
};
  
/**
  * Resets the Duplicate Table by clearing all entries
  */
void rdcp_reset_duplicate_message_table(void);

/**
 * Restore a persisted duplicate table
 */
void rdcp_duplicate_table_restore(void);

/**
 * Persist the current duplicate table
 */
void rdcp_duplicate_table_persist(void);

/**
  * Checks whether an RDCP Message with Origin and SequenceNumber given as parameters
  * should be treated as duplicate (returns true) or new (returns false).
  * @param origin RDCP Origin address of the RDCP Message to check for duplicate 
  * @param sequence_number RDCP Header SequenceNumber of the RDCP Message to check for duplicate 
  * @return true if the message is a duplicate, false if it was not seen before
  */
bool rdcp_check_duplicate_message(uint16_t origin, uint16_t sequence_number);
 
/**
  * Returns the current timestamp value of CFEst (Channel Free Estimator).
  * @return int64_t timestamp (monotonic clock) of when the channel is expected be free again
  */
int64_t rdcp_get_channel_free_estimation(uint8_t channel);

/**
 * Set CFEst for a given channel.
 * @param channel CHANNEL433 or CHANNEL868
 * @param new_value New CFEst value for channel
 */
bool rdcp_set_channel_free_estimation(uint8_t channel, int64_t new_value);

/**
 * Update CFEst for a given channel (set only if longer busy than previously assumed).
 * @param channel CHANNEL433 or CHANNEL868
 * @param new_value New CFEst value for channel
 */
bool rdcp_update_channel_free_estimation(uint8_t channel, int64_t new_value);

/**
  * Calculate the airtime (in milliseconds) of sending a LoRa packet with the payload size
  * given as parameter under consideration of the currently used LoRa settings, such as
  * bandwidth, coding rate, and preamble length.
  * @param channel Either CHANNEL433 or CHANNEL868
  * @param payload_size Number of bytes for the LoRa packet payload (e.g., RDCP Message including Header and Payload)
  * @return Calculated airtime in milliseconds based on current LoRa radio parameters (e.g., SF, bandwidth)
  */
uint16_t airtime_in_ms(uint8_t channel, uint8_t payload_size);

/**
  * Return the duration of a full timeslot given an RDCP Message based
  * on its Message Type (i.e. initial RetransmissionCounter) and size.
  * @param channel Either CHANNEL433 or CHANNEL868
  * @param data RDCP Message to analyse. Must at least be an RDCP Header with rdcp_payload_size field. 
  * @return Timeslot duration in milliseconds (when applying the number of retransmissions according to RDCP specs)
  */
int64_t rdcp_get_timeslot_duration(uint8_t channel, uint8_t *data);

/**
 * Update the Channel Free Estimation (CFEst) based on the most recent incoming RDCP message.
 */
void rdcp_update_cfest_in(void);

/**
 * Update the Channel Free Estimation (CFEst) when sending an RDCP Message 
 * under propagation cycle considerations. 
 * @param channel CHANNEL433 or CHANNEL868 
 * @param len Length of RDCP Message in bytes (Header + Payload)
 * @param rcnt Retransmission counter used in the outgoing message 
 * @param mt RDCP Message Type of the outgoing message
 * @param relay1 RDCP Header field Relay1 of the outgoing message
 * @param relay2 RDCP Header field Relay2 of the outgoing message
 * @param relay3 RDCP Header field Relay3 of the outgoing message
 */

void rdcp_update_cfest_out(uint8_t channel, uint8_t len, uint8_t rcnt, uint8_t mt, uint8_t relay1, uint8_t relay2, uint8_t relay3);

/**
 * The default initial value of the RDCP Header Retransmission Counter field 
 * depends on the used Message Type. Retrieve this default value for a given 
 * message type. 
 * @param mt RDCP Message Type
 * @return Initial value of retransmission counter to use for this message type 
 */
uint8_t rdcp_get_default_retransmission_counter_for_messagetype(uint8_t mt);

/**
 * Returns the DA ACK type to use when responding to a CIRE considering the 
 * current infrastructure status. 
 * @return ACK Type to use for CIREs currently 
 */
uint8_t rdcp_get_ack_from_infrastructure_status(void);

/**
 * Check whether a given (destination) address matches our 
 * RDCP Address, one of our multicast addresses, or the broadcast address. 
 * @return true if the address matches any of this device's 
 */
bool rdcp_matches_any_of_my_addresses(uint16_t rdcpa);

/*
 * Are we within an ongoing previously known propagation cycle right now?
 * @return true if a propagation is still cycle ongoing, false otherwise
 */
bool rdcp_propagation_cycle_duplicate(void);

/*
 * RDCP v0.4 Message Type definitions
 */
#define RDCP_MSGTYPE_TEST                    0x00
#define RDCP_MSGTYPE_ECHO_REQUEST            0x01
#define RDCP_MSGTYPE_ECHO_RESPONSE           0x02
#define RDCP_MSGTYPE_BBK_STATUS_REQUEST      0x03
#define RDCP_MSGTYPE_BBK_STATUS_RESPONSE     0x04
#define RDCP_MSGTYPE_DA_STATUS_REQUEST       0x05
#define RDCP_MSGTYPE_DA_STATUS_RESPONSE      0x06
#define RDCP_MSGTYPE_TRACEROUTE_REQUEST      0x07
#define RDCP_MSGTYPE_TRACEROUTE_RESPONSE     0x08
#define RDCP_MSGTYPE_DEVICE_BLOCK_ALERT      0x09
#define RDCP_MSGTYPE_TIMESTAMP               0x0a
#define RDCP_MSGTYPE_DEVICE_RESET            0x0b
#define RDCP_MSGTYPE_DEVICE_REBOOT           0x0c
#define RDCP_MSGTYPE_MAINTENANCE             0x0d
#define RDCP_MSGTYPE_INFRASTRUCTURE_RESET    0x0e
#define RDCP_MSGTYPE_ACK                     0x0f
  
#define RDCP_MSGTYPE_OFFICIAL_ANNOUNCEMENT   0x10
#define RDCP_MSGTYPE_RESET_ALL_ANNOUNCEMENTS 0x11
  
#define RDCP_MSGTYPE_CITIZEN_REPORT          0x1a
#define RDCP_MSGTYPE_PRIVILEGED_REPORT       0x1c
  
#define RDCP_MSGTYPE_FETCH_ALL_NEW_MESSAGES  0x20
#define RDCP_MSGTYPE_FETCH_MESSAGE           0x21
#define RDCP_MSGTYPE_DELIVERY_RECEIPT        0x2a
#define RDCP_MSGTYPE_SCHEDULE_RCPT           0x2b
  
#define RDCP_MSGTYPE_SIGNATURE               0x30
#define RDCP_MSGTYPE_HEARTBEAT               0x31
#define RDCP_MSGTYPE_RTC                     0x32
  
/*
 * Subtypes for OFFICIAL ANNOUNCEMENTs
 */
#define RDCP_MSGTYPE_OA_SUBTYPE_RESERVED     0x00
#define RDCP_MSGTYPE_OA_SUBTYPE_NONCRISIS    0x10
#define RDCP_MSGTYPE_OA_SUBTYPE_CRISIS_TXT   0x20
#define RDCP_MSGTYPE_OA_SUBTYPE_CRISIS_GFX   0x21
#define RDCP_MSGTYPE_OA_SUBTYPE_UPDATE       0x22
#define RDCP_MSGTYPE_OA_SUBTYPE_FEEDBACK     0x30
#define RDCP_MSGTYPE_OA_SUBTYPE_INQUIRY      0x31
  
/*
 * Subtypes for CITIZEN REPORTs
 */
#define RDCP_MSGTYPE_CIRE_SUBTYPE_EMERGENCY  0x00
#define RDCP_MSGTYPE_CIRE_SUBTYPE_REQUEST    0x01
#define RDCP_MSGTYPE_CIRE_SUBTYPE_RESPONSE   0x02
  
/*
 * Subtypes for ACKNOWLEDGMENTs
 */
#define RDCP_ACKNOWLEDGMENT_POSITIVE         0x00
#define RDCP_ACKNOWLEDGMENT_NEGATIVE         0x01
#define RDCP_ACKNOWLEDGMENT_POSNEG           0x02

/*
 * RDCP Infrastructure modes 
 */
#define RDCP_INFRASTRUCTURE_MODE_NONCRISIS      0x00
#define RDCP_INFRASTRUCTURE_MODE_CRISIS         0x01
#define RDCP_INFRASTRUCTURE_MODE_CRISIS_NOSTAFF 0x02

/// A magic timestamp of NO_FORCED_TIME is used to signal that TX should happen when channel is free again
#define NO_FORCED_TIME 0

/// IMPORTANT and NONIMPORTANT are just parameter aliases for true and false
#define IMPORTANT true
#define NOTIMPORTANT false 

/// FORCEDTX and NOFORCEDTX are just parameter aliases for true and false
#define FORCEDTX true 
#define NOFORCEDTX false
#define NOTFORCEDTX false

/// Numeric identifiers for available callback functions
#define TX_CALLBACK_NONE         0
#define TX_CALLBACK_CIRE         1
#define TX_CALLBACK_RELAY        2
#define TX_CALLBACK_ENTRY        3
#define TX_CALLBACK_FORWARD      4
#define TX_CALLBACK_ACK          5
#define TX_CALLBACK_FETCH_SINGLE 6
#define TX_CALLBACK_FETCH_ALL    7
#define TX_CALLBACK_PERIODIC868  8

#define NUM_TX_CALLBACKS         9

#endif 
/* EOF */