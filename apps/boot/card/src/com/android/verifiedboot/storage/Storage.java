//
// Copyright (C) 2017 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Test on device with:
//    echo -e '00A4040010A0000004765049584C424F4F54000101 9000\n8000000000 9000\n' | ese-replay nq-nci
// Install (after loading the two Shareable caps) with:
// ${JAVA_HOME}/bin/java -jar gp.jar   -d --install avb_storage.cap
//

package com.android.verifiedboot.storage;

import javacard.framework.AID;
import javacard.framework.APDU;
import javacard.framework.Applet;
import javacard.framework.CardRuntimeException;
import javacard.framework.ISO7816;
import javacard.framework.ISOException;
import javacard.framework.JCSystem;
import javacard.framework.Shareable;
import javacard.framework.Util;

import javacard.security.KeyBuilder;
import javacard.security.MessageDigest;
import javacard.security.RSAPublicKey;
import javacard.security.Signature;

// Enables processing longer messages atomically.
import javacardx.apdu.ExtendedLength;

import com.android.verifiedboot.globalstate.callback.CallbackInterface;
import com.android.verifiedboot.globalstate.owner.OwnerInterface;

import com.android.verifiedboot.storage.GlobalStateImpl;
import com.android.verifiedboot.storage.BasicLock;
import com.android.verifiedboot.storage.CarrierLock;
import com.android.verifiedboot.storage.LockInterface;
import com.android.verifiedboot.storage.OsBackupInterface;
import com.android.verifiedboot.storage.VersionStorage;

import com.android.verifiedboot.storage.JcopBackupImpl;

public class Storage extends Applet implements ExtendedLength, Shareable {
    final static byte VERSION = (byte) 0x01;
    final static byte APPLET_CLA = (byte)0x80;

    /* Note, globalState never needs to be backed up as any clients should re-register on every
     * call -- not just on install().
     */
    private GlobalStateImpl globalState;
    private VersionStorage versionStorage;
    private OsBackupInterface osBackupImpl;
    /* lockStorage can be backed up directly, as long as the locks themselves have been
     * properly intiialized.
     */
    private byte[] lockStorage;
    private LockInterface[] locks;
    // Indices into locks[].
    private final static byte LOCK_CARRIER = (byte) 0x00;
    private final static byte LOCK_DEVICE = (byte) 0x01;
    private final static byte LOCK_BOOT = (byte) 0x02;
    private final static byte LOCK_OWNER = (byte) 0x03;

    private final static byte INS_GET_STATE = (byte) 0x00;
    private final static byte INS_LOAD = (byte) 0x02;
    private final static byte INS_STORE = (byte) 0x04;
    private final static byte INS_GET_LOCK = (byte) 0x06;
    private final static byte INS_SET_LOCK = (byte) 0x08;
    private final static byte INS_SET_PRODUCTION = (byte) 0x0a;
    private final static byte INS_CARRIER_LOCK_TEST = (byte) 0x0c;

    private final static short NO_METADATA = (short) 0;
    private final static short NO_REQ_LOCKS = (short) 0;
    // Plenty of space for an owner key and any serialization.
    private final static short OWNER_LOCK_METADATA_SIZE = (short) 2048;

    /**
     * Installs this applet.
     *
     * @param params the installation parameters
     * @param offset the starting offset of the parameters
     * @param length the length of the parameters
     */
    public static void install(byte[] bArray, short bOffset, byte bLength) {
        // GP-compliant-ish JavaCard applet registration
        short aidOffset = bOffset;
        short aidLength = bArray[aidOffset++];
        short privsOffset = (short)(aidOffset + aidLength);
        short privLength = bArray[privsOffset++];
        // Grab the install parameters.
        short paramsOffset = (short)(privsOffset + privLength);
        short paramLength = bArray[paramsOffset++];

        Storage applet = new Storage();
        applet.register(bArray, aidOffset, (byte)aidLength);
        if (paramLength == 0) {
            // TODO(wad) Should we fail the install on failure?
            applet.restore(bArray);
        }
    }

