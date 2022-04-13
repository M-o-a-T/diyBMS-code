/*
____  ____  _  _  ____  __  __  ___
(  _ \(_  _)( \/ )(  _ \(  \/  )/ __)
)(_) )_)(_  \  /  ) _ < )    ( \__ \
(____/(____) (__) (____/(_/\/\_)(___/

DIYBMS V4.0
CELL MODULE FOR ATTINY841

(c)2019/2020 Stuart Pittaway

COMPILE THIS CODE USING PLATFORM.IO

LICENSE
Attribution-NonCommercial-ShareAlike 2.0 UK: England & Wales (CC BY-NC-SA 2.0 UK)
https://creativecommons.org/licenses/by-nc-sa/2.0/uk/

* Non-Commercial — You may not use the material for commercial purposes.
* Attribution — You must give appropriate credit, provide a link to the license, and indicate if changes were made.
  You may do so in any reasonable manner, but not in any way that suggests the licensor endorses you or your use.
* ShareAlike — If you remix, transform, or build upon the material, you must distribute your
  contributions under the same license as the original.
* No additional restrictions — You may not apply legal terms or technological measures
  that legally restrict others from doing anything the license permits.
*/

#include "packet_processor.h"

//Returns TRUE if the internal thermistor is hotter than the required setting (or over max limit)
bool PacketProcessor::BypassOverheatCheck()
{
  uint16_t temp = raw_adc_onboard_temperature;
  if (temp > _config->BypassTemperature)
    return true;
  // safety, not overrideable
  if (temp > DIYBMS_MODULE_SafetyTemperatureCutoff)
    return true;
  return false;
}

//Returns TRUE if the cell voltage is greater than the required setting
bool PacketProcessor::BypassCheck()
{
  uint16_t temp = raw_adc_voltage;

  if (temp >= DIYBMS_MODULE_ProgrammerVoltage*SAMPLEAVERAGING)
    return false;  // we don't "balance" that
  if (bypassThreshold > 0 && temp > bypassThreshold)
    return true;  // temporary limit exceeded
  if (temp > _config->BypassThreshold)
    return true;  // hard limit exceeded

  return false;
}

//Records an ADC reading after the interrupt has finished
void PacketProcessor::ADCReading(uint16_t value)
{
  switch (adcmode)
  {
  case ADC_CELL_VOLTAGE:
#if (SAMPLEAVERAGING == 1)
    raw_adc_voltage = value;
#else
    //Multiple samples - keep history for sample averaging

    // subtract the last reading:
    total = total - readings[readIndex];
    // read from the sensor:
    readings[readIndex] = value;
    // add the reading to the total:
    total = total + value;
    // advance to the next position in the array:
    readIndex++;

    // if we're at the end of the array...
    if (readIndex >= SAMPLEAVERAGING)
    {
      // ...wrap around to the beginning:
      readIndex = 0;
    }

    // calculate the average, and overwrite the "raw" value
    //raw_adc_voltage = total / SAMPLEAVERAGING;
    
    // JB: Keep Full total
    raw_adc_voltage = total ;

#endif

    break;

  case ADC_INTERNAL_TEMP:
#if DIYBMSMODULEVERSION == 420 && defined(SWAPR19R20)
    //R19 and R20 swapped on V4.2 board, invert the thermistor reading
    //Reverted back to 1000 base value to fix issue https://github.com/stuartpittaway/diyBMSv4Code/issues/95
    raw_adc_onboard_temperature = 1000 - value;
#elif DIYBMSMODULEVERSION == 430 && defined(SWAPR19R20)
    //R19 and R20 swapped on V4.3 board (never publically released), invert the thermistor reading
    raw_adc_onboard_temperature = 1000 - value;
#else
    raw_adc_onboard_temperature = value;
#endif
    break;

  case ADC_EXTERNAL_TEMP:
    raw_adc_external_temperature = value;
    break;
  }
}

PacketProcessor::PacketProcessor()
{
#if (SAMPLEAVERAGING > 1)
  for (int thisReading = 0; thisReading < SAMPLEAVERAGING; thisReading++)
  {
    readings[thisReading] = 0;
  }
#endif  
};

