// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_VULKAN_LAYER_LAYER_LOGIC_H_
#define ORBIT_VULKAN_LAYER_LAYER_LOGIC_H_

#include "DeviceManager.h"
#include "DispatchTable.h"
#include "OrbitBase/Logging.h"
#include "OrbitService/ProducerSideChannel.h"
#include "QueueManager.h"
#include "SubmissionTracker.h"
#include "TimerQueryPool.h"
#include "VulkanLayerProducerImpl.h"
#include "vulkan/vulkan.h"

namespace orbit_vulkan_layer {

/**
 * This class controls the logic of this layer. For the instrumented vulkan functions,
 * it provides PreCall*, PostCall* and Call* functions, where the Call* functions just forward
 * to the next layer (using the dispatch table).
 * PreCall* functions are executed before the `actual` vulkan call and PostCall* afterwards.
 * PreCall/PostCall are omitted when not needed.
 *
 * Usage: For an instrumented vulkan function "X" a common pattern from the layers entry (Main.cpp)
 * would be:
 * ```
 * logic_.PreCallX(...);
 * logic_.CallX(...);
 * logic_.PostCallX(...);
 * ```
 */
class LayerLogic {
 public:
  LayerLogic()
      : device_manager_(&dispatch_table_),
        timer_query_pool_(&dispatch_table_, kNumTimerQuerySlots),
        command_buffer_manager_(0, &dispatch_table_, &timer_query_pool_, &device_manager_,
                                &vulkan_layer_producer_) {}

  ~LayerLogic() { CloseVulkanLayerProducerIfNecessary(); }

  [[nodiscard]] VkResult PreCallAndCallCreateInstance(const VkInstanceCreateInfo* create_info,
                                                      const VkAllocationCallbacks* allocator,
                                                      VkInstance* instance);
  void PostCallCreateInstance(const VkInstanceCreateInfo* create_info,
                              const VkAllocationCallbacks* allocator, VkInstance* instance);

  [[nodiscard]] PFN_vkVoidFunction CallGetDeviceProcAddr(VkDevice device, const char* name) {
    return dispatch_table_.GetDeviceProcAddr(device)(device, name);
  }

  [[nodiscard]] PFN_vkVoidFunction CallGetInstanceProcAddr(VkInstance instance, const char* name) {
    return dispatch_table_.GetInstanceProcAddr(instance)(instance, name);
  }

  void CallAndPostDestroyInstance(VkInstance instance, const VkAllocationCallbacks* allocator) {
    PFN_vkDestroyInstance destroy_instance_function = dispatch_table_.DestroyInstance(instance);
    CHECK(destroy_instance_function != nullptr);
    dispatch_table_.RemoveInstanceDispatchTable(instance);

    destroy_instance_function(instance, allocator);

    CloseVulkanLayerProducerIfNecessary();
  }

  void CallAndPostDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator) {
    PFN_vkDestroyDevice destroy_device_function = dispatch_table_.DestroyDevice(device);
    CHECK(destroy_device_function != nullptr);
    device_manager_.UntrackLogicalDevice(device);
    dispatch_table_.RemoveDeviceDispatchTable(device);

    destroy_device_function(device, allocator);
  }

  [[nodiscard]] VkResult PreCallAndCallCreateDevice(VkPhysicalDevice physical_device,
                                                    const VkDeviceCreateInfo* create_info,
                                                    const VkAllocationCallbacks* allocator,
                                                    VkDevice* device);
  void PostCallCreateDevice(VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info,
                            const VkAllocationCallbacks* allocator, VkDevice* device);

  [[nodiscard]] VkResult CallEnumerateDeviceExtensionProperties(VkPhysicalDevice physical_device,
                                                                const char* layer_name,
                                                                uint32_t* property_count,
                                                                VkExtensionProperties* properties) {
    return dispatch_table_.EnumerateDeviceExtensionProperties(physical_device)(
        physical_device, layer_name, property_count, properties);
  }

  [[nodiscard]] VkResult CallResetCommandPool(VkDevice device, VkCommandPool command_pool,
                                              VkCommandPoolResetFlags flags) {
    return dispatch_table_.ResetCommandPool(device)(device, command_pool, flags);
  }
  void PostCallResetCommandPool(VkDevice device, VkCommandPool command_pool,
                                VkCommandPoolResetFlags flags);
  [[nodiscard]] VkResult CallAllocateCommandBuffers(
      VkDevice device, const VkCommandBufferAllocateInfo* allocate_info,
      VkCommandBuffer* command_buffers) {
    return dispatch_table_.AllocateCommandBuffers(device)(device, allocate_info, command_buffers);
  }
  void PostCallAllocateCommandBuffers(VkDevice device,
                                      const VkCommandBufferAllocateInfo* allocate_info,
                                      VkCommandBuffer* command_buffers);

  void CallFreeCommandBuffers(VkDevice device, VkCommandPool command_pool,
                              uint32_t command_buffer_count,
                              const VkCommandBuffer* command_buffers) {
    return dispatch_table_.FreeCommandBuffers(device)(device, command_pool, command_buffer_count,
                                                      command_buffers);
  }
  void PostCallFreeCommandBuffers(VkDevice device, VkCommandPool command_pool,
                                  uint32_t command_buffer_count,
                                  const VkCommandBuffer* command_buffers);
  [[nodiscard]] VkResult CallBeginCommandBuffer(VkCommandBuffer command_buffer,
                                                const VkCommandBufferBeginInfo* begin_info) {
    return dispatch_table_.BeginCommandBuffer(command_buffer)(command_buffer, begin_info);
  }
  void PostCallBeginCommandBuffer(VkCommandBuffer command_buffer,
                                  const VkCommandBufferBeginInfo* begin_info);

