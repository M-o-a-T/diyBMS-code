#include "settings.h"
#include "SerialPacker.h" // recycle its CRC support

bool Settings::WriteConfigToEEPROM(uint8_t* settings, uint16_t size, uint16_t eepromStartAddress) {
  //TODO: We should probably check EEPROM.length() to ensure it's big enough

  uint16_t EEPROMaddress=eepromStartAddress;
  for (uint16_t i = 0; i < size; i++) {
    EEPROM.update( EEPROMaddress, settings[i] );
    EEPROMaddress++;
  }

  //Generate and save the checksum for the setting data block
  uint16_t checksum = SerialPacker::crc16_buffer(settings, size);
  EEPROM.put(eepromStartAddress+size, checksum);

  // verify EEPROM content
  return Settings::ReadConfigFromEEPROM(settings,size,eepromStartAddress);
}

bool Settings::ReadConfigFromEEPROM(uint8_t* settings, uint16_t size, uint16_t eepromStartAddress) {
  uint16_t EEPROMaddress=eepromStartAddress;
  for (uint16_t i = 0; i < size; i++) {
    settings[i]=EEPROM.read(EEPROMaddress);
    EEPROMaddress++;
  }

  // Calculate the checksum
  uint16_t checksum = SerialPacker::crc16_buffer(settings, size);

  uint16_t existingChecksum;
  EEPROM.get(eepromStartAddress+size, existingChecksum);

  return checksum == existingChecksum;
}

void Settings::FactoryDefault(uint16_t size, uint16_t eepromStartAddress) {
  EEPROM.put(eepromStartAddress+size, 0x0000);
}
