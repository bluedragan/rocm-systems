/* Copyright (c) 2025 Advanced Micro Devices, Inc.

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE. */

#ifndef ASYNC_LOGGER_HPP_
#define ASYNC_LOGGER_HPP_

#include "ring_buffer.hpp"
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <condition_variable>
#include <cstdarg>

namespace amd {
namespace logging {

//! \brief Configuration for async logger
struct AsyncLoggerConfig {
  bool enabled = true;                    // Enable async logging
  size_t buffer_size_per_thread = 65536;  // 64KB per thread
  size_t flush_interval_ms = 50;          // Flush every 50ms
  bool flush_on_error = true;             // Flush immediately on errors
  const char* log_file_path = nullptr;    // nullptr = stderr
};

//! \brief Main async logger class - singleton pattern
class AsyncLogger {
 public:
  static AsyncLogger& GetInstance();

  // Initialize the logger
  void Initialize(const AsyncLoggerConfig& config);

  // Shutdown the logger (flushes all buffers)
  void Shutdown();

  // Log a message (fast path - lock-free for registered threads)
  void WriteLog(uint16_t level, uint32_t mask, const char* file, uint32_t line,
                const char* format, va_list args);

  // Force immediate flush (synchronous)
  void FlushSync();

  // Request async flush
  void FlushAsync();

  // Check if async logging is enabled
  bool IsEnabled() const { return config_.enabled; }

  // Get configuration
  const AsyncLoggerConfig& GetConfig() const { return config_; }

  // Get statistics
  struct Stats {
    uint64_t total_logs;
    uint64_t total_dropped;
    uint64_t total_bytes_written;
    size_t active_threads;
  };
  Stats GetStats() const;

 private:
  AsyncLogger();
  ~AsyncLogger();

  // Non-copyable
  AsyncLogger(const AsyncLogger&) = delete;
  AsyncLogger& operator=(const AsyncLogger&) = delete;

  // Get or create thread-local buffer
  RingBuffer* GetThreadBuffer();

  // Background flusher thread
  void FlusherThread();

  // Flush all thread buffers
  void FlushAllBuffers();

  // Write batch to output
  void WriteBatch(const std::vector<char>& batch);

  // Format log entry for output
  void FormatLogEntry(std::vector<char>& output, uint64_t timestamp_us,
                     uint32_t thread_id, uint32_t pid, uint16_t level,
                     uint32_t mask, const char* file, uint32_t line,
                     const char* message);

  AsyncLoggerConfig config_;
  std::atomic<bool> initialized_;
  std::atomic<bool> shutdown_;

  // Thread management
  std::thread flusher_thread_;
  std::condition_variable flush_cv_;
  std::mutex flush_mutex_;
  std::atomic<bool> flush_requested_;

  // Thread-local buffers
  std::mutex buffers_mutex_;
  std::vector<RingBuffer*> thread_buffers_;

  // Thread-local storage key
  static thread_local RingBuffer* tls_buffer_;

  // Output management
  int output_fd_;
  FILE* output_file_;

  // I/O backend (public forward declaration for derived classes)
 public:
  class IOBackend;
 private:
  std::unique_ptr<IOBackend> io_backend_;

  // Statistics
  std::atomic<uint64_t> total_logs_;
  std::atomic<uint64_t> total_bytes_;
};

//! \brief Fast logging function for async path
void async_log_printf(uint16_t level, uint32_t mask, const char* file,
                     uint32_t line, const char* format, ...);

}  // namespace logging
}  // namespace amd

#endif  // ASYNC_LOGGER_HPP_

