// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "VulkanLayerController.h"
#include "VulkanLayerProducer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace orbit_vulkan_layer {

namespace {

class MockDispatchTable {
 public:
};

class MockDeviceManager {
 public:
  explicit MockDeviceManager(MockDispatchTable* /*dispatch_table*/) {}
};

class MockQueueManager {
 public:
};

class MockTimerQueryPool {
 public:
  explicit MockTimerQueryPool(MockDispatchTable* /*dispatch_table*/, uint32_t /*num_slots*/) {}
};

class MockSubmissionTracker {
 public:
  explicit MockSubmissionTracker(MockDispatchTable* /*dispatch_table*/,
                                 MockTimerQueryPool* /*timer_query_pool*/,
                                 MockDeviceManager* /*device_manager*/, uint32_t /*max_depth*/) {}
  MOCK_METHOD((void), SetVulkanLayerProducer, (VulkanLayerProducer*));
};

class VulkanLayerControllerTest : public ::testing::Test {
 protected:
  VulkanLayerController<MockDispatchTable, MockQueueManager, MockDeviceManager, MockTimerQueryPool,
                        MockSubmissionTracker>
      controller_;
};

}  // namespace

TEST_F(VulkanLayerControllerTest, VkCreateInstance) {}

}  // namespace orbit_vulkan_layer
