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

@Observable class Packet<T: File>: Identifiable, CustomStringConvertible, Encodable, Decodable {
  enum State: Int {
    case unknown, picked, loading, loaded, uploading, uploaded, received, downloading, downloaded
  }

  // This is the Identifiable.id used by SwiftUI, not the file ID used for identifying the packet
  // across devices and the cloud
  let id = UUID().uuidString

  var notificationToken: String? = nil
  var packetId: String = ""
  var files: [T] = []

  var receiver: String? = nil
  var sender: String? = nil
  var state: State = .unknown
  var expanded: Bool = true

  var description: String {
    if T.self == OutgoingFile.self {
      "Packet" + (receiver == nil ? "" : " for \(receiver!)")
    } else if T.self == IncomingFile.self {
      "Packet" + (sender == nil ? "" : " from \(sender!)")
    } else {
      "Packet"
    }
  }

  init() {}

  required init(from decoder: Decoder) throws {
    let container = try decoder.container(keyedBy: CodingKeys.self)
    packetId = try container.decode(String.self, forKey: .packetId)
    files = try Array(container.decode([String:T].self, forKey: .files).values)
  }

  func encode(to encoder: Encoder) throws {
    var container = encoder.container(keyedBy: CodingKeys.self)
    try container.encode(packetId, forKey: .packetId)

    let filesDict = files.reduce(into: [String:T]()) {
      (dict, file)  in
      dict[file.fileId] = file
    }

    try container.encode(filesDict, forKey: .files)
  }

  enum CodingKeys: String, CodingKey {
    case packetId, files, notificationToken
  }
}

extension Packet<IncomingFile> {
  /**
   Called when the packet is ready for downloading. newPacket is obtained from Firebase via
   either observing or pulling.
   */
  func update(from newPacket: Packet<IncomingFile>) {
    // Presumably, the new packet should contain the same information as our copy except
    // Packet.files.remotePath. But it's easier to just replace Packet.files altogether.
    files.removeAll()
    for file in newPacket.files {
      file.state = .uploaded
      files.append(file)
    }
    state = .uploaded
  }
  
  func download() async {
    if self.state != .uploaded {
      print("E: Packet is not uploaded before being downloaded.")
      return
    }

    self.state = .downloading
    await withTaskGroup(of: URL?.self) { group in
      // Time the download
      let beginTime = Date()

      // Make a task of downloading for each file
      for file in files {
        group.addTask {
          let result = await file.download()
          let duration: TimeInterval = Date().timeIntervalSince(beginTime)
          print("I: Downloaded. Size(b): \(file.fileSize). Time(s): \(duration).")
          return result
        }
      }

      // Wait for all files to finish downloading
      var allSucceeded = true
      for await (url) in group {
        allSucceeded = allSucceeded && (url != nil)
      }

      if allSucceeded {
        print("I: All files in the packet downloaded.")
        state = .downloaded
      } else {
        print("E: Some files in the packet failed to download.")
        state = .uploaded
      }
    }
  }
}

extension Packet<OutgoingFile> {
  func upload() async {
    if self.state != .loaded {
      print("E: Packet is not loaded before being uploaded.")
      return
    }

    self.state = .uploading
    await withTaskGroup(of: Int64?.self) { group in
      // Time the upload
      let beginTime = Date()

      // Make a task of uploading for each file
      for file in files {
        group.addTask {
          let size = await file.upload()
          if size != nil {
            let duration: TimeInterval = Date().timeIntervalSince(beginTime)
            print(String(format: "I: Uploaded file \(file.remotePath!). Size(b): \(file.fileSize). Time(s): %.1f.", duration))
            try? FileManager.default.removeItem(at: file.localUrl!)
          }
          return size
        }
      }

      // Wait for all files to finish uploading
      var allSucceeded = true
      for await (size) in group {
        allSucceeded = allSucceeded && (size != nil)
      }

      if allSucceeded {
        print("I: All files in the packet uploaded.")
        // Update packet state in Firebase
        // The update will automatically trigger a push notification
        let ref = await CloudDatabase.shared.push(packet: self)
        if ref != nil {
          print("I: Pushed packet \(self.packetId).")
          state = .uploaded
        } else {
          print("E: Failed to push packet to the database.")
          // Set the state back to .loaded for retrying later
          state = .loaded
        }
      } else {
        print("E: Some files in the packet failed to upload.")
        state = .loaded
      }
    }
  }
}
