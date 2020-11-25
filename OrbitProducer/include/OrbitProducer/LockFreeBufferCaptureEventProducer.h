// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_PRODUCER_LOCK_FREE_BUFFER_CAPTURE_EVENT_PRODUCER_H_
#define ORBIT_PRODUCER_LOCK_FREE_BUFFER_CAPTURE_EVENT_PRODUCER_H_

#include "OrbitBase/Logging.h"
#include "OrbitProducer/CaptureEventProducer.h"
#include "concurrentqueue.h"

namespace orbit_producer {

template <typename IntermediateEventT>
class LockFreeBufferCaptureEventProducer : public CaptureEventProducer {
 public:
  [[nodiscard]] bool ConnectAndStart(std::string_view unix_domain_socket_path) override {
    if (!CaptureEventProducer::ConnectAndStart(unix_domain_socket_path)) {
      return false;
    }

    forwarder_thread_ = std::thread{[this] { ForwarderThread(); }};
    return true;
  }

  void ShutdownAndWait() override {
    shutdown_requested_ = true;

    CHECK(forwarder_thread_.joinable());
    forwarder_thread_.join();

    CaptureEventProducer::ShutdownAndWait();
  }

  void EnqueueIntermediateEvent(const IntermediateEventT& event) {
    lock_free_queue_.enqueue(event);
  }

  void EnqueueIntermediateEvent(IntermediateEventT&& event) {
    lock_free_queue_.enqueue(std::move(event));
  }

  void EnqueueIntermediateEventIfCapturing(
      const std::function<IntermediateEventT()>& event_builder_if_capturing) {
    if (IsCapturing()) {
      lock_free_queue_.enqueue(event_builder_if_capturing());
    }
  }

 protected:
  void OnCaptureStart() override {
    CaptureEventProducer::OnCaptureStart();
    {
      absl::MutexLock lock{&should_send_all_events_sent_mutex_};
      should_send_all_events_sent_ = false;
    }
  }

  void OnCaptureStop() override {
    CaptureEventProducer::OnCaptureStop();
    {
      absl::MutexLock lock{&should_send_all_events_sent_mutex_};
      should_send_all_events_sent_ = true;
    }
  }

  [[nodiscard]] virtual orbit_grpc_protos::CaptureEvent TranslateIntermediateEvent(
      IntermediateEventT&& intermediate_event) = 0;

 private:
  void ForwarderThread() {
    constexpr uint64_t kMaxEventsPerRequest = 10'000;
    std::vector<IntermediateEventT> dequeued_events;
    dequeued_events.resize(kMaxEventsPerRequest);
    while (!shutdown_requested_) {
      size_t dequeued_event_count;
      while ((dequeued_event_count = lock_free_queue_.try_dequeue_bulk(dequeued_events.begin(),
                                                                       kMaxEventsPerRequest)) > 0) {
        orbit_grpc_protos::ReceiveCommandsAndSendEventsRequest send_request;
        auto* capture_events = send_request.mutable_buffered_capture_events()->mutable_capture_events();
        for (size_t i = 0; i < dequeued_event_count; ++i) {
          orbit_grpc_protos::CaptureEvent* event = capture_events->Add();
          *event = TranslateIntermediateEvent(std::move(dequeued_events[i]));
        }

        if (!SendCaptureEvents(send_request)) {
          ERROR("Forwarding %lu CaptureEvents", dequeued_event_count);
          break;
        }
        if (dequeued_event_count < kMaxEventsPerRequest) {
          break;
        }
      }

      // lock_free_queue_ is now empty: check if we need to send AllEventsSent.
      {
        should_send_all_events_sent_mutex_.Lock();
        if (should_send_all_events_sent_) {
          should_send_all_events_sent_ = false;
          should_send_all_events_sent_mutex_.Unlock();
          if (!NotifyAllEventsSent()) {
            ERROR("Notifying that all CaptureEvents have been sent");
            break;
          }
          continue;
        }
        should_send_all_events_sent_mutex_.Unlock();
      }

      // Wait for lock_free_queue_ to fill up with new CaptureEvents.
      std::this_thread::sleep_for(std::chrono::microseconds{100});
    }
  }

 private:
  moodycamel::ConcurrentQueue<IntermediateEventT> lock_free_queue_;

  std::thread forwarder_thread_;
  std::atomic<bool> shutdown_requested_ = false;

  bool should_send_all_events_sent_ = false;
  absl::Mutex should_send_all_events_sent_mutex_;
};

}  // namespace orbit_producer

#endif  // ORBIT_PRODUCER_LOCK_FREE_BUFFER_CAPTURE_EVENT_PRODUCER_H_
