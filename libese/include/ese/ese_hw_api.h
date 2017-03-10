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
 */

#ifndef ESE_HW_API_H_
#define ESE_HW_API_H_ 1

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/*
 * Pulls the hardware declarations in to scope for the current file
 * to make use of.
 */
#define __ESE_INCLUDE_HW(name) \
  extern const struct EseOperations * name## _ops; \
  extern const char ** name##_errors; \
  extern const size_t name##_errors_count


struct EseInterface;
/* !! Note !!
 * Receive and transmit operations on SPI buses should ensure the CS
 * does not change between subsequent recieve (or transmit) calls unless
 * the |complete| argument is 1.
 *
 * In practice, this should not require additional state tracking as entry
 * to each function can simply assert the CS state (even if unchanged) and
 * then check whether to unassert based on |complete|.
 *
 * Other communications backends may have different needs which may be solved
 * separately by minimally processing protocol headers.
 *
 * The other option is having a transactional view where we have an explicit
 * begin/end or claim/release.
 */
typedef size_t (ese_hw_receive_op_t)(struct EseInterface *, uint8_t *, size_t, int);
typedef size_t (ese_hw_transmit_op_t)(struct EseInterface *, const uint8_t *, size_t, int);
typedef int (ese_hw_reset_op_t)(struct EseInterface *);
/* Implements wire protocol transceiving and will likely also then require locking. */
typedef size_t (ese_transceive_op_t)(struct EseInterface *, const uint8_t *, size_t, uint8_t *, size_t);
/* Returns 0 on timeout, 1 on byte seen, -1 on error. */
typedef int (ese_poll_op_t)(struct EseInterface *, uint8_t, float, int);
typedef int (ese_open_op_t)(struct EseInterface *, void *);
typedef int (ese_close_op_t)(struct EseInterface *);

#define __ESE_INITIALIZER(TYPE) \
{ \
  .ops = TYPE## _ops, \
  .errors = TYPE## _errors, \
  .errors_count = TYPE## _errors_count, \
  .pad =  { 0 }, \
}

#define __ese_init(_ptr, TYPE) {\
  _ptr->ops = TYPE## _ops; \
  _ptr->errors = TYPE## _errors; \
  _ptr->errors_count = TYPE## _errors_count; \
  _ptr->pad[0] = 0; \
}

struct EseOperations {
  const char *const name;
  /* Used to prepare any implementation specific internal data and
   * state needed for robust communication.
   */
  ese_open_op_t *const open;
  /* Used to receive raw data from the ese. */
  ese_hw_receive_op_t *const hw_receive;
  /* Used to transmit raw data to the ese. */
  ese_hw_transmit_op_t *const hw_transmit;
  /* Used to perform a power reset on the device. */
  ese_hw_reset_op_t *const hw_reset;
  /* Wire-specific protocol polling for readiness. */
  ese_poll_op_t *const poll;
  /* Wire-specific protocol for transmitting and receiving
   * application data to the eSE. By default, this may point to
   * a generic implementation, like teq1_transceive, which uses
   * the hw_* ops above.
   */
  ese_transceive_op_t *const transceive;
  /* Cleans up any required state: file descriptors or heap allocations. */
  ese_close_op_t *const close;

  /* Operational options */
  const void *const opts;
};

/* Maximum private stack storage on the interface instance. */
#define ESE_INTERFACE_STATE_PAD 16
struct EseInterface {
  const struct EseOperations * ops;
  struct {
    int is_err;
    int code;
    const char *message;
  } error;
  const char **errors;
  size_t errors_count;
  /* Reserved to avoid heap allocation requirement. */
  uint8_t pad[ESE_INTERFACE_STATE_PAD];
};


#define ESE_DEFINE_HW_ERRORS(name, ary) \
  const char ** name##_errors = (ary); \
  const size_t name##_errors_count = (sizeof(ary) / sizeof((ary)[0]))

#define ESE_DEFINE_HW_OPS(name, obj) \
  const struct EseOperations * name##_ops = &obj

#ifdef __cplusplus
}  /* extern "C" */
#endif
#endif  /* ESE_HW_API_H_ */
