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

#if DIYBMSMODULEVERSION > 440
#error Incorrect value for DIYBMSMODULEVERSION
#endif


//This is where the data begins in EEPROM
#define EEPROM_CONFIG_ADDRESS 0

#define nop  __asm__("nop\n\t");

#include "common.h"

//Default values
struct CellModuleConfig {
  //uint8_t mybank; // only known to the controller
  uint16_t BypassTemperature;
  uint16_t BypassThreshold;

  // Resistance of bypass load
  //float LoadResistance;  // hardcoded

  //Voltage Calibration
  uint32_t Calibration;  // only stored
  //Reference voltage (millivolt) normally 2.00mV
  //float mVPerADC;
  //Internal Thermistor settings
  //uint16_t Internal_BCoefficient;
  //External Thermistor settings
  //uint16_t External_BCoefficient;
} __attribute__((packed));

#endif
