// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PLATFORM_V2_BASE_BASE_INPUT_STREAM_H_
#define PLATFORM_V2_BASE_BASE_INPUT_STREAM_H_

#include "platform_v2/base/byte_array.h"
#include "platform_v2/base/exception.h"
#include "platform_v2/base/input_stream.h"

namespace location {
namespace nearby {

// A base {@link InputStream } for reading the contents of a byte array.
class BaseInputStream : public InputStream {
 public:
  explicit BaseInputStream(ByteArray &buffer) : buffer_{buffer} {}
  BaseInputStream(const BaseInputStream &) = delete;
  BaseInputStream &operator=(const BaseInputStream &) = delete;
  ~BaseInputStream() override { Close(); }

  ExceptionOr<ByteArray> Read(std::int64_t size) override;

  Exception Close() override {
    // Do nothing.
    return {Exception::kSuccess};
  }

  std::uint8_t ReadUint8();
  std::uint16_t ReadUint16();
  std::uint32_t ReadUint32();
  std::uint64_t ReadUint64();
  bool IsAvailable(int size) const {
    return buffer_.size() - position_ >= size;
  }

 private:
  ByteArray ReadBytes(int size);

  ByteArray &buffer_;
  int position_{0};
};

}  // namespace nearby
}  // namespace location

#endif  // PLATFORM_V2_BASE_BASE_INPUT_STREAM_H_
