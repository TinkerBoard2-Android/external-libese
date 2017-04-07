/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Commandline tool for interfacing with the Boot Storage app.
 */

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <cutils/properties.h>
#include <ese/ese.h>
ESE_INCLUDE_HW(ESE_HW_NXP_PN80T_NQ_NCI);

#include "include/ese/app/boot.h"

void usage(const char *prog) {
  fprintf(stderr,
    "Usage:\n"
    "%s <cmd> <args>\n"
    "    state    get\n"
    "    production set {true,false}\n"
    "    rollback set <іndex> <value>\n"
    "             get <іndex>\n"
    "    lock get {carrier,device,boot,owner}\n"
    "         set carrier 0 <unlockToken>\n"
    "                     <nonzero byte> {IMEI,MEID}\n"
    "             device <byte>\n"
    "             boot <byte>\n"
    "             owner 0\n"
    "             owner <non-zero byte> <keyValue>\n"
    "    verify-key test <blob>\n"
    "    verify-key auto\n"
    "\n"
    "Note, any non-zero byte value is considered 'locked'.\n"
    "\n\n", prog);
}

#define handle_error(ese, result) ;
#if 0  // TODO
void handle_error(struct EseInterface *ese, EseAppResult result) {
  uint32_t minutes =  ese_cooldown_get(ese, ESE_COOLDOWN_ATTACK);
  set_timer_cb(minutes * 60, power_down_ese);
  ...
}
#endif

static void print_hexdump(const uint8_t* data, int start, int stop) {
  for (int i = start; i < stop; ++i) {
    if (i % 20 == start - 1) {
      printf("\n");
    }
    printf("%.2x ", data[i]);
  }
  printf("\n");
}

static uint16_t hexify(const std::string& input, std::vector<uint8_t> *output) {
  for (auto it = input.cbegin(); it != input.cend(); ++it) {
    std::string hex;
    hex.push_back(*it++);
    hex.push_back(*it);
    output->push_back(static_cast<uint8_t>(std::stoi(hex, nullptr, 16)));
  }
  return static_cast<uint16_t>(output->size() & 0xffff);
}


bool get_property_helper(const char *key, char *value) {
  if (property_get(key, value, NULL) == 0) {
    fprintf(stderr, "Property '%s' is empty!\n", key);
    return false;
  }
  return true;
}

// Serializes the data to a string which is hashed by the applet.
bool collect_device_data(const std::string &modem_id, std::string *device_data) {
  static const char *kDeviceKeys[] = {
    "ro.product.brand",
    "ro.product.device",
    "ro.build.product",
    "ro.serialno",
    "",
    "ro.product.manufacturer",
    "ro.product.model",
    NULL,
  };
  uint8_t len = 0;
  const char **key = &kDeviceKeys[0];
  do {
    if (strlen(*key) == 0) {
      len = static_cast<uint8_t>(modem_id.length());
      device_data->push_back(len);
      device_data->append(modem_id);
    } else {
      char value[PROPERTY_VALUE_MAX];
      if (!get_property_helper(*key, &value[0])) {
        return false;
      }
      len = static_cast<uint8_t>(strlen(value));
      device_data->push_back(len);
      device_data->append(value);
    }
    if (*++key == NULL) {
      break;
    }
  } while (*key != NULL);
  return true;
}

int handle_production(struct EseBootSession *session, std::vector<std::string> &args) {
  EseAppResult res;
  if (args[1] != "set") {
    fprintf(stderr, "production: unknown command '%s'\n", args[2].c_str());
    return -1;
  }
  if (args.size() < 3) {
    fprintf(stderr, "production: not enough arguments\n");
    return -1;
  }
  bool prod = false;
  if (args[2] == "true") {
    prod = true;
  } else if (args[2] == "false") {
    prod = false;
  } else {
    fprintf(stderr, "production: must be 'true' or 'false'\n");
    return -1;
  }
  res = ese_boot_set_production(session, prod);
  if (res == ESE_APP_RESULT_OK) {
    printf("production mode changed\n");
    return 0;
  }
  fprintf(stderr, "production: failed to change (%.8x)\n", res);
  return 1;
}

