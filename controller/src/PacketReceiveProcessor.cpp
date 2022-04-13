#include "PacketReceiveProcessor.h"

bool PacketReceiveProcessor::HasCommsTimedOut()
{
  //We timeout the comms if we don't receive a packet within 3 times the normal
  //round trip time of the packets through the modules (minimum of 10 seconds to cater for low numbers of modules)
  uint32_t millisecondSinceLastPacket = millis() - packetLastReceivedMillisecond;
  return ((millisecondSinceLastPacket > 5 * packetTimerMillisecond) && (millisecondSinceLastPacket > 10000));
}

bool PacketReceiveProcessor::ProcessReply(PacketMeta *meta)
{
  packetsReceived++;
  bool processed = true;

  _meta = meta;
  _header = (PacketHeader *)(_meta+1);
  uint16_t len = _meta->dataLen;

  if(len < sizeof(struct PacketHeader)) {
    processed = false;
    _header = nullptr;
    _dataLen = 0;
  }
  if(processed) {
    // the header is OK
    _dataLen = len-sizeof(struct PacketHeader);
    packetLastReceivedMillisecond = millis();

    totalModulesFound = _header->hops;

    // The sequence number wraps.
    if (((_header->sequence - packetLastReceivedSequence) & 0x07) != 1)
    {
      SERIAL_DEBUG.println();
      SERIAL_DEBUG.print(F("OOS Error, expected="));
      SERIAL_DEBUG.print((packetLastReceivedSequence + 1)&0x7, HEX);
      SERIAL_DEBUG.print(", got=");
      SERIAL_DEBUG.println(_header->sequence, HEX);
      totalOutofSequenceErrors++;
    }

    packetLastReceivedSequence = _header->sequence;

    if (!_header->seen)
      processed = false;
  }
  if (processed)
  {
    //ESP_LOGD(TAG, "Hops %u, start %u end %u, command=%u", _header->hops, _header->start, _header->start+_header->cells+1,_header->command);

    switch (_header->command)
    {
    case COMMAND::ResetPacketCounters:
    case COMMAND::WriteBalanceLevel:
      break; // Ignore reply

    case COMMAND::Timing:
    {
      if(_dataLen < sizeof(uint32_t))
      {
        processed = false;
        break;
      }
      uint32_t tprevious = *(uint32_t *)(_header+1);

      packetTimerMillisecond = _meta->timestamp - tprevious;
      break;
    }

    case COMMAND::ReadVoltageAndStatus:
      processed = ProcessReplyVoltage();

      //ESP_LOGD(TAG, "Updated volt status cells %u to %u", _header->start, _header->start+buffer->h.cells+1);

#ifdef ESP32
      //TODO: REVIEW THIS LOGIC
      if (_header->start+_header->cells+1 == _header->hops)
      {
        //We have just processed a voltage reading for the entire chain of modules (all banks)
        //at this point we should update any display or rules logic
        //as we have a clean snapshot of voltages and statues

        ESP_LOGD(TAG, "Finished all reads");
        if (voltageandstatussnapshot_task_handle != NULL)
        {
          xTaskNotify(voltageandstatussnapshot_task_handle, 0x00, eNotifyAction::eNoAction);
        }
      }
#endif
      break;

    case COMMAND::Identify:
      break; // Ignore reply

    case COMMAND::ReadTemperature:
      processed = ProcessReplyTemperature();
      break;

    case COMMAND::ReadSettings:
      processed = ProcessReplySettings();
      break;

    case COMMAND::ReadBalancePowerPWM:
      processed = ProcessReplyBalancePower();
      break;

    case COMMAND::ReadBalanceCurrentCounter:
      processed = ProcessReplyReadBalanceCurrentCounter();
      break;

    case COMMAND::ReadPacketCounters:
      processed = ProcessReplyPacketCounters();
      break;
    }

#if defined(PACKET_LOGGING_RECEIVE)
    SERIAL_DEBUG.println(F("*OK*"));
#endif

  }
  else
  {
    //Error count for a request that was not processed by any module in the string
    totalNotProcessedErrors++;
    // ESP_LOGD(TAG, "Modules ignored request");
  }

  freePacket(_meta);
  return processed;
}

