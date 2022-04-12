#include <SerialPacker.h>
#include <cppQueue.h>

#include "common.h"

#include "PacketRequestGenerator.h"
#include "PacketReceiveProcessor.h"

#include "DIYBMSServer.h"

SerialPacker myPacketSerial;

// TODO: Move to RTOS queues instead
cppQueue requestQueue(sizeof(PacketMeta *), 30, FIFO);
cppQueue replyQueue(sizeof(PacketMeta *), 4, FIFO);

PacketRequestGenerator transmitProc = PacketRequestGenerator(&requestQueue);
PacketReceiveProcessor receiveProc = PacketReceiveProcessor();

// Memory to hold one serial buffer
uint8_t SerialPacketReceiveBuffer[sizeof(struct PacketHeader)+sizeof(union PacketResponseAny)* maximum_cell_modules_per_packet];

uint16_t sequence = 0;

uint16_t transmitOnePacket()
{
    PacketMeta *meta;
    requestQueue.pop(&meta);
    PacketHeader *header = (PacketHeader *)(meta+1);
    
    sequence++;
    header->sequence = sequence;
    
    uint32_t t = millis();
    uint16_t len = sizeof(PacketHeader)+meta->dataLen;
    if (header->command == COMMAND::Timing)
      len += sizeof(t);
    
    // delay so modules can add data without getting overrun by the next
    // packet.
	// Serial overhead: start(1) + len (1/2) plus CRC (2), plus some time
	// for slack, so let's use 8.
	// A byte takes 10 bits if no parity and one stop bit, but maybe a
	// module interrupt delays sending and/or somebody decides to turn on
	// parity.
	uint16_t delay_ms = ((8+len + (uint32_t)meta->dataExpect * (header->cells+1)) * 12)
		* (uint32_t)1000 / mysettings.baudRate;

    myPacketSerial.sendStartFrame(len);
    myPacketSerial.sendBuffer(header, sizeof(PacketHeader)+meta->dataLen);
    
    // Add the timestamp
    if (header->command == COMMAND::Timing)   
      myPacketSerial.sendBuffer(&t,sizeof(t));
    
    myPacketSerial.sendEndFrame();
	freePacket(meta);
	return delay_ms;
}

bool receiveOnePacket()
{
	PacketMeta *ps;
	replyQueue.pop(&ps);

#if defined(PACKET_LOGGING_RECEIVE)
// Process decoded incoming packet
// dumpPacketToDebug('R', &ps);
#endif

	return receiveProc.ProcessReply(ps);
}

static void onPacketHeader()
{
	// a CRC error is a packet where onPacketReceived is not called
	receiveProc.pendingCRCErrors += 1;
}
  
static void onPacketReceived()
{
	// TODO flash green LED
	// let's just count hacked-up packets as error
	receiveProc.totalCRCErrors += receiveProc.pendingCRCErrors-1;
	receiveProc.pendingCRCErrors = 0;

	if(myPacketSerial.receiveCount() < sizeof(struct PacketHeader)) {
		receiveProc.totalNotProcessedErrors += 1;
		return;
	}

	PacketMeta *ps = (PacketMeta *)calloc(1,sizeof(PacketMeta)+myPacketSerial.receiveCount());
	ps->dataLen = myPacketSerial.receiveCount();
	ps->timestamp = millis();
	memcpy(ps+1, SerialPacketReceiveBuffer, ps->dataLen);

	if (!replyQueue.push(&ps))
	{
		// ESP_LOGE(TAG, "Reply Q full");
		receiveProc.totalNotProcessedErrors += 1; // TODO?
		freePacket(ps);
	}
	// TODO turn green LED off
}


void initSerializer()
{
    myPacketSerial.begin(&SERIAL_DATA, &onPacketHeader, nullptr, &onPacketReceived,
		SerialPacketReceiveBuffer, sizeof(SerialPacketReceiveBuffer));

}
