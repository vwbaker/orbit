// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_PRODUCER_CAPTURE_EVENT_PRODUCER_H_
#define ORBIT_PRODUCER_CAPTURE_EVENT_PRODUCER_H_

#include <thread>

#include "absl/synchronization/mutex.h"
#include "grpcpp/grpcpp.h"
#include "producer_side_services.grpc.pb.h"

namespace orbit_producer {

// This abstract class offers the subclasses methods
// to connect and communicate with a ProducerSideService.
class CaptureEventProducer {
 public:
  // Pure virtual destructor, but still with definition (in .cpp file), makes this class abstract.
  virtual ~CaptureEventProducer() = 0;

  [[nodiscard]] bool IsCapturing() { return is_capturing_; }

 protected:
  // This method establishes the connection with ProducerSideService.
  // If a connection fails or is interrupted, this class will keep trying to reconnect.
  // Subclasses can extend this method by overriding it, but must also call the overridden method.
  virtual void BuildAndStart(const std::shared_ptr<grpc::Channel>& channel);
  // This method closes the connection with ProducerSideService.
  // Subclasses can extend this method by overriding it, but must also call the overridden method.
  virtual void ShutdownAndWait();

  // Subclasses can override this method to be notified of a request to start a capture.
  virtual void OnCaptureStart();
  // Subclasses can override this method to be notified of a request to stop the capture.
  virtual void OnCaptureStop();

  // Subclasses can use this method to send a batch of CaptureEvents to the ProducerSideService.
  // A full ReceiveCommandsAndSendEventsRequest with event_case() == kBufferedCaptureEvents
  // needs to be passed to avoid an extra copy from a BufferedCaptureEvents.
  [[nodiscard]] bool SendCaptureEvents(
      const orbit_grpc_protos::ReceiveCommandsAndSendEventsRequest& send_events_request);
  // Subclasses should use this methods to notify the ProducerSideService that
  // they have sent all their CaptureEvents after the capture has been stopped.
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
