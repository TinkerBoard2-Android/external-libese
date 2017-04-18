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
#include <android/hardware/oemlock/1.0/IOemLock.h>
#include <hidl/Status.h>

#include <gtest/gtest.h>

using ::android::OK;
using ::android::sp;
using ::android::status_t;
using ::android::hardware::oemlock::V1_0::IOemLock;
using ::android::hardware::oemlock::V1_0::OemLockSecureStatus;
using ::android::hardware::oemlock::V1_0::OemLockStatus;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;

using ::testing::Test;

class OemLockClientTest : public virtual Test {
 public:
  OemLockClientTest() {
  }
  virtual ~OemLockClientTest() { }
  virtual void SetUp() {
    service_ = IOemLock::getService();
    ASSERT_NE(service_, nullptr);
  }
  virtual void TearDown() { }
  sp<IOemLock> service_;
};

TEST_F(OemLockClientTest, GetName) {
  std::string name;
  OemLockStatus status;
  auto get_name_cb = [&status, &name](OemLockStatus cb_status, hidl_string cb_name) {
    status = cb_status;
    name = cb_name.c_str();
  };
  Return<void> ret = service_->getName(get_name_cb);

  EXPECT_TRUE(ret.isOk());
  EXPECT_EQ(status, OemLockStatus::OK);
  EXPECT_STREQ(name.c_str(), "01");
};

TEST_F(OemLockClientTest, AllowedByDeviceToggle) {
  // Should always work as it is independent of carrier and boot lock states.
  bool allowed = true;
  OemLockStatus status;
  auto get_allowed_cb = [&status, &allowed](OemLockStatus cb_status, bool cb_allowed) {
    status = cb_status;
    allowed = cb_allowed;
  };

  Return<OemLockStatus> set_ret = service_->setOemUnlockAllowedByDevice(allowed);
  EXPECT_EQ(set_ret, OemLockStatus::OK);
  Return<void> get_ret = service_->isOemUnlockAllowedByDevice(get_allowed_cb);
  EXPECT_EQ(status, OemLockStatus::OK);
  EXPECT_EQ(true, allowed);

  allowed = false;
  set_ret = service_->setOemUnlockAllowedByDevice(allowed);
  EXPECT_EQ(set_ret, OemLockStatus::OK);
  get_ret = service_->isOemUnlockAllowedByDevice(get_allowed_cb);
  EXPECT_EQ(status, OemLockStatus::OK);
  EXPECT_EQ(false, allowed);
};

TEST_F(OemLockClientTest, GetAllowedByCarrierIsFalse) {
  bool allowed = true;
  OemLockStatus status;
  auto get_allowed_cb = [&status, &allowed](OemLockStatus cb_status, bool cb_allowed) {
    status = cb_status;
    allowed = cb_allowed;
  };

  Return<void> ret = service_->isOemUnlockAllowedByCarrier(get_allowed_cb);
  EXPECT_EQ(status, OemLockStatus::OK);
  EXPECT_EQ(false, allowed);
};
