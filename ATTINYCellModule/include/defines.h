/*
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

#include "EmbeddedFiles_Defines.h"

#ifndef DIYBMS_DEFINES_H // include guard
#define DIYBMS_DEFINES_H


#if (!defined(DIYBMSMODULEVERSION))
#error You need to specify the DIYBMSMODULEVERSION define
#endif

#if defined(DIYBMSMODULEVERSION) && DIYBMSMODULEVERSION > 440
#error Incorrect value for DIYBMSMODULEVERSION
#endif


//This is where the data begins in EEPROM
#define EEPROM_CONFIG_ADDRESS 0

#define nop  __asm__("nop\n\t");


struct ReadConfigData
{
    uint16_t boardVersion;
    uint16_t bypassTemp;
    uint16_t bypassThreshold;
    uint8_t numSamples;
    uint8_t loadResistance;
    uint32_t voltageCalibration;
    uint32_t gitVersion; 
} __attribute__((__packed__));


// Only the lowest 4 bits can be used!
enum COMMAND: uint8_t
{
    ResetPacketCounters = 0,
    ReadVoltageAndStatus=1,
    Identify=2,
    ReadTemperature=3,
    ReadBadPacketCounter=4,
    ReadSettings=5,
    WriteSettings=6,
    ReadBalancePowerPWM=7,
    Timing=8,
    ReadBalanceCurrentCounter=9,
    ReadPacketReceivedCounter=10,
    ResetBalanceCurrentCounter=11,
    WriteBalanceLevel=12,
};


//Default values
struct CellModuleConfig {
  //uint8_t mybank; // only known to the controller
  uint16_t BypassTemperature;
  uint16_t BypassThreshold;

  // Resistance of bypass load
  //float LoadResistance;  // hardcoded

  //Voltage Calibration
  uint16_t Calibration;  // only stored
  //Reference voltage (millivolt) normally 2.00mV
  //float mVPerADC;
  //Internal Thermistor settings
  //uint16_t Internal_BCoefficient;
  //External Thermistor settings
  //uint16_t External_BCoefficient;
} __attribute__((packed));

#endif
