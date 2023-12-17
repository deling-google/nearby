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

import SwiftUI
import PhotosUI

struct OutgoingFilesView: View {
  let model: Endpoint

  @EnvironmentObject var mainModel: Main
  @Environment(\.dismiss) var dismiss

  @State private var photosPicked: [PhotosPickerItem] = []
  
  func onMediaPicked() -> Void {
//    model.outgoingFiles.removeAll()
//
//    guard let directoryUrl = try? FileManager.default.url(
//      for: .documentDirectory,
//      in: .userDomainMask,
//      appropriateFor: nil,
//      create: true) else {
//      let error = NSError(domain: "Failed to obtain directory for downloading.", code: 1)
//      return
//    }
//
//    for photo in photosPicked {
//      let type =
//        photo.supportedContentTypes.first(
//          where: {$0.preferredMIMEType == "image/jpeg"}) ??
//        photo.supportedContentTypes.first(
//          where: {$0.preferredMIMEType == "image/png"})
//
//      let file = OutgoingFile(
//        mimeType: type?.preferredMIMEType ?? "",
//        fileSize: 0, // We don't know the file size yet. Will fill it once loaded.
//        state: .loading)
//      model.outgoingFiles.append(file)
//
//      Task { [file] in
//        guard let data = try? await photo.loadTransferable(type: Data.self) else {
//          return
//        }
//        var typedData: Data
//        if file.mimeType == "image/jpeg" {
//          guard let uiImage = UIImage(data: data) else {
//            return
//          }
//          guard let jpegData = uiImage.jpegData(compressionQuality: 1) else {
//            return
//          }
//          typedData  = jpegData
//          file.fileSize = UInt64(jpegData.count)
//        }
//        else if file.mimeType == "image/png" {
//          guard let uiImage = UIImage(data: data) else {
//            return
//          }
//          guard let pngData = uiImage.pngData() else {
//            return
//          }
//          typedData  = pngData
//          file.fileSize = UInt64(pngData.count)
//        }
//        else {
//          typedData = data
//          file.fileSize = UInt64(data.count)
//        }
//
//
//        let url = directoryUrl.appendingPathComponent(UUID().uuidString)
//        do {
//          try typedData.write(to:url)
//        } catch {
//          return;
//        }
//        file.localUrl = url
//        file.state = .loaded
//      }
//    }
  }

  func upload() -> Void {
//    for file in model.outgoingFiles {
//      if file.state == .loaded {
//        let beginTime = Date()
//        file.upload() { [beginTime] size, error in
//          let duration: TimeInterval = Date().timeIntervalSince(beginTime)
//          model.transfers.append(
//            Transfer(
//              direction: .upload,
//              remotePath: file.remotePath!,
//              result: .success,
//              size: Int(file.fileSize),
//              duration: duration))
//            try? FileManager.default.removeItem(at: file.localUrl!)
//        }
//      }
//    }
  }
  
  func send() -> Void {
//    guard let payload = OutgoingFile.encodeOutgoingFiles(model.outgoingFiles) else {
//      print("Failed to encode outgoing files. This should not happen.")
//      return
//    }
//
//    Main.shared.sendFiles(payload, to: model.id)
//
//    for file in model.outgoingFiles {
//      model.transfers.append(Transfer(direction: .send, remotePath: file.remotePath!, result: .success, to: model.id))
//    }
  }
  
  var body: some View {
    // TODO: make the buttons float at the button; add a vertical scrollbar
    Form {
//      Section {
//        HStack{
//          PhotosPicker(selection: $photosPicked, matching: .images, photoLibrary: .shared()) {
//            Label("Pick", systemImage: "doc.fill.badge.plus").frame(maxWidth: .infinity)
//          } // Can pick files only when no files are loading or uploading
////          .disabled(!model.outgoingFiles.allSatisfy({$0.state != .loading && $0.state != .uploading}))
//          .onChange(of: photosPicked, onMediaPicked)
//          
//          Button(action: upload) {
//            Label("Upload", systemImage: "arrow.up.circle.fill").frame(maxWidth: .infinity)
//          } // Can upload only when all files are loaded in memory
////          .disabled(model.outgoingFiles.isEmpty || !model.outgoingFiles.allSatisfy({$0.state == .loaded}))
//          
//          Button(action: send) {
//            Label("Send", systemImage: "arrow.up.backward.circle.fill").frame(maxWidth: .infinity)
//          } // Can send only when we're connected and all files are uploaded to the cloud
////          .disabled( model.state != .connected ||
////                     model.outgoingFiles.isEmpty ||
////                     !model.outgoingFiles.allSatisfy({$0.state == .uploaded}))
//        }.buttonStyle(.bordered).fixedSize()
        
        List {
//          ForEach(model.outgoingFiles) { file in
//            HStack {
//              // TODO: replace placholder with thumbnail
//              Image(systemName: "photo")
//              Text(file.remotePath!)
//              Spacer()
//              switch file.state {
//              case .picked:
//                Image(systemName: "circle.dotted").foregroundColor(.gray)
//              case .loading:
//                ProgressView()
//              case .loaded:
//                Image(systemName: "circle.fill").foregroundColor(.gray)
//              case .uploading:
//                ProgressView()
//              case .uploaded:
//                Image(systemName: "circle.fill").foregroundColor(.green)
//              }
//            }
//          }
//        }
      }
    }
    .navigationTitle("Outgoing files")
    .onChange(of: mainModel.endpoints) { _, endpoints in
      if !endpoints.contains(model) { dismiss() }
    }
  }
}

//#Preview {
//  let files: [OutgoingFile] =
//  [
////    OutgoingFile(mimeType: "image/jpeg", fileSize: 4000000, state: .loading),
////    OutgoingFile(mimeType: "image/jpeg", fileSize: 5000000, state: .uploading),
////    OutgoingFile(mimeType: "image/jpeg", fileSize: 5000000, state: .uploaded, remotePath: "1234567890ABCDEF"),
////    OutgoingFile(mimeType: "image/jpeg", fileSize: 4000000, state: .loaded),
////    OutgoingFile(mimeType: "image/jpeg", fileSize: 4000000, state: .picked)
//  ]
//  
//  OutgoingFilesView(
//    model: Endpoint(
//      id: "R2D2",
//      name: "Debug droid",
//      isIncoming: false, 
//      state: .discovered,
//      outgoingPackets: [
//        Packet<OutgoingFile>(notificationToken: "12345", files: [])
//      ]
//    )
//  ).environment(Main.createDebugModel())
//}
