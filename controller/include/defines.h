#ifndef DIYBMS_DEFINES_H_
#define DIYBMS_DEFINES_H_

#include <Arduino.h>

#ifdef ESP8266
#include <uart.h>
#else // ESP32
#include <driver/uart.h>
#endif

#include "EmbeddedFiles_Defines.h"

#include "EmbeddedFiles_Integrity.h"

#include <common.h>

#include <Steinhart.h>

//#define TOUCH_SCREEN

#ifdef ESP8266
#define SERIAL_DATA Serial
#define SERIAL_DEBUG Serial1
#else // ESP32
#define SERIAL_DATA Serial2
#define SERIAL_DEBUG Serial
#define SERIAL_RS485 Serial1
#endif

//Total number of cells a single controler can handle (memory limitation)
#define maximum_controller_cell_modules 128

#ifdef ESP32

enum RGBLED : uint8_t
{
  OFF = 0,
  Blue = B00000001,
  Red = B00000010,
  Purple = B00000011,
  Green = B00000100,
  Cyan = B00000101,
  Yellow = B00000110,
  White = B00000111
};

enum VictronDVCC : uint8_t
{
  Default = 0,
  Balance = 1,
  ControllerError = 2
};

#endif // ESP32

//Maximum of 16 cell modules: number of cells to process in a single packet of data
#define maximum_cell_modules_per_packet 16

//Maximum number of banks allowed
//This also needs changing in default.htm (MAXIMUM_NUMBER_OF_BANKS)
#define maximum_number_of_banks 16

//Version 4.XX of DIYBMS modules operate at 5000 baud (since 26 Jan 2021)
//#define COMMS_BAUD_RATE 5000

#ifdef ESP8266
#define EEPROM_SETTINGS_START_ADDRESS 256
#endif

enum enumInputState : uint8_t
{
  INPUT_HIGH = 0xFF,
  INPUT_LOW = 0x99,
  INPUT_UNKNOWN = 0x00
};

struct i2cQueueMessage
{
  uint8_t command;
  uint8_t data;
};

enum RelayState : uint8_t
{
  RELAY_ON = 0xFF,
  RELAY_OFF = 0x99,
  RELAY_X = 0x00
};

enum RelayType : uint8_t
{
  RELAY_STANDARD = 0x00,
  RELAY_PULSE = 0x01
};

#ifdef ESP32
enum CurrentMonitorDevice : uint8_t
{
  DIYBMS_CURRENT_MON = 0x00,
  PZEM_017 = 0x01
};
#endif

//Number of rules as defined in Rules.h (enum Rule)
#ifdef ESP32
#define RELAY_RULES 15
#else
#define RELAY_RULES 12
#endif

//Number of relays on board (4)
#define RELAY_TOTAL 4

//inputs on board
#ifdef ESP32
#define INPUTS_TOTAL 7
#else
#define INPUTS_TOTAL 5
#endif

#define SHOW_TIME_PERIOD 5000
#define NTP_TIMEOUT 1500

struct diybms_eeprom_settings
{
  // TODO add version number and size here
  uint8_t totalNumberOfBanks;
  uint8_t totalNumberOfSeriesModules;
  uint16_t baudRate;
  uint16_t interpacketgap;

  uint32_t rulevalue[RELAY_RULES];
  uint32_t rulehysteresis[RELAY_RULES];

  //Use a bit pattern to indicate the relay states
  RelayState rulerelaystate[RELAY_RULES][RELAY_TOTAL];
  //Default starting state
  RelayState rulerelaydefault[RELAY_TOTAL];
  //Default starting state for relay types
  RelayType relaytype[RELAY_TOTAL];

  float graph_voltagehigh;
  float graph_voltagelow;

  uint8_t BypassMaxTemp;
  uint16_t BypassThresholdmV;

  int8_t timeZone;        // = 0;
  int8_t minutesTimeZone; // = 0;
  bool daylight;          //=false;
  char ntpServer[64 + 1]; // = "time.google.com";

  bool loggingEnabled;
  uint16_t loggingFrequencySeconds;

#ifdef ESP32
  bool currentMonitoringEnabled;
  uint8_t currentMonitoringModBusAddress;
  CurrentMonitorDevice currentMonitoringDevice;

  int rs485baudrate;
  uart_word_length_t rs485databits;
  uart_parity_t rs485parity;
  uart_stop_bits_t rs485stopbits;

  char language[2 + 1];

  uint16_t cvl[3];
  int16_t ccl[3];
  int16_t dcl[3];

  bool VictronEnabled;
#endif // ESP32

