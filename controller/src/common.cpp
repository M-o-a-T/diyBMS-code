#include "common.h"
#include <malloc.h>

PacketMeta *allocatePacket(uint16_t size)
{
  PacketMeta *meta = (PacketMeta *)calloc(1, sizeof(struct PacketMeta)+sizeof(struct PacketHeader)+size);
  meta->dataLen = size;
  return meta;
}

void freePacket(PacketMeta *packet)
{ 
  free(packet);
}

