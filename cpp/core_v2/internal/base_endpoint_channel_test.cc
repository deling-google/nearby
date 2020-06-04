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

#include "core_v2/internal/base_endpoint_channel.h"

#include <utility>

#include "core_v2/internal/encryption_runner.h"
#include "platform_v2/base/byte_array.h"
#include "platform_v2/base/input_stream.h"
#include "platform_v2/base/output_stream.h"
#include "platform_v2/public/count_down_latch.h"
#include "platform_v2/public/logging.h"
#include "platform_v2/public/multi_thread_executor.h"
#include "platform_v2/public/pipe.h"
#include "platform_v2/public/single_thread_executor.h"
#include "proto/connections_enums.pb.h"
#include "proto/connections_enums.pb.h"
#include "securegcm/d2d_connection_context_v1.h"
#include "securegcm/ukey2_handshake.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace location {
namespace nearby {
namespace connections {
namespace {

using ::location::nearby::proto::connections::DisconnectionReason;
using ::location::nearby::proto::connections::Medium;

class TestEndpointChannel : public BaseEndpointChannel {
 public:
  explicit TestEndpointChannel(InputStream* input, OutputStream* output)
      : BaseEndpointChannel("channel", input, output) {}

  MOCK_METHOD(Medium, GetMedium, (), (const override));
  MOCK_METHOD(void, CloseImpl, (), (override));
};

std::function<void()> MakeDataPump(
    std::string label, InputStream* input, OutputStream* output,
    std::function<void(const ByteArray&)> monitor = nullptr) {
  return [label, input, output, monitor]() {
    NEARBY_LOG(INFO, "streaming data thorough '%s'", label.c_str());
    while (true) {
      auto read_response = input->Read(Pipe::kChunkSize);
      if (!read_response.ok()) {
        NEARBY_LOG(INFO, "Peer reader closed on '%s'", label.c_str());
        output->Close();
        break;
      }
      if (monitor) {
        monitor(read_response.result());
      }
      auto write_response = output->Write(read_response.result());
      if (write_response.Raised()) {
        NEARBY_LOG(INFO, "Peer writer closed on '%s'", label.c_str());
        input->Close();
        break;
      }
    }
    NEARBY_LOG(INFO, "streaming terminated on '%s'", label.c_str());
  };
}

std::function<void(const ByteArray&)> MakeDataMonitor(const std::string& label,
                                                      std::string* capture,
                                                      absl::Mutex* mutex) {
  return [label, capture, mutex](const ByteArray& input) mutable {
    std::string s = std::string(input);
    {
      absl::MutexLock lock(mutex);
      *capture += s;
    }
    NEARBY_LOG(INFO, "source='%s'; message='%s'", label.c_str(), s.c_str());
  };
}

std::pair<std::unique_ptr<securegcm::D2DConnectionContextV1>,
          std::unique_ptr<securegcm::D2DConnectionContextV1>>
DoDhKeyExchange(BaseEndpointChannel* channel_a,
                BaseEndpointChannel* channel_b) {
  std::unique_ptr<securegcm::D2DConnectionContextV1> context_a;
  std::unique_ptr<securegcm::D2DConnectionContextV1> context_b;
  EncryptionRunner crypto_a;
  EncryptionRunner crypto_b;
  ClientProxy proxy_a;
  ClientProxy proxy_b;
  CountDownLatch latch(2);
  crypto_a.StartClient(
      &proxy_a, "endpoint_id", channel_a,
      {
          .on_success_cb =
              [&latch, &context_a](
                  const string& endpoint_id,
                  std::unique_ptr<securegcm::UKey2Handshake> ukey2,
                  const string& auth_token, const ByteArray& raw_auth_token) {
                NEARBY_LOG(INFO, "client-A side key negotiation done");
                EXPECT_TRUE(ukey2->VerifyHandshake());
                auto context = ukey2->ToConnectionContext();
                EXPECT_NE (context, nullptr);
                context_a = std::move(context);
                latch.CountDown();
              },
          .on_failure_cb =
              [&latch](const string& endpoint_id, EndpointChannel* channel) {
                NEARBY_LOG(INFO, "client-A side key negotiation failed");
                latch.CountDown();
              },
      });
  crypto_b.StartServer(
      &proxy_b, "endpoint_id", channel_b,
      {
          .on_success_cb =
              [&latch, &context_b](
                  const string& endpoint_id,
                  std::unique_ptr<securegcm::UKey2Handshake> ukey2,
                  const string& auth_token, const ByteArray& raw_auth_token) {
                NEARBY_LOG(INFO, "client-B side key negotiation done");
                EXPECT_TRUE(ukey2->VerifyHandshake());
                auto context = ukey2->ToConnectionContext();
                EXPECT_NE (context, nullptr);
                context_b = std::move(context);
                latch.CountDown();
              },
          .on_failure_cb =
              [&latch](const string& endpoint_id, EndpointChannel* channel) {
                NEARBY_LOG(INFO, "client-B side key negotiation failed");
                latch.CountDown();
              },
      });
  EXPECT_TRUE(latch.Await(absl::Milliseconds(5000)).result());
  return std::make_pair(std::move(context_a), std::move(context_b));
}

TEST(BaseEndpointChannelTest, ConstructorDestructorWorks) {
  Pipe pipe;
  InputStream& input_stream = pipe.GetInputStream();
  OutputStream& output_stream = pipe.GetOutputStream();

  TestEndpointChannel test_channel(&input_stream, &output_stream);
}

TEST(BaseEndpointChannelTest, ReadWrite) {
  // Direct not-encrypted IO.
  Pipe pipe_a;  // channel_a writes to pipe_a, reads from pipe_b.
  Pipe pipe_b;  // channel_b writes to pipe_b, reads from pipe_a.
  TestEndpointChannel channel_a(&pipe_b.GetInputStream(),
                                &pipe_a.GetOutputStream());
  TestEndpointChannel channel_b(&pipe_a.GetInputStream(),
                                &pipe_b.GetOutputStream());
  ByteArray tx_message{"data message"};
  channel_a.Write(tx_message);
  ByteArray rx_message = std::move(channel_b.Read().result());
  EXPECT_EQ(rx_message, tx_message);
}

TEST(BaseEndpointChannelTest, NotEncryptedReadWriteCanBeIntercepted) {
  // Not encrypted IO; MITM scenario.

  // Setup test communication environment.
  absl::Mutex mutex;
  std::string capture_a;
  std::string capture_b;
  Pipe client_a;  // Channel "a" writes to client "a", reads from server "a".
  Pipe client_b;  // Channel "b" writes to client "b", reads from server "b".
  Pipe server_a;  // Data pump "a" reads from client "a", writes to server "b".
  Pipe server_b;  // Data pump "b" reads from client "b", writes to server "a".
  TestEndpointChannel channel_a(&server_a.GetInputStream(),
                                &client_a.GetOutputStream());
  TestEndpointChannel channel_b(&server_b.GetInputStream(),
                                &client_b.GetOutputStream());

  ON_CALL(channel_a, GetMedium).WillByDefault([]() { return Medium::BLE; });
  ON_CALL(channel_b, GetMedium).WillByDefault([]() { return Medium::BLE; });

  MultiThreadExecutor executor(2);
  executor.Execute(MakeDataPump(
      "pump_a", &client_a.GetInputStream(), &server_b.GetOutputStream(),
      MakeDataMonitor("monitor_a", &capture_a, &mutex)));
  executor.Execute(MakeDataPump(
      "pump_b", &client_b.GetInputStream(), &server_a.GetOutputStream(),
      MakeDataMonitor("monitor_b", &capture_b, &mutex)));

  EXPECT_EQ(channel_a.GetType(), "BLE");
  EXPECT_EQ(channel_b.GetType(), "BLE");

  // Start data transfer
  ByteArray tx_message{"data message"};
  channel_a.Write(tx_message);
  ByteArray rx_message = std::move(channel_b.Read().result());

  // Verify expectations.
  EXPECT_EQ(rx_message, tx_message);
  {
    absl::MutexLock lock(&mutex);
    std::string message{tx_message};
    EXPECT_TRUE(capture_a.find(message) != std::string::npos ||
               capture_b.find(message) != std::string::npos);
  }

  // Shutdown test environment.
  channel_a.Close(DisconnectionReason::LOCAL_DISCONNECTION);
  channel_b.Close(DisconnectionReason::REMOTE_DISCONNECTION);
}

TEST(BaseEndpointChannelTest, EncryptedReadWriteCanNotBeIntercepted) {
  // Encrypted IO; MITM scenario.

  // Setup test communication environment.
  absl::Mutex mutex;
  std::string capture_a;
  std::string capture_b;
  Pipe client_a;  // Channel "a" writes to client "a", reads from server "a".
  Pipe client_b;  // Channel "b" writes to client "b", reads from server "b".
  Pipe server_a;  // Data pump "a" reads from client "a", writes to server "b".
  Pipe server_b;  // Data pump "b" reads from client "b", writes to server "a".
  TestEndpointChannel channel_a(&server_a.GetInputStream(),
                                &client_a.GetOutputStream());
  TestEndpointChannel channel_b(&server_b.GetInputStream(),
                                &client_b.GetOutputStream());

  ON_CALL(channel_a, GetMedium).WillByDefault([]() {
    return Medium::BLUETOOTH;
  });
  ON_CALL(channel_b, GetMedium).WillByDefault([]() {
    return Medium::BLUETOOTH;
  });

  MultiThreadExecutor executor(2);
  executor.Execute(MakeDataPump(
      "pump_a", &client_a.GetInputStream(), &server_b.GetOutputStream(),
      MakeDataMonitor("monitor_a", &capture_a, &mutex)));
  executor.Execute(MakeDataPump(
      "pump_b", &client_b.GetInputStream(), &server_a.GetOutputStream(),
      MakeDataMonitor("monitor_b", &capture_b, &mutex)));

  // Run DH key exchange; setup encryption contexts for channels.
  auto [context_a, context_b] = DoDhKeyExchange(&channel_a, &channel_b);
  ASSERT_NE(context_a, nullptr);
  ASSERT_NE(context_b, nullptr);
  channel_a.EnableEncryption(context_a.get());
  channel_b.EnableEncryption(context_b.get());

  EXPECT_EQ(channel_a.GetType(), "ENCRYPTED_BLUETOOTH");
  EXPECT_EQ(channel_b.GetType(), "ENCRYPTED_BLUETOOTH");

  // Start data transfer
  ByteArray tx_message{"data message"};
  channel_a.Write(tx_message);
  ByteArray rx_message = std::move(channel_b.Read().result());

  // Verify expectations.
  EXPECT_EQ(rx_message, tx_message);
  {
    absl::MutexLock lock(&mutex);
    std::string message{tx_message};
    EXPECT_TRUE(capture_a.find(message) == std::string::npos &&
                capture_b.find(message) == std::string::npos);
  }

  // Shutdown test environment.
  channel_a.Close(DisconnectionReason::LOCAL_DISCONNECTION);
  channel_b.Close(DisconnectionReason::REMOTE_DISCONNECTION);
}

TEST(BaseEndpointChannelTest, CanBesuspendedAndResumed) {
  // Setup test communication environment.
  Pipe pipe_a;  // channel_a writes to pipe_a, reads from pipe_b.
  Pipe pipe_b;  // channel_b writes to pipe_b, reads from pipe_a.
  TestEndpointChannel channel_a(&pipe_b.GetInputStream(),
                                &pipe_a.GetOutputStream());
  TestEndpointChannel channel_b(&pipe_a.GetInputStream(),
                                &pipe_b.GetOutputStream());

  ON_CALL(channel_a, GetMedium).WillByDefault([]() {
    return Medium::WIFI_LAN;
  });
  ON_CALL(channel_b, GetMedium).WillByDefault([]() {
    return Medium::WIFI_LAN;
  });

  EXPECT_EQ(channel_a.GetType(), "WIFI_LAN");
  EXPECT_EQ(channel_b.GetType(), "WIFI_LAN");

  // Start data transfer
  ByteArray tx_message{"data message"};
  ByteArray more_message{"more data"};
  channel_a.Write(tx_message);
  ByteArray rx_message = std::move(channel_b.Read().result());

  // Pause and make sure reader blocks.
  MultiThreadExecutor pause_resume_executor(2);
  channel_a.Pause();
  pause_resume_executor.Execute([&channel_a, &more_message](){
    // Write will block until channel is resumed, or closed.
    EXPECT_TRUE(channel_a.Write(more_message).Ok());
  });
  std::atomic_bool done = false;
  ByteArray read_more;
  pause_resume_executor.Execute([&channel_b, &read_more, &done](){
    // Read will block until channel is resumed, or closed.
    auto response = channel_b.Read();
    EXPECT_TRUE(response.ok());
    read_more = std::move(response.result());
    done = true;
  });
  absl::SleepFor(absl::Milliseconds(500));
  EXPECT_TRUE(read_more.Empty());

  // Resume; verify that data transfer comepleted.
  channel_a.Resume();
  absl::SleepFor(absl::Milliseconds(500));
  EXPECT_TRUE(done);
  EXPECT_EQ(read_more, more_message);

  // Shutdown test environment.
  channel_a.Close(DisconnectionReason::LOCAL_DISCONNECTION);
  channel_b.Close(DisconnectionReason::REMOTE_DISCONNECTION);
}

TEST(BaseEndpointChannelTest, ReadAfterInputStreamClosed) {
  Pipe pipe;
  InputStream& input_stream = pipe.GetInputStream();
  OutputStream& output_stream = pipe.GetOutputStream();

  TestEndpointChannel test_channel(&input_stream, &output_stream);

  // Close the output stream before trying to read from the input.
  output_stream.Close();

  // Trying to read should fail gracefully with an IO error.
  ExceptionOr<ByteArray> read_data = test_channel.Read();

  ASSERT_FALSE(read_data.ok());
  ASSERT_TRUE(read_data.GetException().Raised(Exception::kIo));
}

}  // namespace
}  // namespace connections
}  // namespace nearby
}  // namespace location
