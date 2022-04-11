#ifndef DIYBMS_COMMON_H
#define DIYBMS_COMMON_H

#include <inttypes.h>

// Only bits can be used!
enum COMMAND: uint8_t
{
    ResetPacketCounters = 0,
    ReadVoltageAndStatus=1,
    Identify=2,
    ReadTemperature=3,
    ReadPacketCounters=4,
    ReadSettings=5,
    WriteSettings=6,
    ReadBalancePowerPWM=7,
    Timing=8,
    ReadBalanceCurrentCounter=9,
    ResetBalanceCurrentCounter=10,
    WriteBalanceLevel=11,
};

struct PacketHeader
{
  uint8_t start;
  unsigned int _reserved:2;
  unsigned int global:1;
  unsigned int seen:1;
  unsigned int command:4;
  uint8_t hops;
  unsigned int cells:5;
  unsigned int sequence:3;
} __attribute__((packed));

union FLOAT_UINT {
  float f;
  uint32_t u;
};

struct PacketRequestConfig {
  FLOAT_UINT voltageCalibration;
  uint16_t bypassTemp;
  uint16_t bypassThresh;
} __attribute__((packed));

struct PacketReplyVoltages {
  uint16_t voltRaw;
  uint16_t bypassRaw;
} __attribute__((packed));

struct PacketReplyTemperature {
  uint16_t intRaw:12;
  uint16_t extRaw:12;
} __attribute__((packed));

struct PacketReplyCounters {
  uint16_t received;
  uint16_t bad;
} __attribute__((packed));

struct PacketReplySettings {
  uint16_t boardVersion;
  uint16_t bypassTempRaw;
  uint16_t bypassVoltRaw;
  uint8_t numSamples;
  uint8_t loadResRaw;
  FLOAT_UINT voltageCalibration; 
  uint32_t gitVersion;  
} __attribute__((packed));

struct PacketRequestBalance {
  uint16_t voltageRaw;
} __attribute__((packed));


// collect all request data. Used to auto-size the buffer.
union PacketRequestAny {
  struct PacketRequestConfig config;
  struct PacketRequestBalance setBalance;
};

// collect all possible response data. Used to auto-size the buffer.
union PacketResponseAny {
  struct PacketReplySettings config;
  struct PacketReplyVoltages voltages;
  struct PacketReplyTemperature temperatures;
  struct PacketReplyCounters counters;
};

//
#ifndef MAX_PACKET_SIZE
#error You must declare MAX_PACKET_SIZE. PacketHeader+ReadConfigData for the client, a whole lot for the server.
#endif

struct PacketStruct
{
  struct PacketHeader h;
  uint16_t data[MAX_PACKET_SIZE/2+1];
} __attribute__((packed));



#endif
