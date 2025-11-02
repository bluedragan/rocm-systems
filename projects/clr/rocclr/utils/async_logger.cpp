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

#include "async_logger.hpp"
#include "os/os.hpp"
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <inttypes.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

// No special headers needed - using writev only

namespace amd {

// Import maxLogSize from debug.cpp (AMD_LOG_LEVEL_SIZE * Mi)
extern const size_t maxLogSize;

namespace logging {

// Thread-local storage for ring buffer
thread_local RingBuffer* AsyncLogger::tls_buffer_ = nullptr;

//! \brief I/O Backend abstraction
class AsyncLogger::IOBackend {
 public:
  virtual ~IOBackend() = default;
  virtual bool Initialize(int fd) = 0;
  virtual void Shutdown() = 0;
  virtual ssize_t WriteBatch(const struct iovec* iov, int iovcnt) = 0;
};

//! \brief writev based backend (simple and fast)
class WritevBackend : public AsyncLogger::IOBackend {
 public:
  WritevBackend() : fd_(-1) {}
  ~WritevBackend() override = default;

  bool Initialize(int fd) override {
    fd_ = fd;
    return fd_ >= 0;
  }

  void Shutdown() override {
    // Nothing to do
  }

  ssize_t WriteBatch(const struct iovec* iov, int iovcnt) override {
    if (fd_ < 0 || iovcnt == 0) return -1;

    // writev can handle up to IOV_MAX vectors
    const int max_iov = 1024;  // Safe limit
    ssize_t total = 0;

    for (int i = 0; i < iovcnt; i += max_iov) {
      int count = std::min(max_iov, iovcnt - i);
      ssize_t written = writev(fd_, &iov[i], count);
      if (written < 0) return written;
      total += written;
    }

    return total;
  }

