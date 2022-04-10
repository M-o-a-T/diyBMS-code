#include "PacketRequestGenerator.h"
#include "common.h"
#include "DIYBMSServer.h"

bool PacketRequestGenerator::sendSaveGlobalSetting(uint16_t BypassThresholdmV, uint8_t BypassOverTempShutdown)
{
  PacketMeta *meta = allocatePacket(sizeof(PacketRequestConfig) * mysettings.totalNumberOfSeriesModules);
  PacketHeader *header = (PacketHeader *)(meta+1);
  PacketRequestConfig *data = (PacketRequestConfig *)(header+1);

  //Ask all modules to set bypass and temperature value
  setPacketAddressBroadcast(header);
  header->command = COMMAND::WriteSettings;

  CellModuleInfo *cell = &cmi[0];
  for(; cell != &cmi[mysettings.totalNumberOfSeriesModules]; data++,cell++) {
    // Force refresh of settings
    cell->settingsCached = false;

    data->calibration = *reinterpret_cast<uint32_t *>((void *)&cell->Calibration);
    data->bypassTemp = cell->CelsiusToThermistor(cell->BypassMaxTemp);
    data->bypassThresh = cell->mVToRaw(cell->BypassConfigThresholdmV);
  }

  return pushPacketToQueue(meta);
}

bool PacketRequestGenerator::sendSaveSetting(uint8_t m, uint16_t BypassThresholdmV, uint8_t BypassOverTempShutdown, float Calibration)
{
  PacketMeta *meta = allocatePacket(sizeof(PacketRequestConfig));
  PacketHeader *header = (PacketHeader *)(meta+1);
  PacketRequestConfig *data = (PacketRequestConfig *)(header+1);

  CellModuleInfo *cell = &cmi[m];

  setPacketAddress(header, m);
  header->command = COMMAND::WriteSettings;


  // Force refresh of settings
  cell->settingsCached = false;

  data->calibration = *reinterpret_cast<uint32_t *>((void *)&cell->Calibration);
  data->bypassTemp = cell->CelsiusToThermistor(cell->BypassMaxTemp);
  data->bypassThresh = cell->mVToRaw(cell->BypassConfigThresholdmV);

  return pushPacketToQueue(meta);
}

bool PacketRequestGenerator::sendCellVoltageRequest(uint8_t startmodule, uint8_t endmodule)
{
  return BuildAndSendRequest(COMMAND::ReadVoltageAndStatus, startmodule, endmodule);
}

bool PacketRequestGenerator::sendIdentifyModuleRequest(uint8_t cellid)
{
  return BuildAndSendRequest(COMMAND::Identify, cellid, cellid);
}

bool PacketRequestGenerator::sendTimingRequest()
{
  //Ask all modules to simple pass on a NULL request/packet for timing purposes
  return BuildAndSendRequest(COMMAND::Timing);
}

bool PacketRequestGenerator::sendGetSettingsRequest(uint8_t cellid)
{
  return BuildAndSendRequest(COMMAND::ReadSettings, cellid, cellid);
}

bool PacketRequestGenerator::sendCellTemperatureRequest(uint8_t startmodule, uint8_t endmodule)
{
  return BuildAndSendRequest(COMMAND::ReadTemperature, startmodule, endmodule);
}

bool PacketRequestGenerator::sendReadBalanceCurrentCountRequest(uint8_t startmodule, uint8_t endmodule)
{
  return BuildAndSendRequest(COMMAND::ReadBalanceCurrentCounter, startmodule, endmodule);
}

bool PacketRequestGenerator::sendReadPacketCountersRequest(uint8_t startmodule, uint8_t endmodule)
{
  return BuildAndSendRequest(COMMAND::ReadPacketCounters, startmodule, endmodule);
}

bool PacketRequestGenerator::sendReadBalancePowerRequest(uint8_t startmodule, uint8_t endmodule)
{
  return BuildAndSendRequest(COMMAND::ReadBalancePowerPWM, startmodule, endmodule);
}

bool PacketRequestGenerator::sendResetPacketCounters()
{
  return BuildAndSendRequest(COMMAND::ResetPacketCounters);
}
bool PacketRequestGenerator::sendResetBalanceCurrentCounter()
{
  return BuildAndSendRequest(COMMAND::ResetBalanceCurrentCounter);
}

bool PacketRequestGenerator::BuildAndSendRequest(COMMAND command)
{
  //ESP_LOGD(TAG,"Build %u",command);

  PacketMeta *meta = allocatePacket(0);
  PacketHeader *header = (PacketHeader *)(meta+1);

  setPacketAddressBroadcast(header);
  header->command = command;
  return pushPacketToQueue(meta);
}

bool PacketRequestGenerator::BuildAndSendRequest(COMMAND command, uint8_t startmodule, uint8_t endmodule)
{
  //ESP_LOGD(TAG,"Build %u, %u to %u",command,startmodule,endmodule);

  PacketMeta *meta = allocatePacket(0);
  PacketHeader *header = (PacketHeader *)(meta+1);

  setPacketAddressRange(header, startmodule, endmodule);
  header->command = command;
  return pushPacketToQueue(meta);
}

bool PacketRequestGenerator::pushPacketToQueue(PacketMeta *meta)
{
  // always consumes the packet
  if (!_requestq->push(&meta)) {
    //ESP_LOGE(TAG,"Queue full");
    freePacket(meta);
    return false;
  }

  packetsGenerated++;
  return true;
}

void PacketRequestGenerator::setPacketAddressRange(PacketHeader *header, uint8_t startmodule, uint8_t endmodule)
{
  header->start = startmodule;
  header->cells = endmodule-startmodule;
}

void PacketRequestGenerator::setPacketAddressBroadcast(PacketHeader *header)
{
  setPacketAddressRange(header, 0, maximum_controller_cell_modules);
}

