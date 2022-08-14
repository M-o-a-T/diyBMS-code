/*
 ____  ____  _  _  ____  __  __  ___    _  _  __
(  _ \(_  _)( \/ )(  _ \(  \/  )/ __)  ( \/ )/. |
 )(_) )_)(_  \  /  ) _ < )    ( \__ \   \  /(_  _)
(____/(____) (__) (____/(_/\/\_)(___/    \/   (_)

DIYBMS V4.0
CELL MODULE FOR ATTINY841

(c)2019 to 2021 Stuart Pittaway

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
/*
IMPORTANT

You need to configure the correct DIYBMSMODULEVERSION in defines.h file to build for your module

ATTINY chip frequency dropped to 2Mhz to comply with datasheet at low voltages (<2V)
Baud rate changed to 5000bits/second from 26 Jan 2021, 5000 chosen due to 2Mhz frequency and ATTINY bad freq regulation
https://trolsoft.ru/en/uart-calc

*/

#define RX_BUFFER_SIZE 64

#include <Arduino.h>

#if !(F_CPU == 2000000)
#error Processor speed should be 2Mhz
#endif

#if !defined(ATTINY_CORE)
#error Expected ATTINYCORE
#endif

#if !defined(BAUD)
#error Expected BAUD define
#endif

#if !defined(SAMPLEAVERAGING)
#error Expected SAMPLEAVERAGING define
#endif

//Our project code includes
#include "defines.h"
#include "settings.h"
#include <FastPID.h>

#include <SerialPacker.h>
#include "HAL.h"
#include "packet_processor.h"

uint8_t SerialPacketReceiveBuffer[sizeof(PacketRequestAny) + sizeof(PacketHeader)];

SerialPacker myPacketSerial;

//Default values which get overwritten by EEPROM on power up
CellModuleConfig myConfig;

//HAL hardware;

PacketProcessor PP(&myConfig, &myPacketSerial);

volatile bool wdt_triggered = false;

//Interrupt counter
volatile uint8_t InterruptCounter = 0;
volatile uint16_t PulsePeriod = 0;
volatile uint16_t OnPulseCount = 0;
//volatile bool PacketProcessed = false;

// 2.2100, little-endian
#define DEFAULT_CALIBRATION 0x400d70a4

void DefaultConfig()
{
  myConfig.Calibration = DEFAULT_CALIBRATION;

  //2mV per ADC resolution
  //myConfig.mVPerADC = 2.0; //2048.0/1024.0;

#if DIYBMSMODULEVERSION == 420 && !defined(SWAPR19R20)
  //Keep temperature low for modules with R19 and R20 not swapped
  myConfig.BypassTemperature = 715; // ~45 degC
#else
  //Stop running bypass if temperature over 65 degrees C
  myConfig.BypassTemperature = 850; // ~65 degC
#endif

  //Start bypass at 4.1V
  myConfig.BypassThreshold = 955 *SAMPLEAVERAGING; // 955 = 4100/4398*1024

//Kp: Determines how aggressively the PID reacts to the current amount of error (Proportional)
//Ki: Determines how aggressively the PID reacts to error over time (Integral)
//Kd: Determines how aggressively the PID reacts to the change in error (Derivative)

//6Hz rate - number of times we call this code in Loop
//Kp, Ki, Kd, Hz, output_bits, output_signed);
//Settings for V4.00 boards with 2R2 resistors = (4.0, 0.5, 0.2, 6, 8, false);
//
// The float values used in PID configuration were for °C inputs. Now
// we use raw inputs which are ~10 times larger. Thus in order to keep the
// PID behavior constant we divide by 10 here.
//
#define _PM (PARAM_MULT/10)
  myConfig.kp = int(5.0*_PM);
  myConfig.ki = int(1.0*_PM);
  myConfig.kd = int(0.1*_PM);
#undef _PM
}

ISR(WDT_vect)
{
  //This is the watchdog timer - something went wrong and no serial activity received in over 8 seconds
  wdt_triggered = true;
  PP.IncrementWatchdogCounter();
}

ISR(ADC_vect)
{
  // when ADC completed, take an interrupt and process result
  PP.ADCReading(HAL::ReadADC());
}

void onPacketHeader()
{
  HAL::EnableSerial0TX();

  //A packet header has just arrived, process it
  PP.onHeaderReceived((PacketHeader *)SerialPacketReceiveBuffer);

  // also, tell the watchdog that the system seems to be live
  wdt_reset();
}

void onReadReceived()
{
  PP.onReadReceived((PacketHeader *)SerialPacketReceiveBuffer);
}

void onPacketReceived()
{
  PP.onPacketReceived((PacketHeader *)SerialPacketReceiveBuffer);
}

ISR(USART0_START_vect)
{
  //This interrupt exists just to wake up the CPU so that it can read
  //voltage+temperature while the packet arrives
  asm("NOP");
}

FastPID myPID;

