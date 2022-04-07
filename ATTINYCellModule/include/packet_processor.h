#ifndef DIYBMS_PACKETPROCESSOR_H // include guard
#define DIYBMS_PACKETPROCESSOR_H

#include <Arduino.h>
#include <SerialPacker.h>

#include "diybms_attiny841.h"
#include "defines.h"
#include "settings.h"

#define ADC_CELL_VOLTAGE 0
#define ADC_INTERNAL_TEMP 1
#define ADC_EXTERNAL_TEMP 2

//Define maximum allowed temperature as safety cut off
#define DIYBMS_MODULE_SafetyTemperatureCutoff 940 // ~90 degC for B~4000

#define MAX_PACKET_SIZE 16


//NOTE THIS MUST BE EVEN IN SIZE (BYTES) ESP8266 IS 32 BIT AND WILL ALIGN AS SUCH!
struct PacketHeader
{
  uint8_t start;
  unsigned int _reserved:3;
  unsigned int seen:1;
  unsigned int command:4;
  uint8_t hops;
  unsigned int cells:5;
  unsigned int sequence:3;
} __attribute__((packed));

struct PacketStruct
{
  struct PacketHeader h;
  uint16_t data[MAX_PACKET_SIZE/2+1];
} __attribute__((packed));

#if MAX_PACKET_SIZE < 10
#error Config data size is 6 bytes, plus 4 bytes header. Increase MAX_PACKET_SIZE.
#endif
class PacketProcessor
{
public:
  PacketProcessor(CellModuleConfig *config, SerialPacker *packer) : PacketProcessor()
  {
    _config = config;
    serial = packer;
    SettingsHaveChanged = false;
  }
  ~PacketProcessor() {}

  void onHeaderReceived(PacketStruct *receivebuffer);
  void onReadReceived(PacketStruct *receivebuffer);
  void onPacketReceived(PacketStruct *receivebuffer);

  void ADCReading(uint16_t value);
  void TakeAnAnalogueReading(uint8_t mode);
  uint16_t CellVoltage();

  uint16_t IncrementWatchdogCounter()
  {
    watchdog_counter++;
    return watchdog_counter;
  }

  SerialPacker *serial;

  bool BypassCheck();
  uint16_t TemperatureMeasurement();
  uint8_t identifyModule;
  bool BypassOverheatCheck();

  int16_t InternalTemperature() {
    return raw_adc_onboard_temperature;
  }

  volatile uint32_t MilliAmpHourBalanceCounter = 0;

  //Returns TRUE if the module is in "bypassing current" mode
  bool WeAreInBypass = false;

  //Value of PWM 0-255
  volatile uint8_t PWMSetPoint;
  volatile bool SettingsHaveChanged;

  //Count down which runs whilst bypass is in operation,  zero = bypass stopped/off
  uint16_t bypassCountDown = 0;

  //Count down which starts after the current cycle of bypass has completed (aka cool down period whilst voltage may rise again)
  uint8_t bypassHasJustFinished = 0;

  bool IsBypassActive()
  {
    return WeAreInBypass || bypassHasJustFinished > 0;
  }
  void IncrementBalanceCounter(uint16_t val)
  {
    MilliAmpHourBalanceCounter += raw_adc_voltage * val;
  }

private:
  PacketProcessor();

  CellModuleConfig *_config;

  uint16_t bypassThreshold = 0;
  uint8_t lastSequence = 0;

  bool processPacket(PacketStruct *buffer);

  volatile uint8_t adcmode = 0;
  volatile uint16_t raw_adc_voltage;
  volatile uint16_t raw_adc_onboard_temperature;
  volatile uint16_t raw_adc_external_temperature;

  //Count of bad packets of data received, most likely with corrupt data or crc errors
  uint16_t badpackets = 0;
  //Count of number of WDT events which have triggered, could indicate standalone mode or problems with serial comms
  uint16_t watchdog_counter = 0;

  uint16_t PacketReceivedCounter = 0;

#if SAMPLEAVERAGING > 1
  uint16_t readings[SAMPLEAVERAGING]; // the readings from the analog input
  uint16_t readIndex = 0;             // the index of the current reading
  uint16_t total = 0;                 // the running total
  uint16_t average = 0;               // the average
#endif
};

#endif
