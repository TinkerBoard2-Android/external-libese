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
 *
 * Support SPI communication with NXP PN553/PN80T secure element.
 */

#include <fcntl.h>
#include <limits.h>
#include <linux/spi/spidev.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <ese/ese.h>
#include <ese/hw/nxp/spi_board.h>
#include <ese/teq1.h>
#define LOG_TAG "libese-hw"
#include <ese/log.h>

/* Card state is _required_ to be at the front of eSE pad. */
struct NxpState {
  struct Teq1CardState card_state;
  int spi_fd;
  struct NxpSpiBoard *board;
};
#define NXP_PN80T_SPIDEV_STATE(ese) ((struct NxpState *)(&ese->pad[0]))

int gpio_set(int num, int val) {
  char val_path[256];
  char val_chr = (val ? '1' : '0');
  int fd;
  if (snprintf(val_path, sizeof(val_path), "/sys/class/gpio/gpio%d/value",
               num) >= (int)sizeof(val_path))
    return -1;
  fd = open(val_path, O_WRONLY);
  if (fd < 0)
    return -1;
  if (write(fd, &val_chr, 1) < 0) {
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

int gpio_configure(int num, int out, int val) {
  char dir_path[256];
  char numstr[8];
  char dir[5];
  int fd;
  if (snprintf(dir, sizeof(dir), "%s", (out ? "out" : "in")) >=
      (int)sizeof(dir))
    return -1;
  if (snprintf(dir_path, sizeof(dir_path), "/sys/class/gpio/gpio%d/direction",
               num) >= (int)sizeof(dir_path))
    return -1;
  if (snprintf(numstr, sizeof(numstr), "%d", num) >= (int)sizeof(numstr))
    return -1;
  fd = open("/sys/class/gpio/export", O_WRONLY);
  if (fd < 0)
    return -1;
  /* Exporting can only happen once, so instead of stat()ing, just ignore
   * errors. */
  (void)write(fd, numstr, strlen(numstr));
  close(fd);

  fd = open(dir_path, O_WRONLY);
  if (fd < 0)
    return -1;
  if (write(fd, dir, strlen(dir)) < 0) {
    close(fd);
    return -1;
  }
  close(fd);
  return gpio_set(num, val);
}

int nxp_pn80t_open(struct EseInterface *ese, void *hw_opts) {
  struct NxpState *ns;
  uint8_t mode = 0;
  uint32_t bits = 8;
  uint32_t speed = 1000000L;
  if (!ese)
    return -1;
  if (sizeof(ese->pad) < sizeof(struct NxpState *)) {
    /* This is a compile-time correctable error only. */
    ALOGE("Pad size too small to use NXP HW (%zu < %zu)", sizeof(ese->pad),
          sizeof(struct NxpState));
    return -1;
  }
  ns = NXP_PN80T_SPIDEV_STATE(ese);
  TEQ1_INIT_CARD_STATE((&ns->card_state));
  ns->board = (struct NxpSpiBoard *)(hw_opts);

  /* Configure ESE_SVDD_PWR_REQ */
  /* TODO(wad): We can leave this low and move it to set/unset
   * in a transceive() wrapper.
   */
  if (gpio_configure(ns->board->svdd_pwr_req_gpio, 1, 1) < 0) {
    ese_set_error(ese, 13);
    return -1;
  }

  /* Configure ESE_RST_GPIO */
  if (gpio_configure(ns->board->reset_gpio, 1, 1) < 0) {
    ese_set_error(ese, 12);
    return -1;
  }

  ns->spi_fd = open(ns->board->dev_path, O_RDWR);
  if (ns->spi_fd < 0) {
    ALOGE("failed to open spidev: %s", ns->board->dev_path);
    ese_set_error(ese, 4);
    return -1;
  }

  /* If we need anything fancier, we'll need MODE32 in the headers. */
  if (ioctl(ns->spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
    ALOGE("failed to set spidev mode to %d", mode);
    ese_set_error(ese, 5);
    return -1;
  }
  if (ioctl(ns->spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
    ALOGE("failed to set spidev bits per word to %d", bits);
    ese_set_error(ese, 6);
    return -1;
  }
  if (ioctl(ns->spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
    ALOGE("failed to set spidev max speed to %dhz", speed);
    ese_set_error(ese, 7);
    return -1;
  }
  return 0;
}

int nxp_pn80t_close(struct EseInterface *ese) {
  struct NxpState *ns;
  if (!ese)
    return -1;
  ns = NXP_PN80T_SPIDEV_STATE(ese);
  close(ns->spi_fd);
  /* We're done. */
  gpio_set(ns->board->svdd_pwr_req_gpio, 0);
  return 0;
}

size_t nxp_pn80t_receive(struct EseInterface *ese, uint8_t *buf, size_t len,
                         int complete) {
  struct NxpState *ns = NXP_PN80T_SPIDEV_STATE(ese);
  size_t recvd = len;
  struct spi_ioc_transfer tr = {
      .tx_buf = 0,
      .rx_buf = (unsigned long)buf,
      .len = (uint32_t)len,
      .delay_usecs = 0,
      .speed_hz = 0,
      .bits_per_word = 0,
      .cs_change = !!complete,
  };

  if (len > UINT_MAX) {
    ese_set_error(ese, 9);
    ALOGE("Unexpectedly large receive attempted: %zu", len);
    return 0;
  }

  ALOGV("interface attempting to receive card data");
  if (ioctl(ns->spi_fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
    ese_set_error(ese, 8);
    return 0;
  }
  ALOGV("card sent %zu bytes", len);
  for (recvd = 0; recvd < len; ++recvd)
    ALOGV("RX[%zu]: %.2X", recvd, buf[recvd]);
  if (complete) {
    ALOGV("card sent a frame");
    /* XXX: cool off the bus for 1ms [t_3] */
  }
  return recvd;
}

int nxp_pn80t_reset(struct EseInterface *ese) {
  struct NxpState *ns = NXP_PN80T_SPIDEV_STATE(ese);
  if (gpio_set(ns->board->reset_gpio, 0) < 0) {
    ese_set_error(ese, 13);
    return -1;
  }
  usleep(1000);
  if (gpio_set(ns->board->reset_gpio, 1) < 0) {
    ese_set_error(ese, 13);
    return -1;
  }
  return 0;
}

size_t nxp_pn80t_transmit(struct EseInterface *ese, const uint8_t *buf,
                          size_t len, int complete) {
  struct NxpState *ns = NXP_PN80T_SPIDEV_STATE(ese);
  size_t recvd = len;
  struct spi_ioc_transfer tr = {
      .tx_buf = (unsigned long)buf,
      .rx_buf = 0,
      .len = (uint32_t)len,
      .delay_usecs = 0,
      .speed_hz = 0,
      .bits_per_word = 0,
      .cs_change = !!complete,
  };
  ALOGV("interface transmitting data");
  if (len > UINT_MAX) {
    ese_set_error(ese, 10);
    ALOGE("Unexpectedly large transfer attempted: %zu", len);
    return 0;
  }

  ALOGV("interface attempting to transmit data");
  for (recvd = 0; recvd < len; ++recvd)
    ALOGV("TX[%zu]: %.2X", recvd, buf[recvd]);
  if (ioctl(ns->spi_fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
    ese_set_error(ese, 11);
    return 0;
  }
  ALOGV("interface sent %zu bytes", len);
  if (complete) {
    ALOGV("interface sent a frame");
    /* TODO(wad): Remove this once we have live testing. */
    usleep(1000); /* t3 = 1ms */
  }
  return recvd;
}

int nxp_pn80t_poll(struct EseInterface *ese, uint8_t poll_for, float timeout,
                   int complete) {
  struct NxpState *es = NXP_PN80T_SPIDEV_STATE(ese);
  const struct Teq1ProtocolOptions *opts = ese->ops->opts;
  /* Attempt to read a 8-bit character once per 8-bit character transmission
   * window (in seconds). */
  int intervals = (int)(0.5f + timeout / (7.0f * opts->etu));
  uint8_t byte = 0xff;
  ALOGV("interface polling for start of frame/host node address: %x", poll_for);
  /* If we weren't using spidev, we could just get notified by the driver. */
  do {
    struct spi_ioc_transfer tr = {
        .tx_buf = 0,
        .rx_buf = (unsigned long)&byte,
        .len = 1,
        .delay_usecs = 0,
        .speed_hz = 0,
        .bits_per_word = 0,
        .cs_change = !!complete,
    };
    /*
     * In practice, if complete=true, then no transmission
     * should attempt again until after 1000usec.
     */
    if (ioctl(es->spi_fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
      ALOGV("spidev (fd:%d) failed to read one byte", es->spi_fd);
      ese_set_error(ese, 3);
      return -1;
    }
    if (byte == poll_for) {
      ALOGV("Polled for byte seen: %x with %d intervals remaining.", poll_for,
            intervals);
      ALOGV("RX[0]: %.2X", byte);
      return 1;
    } else {
      ALOGV("No match (saw %x)", byte);
    }
    usleep(7.0f * opts->etu * 1000000.0f); /* s -> us */
    ALOGV("poll interval %d: no match.", intervals);
  } while (intervals-- > 0);
  return -1;
}

int nxp_pn80t_preprocess(const struct Teq1ProtocolOptions *const opts,
                         struct Teq1Frame *frame, int tx) {
  if (tx) {
    /* Recompute the LRC with the NAD of 0x00 */
    frame->header.NAD = 0x00;
    frame->INF[frame->header.LEN] = teq1_compute_LRC(frame);
    frame->header.NAD = opts->node_address;
    ALOGV("interface is preprocessing outbound frame");
  } else {
    /* Replace the NAD with 0x00 so the LRC check passes. */
    ALOGV("interface is preprocessing inbound frame (%x->%x)",
          frame->header.NAD, 0x00);
    if (frame->header.NAD != opts->host_address)
      ALOGV("Rewriting from unknown NAD: %x", frame->header.NAD);
    frame->header.NAD = 0x00;
    ALOGV("Frame length: %x", frame->header.LEN);
  }
  return 0;
}

static const struct Teq1ProtocolOptions teq1_options = {
    .host_address = 0xA5,
    .node_address = 0x5A,
    .bwt = 1.624f,   /* cwt by default would be ~8k * 1.05s */
    .etu = 0.00105f, /* seconds */
    .preprocess = &nxp_pn80t_preprocess,
};

static const struct EseOperations ops = {
    .name = "NXP PN80T (PN553)",
    .open = &nxp_pn80t_open,
    .hw_receive = &nxp_pn80t_receive,
    .hw_transmit = &nxp_pn80t_transmit,
    .hw_reset = &nxp_pn80t_reset,
    .transceive = &teq1_transceive,
    .poll = &nxp_pn80t_poll,
    .close = &nxp_pn80t_close,
    .opts = &teq1_options,
};
ESE_DEFINE_HW_OPS(ESE_HW_NXP_PN80T_SPIDEV, ops);

static const char *kErrorMessages[] = {
    "T=1 hard failure.",                         /* TEQ1_ERROR_HARD_FAIL */
    "T=1 abort.",                                /* TEQ1_ERROR_ABORT */
    "T=1 device reset failed.",                  /* TEQ1_ERROR_DEVICE_ABORT */
    "spidev failed to read one byte",            /* 3 */
    "unable to open spidev device",              /* 4 */
    "unable to set spidev mode",                 /* 5 */
    "unable to set spidev bits per word",        /* 6 */
    "unable to set spidev max speed in hz",      /* 7 */
    "spidev failed to read",                     /* 8 */
    "attempted to receive more than uint_max",   /* 9 */
    "attempted to transfer more than uint_max",  /* 10 */
    "spidev failed to transmit",                 /* 11 */
    "unable to configure ESE_RST gpio",          /* 12 */
    "unable to configure ESE_SVDD_PWR_REQ gpio", /* 13 */
    "unable to toggle ESE_SVDD_PWR_REQ",         /* 14 */
};
ESE_DEFINE_HW_ERRORS(ESE_HW_NXP_PN80T_SPIDEV, kErrorMessages);
