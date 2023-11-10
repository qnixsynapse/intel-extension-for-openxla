/* Copyright (c) 2023 Intel Corporation

Copyright 2015 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "xla/stream_executor/sycl/sycl_platform.h"

#include <algorithm>
#include <string>
#include <utility>

#include "absl/base/call_once.h"
#include "absl/base/const_init.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "tsl/platform/errors.h"
#include "tsl/platform/status.h"
#include "xla/stream_executor/platform/initialize.h"
#include "xla/stream_executor/sycl/sycl_executor.h"
#include "xla/stream_executor/sycl/sycl_platform_id.h"

namespace stream_executor {
namespace gpu {

SyclPlatform::SyclPlatform()
    : name_("SYCL"), min_numa_node_(0), limit_numa_node_(0) {}

SyclPlatform::~SyclPlatform() {}

// Due to legacy issues in user code, we can't currently call InpectNumaNodes
// at module initialization time, because non-GPU programs still include this
// plugin via various methods, so instead, it has to be init-on-reference.
void SyclPlatform::InspectNumaNodes() {
  // To get NUMA node information, we need to create all executors, so we can
  // examine their device descriptions to see their bus assignments.
  static absl::once_flag once;
  absl::call_once(once, [&] {
    for (int i = 0; i < VisibleDeviceCount(); i++) {
      StreamExecutor* exec = *ExecutorForDevice(i);
      if (i == 0) {
        // NUMA nodes may not start at 0, so set the minimum node  based on the
        // first executor we see.
        min_numa_node_ = exec->GetDeviceDescription().numa_node();
        limit_numa_node_ = min_numa_node_ + 1;
      } else {
        min_numa_node_ =
            std::min(min_numa_node_, exec->GetDeviceDescription().numa_node());
        limit_numa_node_ = std::max(
            limit_numa_node_, exec->GetDeviceDescription().numa_node() + 1);
      }
    }
  });
}

int SyclPlatform::BusCount() {
  InspectNumaNodes();
  return limit_numa_node_ - min_numa_node_;
}

int SyclPlatform::DeviceToBus(int device_ordinal) {
  StreamExecutor* exec = *ExecutorForDevice(device_ordinal);
  return exec->GetDeviceDescription().numa_node() - min_numa_node_;
}

tsl::StatusOr<StreamExecutor*> SyclPlatform::FirstExecutorForBus(
    int bus_ordinal) {
  InspectNumaNodes();
  CHECK_LT(bus_ordinal, BusCount()) << "bus ordinal out of available range";
  for (int i = 0; i < VisibleDeviceCount(); i++) {
    if (DeviceToBus(i) == bus_ordinal) {
      return *ExecutorForDevice(i);
    }
  }

  return tsl::Status(
      absl::StatusCode::kNotFound,
      absl::StrFormat("Executor for bus %d not found.", bus_ordinal));
}

Platform::Id SyclPlatform::id() const { return sycl::kSyclPlatformId; }

int SyclPlatform::VisibleDeviceCount() const {
  return GpuDriver::GetDeviceCount();
}

const std::string& SyclPlatform::Name() const { return name_; }

tsl::StatusOr<std::unique_ptr<DeviceDescription>>
SyclPlatform::DescriptionForDevice(int ordinal) const {
  return GpuExecutor::CreateDeviceDescription(ordinal);
}

tsl::StatusOr<StreamExecutor*> SyclPlatform::ExecutorForDevice(int ordinal) {
  StreamExecutorConfig config;
  config.ordinal = ordinal;
  config.device_options = DeviceOptions::Default();
  return GetExecutor(config);
}

tsl::StatusOr<StreamExecutor*> SyclPlatform::GetExecutor(
    const StreamExecutorConfig& config) {
  if (config.gpu_stream) {
    // If the GPU stream was provided, it's not possible to get-or-create a
    // stream with a required pointer: so we are looking for previously
    // allocated streams.
    return executor_cache_.Get(config);
  }
  return executor_cache_.GetOrCreate(
      config, [&]() { return GetUncachedExecutor(config); });
}

tsl::StatusOr<std::unique_ptr<StreamExecutor>>
SyclPlatform::GetUncachedExecutor(const StreamExecutorConfig& config) {
  auto executor = std::make_unique<StreamExecutor>(
      this, std::make_unique<GpuExecutor>(), config.ordinal);
  auto init_status = executor->Init(config.device_options);
  if (!init_status.ok()) {
    return tsl::Status(
        absl::StatusCode::kInternal,
        absl::StrFormat(
            "failed initializing StreamExecutor for CUDA device ordinal %d: %s",
            config.ordinal, init_status.ToString()));
  }

  return std::move(executor);
}

}  // namespace gpu

static void InitializeSyclPlatform() {
  // Disabling leak checking, MultiPlatformManager does not destroy its
  // registered platforms.
  std::unique_ptr<gpu::SyclPlatform> platform(new gpu::SyclPlatform);
  TF_CHECK_OK(MultiPlatformManager::RegisterPlatform(std::move(platform)));
}

}  // namespace stream_executor

REGISTER_MODULE_INITIALIZER(sycl_platform,
                            stream_executor::InitializeSyclPlatform());

// Note that module initialization sequencing is not supported in the
// open-source project, so this will be a no-op there.
REGISTER_MODULE_INITIALIZER_SEQUENCE(sycl_platform, multi_platform_manager);
REGISTER_MODULE_INITIALIZER_SEQUENCE(multi_platform_manager_listener,
                                     sycl_platform);