    private Storage() {
        globalState = new GlobalStateImpl();
        osBackupImpl = new JcopBackupImpl();

        versionStorage = new VersionStorage(globalState);
        osBackupImpl.track(OsBackupInterface.TAG_VERSION_STORAGE,
                           versionStorage);

        lockStorage = new byte[4096];
        // Initialize all supported locks here.
        locks = new LockInterface[4];
        // LOCK_CARRIER can be set only when not in production mode
        // but can be unlocked any time authenticated data is provided.
        locks[LOCK_CARRIER] = new CarrierLock();
        osBackupImpl.track(OsBackupInterface.TAG_LOCK_CARRIER,
                           locks[LOCK_CARRIER]);

        // LOCK_DEVICE can be toggled any time from anywhere.  It
        // expresses a HLOS management policy.
        locks[LOCK_DEVICE] = new BasicLock(NO_METADATA, NO_REQ_LOCKS);
        BasicLock lockRef = (BasicLock)locks[LOCK_DEVICE];
        lockRef.requireHLOS(true);
        osBackupImpl.track(OsBackupInterface.TAG_LOCK_DEVICE,
                           lockRef);

         // LOCK_BOOT can only be toggled if both the carrier and device
         // locks are clear and then, only in the bootloader/fastboot.
        locks[LOCK_BOOT] = new BasicLock(NO_METADATA, (short) 2);
        lockRef = (BasicLock)locks[LOCK_BOOT];
        lockRef.addRequiredLock(locks[LOCK_CARRIER]);
        lockRef.addRequiredLock(locks[LOCK_DEVICE]);
        lockRef.requireBootloader(true);
        osBackupImpl.track(OsBackupInterface.TAG_LOCK_BOOT,
                           lockRef);

        // LOCK_OWNER can be toggled at any time if the BOOT_LOCK is unlocked.
        // It modifies the behavior of LOCK_BOOT by providing an alternative
        // "owner" boot key.
        locks[LOCK_OWNER] = new BasicLock(OWNER_LOCK_METADATA_SIZE, (short) 1);
        lockRef = (BasicLock)locks[LOCK_OWNER];
        lockRef.addRequiredLock(locks[LOCK_BOOT]);
        lockRef.requireMetadata(true);
        osBackupImpl.track(OsBackupInterface.TAG_LOCK_OWNER,
                           lockRef);

        short offset = (short) 0;
        byte lockNum = (byte) 0;
        for ( ; lockNum < (byte) locks.length; ++lockNum) {
            if (locks[lockNum].getStorageNeeded() >
                (short)(lockStorage.length - offset)) {
                ISOException.throwIt((short) 0xfeed);
            }
            locks[lockNum].initialize(globalState, lockStorage, offset);
            offset += locks[lockNum].getStorageNeeded();
        }

    }

    /**
      * Returns the globalState or OsBackupInterface object.
      *
      * @param clientAid AID of the caller.
      * @param arg Object request indicator
      */
    @Override
    public Shareable getShareableInterfaceObject(AID clientAid, byte arg) {
        if (clientAid != null) {  // TODO: check AID.
            switch (arg) {
                case (byte) 0:
                  return globalState;
                default:
                  return osBackupImpl.getShareableInterfaceObject(clientAid, arg);
            }
        }
        return globalState;
    }

