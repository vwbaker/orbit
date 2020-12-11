// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "DeviceManager.h"
#include "DispatchTable.h"
#include "QueueManager.h"
#include "SubmissionTracker.h"
#include "TimerQueryPool.h"
#include "VulkanLayerController.h"
#include "absl/base/casts.h"
#include "vulkan/vk_layer.h"
#include "vulkan/vulkan.h"

/*
 * The big picture:
 * This is the main entry point for Orbit's vulkan layer. The layer is structured as follows:
 * * All instrumented vulkan functions will hook into implementations found here
 *   (e.g. OrbitQueueSubmit)
 * * The actual logic of the layer is implemented in LayerLogic.h/.cpp. This has the following
 *    scheme: For each vk function, there is a PreCall*, Call*, and PostCall* function, where
 *    Call* will just forward the call to the "actual" vulkan function following the dispatch
 *    table (see DispatchTable).
 *  * There are the following helper classes to structure the actual layer logic:
 *     * CommandBufferManager.h: Which keeps track of command buffer allocations.
 *     * DispatchTable.h: Which provides virtual dispatch for the vulkan functions to be called.
 *     * QueryManager.h: Which keeps track of query pool slots e.g. used for timestamp queries
 *        and allows to assign those.
 *     * QueueManager keeps track of association of VkQueue(s) to devices.
 *
 *
 * For this free functions in this namespace:
 * As said, they act as entries to the layer.
 * OrbitGetDeviceProcAddr and OrbitGetInstanceProcAddr are the actual entry points, called by
 * the loader and potential other layers, and forward to all the functions that this layer
 * intercepts.
 *
 * The actual logic of the layer (and thus of each intercepted vulkan function) is implemented
 * in `LayerLogic`.
 *
 * Only the basic enumeration as well as the ProcAddr functions are implemented here.
 *
 */
namespace orbit_vulkan_layer {

#if defined(WIN32)
#define ORBIT_EXPORT extern "C" __declspec(dllexport) VK_LAYER_EXPORT
#else
#define ORBIT_EXPORT extern "C" VK_LAYER_EXPORT
#endif

using DeviceMangerImpl = DeviceManager<DispatchTable>;
using TimerQueryPoolImpl = TimerQueryPool<DispatchTable>;
using SubmissionTrackerImpl =
    SubmissionTracker<DispatchTable, DeviceMangerImpl, TimerQueryPoolImpl>;
static VulkanLayerController<DispatchTable, QueueManager, DeviceMangerImpl, TimerQueryPoolImpl,
                             SubmissionTrackerImpl>
    logic_;

// ----------------------------------------------------------------------------
// Layer bootstrapping code
// ----------------------------------------------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL OrbitCreateInstance(const VkInstanceCreateInfo* create_info,
                                                   const VkAllocationCallbacks* allocator,
                                                   VkInstance* instance) {
  VkResult result = logic_.OnCreateInstance(create_info, allocator, instance);
  return result;
}

VKAPI_ATTR void VKAPI_CALL OrbitDestroyInstance(VkInstance instance,
                                                const VkAllocationCallbacks* allocator) {
  logic_.OnDestroyInstance(instance, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL OrbitCreateDevice(VkPhysicalDevice physical_device,
                                                 const VkDeviceCreateInfo* create_info,
                                                 const VkAllocationCallbacks* allocator,
                                                 VkDevice* device) {
  return logic_.OnCreateDevice(physical_device, create_info, allocator, device);
}

VKAPI_ATTR void VKAPI_CALL OrbitDestroyDevice(VkDevice device,
                                              const VkAllocationCallbacks* allocator) {
  logic_.OnDestroyDevice(device, allocator);
}

// ----------------------------------------------------------------------------
// Core layer logic
// ----------------------------------------------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL OrbitResetCommandPool(VkDevice device, VkCommandPool command_pool,
                                                     VkCommandPoolResetFlags flags) {
  return logic_.OnResetCommandPool(device, command_pool, flags);
}

VKAPI_ATTR VkResult VKAPI_CALL
OrbitAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* allocate_info,
                            VkCommandBuffer* command_buffers) {
  return logic_.OnAllocateCommandBuffers(device, allocate_info, command_buffers);
}

