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

#include <ese/ese.h>
#include <ese/log.h>

#include "ese_private.h"

static const char kUnknownHw[] = "unknown hw";
static const char kNullEse[] = "NULL EseInterface";
static const char *kEseErrorMessages[] = {
    "Hardware supplied no transceive implementation.",
    "Timed out polling for value.",
};
#define ESE_MESSAGES(x) (sizeof(x) / sizeof((x)[0]))

/* TODO(wad): Make the default visibility on this one default default? */
API const char *ese_name(struct EseInterface *ese) {
  if (!ese)
    return kNullEse;
  if (ese->ops->name)
    return ese->ops->name;
  return kUnknownHw;
}

API int ese_open(struct EseInterface *ese, void *hw_opts) {
  if (!ese)
    return -1;
  ALOGV("opening interface '%s'", ese_name(ese));
  if (ese->ops->open)
    return ese->ops->open(ese, hw_opts);
  return 0;
}

API const char *ese_error_message(struct EseInterface *ese) {
  return ese->error.message;
}

API int ese_error_code(struct EseInterface *ese) { return ese->error.code; }

API int ese_error(struct EseInterface *ese) { return ese->error.is_err; }

API void ese_set_error(struct EseInterface *ese, int code) {
  if (!ese)
    return;
  /* Negative values are reserved for API wide messages. */
  ese->error.code = code;
  ese->error.is_err = 1;
  if (code < 0) {
    code = -(code + 1); /* Start at 0. */
    if ((size_t)(code) >= ESE_MESSAGES(kEseErrorMessages)) {
      LOG_ALWAYS_FATAL("Unknown global error code passed to ese_set_error(%d)",
                       code);
    }
    ese->error.message = kEseErrorMessages[code];
    return;
  }
  if ((size_t)(code) >= ese->errors_count) {
    LOG_ALWAYS_FATAL("Unknown hw error code passed to ese_set_error(%d)", code);
  }
  ese->error.message = ese->errors[code];
}

/* Blocking. */
API int ese_transceive(struct EseInterface *ese, uint8_t *const tx_buf,
                       size_t tx_len, uint8_t *rx_buf, size_t rx_max) {
  size_t recvd = 0;
  if (!ese)
    return -1;
  while (1) {
    if (ese->ops->transceive) {
      recvd = ese->ops->transceive(ese, tx_buf, tx_len, rx_buf, rx_max);
      break;
    }
    if (ese->ops->hw_transmit && ese->ops->hw_receive) {
      ese->ops->hw_transmit(ese, tx_buf, tx_len, 1);
      if (ese->error.is_err)
        break;
      recvd = ese->ops->hw_receive(ese, rx_buf, rx_max, 1);
      break;
    }
    ese_set_error(ese, -1);
    break;
  }
  if (ese->error.is_err)
    return -1;
  return recvd;
}

API int ese_close(struct EseInterface *ese) {
  if (!ese)
    return -1;
  ALOGV("closing interface '%s'", ese_name(ese));
  if (!ese->ops->close)
    return 0;
  return ese->ops->close(ese);
}