int handle_state(struct EseBootSession *session, std::vector<std::string> &args) {
  EseAppResult res;
  if (args[1] != "get") {
    fprintf(stderr, "state: unknown command '%s'\n", args[2].c_str());
    return -1;
  }
  // Read in the hex unlockToken and hope for the best.
  std::vector<uint8_t> data;
  data.resize(8192);
  uint16_t len = static_cast<uint16_t>(data.size());
  res = ese_boot_get_state(session, data.data(), len);
  if (res != ESE_APP_RESULT_OK) {
    fprintf(stderr, "state: failed (%.8x)\n", res);
    return 1;
  }
  // TODO: ese_boot_get_state should guarantee length is safe...
  len = (data[1] << 8) | (data[2]) + 3;
  printf("Boot Storage State:\n    ");
  print_hexdump(data.data(), 3, len);
  return 0;
}


static const uint8_t auto_data[] = {
  // lastNonce
  0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x40,
  // deviceData
  0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
  0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
  0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
  0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
  // Version
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  // Nonce
  0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
  // Signature
  0x79, 0x71, 0xd9, 0x5a, 0x2c, 0x64, 0x16, 0xdc, 0x9c, 0xff, 0xa5, 0xfe,
  0x6a, 0xd3, 0x80, 0x74, 0xa7, 0xc6, 0x1d, 0x59, 0xce, 0x90, 0x4f, 0xa1,
  0xe7, 0x24, 0x0d, 0x9f, 0x18, 0x2f, 0x21, 0xd1, 0x2b, 0xec, 0xf3, 0x5e,
  0x9e, 0xa7, 0x27, 0x11, 0xfa, 0x42, 0xa3, 0x3c, 0xbc, 0x27, 0xa6, 0xff,
  0x2a, 0x2c, 0x01, 0xae, 0x1f, 0x29, 0xf8, 0x06, 0x73, 0x70, 0x21, 0x8b,
  0xcb, 0x0b, 0xa1, 0xd4, 0xbc, 0xc4, 0xbc, 0x7a, 0x51, 0x87, 0xf4, 0x64,
  0xdc, 0x18, 0x0f, 0x44, 0xd2, 0x95, 0x78, 0xe8, 0x51, 0xc5, 0xac, 0x6a,
  0x55, 0x2b, 0x72, 0x64, 0x31, 0x56, 0x7a, 0x4f, 0x46, 0x15, 0xec, 0x1d,
  0xe4, 0xc5, 0x9b, 0x2a, 0xcf, 0x81, 0x39, 0xc9, 0x1b, 0x60, 0x89, 0x56,
  0x1f, 0x91, 0x62, 0xd1, 0xa0, 0x10, 0xba, 0x80, 0x5a, 0x30, 0x52, 0x6e,
  0x46, 0x45, 0xff, 0x0f, 0xf5, 0x4a, 0xf3, 0x0d, 0x64, 0x6e, 0x58, 0xa7,
  0xbe, 0x9e, 0xba, 0xa9, 0x78, 0xee, 0x10, 0xc7, 0xea, 0xc4, 0xc0, 0xdb,
  0x40, 0xe5, 0xe6, 0xae, 0xf1, 0x5f, 0x88, 0xd7, 0x60, 0x73, 0xbd, 0x97,
  0xfc, 0x01, 0x4a, 0xff, 0x29, 0x6f, 0x86, 0x17, 0x33, 0x53, 0xfd, 0xe0,
  0xb2, 0x4f, 0xe7, 0xf8, 0x28, 0xf3, 0x23, 0x88, 0xd6, 0x60, 0x52, 0xa4,
  0x77, 0x43, 0x6c, 0x9c, 0x1e, 0x35, 0x58, 0x7f, 0x1c, 0x04, 0x06, 0xec,
  0x3c, 0x2f, 0x18, 0xa6, 0xee, 0x71, 0xc4, 0x26, 0xf6, 0x53, 0x05, 0x8d,
  0x24, 0xad, 0x11, 0xf3, 0x2f, 0x11, 0xe6, 0x75, 0x65, 0xfd, 0x14, 0xcf,
  0x66, 0x5b, 0x27, 0x91, 0x3d, 0xfe, 0x59, 0xc2, 0x82, 0x3f, 0xa1, 0x36,
  0xe0, 0x3d, 0xb9, 0x86, 0xe3, 0xa5, 0x7a, 0xb6, 0xde, 0x72, 0xab, 0x31,
  0x4f, 0x06, 0xb0, 0xd8, 0x4e, 0xfa, 0x1b, 0xd0, 0x3a, 0x93, 0xae, 0x11,
  0xaa, 0x64, 0x2b, 0xd3,
};

