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

#ifndef RING_BUFFER_HPP_
#define RING_BUFFER_HPP_

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

namespace amd {
namespace logging {

//! \brief Lock-free single-producer, single-consumer ring buffer
//! Optimized for per-thread logging with minimal contention
class RingBuffer {
 public:
  struct LogEntry {
    uint64_t timestamp_us;    // Timestamp in microseconds
    uint32_t thread_id;       // Thread ID
    uint32_t pid;             // Process ID
    uint16_t level;           // Log level
    uint32_t mask;            // Log mask (uint32_t for large values like LOG_MEM_POOL)
    uint16_t msg_length;      // Message length
    uint16_t file_length;     // Filename length
    uint32_t line;            // Line number
    // Followed by: filename + message (variable length)
  };

  explicit RingBuffer(size_t capacity = 64 * 1024)  // Default 64KB
      : capacity_(capacity),
        buffer_(new char[capacity]),
        write_pos_(0),
        read_pos_(0),
        dropped_count_(0) {
    // Ensure capacity is power of 2 for efficient masking
    if ((capacity & (capacity - 1)) != 0) {
      size_t pow2 = 1;
      while (pow2 < capacity) pow2 <<= 1;
      capacity_ = pow2;
      buffer_.reset(new char[capacity_]);
    }
  }

  ~RingBuffer() = default;

  // Non-copyable, non-movable
  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator=(const RingBuffer&) = delete;