  void PreCallEndCommandBuffer(VkCommandBuffer command_buffer);
  [[nodiscard]] VkResult CallEndCommandBuffer(VkCommandBuffer command_buffer) {
    return dispatch_table_.EndCommandBuffer(command_buffer)(command_buffer);
  }

  void PreCallResetCommandBuffer(VkCommandBuffer command_buffer, VkCommandBufferResetFlags flags);
  [[nodiscard]] VkResult CallResetCommandBuffer(VkCommandBuffer command_buffer,
                                                VkCommandBufferResetFlags flags) {
    return dispatch_table_.ResetCommandBuffer(command_buffer)(command_buffer, flags);
  }

  void CallGetDeviceQueue(VkDevice device, uint32_t queue_family_index, uint32_t queue_index,
                          VkQueue* queue) {
    return dispatch_table_.GetDeviceQueue(device)(device, queue_family_index, queue_index, queue);
  }
  void PostCallGetDeviceQueue(VkDevice device, uint32_t queue_family_index, uint32_t queue_index,
                              VkQueue* queue);

  void CallGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* queue_info, VkQueue* queue) {
    return dispatch_table_.GetDeviceQueue2(device)(device, queue_info, queue);
  }
  void PostCallGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* queue_info,
                               VkQueue* queue);

  std::optional<uint64_t> PreCallQueueSubmit(VkQueue queue, uint32_t submit_count,
                                             const VkSubmitInfo* submits, VkFence fence);
  [[nodiscard]] VkResult CallQueueSubmit(VkQueue queue, uint32_t submit_count,
                                         const VkSubmitInfo* submits, VkFence fence) {
    return dispatch_table_.QueueSubmit(queue)(queue, submit_count, submits, fence);
  }
  void PostCallQueueSubmit(VkQueue queue, uint32_t submit_count, const VkSubmitInfo* submits,
                           VkFence fence, std::optional<uint64_t> pre_submit_timestamp);

  [[nodiscard]] VkResult CallQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* present_info) {
    return dispatch_table_.QueuePresentKHR(queue)(queue, present_info);
  }
  void PostCallQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* present_info);

  void CallCmdBeginDebugUtilsLabelEXT(VkCommandBuffer command_buffer,
                                      const VkDebugUtilsLabelEXT* label_info) {
    if (dispatch_table_.IsDebugUtilsExtensionSupported(command_buffer)) {
      dispatch_table_.CmdBeginDebugUtilsLabelEXT(command_buffer)(command_buffer, label_info);
    }
  }
  void PostCallCmdBeginDebugUtilsLabelEXT(VkCommandBuffer command_buffer,
                                          const VkDebugUtilsLabelEXT* label_info);

  void PreCallCmdEndDebugUtilsLabelEXT(VkCommandBuffer command_buffer);
  void CallCmdEndDebugUtilsLabelEXT(VkCommandBuffer command_buffer) {
    if (dispatch_table_.IsDebugUtilsExtensionSupported(command_buffer)) {
      dispatch_table_.CmdEndDebugUtilsLabelEXT(command_buffer)(command_buffer);
    }
  }

  void CallCmdDebugMarkerBeginEXT(VkCommandBuffer command_buffer,
                                  const VkDebugMarkerMarkerInfoEXT* marker_info) {
    if (dispatch_table_.IsDebugMarkerExtensionSupported(command_buffer)) {
      dispatch_table_.CmdDebugMarkerBeginEXT(command_buffer)(command_buffer, marker_info);
    }
  }
  void PostCallCmdDebugMarkerBeginEXT(VkCommandBuffer command_buffer,
                                      const VkDebugMarkerMarkerInfoEXT* marker_info);

  void PreCallCmdDebugMarkerEndEXT(VkCommandBuffer command_buffer);
  void CallCmdDebugMarkerEndEXT(VkCommandBuffer command_buffer) {
    if (dispatch_table_.IsDebugMarkerExtensionSupported(command_buffer)) {
      dispatch_table_.CmdDebugMarkerEndEXT(command_buffer)(command_buffer);
    }
  }

 private:
  void InitVulkanLayerProducerIfNecessary() {
    absl::MutexLock lock{&vulkan_layer_producer_mutex_};
    if (vulkan_layer_producer_ == nullptr) {
      vulkan_layer_producer_ = std::make_unique<VulkanLayerProducerImpl>();
      vulkan_layer_producer_->BringUp(orbit_service::CreateProducerSideChannel());
    }
  }

  void CloseVulkanLayerProducerIfNecessary() {
    absl::MutexLock lock{&vulkan_layer_producer_mutex_};
    if (vulkan_layer_producer_ != nullptr) {
      // TODO: Only do this when DestroyInstance has been called a number of times
      //  equal to the number of times CreateInstance was called.
      vulkan_layer_producer_->TakeDown();
      vulkan_layer_producer_.reset();
    }
  }

  std::unique_ptr<VulkanLayerProducer> vulkan_layer_producer_ = nullptr;
  absl::Mutex vulkan_layer_producer_mutex_;

  DispatchTable dispatch_table_;
  DeviceManager<DispatchTable> device_manager_;
  TimerQueryPool<DispatchTable> timer_query_pool_;
  SubmissionTracker<DispatchTable, DeviceManager<DispatchTable>, TimerQueryPool<DispatchTable>>
      command_buffer_manager_;
  QueueManager queue_manager_;

  static constexpr uint32_t kNumTimerQuerySlots = 65536;
};

}  // namespace orbit_vulkan_layer

#endif  // ORBIT_VULKAN_LAYER_LAYER_LOGIC_H_
