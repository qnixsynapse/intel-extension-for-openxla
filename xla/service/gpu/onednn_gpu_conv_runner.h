/* Copyright (c) 2024 Intel Corporation

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

#ifndef XLA_SERVICE_GPU_ONEDNN_GPU_CONV_RUNNER_H_
#define XLA_SERVICE_GPU_ONEDNN_GPU_CONV_RUNNER_H_

#include <optional>

#include "xla/ffi/ffi.h"
#include "xla/service/gpu/cublas_cudnn.h"
#include "xla/stream_executor/gpu/gpu_stream.h"

namespace xla {

namespace gpu {

absl::Status RunGpuConv(se::Stream*, const ffi::Dictionary&,
                        absl::Span<const ffi::BufferBase>, ffi::BufferBase&,
                        se::ScratchAllocator*, CudnnConvKind);

}  // namespace gpu
}  // namespace xla

#endif  // XLA_SERVICE_GPU_ONEDNN_GPU_CONV_RUNNER_H_
