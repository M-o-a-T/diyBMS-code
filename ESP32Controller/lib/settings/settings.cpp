#include "settings.h"
#include "crc16.h"

#include <Preferences.h>

void Settings::WriteConfig(const char *tag, char *settings, int size)
{
  ESP_LOGD(TAG, "WriteConfig %s", tag);

  Preferences prefs;
  prefs.begin(tag);

  prefs.putBytes("bytes", settings, size);

  //Generate and save the checksum for the setting data block
  prefs.putUShort("checksum", CRC16_Buffer((uint8_t *)settings, size));

  prefs.end();
}

bool Settings::ReadConfig(const char *tag, char *settings, int size)
{
  uint16_t checksum, existingChecksum;
  ESP_LOGD(TAG, "ReadConfig %s", tag);

  Preferences prefs;

  prefs.begin(tag);


  size_t schLen = prefs.getBytesLength("bytes");

  if (schLen != size)
  {
    prefs.end();
    return false;
  }

  prefs.getBytes("bytes", settings, schLen);

  // Calculate the checksum
  existingChecksum = prefs.getUShort("checksum");
  prefs.end();
  checksum = CRC16_Buffer((uint8_t*)settings, size);
  return (checksum == existingChecksum);
}

void Settings::FactoryDefault(const char *tag)
{
  ESP_LOGI(TAG, "FactoryDefault %s", tag);
  Preferences prefs;
  prefs.begin(tag);
  prefs.clear();
  //prefs.remove("bytes");
  //prefs.remove("checksum");
  prefs.end();
}
