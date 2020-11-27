// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "OrbitProducer/CaptureEventProducer.h"

#include "OrbitBase/Logging.h"

using orbit_grpc_protos::ProducerSideService;
using orbit_grpc_protos::ReceiveCommandsAndSendEventsRequest;
using orbit_grpc_protos::ReceiveCommandsAndSendEventsResponse;

namespace orbit_producer {

CaptureEventProducer::~CaptureEventProducer() = default;

void CaptureEventProducer::BuildAndStart(const std::shared_ptr<grpc::Channel>& channel) {
  CHECK(channel != nullptr);

  producer_side_service_stub_ = ProducerSideService::NewStub(channel);
  CHECK(producer_side_service_stub_ != nullptr);

  connect_and_receive_commands_thread_ = std::thread{[this] { ConnectAndReceiveCommandsThread(); }};
}

void CaptureEventProducer::ShutdownAndWait() {
  {
    absl::WriterMutexLock lock{&shutdown_requested_mutex_};
    CHECK(!shutdown_requested_);
    shutdown_requested_ = true;
  }

  {
    absl::ReaderMutexLock lock{&context_and_stream_mutex_};
    if (context_ != nullptr) {
      LOG("Attempting to disconnect from ProducerSideService as exit was requested");
      context_->TryCancel();
    }
  }

  CHECK(connect_and_receive_commands_thread_.joinable());
  connect_and_receive_commands_thread_.join();

  producer_side_service_stub_ = nullptr;
}

void CaptureEventProducer::OnCaptureStart() { LOG("CaptureEventProducer called OnCaptureStart"); }

void CaptureEventProducer::OnCaptureStop() { LOG("CaptureEventProducer called OnCaptureStop"); }

bool CaptureEventProducer::SendCaptureEvents(
    const orbit_grpc_protos::ReceiveCommandsAndSendEventsRequest& send_events_request) {
  CHECK(send_events_request.event_case() ==
        orbit_grpc_protos::ReceiveCommandsAndSendEventsRequest::kBufferedCaptureEvents);

  CHECK(producer_side_service_stub_ != nullptr);
  {
    // Acquiring the mutex just for the CHECK might seem expensive,
    // but the gRPC call that follows is orders of magnitude slower.
    absl::ReaderMutexLock lock{&shutdown_requested_mutex_};
    CHECK(!shutdown_requested_);
  }

  bool write_succeeded;
  {
    absl::ReaderMutexLock lock{&context_and_stream_mutex_};
    if (stream_ == nullptr) {
      ERROR("Sending BufferedCaptureEvents to ProducerSideService: not connected");
      return false;
    }
    write_succeeded = stream_->Write(send_events_request);
  }
  if (!write_succeeded) {
    ERROR("Sending BufferedCaptureEvents to ProducerSideService");
  }
  return write_succeeded;
}

bool CaptureEventProducer::NotifyAllEventsSent() {
  CHECK(producer_side_service_stub_ != nullptr);
  {
    absl::ReaderMutexLock lock{&shutdown_requested_mutex_};
    CHECK(!shutdown_requested_);
  }

  orbit_grpc_protos::ReceiveCommandsAndSendEventsRequest all_events_sent_request;
  all_events_sent_request.mutable_all_events_sent();
  bool write_succeeded;
  {
    absl::ReaderMutexLock lock{&context_and_stream_mutex_};
    if (stream_ == nullptr) {
      ERROR("Sending AllEventsSent to ProducerSideService: not connected");
      return false;
    }
    write_succeeded = stream_->Write(all_events_sent_request);
  }
  if (write_succeeded) {
    LOG("Sent AllEventsSent to ProducerSideService");
  } else {
    ERROR("Sending AllEventsSent to ProducerSideService");
  }
  return write_succeeded;
}

void CaptureEventProducer::ConnectAndReceiveCommandsThread() {
  CHECK(producer_side_service_stub_ != nullptr);
  static constexpr absl::Duration kRetryConnectingDelay = absl::Seconds(5);

  while (true) {
    {
      absl::ReaderMutexLock lock{&shutdown_requested_mutex_};
      if (shutdown_requested_) {
        break;
      }
    }

    // Attempt to connect to ProducerSideService. Note that getting a stream_ != nullptr doesn't
    // mean that the service is listening nor that the connection is actually established.
    {
      absl::ReaderMutexLock lock{&context_and_stream_mutex_};
      context_ = std::make_unique<grpc::ClientContext>();
      stream_ = producer_side_service_stub_->ReceiveCommandsAndSendEvents(context_.get());
    }

    if (stream_ == nullptr) {
      ERROR(
          "Calling ReceiveCommandsAndSendEvents to establish "
          "gRPC connection with ProducerSideService");
      // This is the reason why we protect shutdown_requested_ with a Mutex
      // instead of using an std::atomic<bool>: so that we can use LockWhenWithTimeout.
      if (shutdown_requested_mutex_.ReaderLockWhenWithTimeout(
              absl::Condition(
                  +[](bool* shutdown_requested) { return *shutdown_requested; },
                  &shutdown_requested_),
              kRetryConnectingDelay)) {
        shutdown_requested_mutex_.ReaderUnlock();
      }
      continue;
    }
    LOG("Called ReceiveCommandsAndSendEvents on ProducerSideService");

    while (true) {
      ReceiveCommandsAndSendEventsResponse response;
      bool read_succeeded;
      {
        absl::ReaderMutexLock lock{&context_and_stream_mutex_};
        read_succeeded = stream_->Read(&response);
      }
      if (!read_succeeded) {
        ERROR("Receiving ReceiveCommandsAndSendEventsResponse from ProducerSideService");
        if (is_capturing_) {
          is_capturing_ = false;
          OnCaptureStop();
        }
        LOG("Terminating call to ReceiveCommandsAndSendEvents");
        {
          absl::WriterMutexLock lock{&context_and_stream_mutex_};
          stream_->Finish();
          context_ = nullptr;
          stream_ = nullptr;
        }

        // Wait before trying to reconnect, to avoid continuously trying to reconnect
        // when OrbitService is not reachable.
        if (shutdown_requested_mutex_.ReaderLockWhenWithTimeout(
                absl::Condition(
                    +[](bool* shutdown_requested) { return *shutdown_requested; },
                    &shutdown_requested_),
                kRetryConnectingDelay)) {
          shutdown_requested_mutex_.ReaderUnlock();
        }
        break;
      }

      switch (response.command_case()) {
        case ReceiveCommandsAndSendEventsResponse::kStartCaptureCommand: {
          LOG("ProducerSideService sent StartCaptureCommand");
          if (!is_capturing_) {
            is_capturing_ = true;
            OnCaptureStart();
          }
        } break;

        case ReceiveCommandsAndSendEventsResponse::kStopCaptureCommand: {
          LOG("ProducerSideService sent StopCaptureCommand");
          if (is_capturing_) {
            is_capturing_ = false;
            OnCaptureStop();
          }
        } break;

        case ReceiveCommandsAndSendEventsResponse::COMMAND_NOT_SET: {
          ERROR("ProducerSideService sent COMMAND_NOT_SET");
        } break;
      }
    }
  }
}

}  // namespace orbit_producer
