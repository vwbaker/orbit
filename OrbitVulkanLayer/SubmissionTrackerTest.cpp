// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "SubmissionTracker.h"
#include "VulkanLayerProducer.h"
#include "absl/base/casts.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::UnorderedElementsAre;

namespace orbit_vulkan_layer {
namespace {
class MockDispatchTable {
 public:
  MOCK_METHOD(PFN_vkGetQueryPoolResults, GetQueryPoolResults, (VkDevice), ());
  MOCK_METHOD(PFN_vkCmdWriteTimestamp, CmdWriteTimestamp, (VkCommandBuffer), ());
};

PFN_vkCmdWriteTimestamp dummy_write_timestamp_function =
    +[](VkCommandBuffer /*command_buffer*/, VkPipelineStageFlagBits /*pipeline_stage*/,
        VkQueryPool /*query_pool*/, uint32_t /*query*/) {};

class MockTimerQueryPool {
 public:
  MOCK_METHOD(VkQueryPool, GetQueryPool, (VkDevice), ());
  MOCK_METHOD(void, ResetQuerySlots, (VkDevice, const std::vector<uint32_t>&), ());
  MOCK_METHOD(void, RollbackPendingQuerySlots, (VkDevice, const std::vector<uint32_t>&), ());
  MOCK_METHOD(bool, NextReadyQuerySlot, (VkDevice, uint32_t*), ());
};

class MockDeviceManager {
 public:
  MOCK_METHOD(VkPhysicalDevice, GetPhysicalDeviceOfLogicalDevice, (VkDevice), ());
  MOCK_METHOD(VkPhysicalDeviceProperties, GetPhysicalDeviceProperties, (VkPhysicalDevice), ());
};

class MockVulkanLayerProducer : public VulkanLayerProducer {
 public:
  MOCK_METHOD(bool, IsCapturing, (), (override));
  MOCK_METHOD(uint64_t, InternStringIfNecessaryAndGetKey, (std::string), (override));
  MOCK_METHOD(void, EnqueueCaptureEvent, (orbit_grpc_protos::CaptureEvent && capture_event),
              (override));

  MOCK_METHOD(bool, BringUp, (std::string_view unix_domain_socket_path), (override));
  MOCK_METHOD(void, TakeDown, (), (override));
};

}  // namespace

TEST(SubmissionTracker, CanBeInitialized) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<VulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager, &producer);
}

TEST(SubmissionTracker, CannotTrackTheSameCommandBufferTwice) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<VulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager, &producer);

  VkDevice device = {};
  VkCommandPool pool = {};
  VkCommandBuffer command_buffer = {};
  tracker.TrackCommandBuffers(device, pool, &command_buffer, 1);
  EXPECT_DEATH({ tracker.TrackCommandBuffers(device, pool, &command_buffer, 1); }, "");
}

TEST(SubmissionTracker, CannotUntrackAnUntrackedCommandBuffer) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<VulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager, &producer);

  VkDevice device = {};
  VkCommandPool pool = {};
  VkCommandBuffer command_buffer = {};
  EXPECT_DEATH({ tracker.UntrackCommandBuffers(device, pool, &command_buffer, 1); }, "");
}

TEST(SubmissionTracker, CanTrackCommandBufferAgainAfterUntrack) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<VulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager, &producer);

  VkDevice device = {};
  VkCommandPool pool = {};
  VkCommandBuffer command_buffer = {};
  tracker.TrackCommandBuffers(device, pool, &command_buffer, 1);
  tracker.UntrackCommandBuffers(device, pool, &command_buffer, 1);
  tracker.TrackCommandBuffers(device, pool, &command_buffer, 1);
}

TEST(SubmissionTracker, MarkCommandBufferBeginWontWriteTimestampsWhenNotCapturing) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<MockVulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager,
      absl::bit_cast<std::unique_ptr<VulkanLayerProducer>*>(&producer));

  VkDevice device = {};
  VkCommandPool command_pool = {};
  VkCommandBuffer command_buffer = {};

  EXPECT_CALL(*producer, IsCapturing).Times(1).WillOnce(Return(false));
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot).Times(0);

  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
}

