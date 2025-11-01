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
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "ptrace_session.hpp"

#include "lib/common/environment.hpp"
#include "lib/common/logging.hpp"
#include "lib/common/static_object.hpp"

#include <rocprofiler-sdk-rocattach/defines.h>
#include <rocprofiler-sdk-rocattach/rocattach.h>
#include <rocprofiler-sdk-rocattach/types.h>

#include <map>
#include <unordered_map>

extern char** environ;

namespace common = ::rocprofiler::common;

namespace rocprofiler
{
namespace rocattach
{
namespace
{
using session_t      = rocprofiler::rocattach::PTraceSession;
using session_list_t = std::map<int, session_t>;

void
initialize_logging()
{
    auto logging_cfg = rocprofiler::common::logging_config{.install_failure_handler = true};
    common::init_logging("ROCATTACH", logging_cfg);
    FLAGS_colorlogtostderr = true;
}

session_list_t*
get_sessions()
{
    static auto*& session_list = rocprofiler::common::static_object<session_list_t>::construct();
    return session_list;
}

// Helper function to allocate memory in target process and write data
rocattach_status_t
write_data_to_target(session_t&                  session,
                     const std::string&          description,
                     const std::vector<uint8_t>& data,
                     void*&                      allocated_addr)
{
    // Allocate memory in target process
    auto status = ROCATTACH_STATUS_SUCCESS;
    status      = session.simple_mmap(allocated_addr, data.size());
    if(status != ROCATTACH_STATUS_SUCCESS)
    {
        ROCP_ERROR << "Failed to allocate memory for " << description << " in target process";
        return status;
    }
    ROCP_TRACE << "Allocated memory for " << description << " at " << allocated_addr;

    // Write data to target process memory
    status = session.write(reinterpret_cast<size_t>(allocated_addr), data, data.size());
    if(status != ROCATTACH_STATUS_SUCCESS)
    {
        ROCP_ERROR << "Failed to write " << description << " to target process";
        return status;
    }
    ROCP_TRACE << "Wrote " << description << " to target process";
    return status;
}

// Helper function to build environment buffer
std::vector<uint8_t>
build_environment_buffer()
{
    std::vector<uint8_t> environment_buffer(4);
    uint32_t             var_count = 0;

    char** invars = environ;
    for(; *invars; invars++)
    {
        const char* var = *invars;
        if(strncmp("ROCP", var, 4) != 0)
        {
            // only take envvars starting with ROCP
            continue;
        }

        var_count++;
        ROCP_TRACE << "Adding to environment buffer: " << var;

        // Add variable name
        while(*var != '=')
        {
            environment_buffer.emplace_back(*var++);
        }
        environment_buffer.emplace_back(0);

        // Add variable value
        var++;
        while(*var != 0)
        {
            environment_buffer.emplace_back(*var++);
        }
        environment_buffer.emplace_back(0);
    }

    // Store count in first 4 bytes
    const uint8_t* var_count_bytes = reinterpret_cast<uint8_t*>(&var_count);
    std::copy(var_count_bytes, var_count_bytes + 4, environment_buffer.data());

    return environment_buffer;
}

rocattach_status_t
setup(int pid)
{
    // Setup attachement for rocprofiler
    ROCP_TRACE << "Attachment library attach function called for pid " << pid;

    auto sessions = CHECK_NOTNULL(get_sessions());

    if(sessions->count(pid) > 0)
    {
        ROCP_ERROR << "[rocprofiler-sdk-rocattach] attach called for a pid with an active "
                      "attachment session";
        return ROCATTACH_STATUS_INVALID_ARGUMENT;
    }

    sessions->emplace(pid, pid);
    auto& session = sessions->at(pid);
    auto  status  = ROCATTACH_STATUS_SUCCESS;

    ROCP_TRACE << "Attempting attachment to pid " << pid;
    status = session.attach();
    if(status != ROCATTACH_STATUS_SUCCESS)
    {
        ROCP_ERROR << "Attachment failed to pid " << pid;
        return status;
    }
    ROCP_TRACE << "Attachment success to pid " << pid;

    // Build and write environment buffer to target process
    auto  environment_buffer      = build_environment_buffer();
    void* environment_buffer_addr = nullptr;
    status                        = write_data_to_target(
        session, "environment buffer", environment_buffer, environment_buffer_addr);
    if(status != ROCATTACH_STATUS_SUCCESS)
    {
        return status;
    }

    // Build and write tool library path to target process
    auto tool_lib_path_env =
        rocprofiler::common::get_env("ROCPROF_ATTACH_TOOL_LIBRARY", "librocprofiler-sdk-tool.so");
    const char* tool_lib_path = tool_lib_path_env.c_str();
    ROCP_TRACE << "Tool library path: " << tool_lib_path;

    size_t               tool_lib_path_len = strlen(tool_lib_path) + 1;
    std::vector<uint8_t> tool_lib_buffer(tool_lib_path, tool_lib_path + tool_lib_path_len);

    void* tool_lib_path_addr = nullptr;
    status =
        write_data_to_target(session, "tool library path", tool_lib_buffer, tool_lib_path_addr);
    if(status != ROCATTACH_STATUS_SUCCESS)
    {
        return status;
    }

    uint64_t retval = 0;
    // Execute the attach function with both parameters
    status = session.call_function("librocprofiler-register.so",
                                   "rocprofiler_register_attach",
                                   retval,
                                   environment_buffer_addr,
                                   tool_lib_path_addr);
    if(status != ROCATTACH_STATUS_SUCCESS)
    {
        ROCP_ERROR << "Failed to call attach function in target process " << pid;
        return status;
    }
    else if(retval != 0)
    {
        ROCP_ERROR << "Attach function returned non-zero status in target process " << pid
                   << ". status: " << retval;
        return ROCATTACH_STATUS_ERROR;
    }

    // Clean up - free the environment buffer and tool library path memory in target process
    status = session.simple_munmap(environment_buffer_addr, environment_buffer.size());
    if(status != ROCATTACH_STATUS_SUCCESS)
    {
        ROCP_ERROR << "Failed to free environment buffer memory in target process";
        // Continue anyway since the main operation succeeded
    }
    ROCP_TRACE << "Cleaned up tool environment memory in target process";

    status = session.simple_munmap(tool_lib_path_addr, tool_lib_path_len);
    if(status != ROCATTACH_STATUS_SUCCESS)
    {
        ROCP_ERROR << "Failed to free tool library path memory in target process";
        // Continue anyway since the main operation succeeded
    }
    ROCP_TRACE << "Cleaned up tool library path memory in target process";
    return ROCATTACH_STATUS_SUCCESS;
}

rocattach_status_t
teardown(int pid)
{
    // Setup attachement for rocprofiler
    ROCP_TRACE << "Attachment library detach function called for pid " << pid;

    auto sessions = CHECK_NOTNULL(get_sessions());

    if(sessions->count(pid) == 0)
    {
        ROCP_ERROR << "[rocprofiler-sdk-rocattach] detach called for a pid with no active "
                      "attachment session";
        return ROCATTACH_STATUS_INVALID_ARGUMENT;
    }

    auto& session = sessions->at(pid);
    auto  status  = ROCATTACH_STATUS_SUCCESS;

    uint64_t retval = 0;
    // Execute the attach function with both parameters
    status =
        session.call_function("librocprofiler-register.so", "rocprofiler_register_detach", retval);
    if(status != ROCATTACH_STATUS_SUCCESS)
    {
        ROCP_ERROR << "Failed to call detach function in target process " << pid
                   << ". Status code: " << status;
        // continue to detach anyways
    }
    else if(retval != 0)
    {
        ROCP_ERROR << "Detach function returned non-zero status in target process " << pid
                   << ". status: " << retval;
        // continue to detach anyways
    }

    session.detach();
    if(status != ROCATTACH_STATUS_SUCCESS)
    {
        ROCP_ERROR << "Error during detach for pid " << pid;
        return status;
    }
    return ROCATTACH_STATUS_SUCCESS;
}

}  // namespace
}  // namespace rocattach
}  // namespace rocprofiler

ROCATTACH_EXTERN_C_INIT

rocattach_status_t
attach(int pid) ROCATTACH_PUBLIC_API;

rocattach_status_t
detach(int pid) ROCATTACH_PUBLIC_API;

rocattach_status_t
attach(int pid)
{
    rocprofiler::rocattach::initialize_logging();
    auto status = rocprofiler::rocattach::setup(pid);
    if(status != ROCATTACH_STATUS_SUCCESS)
    {
        ROCP_ERROR << "attach failed with error code " << status;
        return status;
    }
    return ROCATTACH_STATUS_SUCCESS;
}

rocattach_status_t
detach(int pid)
{
    if(pid != 0)
    {
        return rocprofiler::rocattach::teardown(pid);
    }
    else
    {
        for(auto& myPair : *(CHECK_NOTNULL(rocprofiler::rocattach::get_sessions())))
        {
            int pid = myPair.first;
            rocprofiler::rocattach::teardown(pid);
        }
        return ROCATTACH_STATUS_SUCCESS;
    }
}

ROCATTACH_EXTERN_C_FINI