VKAPI_ATTR void VKAPI_CALL OrbitFreeCommandBuffers(VkDevice device, VkCommandPool command_pool,
                                                   uint32_t command_buffer_count,
                                                   const VkCommandBuffer* command_buffers) {
  logic_.OnFreeCommandBuffers(device, command_pool, command_buffer_count, command_buffers);
}

VKAPI_ATTR VkResult VKAPI_CALL OrbitBeginCommandBuffer(VkCommandBuffer command_buffer,
                                                       const VkCommandBufferBeginInfo* begin_info) {
  return logic_.OnBeginCommandBuffer(command_buffer, begin_info);
}

VKAPI_ATTR VkResult VKAPI_CALL OrbitEndCommandBuffer(VkCommandBuffer command_buffer) {
  return logic_.OnEndCommandBuffer(command_buffer);
}

VKAPI_ATTR VkResult VKAPI_CALL OrbitResetCommandBuffer(VkCommandBuffer command_buffer,
                                                       VkCommandBufferResetFlags flags) {
  return logic_.OnResetCommandBuffer(command_buffer, flags);
}

VKAPI_ATTR void VKAPI_CALL OrbitGetDeviceQueue(VkDevice device, uint32_t queue_family_index,
                                               uint32_t queue_index, VkQueue* pQueue) {
  logic_.OnGetDeviceQueue(device, queue_family_index, queue_index, pQueue);
}

VKAPI_ATTR void VKAPI_CALL OrbitGetDeviceQueue2(VkDevice device,
                                                const VkDeviceQueueInfo2* queue_info,
                                                VkQueue* queue) {
  logic_.OnGetDeviceQueue2(device, queue_info, queue);
}