    /**
     * Emits the state of this Applet to the wire.
     *
     * Format:
     * VERSION (byte)
     * Length (short)
     * [global_state.OwnerInterface data]
     *   inBootloader (byte)
     *   production (byte)
     * [lock state]
     *   numLocks (byte)
     *   locks[0].initialized (short)
     *   ...
     *   locks[numLocks - 1].initialized (short)
     * [lockStorage]
     *   lockStorageLength (short)
     *   lockStorage (lockStorageLength=4096)
     *
     * TODO(wad) It'd be nice to TLV these values...
     *
     * @return 0x00 if the response has been sent.
     */
    private short sendStorageState(APDU apdu) {
        final byte buffer[] = apdu.getBuffer();
        byte[] working = new byte[2];
        short value = 0;
        short resp = 0;
        byte i;
        short expectedLength = apdu.setOutgoing();
        short length = (short)(2 + 1 + 2 + 1 + 1 + 1 + (2 * locks.length) +
                               2 + lockStorage.length + 2);
        if (expectedLength < length) {
            // Error with length.
            buffer[0] = (byte) 0x01;
            buffer[1] = (byte) 0x00;
            buffer[2] = (byte)(length >> 8);
            buffer[3] = (byte)(length & 0xff);
            apdu.setOutgoingLength((short) 4);
            apdu.sendBytes((short) 0, (short) 4);
            return 0x0;
        }
        try {
            apdu.setOutgoingLength(length);
        } catch (CardRuntimeException e) {
            return 0x0101;
        }

        // Send the usual prefix status indicating we made it this far.
        try {
            Util.setShort(working, (short) 0, (short) 0x0);
            apdu.sendBytesLong(working, (short) 0, (short) 2);
            length -= 2;
        } catch (CardRuntimeException e) {
            return 0x0001;
        }

        try {
            working[0] = VERSION;
            apdu.sendBytesLong(working, (short) 0, (short) 1);
            length--;
        } catch (CardRuntimeException e) {
            return 0x0001;
        }

        try {
            Util.setShort(working, (short) 0, length);
            apdu.sendBytesLong(working, (short) 0, (short) 2);
            length -= 2;
        } catch (CardRuntimeException e) {
            ISOException.throwIt(length);
        }

        try {
            working[0] = (byte)0;
            if (globalState.inBootloader() == true) {
                working[0] = (byte)1;
            }
            apdu.sendBytesLong(working, (short) 0, (short) 1);
            length--;
        } catch (CardRuntimeException e) {
            ISOException.throwIt(length);
        }

        try {
            working[0] = (byte)0;
            if (globalState.production() == true) {
                working[0] = (byte)1;
            }
            apdu.sendBytesLong(working, (short) 0, (short) 1);
            length--;
        } catch (CardRuntimeException e) {
            ISOException.throwIt(length);
        }
        try {
            working[0] = (byte)locks.length;
            apdu.sendBytesLong(working, (short) 0, (short) 1);
            length--;
        } catch (CardRuntimeException e) {
            ISOException.throwIt(length);
        }

        try {
            for (i = 0; i < (byte)locks.length; ++i) {
                Util.setShort(working, (short) 0, locks[i].initialized());
                apdu.sendBytesLong(working, (short) 0, (short) 2);
                length -= 2;
            }
        } catch (CardRuntimeException e) {
            ISOException.throwIt(length);
        }

        try {
            Util.setShort(working, (short) 0, (short)lockStorage.length);
            apdu.sendBytesLong(working, (short) 0, (short) 2);
            length -= 2;
        } catch (CardRuntimeException e) {
            ISOException.throwIt(length);
        }

        try {
          apdu.sendBytesLong(lockStorage, (short) 0, (short) lockStorage.length);
          length -= (short) lockStorage.length;
        } catch (CardRuntimeException e) {
            ISOException.throwIt(length);
        }
        if (length != 0) {
            ISOException.throwIt(length);
        }
        return 0;
    }

    private void sendResponseCode(APDU apdu, short resp) {
        final byte buffer[] = apdu.getBuffer();
        buffer[0] = (byte)(resp >> 8);
        buffer[1] = (byte)(resp & 0xff);
        apdu.setOutgoingAndSend((short)0, (short)2);
    }

    /**
     * Returns 0x0 if no data needs to be sent.
     */
    private short sendLockData(APDU apdu, byte p1, byte p2) {
        final byte buffer[] = apdu.getBuffer();
        short resp = 0;
        if (p1 >= (byte)locks.length) {
            return 0x0001;
        }
        if (locks[p1].initialized() != 0) {
          return locks[p1].initialized();
        }

        if (p2 == (byte) 0x00) {
            resp = locks[p1].get(buffer, (short) 2);
            Util.setShort(buffer, (short) 0, resp);
            apdu.setOutgoingAndSend((short) 0, (short) 3);
            return 0;
        }
        short length = (short)(3 + locks[p1].metadataLength());
        try {
            byte[] resp_val = new byte[1];
            apdu.setOutgoing();
            apdu.setOutgoingLength(length);

            // Send a successful response code.
            resp_val[0] = (byte) 0x00;
            apdu.sendBytesLong(resp_val, (short) 0, (short) 1);
            apdu.sendBytesLong(resp_val, (short) 0, (short) 1);
            // Then the lock byte.
            apdu.sendBytesLong(lockStorage, locks[p1].lockOffset(), (short) 1);
            // Then any exported metadata.  Note that the metadataOffset may
            // exclude some data which is considered private to the lock.
            apdu.sendBytesLong(lockStorage, locks[p1].metadataOffset(),
                               locks[p1].metadataLength());
            return 0;
        } catch (CardRuntimeException e) {
            return 0x0002;
        }
    }

