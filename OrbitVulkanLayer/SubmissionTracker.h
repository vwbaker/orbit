// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_VULKAN_LAYER_COMMAND_BUFFER_MANAGER_H_
#define ORBIT_VULKAN_LAYER_COMMAND_BUFFER_MANAGER_H_

#include <stack>

#include "DeviceManager.h"
#include "DispatchTable.h"
#include "OrbitBase/Logging.h"
#include "TimerQueryPool.h"
#include "VulkanLayerProducer.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"
#include "vulkan/vulkan.h"

namespace orbit_vulkan_layer {

namespace internal {
enum MarkerType { kDebugMarkerBegin = 0, kDebugMarkerEnd };

struct Color {
  // Values are all in range [0.f, 1.f.]
  float red;
  float green;
  float blue;
  float alpha;
};

struct SubmissionMetaInformation {
  uint64_t pre_submission_cpu_timestamp;
  uint64_t post_submission_cpu_timestamp;
  int32_t thread_id;
};

struct Marker {
  MarkerType type;
  std::optional<uint32_t> slot_index;
  std::string text;
  Color color;
};

struct SubmittedMarker {
  SubmissionMetaInformation meta_information;
  uint32_t slot_index;
};

struct MarkerState {
  std::optional<SubmittedMarker> begin_info;
  std::optional<SubmittedMarker> end_info;
  std::string text;
  Color color;
  size_t depth;
};

struct QueueMarkerState {
  std::stack<MarkerState> marker_stack;
};

struct CommandBufferState {
  std::optional<uint32_t> command_buffer_begin_slot_index;
  std::optional<uint32_t> command_buffer_end_slot_index;
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
 * This class ultimately is responsible to track command buffer and debug marker timings.
 * To do so, it keeps tracks of command-buffer allocations, destruction, begins, ends as well as
 * submissions.
 * On `VkBeginCommandBuffer` and `VkEndCommandBuffer` it can (if capturing) insert write timestamp
 * commands (`VkCmdWriteTimestamp`). The same is done for debug marker begins and ends. All that
 * data will be gathered together at a queue submission (`VkQueueSubmit`).
 *
 * Upon every `VkQueuePresentKHR` it will check if the timestamps of a certain submission are
 * already available, and if so, it will send the results over to the `VulkanLayerProducer`.
 *
 * See also `DispatchTable` (for vulkan dispatch), `TimerQueryPool` (to manage the timestamp slots),
 * and `DeviceManager` (to retrieve device properties).
 *
 * Thread-Safety: This class is internally synchronized (using read/write locks), and can be
 * safely accessed from different threads.
 */
class SubmissionTracker {
 public:
  explicit SubmissionTracker(DispatchTable* dispatch_table,
                             TimerQueryPool<DispatchTable>* timer_query_pool,
                             DeviceManager<DispatchTable>* device_manager,
                             std::unique_ptr<VulkanLayerProducer>* vulkan_layer_producer)
      : dispatch_table_(dispatch_table),
        timer_query_pool_(timer_query_pool),
        device_manager_(device_manager),
        vulkan_layer_producer_{vulkan_layer_producer} {
    CHECK(vulkan_layer_producer_ != nullptr);
  }
  void TrackCommandBuffers(VkDevice device, VkCommandPool pool,
                           const VkCommandBuffer* command_buffers, uint32_t count);
  void UntrackCommandBuffers(VkDevice device, VkCommandPool pool,
                             const VkCommandBuffer* command_buffers, uint32_t count);

  void MarkCommandBufferBegin(VkCommandBuffer command_buffer);

  void MarkCommandBufferEnd(VkCommandBuffer command_buffer);

  void MarkDebugMarkerBegin(VkCommandBuffer command_buffer, const char* text,
                            internal::Color color);
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
  TimerQueryPool<DispatchTable>* timer_query_pool_;
  DeviceManager<DispatchTable>* device_manager_;

  [[nodiscard]] bool IsCapturing() {
    return *vulkan_layer_producer_ != nullptr && (*vulkan_layer_producer_)->IsCapturing();
  }
  std::unique_ptr<VulkanLayerProducer>* vulkan_layer_producer_;
};

}  // namespace orbit_vulkan_layer

#endif  // ORBIT_VULKAN_LAYER_COMMAND_BUFFER_MANAGER_H_