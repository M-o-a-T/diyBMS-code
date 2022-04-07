#ifndef CHECKSUM16_H // include guard
#define CHECKSUM16_H

#include <Arduino.h>
#include <CRC16.h>

/*
Calculates XMODEM CRC16 against an array of bytes
*/

static inline uint16_t CRC16_Buffer(uint8_t data[], uint16_t length)
{
  CRC16 crc;

  crc.setPolynome(0x1021);        
  crc.add(data, length);
  return crc.getCRC();
}                       

#endif
