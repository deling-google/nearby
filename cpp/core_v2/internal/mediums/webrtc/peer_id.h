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

#ifndef CORE_V2_INTERNAL_MEDIUMS_WEBRTC_PEER_ID_H_
#define CORE_V2_INTERNAL_MEDIUMS_WEBRTC_PEER_ID_H_

#include <memory>
#include <string>

#include "platform_v2/base/byte_array.h"

namespace location {
namespace nearby {
namespace connections {
namespace mediums {

// PeerId is used as an identifier to exchange SDP messages to establish WebRTC
// p2p connection.
class PeerId {
 public:
  explicit PeerId(const string& id) : id_(id) {}
  ~PeerId() = default;

  static PeerId FromRandom();
  static PeerId FromSeed(const ByteArray& seed);

  const string& GetId() const { return id_; }

 private:
  const string id_;
};

}  // namespace mediums
}  // namespace connections
}  // namespace nearby
}  // namespace location

#endif  // CORE_V2_INTERNAL_MEDIUMS_WEBRTC_PEER_ID_H_
