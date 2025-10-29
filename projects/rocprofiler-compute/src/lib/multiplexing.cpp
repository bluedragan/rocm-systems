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

#include "multiplexing.h"

template<typename T>
void advance_dispatch_record_iterator(
    std::vector<rocprofiler_counter_config_id_t>& config_list,
    std::map<T, std::vector<rocprofiler_counter_config_id_t>::iterator>& curr_iterator) {
        auto next_it = std::next(curr_iterator);
        std::vector<rocprofiler_counter_config_id_t>::iterator begin_it = config_list.begin();
        curr_iterator = next_it != config_list.end() ? next_it : begin_it;
  }

void advance_dispatch_record_iterator(
    std::vector<rocprofiler_counter_config_id_t>& config_list,
    std::vector<rocprofiler_counter_config_id_t>::iterator& curr_iterator) {
        auto next_it = std::next(curr_iterator);
        std::vector<rocprofiler_counter_config_id_t>::iterator begin_it = config_list.begin();
        curr_iterator = next_it != config_list.end() ? next_it : begin_it;
  }

iteration_multiplexing_mode_t iteration_multiplexing_mode(const std::string& mode){
    if (mode == "simple")
        return iteration_multiplexing_mode_t::SIMPLE;
      else if (mode == "kernel")
        return iteration_multiplexing_mode_t::KERNEL;
      else if (mode == "launch")
        return iteration_multiplexing_mode_t::LAUNCH;
      else
        return iteration_multiplexing_mode_t::DISABLED;
}


bool search_counter_config_cache(
    const iteration_multiplexing_mode_t& mode,
    const uint64_t agent_id,
    const kernel_dispatch_info_t& dispatch_info,
    const std::unordered_map<uint64_t, std::vector<rocprofiler_counter_config_id_t>>& config_map,
    iteration_multiplexing_dispatch_record_t& dispatch_record) {
    
    auto pos = config_map.find(agent_id);
    if (pos == config_map.end() || pos->second.empty()) {
        return false;
    }

    auto config_list = pos->second;
    switch(mode) {
        case iteration_multiplexing_mode_t::KERNEL:
            return dispatch_record.kernel_config.find(dispatch_info.kernel_id) == dispatch_record.kernel_config.end() ? false : true;

        case iteration_multiplexing_mode_t::LAUNCH:
            return dispatch_record.dispatch_config.find(dispatch_info) == dispatch_record.dispatch_config.end() ? false : true;

        case iteration_multiplexing_mode_t::SIMPLE:
        default:
            return true;
    }
}

void set_counter_config(
    const iteration_multiplexing_mode_t& mode,
    const uint64_t agent_id,
    const kernel_dispatch_info_t& dispatch_info,
    const std::unordered_map<uint64_t, std::vector<rocprofiler_counter_config_id_t>>& config_map,
    iteration_multiplexing_dispatch_record_t& dispatch_record,
    rocprofiler_counter_config_id_t *config) {

    auto pos = config_map.find(agent_id);
    if (pos == config_map.end() || pos->second.empty()) {
        return;
    }

    auto config_list = pos->second;
    switch (mode) {
        case iteration_multiplexing_mode_t::SIMPLE:
            advance_dispatch_record_iterator(
                config_list,
                dispatch_record.config);
            *config = *dispatch_record.config;
            return;

        case iteration_multiplexing_mode_t::KERNEL:
            advance_dispatch_record_iterator(
                config_list,
                dispatch_record.kernel_config[dispatch_info.kernel_id]);
            *config = *dispatch_record.kernel_config[dispatch_info.kernel_id];
            return;
        case iteration_multiplexing_mode_t::LAUNCH:
            advance_dispatch_record_iterator(
                config_list,
                dispatch_record.dispatch_config[dispatch_info]);
            *config = *dispatch_record.dispatch_config[dispatch_info];
            return;

        default:
            *config = config_list[0];
            return;
    }
}