// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "FakeProducerSideService.h"
#include "OrbitProducer/LockFreeBufferCaptureEventProducer.h"
#include "absl/strings/str_format.h"
#include "grpcpp/grpcpp.h"
#include "producer_side_services.grpc.pb.h"

namespace orbit_producer {

namespace {

class LockFreeBufferCaptureEventProducerImpl
    : public LockFreeBufferCaptureEventProducer<std::string> {
 protected:
  orbit_grpc_protos::CaptureEvent TranslateIntermediateEvent(std::string&&) override {
    return orbit_grpc_protos::CaptureEvent{};
  }
};

class LockFreeBufferCaptureEventProducerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static const std::string kUnixDomainSocketPath = "./orbit-producer-tests-socket";

    grpc::ServerBuilder builder;
    builder.AddListeningPort(absl::StrFormat("unix:%s", kUnixDomainSocketPath),
                             grpc::InsecureServerCredentials());

    fake_service.emplace();
    builder.RegisterService(&fake_service.value());
    fake_server = builder.BuildAndStart();
    ASSERT_NE(fake_server, nullptr);

    buffer_producer.emplace();
    ASSERT_TRUE(buffer_producer->ConnectAndStart(kUnixDomainSocketPath));

    // Leave some time for the ReceiveCommandsAndSendEvents RPC to actually happen.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  void TearDown() override {
    // Leave some time for all pending communication to finish.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    buffer_producer->ShutdownAndWait();
    buffer_producer.reset();

    fake_service->FinishRpc();
    fake_server->Shutdown();
    fake_server->Wait();

    fake_service.reset();
    fake_server.reset();
  }

  std::optional<FakeProducerSideService> fake_service;
  std::unique_ptr<grpc::Server> fake_server;
  std::optional<LockFreeBufferCaptureEventProducerImpl> buffer_producer;
};

constexpr std::chrono::duration kWaitMessagesSentDuration = std::chrono::milliseconds(25);

}  // namespace

TEST_F(LockFreeBufferCaptureEventProducerTest, EnqueueIntermediateEventIfCapturing) {
  EXPECT_FALSE(buffer_producer->IsCapturing());

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; });
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClear(&*fake_service);

  fake_service->SendStartCaptureCommand();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_TRUE(buffer_producer->IsCapturing());

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(::testing::Between(1, 3));
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; });
  buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; });
  buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; });
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClear(&*fake_service);

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(1);

  fake_service->SendStopCaptureCommand();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_FALSE(buffer_producer->IsCapturing());

  ::testing::Mock::VerifyAndClear(&*fake_service);

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; });
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
}

TEST_F(LockFreeBufferCaptureEventProducerTest, EnqueueIntermediateEvent) {
  EXPECT_FALSE(buffer_producer->IsCapturing());

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(1);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  buffer_producer->EnqueueIntermediateEvent("");
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClear(&*fake_service);

  fake_service->SendStartCaptureCommand();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_TRUE(buffer_producer->IsCapturing());

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(::testing::Between(1, 3));
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  buffer_producer->EnqueueIntermediateEvent("");
  buffer_producer->EnqueueIntermediateEvent("");
  buffer_producer->EnqueueIntermediateEvent("");
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClear(&*fake_service);

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(1);

  fake_service->SendStopCaptureCommand();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_FALSE(buffer_producer->IsCapturing());

  ::testing::Mock::VerifyAndClear(&*fake_service);

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(1);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  buffer_producer->EnqueueIntermediateEvent("");
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
}

}  // namespace orbit_producer
