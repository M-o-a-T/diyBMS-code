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

  if (bypassThreshold > 0 && temp > bypassThreshold)
    return true;
  if (temp > _config->BypassThreshold)
    return true;

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
#if (defined(DIYBMSMODULEVERSION) && (DIYBMSMODULEVERSION == 420 && defined(SWAPR19R20)))
    //R19 and R20 swapped on V4.2 board, invert the thermistor reading
    //Reverted back to 1000 base value to fix issue https://github.com/stuartpittaway/diyBMSv4Code/issues/95
    raw_adc_onboard_temperature = 1000 - value;
#elif (defined(DIYBMSMODULEVERSION) && (DIYBMSMODULEVERSION == 430 && defined(SWAPR19R20)))
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
void PacketProcessor::onHeaderReceived(PacketStruct *buffer)
{

  uint8_t addr = buffer->h.hops;
  uint8_t more = 0;

  buffer->h.hops += 1;

  if(buffer->h.start > addr || buffer->h.start + buffer->h.cells < addr) {
    // Not for us. Just forward.
    serial->sendStartCopy(0);
    return;
  }

  switch (buffer->h.command)
  {
  case COMMAND::Timing:
  case COMMAND::Identify:
  case COMMAND::ResetBalanceCurrentCounter:
  ok:
    buffer->h.seen = 1;
    // fall thru
  default:
    // do nothing. Just forward.
  copy:
    serial->sendStartCopy(more);
    break;

  case COMMAND::ReadBalancePowerPWM:
    more = 1;
    goto ok;

  case COMMAND::ReadBadPacketCounter:
  case COMMAND::ReadPacketReceivedCounter:
    more = 2;
    goto ok;

  case COMMAND::ReadVoltageAndStatus:
  case COMMAND::ReadBalanceCurrentCounter:
    more = 4;
    goto ok;

  case COMMAND::ReadTemperature:
    more = 3;
    goto ok;

  case COMMAND::ReadSettings:
    more = sizeof(struct ReadConfigData);
    goto ok;

  case COMMAND::WriteSettings:
    more = 6;
  defer:
    serial->sendDefer(more);
    break;
    
  case COMMAND::WriteBalanceLevel:
    more = 4;
    goto defer;
  }

  return;
}

// here we have received our data, so assuming that we don't need to write
// any, just start forwarding.
//
// Reminder: Processing the data may only be done in `processPacket`!
void PacketProcessor::onReadReceived(PacketStruct *buffer)
{
  switch (buffer->h.command & 0x0F)
  {
  case COMMAND::WriteSettings:
  case COMMAND::WriteBalanceLevel:
    buffer->h.seen = 1;
    // fall through
  default:
    serial->sendStartFrame(0);
    break;
  }
}

void PacketProcessor::onPacketReceived(PacketStruct *buffer)
{
  uint8_t addr = buffer->h.hops;
  badpackets += ((buffer->h.sequence - lastSequence) & 0x07) - 1;
  lastSequence = buffer->h.sequence;

  if(buffer->h.start > addr || buffer->h.start + buffer->h.cells < addr) {
    // Not for us. Done.
    return;
  }

  uint16_t val;
  switch (buffer->h.command)
  {
  case COMMAND::ReadVoltageAndStatus:
    val = raw_adc_voltage;
    if (BypassOverheatCheck())
      val |= 0x4000;
    if (BypassOverheatCheck())
      val |= 0x8000;
    serial->sendBuffer(&val,sizeof(val));

    val = bypassThreshold;
  xmit2:
    serial->sendBuffer(&val,sizeof(val));
    break;

  case COMMAND::ReadSettings:
    {
      struct ReadConfigData rcd;
      rcd.boardVersion = DIYBMSMODULEVERSION;
      rcd.bypassTemp = _config->BypassTemperature;
      rcd.bypassThreshold = _config->BypassThreshold;
      rcd.loadResistance = (uint8_t)(LOAD_RESISTANCE*16);
      rcd.numSamples = SAMPLEAVERAGING;
      rcd.voltageCalibration = _config->Calibration;
      rcd.gitVersion = GIT_VERSION_B;

      serial->sendBuffer(&rcd,sizeof(rcd));
      break;
    }

  case COMMAND::ReadTemperature:
    {
      uint16_t val1 = raw_adc_onboard_temperature;
      uint16_t val2 = raw_adc_external_temperature;
      serial->sendByte(val1 >> 4);
      serial->sendByte((val1 << 4) | (val2 >> 8));
      serial->sendByte(val2);
      break;
    }

  case COMMAND::ReadBalancePowerPWM:
    serial->sendByte(WeAreInBypass ? PWMSetPoint : 0);
    break;

  case COMMAND::ReadBadPacketCounter:
    val = badpackets;
    goto xmit2;

  case COMMAND::ReadBalanceCurrentCounter:
    {
      uint32_t val3 = MilliAmpHourBalanceCounter;
      serial->sendBuffer(&val3, sizeof(val3));
      break;
    }

  case COMMAND::ReadPacketReceivedCounter:
    val = PacketReceivedCounter;
    goto xmit2;

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
    
    val = buffer->data[0];
    if(val)
      _config->Calibration = val;

    val = buffer->data[1];
    if(val) {
#if defined(DIYBMSMODULEVERSION) && (DIYBMSMODULEVERSION == 420 && !defined(SWAPR19R20))
      //Keep temperature low for modules with R19 and R20 not swapped
      if (val > 715) // see main.cpp
        val = 715;
#endif
      _config->BypassTemperature = val;
    }

    val = buffer->data[2];
    if (val)
      _config->BypassThreshold = val;

    //Save settings
    Settings::WriteConfigToEEPROM((uint8_t *)_config, sizeof(CellModuleConfig), EEPROM_CONFIG_ADDRESS);

    SettingsHaveChanged = true;

    break;

  case COMMAND::ResetBalanceCurrentCounter:
    MilliAmpHourBalanceCounter = 0;
    break;

  case COMMAND::WriteBalanceLevel:
    bypassThreshold = buffer->data[0];
    break;
  }

  return;
}