TEST(SubmissionTracker, MarkDebugMarkersWontWriteTimestampsWhenNotCapturing) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<MockVulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager,
      absl::bit_cast<std::unique_ptr<VulkanLayerProducer>*>(&producer));

  VkDevice device = {};
  VkCommandPool command_pool = {};
  VkCommandBuffer command_buffer = {};

  EXPECT_CALL(*producer, IsCapturing).Times(3).WillRepeatedly(Return(false));
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot).Times(0);

  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkDebugMarkerBegin(command_buffer, "Test", {});
  tracker.MarkDebugMarkerEnd(command_buffer);
}

TEST(SubmissionTracker, MarkCommandBufferBeginWillWriteTimestampWhenCapturing) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<MockVulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager,
      absl::bit_cast<std::unique_ptr<VulkanLayerProducer>*>(&producer));

  VkDevice device = {};
  VkCommandPool command_pool = {};
  VkCommandBuffer command_buffer = {};
  VkQueryPool query_pool = {};

  // We cannot capture anything in that lambda as we need to cast it to a function pointer.
  static bool was_called = false;
  static constexpr uint32_t kSlotIndex = 32;

  PFN_vkCmdWriteTimestamp mock_write_timestamp_function =
      +[](VkCommandBuffer /*command_buffer*/, VkPipelineStageFlagBits /*pipeline_stage*/,
          VkQueryPool /*query_pool*/, uint32_t query) {
        EXPECT_EQ(query, kSlotIndex);
        was_called = true;
      };
  auto mock_next_ready_query_slot_function = [](VkDevice /*device*/,
                                                uint32_t* allocated_slot) -> bool {
    *allocated_slot = kSlotIndex;
    return true;
  };

  EXPECT_CALL(*producer, IsCapturing).Times(1).WillOnce(Return(true));
  EXPECT_CALL(timer_query_pool, GetQueryPool).Times(1).WillOnce(Return(query_pool));
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(1)
      .WillOnce(Invoke(mock_next_ready_query_slot_function));
  EXPECT_CALL(dispatch_table, CmdWriteTimestamp)
      .Times(1)
      .WillOnce(Return(mock_write_timestamp_function));

  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);

  EXPECT_TRUE(was_called);
  was_called = false;
}

TEST(SubmissionTracker, ResetCommandBufferShouldRollbackUnsubmittedSlots) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<MockVulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager,
      absl::bit_cast<std::unique_ptr<VulkanLayerProducer>*>(&producer));

  VkDevice device = {};
  VkCommandPool command_pool = {};
  VkCommandBuffer command_buffer = {};
  VkQueryPool query_pool = {};

  // We cannot capture anything in that lambda as we need to cast it to a function pointer.
  constexpr uint32_t kSlotIndex = 32;

  auto mock_next_ready_query_slot_function = [](VkDevice /*device*/,
                                                uint32_t* allocated_slot) -> bool {
    *allocated_slot = kSlotIndex;
    return true;
  };

  EXPECT_CALL(*producer, IsCapturing).Times(1).WillOnce(Return(true));
  EXPECT_CALL(timer_query_pool, GetQueryPool).Times(1).WillOnce(Return(query_pool));
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(1)
      .WillOnce(Invoke(mock_next_ready_query_slot_function));
  std::vector<uint32_t> actual_slots_to_rollback;
  EXPECT_CALL(timer_query_pool, RollbackPendingQuerySlots)
      .Times(1)
      .WillOnce(SaveArg<1>(&actual_slots_to_rollback));
  EXPECT_CALL(dispatch_table, CmdWriteTimestamp)
      .Times(1)
      .WillOnce(Return(dummy_write_timestamp_function));

  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.ResetCommandBuffer(command_buffer);

  EXPECT_THAT(actual_slots_to_rollback, ElementsAre(kSlotIndex));
}

TEST(SubmissionTracker, ResetCommandPoolShouldRollbackUnsubmittedSlots) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<MockVulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager,
      absl::bit_cast<std::unique_ptr<VulkanLayerProducer>*>(&producer));

  VkDevice device = {};
  VkCommandPool command_pool = {};
  VkCommandBuffer command_buffer = {};
  VkQueryPool query_pool = {};

  constexpr uint32_t kSlotIndex = 32;

  auto mock_next_ready_query_slot_function = [](VkDevice /*device*/,
                                                uint32_t* allocated_slot) -> bool {
    *allocated_slot = kSlotIndex;
    return true;
  };

  EXPECT_CALL(*producer, IsCapturing).Times(1).WillOnce(Return(true));
  EXPECT_CALL(timer_query_pool, GetQueryPool).Times(1).WillOnce(Return(query_pool));
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(1)
      .WillOnce(Invoke(mock_next_ready_query_slot_function));
  std::vector<uint32_t> actual_slots_to_rollback;
  EXPECT_CALL(timer_query_pool, RollbackPendingQuerySlots)
      .Times(1)
      .WillOnce(SaveArg<1>(&actual_slots_to_rollback));
  EXPECT_CALL(dispatch_table, CmdWriteTimestamp)
      .Times(1)
      .WillOnce(Return(dummy_write_timestamp_function));

  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.ResetCommandPool(command_pool);

  EXPECT_THAT(actual_slots_to_rollback, ElementsAre(kSlotIndex));
}