void ValidateConfiguration()
{
  if (myConfig.Calibration == 0)
  {
    myConfig.Calibration = DEFAULT_CALIBRATION;
  }

  if (myConfig.BypassTemperature > DIYBMS_MODULE_SafetyTemperatureCutoff)
  {
    myConfig.BypassTemperature = DIYBMS_MODULE_SafetyTemperatureCutoff;
  }

#if DIYBMSMODULEVERSION == 420 && !defined(SWAPR19R20)
  //Keep temperature low for modules with R19 and R20 not swapped
  if (myConfig.BypassTemperature > 715)
  {
    myConfig.BypassTemperature = 715;
  }
#endif
}

void StopBalance()
{
  PP.WeAreInBypass = false;
  PP.bypassCountDown = 0;
  PP.bypassHasJustFinished = 0;
  PP.PWMSetPoint = 0;
  PP.SettingsHaveChanged = false;

  OnPulseCount = 0;
  PulsePeriod = 0;

  HAL::StopTimer1();
  HAL::DumpLoadOff();
}

void setup()
{
  //Must be first line of code
  wdt_disable();
  wdt_reset();

  HAL::SetPrescaler();

  //below 2Mhz is required for running ATTINY at low voltages (less than 2V)

  //8 second between watchdogs
  HAL::SetWatchdog8sec();

  //Setup IO ports
  HAL::ConfigurePorts();

  //More power saving changes
  HAL::DisableSerial1();

  // Start talking
  HAL::EnableSerial0();

  //Check if setup routine needs to be run
  if (!Settings::ReadConfigFromEEPROM((uint8_t *)&myConfig, sizeof(myConfig), EEPROM_CONFIG_ADDRESS))
  {
    DefaultConfig();
    //No need to save here, as the default config will load whenever the CRC is wrong
  }

  ValidateConfiguration();
  myPID.configure(myConfig.kp, myConfig.ki, myConfig.kd, 3, 8, false);

  HAL::double_tap_Notification_led();

#if DIYBMSMODULEVERSION < 430
  HAL::double_tap_blue_led();
#endif

  //The PID can vary between 0 and 255 (1 byte)
  myPID.setOutputRange(0, 255);

  StopBalance();

  //Set up data handler
  Serial.begin(BAUD, SERIAL_8N1);
  Serial.println("Reboot");

  myPacketSerial.begin(&Serial, &onPacketHeader, &onReadReceived, &onPacketReceived, SerialPacketReceiveBuffer, sizeof(SerialPacketReceiveBuffer), sizeof(PacketHeader));

#if 0 // def SP_NONFRAME_STREAM
  SP_NONFRAME_STREAM.println("Module Start");
  SP_NONFRAME_STREAM.flush();
#endif
}

ISR(TIMER1_COMPA_vect)
{
  // This ISR is called every 1 millisecond when TIMER1 is enabled

  // when v=1, the duration between on and off is 2.019ms (1.0095ms per interrupt) - on for 2.019ms off for 255.7ms, 0.7844% duty
  // when v=128 (50%), the duration between on and off is 130.8ms (1.0218ms per interrupt) - on for 130.8ms, off for 127.4ms 50.67% duty
  // when v=192 (75%), the duration between on and off is 195.6ms (1.0187ms per interrupt) - on for 62.63ms, off for 30.83ms, 75.74% duty
  InterruptCounter++;
  PulsePeriod++;

  //Switch the load on if the counter is below the SETPOINT
  if (InterruptCounter <= PP.PWMSetPoint)
  {
    //Enable the pin
    //HAL::SparePinOn();
    HAL::DumpLoadOn();

    //Count the number of "on" periods, so we can calculate the amount of energy consumed
    //over time
    OnPulseCount++;
  }
  else
  {
    //Off
    //HAL::SparePinOff();
    HAL::DumpLoadOff();
  }

  if (PulsePeriod == 1000)
  {
    PP.IncrementBalanceCounter(OnPulseCount);

    OnPulseCount = 0;
    PulsePeriod = 0;
  }
}

inline void identifyModule()
{
  if (PP.identifyModule)
  {
    HAL::NotificationLedOn();
#if DIYBMSMODULEVERSION < 430
    HAL::BlueLedOn();
#endif
  }
  else
  {
    HAL::NotificationLedOff();
#if DIYBMSMODULEVERSION < 430
    HAL::BlueLedOff();
#endif
  }
}