  //NOTE this array is subject to buffer overflow vulnerabilities!
  bool mqtt_enabled;
  uint16_t mqtt_port;
  char mqtt_server[64 + 1];
  char mqtt_topic[32 + 1];
  char mqtt_username[32 + 1];
  char mqtt_password[32 + 1];

  bool influxdb_enabled;
  //uint16_t influxdb_httpPort;
#ifdef ESP32
  char influxdb_serverurl[128 + 1];
  char influxdb_databasebucket[64 + 1];
  char influxdb_apitoken[128 + 1];
  char influxdb_orgid[128 + 1];
  uint8_t influxdb_loggingFreqSeconds;
#else
  uint16_t influxdb_httpPort;
  char influxdb_host[64 + 1];
  char influxdb_database[32 + 1];
  char influxdb_user[32 + 1];
  char influxdb_password[32 + 1];
#endif
};

class CellModuleInfo
{
public:
  CellModuleInfo() {}

  //Used as part of the enquiry functions
  bool settingsCached : 1;
  //Set to true once the module has replied with data
  bool valid : 1;
  //Bypass is active
  bool inBypass : 1;
  //Bypass active and temperature over set point
  bool bypassOverTemp : 1;

  uint16_t voltagemV;
  uint16_t voltagemVMin;
  uint16_t voltagemVMax;
  //Signed integer byte (temperatures in degC, may be negative)
  int8_t internalTemp;
  int8_t externalTemp;

  // raw voltage readings are scaled up by this factor (N-average)
  uint8_t voltageSamples;
  // degC, assumed to be >0 :-)
  uint8_t BypassMaxTemp;
  // temporary bypass threshold. Not yet used
  uint16_t BypassCurrentThresholdmV;
  // absolute bypass threshold
  uint16_t BypassConfigThresholdmV;

  // Resistance of bypass load
  float LoadResistance;
  //Voltage Calibration
  float Calibration;
  //Reference voltage (millivolt) normally 2.00mV
  float mVPerADC;
  //Internal Thermistor settings
  uint16_t Internal_BCoefficient;
  //External Thermistor settings
  uint16_t External_BCoefficient;
  //Version number returned by code of module
  uint16_t BoardVersionNumber;
  //First 4 bytes of GITHUB version
  uint32_t CodeVersionNumber;
  //Value of PWM timer for load shedding
  uint16_t PWMValue;

  // mAh since last reset
  uint16_t BalanceCurrentCount;
  // good packets
  uint16_t PacketReceivedCount;
  // bad packets, for whatever reason
  uint16_t badPacketCount;

  // conversion functions
  inline int8_t ThermistorToCelsius(uint16_t raw) {
    if(Internal_BCoefficient == 0) return -100;
    if(raw == 0) return -100;
    return Steinhart::ThermistorToCelsius(Internal_BCoefficient, raw);
  }

  // conversion functions
  inline int8_t ThermistorToCelsiusExt(uint16_t raw) {
    if(External_BCoefficient == 0) return -100;
    if(raw == 0) return -100;
    return Steinhart::ThermistorToCelsius(External_BCoefficient, raw);
  }

  inline uint16_t CelsiusToThermistor(int8_t degC) {
    if(Internal_BCoefficient == 0) return 0;
    if(degC < -99) return 0;
    return Steinhart::CelsiusToThermistor(Internal_BCoefficient, degC);
  }

  inline uint16_t mVToRaw(float mV) {
    if(mV == 0) return 0;
    if(mVPerADC == 0) return 0;
    if(voltageSamples == 0) return 0;
    if(Calibration == 0) return 0;
    return mV/mVPerADC*voltageSamples/Calibration;
  }

  inline float RawTomV(uint16_t raw) {
    if(raw == 0) return 0;
    if(mVPerADC == 0) return 0;
    if(voltageSamples == 0) return 0;
    if(Calibration == 0) return 0;
    return raw*mVPerADC/voltageSamples*Calibration;
  }
};

// This enum holds the states the controller goes through whilst
// it stabilizes and moves into running state.
enum ControllerState : uint8_t
{
  Unknown = 0,
  PowerUp = 1,
  Stabilizing = 2,
  ConfigurationSoftAP = 3,
  Running = 255,
};

struct sdcard_info
{
  bool available;
  uint32_t totalkilobytes;
  uint32_t usedkilobytes;
  uint32_t flash_totalkilobytes;
  uint32_t flash_usedkilobytes;
};

//This holds all the cell information in a large array array
extern CellModuleInfo cmi[maximum_controller_cell_modules];

#ifdef ESP32

struct avrprogramsettings
{
  uint8_t efuse;
  uint8_t hfuse;
  uint8_t lfuse;
  uint32_t mcu;
  uint32_t duration;
  bool inProgress;
  char filename[64];
  uint8_t progresult;
  size_t programsize;

