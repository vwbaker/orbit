// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_VULKAN_LAYER_VULKAN_LAYER_PRODUCER_H_
#define ORBIT_VULKAN_LAYER_VULKAN_LAYER_PRODUCER_H_

#include "CaptureEventProducer/LockFreeBufferCaptureEventProducer.h"
#include "absl/container/flat_hash_set.h"

namespace orbit_vulkan_layer {

class VulkanLayerProducer
    : public orbit_producer::LockFreeBufferCaptureEventProducer<orbit_grpc_protos::CaptureEvent> {
  using Base = orbit_producer::LockFreeBufferCaptureEventProducer<orbit_grpc_protos::CaptureEvent>;

 public:
  uint64_t InternStringIfNecessaryAndGetKey(std::string str);

  void ClearStringInternPool();

 protected:
  orbit_grpc_protos::CaptureEvent TranslateIntermediateEvent(
      orbit_grpc_protos::CaptureEvent&& intermediate_event) override;

  void OnCaptureStart() override;

 private:
  static uint64_t ComputeStringKey(const std::string& str) { return std::hash<std::string>{}(str); }

  absl::flat_hash_set<uint64_t> string_keys_sent_;
  absl::Mutex string_keys_sent_mutex_;
};

}  // namespace orbit_vulkan_layer

#endif  // ORBIT_VULKAN_LAYER_VULKAN_LAYER_PRODUCER_H_
