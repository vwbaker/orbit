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
  orbit_grpc_protos::CaptureEvent TranslateIntermediateEvent(
      std::string&& /*intermediate_event*/) override {
    return orbit_grpc_protos::CaptureEvent{};
  }
};

class LockFreeBufferCaptureEventProducerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_service.emplace();

    grpc::ServerBuilder builder;
    builder.RegisterService(&fake_service.value());
    fake_server = builder.BuildAndStart();
    ASSERT_NE(fake_server, nullptr);

    std::shared_ptr<grpc::Channel> channel =
        fake_server->InProcessChannel(grpc::ChannelArguments{});

    buffer_producer.emplace();
    buffer_producer->BuildAndStart(channel);

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
  EXPECT_FALSE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  fake_service->SendStartCaptureCommand();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_TRUE(buffer_producer->IsCapturing());

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(::testing::Between(1, 3));
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  EXPECT_TRUE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  EXPECT_TRUE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  EXPECT_TRUE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(1);
  fake_service->SendStopCaptureCommand();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_FALSE(buffer_producer->IsCapturing());

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  EXPECT_FALSE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
}

TEST_F(LockFreeBufferCaptureEventProducerTest, EnqueueIntermediateEvent) {
  EXPECT_FALSE(buffer_producer->IsCapturing());

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(1);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  buffer_producer->EnqueueIntermediateEvent("");
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  fake_service->SendStartCaptureCommand();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_TRUE(buffer_producer->IsCapturing());

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(::testing::Between(1, 3));
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  buffer_producer->EnqueueIntermediateEvent("");
  buffer_producer->EnqueueIntermediateEvent("");
  buffer_producer->EnqueueIntermediateEvent("");
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(1);
  fake_service->SendStopCaptureCommand();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_FALSE(buffer_producer->IsCapturing());

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(1);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  buffer_producer->EnqueueIntermediateEvent("");
}

TEST_F(LockFreeBufferCaptureEventProducerTest, UnexpectedStartStopCaptureCommands) {
  EXPECT_FALSE(buffer_producer->IsCapturing());

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  EXPECT_FALSE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  fake_service->SendStartCaptureCommand();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_TRUE(buffer_producer->IsCapturing());

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(::testing::Between(1, 3));
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  EXPECT_TRUE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  EXPECT_TRUE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  EXPECT_TRUE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  // This should have no effect.
  fake_service->SendStartCaptureCommand();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_TRUE(buffer_producer->IsCapturing());

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(::testing::Between(1, 2));
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  EXPECT_TRUE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  EXPECT_TRUE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(1);
  fake_service->SendStopCaptureCommand();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_FALSE(buffer_producer->IsCapturing());

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  EXPECT_FALSE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  // This should have no effect.
  fake_service->SendStopCaptureCommand();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_FALSE(buffer_producer->IsCapturing());

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  EXPECT_FALSE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
}

TEST_F(LockFreeBufferCaptureEventProducerTest, ServiceDisconnects) {
  EXPECT_FALSE(buffer_producer->IsCapturing());

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  EXPECT_FALSE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  fake_service->SendStartCaptureCommand();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_TRUE(buffer_producer->IsCapturing());

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(::testing::Between(1, 3));
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  EXPECT_TRUE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  EXPECT_TRUE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  EXPECT_TRUE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
  std::this_thread::sleep_for(kWaitMessagesSentDuration);

  ::testing::Mock::VerifyAndClearExpectations(&*fake_service);

  // Disconnect.
  fake_service->FinishRpc();
  std::this_thread::sleep_for(kWaitMessagesSentDuration);
  EXPECT_FALSE(buffer_producer->IsCapturing());

  EXPECT_CALL(*fake_service, OnCaptureEventsReceived).Times(0);
  EXPECT_CALL(*fake_service, OnAllEventsSentReceived).Times(0);
  EXPECT_FALSE(buffer_producer->EnqueueIntermediateEventIfCapturing([] { return ""; }));
}

}  // namespace orbit_producer
