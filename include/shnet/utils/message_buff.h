#pragma once

#include <errno.h>
#include <stdint.h>
#include <sys/uio.h>

#include <cstring>
#include <vector>

struct Message {
    char* data_;
    size_t size_;
};

class MessageBuffer {
   public:
    MessageBuffer(size_t size = DEFAULT_SIZE) : read_pos_(0), write_pos_(0) {
        buffer_.resize(size);
    }

    MessageBuffer(const MessageBuffer&) = delete;
    MessageBuffer& operator=(const MessageBuffer&) = delete;

    MessageBuffer(MessageBuffer&& other) noexcept
        : buffer_(std::move(other.buffer_)),
          read_pos_(other.read_pos_),
          write_pos_(other.write_pos_) {
        other.read_pos_ = 0;
        other.write_pos_ = 0;
    }

    MessageBuffer& operator=(MessageBuffer&& other) noexcept {
        if (this != &other) {
            buffer_ = std::move(other.buffer_);
            read_pos_ = other.read_pos_;
            write_pos_ = other.write_pos_;
            other.read_pos_ = 0;
            other.write_pos_ = 0;
        }
        return *this;
    }

    char* readPointer() { return buffer_.data() + read_pos_; }

    void readCommit(std::size_t size) { read_pos_ += size; }

    Message getAllData() { return {readPointer(), readableSize()}; }

    Message getDataUntil(char terminator = 0) {
        char* data = readPointer();
        std::size_t active_size = readableSize();
        for (std::size_t i = 0; i < active_size; ++i) {
            if (data[i] == terminator) {
                return {data, i};  // terminator not included
            }
        }
        return {nullptr, 0};  // not found
    }

    char* writePointer() { return buffer_.data() + write_pos_; }

    // upd write pos when writing data without using write(const void *, size_t)
    void writeCommit(std::size_t size) { write_pos_ += size; }

    // write data and commit
    void write(const void* data, std::size_t size) {
        if (size > 0) {
            prepare(size);
            memcpy(writePointer(), data, size);
            writeCommit(size);
        }
    }

    // size of data between read pos and write pos
    std::size_t readableSize() const { return write_pos_ - read_pos_; }

    // size of data that could be written directly
    std::size_t writableSize() const { return buffer_.size() - write_pos_; }

    // size of data that could be written after shrinking
    std::size_t getFreeSize() const { return getBufferSize() - readableSize(); }

    std::size_t getBufferSize() const { return buffer_.size(); }

    bool full() const { return readableSize() == getBufferSize(); }
    bool empty() const { return write_pos_ == read_pos_; }

    // move data to the beginning of the buffer to reserve place for write
    void shrink() {
        if (read_pos_) {
            memmove(buffer_.data(), buffer_.data() + read_pos_, readableSize());
            write_pos_ -= read_pos_;
            read_pos_ = 0;
        }
    }

    // ensure a write space of given size
    // may trigger resize
    void prepare(std::size_t size) {
        if (getFreeSize() < size) {
            // no enough room
            // shrink and expand
            shrink();
            buffer_.resize(buffer_.size() + std::max(size, buffer_.size() / 2));
        } else if (writableSize() < size) {
            // has enough room
            // not enough place to write
            shrink();
        }
    }

    static constexpr size_t DEFAULT_SIZE = 1 << 16;

   private:
    std::vector<char> buffer_;
    std::size_t read_pos_;
    std::size_t write_pos_;
};
