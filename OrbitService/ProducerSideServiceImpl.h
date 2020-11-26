// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_SERVICE_PRODUCER_SIDE_SERVICE_IMPL_H_
#define ORBIT_SERVICE_PRODUCER_SIDE_SERVICE_IMPL_H_

#include "CaptureStartStopListener.h"
#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"
#include "producer_side_services.grpc.pb.h"

namespace orbit_service {

class ProducerSideServiceImpl final : public orbit_grpc_protos::ProducerSideService::Service,
                                      public CaptureStartStopListener {
 public:
  void OnCaptureStartRequested(CaptureEventBuffer* capture_event_buffer) override;

  void OnCaptureStopRequested() override;

  void OnExitRequest();

  grpc::Status ReceiveCommandsAndSendEvents(
      ::grpc::ServerContext* context,
      ::grpc::ServerReaderWriter< ::orbit_grpc_protos::ReceiveCommandsAndSendEventsResponse,
                                  ::orbit_grpc_protos::ReceiveCommandsAndSendEventsRequest>* stream)
      override;

 private:
  void SendCommandsThread(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<orbit_grpc_protos::ReceiveCommandsAndSendEventsResponse,
                               orbit_grpc_protos::ReceiveCommandsAndSendEventsRequest>* stream,
      bool* all_events_sent_received, std::atomic<bool>* exit_send_commands_thread);

  void ReceiveEventsThread(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<orbit_grpc_protos::ReceiveCommandsAndSendEventsResponse,
                               orbit_grpc_protos::ReceiveCommandsAndSendEventsRequest>* stream,
      bool* all_events_sent_received);

 private:
  absl::flat_hash_set<grpc::ServerContext*> server_contexts_;
  absl::Mutex server_contexts_mutex_;

  enum class CaptureStatus { kCaptureStarted, kCaptureStopping, kCaptureFinished };
  struct ServiceState {
    CaptureStatus capture_status = CaptureStatus::kCaptureFinished;
    int32_t producers_remaining = 0;
    bool exit_requested = false;
  } service_state_;
  absl::Mutex service_state_mutex_;

  CaptureEventBuffer* capture_event_buffer_ = nullptr;
  absl::Mutex capture_event_buffer_mutex_;
};

}  // namespace orbit_service

#endif  // ORBIT_SERVICE_PRODUCER_SIDE_SERVICE_IMPL_H_
