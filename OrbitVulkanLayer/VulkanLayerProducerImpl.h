// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_VULKAN_LAYER_VULKAN_LAYER_PRODUCER_IMPL_H_
#define ORBIT_VULKAN_LAYER_VULKAN_LAYER_PRODUCER_IMPL_H_

#include "CaptureEventProducer/LockFreeBufferCaptureEventProducer.h"
#include "VulkanLayerProducer.h"
#include "absl/container/flat_hash_set.h"
#include "producer_side_services.grpc.pb.h"

namespace orbit_vulkan_layer {

// This class provides the implementation of VulkanLayerProducer,
// delegating most methods to LockFreeBufferCaptureEventProducer
// while also handling interning of strings.

class VulkanLayerProducerImpl : public VulkanLayerProducer {
 public:
  [[nodiscard]] bool BringUp(std::string_view unix_domain_socket_path) override {
    return lock_free_producer_.BringUp(unix_domain_socket_path);
  }

  void TakeDown() override { lock_free_producer_.TakeDown(); }

  [[nodiscard]] bool IsCapturing() override { return lock_free_producer_.IsCapturing(); }

  void EnqueueCaptureEvent(orbit_grpc_protos::CaptureEvent&& capture_event) override {
    lock_free_producer_.EnqueueIntermediateEvent(std::move(capture_event));
  }

  [[nodiscard]] uint64_t InternStringIfNecessaryAndGetKey(std::string str) override;

 private:
  class LockFreeBufferVulkanLayerProducer
      : public orbit_producer::LockFreeBufferCaptureEventProducer<orbit_grpc_protos::CaptureEvent> {
   public:
    explicit LockFreeBufferVulkanLayerProducer(VulkanLayerProducerImpl* outer) : outer_{outer} {}

   protected:
    void OnCaptureStart() override {
      LockFreeBufferCaptureEventProducer::OnCaptureStart();
      outer_->ClearStringInternPool();
    }

    orbit_grpc_protos::CaptureEvent TranslateIntermediateEvent(
        orbit_grpc_protos::CaptureEvent&& intermediate_event) override {
      return std::move(intermediate_event);
    }

   private:
    VulkanLayerProducerImpl* outer_;
  };

 private:
  static uint64_t ComputeStringKey(const std::string& str) { return std::hash<std::string>{}(str); }

  void ClearStringInternPool() {
    absl::MutexLock lock{&string_keys_sent_mutex_};
    string_keys_sent_.clear();
  }

 private:
  LockFreeBufferVulkanLayerProducer lock_free_producer_{this};

  absl::flat_hash_set<uint64_t> string_keys_sent_;
  absl::Mutex string_keys_sent_mutex_;
};

}  // namespace orbit_vulkan_layer

#endif  // ORBIT_VULKAN_LAYER_VULKAN_LAYER_PRODUCER_IMPL_H_
