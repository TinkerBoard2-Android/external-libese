/*
 * Copyright (C) 2016 The Android Open Source Project
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
 */

#ifndef ESE_TEQ1_H_
#define ESE_TEQ1_H_ 1

#ifdef __cplusplus
extern "C" {
#endif

#include <ese/ese.h>

/* Reserved codes for T=1 devices. */
#define TEQ1_ERROR_HARD_FAIL 0
#define TEQ1_ERROR_ABORT 1
#define TEQ1_ERROR_DEVICE_RESET 2


enum pcb_type {
  kPcbTypeInfo0 = 0x0,
  kPcbTypeInfo1 = 0x1,
  kPcbTypeReceiveReady = 0x2,
  kPcbTypeSupervisory = 0x3,
  kPcbTypeMax,
};

enum super_type {
  kSuperTypeResync = 0x0,
  kSuperTypeIFS = 0x1,
  kSuperTypeAbort = 0x2,
  kSuperTypeWTX = 0x3,
};

struct PCB {
  union {
    /* Info bits */
    struct {
      uint8_t reserved:5;  /* Should be 0. */
      uint8_t more_data:1;
      uint8_t send_seq:1;
      uint8_t bit8:1;  /* Must be 0 for I-blocks. */
    } I;  /* Information Block */
    /* receive bits */
    struct {
      uint8_t parity_err:1;  /* char parity or redundancy code err */
      uint8_t other_err:1;  /*  any other errors */
      uint8_t unused_1:2;
      uint8_t next_seq:1;  /* If the same seq as last frame, then err even if other bits are 0. */
      uint8_t unused_0:1;
      uint8_t pcb_type:2;  /* Always (1, 0)=2 for R */
    } R;  /* Receive ready block */
    struct {
      uint8_t type:2;
      uint8_t unused_0:3;
      uint8_t response:1;
      uint8_t pcb_type:2;
    } S;  /* Supervisory block */
    struct {
      uint8_t data:6;
      uint8_t type:2; /* I = 0|1, R = 2, S = 3 */
    };  /* Bit7-8 access for block type access. */
    /* Bitwise access */
    struct {
     uint8_t bit0:1;  /* lsb */
     uint8_t bit1:1;
     uint8_t bit2:1;
     uint8_t bit3:1;
     uint8_t bit4:1;
     uint8_t bit5:1;
     uint8_t bit6:1;
     uint8_t bit7:1; /* msb */
    } bits;
    uint8_t val;
  };
};

struct Teq1Header {
  uint8_t NAD;
  struct PCB PCB;
  uint8_t LEN;
} __attribute__((packed));

#define INF_LEN 254
#define IFSC 254
struct Teq1Frame {
  union {
    uint8_t val[sizeof(struct Teq1Header) + INF_LEN + 1];
    struct {
      struct Teq1Header header;
      union {
        uint8_t INF[INF_LEN + 1]; /* Up to 254 with trailing LRC byte. */
      };
      /* uint8_t LRC;  If CRC was supported, it would be uint16_t. */
    };
  };
} __attribute__((packed));


/*
 * Required to be the header for all EseInterface pad[]s for
 * cards implementing T=1.
 */
struct Teq1CardState {
  union {
    struct {
      int card:1;
      int interface:1;
    };
    uint8_t seq_bits;
  } seq;
};
/* Set "last sent" to 1 so we start at 0. */
#define TEQ1_INIT_CARD_STATE(CARD) \
  (CARD)->seq.card = 1; \
  (CARD)->seq.interface = 1;

/*
 * Used by devices implementing T=1 to set specific options
 * or callback behavior.
 */
struct Teq1ProtocolOptions;
typedef int (teq1_protocol_preprocess_op_t)(const struct Teq1ProtocolOptions *const, struct Teq1Frame *, int);

struct Teq1ProtocolOptions {
  uint8_t host_address;  /* NAD to listen for */
  uint8_t node_address;  /* NAD to send to */
  float bwt;
  float etu;
  /*
   * If not NULL, is called immediately before transmit (1)
   * and immediately after receive.
   */
  teq1_protocol_preprocess_op_t *preprocess;
};

/* I-block bits */
#define kTeq1InfoMoreBit     (1 << 5)
#define kTeq1InfoSeqBit      (1 << 6)

/* R-block bits */
#define kTeq1RrType         (1 << 7)
#define kTeq1RrSeqBit       (1 << 4)
#define kTeq1RrParityError  (1)
#define kTeq1RrOtherError   (1 << 1)

/* S-block bits */
#define kTeq1SuperType      (3 << 6)
#define kTeq1SuperRequestBit (0)
#define kTeq1SuperResponseBit (1 << 5)
#define kTeq1SuperResyncBit (0)
#define kTeq1SuperIfsBit (1)
#define kTeq1SuperAbortBit (1 << 1)
#define kTeq1SuperWtxBit (3)

/* I(Seq, More-bit) */
#define TEQ1_I(S, M) ((S) << 6) | ((M) << 5)

/* R(Seq, Other Error, Parity Error) */
#define TEQ1_R(S, O, P) (kTeq1RrType | ((S) << 4) | (P) | ((O) << 1))
/* S_<TYPE>(response) */
#define TEQ1_S_RESYNC(R) (kTeq1SuperType | ((R) << 5) | kTeq1SuperResyncBit)
#define TEQ1_S_WTX(R)  (kTeq1SuperType | ((R) << 5) | kTeq1SuperWtxBit)
#define TEQ1_S_ABORT(R) (kTeq1SuperType | ((R) << 5) | kTeq1SuperAbortBit)
#define TEQ1_S_IFS(R) (kTeq1SuperType | ((R) << 5) | kTeq1SuperIfsBit)

size_t teq1_transceive(struct EseInterface *ese,
                       const uint8_t *const tx_buf, size_t tx_len,
                       uint8_t *rx_buf, size_t rx_max);

uint8_t teq1_compute_LRC(const struct Teq1Frame *frame);

#define teq1_trace_header() ALOGI("%-20s --- %20s", "Interface", "Card")
#define teq1_trace_transmit(PCB, LEN) ALOGI("%-20s --> %20s [%3hhu]", teq1_pcb_to_name(PCB), "", LEN)
#define teq1_trace_receive(PCB, LEN) ALOGI("%-20s <-- %20s [%3hhu]", "", teq1_pcb_to_name(PCB), LEN)

#ifdef __cplusplus
}  /* extern "C" */
#endif
#endif  /* ESE_TEQ1_H_ */
