/*

 ____  ____  _  _  ____  __  __  ___    _  _  __
(  _ \(_  _)( \/ )(  _ \(  \/  )/ __)  ( \/ )/. |
 )(_) )_)(_  \  /  ) _ < )    ( \__ \   \  /(_  _)
(____/(____) (__) (____/(_/\/\_)(___/    \/   (_)

  (c) 2017 to 2021 Stuart Pittaway

  This is the code for the ESP32 controller - it talks to the V4.X cell modules over isolated serial bus

  This code runs on ESP32 DEVKIT-C and compiles with VS CODE and PLATFORM IO environment.

  Unless you are making code changes, please use the pre-compiled version from GITHUB instead.
*/

#undef CONFIG_DISABLE_HAL_LOCKS

const char *TAG = "diybms";

#include "esp_log.h"
#include <Arduino.h>

//#define PACKET_LOGGING_RECEIVE
//#define PACKET_LOGGING_SEND
//#define RULES_LOGGING
//#define MQTT_LOGGING

#include "FS.h"
#include <LITTLEFS.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include "time.h"
#include <esp_wifi.h>
#include <esp_bt.h>
#include <Preferences.h>
#include <malloc.h>

// Libraries for SD card
#include "SD.h"
#include "driver/gpio.h"
#include "driver/can.h"
#include "driver/adc.h"
//#include "driver/twai.h"
#include <driver/uart.h>

#include <ESPAsyncWebServer.h>
#include <AsyncMqttClient.h>

#include "serializer.h"

#include <ArduinoJson.h>
#include "defines.h"
#include "HAL.h"
#include "Rules.h"
#include "avrisp_programmer.h"
#include "tft.h"
#include "influxdb.h"

#include "victron_canbus.h"

const uart_port_t rs485_uart_num = uart_port_t::UART_NUM_1;

HAL hal;

volatile bool emergencyStop = false;
bool _sd_card_installed = false;

// Used for WIFI hostname and also sent to Victron over CANBUS
char hostname[16];

// holds modbus data
uint8_t frame[256];

extern bool _tft_screen_available;

Rules rules;
diybms_eeprom_settings mysettings;
uint16_t TotalNumberOfCells() { return mysettings.totalNumberOfBanks * mysettings.totalNumberOfSeriesModules; }

uint32_t canbus_messages_received = 0;
uint32_t canbus_messages_sent = 0;
uint32_t canbus_messages_failed_sent = 0;

bool server_running = false;
RelayState previousRelayState[RELAY_TOTAL];
bool previousRelayPulse[RELAY_TOTAL];

volatile enumInputState InputState[INPUTS_TOTAL];

currentmonitoring_struct currentMonitor;

AsyncWebServer server(80);

TaskHandle_t i2c_task_handle = NULL;
TaskHandle_t ledoff_task_handle = NULL;
TaskHandle_t wifiresetdisable_task_handle = NULL;
TaskHandle_t sdcardlog_task_handle = NULL;
TaskHandle_t sdcardlog_outputs_task_handle = NULL;
TaskHandle_t avrprog_task_handle = NULL;
TaskHandle_t mqtt1_task_handle = NULL;
TaskHandle_t mqtt2_task_handle = NULL;
TaskHandle_t enqueue_task_handle = NULL;
TaskHandle_t transmit_task_handle = NULL;
TaskHandle_t replyqueue_task_handle = NULL;
TaskHandle_t lazy_task_handle = NULL;
TaskHandle_t rule_task_handle = NULL;
TaskHandle_t influxdb_task_handle = NULL;
TaskHandle_t pulse_relay_off_task_handle = NULL;
TaskHandle_t voltageandstatussnapshot_task_handle = NULL;
TaskHandle_t updatetftdisplay_task_handle = NULL;
TaskHandle_t tftsleep_task_handle = NULL;
TaskHandle_t tftwakeup_task_handle = NULL;

TaskHandle_t tca6408_isr_task_handle = NULL;
TaskHandle_t tca9534_isr_task_handle = NULL;

TaskHandle_t rs485_tx_task_handle = NULL;
TaskHandle_t rs485_rx_task_handle = NULL;
TaskHandle_t service_rs485_transmit_q_task_handle = NULL;

TaskHandle_t victron_canbus_tx_task_handle = NULL;
TaskHandle_t victron_canbus_rx_task_handle = NULL;

// This large array holds all the information about the modules
CellModuleInfo cmi[maximum_controller_cell_modules];

avrprogramsettings _avrsettings;

// Number of bytes of the largest MODBUS request we make
#define MAX_SEND_RS485_PACKET_LENGTH 36

QueueHandle_t rs485_transmit_q_handle;

#include "settings.h"
#include "SoftAP.h"
#include "DIYBMSServer.h"

ControllerState _controller_state = ControllerState::Unknown;

AsyncMqttClient mqttClient;

void LED(uint8_t bits)
{
  hal.Led(bits);
}

// When triggered, the VOLTAGE and STATUS in the CellModuleInfo structure are accurate and consistent at this point in time.
// Good point to apply rules and update screen/statistics
void voltageandstatussnapshot_task(void *param)
{
  for (;;)
  {
    // Wait until this task is triggered, when
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // ESP_LOGD(TAG, "Snap");

    if (_tft_screen_available)
    {
      // Refresh the TFT display
      xTaskNotify(updatetftdisplay_task_handle, 0x00, eNotifyAction::eNoAction);
    }

  } // end for
}

// Sets the RS485 serial parameters after they have been changed
void ConfigureRS485()
{

  if (hal.GetRS485Mutex())
  {
    ESP_LOGD(TAG, "Configure RS485");
    ESP_ERROR_CHECK_WITHOUT_ABORT(uart_set_parity(rs485_uart_num, mysettings.rs485parity));
    ESP_ERROR_CHECK_WITHOUT_ABORT(uart_set_stop_bits(rs485_uart_num, mysettings.rs485stopbits));
    ESP_ERROR_CHECK_WITHOUT_ABORT(uart_set_baudrate(rs485_uart_num, mysettings.rs485baudrate));
    ESP_ERROR_CHECK_WITHOUT_ABORT(uart_set_word_length(rs485_uart_num, mysettings.rs485databits));

    hal.ReleaseRS485Mutex();
  }
  else
  {
    ESP_ERROR_CHECK(ESP_FAIL)
  }
}

void SetupRS485()
{
  ESP_LOGD(TAG, "Setup RS485");
  /* TEST RS485 */

  // Zero all data to start with
  memset(&currentMonitor, 0, sizeof(currentmonitoring_struct));

  // if (mysettings.currentMonitoringEnabled) {
  // }

  uart_config_t uart_config = {
      .baud_rate = mysettings.rs485baudrate,
      .data_bits = mysettings.rs485databits,
      .parity = mysettings.rs485parity,
      .stop_bits = mysettings.rs485stopbits,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 122,
  };

  // Configure UART parameters
  ESP_ERROR_CHECK(uart_param_config(rs485_uart_num, &uart_config));

  // Set UART1 pins(TX: IO23, RX: I022, RTS: IO18, CTS: Not used)
  ESP_ERROR_CHECK(uart_set_pin(rs485_uart_num, RS485_TX, RS485_RX, RS485_ENABLE, UART_PIN_NO_CHANGE));

  // Install UART driver (we don't need an event queue here)
  ESP_ERROR_CHECK(uart_driver_install(rs485_uart_num, 256, 256, 0, NULL, 0));

  // Set RS485 half duplex mode
  ESP_ERROR_CHECK(uart_set_mode(rs485_uart_num, uart_mode_t::UART_MODE_RS485_HALF_DUPLEX));

  ConfigureRS485();
}

void mountSDCard()
{
  if (_avrsettings.programmingModeEnabled)
  {
    ESP_LOGW(TAG, "Attempt to mount sd but AVR prog mode enabled");
    return;
  }

  ESP_LOGI(TAG, "Mounting SD card");
  if (hal.GetVSPIMutex())
  {
    // Initialize SD card
    if (SD.begin(SDCARD_CHIPSELECT, hal.vspi))
    {
      uint8_t cardType = SD.cardType();
      if (cardType == CARD_NONE)
      {
        ESP_LOGW(TAG, "No SD card attached");
      }
      else
      {
        ESP_LOGI(TAG, "SD card available");
        _sd_card_installed = true;
      }
    }
    else
    {
      ESP_LOGE(TAG, "Card Mount Failed");
    }
    hal.ReleaseVSPIMutex();
  }
}

void unmountSDCard()
{
  if (_sd_card_installed == false)
    return;

  ESP_LOGI(TAG, "Unmounting SD card");
  if (hal.GetVSPIMutex())
  {
    SD.end();
    hal.ReleaseVSPIMutex();
    _sd_card_installed = false;
  }
}
void sdcardaction_callback(uint8_t action)
{
  if (action == 0)
  {
    unmountSDCard();
  }
  else
  {
    mountSDCard();
  }
}

#ifdef USE_AVRISP
void avrprog_task(void *param)
{
  for (;;)
  {
    // Wait until this task is triggered
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Wake up the display
    if (tftwakeup_task_handle != NULL)
    {
      xTaskNotify(tftwakeup_task_handle, 0x01, eNotifyAction::eSetValueWithOverwrite);
    }

    // TODO: This needs to be passed into this as a parameter
    avrprogramsettings *s;
    s = (avrprogramsettings *)param;

    ESP_LOGI(TAG, "AVR setting e=%02X h=%02X l=%02X mcu=%08X file=%s", s->efuse, s->hfuse, s->lfuse, s->mcu, s->filename);

    bool old_sd_card_installed = _sd_card_installed;

    if (_sd_card_installed)
    {
      // Unmount SD card so we don't have issues on SPI bus
      unmountSDCard();
    }

    // Now we load the file into program array, from LITTLEFS (SPIFF)
    if (LITTLEFS.exists(s->filename))
    {
      File binaryfile = LITTLEFS.open(s->filename);

      s->programsize = binaryfile.size();

      // Reserve the SPI bus for programming purposes
      if (hal.GetVSPIMutex())
      {
        // Stop tasks which may want to use something on the VSPI
        // prevents corruption of programming or SD CARD contents
        // vTaskSuspend(sdcardlog_task_handle);
        // vTaskSuspend(sdcardlog_outputs_task_handle);

        hal.SwapGPIO0ToOutput();

        // This will block for the 6 seconds it takes to program ATTINY841...
        // although AVRISP_PROGRAMMER will call the watchdog to prevent reboots

        uint32_t starttime = millis();
        AVRISP_PROGRAMMER isp = AVRISP_PROGRAMMER(&(hal.vspi), GPIO_NUM_0, false, VSPI_SCK);

        ESP_LOGI(TAG, "Programming AVR");
        // This would be much better using a stream instead of a in ram buffer
        s->progresult = isp.ProgramAVRDevice(&tftdisplay_avrprogrammer_progress, s->mcu, s->programsize, binaryfile, s->lfuse, s->hfuse, s->efuse);

        s->duration = millis() - starttime;

        hal.ConfigureVSPI();
        hal.ReleaseVSPIMutex();

        if (s->progresult == AVRISP_PROGRAMMER_RESULT::SUCCESS)
        {
          // sprintf(message, "Programming complete, duration %ums, %i bytes", s->duration, programsize);
          ESP_LOGI(TAG, "Success");
        }
        else
        {
          // sprintf(message, "Programming failed, reason %i", (int)progresult);
          ESP_LOGE(TAG, "Failed %i", s->progresult);
        }

        binaryfile.close();

        // Resume tasks after programming is complete
        // vTaskResume(sdcardlog_task_handle);
        // vTaskResume(sdcardlog_outputs_task_handle);
      }
      else
      {
        ESP_LOGE(TAG, "Unable to obtain Mutex");
      }
    }
    else
    {
      ESP_LOGE(TAG, "AVR file not found %s", s->filename);
    }

    s->inProgress = false;

    // Refresh the display, after programming is complete
    xTaskNotify(updatetftdisplay_task_handle, 0x00, eNotifyAction::eNoAction);

    // If we unmounted the SD card, remount it here
    if (old_sd_card_installed)
    {
      mountSDCard();
    }

  } // end for
}
#endif

