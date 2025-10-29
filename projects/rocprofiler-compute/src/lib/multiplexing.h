// MIT License
//
// Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <rocprofiler-sdk/rocprofiler.h>

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// Multiplexing modes enum
enum class iteration_multiplexing_mode_t {
  DISABLED,
  SIMPLE,
  KERNEL,
  LAUNCH
};

// Kernel dispatch info struct for iteration multiplexing
struct kernel_dispatch_info_t {
  uint64_t kernel_id;
  uint64_t queue_id;
  rocprofiler_dim3_t workgroup_size;
  rocprofiler_dim3_t grid_size;
  uint32_t LDS_memory_size;

  // Overload operator< for strict weak ordering
  bool operator<(const kernel_dispatch_info_t other) const {
    // Compare based on kernel_id first, then queue_id, then workgroup_size,
    // then grid_size, and finally LDS_memory_size
    return std::tie(
      kernel_id,
      queue_id,
      workgroup_size.x,
      workgroup_size.y,
      workgroup_size.z,
      grid_size.x,
      grid_size.y,
      grid_size.z,
      LDS_memory_size
    ) < std::tie(
      other.kernel_id,
      other.queue_id,
      other.workgroup_size.x,
      other.workgroup_size.y,
      other.workgroup_size.z,
      other.grid_size.x, 
      other.grid_size.y,
      other.grid_size.z,
      other.LDS_memory_size
    );
  }
};

// Iteration multiplexing data struct
union iteration_multiplexing_dispatch_record_t {
  std::vector<rocprofiler_counter_config_id_t>::iterator config;
  std::map<uint64_t, std::vector<rocprofiler_counter_config_id_t>::iterator> kernel_config;
  std::map<kernel_dispatch_info_t, std::vector<rocprofiler_counter_config_id_t>::iterator> dispatch_config;

  iteration_multiplexing_dispatch_record_t() {
    config = {};
  }

  ~iteration_multiplexing_dispatch_record_t() {
    // No dynamic memory to free
  }
};

iteration_multiplexing_mode_t iteration_multiplexing_mode(const std::string& mode);

bool search_counter_config_cache(
    const iteration_multiplexing_mode_t& mode,
    const uint64_t agent_id,
    const kernel_dispatch_info_t& dispatch_info,
    const std::unordered_map<uint64_t, std::vector<rocprofiler_counter_config_id_t>>& config_map,
    iteration_multiplexing_dispatch_record_t& dispatch_record);

void set_counter_config(
    const iteration_multiplexing_mode_t& mode,
    const uint64_t agent_id,
    const kernel_dispatch_info_t& dispatch_info,
    const std::unordered_map<uint64_t, std::vector<rocprofiler_counter_config_id_t>>& config_map,
    iteration_multiplexing_dispatch_record_t& dispatch_record,
    rocprofiler_counter_config_id_t *config);

