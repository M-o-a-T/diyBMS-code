#include "Steinhart.h"

#include <math.h>

#define NOMINAL_TEMPERATURE 25

int16_t Steinhart::ThermistorToCelsius(uint16_t BCOEFFICIENT, uint16_t RawADC)
{
//The thermistor is connected in series with another 47k resistor
//and across the 2.048V reference giving 50:50 weighting

//We can calculate the  Steinhart-Hart Thermistor Equation based on the B Coefficient of the thermistor
// at 25 degrees C rating

//If we get zero its likely the ADC is connected to ground
  if (!RawADC)
    return -999;

  // source: https://arduinodiy.wordpress.com/2015/11/10/measuring-temperature-with-ntc-the-steinhart-hart-formula/

  // ADC is 10 bits wide
  float res = (float)((1<<10)-1) / (float)RawADC - 1.0;

  res = log(res); // ln(R/Ro)
  res /= BCOEFFICIENT; // 1/B * ln(R/Ro)
  res += 1.0 / (NOMINAL_TEMPERATURE + 273.15); // + (1/To)
  res = 1.0 / res; // Invert
  res -= 273.15; // convert to oC

  return (int16_t)(res+0.5);

  //Temp = log(Temp);
  //Temperature in Kelvin = 1 / {A + B[ln(R)] + C[ln(R)]^3}
  //Temp = 1.0 / (A + (B*Temp) + (C * Temp * Temp * Temp ));
}

uint16_t Steinhart::CelsiusToThermistor(uint16_t BCOEFFICIENT, int16_t degC)
{
  // this basically inverts the above algorithm
  //
  if(degC < -100)
    return 0;

  float res = (float)degC + 273.15; // convert to K
  res = 1.0 / res; // Invert
  res -= 1.0 / (NOMINAL_TEMPERATURE + 273.15); // + (1/To)
  res *= BCOEFFICIENT; // 1/B * ln(R/Ro)
  res = exp(res); // ln(R/Ro)
  res = (float)((1<<10)-1) / (res+1.0);


  return (int16_t)res;

  //Temp = log(Temp);
  //Temperature in Kelvin = 1 / {A + B[ln(R)] + C[ln(R)]^3}
  //Temp = 1.0 / (A + (B*Temp) + (C * Temp * Temp * Temp ));
}
