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

#ifndef PLATFORM_V2_PUBLIC_FILE_H_
#define PLATFORM_V2_PUBLIC_FILE_H_

#include <cstdint>
#include <memory>
#include <string>

#include "platform_v2/api/input_file.h"
#include "platform_v2/api/output_file.h"
#include "platform_v2/api/platform.h"
#include "platform_v2/base/byte_array.h"
#include "platform_v2/base/exception.h"

namespace location {
namespace nearby {

class InputFile final : public api::InputFile {
 public:
  using Platform = api::ImplementationPlatform;
  InputFile(std::int64_t payload_id, std::int64_t size)
      : impl_(Platform::CreateInputFile(payload_id, size)) {}
  ~InputFile() override = default;
  InputFile(InputFile&&) = default;
  InputFile& operator=(InputFile&&) = default;

  ExceptionOr<ByteArray> Read(std::int64_t size) override {
    return impl_->Read(size);
  }
  std::string GetFilePath() const override { return impl_->GetFilePath(); }
  std::int64_t GetTotalSize() const override { return impl_->GetTotalSize(); }
  Exception Close() override { return impl_->Close(); }

 private:
  std::unique_ptr<api::InputFile> impl_;
};

class OutputFile final : public api::OutputFile {
 public:
  using Platform = api::ImplementationPlatform;
  explicit OutputFile(std::int64_t payload_id)
      : impl_(Platform::CreateOutputFile(payload_id)) {}
  ~OutputFile() override = default;
  OutputFile(OutputFile&&) = default;
  OutputFile& operator=(OutputFile&&) = default;

  Exception Write(const ByteArray& data) override { return impl_->Write(data); }
  Exception Flush() override { return impl_->Flush(); }
  Exception Close() override { return impl_->Close(); }

 private:
  std::unique_ptr<api::OutputFile> impl_;
};

}  // namespace nearby
}  // namespace location

#endif  // PLATFORM_V2_PUBLIC_FILE_H_
