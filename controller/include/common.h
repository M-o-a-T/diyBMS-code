#ifndef COMMON_H
#define COMMON_H

#ifndef MAX_PACKET_SIZE
#define MAX_PACKET_SIZE SP_MAX_PACKET
#endif

#include "diybms_common.h"

struct PacketMeta {
  uint16_t dataLen;
  uint16_t dataExpect;
  uint32_t timestamp;

  // PacketHeader follows
};

PacketMeta *allocatePacket(uint16_t size);
void freePacket(PacketMeta *packet);

#endif
