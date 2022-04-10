# Message format

The BMS modules are daisy-chained, i.e. the controller sends serial data to
the first module, which forwards messages to the second, and so on, until the
result returns to the controller.

Modules are free to modify messages. Typically this is done by appending
data (when reading), or dropping some (when writing). Otherwise the modules
immediately forward any data they read. This scheme requires a four-byte
send buffer for the header, and a two-byte receive buffer for the CRC.

Message format:
* optional start byte, as per SerialPacker
* one length byte
* start address the packet is intended for
* flags (4 bits)
* command (4 bits)
* hop count = cell#
* cell count (5 bits)
* sequence# (3 bits)

* message data, depending on the type
* two bytes of CRC-16, polynome 0x1021

The hop count is initially zero and inremented when the message is
forwarded. A cell is addressed when the hop count is >= the
start address and <= (start address plus cell count). Thus a message
may address 1 to 32 cells.

Bit 0 of the flags nibble indicates that the message has been processed by
at least one module. Bit 1 states that the message applies to all modules,
i.e. modules won't consume data from the packet. The other two bits are reserved.

The master increments the sequence number for every message. Modules
increment a counter when the incoming sequence number is not what they
expect. By reading these counters later, the master can determine where
messages have been lost. (Whether messages have been lost at all is obvious
from the fact that one or more have not returned to the master.)

## Commands

For "write" messages, the information is dropped from the start of a
message. This way the next module also sees its data at the message's
start.

The data for "read" messages are appended to the message's end.

All of this is facilitated by the
[SerialPacker](https://github.com/M-o-a-T/SerialPacker) library.

### ResetPacketCounters (0)

Resets the valid and missed packet counters.

No data.

### ReadVoltageAndStatus (1)

Reads the voltage.

Data: send four bytes:
* current voltage (sum over the last SAMPLEAVERAGING ADC readings)
  bit 15: bypass is on
  bit 14: bypass teperature exceeded
* dynamic bypass threshold

### Identify (2)

The LED lights up for the next 10 messages.

No data.

### ReadTemperature (3)

Reads the temperature (raw ADC values).

Data: send three bytes: The 12-bit values (10 bit actually, the top two
bits are zero) of the internal and external sensor's ADC are packed into
three bytes, big-endian style.

### ReadBadPacketCounter (4)

Reads the bad packet counter.

Data: send two bytes.

### ReadSettings (5)

Reads settings and parameters.

Data: send ReadConfigData struct (16 bytes):

* board version (2 bytes, 440)
* bypass temperature (2 bytes, ADC voltage)
* bypass voltage (2 bytes, ADC voltage times numSamples)
* numSamples (1 byte)
* loadResistance (1 byte, 1/16th Ohm)
* voltage calibration (float, 4 bytes)
* source's git hash (first 4 bytes)

Bypass temperature, bypass voltage, and voltage calibration are from EPROM.

### WriteSettings (6)

Writes settings to EPROM.

Data: read 8 bytes:

* voltage calibration (4 bytes, ignored if zero)
* bypass temperature setpoint (ADC value)
* bypass threshold (ADC voltage, times SAMPLEAVERAGING)

### ReadBalancePowerPWM (7)

Data: read 1 byte: current PWM value of the bypass transistor.

### Timing (8)

Check how fast the system can process a message.

No data.

### ReadBalanceCurrentCounter (9)

The raw balancing current counter. The counter is incremented by the
raw current voltage (multiplied by 0…1000 depending on the PWM state),
every second. Thus, to prevent overflows, the controller should read this
value every quarter hour or so.

Data: read four bytes.

### ReadPacketReceivedCounter (10)

Read the packet counter and this module's idea of the number of unhandled
packets.

Data: four bytes.

### ResetBalanceCurrentCounter (11)

Zeroes the balance counter.

No data.

### WriteBalanceLevel (12)

Set the balance level (ADC voltage, times SAMPLEAVERAGING). Balancing is on
if the battery voltage exceeds this level. This value is intended for
balancing while charging and is not saved to storage.

Data: two bytes.

