// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_VULKAN_LAYER_COMMAND_BUFFER_MANAGER_H_
#define ORBIT_VULKAN_LAYER_COMMAND_BUFFER_MANAGER_H_

#include <stack>

#include "DispatchTable.h"
#include "OrbitBase/Logging.h"
#include "PhysicalDeviceManager.h"
#include "TimerQueryPool.h"
#include "VulkanLayerProducer.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"
#include "vulkan/vulkan.h"

namespace orbit_vulkan_layer {

namespace internal {
enum MarkerType { kDebugMarkerBegin = 0, kDebugMarkerEnd };

struct SubmissionMetaInformation {
  uint64_t pre_submission_cpu_timestamp;
  uint64_t post_submission_cpu_timestamp;
  int32_t thread_id;
};

struct Marker {
  MarkerType type;
  std::optional<uint32_t> slot_index;
  std::string text;
};

struct SubmittedMarker {
  SubmissionMetaInformation meta_information;
  uint32_t slot_index;
};

struct MarkerState {
  std::optional<SubmittedMarker> begin_info;
  std::optional<SubmittedMarker> end_info;
  std::string text;
  size_t depth;
};

struct QueueMarkerState {
  std::stack<MarkerState> marker_stack;
};

struct CommandBufferState {
  std::optional<uint32_t> command_buffer_begin_slot_index;
  std::optional<uint32_t> command_buffer_end_slot_index = std::nullopt;
  std::vector<Marker> markers;
};

struct SubmittedCommandBuffer {
  uint32_t command_buffer_begin_slot_index;
  uint32_t command_buffer_end_slot_index;
};

struct SubmitInfo {
  std::vector<SubmittedCommandBuffer> command_buffers;
};

struct QueueSubmission {
  SubmissionMetaInformation meta_information;
  std::vector<SubmitInfo> submit_infos;
  std::vector<MarkerState> completed_markers;
  uint32_t num_begin_markers = 0;
};
}  // namespace internal

/*
 * This class is responsible to track command buffers and command pools.
 * TODO: So far it only tracks the allocation/de-allocation of buffers and pools.
 *  It should probably also track the timestamps inside the buffers (and the markers?)
 *
 * It also tracks which command buffer belongs to which device, which can be used
 * in the `DispatchTable` for function look-up.
 *
 * Thread-Safety: This class is internally synchronized (using read/write locks), and can be
 * safely accessed from different threads.
 */
class CommandBufferManager {
 public:
  explicit CommandBufferManager(DispatchTable* dispatch_table, TimerQueryPool* timer_query_pool,
                                PhysicalDeviceManager<DispatchTable>* physical_device_manager,
                                std::optional<VulkanLayerProducer>* vulkan_layer_producer)
      : dispatch_table_(dispatch_table),
        timer_query_pool_(timer_query_pool),
        physical_device_manager_(physical_device_manager),
        vulkan_layer_producer_{vulkan_layer_producer} {}
  void TrackCommandBuffers(VkDevice device, VkCommandPool pool,
                           const VkCommandBuffer* command_buffers, uint32_t count);
  void UntrackCommandBuffers(VkDevice device, VkCommandPool pool,
                             const VkCommandBuffer* command_buffers, uint32_t count);

  void MarkCommandBufferBegin(VkCommandBuffer command_buffer);

  void MarkCommandBufferEnd(VkCommandBuffer command_buffer);

  void MarkDebugMarkerBegin(VkCommandBuffer command_buffer, const char* text);
  void MarkDebugMarkerEnd(VkCommandBuffer command_buffer);

  void PersistSubmitInformation(VkQueue queue, uint32_t submit_count, const VkSubmitInfo* submits);
  void DoPostSubmitQueue(VkQueue queue, uint32_t submit_count, const VkSubmitInfo* submits);

  void CompleteSubmits(VkDevice device);

  void ResetCommandBuffer(VkCommandBuffer command_buffer);

  void ResetCommandPool(VkCommandPool command_pool);

 private:
  uint32_t RecordTimestamp(VkCommandBuffer command_buffer,
                           VkPipelineStageFlagBits pipeline_stage_flags);

  std::vector<internal::QueueSubmission> PullCompletedSubmissions(VkDevice device,
                                                                  VkQueryPool query_pool);

  uint64_t QueryGpuTimestampNs(VkDevice device, VkQueryPool query_pool, uint32_t slot_index,
                               float timestamp_period);

  static void WriteMetaInfo(const internal::SubmissionMetaInformation& meta_info,
                            orbit_grpc_protos::GpuQueueSubmissionMetaInfo* target_proto);

  absl::Mutex mutex_;
  absl::flat_hash_map<VkCommandPool, absl::flat_hash_set<VkCommandBuffer>> pool_to_command_buffers_;
  absl::flat_hash_map<VkCommandBuffer, VkDevice> command_buffer_to_device_;

  absl::flat_hash_map<VkCommandBuffer, internal::CommandBufferState> command_buffer_to_state_;
  absl::flat_hash_map<VkQueue, std::vector<internal::QueueSubmission>> queue_to_submissions_;
  absl::flat_hash_map<VkQueue, internal::QueueMarkerState> queue_to_markers_;

  DispatchTable* dispatch_table_;
  TimerQueryPool* timer_query_pool_;
  PhysicalDeviceManager<DispatchTable>* physical_device_manager_;

  [[nodiscard]] bool IsCapturing() {
    return vulkan_layer_producer_->has_value() && (*vulkan_layer_producer_)->IsCapturing();
  }
  std::optional<VulkanLayerProducer>* vulkan_layer_producer_;
};

}  // namespace orbit_vulkan_layer

#endif  // ORBIT_VULKAN_LAYER_COMMAND_BUFFER_MANAGER_H_