// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "VulkanLayerProducer.h"

namespace orbit_vulkan_layer {

orbit_grpc_protos::CaptureEvent VulkanLayerProducer::TranslateIntermediateEvent(
    orbit_grpc_protos::CaptureEvent&& intermediate_event) {
  return std::move(intermediate_event);
}

void VulkanLayerProducer::OnCaptureStart() {
  Base::OnCaptureStart();
  ClearStringInternPool();
}

uint64_t VulkanLayerProducer::InternStringIfNecessaryAndGetKey(std::string str) {
  uint64_t key = ComputeStringKey(str);
  {
    absl::MutexLock lock{&string_keys_sent_mutex_};
    bool inserted = string_keys_sent_.emplace(key).second;
    if (!inserted) {
      return key;
    }
  }

  orbit_grpc_protos::CaptureEvent event;
  event.mutable_interned_string()->set_key(key);
  event.mutable_interned_string()->set_intern(std::move(str));
  EnqueueIntermediateEvent(std::move(event));
  return key;
}

void VulkanLayerProducer::ClearStringInternPool() {
  absl::MutexLock lock{&string_keys_sent_mutex_};
  string_keys_sent_.clear();
}

}  // namespace orbit_vulkan_layer
