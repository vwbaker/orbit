// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_VULKAN_LAYER_TIMER_QUERY_POOL_H_
#define ORBIT_VULKAN_LAYER_TIMER_QUERY_POOL_H_

#include "DispatchTable.h"
#include "OrbitBase/Logging.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace orbit_vulkan_layer {

class TimerQueryPool {
 public:
  explicit TimerQueryPool(DispatchTable* dispatch_table) : dispatch_table_(dispatch_table) {}
  void InitializeTimerQueryPool(VkDevice device);
  [[nodiscard]] VkQueryPool GetQueryPool(VkDevice device);
  [[nodiscard]] bool NextReadyQuerySlot(VkDevice device, uint32_t* allocated_index);

  void ResetQuerySlots(VkDevice device, const std::vector<uint32_t>& physical_slot_indices);

  void RollbackPendingQuerySlots(VkDevice device,
                                 const std::vector<uint32_t>& physical_slot_indices);

 private:
  enum SlotState {
    kReadyForQueryIssue = 0,
    kQueryPendingOnGPU,
  };

  static constexpr uint32_t kNumPhysicalTimerQuerySlots = 65536;

  DispatchTable* dispatch_table_;
  absl::Mutex mutex_;
  absl::flat_hash_map<VkDevice, VkQueryPool> device_to_query_pool_;

  absl::flat_hash_map<VkDevice, std::array<SlotState, kNumPhysicalTimerQuerySlots>>
      device_to_query_slots_;
  absl::flat_hash_map<VkDevice, uint32_t> device_to_potential_next_free_index_;

  absl::flat_hash_map<VkDevice, std::vector<uint32_t>> device_to_pending_reset_slots_;
};
}  // namespace orbit_vulkan_layer

#endif  // ORBIT_VULKAN_LAYER_TIMER_QUERY_POOL_H_