  bool programmingModeEnabled;
};

struct currentmonitor_raw_modbus {
  //These variables are in STRICT order
  //and must match the MODBUS register sequence and data types!!

  //Voltage
  float voltage;
  //Current in AMPS
  float current;
  uint32_t milliamphour_out;
  uint32_t milliamphour_in;
  int16_t temperature;
  uint16_t flags;
  float power;
  float shuntmV;
  float currentlsb;
  float shuntresistance;
  uint16_t shuntmaxcurrent;
  uint16_t shuntmillivolt;
  uint16_t batterycapacityamphour;
  float fullychargedvoltage;
  float tailcurrentamps;
  uint16_t raw_chargeefficiency;
  uint16_t raw_stateofcharge;
  uint16_t shuntcal;
  int16_t temperaturelimit;
  float overvoltagelimit;
  float undervoltagelimit;
  float overcurrentlimit;
  float undercurrentlimit;
  float overpowerlimit;
  uint16_t shunttempcoefficient;
  uint16_t modelnumber;
  uint32_t firmwareversion;
  uint32_t firmwaredatetime;
  uint16_t watchdogcounter;
}__attribute__((packed));

struct currentmonitoring_struct
{
  currentmonitor_raw_modbus modbus;

  //Uses float as these are 4 bytes on ESP32
  int64_t timestamp;
  bool validReadings;

  float chargeefficiency;
  float stateofcharge;

  bool TemperatureOverLimit : 1;
  bool CurrentOverLimit : 1;
  bool CurrentUnderLimit : 1;
  bool VoltageOverlimit : 1;
  bool VoltageUnderlimit : 1;
  bool PowerOverLimit : 1;
  bool TempCompEnabled : 1;
  bool ADCRange4096mV : 1;

  bool RelayTriggerTemperatureOverLimit : 1;
  bool RelayTriggerCurrentOverLimit : 1;
  bool RelayTriggerCurrentUnderLimit : 1;
  bool RelayTriggerVoltageOverlimit : 1;
  bool RelayTriggerVoltageUnderlimit : 1;
  bool RelayTriggerPowerOverLimit : 1;
  bool RelayState : 1;
};


/*
struct currentmonitoring_struct
{
  //Uses float as these are 4 bytes on ESP32
  int64_t timestamp;
  bool validReadings;
  //Voltage
  float voltage;
  //Current in AMPS
  float current;
  uint32_t milliamphour_out;
  uint32_t milliamphour_in;
  int16_t temperature;
  uint16_t watchdogcounter;
  float power;
  float shuntmV;
  float currentlsb;
  float shuntresistance;
  uint16_t shuntmaxcurrent;
  uint16_t shuntmillivolt;
  uint16_t shuntcal;
  int16_t temperaturelimit;
  float undervoltagelimit;
  float overvoltagelimit;

  float overpowerlimit;
  float overcurrentlimit;
  float undercurrentlimit;
  uint16_t shunttempcoefficient;
  uint16_t modelnumber;
  uint32_t firmwareversion;
  uint32_t firmwaredatetime;

  bool TemperatureOverLimit : 1;
  bool CurrentOverLimit : 1;
  bool CurrentUnderLimit : 1;
  bool VoltageOverlimit : 1;
  bool VoltageUnderlimit : 1;
  bool PowerOverLimit : 1;
  bool TempCompEnabled : 1;
  bool ADCRange4096mV : 1;

  bool RelayTriggerTemperatureOverLimit : 1;
  bool RelayTriggerCurrentOverLimit : 1;
  bool RelayTriggerCurrentUnderLimit : 1;
  bool RelayTriggerVoltageOverlimit : 1;
  bool RelayTriggerVoltageUnderlimit : 1;
  bool RelayTriggerPowerOverLimit : 1;
  bool RelayState : 1;

  uint16_t batterycapacityamphour;
  float tailcurrentamps;
  float fullychargedvoltage;
  float chargeefficiency;

  float stateofcharge;
};
*/

enum DIAG_ALRT_FIELD : uint16_t
{
  ALATCH = 15,
  CNVR = 14,
  SLOWALERT = 13,
  APOL = 12,
  ENERGYOF = 11,
  CHARGEOF = 10,
  MATHOF = 9,
  RESERVED = 8,
  TMPOL = 7,
  SHNTOL = 6,
  SHNTUL = 5,
  BUSOL = 4,
  BUSUL = 3,
  POL = 2,
  CNVRF = 1,
  MEMSTAT = 0
};

#endif // ESP32

#endif // guard
