//
//  Copyright 2023 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//

import Foundation

@Observable class OutgoingFile: Identifiable, Hashable, Encodable {
  enum State: Int {
    case picked, loading, loaded, uploading, uploaded
  }

  let id: UUID = UUID()
  
  let localPath: String
  var state: State

  @ObservationIgnored var fileSize: UInt64
  @ObservationIgnored var data: Data?
  @ObservationIgnored var remotePath: String?

  init(localPath: String, fileSize: UInt64 = 0, state: State = .picked, remotePath: String? = nil) {
    self.localPath = localPath
    self.fileSize = fileSize
    self.state = state
    self.remotePath = remotePath
  }

  func upload() -> Void {
    guard let data else {
      print("Data is not loaded. This should not happen.")
      return;
    }
    if state != .loaded {
      print("File is not loaded. This should not happen")
      return
    }
    // Since we use UUIDs for localPath on ios/macos, there's no risk of conflict. Let's just
    // use the same path for the remotePath
    remotePath = localPath
    state = .uploading

    CloudStorage.shared.upload(data, as: remotePath!) { [weak self] error in
      self?.state = error == nil ? .uploaded : .loaded
    }
  }

  static func == (lhs: OutgoingFile, rhs: OutgoingFile) -> Bool { lhs.id == rhs.id }
  func hash(into hasher: inout Hasher) { hasher.combine(self.id) }

  enum CodingKeys: String, CodingKey {
    case localPath, remotePath, fileSize
  }

  static func encodeOutgoingFiles(_ files: [OutgoingFile]) -> Data? {
    return try? JSONEncoder().encode(files)
  }
}