int handle_verify_key(struct EseBootSession *session, std::vector<std::string> &args) {
  EseAppResult res;
  if (args[1] != "test" && args[1] != "auto") {
    fprintf(stderr, "verify-key: unknown command '%s'\n", args[2].c_str());
    return -1;
  }
  // Read in the hex unlockToken and hope for the best.
  std::vector<uint8_t> data;
  uint16_t len;
  if (args[1] == "test") {
    len = hexify(args[2], &data);
    const uint16_t kExpectedLength = (sizeof(uint64_t) * 2 + sizeof(uint64_t) + 32 + 256);
    if (len != kExpectedLength) {
      fprintf(stderr, "verify-key: expected blob of length %hu not %hu\n", kExpectedLength, len);
      fprintf(stderr, "verify-key: format is as follows (in hex):\n");
      fprintf(stderr, "[lastNonce:8][deviceData:32][version:8][unlockNonce:8][RSA-SHA256PKCS#1 Signature:256]\n");
      return 2;
    }
  } else {
    len = sizeof(auto_data);
    data.assign(&auto_data[0], auto_data + sizeof(auto_data));
  }
  printf("verify-key: sending the following test data:\n");
  print_hexdump(data.data(), 0, data.size());
  res = ese_boot_carrier_lock_test(session, data.data(), len);
  if (res == ESE_APP_RESULT_OK) {
    printf("verified\n");
    return 0;
  }
  printf("failed to verify (%.8x)\n", res);
  return 1;
}

int handle_lock_state(struct EseBootSession *session, std::vector<std::string> &args) {
  EseAppResult res;
  EseBootLockId lockId;
  uint16_t lockMetaLen = 0;
  uint8_t lockMeta[1024];
  if (args[2] == "carrier") {
    lockId = kEseBootLockIdCarrier;
  } else if (args[2] == "device") {
    lockId = kEseBootLockIdDevice;
  } else if (args[2] == "boot") {
    lockId = kEseBootLockIdBoot;
  } else if (args[2] == "owner") {
    lockId = kEseBootLockIdOwner;
  } else {
    fprintf(stderr, "lock: unknown lock '%s'\n", args[2].c_str());
    return 1;
  }

  if (args[1] == "get") {
    uint8_t lockVal = 0;
    if (lockId == kEseBootLockIdCarrier ||
        lockId == kEseBootLockIdOwner) {
      res = ese_boot_lock_xget(session, lockId, lockMeta,
                               sizeof(lockMeta), &lockMetaLen);
    } else {
      res = ese_boot_lock_get(session, lockId, &lockVal);
    }
    if (res == ESE_APP_RESULT_OK) {
      if (lockMetaLen > 0) {
        lockVal = lockMeta[0];
      }
      printf("%.2x\n", lockVal);
      if (lockMetaLen > 0) {
        print_hexdump(&lockMeta[1], 0, lockMetaLen - 1);
      }
      return 0;
     }
    fprintf(stderr, "lock: failed to get '%s' (%.8x)\n", args[2].c_str(), res);
    handle_error(session->ese, res);
    return 2;
  } else if (args[1] == "set") {
    if (args.size() < 4) {
      fprintf(stderr, "lock set: not enough arguments supplied\n");
      return 2;
    }
    uint8_t lockVal = static_cast<uint8_t>(std::stoi(args[3], nullptr, 0));
    if (lockId == kEseBootLockIdCarrier) {
      res = ESE_APP_RESULT_ERROR_UNCONFIGURED;
      if (lockVal != 0) {
        std::string device_data;
        device_data.push_back(lockVal);
        if (!collect_device_data(args[4], &device_data)) {
          fprintf(stderr, "carrier set 1: failed to aggregate device data\n");
          return 3;
        }
        printf("Setting carrier lock with '");
        for (std::string::iterator it = device_data.begin();
             it != device_data.end(); ++it) {
          printf("%c", isprint(*it) ? *it : '_');
        }
        printf("'\n");
        const uint8_t *data = reinterpret_cast<const uint8_t *>(device_data.data());
        res = ese_boot_lock_xset(session, lockId, data, device_data.length());
      } else {
        // Read in the hex unlockToken and hope for the best.
        std::vector<uint8_t> data;
        data.push_back(lockVal);
        uint16_t len = hexify(args[4], &data);
        if (len == 1) {
          fprintf(stderr, "lock: carrier unlock requires a token\n");
          return 5;
        }
        printf("Passing an unlockToken of length %d to the eSE\n", len - 1);
        res = ese_boot_lock_xset(session, lockId, data.data(), len);
      }
    } else if (lockId == kEseBootLockIdOwner && lockVal != 0) {
      std::vector<uint8_t> data;
      data.push_back(lockVal);
      uint16_t len = hexify(args[4], &data);
      res = ese_boot_lock_xset(session, lockId, data.data(), len);
    } else {
      res = ese_boot_lock_set(session, lockId, lockVal);
    }
    if (res != ESE_APP_RESULT_OK) {
      fprintf(stderr, "lock: failed to set %s state (%.8x)\n",
              args[2].c_str(), res);
      handle_error(session->ese, res);
      return 4;
    }
    return 0;
  }
  fprintf(stderr, "lock: invalid command\n");
  return -1;
}

