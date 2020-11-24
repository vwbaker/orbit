// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_VULKAN_LAYER_VULKAN_LAYER_PRODUCER_H_
#define ORBIT_VULKAN_LAYER_VULKAN_LAYER_PRODUCER_H_

#include "producer_side_services.grpc.pb.h"

namespace orbit_vulkan_layer {

// This interface exposes methods for the communication between the Vulkan layer and Orbit.
// In particular, it provides those methods to LayerLogic and CommandBufferManager.
// All the methods are pure virtual to allow mocking for testing.

class VulkanLayerProducer {
 public:
  virtual ~VulkanLayerProducer() = default;

  [[nodiscard]] virtual bool BringUp(std::string_view unix_domain_socket_path) = 0;

  virtual void TakeDown() = 0;

  [[nodiscard]] virtual bool IsCapturing() = 0;

  virtual void EnqueueCaptureEvent(orbit_grpc_protos::CaptureEvent&& capture_event) = 0;

  [[nodiscard]] virtual uint64_t InternStringIfNecessaryAndGetKey(std::string str) = 0;
};

}  // namespace orbit_vulkan_layer

#endif  // ORBIT_VULKAN_LAYER_VULKAN_LAYER_PRODUCER_H_