//Start an ADC reading via Interrupt
void PacketProcessor::TakeAnAnalogueReading(uint8_t mode)
{
  adcmode = mode;

  switch (adcmode)
  {
  case ADC_CELL_VOLTAGE:
  {
    DiyBMSATTiny841::SelectCellVoltageChannel();
    break;
  }
  case ADC_INTERNAL_TEMP:
  {
    DiyBMSATTiny841::SelectInternalTemperatureChannel();
    break;
  }
  case ADC_EXTERNAL_TEMP:
  {
    DiyBMSATTiny841::SelectExternalTemperatureChannel();
    break;
  }
  default:
    //Avoid taking a reading if we get to here
    return;
  }

  DiyBMSATTiny841::BeginADCReading();
}


// Process the request in the received packet
//command byte
// RRRR CCCC
// X    = 1 bit indicate if packet processed
// R    = 3 bits reserved not used
// C    = 4 bits command (16 possible commands)
void PacketProcessor::onHeaderReceived(PacketHeader *header)
{

  uint8_t addr = header->hops;
  uint8_t more = 0;

  header->hops += 1;

  if(header->start > addr || header->start + header->cells < addr) {
    // Not for us. Just forward.
    serial->sendStartCopy(0);
    return;
  }

  switch (header->command)
  {
  ok:
    header->seen = 1;
    // fall thru
  default:
    // do nothing. Just forward.
    serial->sendStartCopy(more);
    break;

  case COMMAND::Timing:
    more = 0;
    goto ok;

  case COMMAND::Identify:
  case COMMAND::ResetBalanceCurrentCounter:
    more = 0;
    goto ok;

  case COMMAND::ReadBalancePowerPWM:
    more = 1;
    goto ok;

  case COMMAND::ReadPacketCounters:
    more = sizeof(struct PacketReplyCounters);
    goto ok;

  case COMMAND::ReadVoltageAndStatus:
    more = sizeof(struct PacketReplyVoltages);
    goto ok;

  case COMMAND::ReadBalanceCurrentCounter:
    more = sizeof(uint32_t);
    goto ok;

  case COMMAND::ReadTemperature:
    more = sizeof(struct PacketReplyTemperature);
    goto ok;

  case COMMAND::ReadSettings:
    more = sizeof(struct PacketReplySettings);
    goto ok;

  case COMMAND::WriteSettings:
    more = sizeof(struct PacketRequestConfig);
  defer:
    if (header->global) {
      header->seen = 1;
      serial->sendStartCopy(0);
    } else
      serial->sendDefer(more);
    break;
    
  case COMMAND::WriteBalanceLevel:
    more = sizeof(struct PacketRequestBalance);
    goto defer;
  }

  return;
}

// here we have received our data, so assuming that we don't need to write
// any, just start forwarding.
// Sending the new header is delayed until the consumed data have arrived
// because otherwise the delay between header and remaining data
// accumulates, causing a packet timeout.
//
void PacketProcessor::onReadReceived(PacketHeader *header)
{
  // unused for now, as no current command reads and writes data
  uint16_t more=0;
Serial.write('H');
Serial.print(header->command);
Serial.write(' ');
  switch (header->command)
  {
  case COMMAND::WriteSettings:
  case COMMAND::WriteBalanceLevel:
    header->seen = 1;
    // fall through, though TODO actually no other commands should be seen
    // here in the first place
  default:
    serial->sendStartCopy(more);
    break;
  }
}

#define CHECK_LEN(type) \
    do { \
      if(serial->receiveCount() < sizeof(PacketHeader) + sizeof(type)) { \
        serial->sendEndFrame(true); \
        Serial.write("\nM ERR BADLEN\n"); \
        return; \
      } \
    } while(0)