  //! \brief Try to write a log entry to the ring buffer
  //! \return true if written, false if buffer full
  bool TryWrite(uint64_t timestamp_us, uint32_t thread_id, uint32_t pid,
                uint16_t level, uint32_t mask, const char* file, uint32_t line,
                const char* message) {
    const uint16_t file_len = file ? static_cast<uint16_t>(strlen(file)) : 0;
    const uint16_t msg_len = message ? static_cast<uint16_t>(strlen(message)) : 0;
    const size_t total_size = sizeof(LogEntry) + file_len + msg_len;

    // Check if we have space (leave some padding for safety)
    size_t write_pos = write_pos_.load(std::memory_order_relaxed);
    size_t read_pos = read_pos_.load(std::memory_order_acquire);
    size_t available = (read_pos <= write_pos)
                           ? (capacity_ - (write_pos - read_pos))
                           : (read_pos - write_pos);

    if (available < total_size + sizeof(LogEntry)) {
      dropped_count_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    // Write header
    size_t pos = write_pos & (capacity_ - 1);
    LogEntry* entry = reinterpret_cast<LogEntry*>(buffer_.get() + pos);

    // Handle wrap-around for header
    if (pos + sizeof(LogEntry) > capacity_) {
      // Split write
      size_t first_part = capacity_ - pos;
      memcpy(buffer_.get() + pos, &timestamp_us, sizeof(uint64_t));
      pos = 0;
      // Continue with rest of header at position 0
      char temp_header[sizeof(LogEntry)];
      LogEntry* temp = reinterpret_cast<LogEntry*>(temp_header);
      temp->timestamp_us = timestamp_us;
      temp->thread_id = thread_id;
      temp->pid = pid;
      temp->level = level;
      temp->mask = mask;
      temp->msg_length = msg_len;
      temp->file_length = file_len;
      temp->line = line;
      memcpy(buffer_.get(), temp_header, sizeof(LogEntry));
      pos = sizeof(LogEntry) - first_part;
    } else {
      entry->timestamp_us = timestamp_us;
      entry->thread_id = thread_id;
      entry->pid = pid;
      entry->level = level;
      entry->mask = mask;
      entry->msg_length = msg_len;
      entry->file_length = file_len;
      entry->line = line;
      pos = (pos + sizeof(LogEntry)) & (capacity_ - 1);
    }

    // Write filename
    if (file_len > 0) {
      WriteData(pos, file, file_len);
      pos = (pos + file_len) & (capacity_ - 1);
    }

    // Write message
    if (msg_len > 0) {
      WriteData(pos, message, msg_len);
    }

    // Commit write
    write_pos_.store(write_pos + total_size, std::memory_order_release);
    return true;
  }

  //! \brief Read available data from ring buffer
  //! \param callback Function to call for each log entry
  //! \return Number of entries read
  template <typename Callback>
  size_t ReadAvailable(Callback callback) {
    size_t count = 0;
    size_t read_pos = read_pos_.load(std::memory_order_relaxed);
    size_t write_pos = write_pos_.load(std::memory_order_acquire);

    while (read_pos < write_pos) {
      size_t available = write_pos - read_pos;
      if (available < sizeof(LogEntry)) break;

      size_t pos = read_pos & (capacity_ - 1);

      // Read header (handle wrap-around)
      LogEntry entry;
      if (pos + sizeof(LogEntry) > capacity_) {
        // Split read
        size_t first_part = capacity_ - pos;
        memcpy(&entry, buffer_.get() + pos, first_part);
        memcpy(reinterpret_cast<char*>(&entry) + first_part, buffer_.get(),
               sizeof(LogEntry) - first_part);
        pos = sizeof(LogEntry) - first_part;
      } else {
        memcpy(&entry, buffer_.get() + pos, sizeof(LogEntry));
        pos = (pos + sizeof(LogEntry)) & (capacity_ - 1);
      }

      // Read filename
      char filename[256] = {0};
      if (entry.file_length > 0 && entry.file_length < sizeof(filename)) {
        ReadData(pos, filename, entry.file_length);
        pos = (pos + entry.file_length) & (capacity_ - 1);
      }

      // Read message
      char message[4096] = {0};
      if (entry.msg_length > 0 && entry.msg_length < sizeof(message)) {
        ReadData(pos, message, entry.msg_length);
      }

      // Invoke callback
      callback(entry.timestamp_us, entry.thread_id, entry.pid, entry.level,
               entry.mask, filename, entry.line, message);

      size_t total_size = sizeof(LogEntry) + entry.file_length + entry.msg_length;
      read_pos += total_size;
      count++;
    }

    read_pos_.store(read_pos, std::memory_order_release);
    return count;
  }

  //! \brief Get number of dropped messages
  uint64_t GetDroppedCount() const {
    return dropped_count_.load(std::memory_order_relaxed);
  }

  //! \brief Reset dropped count
  void ResetDroppedCount() {
    dropped_count_.store(0, std::memory_order_relaxed);
  }

  //! \brief Check if buffer is empty
  bool IsEmpty() const {
    return read_pos_.load(std::memory_order_acquire) >=
           write_pos_.load(std::memory_order_acquire);
  }

  //! \brief Get buffer utilization (0.0 to 1.0)
  float GetUtilization() const {
    size_t write_pos = write_pos_.load(std::memory_order_acquire);
    size_t read_pos = read_pos_.load(std::memory_order_acquire);
    size_t used = write_pos - read_pos;
    return static_cast<float>(used) / capacity_;
  }

 private:
  void WriteData(size_t pos, const char* data, size_t len) {
    if (pos + len <= capacity_) {
      memcpy(buffer_.get() + pos, data, len);
    } else {
      // Wrap around
      size_t first_part = capacity_ - pos;
      memcpy(buffer_.get() + pos, data, first_part);
      memcpy(buffer_.get(), data + first_part, len - first_part);
    }
  }

  void ReadData(size_t pos, char* data, size_t len) {
    if (pos + len <= capacity_) {
      memcpy(data, buffer_.get() + pos, len);
    } else {
      // Wrap around
      size_t first_part = capacity_ - pos;
      memcpy(data, buffer_.get() + pos, first_part);
      memcpy(data + first_part, buffer_.get(), len - first_part);
    }
  }

  size_t capacity_;
  std::unique_ptr<char[]> buffer_;
  std::atomic<size_t> write_pos_;
  std::atomic<size_t> read_pos_;
  std::atomic<uint64_t> dropped_count_;
};

}  // namespace logging
}  // namespace amd

#endif  // RING_BUFFER_HPP_