 private:
  int fd_;
};

// ================================================================================================
AsyncLogger& AsyncLogger::GetInstance() {
  static AsyncLogger instance;
  return instance;
}

// ================================================================================================
AsyncLogger::AsyncLogger()
    : initialized_(false),
      shutdown_(false),
      flush_requested_(false),
      output_fd_(-1),
      output_file_(nullptr),
      total_logs_(0),
      total_bytes_(0) {
}

// ================================================================================================
AsyncLogger::~AsyncLogger() {
  Shutdown();
}

// ================================================================================================
void AsyncLogger::Initialize(const AsyncLoggerConfig& config) {
  if (initialized_.exchange(true)) {
    return;  // Already initialized
  }

  config_ = config;

  if (!config_.enabled) {
    return;
  }

  // Open output file
  if (config_.log_file_path) {
    output_fd_ = open(config_.log_file_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (output_fd_ < 0) {
      output_fd_ = STDERR_FILENO;
    }
  } else {
    output_fd_ = STDERR_FILENO;
  }

  // Initialize I/O backend using writev
  auto writev_backend = std::make_unique<WritevBackend>();
  writev_backend->Initialize(output_fd_);
  io_backend_ = std::move(writev_backend);

  // Start flusher thread
  shutdown_.store(false);
  flusher_thread_ = std::thread(&AsyncLogger::FlusherThread, this);

  // Set thread name for debugging
#ifdef __linux__
  pthread_setname_np(flusher_thread_.native_handle(), "amd_log_flush");
#endif
}

// ================================================================================================
void AsyncLogger::Shutdown() {
  if (!initialized_.load() || shutdown_.exchange(true)) {
    return;
  }

  // Wake up flusher thread
  {
    std::lock_guard<std::mutex> lock(flush_mutex_);
    flush_requested_.store(true);
  }
  flush_cv_.notify_one();

  // Wait for flusher thread
  if (flusher_thread_.joinable()) {
    flusher_thread_.join();
  }

  // Final flush
  FlushAllBuffers();

  // Cleanup I/O backend
  if (io_backend_) {
    io_backend_->Shutdown();
    io_backend_.reset();
  }

  // Close output file
  if (output_fd_ >= 0 && output_fd_ != STDERR_FILENO) {
    close(output_fd_);
    output_fd_ = -1;
  }

  // Cleanup thread buffers
  {
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    for (auto* buffer : thread_buffers_) {
      delete buffer;
    }
    thread_buffers_.clear();
  }
}

// ================================================================================================
RingBuffer* AsyncLogger::GetThreadBuffer() {
  if (tls_buffer_) {
    return tls_buffer_;
  }

  // Create new buffer for this thread
  auto* buffer = new RingBuffer(config_.buffer_size_per_thread);
  tls_buffer_ = buffer;

  // Register buffer
  {
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    thread_buffers_.push_back(buffer);
  }

  return buffer;
}

// ================================================================================================
void AsyncLogger::WriteLog(uint16_t level, uint32_t mask, const char* file,
                           uint32_t line, const char* format, va_list args) {
  if (!config_.enabled) {
    return;
  }

  // Format message
  char message[4096];
  vsnprintf(message, sizeof(message), format, args);

  // Get thread-local buffer
  RingBuffer* buffer = GetThreadBuffer();
  if (!buffer) {
    return;
  }

  // Get timestamp and thread info
  uint64_t timestamp_us = Os::timeNanos() / 1000ULL;
  uint32_t thread_id = static_cast<uint32_t>(
      std::hash<std::thread::id>{}(std::this_thread::get_id()));
  uint32_t pid = Os::getProcessId();

  // Try to write to buffer
  bool success = buffer->TryWrite(timestamp_us, thread_id, pid, level, mask,
                                  file, line, message);

  if (success) {
    total_logs_.fetch_add(1, std::memory_order_relaxed);

    // Flush immediately on errors if configured
    if (config_.flush_on_error && level <= 1) {  // LOG_ERROR or LOG_NONE
      FlushAsync();
    }
  } else {
    // Buffer full - trigger flush
    FlushAsync();

    // Retry once
    buffer->TryWrite(timestamp_us, thread_id, pid, level, mask, file, line, message);
  }
}

// ================================================================================================
void AsyncLogger::FlushSync() {
  FlushAllBuffers();
}

// ================================================================================================
void AsyncLogger::FlushAsync() {
  flush_requested_.store(true, std::memory_order_release);
  flush_cv_.notify_one();
}

// ================================================================================================
void AsyncLogger::FlusherThread() {
  std::unique_lock<std::mutex> lock(flush_mutex_);

  while (!shutdown_.load(std::memory_order_acquire)) {
    // Wait for flush request or timeout
    flush_cv_.wait_for(lock, std::chrono::milliseconds(config_.flush_interval_ms),
                      [this] {
                        return flush_requested_.load(std::memory_order_acquire) ||
                               shutdown_.load(std::memory_order_acquire);
                      });

    flush_requested_.store(false, std::memory_order_relaxed);

    // Unlock while flushing
    lock.unlock();
    FlushAllBuffers();
    lock.lock();
  }
}

// ================================================================================================
void AsyncLogger::FlushAllBuffers() {
  std::vector<char> batch;
  batch.reserve(1024 * 1024);  // 1MB batch buffer

  std::vector<RingBuffer*> buffers_snapshot;
  {
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    buffers_snapshot = thread_buffers_;
  }

  // Read from all thread buffers
  for (auto* buffer : buffers_snapshot) {
    buffer->ReadAvailable([this, &batch](uint64_t timestamp_us, uint32_t thread_id,
                                        uint32_t pid, uint16_t level, uint16_t mask,
                                        const char* file, uint32_t line,
                                        const char* message) {
      FormatLogEntry(batch, timestamp_us, thread_id, pid, level, mask, file, line, message);
    });
  }

  // Write batch
  if (!batch.empty()) {
    WriteBatch(batch);
  }
}

// ================================================================================================
void AsyncLogger::WriteBatch(const std::vector<char>& batch) {
  if (batch.empty() || !io_backend_) {
    return;
  }

  // Check if log file needs truncation (matches truncate_log_file() behavior)
  if (output_fd_ > STDERR_FILENO && config_.log_file_path) {  // Only for actual files, not stderr
    off_t current_pos = lseek(output_fd_, 0, SEEK_CUR);
    if (current_pos >= 0) {
      // Get file size
      off_t file_size = lseek(output_fd_, 0, SEEK_END);

      // Check against AMD_LOG_LEVEL_SIZE limit (in bytes)
      if (file_size > static_cast<off_t>(amd::maxLogSize)) {
        // Truncate by reopening file in write mode
        close(output_fd_);
        output_fd_ = open(config_.log_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (output_fd_ < 0) {
          output_fd_ = STDERR_FILENO;  // Fall back to stderr on error
        }
        // File is now at position 0 (empty)
      } else {
        // Restore original position
        lseek(output_fd_, current_pos, SEEK_SET);
      }
    }
  }

  struct iovec iov;
  iov.iov_base = const_cast<char*>(batch.data());
  iov.iov_len = batch.size();

  ssize_t written = io_backend_->WriteBatch(&iov, 1);
  if (written > 0) {
    total_bytes_.fetch_add(written, std::memory_order_relaxed);
  }
}

// ================================================================================================
void AsyncLogger::FormatLogEntry(std::vector<char>& output, uint64_t timestamp_us,
                                uint32_t thread_id, uint32_t pid, uint16_t level,
                                uint32_t mask, const char* file, uint32_t line,
                                const char* message) {
  char buffer[8192];
  int len;

  // Format similar to existing log format
  if (config_.buffer_size_per_thread >= 4) {  // LOG_DEBUG level shows pid/tid
    len = snprintf(buffer, sizeof(buffer),
                  ":%d:%-25s:%-4d: %010" PRIu64 " us: [pid:%d tid:0x%x] %s\n",
                  level, file, line, timestamp_us, pid, thread_id, message);
  } else {
    len = snprintf(buffer, sizeof(buffer),
                  ":%d:%-25s:%-4d: %010" PRIu64 " us: %s\n",
                  level, file, line, timestamp_us, message);
  }

  if (len > 0 && len < (int)sizeof(buffer)) {
    output.insert(output.end(), buffer, buffer + len);
  }
}

// ================================================================================================
AsyncLogger::Stats AsyncLogger::GetStats() const {
  Stats stats;
  stats.total_logs = total_logs_.load(std::memory_order_relaxed);
  stats.total_bytes_written = total_bytes_.load(std::memory_order_relaxed);
  stats.total_dropped = 0;

  {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(buffers_mutex_));
    stats.active_threads = thread_buffers_.size();

    for (auto* buffer : thread_buffers_) {
      stats.total_dropped += buffer->GetDroppedCount();
    }
  }

  return stats;
}

// ================================================================================================
void async_log_printf(uint16_t level, uint32_t mask, const char* file,
                     uint32_t line, const char* format, ...) {
  va_list args;
  va_start(args, format);
  AsyncLogger::GetInstance().WriteLog(level, mask, file, line, format, args);
  va_end(args);
}

}  // namespace logging
}  // namespace amd

