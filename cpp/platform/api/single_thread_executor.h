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

#ifndef PLATFORM_API_SINGLE_THREAD_EXECUTOR_H_
#define PLATFORM_API_SINGLE_THREAD_EXECUTOR_H_

#include "platform/api/submittable_executor.h"

namespace location {
namespace nearby {

// An Executor that uses a single worker thread operating off an unbounded
// queue.
//
// https://docs.oracle.com/javase/8/docs/api/java/util/concurrent/Executors.html#newSingleThreadExecutor--
class SingleThreadExecutor : public SubmittableExecutor {
 public:
  ~SingleThreadExecutor() override = default;
};

}  // namespace nearby
}  // namespace location

#endif  // PLATFORM_API_SINGLE_THREAD_EXECUTOR_H_
