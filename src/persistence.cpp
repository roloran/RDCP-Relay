#include "persistence.h"
#include "serial.h"

#include <FFat.h>

#define FILENAME_SERIAL_REPLAY "/config.txt"
#define FILENAME_PREFIX_SEQNR "/seqnr_"

bool hasFFat = false;

bool hasStorage(void)
{
  return hasFFat;
}

void setup_persistence(void)
{
  // FFat.format(); // unbrick device :-)
  if(FFat.begin(true)) hasFFat = true;
  return;
}

void persist_serial_command_for_replay(String s)
{
  if (!hasFFat) return;
  File f = FFat.open(FILENAME_SERIAL_REPLAY, FILE_APPEND);
  if (!f) return;
  char cline[256];
  s.toCharArray(cline, 256);
  f.printf("%s\n", cline);
  f.close();
  return;
}

void persistence_replay_serial(void)
{
  if (!hasFFat) return;
  File f = FFat.open(FILENAME_SERIAL_REPLAY, FILE_READ);
  if (!f) return;
  while (f.available())
  {
    String line = f.readStringUntil('\n');
    serial_process_command(line, "REPLAY: ", false);
  }
  f.close();
  return;
}

void persistence_reset_replay_serial(void)
{
  if (hasFFat) FFat.remove(FILENAME_SERIAL_REPLAY);
  return;
}

uint16_t get_next_rdcp_sequence_number(uint16_t origin)
{
  uint16_t seq = 1;
  if (!hasFFat) return seq;
  char fn[256];
  snprintf(fn, 256, "%s%04X", FILENAME_PREFIX_SEQNR, origin);
  File f = FFat.open(fn, FILE_READ);
  if (!f)
  {
    serial_writeln("WARNING: Missing sequence number file, starting with defaults");
    set_next_rdcp_sequence_number(origin, 2);
    return seq;
  }
  String line = "0";
  if (f.available())
  {
    line = f.readString();
  }
  seq = line.toInt();
  f.close();
  if (seq == 0) serial_writeln("WARNING: Existing sequence number file yielded 0");
  set_next_rdcp_sequence_number(origin, seq+1);
  return seq;
}

uint16_t set_next_rdcp_sequence_number(uint16_t origin, uint16_t seq)
{
  if (!hasFFat) return seq;
  char fn[256];
  snprintf(fn, 256, "INFO: Persisting next-up seqnr %u for %04X", seq, origin);
  serial_writeln(fn);
  snprintf(fn, 256, "%s%04X", FILENAME_PREFIX_SEQNR, origin);
  FFat.remove(fn);
  File f = FFat.open(fn, FILE_WRITE);
  if (!f) return seq;
  char content[256];
  snprintf(content, 256, "%" PRIu16 "\n", seq);
  f.print(content);
  f.close();
  delay(1);
  return seq;
}

bool persistence_checkset_nonce(char *name, uint16_t nonce)
{
  if (!hasFFat) return false;
  bool is_valid = false;
  char filename[64];
  snprintf(filename, 64, "%s.nce", name);
  File f = FFat.open(filename, FILE_READ);
  if (!f)
  {
    is_valid = true; // never seen a nonce for this type before
  }
  else
  {
    String line = "0";
    if (f.available())
    {
      line = f.readString();
    }
    int old_nonce = line.toInt();
    f.close();
    if (old_nonce < nonce) is_valid = true;
  }
  if (is_valid)
  {
    f = FFat.open(filename, FILE_WRITE);
    if (!f) is_valid = false; // cannot persist nonce, don't trust it
    char content[256];
    snprintf(content, 256, "%" PRIu16 "\n", nonce);
    f.print(content);
    f.close();
  }
  return is_valid;
}

/* EOF */