// Output a status log to the SD Card in CSV format
void sdcardlog_task(void *param)
{
  for (;;)
  {
    // Wait X seconds
    for (size_t i = 0; i < mysettings.loggingFrequencySeconds; i++)
    {
      // Delay 1 second
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    if (_sd_card_installed && !_avrsettings.programmingModeEnabled && mysettings.loggingEnabled && _controller_state == ControllerState::Running && hal.IsVSPIMutexAvailable())
    {
      // ESP_LOGD(TAG, "sdcardlog_task");

      struct tm timeinfo;
      // getLocalTime has delay() functions in it :-(
      if (getLocalTime(&timeinfo, 1))
      {
        timeinfo.tm_year += 1900;
        // Month is 0 to 11 based!
        timeinfo.tm_mon++;

        // ESP_LOGD(TAG, "%04u-%02u-%02u %02u:%02u:%02u", timeinfo.tm_year, timeinfo.tm_mon, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        char filename[32];
        sprintf(filename, "/data_%04u%02u%02u.csv", timeinfo.tm_year, timeinfo.tm_mon, timeinfo.tm_mday);

        File file;

        // Prevent other devices using the VSPI bus
        if (hal.GetVSPIMutex())
        {
          if (SD.exists(filename))
          {
            // Open existing file (assumes there is enough SD card space to log)
            file = SD.open(filename, FILE_APPEND);
            // ESP_LOGD(TAG, "Open log %s", filename);
          }
          else
          {
            // Create a new file
            uint64_t freeSpace = SD.totalBytes() - SD.usedBytes();

            // Ensure there is more than 25MB of free space on SD card before creating a file
            if (freeSpace > (uint64_t)(25 * 1024 * 1024))
            {
              // Create the file
              File file = SD.open(filename, FILE_WRITE);
              if (file)
              {
                ESP_LOGI(TAG, "Create log %s", filename);

                file.print("DateTime,");

                for (uint8_t i = 0; i < TotalNumberOfCells(); i++)
                {
                  file.print("VoltagemV_");
                  file.print(i);
                  file.print(",InternalTemp_");
                  file.print(i);
                  file.print(",ExternalTemp_");
                  file.print(i);
                  file.print(",Bypass_");
                  file.print(i);
                  file.print(",PWM_");
                  file.print(i);
                  file.print(",BypassOverTemp_");
                  file.print(i);
                  file.print(",BadPackets_");
                  file.print(i);
                  file.print(",BalancemAh_");
                  file.print(i);

                  if (i < TotalNumberOfCells() - 1)
                  {
                    file.print(',');
                  }
                }
                file.println();
              }
            }
            else
            {
              ESP_LOGE(TAG, "SD card has less than 25MiB remaining, logging stopped");
              // We had an error, so switch off logging (this is only in memory so not written perm.)
              mysettings.loggingEnabled = false;
            }
          }

          if (file && mysettings.loggingEnabled)
          {
            char dataMessage[255];

            sprintf(dataMessage, "%04u-%02u-%02u %02u:%02u:%02u,", timeinfo.tm_year, timeinfo.tm_mon, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            file.print(dataMessage);

            for (uint8_t i = 0; i < TotalNumberOfCells(); i++)
            {
              // This may output invalid data when controller is first powered up
              sprintf(dataMessage, "%u,%i,%i,%c,%u,%c,%u,%u",
                      cmi[i].voltagemV, cmi[i].internalTemp,
                      cmi[i].externalTemp, cmi[i].inBypass ? 'Y' : 'N',
                      (int)((float)cmi[i].PWMValue / (float)255.0 * 100), cmi[i].bypassOverTemp ? 'Y' : 'N',
                      cmi[i].badPacketCount, cmi[i].BalanceCurrentCount);
              file.print(dataMessage);
              if (i < TotalNumberOfCells() - 1)
              {
                file.print(',');
              }
            }
            file.println();
            file.close();

            ESP_LOGD(TAG, "Wrote to SD log");
          }
          else
          {
            ESP_LOGE(TAG, "Failed to create/append SD logging file");
            // We had an error opening the file, so switch off logging
            // mysettings.loggingEnabled = false;
          }

          // Now log the current monitor
          if (mysettings.currentMonitoringEnabled)
          {
            char cmon_filename[32];
            sprintf(cmon_filename, "/modbus%02u_%04u%02u%02u.csv", mysettings.currentMonitoringModBusAddress, timeinfo.tm_year, timeinfo.tm_mon, timeinfo.tm_mday);

            File file;

            if (SD.exists(cmon_filename))
            {
              // Open existing file (assumes there is enough SD card space to log)
              file = SD.open(cmon_filename, FILE_APPEND);
              // ESP_LOGD(TAG, "Open log %s", filename);
            }
            else
            {
              // Create a new file
              uint64_t freeSpace = SD.totalBytes() - SD.usedBytes();

              // Ensure there is more than 25MB of free space on SD card before creating a file
              if (freeSpace > (uint64_t)(25 * 1024 * 1024))
              {
                // Create the file
                File file = SD.open(cmon_filename, FILE_WRITE);
                if (file)
                {
                  ESP_LOGI(TAG, "Create log %s", cmon_filename);
                  file.println("DateTime,valid,voltage,current,mAhIn,mAhOut,power,temperature,shuntmV,relayState");
                }
              }
              else
              {
                ESP_LOGE(TAG, "SD card has less than 25MiB remaining, logging stopped");
                // We had an error, so switch off logging (this is only in memory so not written perm.)
                mysettings.loggingEnabled = false;
              }
            }

            if (file && mysettings.loggingEnabled)
            {
              char dataMessage[255];

              sprintf(dataMessage, "%04u-%02u-%02u %02u:%02u:%02u,", timeinfo.tm_year, timeinfo.tm_mon, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
              file.print(dataMessage);

              sprintf(dataMessage, "%i,%.3f,%.3f,%u,%u,%.3f,%i,%.3f,%i",
                      currentMonitor.validReadings ? 1 : 0,
                      currentMonitor.modbus.voltage, currentMonitor.modbus.current,
                      currentMonitor.modbus.milliamphour_in, currentMonitor.modbus.milliamphour_out,
                      currentMonitor.modbus.power, currentMonitor.modbus.temperature,
                      currentMonitor.modbus.shuntmV, currentMonitor.RelayState ? 1 : 0);
              file.print(dataMessage);

              file.println();
              file.close();

              ESP_LOGD(TAG, "Wrote current monitor data to SD log");
            }
            else
            {
              ESP_LOGE(TAG, "Failed to create/append SD logging file");
              // We had an error opening the file, so switch off logging
              // mysettings.loggingEnabled = false;
            }
          } // end of logging for current monitor
        }
        else
        {
          ESP_LOGE(TAG, "Invalid datetime");
        }

        // Must be the last thing...
        hal.ReleaseVSPIMutex();
      }
    }
  } // end for loop

  // vTaskDelete( NULL );
}

// Writes a status log of the OUTPUT STATUES to the SD Card in CSV format
void sdcardlog_outputs_task(void *param)
{
  for (;;)
  {
    // Wait until this task is triggered https://www.freertos.org/ulTaskNotifyTake.html
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (_sd_card_installed && !_avrsettings.programmingModeEnabled && mysettings.loggingEnabled)
    {
      ESP_LOGD(TAG, "sdcardlog_outputs_task");

      struct tm timeinfo;
      // getLocalTime has delay() functions in it :-(
      if (getLocalTime(&timeinfo, 1))
      {
        timeinfo.tm_year += 1900;
        // Month is 0 to 11 based!
        timeinfo.tm_mon++;

        // ESP_LOGD(TAG, "%04u-%02u-%02u %02u:%02u:%02u", timeinfo.tm_year, timeinfo.tm_mon, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        char filename[32];
        sprintf(filename, "/output_status_%04u%02u%02u.csv", timeinfo.tm_year, timeinfo.tm_mon, timeinfo.tm_mday);

        File file;

        // Prevent other devices using the VSPI bus
        if (hal.GetVSPIMutex())
        {

          if (SD.exists(filename))
          {
            // Open existing file (assumes there is enough SD card space to log)
            file = SD.open(filename, FILE_APPEND);

            // ESP_LOGD(TAG, "Open log %s", filename);
          }
          else
          {
            // Create a new file
            uint64_t freeSpace = SD.totalBytes() - SD.usedBytes();

            // Ensure there is more than 25MB of free space on SD card before creating a file
            if (freeSpace > (uint64_t)(25 * 1024 * 1024))
            {
              // Create the file
              File file = SD.open(filename, FILE_WRITE);
              if (file)
              {
                // ESP_LOGD(TAG, "Create log %s", filename);

                file.print("DateTime,Bits,");

                for (uint8_t i = 0; i < RELAY_TOTAL; i++)
                {
                  file.print("Output_");
                  file.print(i);
                  if (i < RELAY_TOTAL - 1)
                  {
                    file.print(',');
                  }
                }
                file.println();
              }
            }
            else
            {
              ESP_LOGE(TAG, "SD card has less than 25MiB remaining, logging stopped");
              // We had an error, so switch off logging
              // mysettings.loggingEnabled = false;
            }
          }

          if (file && mysettings.loggingEnabled)
          {
            char dataMessage[255];

            sprintf(dataMessage, "%04u-%02u-%02u %02u:%02u:%02u,", timeinfo.tm_year, timeinfo.tm_mon, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            file.print(dataMessage);
            file.print(hal.LastTCA6408Value(), BIN);
            file.print(',');

            for (uint8_t i = 0; i < RELAY_TOTAL; i++)
            {
              // This may output invalid data when controller is first powered up
              sprintf(dataMessage, "%c", previousRelayState[i] == RelayState::RELAY_ON ? 'Y' : 'N');
              file.print(dataMessage);
              if (i < RELAY_TOTAL - 1)
              {
                file.print(',');
              }
            }
            file.println();
            file.close();

            ESP_LOGD(TAG, "Wrote to SD log");
          }
          else
          {
            ESP_LOGE(TAG, "Failed to create/append SD logging file");
            // We had an error opening the file, so switch off logging
            // mysettings.loggingEnabled = false;
          }
        }
        else
        {
          ESP_LOGE(TAG, "Invalid datetime");
        }

        // Must be the last thing...
        hal.ReleaseVSPIMutex();
      } // end if
    }   // end if
  }     // end for loop

  // vTaskDelete( NULL );
}

// Disable the BOOT button from acting as a WIFI RESET
// button which clears the EEPROM settings for WIFI connection
void wifiresetdisable_task(void *param)
{
  for (;;)
  {
    // Wait until this task is triggered https://www.freertos.org/ulTaskNotifyTake.html
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Wait for 20 seconds before disabling button/pin
    for (size_t i = 0; i < 20; i++)
    {
      // Wait 1 second
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    hal.SwapGPIO0ToOutput();
  }

  // vTaskDelete( NULL );
}

void ledoff_task(void *param)
{
  for (;;)
  {
    // Wait until this task is triggered https://www.freertos.org/ulTaskNotifyTake.html
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Wait 100ms
    vTaskDelay(100 / portTICK_PERIOD_MS);
    // LED OFF
    // ESP_LOGD(TAG, "Led off")
    LED(RGBLED::OFF);
  }
}

#if INPUTS_TOTAL != 7
#error Number of inputs is wrong
#endif

void tca6408_isr_task(void *param)
{
  for (;;)
  {
    // Wait until this task is triggered https://www.freertos.org/ulTaskNotifyTake.html
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    ESP_LOGD(TAG, "tca6408_isr");

    // Read ports A/B/C/D inputs (on TCA6408)
    uint8_t v = hal.ReadTCA6408InputRegisters();
    // P0=A
    InputState[0] = (v & B00000001) == 0 ? enumInputState::INPUT_LOW : enumInputState::INPUT_HIGH;
    // P1=B
    InputState[1] = (v & B00000010) == 0 ? enumInputState::INPUT_LOW : enumInputState::INPUT_HIGH;
    // P2=C
    InputState[2] = (v & B00000100) == 0 ? enumInputState::INPUT_LOW : enumInputState::INPUT_HIGH;
    // P3=D
    InputState[3] = (v & B00001000) == 0 ? enumInputState::INPUT_LOW : enumInputState::INPUT_HIGH;
  }
}

void tca9534_isr_task(void *param)
{
  for (;;)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    ESP_LOGD(TAG, "tca9534_isr");

    // Read ports
    // The 9534 deals with internal LED outputs and spare IO on J10
    uint8_t v = hal.ReadTCA9534InputRegisters();

    // P4= J13 PIN 1 = WAKE UP TFT FOR DISPLAYS WITHOUT TOUCH
    InputState[4] = (v & B00010000) == 0 ? enumInputState::INPUT_LOW : enumInputState::INPUT_HIGH;
    // P6 = spare I/O (on PCB pin)
    InputState[5] = (v & B01000000) == 0 ? enumInputState::INPUT_LOW : enumInputState::INPUT_HIGH;
    // P7 = Emergency Stop
    InputState[6] = (v & B10000000) == 0 ? enumInputState::INPUT_LOW : enumInputState::INPUT_HIGH;

    // Emergency Stop (J1) has triggered
    if (InputState[6] == enumInputState::INPUT_LOW)
    {
      emergencyStop = true;
    }

    if (InputState[4] == enumInputState::INPUT_LOW)
    {
      // Wake screen on pin going low
      if (tftwakeup_task_handle != NULL)
      {
        xTaskNotify(tftwakeup_task_handle, 0x00, eNotifyAction::eNoAction);
      }
    }
  }
}

volatile uint32_t WifiPasswordClearTime;
volatile bool ResetWifi = false;

// Check if BOOT button is pressed, if held down for more than 4 seconds
// trigger a wifi password reset/clear from EEPROM.
void IRAM_ATTR WifiPasswordClear()
{
  if (digitalRead(GPIO_NUM_0) == LOW)
  {
    // Button pressed, store time
    WifiPasswordClearTime = millis() + 4000;
    ResetWifi = false;
  }
  else
  {
    // Button released
    // Did user press button for longer than 4 seconds?
    if (millis() > WifiPasswordClearTime)
    {
      ResetWifi = true;
    }
  }
}

// Triggered when TCA6408 INT pin goes LOW
void IRAM_ATTR TCA6408Interrupt()
{
  if (tca6408_isr_task_handle != NULL)
  {
    xTaskNotifyFromISR(tca6408_isr_task_handle, 0x00, eNotifyAction::eNoAction, pdFALSE);
  }
}

// Triggered when TCA9534A INT pin goes LOW
void IRAM_ATTR TCA9534AInterrupt()
{
  if (tca9534_isr_task_handle != NULL)
  {
    xTaskNotifyFromISR(tca9534_isr_task_handle, 0x00, eNotifyAction::eNoAction, pdFALSE);
  }
}

const char *packetType(uint8_t cmd)
{
  switch (cmd)
  {
  case COMMAND::ResetPacketCounters:
    return "ResetPkCC";
    break;
  case COMMAND::ReadVoltageAndStatus:
    return "RdVolt";
    break;
  case COMMAND::Identify:
    return "Ident";
    break;
  case COMMAND::ReadTemperature:
    return "RdTemp";
    break;
  case COMMAND::ReadPacketCounters:
    return "RdPkC";
    break;
  case COMMAND::ReadSettings:
    return "RdSettin";
    break;
  case COMMAND::WriteSettings:
    return "WriteSet";
    break;
  case COMMAND::ReadBalancePowerPWM:
    return "RdBalanc";
    break;
  case COMMAND::Timing:
    return "Timing";
    break;
  case COMMAND::ReadBalanceCurrentCounter:
    return "Current";
    break;
  case COMMAND::WriteBalanceLevel:
    return "WriteBal";
    break;
  }

  return " ??????   ";
}

void dumpPacketToDebug(char indicator, PacketMeta *meta)
{
  // Filter on some commands
  // if ((buffer->command & 0x0F) != COMMAND::Timing)    return;

  PacketHeader *header = (PacketHeader *)(meta+1);
  ESP_LOGD(TAG, "%c %02X-%02X H:%02X C:%02X SEQ:%01X %s",
           indicator,
           header->start,
           header->start+header->cells,
           header->hops,
           header->command,
           header->sequence,
           packetType(header->command));

  // ESP_LOG_BUFFER_HEX("packet", &(buffer->moduledata[0]), sizeof(buffer->moduledata), ESP_LOG_DEBUG);
}

const char *ControllerStateString(ControllerState value)
{
  switch (value)
  {
  case ControllerState::PowerUp:
    return "PowerUp";
  case ControllerState::ConfigurationSoftAP:
    return "ConfigurationSoftAP";
  case ControllerState::Stabilizing:
    return "Stabilizing";
  case ControllerState::Running:
    return "Running";
  case ControllerState::Unknown:
    return "Unknown";
  }

  return "?";
}

void SetControllerState(ControllerState newState)
{
  if (_controller_state != newState)
  {
    ESP_LOGI(TAG, "** Controller changed state from %s to %s **", ControllerStateString(_controller_state), ControllerStateString(newState));

    _controller_state = newState;

    switch (_controller_state)
    {
    case ControllerState::PowerUp:
      // Purple during start up, don't use the LED as thats not setup at this state
      hal.Led(RGBLED::Purple);
      break;
    case ControllerState::ConfigurationSoftAP:
      // Don't use the LED as thats not setup at this state
      hal.Led(RGBLED::White);
      break;
    case ControllerState::Stabilizing:
      LED(RGBLED::Yellow);
      break;
    case ControllerState::Running:
      LED(RGBLED::Green);
      // Fire task to switch off BOOT button after 30 seconds
      xTaskNotify(wifiresetdisable_task_handle, 0x00, eNotifyAction::eNoAction);
      break;
    case ControllerState::Unknown:
      // Do nothing
      break;
    }
  }
}

uint16_t minutesSinceMidnight()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    return 0;
  }
  else
  {
    return (timeinfo.tm_hour * 60) + timeinfo.tm_min;
  }
}

void replyqueue_task(void *param)
{
  for (;;)
  {
    // Delay 0.1 second
    vTaskDelay(pdMS_TO_TICKS(100));

    while (!replyQueue.isEmpty())
    {
      if (!receiveOnePacket())
      {
        // Error blue
        LED(RGBLED::Blue);

        ESP_LOGE(TAG, "Packet Failed");

        // SERIAL_DEBUG.print(F("*FAIL*"));
        // dumpPacketToDebug('F', &ps);
      }

      // Small delay to allow watchdog to be fed
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

void transmit_task(void *param)
{
  uint16_t delay_wait;

  if (mysettings.baudRate >= 9600)
    delay_wait = 50;
  else if (mysettings.baudRate >= 5000)
    delay_wait = 70;
  else
    delay_wait = 100;

  for (;;)
  {
    // TODO: Move to proper RTOS QUEUE...
    while(requestQueue.isEmpty())
      vTaskDelay(pdMS_TO_TICKS(delay_wait));

    uint16_t delay_ms = transmitOnePacket();
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
}

// Runs the rules and populates rule_outcome array with true/false for each rule
// Rules based on module parameters/readings like voltage and temperature
// are only processed once every module has returned at least 1 reading/communication
void ProcessRules()
{
  rules.ClearValues();
  rules.ClearWarnings();
  rules.ClearErrors();

  rules.rule_outcome[Rule::BMSError] = false;

  uint16_t totalConfiguredModules = TotalNumberOfCells();
  if (totalConfiguredModules > maximum_controller_cell_modules)
  {
    // System is configured with more than maximum modules - abort!
    rules.SetError(InternalErrorCode::TooManyModules);
  }

  if (receiveProc.totalModulesFound > 0 && receiveProc.totalModulesFound != totalConfiguredModules)
  {
    // Found more or less modules than configured for
    rules.SetError(InternalErrorCode::ModuleCountMismatch);
  }

  // Communications error...
  if (receiveProc.HasCommsTimedOut())
  {
    rules.SetError(InternalErrorCode::CommunicationsError);
    rules.rule_outcome[Rule::BMSError] = true;
  }

  if (rules.rule_outcome[Rule::EmergencyStop])
  {
    // Lowest 3 bits are RGB led GREEN/RED/BLUE
    rules.SetError(InternalErrorCode::ErrorEmergencyStop);
  }

  rules.numberOfBalancingModules = 0;
  uint8_t cellid = 0;
  for (int8_t bank = 0; bank < mysettings.totalNumberOfBanks; bank++)
  {
    for (int8_t i = 0; i < mysettings.totalNumberOfSeriesModules; i++)
    {
      rules.ProcessCell(bank, cellid, &cmi[cellid]);

      if (cmi[cellid].valid && cmi[cellid].settingsCached)
      {

        if (cmi[cellid].BypassConfigThresholdmV != mysettings.BypassThresholdmV)
        {
          rules.SetWarning(InternalWarningCode::ModuleInconsistentBypassVoltage);
        }

        if (cmi[cellid].BypassMaxTemp != mysettings.BypassMaxTemp)
        {
          rules.SetWarning(InternalWarningCode::ModuleInconsistentBypassTemperature);
        }

        if (cmi[cellid].inBypass)
        {
          rules.numberOfBalancingModules++;
        }

        if (cmi[0].settingsCached && cmi[cellid].CodeVersionNumber != cmi[0].CodeVersionNumber)
        {
          // Do all the modules have the same version of code as module zero?
          rules.SetWarning(InternalWarningCode::ModuleInconsistentCodeVersion);
        }

        if (cmi[0].settingsCached && cmi[cellid].BoardVersionNumber != cmi[0].BoardVersionNumber)
        {
          // Do all the modules have the same hardware revision?
          rules.SetWarning(InternalWarningCode::ModuleInconsistentBoardRevision);
        }
      }

      cellid++;
    }
    rules.ProcessBank(bank);
  }

  if (mysettings.loggingEnabled && !_sd_card_installed && !_avrsettings.programmingModeEnabled)
  {
    rules.SetWarning(InternalWarningCode::LoggingEnabledNoSDCard);
  }

  if (_avrsettings.programmingModeEnabled)
  {
    rules.SetWarning(InternalWarningCode::AVRProgrammingMode);
  }

  if (rules.invalidModuleCount > 0)
  {
    // Some modules are not yet valid
    rules.SetError(InternalErrorCode::WaitingForModulesToReply);
  }

  if (_controller_state == ControllerState::Running && rules.zeroVoltageModuleCount > 0)
  {
    rules.SetError(InternalErrorCode::ZeroVoltModule);
    rules.rule_outcome[Rule::BMSError] = true;
  }

  rules.RunRules(
      mysettings.rulevalue,
      mysettings.rulehysteresis,
      emergencyStop,
      minutesSinceMidnight(),
      &currentMonitor);

  if (_controller_state == ControllerState::Stabilizing)
  {
    // Check for zero volt modules - not a problem whilst we are in stabilizing start up mode
    if (rules.zeroVoltageModuleCount == 0 && rules.invalidModuleCount == 0)
    {
      // Every module has been read and they all returned a voltage move to running state
      SetControllerState(ControllerState::Running);
    }
  }

  if (rules.rule_outcome[Rule::EmergencyStop])
  {
    // Lowest 3 bits are RGB led GREEN/RED/BLUE
    LED(RGBLED::Red);
  }

  if (rules.numberOfActiveErrors > 0 && _tft_screen_available)
  {
    // We have active errors

    // Wake up the screen, this will also trigger it to update the display
    if (tftwakeup_task_handle != NULL)
    {
      xTaskNotify(tftwakeup_task_handle, 0x00, eNotifyAction::eNoAction);
    }
  }
}

void pulse_relay_off_task(void *param)
{
  for (;;)
  {
    // Wait until this task is triggered
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Now wait 200ms before switching off the relays
    vTaskDelay(pdMS_TO_TICKS(200));

    for (int8_t y = 0; y < RELAY_TOTAL; y++)
    {
      if (previousRelayPulse[y])
      {
        // We now need to rapidly turn off the relay after a fixed period of time (pulse mode)
        // However we leave the relay and previousRelayState looking like the relay has triggered (it has!)
        // to prevent multiple pulses being sent on each rule refresh
        hal.SetOutputState(y, RelayState::RELAY_OFF);

        previousRelayPulse[y] = false;
      }
    }

    // Fire task to record state of outputs to SD Card
    xTaskNotify(sdcardlog_outputs_task_handle, 0x00, eNotifyAction::eNoAction);
  }
}

void rules_task(void *param)
{
  for (;;)
  {
    // 3 seconds
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Run the rules
    ProcessRules();

#if defined(RULES_LOGGING)
    for (int8_t r = 0; r < RELAY_RULES; r++)
    {
      if (rules.rule_outcome[r])
      {
        ESP_LOGD(TAG, "Rule outcome %i=TRUE", r);
      }
    }
#endif

    RelayState relay[RELAY_TOTAL];

    // Set defaults based on configuration
    for (int8_t y = 0; y < RELAY_TOTAL; y++)
    {
      relay[y] = mysettings.rulerelaydefault[y] == RELAY_ON ? RELAY_ON : RELAY_OFF;
    }

    // Test the rules (in reverse order)
    for (int8_t n = RELAY_RULES - 1; n >= 0; n--)
    {
      if (rules.rule_outcome[n] == true)
      {
        for (int8_t y = 0; y < RELAY_TOTAL; y++)
        {
          // Dont change relay if its set to ignore/X
          if (mysettings.rulerelaystate[n][y] != RELAY_X)
          {
            if (mysettings.rulerelaystate[n][y] == RELAY_ON)
            {
              relay[y] = RELAY_ON;
            }
            else
            {
              relay[y] = RELAY_OFF;
            }
          }
        }
      }
    }

    uint8_t changes = 0;
    bool firePulse = false;
    for (int8_t n = 0; n < RELAY_TOTAL; n++)
    {
      if (previousRelayState[n] != relay[n])
      {
        ESP_LOGI(TAG, "Set relay %i=%i", n, relay[n] == RelayState::RELAY_ON ? 1 : 0);
        changes++;

        // This would be better if we worked out the bit pattern first and then
        // just submitted that as a single i2c read/write transaction
        hal.SetOutputState(n, relay[n]);

        // Record the previous state of the relay, to use on the next loop
        // to prevent chatter
        previousRelayState[n] = relay[n];

        if (mysettings.relaytype[n] == RELAY_PULSE)
        {
          previousRelayPulse[n] = true;
          firePulse = true;
          ESP_LOGI(TAG, "Relay %i PULSED", n);
        }
      }
    }

    if (firePulse)
    {
      xTaskNotify(pulse_relay_off_task_handle, 0x00, eNotifyAction::eNoAction);
    }

    if (changes)
    {
      // Fire task to record state of outputs to SD Card
      xTaskNotify(sdcardlog_outputs_task_handle, 0x00, eNotifyAction::eNoAction);
    }
  }
}

void enqueue_task(void *param)
{
  for (;;)
  {
    vTaskDelay(pdMS_TO_TICKS(mysettings.interpacketgap));

    LED(RGBLED::Green);
    // Fire task to switch off LED in a few ms
    xTaskNotify(ledoff_task_handle, 0x00, eNotifyAction::eNoAction);

    uint16_t i = 0;
    uint16_t max = TotalNumberOfCells();

    uint8_t startmodule = 0;

    while (i < max)
    {
      uint16_t endmodule = (startmodule + maximum_cell_modules_per_packet) - 1;

      // Limit to number of modules we have configured
      if (endmodule > max)
      {
        endmodule = max - 1;
      }

      // Request voltage, but if queue is full, sleep and try again (other threads will reduce the queue)
      while (transmitProc.sendCellVoltageRequest(startmodule, endmodule) == false)
      {
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      // Same for temperature
      while (transmitProc.sendCellTemperatureRequest(startmodule, endmodule) == false)
      {
        vTaskDelay(pdMS_TO_TICKS(500));
      }

      // If any module is in bypass then request PWM reading for whole bank
      for (uint8_t m = startmodule; m <= endmodule; m++)
      {
        if (cmi[m].inBypass)
        {
          while (transmitProc.sendReadBalancePowerRequest(startmodule, endmodule) == false)
          {
            vTaskDelay(pdMS_TO_TICKS(500));
          }
          // We only need 1 reading for whole bank
          break;
        }
      }

      // Move to the next bank
      startmodule = endmodule + 1;
      i += maximum_cell_modules_per_packet;
    }
  }
}

void connectToWifi()
{
  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED)
  {
    return;
  }

  /*
WiFi.status() only returns:

    switch(status) {
        case STATION_GOT_IP:
            return WL_CONNECTED;
        case STATION_NO_AP_FOUND:
            return WL_NO_SSID_AVAIL;
        case STATION_CONNECT_FAIL:
        case STATION_WRONG_PASSWORD:
            return WL_CONNECT_FAILED;
        case STATION_IDLE:
            return WL_IDLE_STATUS;
        default:
            return WL_DISCONNECTED;
    }
*/

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  uint32_t chipId = 0;
  for (int i = 0; i < 17; i = i + 8)
  {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  // DIYBMS-00000000
  sprintf(hostname, "DIYBMS-%08X", chipId);
  WiFi.setHostname(hostname);

  ESP_LOGI(TAG, "Hostname: %s, current state %i", hostname, status);

  WiFi.begin(DIYBMSSoftAP::Config()->wifi_ssid, DIYBMSSoftAP::Config()->wifi_passphrase);
}

void connectToMqtt()
{
  if (mysettings.mqtt_enabled && WiFi.isConnected())
  {
    if (mqttClient.connected() == false)
    {
      // ESP_LOGD(TAG, "MQTT Enabled");
      mqttClient.setServer(mysettings.mqtt_server, mysettings.mqtt_port);
      mqttClient.setCredentials(mysettings.mqtt_username, mysettings.mqtt_password);

      ESP_LOGI(TAG, "Connect MQTT");
      mqttClient.connect();
    }
  }

  if (mysettings.mqtt_enabled == false && mqttClient.connected())
  {
    // We are connected but shouldn't be!
    ESP_LOGI(TAG, "Disconnecting MQTT");
    mqttClient.disconnect(true);
  }
}

void influxdb_task(void *param)
{
  for (;;)
  {
    // Delay between transmissions
    for (size_t i = 0; i < mysettings.influxdb_loggingFreqSeconds; i++)
    {
      // Wait 1 second
      vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (mysettings.influxdb_enabled 
          && WiFi.isConnected() 
          && rules.invalidModuleCount == 0 
          && _controller_state == ControllerState::Running 
          && rules.rule_outcome[Rule::BMSError] == false)
    {
      ESP_LOGI(TAG, "Influx task");
      influx_task_action();
    }
  }
}

void onWifiConnect(WiFiEvent_t event, WiFiEventInfo_t info)
{

  ESP_LOGI(TAG, "Wi-Fi status=%i", (uint16_t)WiFi.status());

  ESP_LOGI(TAG, "Request NTP from %s", mysettings.ntpServer);

  // Use native ESP32 code
  configTime(mysettings.timeZone * 3600 + mysettings.minutesTimeZone * 60, mysettings.daylight * 3600, mysettings.ntpServer);

  /*
  TODO: CHECK ERROR CODES BETTER!
  0 : WL_IDLE_STATUS when Wi-Fi is in process of changing between statuses
  1 : WL_NO_SSID_AVAIL in case configured SSID cannot be reached
  3 : WL_CONNECTED after successful connection is established
  4 : WL_CONNECT_FAILED if password is incorrect
  6 : WL_DISCONNECTED if module is not configured in station mode
  */
  if (!server_running)
  {
    DIYBMSServer::StartServer(&server, &mysettings, &SD, &transmitProc, &receiveProc, &_controller_state, &rules, &sdcardaction_callback, &hal);
    server_running = true;
  }

  connectToMqtt();

  // Set up mDNS responder:
  // - first argument is the domain name, in this example
  //   the fully-qualified domain name is "esp8266.local"
  // - second argument is the IP address to advertise
  //   we send our IP address on the WiFi network
  if (!MDNS.begin(WiFi.getHostname()))
  {
    ESP_LOGE(TAG, "Error setting up MDNS responder!");
  }
  else
  {
    ESP_LOGI(TAG, "mDNS responder started");
    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
  }

  ESP_LOGI(TAG, "You can access DIYBMS interface at http://%s.local or http://%s", WiFi.getHostname(), WiFi.localIP().toString().c_str());

  // Wake up the screen, this will show the IP address etc.
  if (tftwakeup_task_handle != NULL)
  {
    xTaskNotify(tftwakeup_task_handle, 0x01, eNotifyAction::eSetValueWithOverwrite);
  }
}

void onWifiDisconnect(WiFiEvent_t event, WiFiEventInfo_t info)
{
  ESP_LOGE(TAG, "Disconnected from Wi-Fi.");

  // Indicate to loop() to reconnect, seems to be
  // ESP issues using Wifi from timers - https://github.com/espressif/arduino-esp32/issues/2686

  // Wake up the screen, this will also trigger it to update the display
  if (tftwakeup_task_handle != NULL)
  {
    xTaskNotify(tftwakeup_task_handle, 0x01, eNotifyAction::eSetValueWithOverwrite);
  }
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  ESP_LOGE(TAG, "Disconnected from MQTT.");
}

void mqtt2(void *param)
{
  for (;;)
  {
    // Delay 25 seconds
    vTaskDelay(pdMS_TO_TICKS(25000));

    if (mysettings.mqtt_enabled && mqttClient.connected())
    {
      // ESP_LOGI(TAG, "Send MQTT Status");

      char topic[80];
      char jsonbuffer[400];
      DynamicJsonDocument doc(400);
      JsonObject root = doc.to<JsonObject>();

      root["banks"] = mysettings.totalNumberOfBanks;
      root["cells"] = mysettings.totalNumberOfSeriesModules;
      root["uptime"] = millis() / 1000; // I want to know the uptime of the device.

      // Set error flag if we have attempted to send 2*number of banks without a reply
      root["commserr"] = receiveProc.HasCommsTimedOut() ? 1 : 0;
      root["sent"] = transmitProc.packetsGenerated;
      root["received"] = receiveProc.packetsReceived;
      root["badcrc"] = receiveProc.totalCRCErrors;
      root["ignored"] = receiveProc.totalNotProcessedErrors;
      root["oos"] = receiveProc.totalOutofSequenceErrors;
      root["sendqlvl"] = requestQueue.getCount();
      root["roundtrip"] = receiveProc.packetTimerMillisecond;

      serializeJson(doc, jsonbuffer, sizeof(jsonbuffer));
      sprintf(topic, "%s/status", mysettings.mqtt_topic);
      mqttClient.publish(topic, 0, false, jsonbuffer);
#if defined(MQTT_LOGGING)
      ESP_LOGD(TAG, "MQTT %s %s", topic, jsonbuffer);
// SERIAL_DEBUG.print("MQTT - ");SERIAL_DEBUG.print(topic);  SERIAL_DEBUG.print('=');  SERIAL_DEBUG.println(jsonbuffer);
#endif

      // Output bank level information (just voltage for now)
      for (int8_t bank = 0; bank < mysettings.totalNumberOfBanks; bank++)
      {
        doc.clear();
        doc["voltage"] = (float)rules.packvoltage[bank] / (float)1000.0;

        serializeJson(doc, jsonbuffer, sizeof(jsonbuffer));
        sprintf(topic, "%s/bank/%d", mysettings.mqtt_topic, bank);
        mqttClient.publish(topic, 0, false, jsonbuffer);
#if defined(MQTT_LOGGING)
        ESP_LOGD(TAG, "MQTT %s %s", topic, jsonbuffer);
// SERIAL_DEBUG.print("MQTT - ");SERIAL_DEBUG.print(topic);  SERIAL_DEBUG.print('=');  SERIAL_DEBUG.println(jsonbuffer);
#endif
      }

      // Using Json for below reduced MQTT messages from 14 to 2. Could be combined into same json object too. But even better is status + event driven.
      doc.clear(); // Need to clear the json object for next message
      sprintf(topic, "%s/rule", mysettings.mqtt_topic);
      for (uint8_t i = 0; i < RELAY_RULES; i++)
      {
        doc[(String)i] = rules.rule_outcome[i] ? 1 : 0; // String conversion should be removed but just quick to get json format nice
      }
      serializeJson(doc, jsonbuffer, sizeof(jsonbuffer));
#if defined(MQTT_LOGGING)
      ESP_LOGD(TAG, "MQTT %s %s", topic, jsonbuffer);
#endif
      mqttClient.publish(topic, 0, false, jsonbuffer);

      doc.clear(); // Need to clear the json object for next message
      sprintf(topic, "%s/output", mysettings.mqtt_topic);
      for (uint8_t i = 0; i < RELAY_TOTAL; i++)
      {
        doc[(String)i] = (previousRelayState[i] == RelayState::RELAY_ON) ? 1 : 0;
      }

      serializeJson(doc, jsonbuffer, sizeof(jsonbuffer));
#if defined(MQTT_LOGGING)
      ESP_LOGD(TAG, "MQTT %s %s", topic, jsonbuffer);
#endif
      mqttClient.publish(topic, 0, false, jsonbuffer);

    } // end if
  }   // end for
}

uint16_t calculateCRC(const uint8_t *frame, uint8_t bufferSize)
{
  uint16_t flag;
  uint16_t temp;
  temp = 0xFFFF;
  for (unsigned char i = 0; i < bufferSize; i++)
  {
    temp = temp ^ frame[i];
    for (unsigned char j = 1; j <= 8; j++)
    {
      flag = temp & 0x0001;
      temp >>= 1;
      if (flag)
        temp ^= 0xA001;
    }
  }

  return temp;
}

typedef union
{
  float value;
  uint16_t word[2];
} FloatUnionType;

uint8_t SetModbusRegistersFromFloat(uint8_t *cmd, uint8_t ptr, float value)
{
  FloatUnionType fut;
  fut.value = value;
  // 4 bytes
  cmd[ptr++] = (uint8_t)(fut.word[0] >> 8);
  cmd[ptr++] = (uint8_t)(fut.word[0] & 0xFF);
  cmd[ptr++] = (uint8_t)(fut.word[1] >> 8);
  cmd[ptr++] = (uint8_t)(fut.word[1] & 0xFF);

  return ptr;
}

void PZEM017_SetShuntType(uint8_t modbusAddress, uint16_t shuntMaxCurrent)
{
  // Default 100A
  uint8_t shuntType;

  switch (shuntMaxCurrent)
  {
  case 50:
    shuntType = 1;
    break;
  case 200:
    shuntType = 2;
    break;
  case 300:
    shuntType = 3;
    break;
  default:
    // 100amp
    shuntType = 0;
    break;
  }

  uint8_t cmd[MAX_SEND_RS485_PACKET_LENGTH];
  memset(&cmd, 0, sizeof(cmd));

  cmd[0] = modbusAddress;
  // Function Code 6
  cmd[1] = 0x06;
  // Address of the shunt register (3)
  cmd[3] = 0x03;
  // Value
  cmd[5] = shuntType;

  ESP_LOGD(TAG, "Set PZEM017 max current %uA=%u", shuntMaxCurrent, shuntType);

  xQueueSend(rs485_transmit_q_handle, &cmd, portMAX_DELAY);

  // Zero all data
  memset(&currentMonitor, 0, sizeof(currentmonitoring_struct));
  currentMonitor.validReadings = false;
}

void PZEM017_SetDeviceAddress(uint8_t newAddress)
{
  // First we force the PZEM device to assume the selected MODBUS address using
  // the special "broadcast" address of 0xF8.  Technically the PZEM devices
  // support multiple devices on same RS485 bus, but DIYBMS doesn't....

  uint8_t cmd[MAX_SEND_RS485_PACKET_LENGTH];
  memset(&cmd, 0, sizeof(cmd));

  // The configuration address (only 1 PZEM device can be connected)
  cmd[0] = 0xf8;
  // Function Code 6
  cmd[1] = 0x06;
  // Register 2
  // cmd[2] = 0;
  cmd[3] = 2;
  // value
  // cmd[4] = 0;
  cmd[5] = newAddress;

  ESP_LOGD(TAG, "Sent PZEM_017 change address");

  // Zero all data
  memset(&currentMonitor, 0, sizeof(currentmonitoring_struct));
  currentMonitor.validReadings = false;
  xQueueSend(rs485_transmit_q_handle, &cmd, portMAX_DELAY);
}

void currentMon_ConfigureBasic(uint16_t shuntmv, uint16_t shuntmaxcur, uint16_t batterycapacity, float fullchargevolt, float tailcurrent, float chargeefficiency)
{
  uint16_t chargeeff = chargeefficiency * 100.0;

  uint8_t cmd[MAX_SEND_RS485_PACKET_LENGTH];
  memset(&cmd, 0, sizeof(cmd));

  //	Write Multiple Holding Registers
  uint8_t cmd2[] = {
      // The Slave Address
      mysettings.currentMonitoringModBusAddress,
      // The Function Code 16
      16,
      // Data Address of the first register (zero based so 18 = register 40019)
      0,
      18,
      // number of registers to write
      0,
      8,
      // number of data bytes to follow (2 registers x 2 bytes each = 4 bytes)
      16,
      // value to write to register 40019 |40019|shunt_max_current  (unsigned int16)
      (uint8_t)(shuntmaxcur >> 8),
      (uint8_t)(shuntmaxcur & 0xFF),
      // value to write to register 40020 |40020|shunt_millivolt  (unsigned int16)
      (uint8_t)(shuntmv >> 8),
      (uint8_t)(shuntmv & 0xFF),
      //|40021|Battery Capacity (ah)  (unsigned int16)
      (uint8_t)(batterycapacity >> 8),
      (uint8_t)(batterycapacity & 0xFF),
      //|40022|Fully charged voltage (4 byte double)
      0,
      0,
      0,
      0,
      //|40024|Tail current (Amps) (4 byte double)
      0,
      0,
      0,
      0,
      //|40026|Charge efficiency factor % (unsigned int16) (scale x100 eg. 10000 = 100.00%, 9561 = 95.61%)
      (uint8_t)(chargeeff >> 8),
      (uint8_t)(chargeeff & 0xFF),
  };

  uint8_t ptr = SetModbusRegistersFromFloat(cmd2, 13, fullchargevolt);
  ptr = SetModbusRegistersFromFloat(cmd2, ptr, tailcurrent);

  memcpy(&cmd, &cmd2, sizeof(cmd2));
  xQueueSend(rs485_transmit_q_handle, &cmd, portMAX_DELAY);

  // Zero all data
  memset(&currentMonitor, 0, sizeof(currentmonitoring_struct));
  currentMonitor.validReadings = false;
}

void CurrentMonitorSetBasicSettings(uint16_t shuntmv, uint16_t shuntmaxcur, uint16_t batterycapacity, float fullchargevolt, float tailcurrent, float chargeefficiency)
{
  if (mysettings.currentMonitoringDevice == CurrentMonitorDevice::DIYBMS_CURRENT_MON)
  {
    currentMon_ConfigureBasic(shuntmv, shuntmaxcur, batterycapacity, fullchargevolt, tailcurrent, chargeefficiency);
  }

  if (mysettings.currentMonitoringDevice == CurrentMonitorDevice::PZEM_017)
  {
    PZEM017_SetDeviceAddress(mysettings.currentMonitoringModBusAddress);
    PZEM017_SetShuntType(mysettings.currentMonitoringModBusAddress, shuntmaxcur);
  }
}

// Save the current monitor advanced settings back to the device over MODBUS/RS485
void CurrentMonitorSetRelaySettings(currentmonitoring_struct newvalues)
{
  uint8_t flag1 = 0;
  uint8_t flag2 = 0;

  flag1 += newvalues.TempCompEnabled ? B00000010 : 0;

  // Use the previous value for setting the ADCRange4096mV flag
  flag1 += currentMonitor.ADCRange4096mV ? B00000001 : 0;

  // Apply new settings
  flag2 += newvalues.RelayTriggerTemperatureOverLimit ? bit(DIAG_ALRT_FIELD::TMPOL) : 0;
  flag2 += newvalues.RelayTriggerCurrentOverLimit ? bit(DIAG_ALRT_FIELD::SHNTOL) : 0;
  flag2 += newvalues.RelayTriggerCurrentUnderLimit ? bit(DIAG_ALRT_FIELD::SHNTUL) : 0;
  flag2 += newvalues.RelayTriggerVoltageOverlimit ? bit(DIAG_ALRT_FIELD::BUSOL) : 0;
  flag2 += newvalues.RelayTriggerVoltageUnderlimit ? bit(DIAG_ALRT_FIELD::BUSUL) : 0;
  flag2 += newvalues.RelayTriggerPowerOverLimit ? bit(DIAG_ALRT_FIELD::POL) : 0;

  /*
Flag 1
10|Temperature compensation enabled|Read write
9|ADC Range 0=±163.84 mV, 1=±40.96 mV (only 40.96mV supported by diyBMS)|Read only

Flag 2
8|Relay Trigger on TMPOL|Read write
7|Relay Trigger on SHNTOL|Read write
6|Relay Trigger on SHNTUL|Read write
5|Relay Trigger on BUSOL|Read write
4|Relay Trigger on BUSUL|Read write
3|Relay Trigger on POL|Read write
*/

  uint8_t cmd[MAX_SEND_RS485_PACKET_LENGTH];
  memset(&cmd, 0, sizeof(cmd));

  //	Write Multiple Holding Registers

  // The Slave Address
  cmd[0] = mysettings.currentMonitoringModBusAddress;
  // The Function Code 16
  cmd[1] = 16;
  // Data Address of the first register (9=40010, Various status flags)
  cmd[2] = 0;
  cmd[3] = 9;
  // number of registers to write
  cmd[4] = 0;
  cmd[5] = 1;
  // number of data bytes to follow (2 registers x 2 bytes each = 4 bytes)
  cmd[6] = 2;
  // value to write to register 40010
  cmd[7] = flag1;
  cmd[8] = flag2;

  xQueueSend(rs485_transmit_q_handle, &cmd, portMAX_DELAY);

  ESP_LOGD(TAG, "Write register 10 = %u %u", flag1, flag2);

  // Zero all data
  memset(&currentMonitor, 0, sizeof(currentmonitoring_struct));
  currentMonitor.validReadings = false;
}

// Save the current monitor advanced settings back to the device over MODBUS/RS485
void CurrentMonitorSetAdvancedSettings(currentmonitoring_struct newvalues)
{

  //	Write Multiple Holding Registers
  uint8_t cmd2[] = {
      // The Slave Address
      mysettings.currentMonitoringModBusAddress,
      // The Function Code 16
      16,
      // Data Address of the first register (|40028|INA_REGISTER::SHUNT_CAL (unsigned int16))
      0,
      27,
      // number of registers to write
      0,
      13,
      // number of data bytes to follow (13 registers x 2 bytes each)
      2 * 13,
      // value to write to register 40028
      // 21 = shuntcal
      (uint8_t)(newvalues.modbus.shuntcal >> 8),
      (uint8_t)(newvalues.modbus.shuntcal & 0xFF),
      // value to write to register 40029
      // temperaturelimit
      (uint8_t)(newvalues.modbus.temperaturelimit >> 8),
      (uint8_t)(newvalues.modbus.temperaturelimit & 0xFF),
      // overvoltagelimit 40030
      0,
      0,
      0,
      0,
      // undervoltagelimit 40032
      0,
      0,
      0,
      0,
      // overcurrentlimit 40034
      0,
      0,
      0,
      0,
      // undercurrentlimit 40029
      0,
      0,
      0,
      0,
      // overpowerlimit 40038
      0,
      0,
      0,
      0,
      // shunttempcoefficient 40
      (uint8_t)(newvalues.modbus.shunttempcoefficient >> 8),
      (uint8_t)(newvalues.modbus.shunttempcoefficient & 0xFF),
  };

  // ESP_LOGD(TAG, "temp limit=%i", newvalues.temperaturelimit);
  // ESP_LOGD(TAG, "shuntcal=%u", newvalues.shuntcal);

  // Register 18 = shunt_max_current
  // Register 19 = shunt_millivolt

  uint8_t ptr = SetModbusRegistersFromFloat(cmd2, 11, newvalues.modbus.overvoltagelimit);
  ptr = SetModbusRegistersFromFloat(cmd2, ptr, newvalues.modbus.undervoltagelimit);
  ptr = SetModbusRegistersFromFloat(cmd2, ptr, newvalues.modbus.overcurrentlimit);
  ptr = SetModbusRegistersFromFloat(cmd2, ptr, newvalues.modbus.undercurrentlimit);
  ptr = SetModbusRegistersFromFloat(cmd2, ptr, newvalues.modbus.overpowerlimit);

  /*
    newvalues.shuntcal = p1->value().toInt();
    newvalues.temperaturelimit = p1->value().toInt();
    newvalues.overvoltagelimit = p1->value().toFloat();
    newvalues.undervoltagelimit = p1->value().toFloat();
    newvalues.overcurrentlimit = p1->value().toFloat();
    newvalues.undercurrentlimit = p1->value().toFloat();
    newvalues.overpowerlimit = p1->value().toFloat();
    newvalues.shunttempcoefficient = p1->value().toInt();
*/

  uint8_t cmd[MAX_SEND_RS485_PACKET_LENGTH];
  memset(&cmd, 0, sizeof(cmd));
  memcpy(&cmd, &cmd2, sizeof(cmd2));
  xQueueSend(rs485_transmit_q_handle, &cmd, portMAX_DELAY);

  ESP_LOGD(TAG, "Advanced save settings");

  // Zero all data
  memset(&currentMonitor, 0, sizeof(currentmonitoring_struct));
  currentMonitor.validReadings = false;
}

// Swap the two 16 bit words in a 32bit word
static inline unsigned int word16swap32(unsigned int __bsx)
{
  return ((__bsx & 0xffff0000) >> 16) | ((__bsx & 0x0000ffff) << 16);
}

// Extract the current monitor MODBUS registers into our internal STRUCTURE variables
void ProcessDIYBMSCurrentMonitorRegisterReply(uint8_t length)
{
  // ESP_LOGD(TAG, "Modbus len=%i, struct len=%i", length, sizeof(currentmonitor_raw_modbus));

  // ESP_LOG_BUFFER_HEXDUMP(TAG, &frame[3], length, esp_log_level_t::ESP_LOG_DEBUG);

  if (sizeof(currentmonitor_raw_modbus) != length)
  {
    // Abort if the packet sizes are different
    memset(&currentMonitor.modbus, 0, sizeof(currentmonitor_raw_modbus));
    currentMonitor.validReadings = false;
    return;
  }

  // Now byte swap to align to ESP32 endiness, and copy as we go into new structure
  uint8_t *ptr = (uint8_t *)&currentMonitor.modbus;
  for (size_t i = 0; i < length; i += 2)
  {
    uint8_t temp = frame[3 + i];
    ptr[i] = frame[i + 4];
    ptr[i + 1] = temp;
  }

  // Finally, we have to fix the 32 bit fields
  currentMonitor.modbus.milliamphour_out = word16swap32(currentMonitor.modbus.milliamphour_out);
  currentMonitor.modbus.milliamphour_in = word16swap32(currentMonitor.modbus.milliamphour_in);
  currentMonitor.modbus.firmwareversion = word16swap32(currentMonitor.modbus.firmwareversion);
  currentMonitor.modbus.firmwaredatetime = word16swap32(currentMonitor.modbus.firmwaredatetime);

  // ESP_LOG_BUFFER_HEXDUMP(TAG, &currentMonitor.modbus, sizeof(currentmonitor_raw_modbus), esp_log_level_t::ESP_LOG_DEBUG);

  currentMonitor.timestamp = esp_timer_get_time();

  // High byte
  uint8_t flag1 = currentMonitor.modbus.flags >> 8;
  // Low byte
  uint8_t flag2 = currentMonitor.modbus.flags;

  // ESP_LOGD(TAG, "Read relay trigger settings %u %u", flag1, flag2);

  /*
16|TMPOL|Read only
15|SHNTOL|Read only
14|SHNTUL|Read only
13|BUSOL|Read only
12|BUSUL|Read only
11|POL|Read only
10|Temperature compensation enabled|Read write
9|ADC Range 0=±163.84 mV, 1=±40.96 mV (only 40.96mV supported by diyBMS)|Read only
*/

  currentMonitor.TemperatureOverLimit = flag1 & bit(DIAG_ALRT_FIELD::TMPOL);
  currentMonitor.CurrentOverLimit = flag1 & bit(DIAG_ALRT_FIELD::SHNTOL);
  currentMonitor.CurrentUnderLimit = flag1 & bit(DIAG_ALRT_FIELD::SHNTUL);
  currentMonitor.VoltageOverlimit = flag1 & bit(DIAG_ALRT_FIELD::BUSOL);
  currentMonitor.VoltageUnderlimit = flag1 & bit(DIAG_ALRT_FIELD::BUSUL);
  currentMonitor.PowerOverLimit = flag1 & bit(DIAG_ALRT_FIELD::POL);

  currentMonitor.TempCompEnabled = flag1 & B00000010;
  currentMonitor.ADCRange4096mV = flag1 & B00000001;

  /*
8|Relay Trigger on TMPOL|Read write
7|Relay Trigger on SHNTOL|Read write
6|Relay Trigger on SHNTUL|Read write
5|Relay Trigger on BUSOL|Read write
4|Relay Trigger on BUSUL|Read write
3|Relay Trigger on POL|Read write
2|Existing Relay state (0=off)|Read write
1|Factory reset bit (always 0 when read)|Read write
*/
  currentMonitor.RelayTriggerTemperatureOverLimit = flag2 & bit(DIAG_ALRT_FIELD::TMPOL);
  currentMonitor.RelayTriggerCurrentOverLimit = flag2 & bit(DIAG_ALRT_FIELD::SHNTOL);
  currentMonitor.RelayTriggerCurrentUnderLimit = flag2 & bit(DIAG_ALRT_FIELD::SHNTUL);
  currentMonitor.RelayTriggerVoltageOverlimit = flag2 & bit(DIAG_ALRT_FIELD::BUSOL);
  currentMonitor.RelayTriggerVoltageUnderlimit = flag2 & bit(DIAG_ALRT_FIELD::BUSUL);
  currentMonitor.RelayTriggerPowerOverLimit = flag2 & bit(DIAG_ALRT_FIELD::POL);
  currentMonitor.RelayState = flag2 & B00000010;
  // Last bit is for factory reset (always zero)

  currentMonitor.chargeefficiency = ((float)currentMonitor.modbus.raw_chargeefficiency) / 100.0;
  currentMonitor.stateofcharge = ((float)currentMonitor.modbus.raw_stateofcharge) / 100.0;

  currentMonitor.validReadings = true;

  /*
  ESP_LOGD(TAG, "WDog = %u", currentMonitor.modbus.watchdogcounter);
  ESP_LOGD(TAG, "SOC = %i", currentMonitor.stateofcharge);

  ESP_LOGD(TAG, "Volt = %f", currentMonitor.modbus.voltage);
  ESP_LOGD(TAG, "Curr = %f", currentMonitor.modbus.current);
  ESP_LOGD(TAG, "Temp = %i", currentMonitor.modbus.temperature);

  ESP_LOGD(TAG, "Out = %f", currentMonitor.modbus.milliamphour_in);
  ESP_LOGD(TAG, "In = %f", currentMonitor.modbus.milliamphour_out);

  ESP_LOGD(TAG, "Ver = %x", currentMonitor.modbus.firmwareversion);
  ESP_LOGD(TAG, "Date = %u", currentMonitor.modbus.firmwaredatetime);
*/
}

#ifdef USE_CANBUS
void victron_canbus_tx(void *param)
{
  for (;;)
  {
    // Delay 1 seconds
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (mysettings.VictronEnabled)
    {
      // minimum CAN-IDs required for the core functionality are 0x351, 0x355, 0x356 and 0x35A.

      // 351 message must be sent at least every 3 seconds - or Victron will stop charge/discharge
      victron_message_351();

      // Delay a little whilst sending packets to give ESP32 some breathing room and not flood the CANBUS
      vTaskDelay(pdMS_TO_TICKS(100));

      // Advertise the diyBMS name on CANBUS
      victron_message_370_371();
      victron_message_35e();
      victron_message_35a();
      victron_message_372();
      victron_message_35f();

      vTaskDelay(pdMS_TO_TICKS(100));

      if (_controller_state == ControllerState::Running)
      {
        victron_message_355();
        victron_message_356();

        vTaskDelay(pdMS_TO_TICKS(100));

        // Detail about individual cells
        victron_message_373();
        victron_message_374_375_376_377();
      }
    }
  }
}

void victron_canbus_rx(void *param)
{
  for (;;)
  {

    if (mysettings.VictronEnabled)
    {

      // Wait for message to be received
      can_message_t message;
      esp_err_t res = can_receive(&message, pdMS_TO_TICKS(10000));
      if (res == ESP_OK)
      {
        canbus_messages_received++;
        // ESP_LOGI(TAG, "CANBUS received message");

        /*
        SERIAL_DEBUG.println("Message received\n");
        SERIAL_DEBUG.print("\nID is 0x");
        SERIAL_DEBUG.print(message.identifier, HEX);
        SERIAL_DEBUG.print("=");
        if (!(message.flags & CAN_MSG_FLAG_RTR))
        {
          for (int i = 0; i < message.data_length_code; i++)
          {
            dumpByte(message.data[i]);
            SERIAL_DEBUG.print(" ");
          }
        }
        SERIAL_DEBUG.println();
        */
      }
      else if (res == ESP_ERR_TIMEOUT)
      {
        /// ignore the timeout or do something
        ESP_LOGE(TAG, "CANBUS timeout");
      }

      /*
      // check the health of the bus
      can_status_info_t status;
      can_get_status_info(&status);
      SERIAL_DEBUG.printf("  rx-q:%d, tx-q:%d, rx-err:%d, tx-err:%d, arb-lost:%d, bus-err:%d, state: %s",
                          status.msgs_to_rx, status.msgs_to_tx, status.rx_error_counter, status.tx_error_counter, status.arb_lost_count,
                          status.bus_error_count, ESP32_CAN_STATUS_STRINGS[status.state]);
      if (status.state == can_state_t::CAN_STATE_BUS_OFF)
      {
        // When the bus is OFF we need to initiate recovery, transmit is
        // not possible when in this state.
        SERIAL_DEBUG.printf("ESP32-CAN: initiating recovery");
        can_initiate_recovery();
      }
      else if (status.state == can_state_t::CAN_STATE_RECOVERING)
      {
        // when the bus is in recovery mode transmit is not possible.
        //delay(200);
      }
      */
    }
    else
    {
      // Canbus is disbled, sleep....
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
}
#endif // USE_CANBUS

void service_rs485_transmit_q(void *param)
{
  for (;;)
  {
    uint8_t cmd[MAX_SEND_RS485_PACKET_LENGTH];

    // Wait for a item in the queue, blocking indefinately
    xQueueReceive(rs485_transmit_q_handle, &cmd, portMAX_DELAY);

    if (hal.GetRS485Mutex())
    {

      // Ensure we have empty receive buffer
      // uart_flush_input(rs485_uart_num);

      // Default of 8 bytes for a modbus request (including CRC)
      size_t packet_length = 8;

      if (cmd[1] == 15 || cmd[1] == 16)
      {
        // Calculate length of this packet, add on extra data
        // Force Multiple Coils (FC=15)
        // https://www.simplymodbus.ca/FC15.htm
        // Preset Multiple Registers (FC=16)
        // https://www.simplymodbus.ca/FC16.htm
        packet_length = 9 + cmd[6];
      }

      // Calculate the MODBUS CRC. Yes, it is byteswapped.
      uint16_t temp = calculateCRC(cmd, packet_length - 2);
      cmd[packet_length - 2] = temp & 0xFF;
      cmd[packet_length - 1] = temp >> 8;

      // Send the bytes (actually just put them into the TX FIFO buffer)
      uart_write_bytes(rs485_uart_num, (char *)cmd, packet_length);

      hal.ReleaseRS485Mutex();

      // Notify the receive task that a packet should be on its way
      if (rs485_rx_task_handle != NULL)
      {
        xTaskNotify(rs485_rx_task_handle, 0x00, eNotifyAction::eNoAction);
      }

      ESP_LOGD(TAG, "Send addr=%u, func=%u, len=%u", cmd[0], cmd[1], packet_length);
      // Debug
      // ESP_LOG_BUFFER_HEXDUMP(TAG, cmd, packet_length, esp_log_level_t::ESP_LOG_DEBUG);

      // Once we have notified the receive task, we pause here to avoid sending
      // another request until the last one has been processed (or we timeout after 2 seconds)
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
    }
  }
}

// RS485 receive
void rs485_rx(void *param)
{
  for (;;)
  {
    // Wait until this task is triggered (sending queue task triggers it)
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Delay 50ms for the data to arrive
    vTaskDelay(pdMS_TO_TICKS(50));

    int len = 0;

    if (hal.GetRS485Mutex())
    {
      // Wait 200ms before timeout
      len = uart_read_bytes(rs485_uart_num, frame, sizeof(frame), pdMS_TO_TICKS(200));
      hal.ReleaseRS485Mutex();
    }

    // Min packet length of 5 bytes
    if (len > 5)
    {
      uint8_t id = frame[0];

      uint16_t crc = ((frame[len - 1] << 8) | frame[len - 2]); // combine the crc Low & High bytes.
      // Yes, this is byte swapped.

      // ESP_LOG_BUFFER_HEXDUMP(TAG, frame, len, esp_log_level_t::ESP_LOG_DEBUG);

      if (calculateCRC(frame, len - 2) == crc)
      {
        // if the calculated crc matches the received crc continue to process data...
        uint8_t RS485Error = frame[1] & B10000000;
        if (RS485Error == 0)
        {
          uint8_t cmd = frame[1] & B01111111;
          uint8_t length = frame[2];

          ESP_LOGD(TAG, "Recv %i bytes, id=%u, cmd=%u", len, id, cmd);
          // ESP_LOG_BUFFER_HEXDUMP(TAG, frame, len, esp_log_level_t::ESP_LOG_DEBUG);

          if (mysettings.currentMonitoringDevice == CurrentMonitorDevice::PZEM_017)
          {
            if (cmd == 6 && id == 248)
            {
              ESP_LOGI(TAG, "Reply to broadcast/change address");
            }
            if (cmd == 6 && id == mysettings.currentMonitoringModBusAddress)
            {
              ESP_LOGI(TAG, "Reply to set param");
            }
            else if (cmd == 3 && id == mysettings.currentMonitoringModBusAddress)
            {
              // 75mV shunt (hard coded for PZEM)
              currentMonitor.modbus.shuntmillivolt = 75;

              // Shunt type 0x0000 - 0x0003 (100A/50A/200A/300A)
              switch (((uint32_t)frame[9] << 8 | (uint32_t)frame[10]))
              {
              case 0:
                currentMonitor.modbus.shuntmaxcurrent = 100;
                break;
              case 1:
                currentMonitor.modbus.shuntmaxcurrent = 50;
                break;
              case 2:
                currentMonitor.modbus.shuntmaxcurrent = 200;
                break;
              case 3:
                currentMonitor.modbus.shuntmaxcurrent = 300;
                break;
              default:
                currentMonitor.modbus.shuntmaxcurrent = 0;
              }
            }
            else if (cmd == 4 && id == mysettings.currentMonitoringModBusAddress && len == 21)
            {
              // ESP_LOG_BUFFER_HEXDUMP(TAG, frame, len, esp_log_level_t::ESP_LOG_DEBUG);

              // memset(&currentMonitor.modbus, 0, sizeof(currentmonitor_raw_modbus));
              currentMonitor.validReadings = true;
              currentMonitor.timestamp = esp_timer_get_time();
              // voltage in 0.01V
              currentMonitor.modbus.voltage = (float)((uint32_t)frame[3] << 8 | (uint32_t)frame[4]) / (float)100.0;
              // current in 0.01A
              currentMonitor.modbus.current = (float)((uint32_t)frame[5] << 8 | (uint32_t)frame[6]) / (float)100.0;
              // power in 0.1W
              currentMonitor.modbus.power = ((uint32_t)frame[7] << 8 | (uint32_t)frame[8] | (uint32_t)frame[9] << 24 | (uint32_t)frame[10] << 16) / 10.0;
            }
            else
            {
              // Dump out unhandled reply
              ESP_LOG_BUFFER_HEXDUMP(TAG, frame, len, esp_log_level_t::ESP_LOG_DEBUG);
            }
          }
          // ESP_LOGD(TAG, "CRC pass Id=%u F=%u L=%u", id, cmd, length);
          if (mysettings.currentMonitoringDevice == CurrentMonitorDevice::DIYBMS_CURRENT_MON)
          {
            if (id == mysettings.currentMonitoringModBusAddress && cmd == 3)
            {
              ProcessDIYBMSCurrentMonitorRegisterReply(length);

              if (_tft_screen_available)
              {
                // Refresh the TFT display
                xTaskNotify(updatetftdisplay_task_handle, 0x00, eNotifyAction::eNoAction);
              }
            }
            else if (id == mysettings.currentMonitoringModBusAddress && cmd == 16)
            {
              ESP_LOGI(TAG, "Write multiple regs, success");
            }
            else
            {
              // Dump out unhandled reply
              ESP_LOG_BUFFER_HEXDUMP(TAG, frame, len, esp_log_level_t::ESP_LOG_DEBUG);
            }
          }
        }
        else
        {
          ESP_LOGE(TAG, "RS485 error");
          ESP_LOG_BUFFER_HEXDUMP(TAG, frame, len, esp_log_level_t::ESP_LOG_DEBUG);
        }
      }
      else
      {
        ESP_LOGE(TAG, "CRC error");
      }
    }
    else
    {
      // We didn't receive anything on RS485, record error and mark current monitor as invalid
      ESP_LOGE(TAG, "Short packet %i bytes", len);

      // Indicate that the current monitor values are now invalid/unknown
      currentMonitor.validReadings = false;
    }

    // Notify sending queue, to continue
    xTaskNotify(service_rs485_transmit_q_task_handle, 0x00, eNotifyAction::eNoAction);

  } // infinite loop
}

// RS485 transmit
void rs485_tx(void *param)
{
  uint8_t cmd[MAX_SEND_RS485_PACKET_LENGTH];
  memset(&cmd, 0, sizeof(cmd));

  for (;;)
  {
    // Delay 5 seconds
    vTaskDelay(pdMS_TO_TICKS(5000));

    if (mysettings.currentMonitoringEnabled == true)
    {
      if (mysettings.currentMonitoringDevice == CurrentMonitorDevice::DIYBMS_CURRENT_MON)
      {
        // This is the request we send to diyBMS current monitor, it pulls back 38 registers
        // this is all the registers diyBMS current monitor has
        // Holding Registers = command 3
        cmd[0] = mysettings.currentMonitoringModBusAddress;
        // Input registers - 46 of them (92 bytes + headers + crc = 83 byte reply)
        cmd[1] = 3;
        cmd[5] = 46;
        xQueueSend(rs485_transmit_q_handle, &cmd, portMAX_DELAY);
      }

      if (mysettings.currentMonitoringDevice == CurrentMonitorDevice::PZEM_017)
      {
        // ESP_LOGD(TAG, "RS485 TX");
        cmd[0] = mysettings.currentMonitoringModBusAddress;

        if (currentMonitor.modbus.shuntmillivolt == 0)
        {
          ESP_LOGD(TAG, "PZEM_017 Read params");
          cmd[1] = 0x03;
          cmd[5] = 0x04;
        }
        else
        {
          // ESP_LOGD(TAG, "PZEM_017 Read values");
          //  Read the standard voltage/current values
          //   Input registers
          cmd[1] = 0x04;
          // Read 8 registers (0 to 8)
          cmd[5] = 0x08;
        }
        xQueueSend(rs485_transmit_q_handle, &cmd, portMAX_DELAY);
      }
    } // end if
  }
}

void mqtt1(void *param)
{
  // Send a few MQTT packets and keep track so we send the next batch on following calls
  static uint8_t mqttStartModule = 0;
  static int64_t lastcurrentMonitortimestamp = 0;

  for (;;)
  {
    // Delay 5 seconds
    vTaskDelay(pdMS_TO_TICKS(5000));

    if (mysettings.mqtt_enabled && mqttClient.connected() == false)
    {
      ESP_LOGE(TAG, "MQTT enabled, but not connected");
    }

    if (mysettings.mqtt_enabled && mqttClient.connected())
    {

      char topic[80];
      char jsonbuffer[300];
      StaticJsonDocument<300> doc;

      // If the BMS is in error, stop sending MQTT packets for the data
      if (!rules.rule_outcome[Rule::BMSError])
      {

        if (mqttStartModule > (TotalNumberOfCells() - 1))
        {
          mqttStartModule = 0;
        }

        uint8_t counter = 0;
        uint8_t i = mqttStartModule;

        while (i < TotalNumberOfCells() && counter < 8)
        {
          // ESP_LOGI(TAG, "Send MQTT for module %u", i);
          // Only send valid module data
          if (cmi[i].valid)
          {
            uint8_t bank = i / mysettings.totalNumberOfSeriesModules;
            uint8_t module = i - (bank * mysettings.totalNumberOfSeriesModules);

            doc.clear();
            doc["voltage"] = (float)cmi[i].voltagemV / (float)1000.0;
            doc["vMax"] = (float)cmi[i].voltagemVMax / (float)1000.0;
            doc["vMin"] = (float)cmi[i].voltagemVMin / (float)1000.0;
            doc["inttemp"] = cmi[i].internalTemp;
            doc["exttemp"] = cmi[i].externalTemp;
            doc["bypass"] = cmi[i].inBypass ? 1 : 0;
            doc["PWM"] = (int)((float)cmi[i].PWMValue / (float)255.0 * 100);
            doc["bypassT"] = cmi[i].bypassOverTemp ? 1 : 0;
            doc["bpc"] = cmi[i].badPacketCount;
            doc["mAh"] = cmi[i].BalanceCurrentCount;
            serializeJson(doc, jsonbuffer, sizeof(jsonbuffer));

            sprintf(topic, "%s/%d/%d", mysettings.mqtt_topic, bank, module);

            mqttClient.publish(topic, 0, false, jsonbuffer);

#if defined(MQTT_LOGGING)
            ESP_LOGI(TAG, "MQTT %s %s", topic, jsonbuffer);
#endif
          }

          counter++;

          i++;
        }

        // After transmitting this many packets over MQTT, store our current state and exit the function.
        // this prevents flooding the ESP controllers wifi stack and potentially causing reboots/fatal exceptions
        mqttStartModule = i;
      }

      if (mysettings.currentMonitoringEnabled)
      {
        // Send current monitor data
        doc.clear(); // Need to clear the json object for next message
        sprintf(topic, "%s/modbus/A%u", mysettings.mqtt_topic, mysettings.currentMonitoringModBusAddress);

        doc["valid"] = currentMonitor.validReadings ? 1 : 0;

        if (currentMonitor.validReadings && currentMonitor.timestamp != lastcurrentMonitortimestamp)
        {
          // Send current monitor data if its valid and not sent before
          doc["voltage"] = currentMonitor.modbus.voltage;
          doc["current"] = currentMonitor.modbus.current;
          doc["power"] = currentMonitor.modbus.power;
          if (mysettings.currentMonitoringDevice == CurrentMonitorDevice::DIYBMS_CURRENT_MON)
          {
            doc["mAhIn"] = currentMonitor.modbus.milliamphour_in;
            doc["mAhOut"] = currentMonitor.modbus.milliamphour_out;
            doc["temperature"] = currentMonitor.modbus.temperature;
            doc["shuntmV"] = currentMonitor.modbus.shuntmV;
            doc["relayState"] = currentMonitor.RelayState ? 1 : 0;
            doc["soc"] = currentMonitor.stateofcharge;
          }
        }

        lastcurrentMonitortimestamp = currentMonitor.timestamp;

        serializeJson(doc, jsonbuffer, sizeof(jsonbuffer));
#if defined(MQTT_LOGGING)
        ESP_LOGD(TAG, "MQTT %s %s", topic, jsonbuffer);
#endif
        mqttClient.publish(topic, 0, false, jsonbuffer);
      }
    }
  }
}

void onMqttConnect(bool sessionPresent)
{
  ESP_LOGI(TAG, "Connected to MQTT");
}

void LoadConfiguration()
{
  if (Settings::ReadConfig("diybms", (char *)&mysettings, sizeof(mysettings)))
    return;

  ESP_LOGI(TAG, "Apply default config");

  // Zero all the bytes
  memset(&mysettings, 0, sizeof(mysettings));

  // Default to a single module
  mysettings.totalNumberOfBanks = 1;
  mysettings.totalNumberOfSeriesModules = 1;
  // Default serial port speed
  mysettings.baudRate = COMMS_BAUD_RATE;
  mysettings.BypassMaxTemp = 65;
  mysettings.interpacketgap = 6000;
  // 4.10V bypass
  mysettings.BypassThresholdmV = 4100;
  mysettings.graph_voltagehigh = 4.5;
  mysettings.graph_voltagelow = 2.75;

  // EEPROM settings are invalid so default configuration
  mysettings.mqtt_enabled = false;
  mysettings.mqtt_port = 1883;

  mysettings.VictronEnabled = false;

  // Charge current limit (CCL)
  mysettings.ccl[VictronDVCC::Default] = 10 * 10;
  // Charge voltage limit (CVL)
  mysettings.cvl[VictronDVCC::Default] = 12 * 10;
  // Discharge current limit (DCL)
  mysettings.dcl[VictronDVCC::Default] = 10 * 10;

  // Balance
  mysettings.ccl[VictronDVCC::Balance] = 10 * 10;
  mysettings.cvl[VictronDVCC::Balance] = 10 * 10;
  mysettings.dcl[VictronDVCC::Balance] = 10 * 10;
  // Error
  mysettings.ccl[VictronDVCC::ControllerError] = 0 * 10;
  mysettings.cvl[VictronDVCC::ControllerError] = 0 * 10;
  mysettings.dcl[VictronDVCC::ControllerError] = 0 * 10;

  mysettings.loggingEnabled = false;
  mysettings.loggingFrequencySeconds = 15;

  mysettings.currentMonitoringEnabled = false;
  mysettings.currentMonitoringModBusAddress = 90;
  mysettings.currentMonitoringDevice = CurrentMonitorDevice::DIYBMS_CURRENT_MON;

  mysettings.rs485baudrate = 19200;
  mysettings.rs485databits = uart_word_length_t::UART_DATA_8_BITS;
  mysettings.rs485parity = uart_parity_t::UART_PARITY_DISABLE;
  mysettings.rs485stopbits = uart_stop_bits_t::UART_STOP_BITS_1;

  mysettings.currentMonitoringEnabled = false;

  strcpy(mysettings.language, "en");

  // Default to EMONPI default MQTT settings
  strcpy(mysettings.mqtt_topic, "emon/diybms");
  strcpy(mysettings.mqtt_server, "192.168.0.26");
  strcpy(mysettings.mqtt_username, "emonpi");
  strcpy(mysettings.mqtt_password, "emonpimqtt2016");

  mysettings.influxdb_enabled = false;
  strcpy(mysettings.influxdb_serverurl, "http://192.168.0.49:8086/api/v2/write");
  strcpy(mysettings.influxdb_databasebucket, "bucketname");
  strcpy(mysettings.influxdb_orgid, "organisation");
  mysettings.influxdb_loggingFreqSeconds = 15;

  mysettings.timeZone = 0;
  mysettings.minutesTimeZone = 0;
  mysettings.daylight = false;
  strcpy(mysettings.ntpServer, "time.google.com");

  for (size_t x = 0; x < RELAY_TOTAL; x++)
  {
    mysettings.rulerelaydefault[x] = RELAY_OFF;
  }

  // Emergency stop
  mysettings.rulevalue[Rule::EmergencyStop] = 0;
  // Internal BMS error (communication issues, fault readings from modules etc)
  mysettings.rulevalue[Rule::BMSError] = 0;
  // Current monitoring maximum AMPS
  mysettings.rulevalue[Rule::CurrentMonitorOverCurrentAmps] = 100;
  // Individual cell over voltage
  mysettings.rulevalue[Rule::ModuleOverVoltage] = 4150;
  // Individual cell under voltage
  mysettings.rulevalue[Rule::ModuleUnderVoltage] = 3000;
  // Individual cell over temperature (external probe)
  mysettings.rulevalue[Rule::ModuleOverTemperatureExternal] = 55;
  // Pack over voltage (mV)
  mysettings.rulevalue[Rule::ModuleUnderTemperatureExternal] = 5;
  // Pack under voltage (mV)
  mysettings.rulevalue[Rule::BankOverVoltage] = 4200 * 8;
  // RULE_PackUnderVoltage
  mysettings.rulevalue[Rule::BankUnderVoltage] = 3000 * 8;
  mysettings.rulevalue[Rule::Timer1] = 60 * 8;  // 8am
  mysettings.rulevalue[Rule::Timer2] = 60 * 17; // 5pm

  mysettings.rulevalue[Rule::ModuleOverTemperatureInternal] = 60;
  mysettings.rulevalue[Rule::ModuleUnderTemperatureInternal] = 5;

  mysettings.rulevalue[Rule::CurrentMonitorOverVoltage] = 4200 * 8;
  mysettings.rulevalue[Rule::CurrentMonitorUnderVoltage] = 3000 * 8;

  for (size_t i = 0; i < RELAY_RULES; i++)
  {
    mysettings.rulehysteresis[i] = mysettings.rulevalue[i];

    // Set all relays to don't care
    for (size_t x = 0; x < RELAY_TOTAL; x++)
    {
      mysettings.rulerelaystate[i][x] = RELAY_X;
    }
  }

  for (size_t x = 0; x < RELAY_TOTAL; x++)
  {
    mysettings.relaytype[x] = RELAY_STANDARD;
  }
}

// Do activities which are not critical to the system like background loading of config, or updating timing results etc.
void lazy_tasks(void *param)
{
  for (;;)
  {
    // TODO: Perhaps this should be based on some improved logic - based on number of modules in system?
    //  Delay 6.5 seconds

    ESP_LOGI(TAG, "Sleep");
    TickType_t delay_ticks = pdMS_TO_TICKS(6500);
    vTaskDelay(delay_ticks);

    // Task 1
    ESP_LOGI(TAG, "Task 1");
    //  Send a "ping" message through the cells to get a round trip time
    while (transmitProc.sendTimingRequest() == false)
    {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Sleep between sections to give the ESP a chance to do other stuff
    vTaskDelay(delay_ticks);

    // Task 2
    ESP_LOGI(TAG, "Task 2");
    uint8_t counter = 0;
    // Find modules that don't have settings cached and request them
    for (uint8_t module = 0; module < TotalNumberOfCells(); module++)
    {
      if (cmi[module].valid && !cmi[module].settingsCached)
      {
        while (transmitProc.sendGetSettingsRequest(module) == false)
        {
          vTaskDelay(pdMS_TO_TICKS(1000));
        };
        counter++;
      }
    }

    // Sleep between sections to give the ESP a chance to do other stuff
    vTaskDelay(delay_ticks);

    // Task 3
    //  Send these requests to all banks of modules
    uint16_t i = 0;
    uint16_t max = TotalNumberOfCells();

    uint8_t startmodule = 0;

    while (i < max)
    {
      uint16_t endmodule = (startmodule + maximum_cell_modules_per_packet) - 1;

      // Limit to number of modules we have configured
      if (endmodule > max)
      {
        endmodule = max - 1;
      }

      ESP_LOGI(TAG, "Task 3, s=%i e=%i", startmodule, endmodule);
      while (transmitProc.sendReadBalanceCurrentCountRequest(startmodule, endmodule) == false)
      {
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
      while (transmitProc.sendReadPacketCountersRequest(startmodule, endmodule) == false)
      {
        vTaskDelay(pdMS_TO_TICKS(1000));
      }

      // Delay per bank/loop
      vTaskDelay(delay_ticks);

      // Move to the next bank
      startmodule = endmodule + 1;
      i += maximum_cell_modules_per_packet;
    } // end while

  } // end for
}

void resetAllRules()
{
  // Clear all rules
  for (int8_t r = 0; r < RELAY_RULES; r++)
  {
    rules.rule_outcome[r] = false;
  }
}

bool CaptureSerialInput(HardwareSerial stream, char *buffer, int buffersize, bool OnlyDigits, bool ShowPasswordChar)
{
  int length = 0;
  unsigned long timer = millis() + 30000;

  while (true)
  {

    // Abort after 30 seconds of inactivity
    if (millis() > timer)
      return false;

    // We should add a timeout in here, and return FALSE when we abort....
    while (stream.available())
    {
      // Reset timer on serial input
      timer = millis() + 30000;

      int data = stream.read();
      if (data == '\b' || data == '\177')
      { // BS and DEL
        if (length)
        {
          length--;
          stream.write("\b \b");
        }
      }
      else if (data == '\n')
      {
        // Ignore
      }
      else if (data == '\r')
      {
        if (length > 0)
        {
          stream.write("\r\n"); // output CRLF
          buffer[length] = '\0';

          // Soak up any other characters on the buffer and throw away
          while (stream.available())
          {
            stream.read();
          }

          // Return to caller
          return true;
        }

        length = 0;
      }
      else if (length < buffersize - 1)
      {
        if (OnlyDigits && (data < '0' || data > '9'))
        {
          // We need to filter out non-digit characters
        }
        else
        {
          buffer[length++] = data;
          if (ShowPasswordChar)
          {
            // Hide real character
            stream.write('*');
          }
          else
          {
            stream.write(data);
          }
        }
      }
    }
  }
}

void TerminalBasedWifiSetup(HardwareSerial stream)
{
  stream.println(F("\r\n\r\nDIYBMS CONTROLLER - Scanning Wifi"));

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect();

  int n = WiFi.scanNetworks();

  if (n == 0)
    stream.println(F("no networks found"));
  else
  {
    for (int i = 0; i < n; ++i)
    {
      if (i < 10)
      {
        stream.print(' ');
      }
      stream.print(i);
      stream.print(':');
      stream.print(WiFi.SSID(i));

      // Pad out the wifi names into 2 columns
      for (size_t spaces = WiFi.SSID(i).length(); spaces < 36; spaces++)
      {
        stream.print(' ');
      }

      if ((i + 1) % 2 == 0)
      {
        stream.println();
      }
      delay(5);
    }
    stream.println();
  }

  WiFi.mode(WIFI_OFF);

  stream.print(F("Enter the NUMBER of the Wifi network to connect to:"));

  bool result;
  char buffer[10];
  result = CaptureSerialInput(stream, buffer, 10, true, false);
  if (result)
  {
    int index = String(buffer).toInt();
    stream.print(F("Enter the password to use when connecting to '"));
    stream.print(WiFi.SSID(index));
    stream.print("':");

    char passwordbuffer[80];
    result = CaptureSerialInput(stream, passwordbuffer, 80, false, true);

    if (result)
    {
      wifi_eeprom_settings config;
      memset(&config, 0, sizeof(config));
      WiFi.SSID(index).toCharArray(config.wifi_ssid, sizeof(config.wifi_ssid));
      strcpy(config.wifi_passphrase, passwordbuffer);
      Settings::WriteConfig("diybmswifi", (char *)&config, sizeof(config));
    }
  }

  stream.println(F("REBOOTING IN 5..."));
  delay(5000);
  ESP.restart();
}

void createFile(fs::FS &fs, const char *path, const char *message)
{
  ESP_LOGD(TAG, "Writing file: %s", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    ESP_LOGE(TAG, "Failed to open file for writing");
    return;
  }
  if (file.print(message))
  {
    ESP_LOGD(TAG, "File written");
  }
  else
  {
    ESP_LOGE(TAG, "Write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char *path, const char *message)
{
  ESP_LOGD(TAG, "Appending to file: %s", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file)
  {
    ESP_LOGE(TAG, "Failed to open file for appending");
    return;
  }
  if (file.print(message))
  {
    ESP_LOGD(TAG, "Message appended");
  }
  else
  {
    ESP_LOGE(TAG, "Append failed");
  }
  file.close();
}

void dumpByte(uint8_t data)
{
  if (data <= 0x0F)
  {
    SERIAL_DEBUG.print('0');
  }
  SERIAL_DEBUG.print(data, HEX);
}

// CHECK HERE FOR THE PRESENCE OF A /wifi.json CONFIG FILE ON THE SD CARD TO AUTOMATICALLY CONFIGURE WIFI
bool LoadWiFiConfigFromSDCard(bool existingConfigValid)
{
  bool ret = false;
  if (_sd_card_installed)
  {
    const char *wificonfigfilename = "/diybms/wifi.json";

    ESP_LOGI(TAG, "Checking for %s", wificonfigfilename);

    if (hal.GetVSPIMutex())
    {
      if (SD.exists(wificonfigfilename))
      {
        ESP_LOGD(TAG, "Found file %s", wificonfigfilename);

        StaticJsonDocument<3000> json;
        File file = SD.open(wificonfigfilename);
        DeserializationError error = deserializeJson(json, file);
        file.close();
        // Release Mutex as quickly as possible
        hal.ReleaseVSPIMutex();
        if (error != DeserializationError::Ok)
        {
          ESP_LOGE(TAG, "Error deserialize JSON");
        }
        else
        {
          ESP_LOGD(TAG, "Deserialized %s", wificonfigfilename);

          JsonObject wifi = json["wifi"];

          wifi_eeprom_settings _config;
          // Pointer to existing configuration
          wifi_eeprom_settings *_config_existing;

          _config_existing = DIYBMSSoftAP::Config();

          // Clear config
          memset(&_config, 0, sizeof(_config));

          String ssid = wifi["ssid"].as<String>();
          String password = wifi["password"].as<String>();
          ssid.toCharArray(_config.wifi_ssid, sizeof(_config.wifi_ssid));
          password.toCharArray(_config.wifi_passphrase, sizeof(_config.wifi_passphrase));

          // Our configuration is different, so store the details in EEPROM and flash the LED a few times
          if (existingConfigValid == false || strcmp(_config_existing->wifi_ssid, _config.wifi_ssid) != 0 || strcmp(_config_existing->wifi_passphrase, _config.wifi_passphrase) != 0)
          {
            ESP_LOGD(TAG, "Wifi JSON SSID=%s", _config.wifi_ssid);
            ESP_LOGI(TAG, "Wifi config is different, saving");

            Settings::WriteConfig("diybmswifi", (char *)&_config, sizeof(_config));

            for (size_t i = 0; i < 5; i++)
            {
              LED(RGBLED::Purple);
              delay(150);
              LED(RGBLED::OFF);
              delay(150);
            }

            ret = true;
          }
          else
          {
            ESP_LOGI(TAG, "Wifi JSON config is identical, ignoring");
          }
        }
      }
      else
      {
        // Didn't find the file, but still need to release mutex
        hal.ReleaseVSPIMutex();
      }
    }
  }
  return ret;
}

void setup()
{
  WiFi.mode(WIFI_OFF);

  esp_bt_controller_disable();
  esp_log_level_set("*", ESP_LOG_DEBUG);    // set all components to WARN level
  esp_log_level_set("wifi", ESP_LOG_WARN);  // enable WARN logs from WiFi stack
  esp_log_level_set("dhcpc", ESP_LOG_WARN); // enable INFO logs from DHCP client

  const char *diybms_logo = "\r\n\r\n\r\n                _          __ \r\n    _|  o      |_)  |\\/|  (_  \r\n   (_|  |  \\/  |_)  |  |  __) \r\n           /                  ";

  // ESP32 we use the USB serial interface for console/debug messages
  SERIAL_DEBUG.begin(115200, SERIAL_8N1);
  SERIAL_DEBUG.setDebugOutput(true);

  SERIAL_DEBUG.println(diybms_logo);

  ESP_LOGI(TAG, "CONTROLLER - ver:%s compiled %s", GIT_VERSION, COMPILE_DATE_TIME);

  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  ESP_LOGI(TAG, "ESP32 Chip model = %u, Rev %u, Cores=%u, Features=%u", chip_info.model, chip_info.revision, chip_info.cores, chip_info.features);

  hal.ConfigurePins(WifiPasswordClear);
  hal.ConfigureI2C(TCA6408Interrupt, TCA9534AInterrupt);
  hal.ConfigureVSPI();

  _avrsettings.inProgress = false;
  _avrsettings.programmingModeEnabled = false;

  // See if we can get a sensible reading from the TFT touch chip XPT2046
  // if we can, then a screen is fitted, so enable it
  _tft_screen_available = hal.IsScreenAttached();

  if (_tft_screen_available)
  {
    ESP_LOGI(TAG, "TFT screen is INSTALLED");
    // Only attach, if device is fitted otherwise false triggers may occur
    // Touch screen IRQ (GPIO_NUM_36) is active LOW (XPT2046 chip)
    attachInterrupt(TOUCH_IRQ, TFTScreenTouchInterrupt, FALLING);
  }
  else
  {
    ESP_LOGI(TAG, "TFT screen is NOT installed");
  }

  SetControllerState(ControllerState::PowerUp);

  hal.Led(0);

  if (!LITTLEFS.begin(false))
  {
    ESP_LOGE(TAG, "LITTLEFS mount failed, did you upload file system image?");

    hal.Halt(RGBLED::White);
  }
  else
  {
    ESP_LOGI(TAG, "LITTLEFS mounted, totalBytes=%u, usedBytes=%u", LITTLEFS.totalBytes(), LITTLEFS.usedBytes());
    // listDir(LITTLEFS, "/", 0);
  }

  mountSDCard();

  // Switch CAN chip TJA1051T/3 ON
  hal.CANBUSEnable(true);

  hal.ConfigureCAN();

  hal.ConfigureVSPI();
  init_tft_display();

  // Pre configure the array
  memset(&cmi, 0, sizeof(cmi));
  for (size_t i = 0; i < maximum_controller_cell_modules; i++)
  {
    DIYBMSServer::clearModuleValues(i);
  }

  resetAllRules();

  LoadConfiguration();
  ESP_LOGI("Config loaded");

  // Check its not zero
  if (mysettings.influxdb_loggingFreqSeconds < 5)
  {
    mysettings.influxdb_loggingFreqSeconds = 15;
  }

  // Receive is IO2 which means the RX1 plug must be disconnected for programming to work!
  SERIAL_DATA.begin(mysettings.baudRate, SERIAL_8N1, 2, 32); // Serial for comms to modules

  initSerializer();

  SetupRS485();

  // Create queue for transmit, each request could be MAX_SEND_RS485_PACKET_LENGTH bytes long, depth of 3 items
  rs485_transmit_q_handle = xQueueCreate(3, MAX_SEND_RS485_PACKET_LENGTH);

  if (rs485_transmit_q_handle == NULL)
  {
    ESP_LOGE("Failed to create queue");
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  xTaskCreate(ledoff_task, "ledoff", 1500, nullptr, 1, &ledoff_task_handle);
  xTaskCreate(tftwakeup_task, "tftwake", 1900, nullptr, 1, &tftwakeup_task_handle);
  xTaskCreate(tftsleep_task, "tftslp", 900, nullptr, 1, &tftsleep_task_handle);

  xTaskCreate(voltageandstatussnapshot_task, "snap", 1950, nullptr, 1, &voltageandstatussnapshot_task_handle);
  xTaskCreate(updatetftdisplay_task, "tftupd", 2048, nullptr, 1, &updatetftdisplay_task_handle);
#ifdef USE_AVRISP
  xTaskCreate(avrprog_task, "avrprog", 2500, &_avrsettings, configMAX_PRIORITIES - 5, &avrprog_task_handle);
#endif

  xTaskCreate(tca6408_isr_task, "tca6408", 2048, nullptr, configMAX_PRIORITIES - 3, &tca6408_isr_task_handle);
  xTaskCreate(tca9534_isr_task, "tca9534", 2048, nullptr, configMAX_PRIORITIES - 3, &tca9534_isr_task_handle);

  xTaskCreate(wifiresetdisable_task, "wifidbl", 800, nullptr, 1, &wifiresetdisable_task_handle);
  xTaskCreate(sdcardlog_task, "sdlog", 3600, nullptr, 1, &sdcardlog_task_handle);
  xTaskCreate(sdcardlog_outputs_task, "sdout", 4000, nullptr, 1, &sdcardlog_outputs_task_handle);
  xTaskCreate(mqtt1, "mqtt1", 4096, nullptr, 1, &mqtt1_task_handle);
  xTaskCreate(mqtt2, "mqtt2", 4096, nullptr, 1, &mqtt2_task_handle);

  xTaskCreate(rs485_tx, "485_TX", 3000, nullptr, 1, &rs485_tx_task_handle);
  xTaskCreate(rs485_rx, "485_RX", 3000, nullptr, 1, &rs485_rx_task_handle);
  xTaskCreate(service_rs485_transmit_q, "485_Q", 3000, nullptr, 1, &service_rs485_transmit_q_task_handle);

  xTaskCreate(victron_canbus_tx, "v_cantx", 3000, nullptr, 1, &victron_canbus_tx_task_handle);
  xTaskCreate(victron_canbus_rx, "v_canrx", 3000, nullptr, 1, &victron_canbus_rx_task_handle);

  // We process the transmit queue every 1 second (this needs to be lower delay than the queue fills)
  // and slower than it takes a single module to process a command (about 200ms @ 2400baud)

  xTaskCreate(transmit_task, "tx", 2048, nullptr, configMAX_PRIORITIES - 3, &transmit_task_handle);
  xTaskCreate(replyqueue_task, "rxq", 2048, nullptr, configMAX_PRIORITIES - 2, &replyqueue_task_handle);
  xTaskCreate(lazy_tasks, "lazyt", 2048, nullptr, 1, &lazy_task_handle);
  xTaskCreate(pulse_relay_off_task, "pulse", 2048, nullptr, configMAX_PRIORITIES - 1, &pulse_relay_off_task_handle);

  // Set relay defaults
  for (int8_t y = 0; y < RELAY_TOTAL; y++)
  {
    previousRelayState[y] = mysettings.rulerelaydefault[y];
    // Set relay defaults
    hal.SetOutputState(y, mysettings.rulerelaydefault[y]);
  }
  // Fire task to record state of outputs to SD Card
  xTaskNotify(sdcardlog_outputs_task_handle, 0x00, eNotifyAction::eNoAction);

  // Allow user to press SPACE BAR key on serial terminal
  // to enter text based WIFI setup
  SERIAL_DEBUG.print(F("Press SPACE BAR to enter terminal based configuration...."));
  for (size_t i = 0; i < (3000 / 250); i++)
  {
    SERIAL_DEBUG.print('.');
    while (SERIAL_DEBUG.available())
    {
      int x = SERIAL_DEBUG.read();
      // SPACE BAR
      if (x == 32)
      {
        TerminalBasedWifiSetup(SERIAL_DEBUG);
      }
    }
    delay(250);
  }
  SERIAL_DEBUG.println(F("skipped"));
  SERIAL_DEBUG.flush();

  // Retrieve the EEPROM WIFI settings
  bool EepromConfigValid = DIYBMSSoftAP::LoadConfigFromEEPROM();

  if (LoadWiFiConfigFromSDCard(EepromConfigValid))
  {
    // We need to reload the configuration, as it was updated...
    EepromConfigValid = DIYBMSSoftAP::LoadConfigFromEEPROM();
  }

  // Temporarly force WIFI settings
  // wifi_eeprom_settings xxxx;
  // strcpy(xxxx.wifi_ssid,"XXXXXX");
  // strcpy(xxxx.wifi_passphrase,"XXXXXX");
  // Settings::WriteConfig("diybmswifi",(char *)&config, sizeof(config));
  // clearAPSettings = 0;

  if (!EepromConfigValid)
  {
    // We have just started up and the EEPROM is empty of configuration
    SetControllerState(ControllerState::ConfigurationSoftAP);

    ESP_LOGI(TAG, "Setup Access Point");
    // We are in initial power on mode (factory reset)
    DIYBMSSoftAP::SetupAccessPoint(&server);
  }
  else
  {
    /* Explicitly set the ESP to be a WiFi-client, otherwise by default,
      would try to act as both a client and an access-point */
    WiFi.onEvent(onWifiConnect, system_event_id_t::SYSTEM_EVENT_STA_GOT_IP);
    WiFi.onEvent(onWifiDisconnect, system_event_id_t::SYSTEM_EVENT_STA_DISCONNECTED);
    // Newer IDF version will need this...
    // WiFi.onEvent(onWifiConnect, arduino_event_id_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    // WiFi.onEvent(onWifiDisconnect, arduino_event_id_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);

    connectToMqtt();

    xTaskCreate(enqueue_task, "enqueue", 2048, nullptr, configMAX_PRIORITIES / 2, &enqueue_task_handle);
    xTaskCreate(rules_task, "rules", 2048, nullptr, configMAX_PRIORITIES - 5, &rule_task_handle);
    xTaskCreate(influxdb_task, "influxdb", 6000, nullptr, 1, &influxdb_task_handle);

    // We have just started...
    SetControllerState(ControllerState::Stabilizing);

    hal.TFTScreenBacklight(false);

    // Attempt connection in setup(), loop() will also try every 30 seconds
    connectToWifi();

    // We generate a unique number which is used in all following JSON requests
    // we use this as a simple method to avoid cross site scripting attacks
    // This MUST be done once the WIFI is switched on otherwise only PSEUDO random
    // data is generated!!
    DIYBMSServer::generateUUID();

    // Wake screen on power up
    xTaskNotify(tftwakeup_task_handle, 0x00, eNotifyAction::eNoAction);
  }
}

unsigned long wifitimer = 0;
unsigned long heaptimer = 0;

unsigned long taskinfotimer = 0;

void loop()
{
  unsigned long currentMillis = millis();

  if (_controller_state != ControllerState::ConfigurationSoftAP)
  {
    // on first pass wifitimer is zero
    if (currentMillis - wifitimer > 30000)
    {
      // Attempt to connect to WiFi every 30 seconds, this caters for when WiFi drops
      // such as AP reboot, its written to return without action if we are already connected
      connectToWifi();
      wifitimer = currentMillis;
      connectToMqtt();
    }
  }

  if (ResetWifi)
  {
    // Password reset, turn LED CYAN
    LED(RGBLED::Cyan);

    // Wipe EEPROM WIFI setting
    DIYBMSSoftAP::FactoryReset();
    ResetWifi = false;
  }

  // Call update to receive, decode and process incoming packets
  myPacketSerial.checkInputStream();

  if (currentMillis > heaptimer)
  {
    /*
    size_t total_free_bytes;      ///<  Total free bytes in the heap. Equivalent to multi_free_heap_size().
    size_t total_allocated_bytes; ///<  Total bytes allocated to data in the heap.
    size_t largest_free_block;    ///<  Size of largest free block in the heap. This is the largest malloc-able size.
    size_t minimum_free_bytes;    ///<  Lifetime minimum free heap size. Equivalent to multi_minimum_free_heap_size().
    size_t allocated_blocks;      ///<  Number of (variable size) blocks allocated in the heap.
    size_t free_blocks;           ///<  Number of (variable size) free blocks in the heap.
    size_t total_blocks;          ///<  Total number of (variable size) blocks in the heap.
    */
    multi_heap_info_t heap;
    heap_caps_get_info(&heap, MALLOC_CAP_INTERNAL);

    ESP_LOGD(TAG, "total_free_byte=%u total_allocated_byte=%u largest_free_blk=%u min_free_byte=%u alloc_blk=%u free_blk=%u total_blk=%u",
             heap.total_free_bytes,
             heap.total_allocated_bytes,
             heap.largest_free_block,
             heap.minimum_free_bytes,
             heap.allocated_blocks,
             heap.free_blocks,
             heap.total_blocks);

    // Report again in 15 seconds
    heaptimer = currentMillis + 15000;
  }
}
