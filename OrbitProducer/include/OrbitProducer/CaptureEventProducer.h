// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_PRODUCER_CAPTURE_EVENT_PRODUCER_H_
#define ORBIT_PRODUCER_CAPTURE_EVENT_PRODUCER_H_

#include <thread>

#include "absl/synchronization/mutex.h"
#include "producer_side_services.grpc.pb.h"

namespace orbit_producer {

class CaptureEventProducer {
 public:
  // Pure virtual destructor, but still with definition (in .cpp file), makes this class abstract.
  virtual ~CaptureEventProducer() = 0;

  [[nodiscard]] bool IsCapturing() { return is_capturing_; }

 protected:
  [[nodiscard]] virtual bool ConnectAndStart(std::string_view unix_domain_socket_path);
  virtual void ShutdownAndWait();

  virtual void OnCaptureStart();
  virtual void OnCaptureStop();

  [[nodiscard]] bool SendCaptureEvents(
      const orbit_grpc_protos::ReceiveCommandsAndSendEventsRequest& send_events_request);
  [[nodiscard]] bool NotifyAllEventsSent();

 private:
  void ConnectAndReceiveCommandsThread();

 private:
  std::unique_ptr<orbit_grpc_protos::ProducerSideService::Stub> producer_side_service_stub_;
  std::thread connect_and_receive_commands_thread_;

  std::unique_ptr<grpc::ClientContext> context_;
  std::unique_ptr<grpc::ClientReaderWriter<orbit_grpc_protos::ReceiveCommandsAndSendEventsRequest,
                                           orbit_grpc_protos::ReceiveCommandsAndSendEventsResponse>>
      stream_;
  absl::Mutex context_and_stream_mutex_;

  std::atomic<bool> is_capturing_ = false;

  bool shutdown_requested_ = false;
  absl::Mutex shutdown_requested_mutex_;
};

}  // namespace orbit_producer

#endif  // ORBIT_PRODUCER_CAPTURE_EVENT_PRODUCER_H_
