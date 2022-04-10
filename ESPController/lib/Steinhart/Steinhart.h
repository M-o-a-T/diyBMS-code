#ifndef Steinhart_H // include guard
#define Steinhart_H

#include <Arduino.h>

class Steinhart {
   public:
      static int16_t ThermistorToCelsius(uint16_t BCOEFFICIENT, uint16_t RawADC);
      static uint16_t CelsiusToThermistor(uint16_t BCOEFFICIENT, int16_t TempInCelcius);
};
#endif
