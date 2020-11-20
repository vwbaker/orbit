// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "CommandBufferManager.h"

#include "OrbitBase/Logging.h"
#include "OrbitBase/Profiling.h"

namespace orbit_vulkan_layer {

void CommandBufferManager::TrackCommandBuffers(VkDevice device, VkCommandPool pool,
                                               const VkCommandBuffer* command_buffers,
                                               uint32_t count) {
  LOG("TrackCommandBuffers");
  absl::WriterMutexLock lock(&mutex_);
  if (!pool_to_command_buffers_.contains(pool)) {
    pool_to_command_buffers_[pool] = {};
  }
  absl::flat_hash_set<VkCommandBuffer>& associated_cbs = pool_to_command_buffers_.at(pool);
  for (uint32_t i = 0; i < count; ++i) {
    VkCommandBuffer cb = command_buffers[i];
    CHECK(cb != VK_NULL_HANDLE);
    associated_cbs.insert(cb);
    command_buffer_to_device_[cb] = device;
  }
}

void CommandBufferManager::UntrackCommandBuffers(VkDevice device, VkCommandPool pool,
                                                 const VkCommandBuffer* command_buffers,
                                                 uint32_t count) {
  LOG("UntrackCommandBuffers");
  absl::WriterMutexLock lock(&mutex_);
  absl::flat_hash_set<VkCommandBuffer>& associated_command_buffers =
      pool_to_command_buffers_.at(pool);
  for (uint32_t i = 0; i < count; ++i) {
    VkCommandBuffer command_buffer = command_buffers[i];
    CHECK(command_buffer != VK_NULL_HANDLE);
    associated_command_buffers.erase(command_buffer);
    CHECK(command_buffer_to_device_.contains(command_buffer));
    CHECK(command_buffer_to_device_.at(command_buffer) == device);
    command_buffer_to_device_.erase(command_buffer);
  }
  if (associated_command_buffers.empty()) {
    pool_to_command_buffers_.erase(pool);
  }
}

void CommandBufferManager::MarkCommandBufferBegin(VkCommandBuffer command_buffer) {
  LOG("MarkCommandBufferBegin");
  // Even when we are not capturing we create state for this command buffer to allow the
  // debug marker tracking.
  {
    absl::WriterMutexLock lock(&mutex_);
    CHECK(!command_buffer_to_state_.contains(command_buffer));
    command_buffer_to_state_[command_buffer] = {};
  }
  if (!IsCapturing()) {
    return;
  }

  uint32_t slot_index = RecordTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
  {
    absl::WriterMutexLock lock(&mutex_);
    command_buffer_to_state_.at(command_buffer).command_buffer_begin_slot_index =
        std::make_optional(slot_index);
  }
}

void CommandBufferManager::MarkCommandBufferEnd(VkCommandBuffer command_buffer) {
  LOG("MarkCommandBufferEnd");
  if (!IsCapturing()) {
    return;
  }
  {
    absl::ReaderMutexLock lock(&mutex_);
    CHECK(command_buffer_to_state_.contains(command_buffer));
    internal::CommandBufferState& command_buffer_state =
        command_buffer_to_state_.at(command_buffer);
    if (!command_buffer_state.command_buffer_begin_slot_index.has_value()) {
      return;
    }
  }

  // RecordTimestamp will also acquire a reader lock. As we are fine with having multiple readers
  // we don't need to unlock here.
  uint32_t slot_index = RecordTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

  {
    absl::ReaderMutexLock lock(&mutex_);
    CHECK(command_buffer_to_state_.contains(command_buffer));
    internal::CommandBufferState& command_buffer_state =
        command_buffer_to_state_.at(command_buffer);
    // Writing to this field is safe, as there can't be any operation on this command buffer
    // in parallel.
    command_buffer_state.command_buffer_end_slot_index = std::make_optional(slot_index);
  }
}

void CommandBufferManager::MarkDebugMarkerBegin(VkCommandBuffer command_buffer, const char* text) {
  LOG("MarkDebugMarkerBegin");
  CHECK(text != nullptr);
  {
    absl::WriterMutexLock lock(&mutex_);
    CHECK(command_buffer_to_state_.contains(command_buffer));
    internal::CommandBufferState& state = command_buffer_to_state_.at(command_buffer);
    internal::Marker marker{.type = internal::MarkerType::kDebugMarkerBegin,
                            .text = std::string(text)};
    state.markers.emplace_back(std::move(marker));
  }

  if (!IsCapturing()) {
    return;
  }

  uint32_t slot_index = RecordTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

  {
    absl::WriterMutexLock lock(&mutex_);
    CHECK(command_buffer_to_state_.contains(command_buffer));
    internal::CommandBufferState& state = command_buffer_to_state_.at(command_buffer);
    state.markers.back().slot_index = std::make_optional(slot_index);
  }
}

void CommandBufferManager::MarkDebugMarkerEnd(VkCommandBuffer command_buffer) {
  {
    absl::WriterMutexLock lock(&mutex_);
    CHECK(command_buffer_to_state_.contains(command_buffer));
    internal::CommandBufferState& state = command_buffer_to_state_.at(command_buffer);
    internal::Marker marker{.type = internal::MarkerType::kDebugMarkerEnd};
    state.markers.emplace_back(std::move(marker));
  }

  if (!IsCapturing()) {
    return;
  }

  uint32_t slot_index = RecordTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  {
    absl::WriterMutexLock lock(&mutex_);
    CHECK(command_buffer_to_state_.contains(command_buffer));
    internal::CommandBufferState& state = command_buffer_to_state_.at(command_buffer);
    state.markers.back().slot_index = std::make_optional(slot_index);
  }
}

// After command buffers are submitted into a queue, they can be reused for further operations.
// Thus, our identification via the pointers become invalid. We will use the vkQueueSubmit
// to make our data persistent until we have processed the results of the execution of these
// command buffers (which will be done in the vkQueuePresentKHR).
void CommandBufferManager::PersistSubmitInformation(VkQueue queue, uint32_t submit_count,
                                                    const VkSubmitInfo* submits) {
  LOG("PersistSubmitInformation");
  if (!IsCapturing()) {
    return;
  }

  internal::QueueSubmission queue_submission = {};

  absl::WriterMutexLock lock(&mutex_);
  for (uint32_t submit_index = 0; submit_index < submit_count; ++submit_index) {
    VkSubmitInfo submit_info = submits[submit_index];
    queue_submission.submit_infos.emplace_back();
    internal::SubmitInfo& submitted_submit_info = queue_submission.submit_infos.back();
    for (uint32_t command_buffer_index = 0; command_buffer_index < submit_info.commandBufferCount;
         ++command_buffer_index) {
      VkCommandBuffer command_buffer = submit_info.pCommandBuffers[command_buffer_index];
      CHECK(command_buffer_to_state_.contains(command_buffer));
      internal::CommandBufferState& state = command_buffer_to_state_.at(command_buffer);
      if (!state.command_buffer_begin_slot_index.has_value()) {
        continue;
      }
      CHECK(state.command_buffer_end_slot_index.has_value());
      internal::SubmittedCommandBuffer submitted_command_buffer{
          .command_buffer_begin_slot_index = state.command_buffer_begin_slot_index.value(),
          .command_buffer_end_slot_index = state.command_buffer_end_slot_index.value()};
      submitted_submit_info.command_buffers.emplace_back(submitted_command_buffer);
    }
  }
  queue_submission.meta_information.thread_id = GetCurrentThreadId();
  queue_submission.meta_information.pre_submission_cpu_timestamp = MonotonicTimestampNs();

  if (!queue_to_submissions_.contains(queue)) {
    queue_to_submissions_[queue] = {};
  }
  queue_to_submissions_.at(queue).emplace_back(std::move(queue_submission));
}

// Take a timestamp before and after the execution of the driver code for the submission.
// This allows us to map submissions from the vulkan layer to the driver submissions.
void CommandBufferManager::DoPostSubmitQueue(VkQueue queue, uint32_t submit_count,
                                             const VkSubmitInfo* submits) {
  LOG("PersistSubmitInformation");
  {
    absl::WriterMutexLock lock(&mutex_);
    if (!queue_to_markers_.contains(queue)) {
      queue_to_markers_[queue] = {};
    }
    CHECK(queue_to_markers_.contains(queue));
    internal::QueueMarkerState& markers = queue_to_markers_.at(queue);
    internal::QueueSubmission* queue_submission = nullptr;

    // TODO: What happens if we start capturing right between the pre- and post-submission.
    if (queue_to_submissions_.contains(queue) && IsCapturing()) {
      queue_submission = &queue_to_submissions_.at(queue).back();
      queue_submission->meta_information.post_submission_cpu_timestamp = MonotonicTimestampNs();
    }

    for (uint32_t submit_index = 0; submit_index < submit_count; ++submit_index) {
      VkSubmitInfo submit_info = submits[submit_index];
      for (uint32_t command_buffer_index = 0; command_buffer_index < submit_info.commandBufferCount;
           ++command_buffer_index) {
        VkCommandBuffer command_buffer = submit_info.pCommandBuffers[command_buffer_index];
        CHECK(command_buffer_to_state_.contains(command_buffer));
        for (const internal::Marker& marker : command_buffer_to_state_.at(command_buffer).markers) {
          std::optional<internal::SubmittedMarker> submitted_marker = std::nullopt;
          if (queue_submission != nullptr && marker.slot_index.has_value()) {
            submitted_marker = {.meta_information = queue_submission->meta_information,
                                .slot_index = marker.slot_index.value()};
          }

          switch (marker.type) {
            case internal::MarkerType::kDebugMarkerBegin: {
              if (queue_submission != nullptr && marker.slot_index.has_value()) {
                ++queue_submission->num_begin_markers;
              }
              internal::MarkerState marker_state{.text = marker.text,
                                                 .begin_info = submitted_marker,
                                                 .depth = markers.marker_stack.size()};
              markers.marker_stack.push(std::move(marker_state));
              break;
            }

            case internal::MarkerType::kDebugMarkerEnd: {
              internal::MarkerState marker_state = markers.marker_stack.top();
              markers.marker_stack.pop();
              if (queue_submission != nullptr && marker.slot_index.has_value()) {
                marker_state.end_info = submitted_marker;
                queue_submission->completed_markers.emplace_back(std::move(marker_state));
              }

              break;
            }
          }
        }
        command_buffer_to_state_.erase(command_buffer);
      }
    }
  }
}

void CommandBufferManager::CompleteSubmits(VkDevice device) {
  LOG("CompleteSubmits");
  VkQueryPool query_pool = timer_query_pool_->GetQueryPool(device);
  std::vector<internal::QueueSubmission> completed_submissions =
      PullCompletedSubmissions(device, query_pool);

  if (completed_submissions.empty()) {
    return;
  }

  VkPhysicalDevice physical_device = device_manager_->GetPhysicalDeviceOfLogicalDevice(device);
  const float timestamp_period =
      device_manager_->GetPhysicalDeviceProperties(physical_device).limits.timestampPeriod;

  std::vector<uint32_t> query_slots_to_reset;
  for (const auto& completed_submission : completed_submissions) {
    orbit_grpc_protos::CaptureEvent capture_event;
    orbit_grpc_protos::GpuQueueSubmission* submission_proto =
        capture_event.mutable_gpu_queue_submission();
    WriteMetaInfo(completed_submission.meta_information, submission_proto->mutable_meta_info());

    // Now for the command buffer timings:
    for (const auto& completed_submit : completed_submission.submit_infos) {
      orbit_grpc_protos::GpuSubmitInfo* submit_info_proto = submission_proto->add_submit_infos();
      for (const auto& completed_command_buffer : completed_submit.command_buffers) {
        orbit_grpc_protos::GpuCommandBuffer* command_buffer_proto =
            submit_info_proto->add_command_buffers();

        uint64_t begin_timestamp = QueryGpuTimestampNs(
            device, query_pool, completed_command_buffer.command_buffer_begin_slot_index,
            timestamp_period);

        uint64_t end_timestamp = QueryGpuTimestampNs(
            device, query_pool, completed_command_buffer.command_buffer_end_slot_index,
            timestamp_period);

        command_buffer_proto->set_begin_gpu_timestamp_ns(begin_timestamp);
        command_buffer_proto->set_end_gpu_timestamp_ns(end_timestamp);
        query_slots_to_reset.push_back(completed_command_buffer.command_buffer_begin_slot_index);
        query_slots_to_reset.push_back(completed_command_buffer.command_buffer_end_slot_index);
      }
    }

    // Now for the debug markers:
    submission_proto->set_num_begin_markers(completed_submission.num_begin_markers);
    for (const auto& marker_state : completed_submission.completed_markers) {
      uint64_t end_timestamp = QueryGpuTimestampNs(
          device, query_pool, marker_state.end_info->slot_index, timestamp_period);
      query_slots_to_reset.push_back(marker_state.end_info->slot_index);

      orbit_grpc_protos::GpuDebugMarker* marker_proto = submission_proto->add_completed_markers();
      marker_proto->set_text_key(
          (*vulkan_layer_producer_)->InternStringIfNecessaryAndGetKey(marker_state.text));
      marker_proto->set_depth(marker_state.depth);
      marker_proto->set_end_gpu_timestamp_ns(end_timestamp);

      // If we haven't captured the begin marker, we'll leave the optional begin_marker empty.
      if (!marker_state.begin_info.has_value()) {
        continue;
      }
      orbit_grpc_protos::GpuDebugMarkerBeginInfo* begin_debug_marker_proto =
          marker_proto->mutable_begin_marker();
      WriteMetaInfo(marker_state.begin_info->meta_information,
                    begin_debug_marker_proto->mutable_meta_info());

      uint64_t begin_timestamp = QueryGpuTimestampNs(
          device, query_pool, marker_state.begin_info->slot_index, timestamp_period);
      query_slots_to_reset.push_back(marker_state.begin_info->slot_index);

      begin_debug_marker_proto->set_gpu_timestamp_ns(begin_timestamp);
    }

    (*vulkan_layer_producer_)->EnqueueIntermediateEvent(std::move(capture_event));
  }

  timer_query_pool_->ResetQuerySlots(device, query_slots_to_reset, false);
}

std::vector<internal::QueueSubmission> CommandBufferManager::PullCompletedSubmissions(
    VkDevice device, VkQueryPool query_pool) {
  std::vector<internal::QueueSubmission> completed_submissions;

  absl::WriterMutexLock lock(&mutex_);
  for (auto& [unused_queue, queue_submissions] : queue_to_submissions_) {
    auto submission_it = queue_submissions.begin();
    while (submission_it != queue_submissions.end()) {
      const internal::QueueSubmission& submission = *submission_it;
      if (submission.submit_infos.empty()) {
        submission_it = queue_submissions.erase(submission_it);
        continue;
      }

      bool erase_submission = true;
      // Let's find the last command buffer in this submission, so first find the last
      // submit info that has at least one command buffer.
      // We test if for this command buffer, we already have a query result for its last slot
      // and if so (or if the submission does not contain any command buffer) erase this
      // submission.
      auto submit_info_reverse_it = submission.submit_infos.rbegin();
      while (submit_info_reverse_it != submission.submit_infos.rend()) {
        const internal::SubmitInfo& submit_info = submission.submit_infos.back();
        if (submit_info.command_buffers.empty()) {
          ++submit_info_reverse_it;
          continue;
        }
        // We found our last command buffer, so lets check if its result is there:
        const internal::SubmittedCommandBuffer& last_command_buffer =
            submit_info.command_buffers.back();
        uint32_t check_slot_index_end = last_command_buffer.command_buffer_end_slot_index;

        static constexpr VkDeviceSize kResultStride = sizeof(uint64_t);
        uint64_t test_query_result = 0;
        VkResult query_worked = dispatch_table_->GetQueryPoolResults(device)(
            device, query_pool, check_slot_index_end, 1, sizeof(test_query_result),
            &test_query_result, kResultStride, VK_QUERY_RESULT_64_BIT);

        // Only erase the submission if we query its timers now.
        if (query_worked == VK_SUCCESS) {
          erase_submission = true;
          completed_submissions.push_back(submission);
        } else {
          erase_submission = false;
        }
        break;
      }

      if (erase_submission) {
        submission_it = queue_submissions.erase(submission_it);
      } else {
        ++submission_it;
      }
    }
  }

  return completed_submissions;
}

void CommandBufferManager::ResetCommandBuffer(VkCommandBuffer command_buffer) {
  LOG("ResetCommandBuffer");
  absl::WriterMutexLock lock(&mutex_);
  if (!command_buffer_to_state_.contains(command_buffer)) {
    return;
  }
  internal::CommandBufferState& state = command_buffer_to_state_.at(command_buffer);
  VkDevice device = command_buffer_to_device_.at(command_buffer);
  std::vector<uint32_t> marker_slots_to_rollback;
  if (state.command_buffer_begin_slot_index.has_value()) {
    marker_slots_to_rollback.push_back(state.command_buffer_begin_slot_index.value());
  }
  if (state.command_buffer_end_slot_index.has_value()) {
    marker_slots_to_rollback.push_back(state.command_buffer_end_slot_index.value());
  }
  timer_query_pool_->ResetQuerySlots(device, marker_slots_to_rollback, true);

  command_buffer_to_state_.erase(command_buffer);
}

void CommandBufferManager::ResetCommandPool(VkCommandPool command_pool) {
  LOG("ResetCommandPool");
  absl::flat_hash_set<VkCommandBuffer> command_buffers;
  {
    absl::ReaderMutexLock lock(&mutex_);
    if (!pool_to_command_buffers_.contains(command_pool)) {
      return;
    }
    command_buffers = pool_to_command_buffers_.at(command_pool);
  }
  for (const auto& command_buffer : command_buffers) {
    ResetCommandBuffer(command_buffer);
  }
}

uint32_t CommandBufferManager::RecordTimestamp(VkCommandBuffer command_buffer,
                                               VkPipelineStageFlagBits pipeline_stage_flags) {
  LOG("RecordTimestamp");
  VkDevice device;
  {
    absl::ReaderMutexLock lock(&mutex_);
    CHECK(command_buffer_to_device_.contains(command_buffer));
    device = command_buffer_to_device_.at(command_buffer);
  }

  VkQueryPool query_pool = timer_query_pool_->GetQueryPool(device);

  uint32_t slot_index;
  bool found_slot = timer_query_pool_->NextReadyQuerySlot(device, &slot_index);
  CHECK(found_slot);
  dispatch_table_->CmdWriteTimestamp(command_buffer)(command_buffer, pipeline_stage_flags,
                                                     query_pool, slot_index);

  return slot_index;
}

uint64_t CommandBufferManager::QueryGpuTimestampNs(VkDevice device, VkQueryPool query_pool,
                                                   uint32_t slot_index, float timestamp_period) {
  LOG("QueryGpuTimestampNs");
  static constexpr VkDeviceSize kResultStride = sizeof(uint64_t);

  uint64_t timestamp = 0;
  VkResult result_status = dispatch_table_->GetQueryPoolResults(device)(
      device, query_pool, slot_index, 1, sizeof(timestamp), &timestamp, kResultStride,
      VK_QUERY_RESULT_64_BIT);
  CHECK(result_status == VK_SUCCESS);

  return static_cast<uint64_t>(static_cast<double>(timestamp) * timestamp_period);
}

void CommandBufferManager::WriteMetaInfo(
    const internal::SubmissionMetaInformation& meta_info,
    orbit_grpc_protos::GpuQueueSubmissionMetaInfo* target_proto) {
  LOG("WriteMetaInfo");
  target_proto->set_thread_id(meta_info.thread_id);
  target_proto->set_pre_submission_cpu_timestamp(meta_info.pre_submission_cpu_timestamp);
  target_proto->set_post_submission_cpu_timestamp(meta_info.post_submission_cpu_timestamp);
}

}  // namespace orbit_vulkan_layer