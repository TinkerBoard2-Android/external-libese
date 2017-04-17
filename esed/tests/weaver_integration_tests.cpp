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

#include <string>

#include <android-base/logging.h>
#include <android/hardware/weaver/1.0/IWeaver.h>
#include <hidl/Status.h>

#include <gtest/gtest.h>

using ::android::OK;
using ::android::sp;
using ::android::status_t;
using ::android::hardware::weaver::V1_0::IWeaver;
using ::android::hardware::weaver::V1_0::WeaverConfig;
using ::android::hardware::weaver::V1_0::WeaverReadResponse;
using ::android::hardware::weaver::V1_0::WeaverReadStatus;
using ::android::hardware::weaver::V1_0::WeaverStatus;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;

using ::testing::Test;

const std::vector<uint8_t> KEY{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
const std::vector<uint8_t> WRONG_KEY{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const std::vector<uint8_t> VALUE{16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};

struct WeaverClientTest : public Test {
  sp<IWeaver> service;

  WeaverClientTest() = default;
  virtual ~WeaverClientTest() = default;
  void SetUp() override {
    service = IWeaver::getService();
    ASSERT_NE(service, nullptr);
  }
  void TearDown() override {}
};

TEST_F(WeaverClientTest, getConfig) {
  bool cbkCalled = false;
  WeaverStatus status;
  WeaverConfig config;
  auto ret = service->getConfig([&](WeaverStatus s, WeaverConfig c) {
    cbkCalled = true;
    status = s;
    config = c;
  });

  EXPECT_TRUE(cbkCalled);
  EXPECT_TRUE(ret.isOk());
  EXPECT_EQ(status, WeaverStatus::OK);
  const WeaverConfig expectedConfig{64, 16, 16};
  EXPECT_EQ(config, expectedConfig);
}

TEST_F(WeaverClientTest, writeAndReadBack) {
  const uint32_t slotId = 3;
  auto ret = service->write(slotId, KEY, VALUE);
  EXPECT_TRUE(ret.isOk());
  EXPECT_EQ(ret, WeaverStatus::OK);

  bool cbkCalled = false;
  WeaverReadStatus status;
  std::vector<uint8_t> readValue;
  uint32_t timeout;
  auto readRet = service->read(slotId, KEY, [&](WeaverReadStatus s, WeaverReadResponse r) {
    cbkCalled = true;
    status = s;
    readValue = r.value;
    timeout = r.timeout;
  });
  EXPECT_TRUE(cbkCalled);
  EXPECT_TRUE(readRet.isOk());
  EXPECT_EQ(status, WeaverReadStatus::OK);
  EXPECT_EQ(readValue, VALUE);
}

TEST_F(WeaverClientTest, writeAndReadWithWrongKey) {
  const uint32_t slotId = 3;
  auto ret = service->write(slotId, KEY, VALUE);
  EXPECT_TRUE(ret.isOk());
  EXPECT_EQ(ret, WeaverStatus::OK);

  bool cbkCalled = false;
  WeaverReadStatus status;
  std::vector<uint8_t> readValue;
  uint32_t timeout;
  auto readRet = service->read(slotId, WRONG_KEY, [&](WeaverReadStatus s, WeaverReadResponse r) {
    cbkCalled = true;
    status = s;
    readValue = r.value;
    timeout = r.timeout;
  });
  EXPECT_TRUE(cbkCalled);
  EXPECT_TRUE(readRet.isOk());
  EXPECT_EQ(status, WeaverReadStatus::INCORRECT_KEY);
  EXPECT_EQ(timeout, uint32_t{0}); // first timeout is 0
}
