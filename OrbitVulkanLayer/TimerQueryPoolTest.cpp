// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "TimerQueryPool.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::Return;

namespace orbit_vulkan_layer {

namespace {
class MockDispatchTable {
 public:
  MOCK_METHOD(PFN_vkCreateQueryPool, CreateQueryPool, (VkDevice), ());
  MOCK_METHOD(PFN_vkResetQueryPoolEXT, ResetQueryPoolEXT, (VkDevice), ());
};

}  // namespace

TEST(TimerQueryPool, ATimerQueryPoolMustGetInitialized) {
  MockDispatchTable dispatch_table;
  uint32_t num_slots = 4;
  TimerQueryPool query_pool(&dispatch_table, num_slots);
  VkDevice device = {};

  uint32_t slot_index = 32;
  std::vector<uint32_t> reset_slots;
  EXPECT_DEATH({ (void)query_pool.GetQueryPool(device); }, "");
  EXPECT_DEATH({ (void)query_pool.NextReadyQuerySlot(device, &slot_index); }, "");
  reset_slots.push_back(slot_index);
  EXPECT_DEATH({ (void)query_pool.ResetQuerySlots(device, reset_slots); }, "");
  EXPECT_DEATH({ (void)query_pool.RollbackPendingQuerySlots(device, reset_slots); }, "");
}

TEST(TimerQueryPool, InitializationWillCreateAndResetAPool) {
  MockDispatchTable dispatch_table;
  static constexpr uint32_t num_slots = 4;
  TimerQueryPool<MockDispatchTable> query_pool(&dispatch_table, num_slots);
  VkDevice device = {};

  PFN_vkCreateQueryPool mock_create_query_pool_function =
      +[](VkDevice /*device*/, const VkQueryPoolCreateInfo* create_info,
          const VkAllocationCallbacks* /*allocator*/, VkQueryPool* query_pool_out) -> VkResult {
    EXPECT_EQ(create_info->queryType, VK_QUERY_TYPE_TIMESTAMP);
    EXPECT_EQ(create_info->queryCount, num_slots);

    *query_pool_out = {};
    return VK_SUCCESS;
  };

  PFN_vkResetQueryPoolEXT mock_reset_query_pool_function =
      +[](VkDevice /*device*/, VkQueryPool /*query_pool*/, uint32_t first_query,
          uint32_t query_count) {
        EXPECT_EQ(first_query, 0);
        EXPECT_EQ(query_count, num_slots);
      };

  EXPECT_CALL(dispatch_table, CreateQueryPool)
      .Times(1)
      .WillOnce(Return(mock_create_query_pool_function));
  EXPECT_CALL(dispatch_table, ResetQueryPoolEXT)
      .Times(1)
      .WillOnce(Return(mock_reset_query_pool_function));

  query_pool.InitializeTimerQueryPool(device);
}

}  // namespace orbit_vulkan_layer
