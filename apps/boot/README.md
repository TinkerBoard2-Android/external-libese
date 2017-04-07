# Verified Boot Storage Applet for AVB 2.0

  - Status: Draft as of April 6, 2017

## Introduction

The application and support libraries in this directory provide
a mechanism for a device's bootloader, using [AVB](https://android.googlesource.com/platform/external/avb/),
to store sensitive information.  For a bootloader, sensitive information
includes whether the device is unlocked or locked, whether it is unlockable,
and what the minimum version of the OS/kernel is allowed to be booted.  It
may also store other sensitive flags.

The verified boot storage applet provides a mechanism to store this
data in a way that enforceѕ the expected policies even if the higher level
operating system is compromised or operates in an unexpected fashion.


## Design Overview

The Verified Boot Storage Applet, VBSA, provides three purpose-built
interfaces:

  - Lock storage and policy enforcement
  - Rollback index storage
  - Applet state

Each will be detailed below.

### Locks

There are four supported lock types:

  - LOCK\_CARRIER
  - LOCK\_DEVICE
  - LOCK\_BOOT
  - LOCK\_OWNER

Each lock has a single byte of "lock" storage.  If that byte is 0x0, then it is
unlocked, or cleared.  If it is non-zero, then the lock is locked.  Any
non-zero value is valid and may be used by the bootloader if any additional
internal flagging is necessary.

In addition, a lock may have associated metadata which must be supplied during
lock or unlock, or both.

See ese\_boot\_lock\_\* in include/ese/app/boot.h for the specific interfaces.


#### LOCK\_CARRIER

The Carrier Lock implements a lock which can only be set when the device is not
in production and can only be unlocked if provided a cryptographic signature.

This lock is available for use to implement "sim locking" or "phone locking"
such that the carrier can determine if the device is allowed to boot an
unsigned or unknown system image.

To provision this lock, device-specific data must be provided in an exact
format.  An example of this can be found in
'ese-boot-tool.cpp':collect\_device\_data().  This data is then provided to
the applet using ese\_boot\_lock\_xset().

##### Metadata format for locking/provisioning

The following system attributes must be collected in the given order:

  - ro.product.brand
  - ro.product.device
  - ro.build.product
  - ro.serialno
  - [Modem ID: MEID or IMEI]
  - ro.product.manufacturer
  - ro.product.model

The data is serialized as follows:

    \[length||string\]\[...\]

The length is a uint8\_t and the value is appended as a stream of
uint8\_t values.

This data is then prefixed with the desired non-zero lock value before
being submitted to the applet.  If successful, the applet will have
stored a SHA256 hash of the device data

Note, LOCK\_CARRIER can only be locked (non-zero lock value) when the
applet is in 'production' mode.

##### Clearing/unlocking

If LOCK\_CARRIER is set to a non-zero value and the applet is in
production mode, then clearing the lock value requires authorization.

Authorization comes in the form of a RSA\_SHA256-PKCS#1 signature over
the provisioned device data SHA256 hash and a supplied montonically
increasing "nonce".

The nonce value must be higher than the last seen nonce value and the
signature must validate using public key internally stored in the
applet (CarrierLock.java:PK\_MOD).

To perform a clear, ese\_boot\_lock\_xset() must be called with lock
data that begins with 0x0, to clear the lock, and then contains data of
the following format:

    unlockToken = VERSION || NONCE || SIGNATURE

    SIGNATURE = RSA_Sign(SHA256(deviceData))

  - The version is a little endian uint64\_t (8 bytes).
  - The nonce is a little endian uint64\_t (8 bytes).
  - The signature is a RSA 2048-bit with SHA-256 PKCS#1 v1.5 (256 bytes).

On unlock, the device data hash is cleared.

##### Testing

It is possible to test the key with a valid signature but a fake
internal nonce and fake internal device data using
ese\_boot\_carrier\_lock\_test().  When using this interface, it
expects the same unlock token as in the prior but prefixes with the
fake data:

    testVector = LAST_NONCE || DEVICE_DATA || unlockToken

  - The last nonce is the value the nonce is compared against (8 bytes).
  - Device data is a replacement for the internally stored SHA-256
    hash of the deviec data. (32 bytes).

#### LOCK\_DEVICE

The device lock is one of the setting used by the bootloader to
determine if the boot lock can be changed.  It may only be set by the
operating system and is meant to protect the device from being reflashed
by someone that cannot unlock or access the OS.  This may also be used
by an enterprise administrator to control lock policy for managed
devices.

As LOCK\_DEVICE has not metadata, it can be set and retrieved using
ese\_boot\_lock\_set() and ese\_boot\_lock\_get().

#### LOCK\_BOOT

The boot lock is used by the bootloader to control whether it should
only boot verified system software or not.  When the lock value
is cleared (0x0), the bootloader will boot anything.  When the lock
value is non-zero, it should only boot software that is signed by a key
stored in the bootloader except if LOCK\_OWNER is set.  Discussion of
LOCK\_OWNER will follow.

