# Copyright (c) 2020 The Orbit Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

project(OrbitProducerSideChannel)

add_library(OrbitProducerSideChannel INTERFACE)
target_compile_options(OrbitProducerSideChannel INTERFACE ${STRICT_COMPILE_FLAGS})

target_sources(OrbitProducerSideChannel INTERFACE
        include/OrbitProducerSideChannel/ProducerSideChannel.h)

target_include_directories(OrbitProducerSideChannel INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
