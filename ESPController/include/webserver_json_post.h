#ifndef DIYBMSWebServer_Json_Post_H_
#define DIYBMSWebServer_Json_Post_H_

#include "settings.h"
#include "LittleFS.h"
#include "defines.h"
#include "PacketRequestGenerator.h"
#include "PacketReceiveProcessor.h"

esp_err_t post_savebankconfig_json_handler(httpd_req_t *req);
esp_err_t post_saventp_json_handler(httpd_req_t *req);
esp_err_t post_saveglobalsetting_json_handler(httpd_req_t *req);
esp_err_t post_savemqtt_json_handler(httpd_req_t *req);
esp_err_t post_saveinfluxdbsetting_json_handler(httpd_req_t *req);
esp_err_t post_saveconfigurationtosdcard_json_handler(httpd_req_t *req);
esp_err_t post_savewificonfigtosdcard_json_handler(httpd_req_t *req);
esp_err_t post_savesetting_json_handler(httpd_req_t *req);
esp_err_t post_restartcontroller_json_handler(httpd_req_t *req);
esp_err_t post_saverules_json_handler(httpd_req_t *req);
esp_err_t post_savedisplaysetting_json_handler(httpd_req_t *req);
esp_err_t post_savestorage_json_handler(httpd_req_t *req);
esp_err_t post_resetcounters_json_handler(httpd_req_t *req);
esp_err_t post_sdmount_json_handler(httpd_req_t *req);
esp_err_t post_sdunmount_json_handler(httpd_req_t *req);
esp_err_t post_enableavrprog_json_handler(httpd_req_t *req);
esp_err_t post_disableavrprog_json_handler(httpd_req_t *req);
esp_err_t post_avrprog_json_handler(httpd_req_t *req);
esp_err_t post_savers485settings_json_handler(httpd_req_t *req);
esp_err_t post_savecurrentmon_json_handler(httpd_req_t *req);
esp_err_t post_savecmbasic_json_handler(httpd_req_t *req);
esp_err_t post_savecmadvanced_json_handler(httpd_req_t *req);
esp_err_t post_savecmrelay_json_handler(httpd_req_t *req);
esp_err_t post_savevictron_json_handler(httpd_req_t *req);

extern diybms_eeprom_settings mysettings;
extern PacketRequestGenerator prg;
extern PacketReceiveProcessor receiveProc;
extern HAL_ESP32 hal;
extern fs::SDFS SD;

extern TaskHandle_t avrprog_task_handle;
extern uint32_t canbus_messages_received;
extern uint32_t canbus_messages_sent;
extern uint32_t canbus_messages_failed_sent;
extern void sdcardaction_callback(uint8_t action);
extern Rules rules;

extern avrprogramsettings _avrsettings;

extern void ConfigureRS485();
extern void CurrentMonitorSetBasicSettings(uint16_t shuntmv, uint16_t shuntmaxcur, uint16_t batterycapacity, float fullchargevolt, float tailcurrent, float chargeefficiency);
extern void CurrentMonitorSetAdvancedSettings(currentmonitoring_struct newvalues);
extern void CurrentMonitorSetRelaySettings(currentmonitoring_struct newvalues);
extern void setCacheControl(httpd_req_t *req);
#endif