VKAPI_ATTR VkResult VKAPI_CALL OrbitQueueSubmit(VkQueue queue, uint32_t submit_count,
                                                const VkSubmitInfo* submits, VkFence fence) {
  return logic_.OnQueueSubmit(queue, submit_count, submits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL OrbitQueuePresentKHR(VkQueue queue,
                                                    const VkPresentInfoKHR* present_info) {
  return logic_.OnQueuePresentKHR(queue, present_info);
}

VKAPI_ATTR void VKAPI_CALL OrbitCmdBeginDebugUtilsLabelEXT(VkCommandBuffer command_buffer,
                                                           const VkDebugUtilsLabelEXT* label_info) {
  logic_.OnCmdBeginDebugUtilsLabelEXT(command_buffer, label_info);
}

VKAPI_ATTR void VKAPI_CALL OrbitCmdEndDebugUtilsLabelEXT(VkCommandBuffer command_buffer) {
  logic_.OnCmdEndDebugUtilsLabelEXT(command_buffer);
}

VKAPI_ATTR void VKAPI_CALL OrbitCmdDebugMarkerBeginEXT(
    VkCommandBuffer command_buffer, const VkDebugMarkerMarkerInfoEXT* marker_info) {
  logic_.OnCmdDebugMarkerBeginEXT(command_buffer, marker_info);
}

VKAPI_ATTR void VKAPI_CALL OrbitCmdDebugMarkerEndEXT(VkCommandBuffer command_buffer) {
  logic_.OnCmdDebugMarkerEndEXT(command_buffer);
}

// ----------------------------------------------------------------------------
// Layer enumeration functions
// ----------------------------------------------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL
OrbitEnumerateInstanceLayerProperties(uint32_t* property_count, VkLayerProperties* properties) {
  return logic_.OnEnumerateInstanceLayerProperties(property_count, properties);
}

// Deprecated by Khronos, but we'll support it in case older applications still
// use it.
VKAPI_ATTR VkResult VKAPI_CALL OrbitEnumerateDeviceLayerProperties(
    VkPhysicalDevice /*physical_device*/, uint32_t* property_count, VkLayerProperties* properties) {
  // This function is supposed to return the same results as
  // EnumerateInstanceLayerProperties since device layers were deprecated.
  return logic_.OnEnumerateInstanceLayerProperties(property_count, properties);
}

VKAPI_ATTR VkResult VKAPI_CALL OrbitEnumerateInstanceExtensionProperties(
    const char* layer_name, uint32_t* property_count, VkExtensionProperties* properties) {
  return logic_.OnEnumerateInstanceExtensionProperties(layer_name, property_count, properties);
}

VKAPI_ATTR VkResult VKAPI_CALL OrbitEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physical_device, const char* layer_name, uint32_t* property_count,
    VkExtensionProperties* properties) {
  return logic_.OnEnumerateDeviceExtensionProperties(physical_device, layer_name, property_count,
                                                     properties);
}

// ----------------------------------------------------------------------------
// GetProcAddr functions
// ----------------------------------------------------------------------------

#define ORBIT_GETPROCADDR(func)                              \
  if (strcmp(name, "vk" #func) == 0) {                       \
    return absl::bit_cast<PFN_vkVoidFunction>(&Orbit##func); \
  }

ORBIT_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL OrbitGetDeviceProcAddr(VkDevice device,
                                                                             const char* name) {
  // Functions available through GetInstanceProcAddr and GetDeviceProcAddr
  ORBIT_GETPROCADDR(GetDeviceProcAddr)
  ORBIT_GETPROCADDR(EnumerateDeviceLayerProperties)
  ORBIT_GETPROCADDR(EnumerateDeviceExtensionProperties)
  ORBIT_GETPROCADDR(CreateDevice)
  ORBIT_GETPROCADDR(DestroyDevice)

  ORBIT_GETPROCADDR(ResetCommandPool)

  ORBIT_GETPROCADDR(AllocateCommandBuffers)
  ORBIT_GETPROCADDR(FreeCommandBuffers)

  ORBIT_GETPROCADDR(BeginCommandBuffer)
  ORBIT_GETPROCADDR(EndCommandBuffer)
  ORBIT_GETPROCADDR(ResetCommandBuffer)

  ORBIT_GETPROCADDR(QueueSubmit)
  ORBIT_GETPROCADDR(QueuePresentKHR)
  ORBIT_GETPROCADDR(GetDeviceQueue)
  ORBIT_GETPROCADDR(GetDeviceQueue2)

  ORBIT_GETPROCADDR(CmdBeginDebugUtilsLabelEXT)
  ORBIT_GETPROCADDR(CmdEndDebugUtilsLabelEXT)
  ORBIT_GETPROCADDR(CmdDebugMarkerBeginEXT)
  ORBIT_GETPROCADDR(CmdDebugMarkerEndEXT)
  LOG("Fallback");
  return logic_.OnGetDeviceProcAddr(device, name);
}

ORBIT_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL OrbitGetInstanceProcAddr(VkInstance instance,
                                                                               const char* name) {
  // Functions available only through GetInstanceProcAddr
  ORBIT_GETPROCADDR(GetInstanceProcAddr)
  ORBIT_GETPROCADDR(CreateInstance)
  ORBIT_GETPROCADDR(DestroyInstance)
  ORBIT_GETPROCADDR(EnumerateInstanceLayerProperties)
  ORBIT_GETPROCADDR(EnumerateInstanceExtensionProperties)

  // Functions available through GetInstanceProcAddr and GetDeviceProcAddr
  ORBIT_GETPROCADDR(GetDeviceProcAddr)
  ORBIT_GETPROCADDR(EnumerateDeviceLayerProperties)
  ORBIT_GETPROCADDR(EnumerateDeviceExtensionProperties)
  ORBIT_GETPROCADDR(CreateDevice)
  ORBIT_GETPROCADDR(DestroyDevice)

  ORBIT_GETPROCADDR(ResetCommandPool)

  ORBIT_GETPROCADDR(AllocateCommandBuffers)
  ORBIT_GETPROCADDR(FreeCommandBuffers)

  ORBIT_GETPROCADDR(BeginCommandBuffer)
  ORBIT_GETPROCADDR(EndCommandBuffer)
  ORBIT_GETPROCADDR(ResetCommandBuffer)

  ORBIT_GETPROCADDR(QueueSubmit)
  ORBIT_GETPROCADDR(QueuePresentKHR)
  ORBIT_GETPROCADDR(GetDeviceQueue)
  ORBIT_GETPROCADDR(GetDeviceQueue2)

  ORBIT_GETPROCADDR(CmdBeginDebugUtilsLabelEXT)
  ORBIT_GETPROCADDR(CmdEndDebugUtilsLabelEXT)
  ORBIT_GETPROCADDR(CmdDebugMarkerBeginEXT)
  ORBIT_GETPROCADDR(CmdDebugMarkerEndEXT)

  return logic_.OnGetInstanceProcAddr(instance, name);
}

#undef ORBIT_GETPROCADDR

#undef ORBIT_EXPORT

}  // namespace orbit_vulkan_layer
