# diyBMS v4, M-o-a-T fork

This is a fork of Stuart Pittaway's diyBMS code. Controllers and modules
*must* both use this and *can not* talk to devices using the original
firmware.

This fork's main author is Matthias Urlichs <matthias@urlichs.de>.

The hardware is unmodified.

## Changes

### Packet format

We now use variable-length *streamed* data packets. The idea is that a
message may contain data for multiple consecutive modules. A module will
receive the header, eat N bytes of payload, send the header and the rest of
the payload along, add M bytes of data, check the CRC, process the message,
and send along the CRC.

For error recovery, we now use an inter-packet timeout instead of escaping
the sync character.

The sequence number has been reduced to three bits. Rationale: If you lose
more than seven packets in a row, you have worse problems than counting
exactly how many you've lost.

The end packet number has been replaced with a 5-bit cell count. This
removes the ability to broadcast messages. See below for how to fix that.

### No more floating point

Floating point operations in the module is slow and eats valuable flash
storage space, so it has been removed. All math is now done on the
controller.

### Standard CRC

We now use a "standard" CRC library.

## Future Changes

### CRC polynomial

The main reason for using a polynomial like 0x1021 was that it doesn't have
many bits set. This saves space when you implement it in hardware.

The problem is that 0x1021 doesn't find many bit errors.

This can be improved. The polynomial 0xBAAD has much better error detection
characteristics (4 errors for messages up to 256 bytes).
You can read a paper with more details
[here](http://users.ece.cmu.edu/~koopman/roses/dsn04/koopman04_crc_poly_embedded.pdf).

Also, we might want to use a 4-bit lookup table instead of looping through each bit.

### Fix queues

There's an open TODO in the controller to use RTOS queues instead of
cppQueue.

### Protocol versioning

The protocol needs its own version number. Just ask the module(s) what they
understand.

### Support alerts

Teach modules that they can send messages on their own.

This is usful e.g. for high/low voltage, or to alert the master about a
location of a broken link (we'd need to trigger that message earlier when
the hop count is lower, to prevent flooding).

A flag to block incrementing the hop count should be sufficient; alerts
should have separate packet types.

### Free more header bits

If we restrict module blocks to power-of-two, we can replace the count
field with a single bit. If that bit is set, the top bit(s) of the starting
module number can be used to mark how large the block is *and* how far the
module number shall be shifted to the left.

Let's use a 3-bit block number (i.e. up to eight modules) as an example:

* 0 000 - module 0
* 0 001 - module 1
* 0 111 - module 7
* 1 000 - module 0 and 1
* 1 001 - module 2 and 3
* 1 011 - module 6 and 7
* 1 100 - module 0,1,2,3
* 1 101 - module 4,5,6,7
* 1 110 - module 0 through 7: broadcast
* 1 111 - reserved / no module

This scheme removes the capability to address an arbitrary range of
modules, but (a) the current code doesn't need that feature and (b) blocks
tend to be at (or slightly below) a power-of-2-size anyway. We might want
to tell the last module within a block to increment the hop count by more
than 1.

----

The following text is from the original README, unmodified.

# diyBMS v4

Version 4 of the diyBMS.  Do-it-yourself battery management system for Lithium ion battery packs and cells

If you are looking for version 3 of this project take a look here https://github.com/stuartpittaway/diyBMS

THIS CODE IS FOR THE NEW CONTROLLER (AFTER FEB 2021) AND ESP32 DEVKIT-C

# Support the project

If you find the BMS useful, please consider buying me a beer, check out [Patreon](https://www.patreon.com/StuartP) for more information.

You can also send beer tokens via Paypal - [https://paypal.me/stuart2222](https://paypal.me/stuart2222)

Any donations go towards the on going development and prototype costs of the project.

# Videos on how to use and build

https://www.youtube.com/stuartpittaway

### Video on how to program the devices
https://youtu.be/wTqDMg_Ql98

### Video on how to order from JLCPCB
https://youtu.be/E1OS0ZOmOT8


# How to use the code

Instructions for programming/flashing the hardware can be [found here](ProgrammingHardware.md)

# Help

If you need help, ask over at the [forum](https://community.openenergymonitor.org/t/diybms-v4)

If you discover a bug or want to make a feature suggestion, open a Github issue

# Hardware

Hardware for this code is in a seperate repository, and consists of a controller (you need 1 of these) and modules (one per series cell in your battery)

https://github.com/stuartpittaway/diyBMSv4



# WARNING

This is a DIY product/solution so don’t use this for safety critical systems or in any situation where there could be a risk to life.  

There is no warranty, it may not work as expected or at all.

The use of this project is done so entirely at your own risk.  It may involve electrical voltages which could kill - if in doubt, seek help.

The use of this project may not be compliant with local laws or regulations - if in doubt, seek help.


# How to compile the code yourself

The code uses [PlatformIO](https://platformio.org/) to build the code.  There isn't any need to compile the code if you simply want to use it, see "How to use the code" above.

If you want to make changes, fix bugs or poke around, use platformio editor to open the workspace named "diybms_workspace.code-workspace"


# License

This work is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 2.0 UK: England & Wales License.

https://creativecommons.org/licenses/by-nc-sa/2.0/uk/

You are free to:
* Share — copy and redistribute the material in any medium or format
* Adapt — remix, transform, and build upon the material
The licensor cannot revoke these freedoms as long as you follow the license terms.

Under the following terms:
* Attribution — You must give appropriate credit, provide a link to the license, and indicate if changes were made. You may do so in any reasonable manner, but not in any way that suggests the licensor endorses you or your use.
* Non-Commercial — You may not use the material for commercial purposes.
* ShareAlike — If you remix, transform, or build upon the material, you must distribute your contributions under the same license as the original.
* No additional restrictions — You may not apply legal terms or technological measures that legally restrict others from doing anything the license permits.

Notices:
You do not have to comply with the license for elements of the material in the public domain or where your use is permitted by an applicable exception or limitation.

No warranties are given. The license may not give you all of the permissions necessary for your intended use. For example, other rights such as publicity, privacy, or moral rights may limit how you use the material.

