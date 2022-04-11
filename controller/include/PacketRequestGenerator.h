#ifndef PacketRequestGenerator_H_
#define PacketRequestGenerator_H_

#include <Arduino.h>
#include <cppQueue.h>
#include <defines.h>

#include "common.h"

class PacketRequestGenerator
{
public:
  PacketRequestGenerator(cppQueue *requestQ) { _requestq = requestQ; }
  ~PacketRequestGenerator() {}
  bool sendGetSettingsRequest(uint8_t cellid);
  bool sendIdentifyModuleRequest(uint8_t cellid);
  bool sendSaveSetting(uint8_t m, uint16_t BypassThresholdmV, uint8_t BypassOverTempShutdown, float Calibration);
  bool sendSaveGlobalSetting(uint16_t BypassThresholdmV, uint8_t BypassOverTempShutdown);
  bool sendReadPacketCountersRequest(uint8_t startmodule, uint8_t endmodule);

  bool sendCellVoltageRequest(uint8_t startmodule, uint8_t endmodule);
  bool sendCellTemperatureRequest(uint8_t startmodule, uint8_t endmodule);
  bool sendReadBalancePowerRequest(uint8_t startmodule, uint8_t endmodule);
  bool sendReadBalanceCurrentCountRequest(uint8_t startmodule, uint8_t endmodule);
  bool sendResetPacketCounters();
  bool sendTimingRequest();
  bool sendResetBalanceCurrentCounter();
  bool sendWriteBalanceLevels(uint8_t startmodule, uint8_t endmodule, uint16_t threshold);

  uint16_t queueLength() {
    return _requestq->getCount();
  }

  void ResetCounters()
  {
    packetsGenerated = 0;
  }

  uint32_t packetsGenerated = 0;
  
private:
  cppQueue *_requestq;

  bool pushPacketToQueue(PacketMeta *header);
  inline void setPacketAddress(PacketHeader *header, uint8_t module) {
    setPacketAddressRange(header, module, module);
  }
  void setPacketAddressRange(PacketHeader *header, uint8_t startmodule, uint8_t endmodule);
  void setPacketAddressBroadcast(PacketHeader *header);

  bool BuildAndSendRequest(COMMAND command, uint8_t startmodule, uint8_t endmodule);
  bool BuildAndSendRequest(COMMAND command);
};

#endif