TEST(SubmissionTracker, MarkCommandBufferEndWontWriteTimestampsWhenNotCapturing) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<MockVulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager,
      absl::bit_cast<std::unique_ptr<VulkanLayerProducer>*>(&producer));

  VkDevice device = {};
  VkCommandPool command_pool = {};
  VkCommandBuffer command_buffer = {};

  EXPECT_CALL(*producer, IsCapturing).Times(2).WillRepeatedly(Return(false));
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot).Times(0);

  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkCommandBufferEnd(command_buffer);
}

TEST(SubmissionTracker, MarkCommandBufferEndWontWriteTimestampsWhenNotCapturedBegin) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<MockVulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager,
      absl::bit_cast<std::unique_ptr<VulkanLayerProducer>*>(&producer));

  VkDevice device = {};
  VkCommandPool command_pool = {};
  VkCommandBuffer command_buffer = {};

  EXPECT_CALL(*producer, IsCapturing).Times(2).WillOnce(Return(false)).WillOnce(Return(true));
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot).Times(0);

  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkCommandBufferEnd(command_buffer);
}

TEST(SubmissionTracker, CanRetrieveCommandBufferTimestampsForACompleteSubmission) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<MockVulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager,
      absl::bit_cast<std::unique_ptr<VulkanLayerProducer>*>(&producer));

  VkDevice device = {};
  VkPhysicalDevice physical_device = {};
  VkCommandPool command_pool = {};
  VkCommandBuffer command_buffer = {};
  VkQueryPool query_pool = {};
  VkPhysicalDeviceProperties physical_device_properties = {.limits = {.timestampPeriod = 1.f}};
  VkQueue queue = {};
  VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                              .pNext = nullptr,
                              .pCommandBuffers = &command_buffer,
                              .commandBufferCount = 1};

  constexpr uint32_t kBeginSlot = 32;
  constexpr uint32_t kEndSlot = 33;
  auto mock_next_ready_query_slot_function_1 = [](VkDevice /*device*/,
                                                  uint32_t* allocated_slot) -> bool {
    *allocated_slot = kBeginSlot;
    return true;
  };
  auto mock_next_ready_query_slot_function_2 = [](VkDevice /*device*/,
                                                  uint32_t* allocated_slot) -> bool {
    *allocated_slot = kEndSlot;
    return true;
  };
  constexpr uint64_t kBeginGpuTimestamp = 1;
  constexpr uint64_t kEndGpuTimestamp = 2;
  PFN_vkGetQueryPoolResults mock_get_query_pool_results_function =
      +[](VkDevice /*device*/, VkQueryPool /*queryPool*/, uint32_t first_query,
          uint32_t query_count, size_t /*dataSize*/, void* data, VkDeviceSize /*stride*/,
          VkQueryResultFlags flags) -> VkResult {
    EXPECT_EQ(query_count, 1);
    EXPECT_NE((flags & VK_QUERY_RESULT_64_BIT), 0);
    EXPECT_TRUE(first_query == kBeginSlot || first_query == kEndSlot);
    if (first_query == kBeginSlot) {
      *absl::bit_cast<uint64_t*>(data) = kBeginGpuTimestamp;
    } else {
      *absl::bit_cast<uint64_t*>(data) = kEndGpuTimestamp;
    }
    return VK_SUCCESS;
  };

  EXPECT_CALL(*producer, IsCapturing).WillRepeatedly(Return(true));
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(2)
      .WillOnce(Invoke(mock_next_ready_query_slot_function_1))
      .WillOnce(Invoke(mock_next_ready_query_slot_function_2));
  EXPECT_CALL(timer_query_pool, GetQueryPool).WillRepeatedly(Return(query_pool));
  EXPECT_CALL(dispatch_table, CmdWriteTimestamp)
      .WillRepeatedly(Return(dummy_write_timestamp_function));
  EXPECT_CALL(dispatch_table, GetQueryPoolResults)
      .WillRepeatedly(Return(mock_get_query_pool_results_function));
  EXPECT_CALL(device_manager, GetPhysicalDeviceOfLogicalDevice)
      .WillRepeatedly(Return(physical_device));
  EXPECT_CALL(device_manager, GetPhysicalDeviceProperties)
      .WillRepeatedly(Return(physical_device_properties));
  std::vector<uint32_t> actual_reset_slots;
  EXPECT_CALL(timer_query_pool, ResetQuerySlots).Times(1).WillOnce(SaveArg<1>(&actual_reset_slots));
  orbit_grpc_protos::CaptureEvent actual_capture_event;
  auto mock_enqueue_capture_event =
      [&actual_capture_event](orbit_grpc_protos::CaptureEvent&& capture_event) {
        actual_capture_event = std::move(capture_event);
      };
  EXPECT_CALL(*producer, EnqueueCaptureEvent).Times(1).WillOnce(Invoke(mock_enqueue_capture_event));

  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkCommandBufferEnd(command_buffer);
  pid_t pid = GetCurrentThreadId();
  uint64_t pre_submit_time = MonotonicTimestampNs();
  tracker.PersistSubmitInformation(queue, 1, &submit_info);
  tracker.DoPostSubmitQueue(queue, 1, &submit_info);
  uint64_t post_submit_time = MonotonicTimestampNs();
  tracker.CompleteSubmits(device);

  EXPECT_THAT(actual_reset_slots, UnorderedElementsAre(kBeginSlot, kEndSlot));
  EXPECT_TRUE(actual_capture_event.has_gpu_queue_submission());
  const orbit_grpc_protos::GpuQueueSubmission& actual_queue_submission =
      actual_capture_event.gpu_queue_submission();

  EXPECT_LE(pre_submit_time, actual_queue_submission.meta_info().pre_submission_cpu_timestamp());
  EXPECT_LE(actual_queue_submission.meta_info().pre_submission_cpu_timestamp(),
            actual_queue_submission.meta_info().post_submission_cpu_timestamp());
  EXPECT_LE(actual_queue_submission.meta_info().post_submission_cpu_timestamp(), post_submit_time);
  EXPECT_EQ(pid, actual_queue_submission.meta_info().tid());

  ASSERT_EQ(actual_queue_submission.submit_infos_size(), 1);
  const orbit_grpc_protos::GpuSubmitInfo& actual_submit_info =
      actual_queue_submission.submit_infos(0);

  ASSERT_EQ(actual_submit_info.command_buffers_size(), 1);
  const orbit_grpc_protos::GpuCommandBuffer& actual_command_buffer =
      actual_submit_info.command_buffers(0);

  EXPECT_EQ(kBeginGpuTimestamp, actual_command_buffer.begin_gpu_timestamp_ns());
  EXPECT_EQ(kEndGpuTimestamp, actual_command_buffer.end_gpu_timestamp_ns());
}

