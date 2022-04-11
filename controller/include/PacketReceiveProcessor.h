#ifndef PacketReceiveProcessor_H_
#define PacketReceiveProcessor_H_

#include "defines.h"

#ifdef ESP32
#include <freertos/task.h>
#endif
#include <cppQueue.h>

#include "common.h"

class PacketReceiveProcessor
{
public:
  PacketReceiveProcessor() {}
  ~PacketReceiveProcessor() {}
  bool ProcessReply(PacketMeta *data);
  bool HasCommsTimedOut();

  // Count lost packets (CRC, module broken, whatever).
  // NOTE this misses 
  uint16_t pendingCRCErrors = 0;
  uint16_t totalCRCErrors = 0;
  uint16_t totalOutofSequenceErrors = 0;
  uint16_t totalNotProcessedErrors = 0;
  uint32_t packetsReceived = 0;
  uint8_t totalModulesFound = 0;

  //Duration (ms) for a packet to travel through the string (default to 10 seconds at startup)
  uint32_t packetTimerMillisecond = 10 * 1000;
  uint32_t packetLastReceivedMillisecond = 0;
  uint16_t packetLastReceivedSequence = 0;

  void ResetCounters()
  {
    totalNotProcessedErrors = 0;
    packetsReceived = 0;
    totalOutofSequenceErrors = 0;
  }

private:
  PacketMeta *_meta;
  PacketHeader *_header;
  void *_data;
  uint16_t _dataLen;

  bool ProcessReplySettings();
  bool ProcessReplyVoltage();
  bool ProcessReplyTemperature();

  bool ProcessReplyPacketCounters();
  bool ProcessReplyBalancePower();
  bool ProcessReplyReadBalanceCurrentCounter();
  bool ProcessReplyReadPacketReceivedCounter();

  bool isShortPacket(uint16_t len);
};

#ifdef ESP32
extern TaskHandle_t voltageandstatussnapshot_task_handle;
#endif

#endif
