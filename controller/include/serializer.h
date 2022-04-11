#ifndef SERIALIZER_H
#define SERIALIZER_H

extern SerialPacker myPacketSerial;

extern cppQueue requestQueue;
extern cppQueue replyQueue;

extern PacketRequestGenerator transmitProc;
extern PacketReceiveProcessor receiveProc;

extern uint8_t SerialPacketReceiveBuffer[sizeof(struct PacketHeader)+sizeof(union PacketResponseAny)*maximum_cell_modules_per_packet];

extern uint16_t sequence;

// returns #bytes, including expected results
void initSerializer();
uint16_t transmitOnePacket();
void processOnePacket();

#endif // guard