TEST(SubmissionTracker, CanRetrieveCommandBufferTimestampsForACompleteSubmissionAtSecondPresent) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<MockVulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager,
      absl::bit_cast<std::unique_ptr<VulkanLayerProducer>*>(&producer));

  VkDevice device = {};
  VkPhysicalDevice physical_device = {};
  VkCommandPool command_pool = {};
  VkCommandBuffer command_buffer = {};
  VkQueryPool query_pool = {};
  VkPhysicalDeviceProperties physical_device_properties = {.limits = {.timestampPeriod = 1.f}};
  VkQueue queue = {};
  VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                              .pNext = nullptr,
                              .pCommandBuffers = &command_buffer,
                              .commandBufferCount = 1};

  constexpr uint32_t kBeginSlot = 32;
  constexpr uint32_t kEndSlot = 33;
  auto mock_next_ready_query_slot_function_1 = [](VkDevice /*device*/,
                                                  uint32_t* allocated_slot) -> bool {
    *allocated_slot = kBeginSlot;
    return true;
  };
  auto mock_next_ready_query_slot_function_2 = [](VkDevice /*device*/,
                                                  uint32_t* allocated_slot) -> bool {
    *allocated_slot = kEndSlot;
    return true;
  };
  constexpr uint64_t kBeginGpuTimestamp = 1;
  constexpr uint64_t kEndGpuTimestamp = 2;
  PFN_vkGetQueryPoolResults mock_get_query_pool_results_function_ready =
      +[](VkDevice /*device*/, VkQueryPool /*queryPool*/, uint32_t first_query,
          uint32_t query_count, size_t /*dataSize*/, void* data, VkDeviceSize /*stride*/,
          VkQueryResultFlags flags) -> VkResult {
    EXPECT_EQ(query_count, 1);
    EXPECT_NE((flags & VK_QUERY_RESULT_64_BIT), 0);
    EXPECT_TRUE(first_query == kBeginSlot || first_query == kEndSlot);
    if (first_query == kBeginSlot) {
      *absl::bit_cast<uint64_t*>(data) = kBeginGpuTimestamp;
    } else {
      *absl::bit_cast<uint64_t*>(data) = kEndGpuTimestamp;
    }
    return VK_SUCCESS;
  };
  PFN_vkGetQueryPoolResults mock_get_query_pool_results_function_not_ready =
      +[](VkDevice /*device*/, VkQueryPool /*queryPool*/, uint32_t /*first_query*/,
          uint32_t /*query_count*/, size_t /*dataSize*/, void* /*data*/, VkDeviceSize /*stride*/,
          VkQueryResultFlags /*flags*/) -> VkResult { return VK_NOT_READY; };

  EXPECT_CALL(*producer, IsCapturing).WillRepeatedly(Return(true));
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(2)
      .WillOnce(Invoke(mock_next_ready_query_slot_function_1))
      .WillOnce(Invoke(mock_next_ready_query_slot_function_2));
  EXPECT_CALL(timer_query_pool, GetQueryPool).WillRepeatedly(Return(query_pool));
  EXPECT_CALL(dispatch_table, CmdWriteTimestamp)
      .WillRepeatedly(Return(dummy_write_timestamp_function));
  EXPECT_CALL(dispatch_table, GetQueryPoolResults)
      .WillOnce(Return(mock_get_query_pool_results_function_not_ready))
      .WillRepeatedly(Return(mock_get_query_pool_results_function_ready));
  EXPECT_CALL(device_manager, GetPhysicalDeviceOfLogicalDevice)
      .WillRepeatedly(Return(physical_device));
  EXPECT_CALL(device_manager, GetPhysicalDeviceProperties)
      .WillRepeatedly(Return(physical_device_properties));
  std::vector<uint32_t> actual_reset_slots;
  EXPECT_CALL(timer_query_pool, ResetQuerySlots).Times(1).WillOnce(SaveArg<1>(&actual_reset_slots));
  orbit_grpc_protos::CaptureEvent actual_capture_event;
  auto mock_enqueue_capture_event =
      [&actual_capture_event](orbit_grpc_protos::CaptureEvent&& capture_event) {
        actual_capture_event = std::move(capture_event);
      };
  EXPECT_CALL(*producer, EnqueueCaptureEvent).Times(1).WillOnce(Invoke(mock_enqueue_capture_event));

  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkCommandBufferEnd(command_buffer);
  pid_t pid = GetCurrentThreadId();
  uint64_t pre_submit_time = MonotonicTimestampNs();
  tracker.PersistSubmitInformation(queue, 1, &submit_info);
  tracker.DoPostSubmitQueue(queue, 1, &submit_info);
  uint64_t post_submit_time = MonotonicTimestampNs();
  tracker.CompleteSubmits(device);
  tracker.CompleteSubmits(device);

  EXPECT_THAT(actual_reset_slots, UnorderedElementsAre(kBeginSlot, kEndSlot));
  EXPECT_TRUE(actual_capture_event.has_gpu_queue_submission());
  const orbit_grpc_protos::GpuQueueSubmission& actual_queue_submission =
      actual_capture_event.gpu_queue_submission();

  EXPECT_LE(pre_submit_time, actual_queue_submission.meta_info().pre_submission_cpu_timestamp());
  EXPECT_LE(actual_queue_submission.meta_info().pre_submission_cpu_timestamp(),
            actual_queue_submission.meta_info().post_submission_cpu_timestamp());
  EXPECT_LE(actual_queue_submission.meta_info().post_submission_cpu_timestamp(), post_submit_time);
  EXPECT_EQ(pid, actual_queue_submission.meta_info().tid());

  ASSERT_EQ(actual_queue_submission.submit_infos_size(), 1);
  const orbit_grpc_protos::GpuSubmitInfo& actual_submit_info =
      actual_queue_submission.submit_infos(0);

  ASSERT_EQ(actual_submit_info.command_buffers_size(), 1);
  const orbit_grpc_protos::GpuCommandBuffer& actual_command_buffer =
      actual_submit_info.command_buffers(0);

  EXPECT_EQ(kBeginGpuTimestamp, actual_command_buffer.begin_gpu_timestamp_ns());
  EXPECT_EQ(kEndGpuTimestamp, actual_command_buffer.end_gpu_timestamp_ns());
}