void PacketProcessor::onPacketReceived(PacketHeader *header)
{
  uint8_t addr = header->hops-1;
  // was incremented in `onHeaderReceived`
  if(header->start > addr || header->start + header->cells < addr) {
    // Not for us. Done.
    return;
  }

  badpackets += ((header->sequence - lastSequence) & 0x07) - 1;
  lastSequence = header->sequence;

  switch (header->command)
  {
  case COMMAND::ReadVoltageAndStatus:
    {
      PacketReplyVoltages data ;
      data.voltRaw = raw_adc_voltage;
      if (BypassOverheatCheck())
        data.voltRaw |= 0x4000;
      if (BypassOverheatCheck())
        data.voltRaw |= 0x8000;
      serial->sendBuffer(&data,sizeof(data));
      break;
    }

  case COMMAND::ReadSettings:
    {
      struct PacketReplySettings rcd;
      rcd.dataVersion = SETTINGS_VERSION;
      rcd.mvPerADC = (uint8_t)((float)MV_PER_ADC*(float)64);
      rcd.boardVersion = DIYBMSMODULEVERSION;
      rcd.bypassTempRaw = _config->BypassTemperature;
      rcd.bypassVoltRaw = _config->BypassThreshold;
      rcd.loadResRaw = (uint8_t)(LOAD_RESISTANCE*16);
      rcd.BCoeffInternal = INT_BCOEFFICIENT;
      rcd.BCoeffExternal = EXT_BCOEFFICIENT;
      rcd.numSamples = SAMPLEAVERAGING;
      rcd.voltageCalibration.u = _config->Calibration;
      rcd.gitVersion = GIT_VERSION_B;

      serial->sendBuffer(&rcd,sizeof(rcd));
      break;
    }

  case COMMAND::ReadTemperature:
    {
      struct PacketReplyTemperature data;
      data.intRaw = raw_adc_onboard_temperature;
      data.extRaw = raw_adc_external_temperature;
      serial->sendBuffer(&data, sizeof(data));
      break;
    }

  case COMMAND::ReadBalancePowerPWM:
    serial->sendByte(WeAreInBypass ? PWMSetPoint : 0);
    break;

  case COMMAND::ReadPacketCounters:
    {
      PacketReplyCounters data;
      data.received = PacketReceivedCounter;
      data.bad = badpackets;
      serial->sendBuffer(&data,sizeof(data));
      break;
    }

  case COMMAND::ReadBalanceCurrentCounter:
    {
      uint32_t val3 = MilliAmpHourBalanceCounter;
      serial->sendBuffer(&val3, sizeof(val3));
      break;
    }

  case COMMAND::ResetPacketCounters:
    badpackets = 0;
    PacketReceivedCounter = 0;
    break;

  case COMMAND::Identify:
    //identify module
    //For the next 10 received packets - keep the LEDs lit up
    identifyModule = 10;
    break;

  case COMMAND::WriteSettings:
    {
      CHECK_LEN(PacketRequestConfig);

      serial->sendEndFrame(false);
      Serial.print("\nModSettings");

      struct PacketRequestConfig *data = (PacketRequestConfig *)(header+1);

      if(data->voltageCalibration.u) {
        _config->Calibration = data->voltageCalibration.u;
      Serial.print(" cal=");
      Serial.print(_config->Calibration);
      }

      if(data->bypassTempRaw) {
#if DIYBMSMODULEVERSION == 420 && !defined(SWAPR19R20)
        //Keep temperature low for modules with R19 and R20 not swapped
        if (data->bypassTempRaw > 715) // see main.cpp
          data->bypassTempRaw = 715;
#endif
        _config->BypassTemperature = data->bypassTempRaw;
      Serial.print(" temp=");
      Serial.print(_config->BypassTemperature);
      }

      if (data->bypassVoltRaw) {
        _config->BypassThreshold = data->bypassVoltRaw;
      Serial.print(" volt=");
      Serial.print(_config->BypassThreshold);
      }

      //Save settings
      Settings::WriteConfigToEEPROM((uint8_t *)_config, sizeof(CellModuleConfig), EEPROM_CONFIG_ADDRESS);

      SettingsHaveChanged = true;
      Serial.println(" saved");

      break;
    }

  case COMMAND::ResetBalanceCurrentCounter:
    MilliAmpHourBalanceCounter = 0;
    break;

  case COMMAND::WriteBalanceLevel:
    {
      CHECK_LEN(PacketRequestBalance);
      struct PacketRequestBalance *data = (PacketRequestBalance *)(header+1);
    
      bypassThreshold = data->voltageRaw;
      break;
    }
  }

  return;
}

