#include "rdcp-csv.h"
#include "serial.h"
#include "rdcp-common.h"
#include "lora.h"
#include "hal.h"

int64_t last_csv_timestamp[NUMCHANNELS] = { RDCP_TIMESTAMP_ZERO, RDCP_TIMESTAMP_ZERO };

extern lora_message current_lora_message;
extern rdcp_message rdcp_msg_in;
extern da_config CFG;
extern int64_t CFEst[NUMCHANNELS]; 
extern uint16_t most_recent_airtime;
extern uint8_t  most_recent_future_timeslots;

void print_rdcp_csv(void)
{
  int64_t now = my_millis();
  char info[2*INFOLEN];

  uint16_t refnr = RDCP_OA_REFNR_SPECIAL_ZERO;
  if (rdcp_msg_in.header.message_type == RDCP_MSGTYPE_OFFICIAL_ANNOUNCEMENT)
  {
    refnr = rdcp_msg_in.payload.data[1] + 256 * rdcp_msg_in.payload.data[2];
  }
  else if (rdcp_msg_in.header.message_type == RDCP_MSGTYPE_SIGNATURE)
  {
    refnr = rdcp_msg_in.payload.data[0] + 256 * rdcp_msg_in.payload.data[1];
  }

  snprintf(info, 2*INFOLEN, "RDCPCSV: %04X-%s,%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%d,%04X,%d,%04X,%04X,%04X,%04X,%02X,%d,%02X,%02X,%02X,%04X,%d,%3.3f", 
    CFG.rdcp_address, current_lora_message.channel == CHANNEL433 ? "433" : "868",
    now - last_csv_timestamp[current_lora_message.channel],
    now, 
    CFEst[current_lora_message.channel],
    CFEst[current_lora_message.channel] - now,
    rdcp_msg_in.header.rdcp_payload_length + RDCP_HEADER_SIZE,
    refnr,
    most_recent_future_timeslots,
    rdcp_msg_in.header.sender,
    rdcp_msg_in.header.origin,
    rdcp_msg_in.header.sequence_number,
    rdcp_msg_in.header.destination,
    rdcp_msg_in.header.message_type,
    rdcp_msg_in.header.counter,
    rdcp_msg_in.header.relay1,
    rdcp_msg_in.header.relay2,
    rdcp_msg_in.header.relay3,
    rdcp_msg_in.header.checksum,
    most_recent_airtime,
    CFG.lora[current_lora_message.channel].freq
  );
  serial_writeln(info);
  last_csv_timestamp[current_lora_message.channel] = now;
  return;
}

/* EOF */