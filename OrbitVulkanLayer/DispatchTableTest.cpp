// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "DispatchTable.h"
#include "gtest/gtest.h"

namespace orbit_vulkan_layer {

TEST(DispatchTable, CanInitializeForInstance) {
  // We can not create an actual VkInstance, but the first bytes of an any dispatchable type in
  // Vulkan will be a pointer to a dispatch table. This characteristic will be used by our dispatch
  // table wrapper, so we need to mimic it.
  VkLayerInstanceDispatchTable some_dispatch_table = {};
  auto instance = absl::bit_cast<VkInstance>(&some_dispatch_table);
  PFN_vkGetInstanceProcAddr next_get_instance_proc_addr_function =
      +[](VkInstance /*instance*/, const char * /*name*/) -> PFN_vkVoidFunction { return nullptr; };

  DispatchTable dispatch_table = {};
  dispatch_table.CreateInstanceDispatchTable(instance, next_get_instance_proc_addr_function);
}

TEST(DispatchTable, CannotInitializeTwiceInstance) {
  // We can not create an actual VkInstance, but the first bytes of an any dispatchable type in
  // Vulkan will be a pointer to a dispatch table. This characteristic will be used by our dispatch
  // table wrapper, so we need to mimic it.
  VkLayerInstanceDispatchTable some_dispatch_table = {};
  auto instance = absl::bit_cast<VkInstance>(&some_dispatch_table);
  PFN_vkGetInstanceProcAddr next_get_instance_proc_addr_function =
      +[](VkInstance /*instance*/, const char * /*name*/) -> PFN_vkVoidFunction { return nullptr; };

  DispatchTable dispatch_table = {};
  dispatch_table.CreateInstanceDispatchTable(instance, next_get_instance_proc_addr_function);
  EXPECT_DEATH(
      {
        dispatch_table.CreateInstanceDispatchTable(instance, next_get_instance_proc_addr_function);
      },
      "");
}

}  // namespace orbit_vulkan_layer