LOCK\_BOOT can only be toggled when in the bootloader/fastboot and if
both LOCK\_CARRIER and LOCK\_DEVICE are cleared/unlocked.

As with LOCK\_DEVICE, LOCK\_BOOT has no metadata so it does not need the
extended accessors.

#### LOCK\_OWNER

The owner lock is used by the bootloader to support an alternative
OS signing key provided by the device owner.  LOCK\_OWNER can only be
toggled if LOCK\_BOOT is cleared.  LOCK\_OWNER does not require
any metadata to unlock, but to lock, it requires a blob of up to 2048
bytes be provided.  This is just secure storage for use by the
bootloader.  LOCK\_OWNER may be toggled in the bootloader or the
operating system.  This allows an unlocked device (LOCK\_BOOT=0x0) to
install an owner key using fastboot or using software on the operating
system itself.

Before LOCK\_OWNER's key should be honored by the bootloader, LOCK\_BOOT
should be set (in the bootloader) to enforce use of the key and to keep
malicious software in the operating system from changing it.

(Note, that the owner key should not be treated as equivalent to the
bootloader's internally stored key in that the device user should have a
means of knowing if an owner key is in use, but the requirement for the
device to be unlocked implies both physical access the LOCK\_DEVICE
being cleared.)


### Rollback storage

Verifying an operating system kernel and image is useful both for system
reliability and for ensuring that the software has not been modified by
a malicious party.  However, if the system software is updated,
malicious software may attempt to "roll" a device back to an older
version in order to take advantage of mistakes in the older, verified
code.

Rollback indices, or versions, may be stored securely by the bootloader
to prevent these problems.  The Verified Boot Storage applet provides
eight 64-bit slots for storing a value.  They may be read at any time,
but they may only be written to when the device is in the bootloader (or
fastboot).

Rollback storage is written to using
ese\_boot\_rollback\_index\_write() and read using
ese\_boot\_rollback\_index\_read().

### Applet state

The applet supports two operational states:

  - production=true
  - production=false

On initial installation, production is false.  When the applet is not
in production mode, it does not enforce a number of security boundaries,
such as requiring bootloader or hlos mode for lock toggling or
CarrierLock verification.  This allows the factory tools to run in any
mode and properly configure a default lock state.

To transition to "production", a call to ese\_boot\_set\_production(true)
is necessary.

To check the state and collect debugging information, the call
ese\_boot\_get\_state() will return the current bootloader value,
the production state, any errors codes from lock initialization, and the
contents of lock storage.

#### Example applet provisioning

After the applet is installed, a flow as follows would prepare the
applet for use:

  - ese\_boot\_session\_init();
  - ese\_boot\_session\_open();
  - ese\_boot\_session\_lock\_xset(LOCK\_OWNER, {0, ...});
  - ese\_boot\_session\_lock\_set(LOCK\_BOOT, 0x1);
  - ese\_boot\_session\_lock\_set(LOCK\_DEVICE, 0x1);
  - [collect device data]
  - ese\_boot\_session\_lock\_set(LOCK\_CARRIER, {1, deviceData});
  - ese\_boot\_session\_set\_production(true);
  - ese\_boot\_session\_close();

### Additional details

#### Bootloader mode

Bootloader mode detection depends on hardware support to signal the
applet that the application processor has been reset.  Additionally,
there is a mechanism for the bootloader to indicate that is loading the
main OS where it flips the value.  This signal provides the assurances
around when storage is mutable or not during the time a device is
powered on.

#### Error results

EseAppResult is an enum that all ese\_boot\_\* functions return.  The
enum only covers the lower 16-bits.  The upper 16-bits are reserved for
passing applet and secure element OS status for debugging and analysis.
When the lower 16-bits are ESE\_APP\_RESULT\_ERROR\_APPLET, then the
upper bytes will be the applet code. That code can then be
cross-referenced in the applet by function and code.  If the lower
bytes are ESE\_APP\_RESULT\_ERROR\_OS, then the status code are the
ISO7816 code from an uncaught exception or OS-level error.

##### Cooldown

ESE\_APP\_RESULT\_ERROR\_COOLDOWN indicates that the secure element has
indicated that its attack counter is high. In order to decrement it, the secure
element needs to remain powered on for a certain number of minutes.  For
chips that support it, like the one this applet is being tested on, the cooldown
time can be requested with a special payload to ese\_transceive():

    FFFF00E1

In response, a 6 byte response will contain a uint32\_t and a successfuly
status code (90 00).  The integer indicates how many minutes the chip needs to
stay powered and unused to cooldown.  If this happens before the locks or
rollback storage can be read, the bootloader will need to determine a
safe delay or recovery path until boot can proceed securely.