    /**
     * Handles incoming APDU requests
     *
     * @param apdu payload from the client.
     */
    public void process(APDU apdu) {
        final byte buffer[] = apdu.getBuffer();
        final byte cla = buffer[ISO7816.OFFSET_CLA];
        final byte ins = buffer[ISO7816.OFFSET_INS];
        // Handle standard commands
        if (apdu.isISOInterindustryCLA()) {
            switch (ins) {
            case ISO7816.INS_SELECT:
                // Do nothing, successfully
                return;
            default:
                ISOException.throwIt(ISO7816.SW_INS_NOT_SUPPORTED);
            }
            ISOException.throwIt(ISO7816.SW_CLA_NOT_SUPPORTED);
        }

        short bytesRead = apdu.setIncomingAndReceive();
        short numBytes = apdu.getIncomingLength();
        short cdataOffset = apdu.getOffsetCdata();

        byte p1 = (byte)(buffer[ISO7816.OFFSET_P1] & (byte)0xff);
        byte p2 = (byte)(buffer[ISO7816.OFFSET_P2] & (byte)0xff);
        short length = 0;
        short expectedLength = 0;
        short resp = 0;
        boolean enable = false;
        if (p1 != 0) {
          enable = true;
        }

        switch (buffer[ISO7816.OFFSET_INS]) {
        case INS_GET_STATE:  /* getStorageState(0x0, 0x0) */
            resp = sendStorageState(apdu);
            if (resp != 0) {
                sendResponseCode(apdu, resp);
            }
            return;
        case INS_LOAD: /* getSlot(id) */
            resp = versionStorage.getSlot(p1, buffer, (short) 2);
            buffer[0] = (byte)(resp >> 8);
            buffer[1] = (byte)(resp & 0xff);
            length = 2;
            if (resp == 0) {
                length += (short) VersionStorage.SLOT_BYTES;
            }
            // Always send the two bytes of status as they are more
            // useful than the APDU error.
            apdu.setOutgoingAndSend((short)0, length);
            return;
        case INS_STORE: /* setSlot(id) {uint64_t} */
            resp = versionStorage.setSlot(p1, buffer, cdataOffset);
            Util.setShort(buffer, (short) 0, resp);
            apdu.setOutgoingAndSend((short) 0, (short) 2);
            return;
        case INS_GET_LOCK: /* getLock(lockId, sendMetadata) */
            resp = sendLockData(apdu, p1, p2);
            if (resp != 0) {
                sendResponseCode(apdu, resp);
            }
            return;
        case INS_SET_LOCK: /* setlock(index, val) { data } */
            if (p1 >= (byte)locks.length) {
                sendResponseCode(apdu, (short)0x0100);
            }

            if (bytesRead == (short) 0) {
                resp = locks[p1].set(p2);
             } else {
                // Note, there may be more bytes to read than fit in the first pass.
                // If so, we'll need to stage it in a transient buffer to pass in.
                resp = (short) 0x0101;
                if (bytesRead == numBytes) {
                    resp = locks[p1].setWithMetadata(p2, buffer,
                                                     cdataOffset,
                                                     bytesRead);
                }
            }
            sendResponseCode(apdu, resp);
            return;
        case INS_SET_PRODUCTION: /* setProduction(p1) */
            if (globalState.setProduction(enable) == true) {
                resp = 0x0000;
            } else {
                resp = 0x0001;
            }
            sendResponseCode(apdu, resp);
            return;
        /* carrierLockTest() { testVector } */
        case INS_CARRIER_LOCK_TEST:
            // Note, there may be more bytes to read than fit in the first pass.
            // If so, we'll need to stage it in a transient buffer to pass in.
            if (numBytes != bytesRead) {
                resp = 0x0100;
            }
            resp = ((CarrierLock)locks[0]).testVector(buffer, cdataOffset, bytesRead);
            sendResponseCode(apdu, resp);
            return;
        default:
            ISOException.throwIt(ISO7816.SW_INS_NOT_SUPPORTED);
        }
    }

    /**
     * Restores data across upgrades
     *
     * @param buffer bytes
     * @return true on success and false on failure.
     */
    private boolean restore(byte[] buffer) {
        return osBackupImpl.restore(buffer, (short) 0);
    }
}
