// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_SERVICE_TARGET_SIDE_CHANNEL_H_
#define ORBIT_SERVICE_TARGET_SIDE_CHANNEL_H_

#include "grpcpp/grpcpp.h"

namespace orbit_service {

constexpr const char* kProducerSideUnixDomainSocketPath = "/tmp/orbit-producer-side-socket";

inline std::shared_ptr<grpc::Channel> CreateProducerSideChannel(
    std::string_view unix_domain_socket_path = kProducerSideUnixDomainSocketPath) {
  std::string server_address = absl::StrFormat("unix:%s", unix_domain_socket_path);
  return grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
}

}  // namespace orbit_service

#endif  // ORBIT_SERVICE_TARGET_SIDE_CHANNEL_H_
