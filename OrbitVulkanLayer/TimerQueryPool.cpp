// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "TimerQueryPool.h"

#include <thread>

#include "OrbitBase/Profiling.h"

namespace orbit_vulkan_layer {
void orbit_vulkan_layer::TimerQueryPool::InitializeTimerQueryPool(const VkDevice& device) {
  LOG("InitializeTimerQueryPool");
  VkQueryPool query_pool;

  VkQueryPoolCreateInfo create_info = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                       .pNext = nullptr,
                                       .flags = 0,
                                       .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                       .queryCount = kNumPhysicalTimerQuerySlots,
                                       .pipelineStatistics = 0};

  VkResult result =
      dispatch_table_->CreateQueryPool(device)(device, &create_info, nullptr, &query_pool);
  CHECK(result == VK_SUCCESS);

  dispatch_table_->ResetQueryPoolEXT(device)(device, query_pool, 0, kNumPhysicalTimerQuerySlots);

  {
    absl::WriterMutexLock lock(&mutex_);
    device_to_query_pool_[device] = query_pool;
    std::array<SlotState, kNumPhysicalTimerQuerySlots> slots{};
    slots.fill(kReadyForQueryIssue);
    device_to_query_slots_[device] = slots;
    device_to_pending_reset_slots_[device] = {};
    device_to_potential_next_free_index_[device] = 0;
  }
}

VkQueryPool TimerQueryPool::GetQueryPool(const VkDevice& device) {
  LOG("GetQueryPool");
  absl::ReaderMutexLock lock(&mutex_);
  CHECK(device_to_query_pool_.contains(device));
  return device_to_query_pool_.at(device);
}

bool TimerQueryPool::NextReadyQuerySlot(const VkDevice& device, uint32_t* allocated_index) {
  LOG("NextReadyQuerySlot");
  absl::WriterMutexLock lock(&mutex_);
  CHECK(device_to_potential_next_free_index_.contains(device));
  CHECK(device_to_query_slots_.contains(device));
  uint32_t potential_next_free_slot = device_to_potential_next_free_index_.at(device);
  std::array<SlotState, kNumPhysicalTimerQuerySlots>& slots = device_to_query_slots_.at(device);
  uint32_t current_slot = potential_next_free_slot;
  do {
    if (slots.at(current_slot) == kReadyForQueryIssue) {
      device_to_potential_next_free_index_[device] =
          (current_slot + 1) % kNumPhysicalTimerQuerySlots;
      slots.at(current_slot) = kQueryPendingOnGPU;
      *allocated_index = current_slot;
      return true;
    }
    current_slot = (current_slot + 1) % kNumPhysicalTimerQuerySlots;
  } while (current_slot != potential_next_free_slot);

  return false;
}

void TimerQueryPool::ResetQuerySlots(const VkDevice& device,
                                     const std::vector<uint32_t>& physical_slot_indices) {
  if (physical_slot_indices.empty()) {
    return;
  }
  LOG("ResetQuerySlots");
  absl::WriterMutexLock lock(&mutex_);
  std::array<SlotState, kNumPhysicalTimerQuerySlots>& slot_states =
      device_to_query_slots_.at(device);
  for (uint32_t physical_slot_index : physical_slot_indices) {
    CHECK(physical_slot_index < kNumPhysicalTimerQuerySlots);
    const SlotState& current_state = slot_states.at(physical_slot_index);
    CHECK(current_state == kQueryPendingOnGPU);
    const VkQueryPool& query_pool = device_to_query_pool_.at(device);
    dispatch_table_->ResetQueryPoolEXT(device)(device, query_pool, physical_slot_index, 1);
    slot_states.at(physical_slot_index) = kReadyForQueryIssue;
  }
}

void TimerQueryPool::RollbackPendingQuerySlots(const VkDevice& device,
                                               const std::vector<uint32_t>& physical_slot_indices) {
  if (physical_slot_indices.empty()) {
    return;
  }
  LOG("RollbackPendingQuerySlots");
  absl::WriterMutexLock lock(&mutex_);
  std::array<SlotState, kNumPhysicalTimerQuerySlots>& slot_states =
      device_to_query_slots_.at(device);
  for (uint32_t physical_slot_index : physical_slot_indices) {
    CHECK(physical_slot_index < kNumPhysicalTimerQuerySlots);
    const SlotState& current_state = slot_states.at(physical_slot_index);
    CHECK(current_state == kQueryPendingOnGPU);
    slot_states.at(physical_slot_index) = kReadyForQueryIssue;
  }
}

}  // namespace orbit_vulkan_layer