void loop()
{
  //This loop runs around 3 times per second when the module is in bypass
  if (PP.SettingsHaveChanged)
  {
    //The configuration has just been modified so stop balancing if we are and reset our status
    StopBalance();
  }

  noInterrupts();
  if (HAL::CheckSerial0Idle() && myPacketSerial.isIdle())
  {
    // count down twice; once because it's been set as the header arrived,
    // twice to count down for the Identify command
    if(PP.identifyModule) {
      if(--PP.identifyModule)
        --PP.identifyModule;
    }
    identifyModule();

    if (!PP.WeAreInBypass && PP.bypassHasJustFinished == 0)
    {
      //Go to SLEEP, we are not in bypass anymore and no serial data waiting...

      //Reset PID to defaults, in case we want to start balancing
      myPID.clear();

      //Switch off TX - save power
      //HAL::DisableSerial0TX();

      //Wake up on Serial port RX
      HAL::EnableStartFrameDetection();

      //Program stops here until woken by watchdog or Serial port ISR
      HAL::Sleep();

      //We are awake
    }
  }
  else
    identifyModule();
  interrupts(); // in case we didn't sleep

  if (wdt_triggered)
  {
    wdt_triggered = false;
#if DIYBMSMODULEVERSION < 440
    //Flash blue LED twice after a watchdog wake up
    HAL::double_tap_blue_led();
#else
    //Flash Notification LED twice after a watchdog wake up
    HAL::double_tap_Notification_led();
#endif

    //If we have just woken up, we shouldn't be in balance safety check that we are not
    StopBalance();
  }
  interrupts();

  //We always take a voltage and temperature reading on every loop cycle to check if we need to go into bypass
  //this is also triggered by the watchdog should comms fail or the module is running standalone
  HAL::ReferenceVoltageOn();

  //Internal temperature
  PP.TakeAnAnalogueReading(ADC_INTERNAL_TEMP);

  //Only take these readings when we are NOT in bypass....
  //this causes the voltage and temperature to "freeze" during bypass cycles
  if (PP.bypassCountDown == 0)
  {
    //Just for debug purposes, shows when voltage is read
    //#if DIYBMSMODULEVERSION < 430
    //    HAL::BlueLedOn();
    //#endif

    //External temperature
    PP.TakeAnAnalogueReading(ADC_EXTERNAL_TEMP);

#if (SAMPLEAVERAGING == 1)
    //Sample averaging not enabled
    //Do voltage reading last to give as much time for voltage to settle
    PP.TakeAnAnalogueReading(ADC_CELL_VOLTAGE);
#else
    //Take several samples and average the result
    for (size_t i = 0; i < SAMPLEAVERAGING; i++)
    {
      PP.TakeAnAnalogueReading(ADC_CELL_VOLTAGE);
    }
#endif
  }

  //Switch reference off if we are not in bypass (otherwise leave on)
  HAL::ReferenceVoltageOff();

  myPacketSerial.checkInputStream();

  //We should probably check for invalid InternalTemperature ranges here and throw error (shorted or unconnecter thermistor for example)
  uint16_t internal_temperature = PP.InternalTemperature();

  // this roughly approximates the ADC delta for 1 degC around the current temperature
  uint16_t temp_delta = (internal_temperature > 950) ? 2 : 21-internal_temperature/50;

  if (internal_temperature > DIYBMS_MODULE_SafetyTemperatureCutoff || internal_temperature > (myConfig.BypassTemperature + 10*temp_delta))
  {
    //Force shut down if temperature is too high although this does run the risk that the voltage on the cell will go high
    //but the BMS controller should shut off the charger in this situation
    myPID.clear();
    StopBalance();
  }

  //Only enter bypass if the board temperature is below safety
  if (PP.BypassCheck() && internal_temperature < DIYBMS_MODULE_SafetyTemperatureCutoff)
  {
    //Our cell voltage is OVER the voltage setpoint limit, start draining cell using bypass resistor

    if (!PP.WeAreInBypass)
    {
      //We have just entered the bypass code
      PP.WeAreInBypass = true;

      //This controls how many cycles of loop() we make before re-checking the situation
      //about every 30 seconds
      PP.bypassCountDown = 50;
      PP.bypassHasJustFinished = 0;

      //Start PWM
      HAL::StartTimer1();
      PP.PWMSetPoint = 0;
    }
  }

  if (PP.bypassCountDown > 0)
  {
    if (internal_temperature < (myConfig.BypassTemperature - 6*temp_delta))
    {
      //Full power if we are nowhere near the setpoint (more than 6 degrees C away)
      PP.PWMSetPoint = 0xFF;
    }
    else
    {
      //Compare the real temperature against max setpoint, we want the PID to keep at this temperature
      PP.PWMSetPoint = myPID.step(myConfig.BypassTemperature, internal_temperature);

      if (myPID.err())
      {
        //Clear the error and stop balancing
        myPID.clear();
        StopBalance();
      }
    }

    PP.bypassCountDown--;

    if (PP.bypassCountDown == 0)
    {
      //Switch everything off for this cycle
      StopBalance();
      //On the next iteration of loop, don't sleep so we are forced to take another
      //cell voltage reading without the bypass being enabled, and we can then
      //evaluate if we need to stay in bypass mode, we do this a few times
      //as the cell has a tendancy to float back up in voltage once load resistor is removed
      PP.bypassHasJustFinished = 200;
    }
  }

  if (PP.bypassHasJustFinished > 0)
  {
    PP.bypassHasJustFinished--;
  }
}
