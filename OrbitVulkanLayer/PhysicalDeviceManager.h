// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_VULKAN_LAYER_PHYSICAL_DEVICE_MANAGER_H_
#define ORBIT_VULKAN_LAYER_PHYSICAL_DEVICE_MANAGER_H_

#include "DispatchTable.h"
#include "OrbitBase/Logging.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "vulkan/vulkan.h"

namespace orbit_vulkan_layer {

class PhysicalDeviceManager {
 public:
  explicit PhysicalDeviceManager(DispatchTable* dispatch_table) : dispatch_table_(dispatch_table) {}

  void TrackPhysicalDevice(const VkPhysicalDevice& physical_device, const VkDevice& device) {
    absl::WriterMutexLock lock(&mutex_);
    device_to_physical_device_[device] = physical_device;
    VkPhysicalDeviceProperties properties;
    dispatch_table_->GetPhysicalDeviceProperties(physical_device)(physical_device, &properties);
    physical_device_to_properties_[physical_device] = properties;
  }

  [[nodiscard]] VkPhysicalDevice GetPhysicalDeviceOfLogicalDevice(const VkDevice& device) {
    absl::ReaderMutexLock lock(&mutex_);
    CHECK(device_to_physical_device_.contains(device));
    return device_to_physical_device_.at(device);
  }

  void UntrackLogicalDevice(const VkDevice& device) {
    absl::WriterMutexLock lock(&mutex_);
    CHECK(device_to_physical_device_.contains(device));
    device_to_physical_device_.erase(device);
  }

  [[nodiscard]] VkPhysicalDeviceProperties GetPhysicalDeviceProperties(
      const VkPhysicalDevice& physical_device);

 private:
  absl::Mutex mutex_;
  DispatchTable* dispatch_table_;
  absl::flat_hash_map<VkPhysicalDevice, VkPhysicalDeviceProperties> physical_device_to_properties_;
  absl::flat_hash_map<VkDevice, VkPhysicalDevice> device_to_physical_device_;
};

}  // namespace orbit_vulkan_layer

#endif  // ORBIT_VULKAN_LAYER_PHYSICAL_DEVICE_MANAGER_H_
