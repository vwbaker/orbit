// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "FakeProducerSideService.h"
#include "OrbitProducer/CaptureEventProducer.h"
#include "absl/strings/str_format.h"
#include "grpcpp/grpcpp.h"
#include "producer_side_services.grpc.pb.h"

using ::testing::InSequence;

using orbit_grpc_protos::ReceiveCommandsAndSendEventsRequest;
using orbit_grpc_protos::ReceiveCommandsAndSendEventsResponse;

namespace orbit_producer {

namespace {

class CaptureEventProducerImpl : public CaptureEventProducer {
 public:
  // Override and forward these methods to make them public.
  [[nodiscard]] bool ConnectAndStart(std::string_view unix_domain_socket_path) override {
    return CaptureEventProducer::ConnectAndStart(unix_domain_socket_path);
  }

  void ShutdownAndWait() override { CaptureEventProducer::ShutdownAndWait(); }

  // Hide and forward these methods to make them public.
  [[nodiscard]] bool SendCaptureEvents(
      const orbit_grpc_protos::ReceiveCommandsAndSendEventsRequest& send_events_request) {
    return CaptureEventProducer::SendCaptureEvents(send_events_request);
  }

  [[nodiscard]] bool NotifyAllEventsSent() { return CaptureEventProducer::NotifyAllEventsSent(); }

  MOCK_METHOD(void, OnCaptureStart, (), (override));
  MOCK_METHOD(void, OnCaptureStop, (), (override));
};

class CaptureEventProducerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static const std::string kUnixDomainSocketPath = "./capture-event-producer-test-socket";

    grpc::ServerBuilder builder;
    builder.AddListeningPort(absl::StrFormat("unix:%s", kUnixDomainSocketPath),
                             grpc::InsecureServerCredentials());

    fake_service.emplace();
    builder.RegisterService(&fake_service.value());
    fake_server = builder.BuildAndStart();
    ASSERT_NE(fake_server, nullptr);

    producer.emplace();
    ASSERT_TRUE(producer->ConnectAndStart(kUnixDomainSocketPath));

    // Leave some time for the ReceiveCommandsAndSendEvents RPC to actually happen.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  void TearDown() override {
    // Leave some time for all pending communication to be finish.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    producer->ShutdownAndWait();
    producer.reset();

    fake_service->Done();
    fake_server->Shutdown();
    fake_server->Wait();

    fake_service.reset();
    fake_server.reset();
  }

  std::optional<FakeProducerSideService> fake_service;
  std::unique_ptr<grpc::Server> fake_server;
  std::optional<CaptureEventProducerImpl> producer;
};

}  // namespace

TEST_F(CaptureEventProducerTest, CalledOnCaptureStartStop) {
  {
    InSequence in_sequence;
    EXPECT_CALL(*producer, OnCaptureStart).Times(1);
    EXPECT_CALL(*producer, OnCaptureStop).Times(1);
    EXPECT_CALL(*producer, OnCaptureStart).Times(1);
    EXPECT_CALL(*producer, OnCaptureStop).Times(1);
  }

  fake_service->SendStartCaptureCommand();
  fake_service->SendStartCaptureCommand();
  fake_service->SendStopCaptureCommand();
  fake_service->SendStopCaptureCommand();

  fake_service->SendStartCaptureCommand();
  fake_service->SendStopCaptureCommand();
}

TEST_F(CaptureEventProducerTest, SentCaptureEventsAndAllEventsSent) {
  {
    InSequence in_sequence;
    EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(2);
    EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(1);
  }

  ReceiveCommandsAndSendEventsRequest send_events_request;
  send_events_request.mutable_buffered_capture_events()
      ->mutable_capture_events()
      ->Add()
      ->mutable_gpu_queue_submission();
  EXPECT_TRUE(producer->SendCaptureEvents(send_events_request));
  EXPECT_TRUE(producer->SendCaptureEvents(send_events_request));
  EXPECT_TRUE(producer->NotifyAllEventsSent());
}

}  // namespace orbit_producer