#define LOOP(_typ)                                              \
  if(isShortPacket(sizeof(_typ)))                               \
    return false;                                               \
  _typ *data = reinterpret_cast<_typ *>(_header+1);             \
  CellModuleInfo *cell = &cmi[_header->start];                  \
  for(uint16_t i = 0; i <= _header->cells; data++,cell++,i++)

bool PacketReceiveProcessor::ProcessReplyTemperature()
{
  LOOP(PacketReplyTemperature) {
    cell->internalTemp = cell->ThermistorToCelsius(data->intRaw);
    cell->externalTemp = cell->ThermistorToCelsiusExt(data->extRaw);
  }
  return true;
}

bool PacketReceiveProcessor::ProcessReplyReadBalanceCurrentCounter()
{
  LOOP(uint32_t) {
    uint32_t current = *data;

    if(cell->LoadResistance != 0)
      // the raw value is the cell voltage ADC * voltageSamples, added up once per millisecond.
      // Thus here we divide by resistance and 1000*3600 (msec in an hour) to get mAh.
      cell->BalanceCurrentCount = (uint16_t)(cell->RawTomV(current)/cell->LoadResistance/3600000.0 +0.5);
    else
      cell->BalanceCurrentCount = 0;
  }
  return true;
}

bool PacketReceiveProcessor::ProcessReplyPacketCounters()
{
  LOOP(PacketReplyCounters) {
    cell->PacketReceivedCount = data->received;
    cell->badPacketCount = data->bad;
  }

  return true;
}

bool PacketReceiveProcessor::ProcessReplyBalancePower()
{
  LOOP(uint8_t) {
    cell->PWMValue = *data;
  }
  return true;
}

bool PacketReceiveProcessor::ProcessReplyVoltage()
{
  LOOP(PacketReplyVoltages) {
    // bit 15 = In bypass
    // bit 14 = Bypass over temperature
    // bit 13 = free
    // bit 12,11,10: required for up to 8 samples

    uint16_t val = data->voltRaw;
    cell->inBypass = (val & 0x8000) > 0;
    cell->bypassOverTemp = (val & 0x4000) > 0;

    val &= 0x1FFF;
    float mV = cell->RawTomV(val);
    cell->voltagemV = mV;
    if(mV) {
      if (mV > cell->voltagemVMax)
        cell->voltagemVMax = mV;
      if (mV < cell->voltagemVMin)
        cell->voltagemVMin = mV;

      cell->valid = true;
    }
    
    cell->BypassCurrentThresholdmV = cell->RawTomV(data->bypassRaw);
  }
  return true;
}

bool PacketReceiveProcessor::ProcessReplySettings()
{
  LOOP(PacketReplySettings) {
    if(data->dataVersion > SETTINGS_VERSION) {
        // ugh. We don't know anything. TODO: Mark this module as bad.
        return false;
    }
    if(data->dataVersion < SETTINGS_VERSION) {
        // TODO handle old protocol versions (assuming there are any)
        return false;
    }
    cell->mVPerADC = (float)data->mvPerADC / (float)64;
    cell->BoardVersionNumber = data->boardVersion;
    cell->voltageSamples = data->numSamples;
    cell->LoadResistance = (float)data->loadResRaw / 16.0;
    cell->Calibration = data->voltageCalibration.f;
    cell->Internal_BCoefficient = data->BCoeffInternal;
    cell->External_BCoefficient = data->BCoeffExternal;
    cell->CodeVersionNumber = data->gitVersion;
    // calculations depend on previous values, so 
    cell->BypassConfigThresholdmV = cell->RawTomV(data->bypassVoltRaw);
    cell->BypassMaxTemp = cell->ThermistorToCelsius(data->bypassTempRaw);

    cell->settingsCached = true;
  }
  return true;
}

bool PacketReceiveProcessor::isShortPacket(uint16_t len)
{
  if(_meta->dataLen < (_header->cells+1) * len)
    return true;
  return false;
}