TEST(SubmissionTracker, StopCaptureBeforeSubmissionWillResetTheSlots) {
  MockDispatchTable dispatch_table;
  MockTimerQueryPool timer_query_pool;
  MockDeviceManager device_manager;
  std::unique_ptr<MockVulkanLayerProducer> producer = std::make_unique<MockVulkanLayerProducer>();

  SubmissionTracker<MockDispatchTable, MockDeviceManager, MockTimerQueryPool> tracker(
      0, &dispatch_table, &timer_query_pool, &device_manager,
      absl::bit_cast<std::unique_ptr<VulkanLayerProducer>*>(&producer));

  VkDevice device = {};
  VkCommandPool command_pool = {};
  VkCommandBuffer command_buffer = {};
  VkQueryPool query_pool = {};
  VkQueue queue = {};
  VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                              .pNext = nullptr,
                              .pCommandBuffers = &command_buffer,
                              .commandBufferCount = 1};

  constexpr uint32_t kBeginSlot = 32;
  constexpr uint32_t kEndSlot = 33;
  auto mock_next_ready_query_slot_function_1 = [](VkDevice /*device*/,
                                                  uint32_t* allocated_slot) -> bool {
    *allocated_slot = kBeginSlot;
    return true;
  };
  auto mock_next_ready_query_slot_function_2 = [](VkDevice /*device*/,
                                                  uint32_t* allocated_slot) -> bool {
    *allocated_slot = kEndSlot;
    return true;
  };

  EXPECT_CALL(*producer, IsCapturing)
      .Times(4)
      .WillOnce(Return(true))
      .WillOnce(Return(true))
      .WillOnce(Return(false))
      .WillOnce(Return(false));
  EXPECT_CALL(timer_query_pool, NextReadyQuerySlot)
      .Times(2)
      .WillOnce(Invoke(mock_next_ready_query_slot_function_1))
      .WillOnce(Invoke(mock_next_ready_query_slot_function_2));
  EXPECT_CALL(timer_query_pool, GetQueryPool).WillRepeatedly(Return(query_pool));
  EXPECT_CALL(dispatch_table, CmdWriteTimestamp)
      .WillRepeatedly(Return(dummy_write_timestamp_function));
  EXPECT_CALL(dispatch_table, GetQueryPoolResults).Times(0);
  std::vector<uint32_t> actual_reset_slots;
  EXPECT_CALL(timer_query_pool, ResetQuerySlots).Times(1).WillOnce(SaveArg<1>(&actual_reset_slots));

  EXPECT_CALL(*producer, EnqueueCaptureEvent).Times(0);

  tracker.TrackCommandBuffers(device, command_pool, &command_buffer, 1);
  tracker.MarkCommandBufferBegin(command_buffer);
  tracker.MarkCommandBufferEnd(command_buffer);
  tracker.PersistSubmitInformation(queue, 1, &submit_info);
  tracker.DoPostSubmitQueue(queue, 1, &submit_info);
  tracker.CompleteSubmits(device);

  EXPECT_THAT(actual_reset_slots, UnorderedElementsAre(kBeginSlot, kEndSlot));
}

// TODO: stop capturing after submission but before present -> reset slot + get result
// TODO: stop capturing between pre/post submission
// TODO: debug marker begin
// TODO: debug marker end
// TODO: nested debug markers
// TODO: debug markers across submissions
// TODO: debug markers across submissions, miss begin marker (not capturing)
// TODO: debug markers across submissions, miss end marker (not capturing)
// TODO: debug markers, stop capturing before submission
// TODO: debug markers limited by depth
// TODO: Reuse command buffer without reset will fail

}  // namespace orbit_vulkan_layer