int handle_rollback(struct EseBootSession *session, std::vector<std::string> &args) {
  int index = std::stoi(args[2], nullptr, 0);
  uint8_t slot = static_cast<uint8_t>(index & 0xff);
  if (slot > 7) {
    fprintf(stderr, "rollback: slot must be one of [0-7]\n");
    return 2;
  }

  uint64_t value = 0;
  if (args.size() > 3) {
    unsigned long long conv = std::stoull(args[3], nullptr, 0);
    value = static_cast<uint64_t>(conv);
  }

  EseAppResult res;
  if (args[1] == "get") {
    res = ese_boot_rollback_index_read(session, slot, &value);
    if (res != ESE_APP_RESULT_OK) {
      fprintf(stderr, "rollback: failed to read slot %2x (%.8x)\n",
              slot, res);
      handle_error(session->ese, res);
      return 3;
    }
    printf("%" PRIu64 "\n", value);
    return 0;
  } else if (args[1] == "set") {
    res = ese_boot_rollback_index_write(session, slot, value);
    if (res != ESE_APP_RESULT_OK) {
      fprintf(stderr, "rollback: failed to write slot %2x (%.8x)\n",
              slot, res);
      handle_error(session->ese, res);
      return 4;
    }
    return 0;
  }
  fprintf(stderr, "rollback: unknown command '%s'\n", args[1].c_str());
  return -1;
}

int handle_args(struct EseBootSession *session, const char *prog, std::vector<std::string> &args) {
  if (args[0] == "rollback") {
    return handle_rollback(session, args);
  } else if (args[0] == "lock") {
    return handle_lock_state(session, args);
  } else if (args[0] == "verify-key") {
    return handle_verify_key(session, args);
  } else if (args[0] == "production") {
    return handle_production(session, args);
  } else if (args[0] == "state") {
    return handle_state(session, args);
  } else {
    usage(prog);
    return 1;
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    usage(argv[0]);
    return 1;
  }
  // TODO(wad): move main to a class so we can just dep inject the hw.
  struct EseInterface ese = ESE_INITIALIZER(ESE_HW_NXP_PN80T_NQ_NCI);
  ese_open(&ese, nullptr);
  EseBootSession session;
  ese_boot_session_init(&session);
  EseAppResult res = ese_boot_session_open(&ese, &session);
  if (res != ESE_APP_RESULT_OK) {
    fprintf(stderr, "failed to initiate session (%.8x)\n", res);
    handle_error(ese, res);
    return 1;
  }
  std::vector<std::string> args;
  args.assign(argv + 1, argv + argc);
  int ret = handle_args(&session, argv[0], args);

  res = ese_boot_session_close(&session);
  if (res != ESE_APP_RESULT_OK) {
    handle_error(&ese, res);
  }
  ese_close(&ese);
  return ret;
}
