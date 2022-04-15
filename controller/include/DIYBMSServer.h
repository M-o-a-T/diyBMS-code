
#ifndef DIYBMSServer_H_
#define DIYBMSServer_H_

#include <Arduino.h>

#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#include "pref_settings.h"
#else
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include "ESP8266TrueRandom.h"
#include "settings.h"
#endif

#include <ESPAsyncWebServer.h>

#include <EEPROM.h>

#include "defines.h"
#include "Rules.h"
#include "ArduinoJson.h"
#include "PacketRequestGenerator.h"
#include "PacketReceiveProcessor.h"

#ifdef ESP32
#include "FS.h"
#include <LITTLEFS.h>
#include "SD.h"
#endif

#include "HAL.h"
class DIYBMSServer
{
public:
    static void StartServer(AsyncWebServer *webserver,
                            diybms_eeprom_settings *mysettings,
#ifdef ESP32
                            fs::SDFS *sdcard,
#else
                            sdcard_info (*sdcardcallback)(),
#endif
                            PacketRequestGenerator *pkttransmitproc,
                            PacketReceiveProcessor *pktreceiveproc,
                            ControllerState *controlState,
                            Rules *rules,
                            void (*sdcardaction_callback)(uint8_t action),
                            HAL *hal);

    static void generateUUID();
    static void clearModuleValues(uint8_t module);

private:
    static AsyncWebServer *_myserver;
    static String UUIDString;
    static String UUIDStringLast2Chars;

    //Pointers to other classes (not always a good idea in static classes)
#ifdef ESP32                
    static fs::SDFS *_sdcard;
#else
    static sdcard_info (*_sdcardcallback)();
#endif
    static void (*_sdcardaction_callback)(uint8_t action);
    static PacketRequestGenerator *_transmitProc;
    static PacketReceiveProcessor *_receiveProc;
    static diybms_eeprom_settings *_mysettings;
    static Rules *_rules;
    static ControllerState *_controlState;
    static HAL *_hal;

    static void saveConfiguration()
    {
#ifdef ESP32
        Settings::WriteConfig("diybms", (uint8_t *)_mysettings, sizeof(diybms_eeprom_settings));
#else
        Settings::WriteConfigToEEPROM((uint8_t *)_mysettings, sizeof(diybms_eeprom_settings), EEPROM_SETTINGS_START_ADDRESS);
#endif
    }
#ifdef ESP32
    static void PrintStreamCommaFloat(AsyncResponseStream *response, const char *text, float value);
    static void PrintStreamComma(AsyncResponseStream *response, const char *text, uint32_t value);
    static void PrintStreamCommaInt16(AsyncResponseStream *response, const char *text, int16_t value);
    static void PrintStream(AsyncResponseStream *response, const char *text, uint32_t value);
    static void PrintStreamCommaBoolean(AsyncResponseStream *response, const char *text, bool value);
    static void fileSystemListDirectory(AsyncResponseStream *response, fs::FS &fs, const char *dirname, uint8_t levels);
#else
    static void PrintStreamComma(AsyncResponseStream *response,const __FlashStringHelper *ifsh, uint32_t value);
#endif

    static void handleNotFound(AsyncWebServerRequest *request);
    static void monitor2(AsyncWebServerRequest *request);
    static void monitor3(AsyncWebServerRequest *request);
    //static void monitor(AsyncWebServerRequest *request);
    static void modules(AsyncWebServerRequest *request);
    static void integration(AsyncWebServerRequest *request);
    static void identifyModule(AsyncWebServerRequest *request);
    static void GetRules(AsyncWebServerRequest *request);
    static String TemplateProcessor(const String &var);
    static bool validateXSS(AsyncWebServerRequest *request);
    static void SendSuccess(AsyncWebServerRequest *request);
    static void SendFailure(AsyncWebServerRequest *request);
    static void settings(AsyncWebServerRequest *request);
    static void resetCounters(AsyncWebServerRequest *request);
    static void handleRestartController(AsyncWebServerRequest *request);
    static void storage(AsyncWebServerRequest *request);
#ifdef ESP32
    static void avrstorage(AsyncWebServerRequest *request);
    static void avrstatus(AsyncWebServerRequest *request);
    static void currentmonitor(AsyncWebServerRequest *request);
    static void rs485settings(AsyncWebServerRequest *request);
    static void getvictron(AsyncWebServerRequest *request);
    
    

    static void downloadFile(AsyncWebServerRequest *request);
#endif
    static void saveSetting(AsyncWebServerRequest *request);
    static void saveInfluxDBSetting(AsyncWebServerRequest *request);
    static void saveMQTTSetting(AsyncWebServerRequest *request);
    static void saveGlobalSetting(AsyncWebServerRequest *request);
    static void saveBankConfiguration(AsyncWebServerRequest *request);
    static void saveRuleConfiguration(AsyncWebServerRequest *request);
#ifdef ESP32
    static void saveCurrentMonBasic(AsyncWebServerRequest *request);
    static void saveCurrentMonAdvanced(AsyncWebServerRequest *request);
    static void saveCurrentMonRelay(AsyncWebServerRequest *request);
    static void saveRS485Settings(AsyncWebServerRequest *request);
    static void saveCurrentMonSettings(AsyncWebServerRequest *request);
#endif
    static void saveNTP(AsyncWebServerRequest *request);
    static void saveStorage(AsyncWebServerRequest *request);

    static void saveDisplaySetting(AsyncWebServerRequest *request);

    static void sdMount(AsyncWebServerRequest *request);
    static void sdUnmount(AsyncWebServerRequest *request);
#ifdef ESP32
    static void avrProgrammer(AsyncWebServerRequest *request);
    static void saveWifiConfigToSDCard(AsyncWebServerRequest *request);
    static void saveConfigurationToSDCard(AsyncWebServerRequest *request);
#endif

    static String uuidToString(uint8_t *uuidLocation);
    static void SetCacheAndETagGzip(AsyncWebServerResponse *response, String ETag);
    static void SetCacheAndETag(AsyncWebServerResponse *response, String ETag);

#ifdef ESP32
    static void enableAVRprog(AsyncWebServerRequest *request);
    static void disableAVRprog(AsyncWebServerRequest *request);

    static void saveVictron(AsyncWebServerRequest *request);
#endif
};

#ifdef ESP32
//TODO: Remove this
extern bool _sd_card_installed;
extern TaskHandle_t avrprog_task_handle;
extern avrprogramsettings _avrsettings;
extern RelayState previousRelayState[RELAY_TOTAL];
extern currentmonitoring_struct currentMonitor;

extern uint32_t canbus_messages_failed_sent;
extern uint32_t canbus_messages_sent;
extern uint32_t canbus_messages_received;

extern void ConfigureRS485();
extern void CurrentMonitorSetBasicSettings(uint16_t shuntmv, uint16_t shuntmaxcur, uint16_t batterycapacity, float fullchargevolt, float tailcurrent,float chargeefficiency);
extern void CurrentMonitorSetAdvancedSettings(currentmonitoring_struct newvalues);
extern void CurrentMonitorSetRelaySettings(currentmonitoring_struct newvalues);

#else
extern bool OutputsEnabled;
extern bool InputsEnabled;
#endif // ESP32

extern diybms_eeprom_settings mysettings;

#endif // guard
