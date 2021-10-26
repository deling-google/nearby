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

#include "platform/base/medium_environment.h"

#include <atomic>
#include <cinttypes>
#include <functional>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

#include "platform/api/ble.h"
#include "platform/api/bluetooth_adapter.h"
#include "platform/api/bluetooth_classic.h"
#include "platform/api/wifi_lan.h"
#include "platform/api/wifi_lan_v2.h"
#include "platform/base/feature_flags.h"
#include "platform/base/logging.h"
#include "platform/base/nsd_service_info.h"
#include "platform/public/count_down_latch.h"

namespace location {
namespace nearby {

MediumEnvironment& MediumEnvironment::Instance() {
  static std::aligned_storage_t<sizeof(MediumEnvironment),
                                alignof(MediumEnvironment)>
      storage;
  static MediumEnvironment* env = new (&storage) MediumEnvironment();
  return *env;
}

void MediumEnvironment::Start(EnvironmentConfig config) {
  if (!enabled_.exchange(true)) {
    NEARBY_LOGS(INFO) << "MediumEnvironment::Start()";
    config_ = std::move(config);
    Reset();
  }
}

void MediumEnvironment::Stop() {
  if (enabled_.exchange(false)) {
    NEARBY_LOGS(INFO) << "MediumEnvironment::Stop()";
    Sync(false);
  }
}

void MediumEnvironment::Reset() {
  RunOnMediumEnvironmentThread([this]() {
    NEARBY_LOGS(INFO) << "MediumEnvironment::Reset()";
    bluetooth_adapters_.clear();
    bluetooth_mediums_.clear();
    ble_mediums_.clear();
    webrtc_signaling_message_callback_.clear();
    webrtc_signaling_complete_callback_.clear();
    wifi_lan_mediums_.clear();
    wifi_lan_mediums_v2_.clear();
    use_valid_peer_connection_ = true;
    peer_connection_latency_ = absl::ZeroDuration();
  });
  Sync();
}

void MediumEnvironment::Sync(bool enable_notifications) {
  enable_notifications_ = enable_notifications;
  NEARBY_LOGS(INFO) << "MediumEnvironment::sync=" << enable_notifications;
  int count = 0;
  do {
    CountDownLatch latch(1);
    count = job_count_ + 1;
    // We are about to schedule one last job.
    // When it is done, counter must be equal to count.
    // However, if pending jobs schedule anything else,
    // it will be pending after us.
    // If we want to ensure we are completely idle, then we have to
    // repeat sync, until this becomes true.
    RunOnMediumEnvironmentThread([&latch]() { latch.CountDown(); });
    latch.Await();
  } while (count < job_count_);
  NEARBY_LOGS(INFO) << "MediumEnvironment::Sync(): done [count=" << count
                    << "]";
}

const EnvironmentConfig& MediumEnvironment::GetEnvironmentConfig() {
  return config_;
}

void MediumEnvironment::OnBluetoothAdapterChangedState(
    api::BluetoothAdapter& adapter, api::BluetoothDevice& adapter_device,
    std::string name, bool enabled, api::BluetoothAdapter::ScanMode mode) {
  if (!enabled_) return;
  CountDownLatch latch(1);
  RunOnMediumEnvironmentThread([this, &adapter, &adapter_device,
                                name = std::move(name), enabled, mode,
                                &latch]() {
    NEARBY_LOGS(INFO) << "[adapter=" << &adapter
                      << ", device=" << &adapter_device << "] update: name="
                      << ", enabled=" << enabled << ", mode=" << int32_t(mode);
    for (auto& medium_info : bluetooth_mediums_) {
      auto& info = medium_info.second;
      // Do not send notification to medium that owns this adapter.
      if (info.adapter == &adapter) continue;
      NEARBY_LOGS(INFO) << "[adapter=" << &adapter
                        << ", device=" << &adapter_device
                        << "] notify: adapter=" << info.adapter;
      OnBluetoothDeviceStateChanged(info, adapter_device, name, mode, enabled);
    }
    // We don't care if there is an adapter already since all we store is a
    // pointer. Pointer must remain valid for the duration of a Core session
    // (since it is owned by the correspoinding Medium, and mediums lifetime
    // matches Core lifetime).
    if (enabled) {
      bluetooth_adapters_.emplace(&adapter, &adapter_device);
    } else {
      bluetooth_adapters_.erase(&adapter);
    }
    latch.CountDown();
  });
  latch.Await();
}

void MediumEnvironment::OnBluetoothDeviceStateChanged(
    BluetoothMediumContext& info, api::BluetoothDevice& device,
    const std::string& name, api::BluetoothAdapter::ScanMode mode,
    bool enabled) {
  if (!enabled_) return;
  auto item = info.devices.find(&device);
  if (item == info.devices.end()) {
    NEARBY_LOGS(INFO) << "G3 OnBluetoothDeviceStateChanged [device impl="
                      << &device << "]: new device; notify="
                      << enable_notifications_.load();
    if (mode == api::BluetoothAdapter::ScanMode::kConnectableDiscoverable &&
        enabled) {
      // New device is turned on, and is in discoverable state.
      // Store device name, and report it as discovered.
      info.devices.emplace(&device, name);
      if (enable_notifications_) {
        RunOnMediumEnvironmentThread(
            [&info, &device]() { info.callback.device_discovered_cb(device); });
      }
    }
  } else {
    NEARBY_LOGS(INFO) << "G3 OnBluetoothDeviceStateChanged [device impl="
                      << &device << "]: existing device; notify="
                      << enable_notifications_.load();
    auto& discovered_name = item->second;
    if (mode == api::BluetoothAdapter::ScanMode::kConnectableDiscoverable &&
        enabled) {
      if (name != discovered_name) {
        // Known device is turned on, and is in discoverable state.
        // Store device name, and report it as renamed.
        item->second = name;
        if (enable_notifications_) {
          RunOnMediumEnvironmentThread([&info, &device]() {
            info.callback.device_name_changed_cb(device);
          });
        }
      } else {
        // Device is in discovery mode, so we are reporting it anyway.
        if (enable_notifications_) {
          RunOnMediumEnvironmentThread([&info, &device]() {
            info.callback.device_discovered_cb(device);
          });
        }
      }
    }
    if (!enabled) {
      // Known device is turned off.
      // Erase it from the map, and report as lost.
      if (enable_notifications_) {
        RunOnMediumEnvironmentThread(
            [&info, &device]() { info.callback.device_lost_cb(device); });
      }
      info.devices.erase(item);
    }
  }
}

api::BluetoothDevice* MediumEnvironment::FindBluetoothDevice(
    const std::string& mac_address) {
  api::BluetoothDevice* device = nullptr;
  CountDownLatch latch(1);
  RunOnMediumEnvironmentThread([this, &device, &latch, &mac_address]() {
    for (auto& item : bluetooth_mediums_) {
      auto* adapter = item.second.adapter;
      if (!adapter) continue;
      if (adapter->GetMacAddress() == mac_address) {
        device = bluetooth_adapters_[adapter];
        break;
      }
    }
    latch.CountDown();
  });
  latch.Await();
  return device;
}

void MediumEnvironment::OnBlePeripheralStateChanged(
    BleMediumContext& info, api::BlePeripheral& peripheral,
    const std::string& service_id, bool fast_advertisement, bool enabled) {
  if (!enabled_) return;
  NEARBY_LOGS(INFO) << "G3 OnBleServiceStateChanged [peripheral impl="
                    << &peripheral << "]; context=" << &info
                    << "; service_id=" << service_id
                    << "; notify=" << enable_notifications_.load();
  if (!enable_notifications_) return;
  RunOnMediumEnvironmentThread([&info, enabled, &peripheral, service_id,
                                fast_advertisement]() {
    NEARBY_LOGS(INFO) << "G3 [Run] OnBleServiceStateChanged [peripheral impl="
                      << &peripheral << "]; context=" << &info
                      << "; service_id=" << service_id
                      << "; notify=" << enabled;
    if (enabled) {
      info.discovery_callback.peripheral_discovered_cb(peripheral, service_id,
                                                       fast_advertisement);
    } else {
      info.discovery_callback.peripheral_lost_cb(peripheral, service_id);
    }
  });
}

void MediumEnvironment::OnWifiLanServiceStateChanged(
    WifiLanMediumContext& info, api::WifiLanService& wifi_lan_service,
    const std::string& service_id, bool enabled) {
  if (!enabled_) return;
  NEARBY_LOGS(INFO) << "G3 OnWifiLanServiceStateChanged [wifi_lan_service impl="
                    << &wifi_lan_service << "]; context=" << &info
                    << "; service_id=" << service_id
                    << "; notify=" << enable_notifications_.load();
  if (!enable_notifications_) return;
  RunOnMediumEnvironmentThread(
      [&info, enabled, &wifi_lan_service, service_id]() {
        NEARBY_LOGS(INFO)
            << "G3 [Run] OnWifiLanServiceStateChanged [wifi_lan_service impl="
            << &wifi_lan_service << "]; context=" << &info
            << "; service_id=" << service_id << "; enabled=" << enabled;
        auto service_id_context = info.services.find(service_id);
        if (service_id_context == info.services.end()) return;

        if (enabled) {
          service_id_context->second.discovery_callback.service_discovered_cb(
              wifi_lan_service, service_id);
        } else {
          service_id_context->second.discovery_callback.service_lost_cb(
              wifi_lan_service, service_id);
        }
      });
}

void MediumEnvironment::RunOnMediumEnvironmentThread(
    std::function<void()> runnable) {
  job_count_++;
  executor_.Execute(std::move(runnable));
}

void MediumEnvironment::RegisterBluetoothMedium(
    api::BluetoothClassicMedium& medium,
    api::BluetoothAdapter& medium_adapter) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread([this, &medium, &medium_adapter]() {
    auto& context = bluetooth_mediums_
                        .insert({&medium,
                                 BluetoothMediumContext{
                                     .adapter = &medium_adapter,
                                 }})
                        .first->second;
    auto* owned_adapter = context.adapter;
    NEARBY_LOGS(INFO) << "Registered: medium=" << &medium
                      << "; adapter=" << owned_adapter;
    for (auto& adapter_device : bluetooth_adapters_) {
      auto& adapter = adapter_device.first;
      auto& device = adapter_device.second;
      if (adapter == nullptr) continue;
      OnBluetoothDeviceStateChanged(context, *device, adapter->GetName(),
                                    adapter->GetScanMode(),
                                    adapter->IsEnabled());
    }
  });
}

void MediumEnvironment::UpdateBluetoothMedium(
    api::BluetoothClassicMedium& medium, BluetoothDiscoveryCallback callback) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread(
      [this, &medium, callback = std::move(callback)]() {
        auto item = bluetooth_mediums_.find(&medium);
        if (item == bluetooth_mediums_.end()) return;
        auto& context = item->second;
        context.callback = std::move(callback);
        auto* owned_adapter = context.adapter;
        NEARBY_LOGS(INFO) << "Updated: this=" << this << "; medium=" << &medium
                          << "; adapter=" << owned_adapter
                          << "; name=" << owned_adapter->GetName()
                          << "; enabled=" << owned_adapter->IsEnabled()
                          << "; mode=" << int32_t(owned_adapter->GetScanMode());
        for (auto& adapter_device : bluetooth_adapters_) {
          auto& adapter = adapter_device.first;
          auto& device = adapter_device.second;
          if (adapter == nullptr) continue;
          OnBluetoothDeviceStateChanged(context, *device, adapter->GetName(),
                                        adapter->GetScanMode(),
                                        adapter->IsEnabled());
        }
      });
}

void MediumEnvironment::UnregisterBluetoothMedium(
    api::BluetoothClassicMedium& medium) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread([this, &medium]() {
    auto item = bluetooth_mediums_.extract(&medium);
    if (item.empty()) return;
    NEARBY_LOGS(INFO) << "Unregistered Bluetooth medium:" << &medium;
  });
}

void MediumEnvironment::RegisterBleMedium(api::BleMedium& medium) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread([this, &medium]() {
    ble_mediums_.insert({&medium, BleMediumContext{}});
    NEARBY_LOGS(INFO) << "Registered: medium:" << &medium;
  });
}

void MediumEnvironment::UpdateBleMediumForAdvertising(
    api::BleMedium& medium, api::BlePeripheral& peripheral,
    const std::string& service_id, bool fast_advertisement, bool enabled) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread([this, &medium, &peripheral, service_id,
                                fast_advertisement, enabled]() {
    auto item = ble_mediums_.find(&medium);
    if (item == ble_mediums_.end()) {
      NEARBY_LOGS(INFO) << "UpdateBleMediumForAdvertising failed. There is no "
                           "medium registered.";
      return;
    }
    auto& context = item->second;
    context.ble_peripheral = &peripheral;
    context.advertising = enabled;
    context.fast_advertisement = fast_advertisement;
    NEARBY_LOGS(INFO) << "Update Ble medium for advertising: this=" << this
                      << "; medium=" << &medium << "; service_id=" << service_id
                      << "; name=" << peripheral.GetName()
                      << "; fast_advertisement=" << fast_advertisement
                      << "; enabled=" << enabled;
    for (auto& medium_info : ble_mediums_) {
      auto& local_medium = medium_info.first;
      auto& info = medium_info.second;
      // Do not send notification to the same medium.
      if (local_medium == &medium) continue;
      OnBlePeripheralStateChanged(info, peripheral, service_id,
                                  fast_advertisement, enabled);
    }
  });
}

void MediumEnvironment::UpdateBleMediumForScanning(
    api::BleMedium& medium, const std::string& service_id,
    const std::string& fast_advertisement_service_uuid,
    BleDiscoveredPeripheralCallback callback, bool enabled) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread(
      [this, &medium, service_id, fast_advertisement_service_uuid,
       callback = std::move(callback), enabled]() {
        auto item = ble_mediums_.find(&medium);
        if (item == ble_mediums_.end()) {
          NEARBY_LOGS(INFO)
              << "UpdateBleMediumFoScanning failed. There is no medium "
                 "registered.";
          return;
        }
        auto& context = item->second;
        context.discovery_callback = std::move(callback);
        NEARBY_LOGS(INFO) << "Update Ble medium for scanning: this=" << this
                          << "; medium=" << &medium
                          << "; service_id=" << service_id
                          << "; fast_advertisement_service_uuid="
                          << fast_advertisement_service_uuid
                          << "; enabled=" << enabled;
        for (auto& medium_info : ble_mediums_) {
          auto& local_medium = medium_info.first;
          auto& info = medium_info.second;
          // Do not send notification to the same medium.
          if (local_medium == &medium) continue;
          // Search advertising mediums and send notification.
          if (info.advertising && enabled) {
            OnBlePeripheralStateChanged(context, *(info.ble_peripheral),
                                        service_id, info.fast_advertisement,
                                        enabled);
          }
        }
      });
}

void MediumEnvironment::UpdateBleMediumForAcceptedConnection(
    api::BleMedium& medium, const std::string& service_id,
    BleAcceptedConnectionCallback callback) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread(
      [this, &medium, service_id, callback = std::move(callback)]() {
        auto item = ble_mediums_.find(&medium);
        if (item == ble_mediums_.end()) {
          NEARBY_LOGS(INFO)
              << "Update Ble medium failed. There is no medium registered.";
          return;
        }
        auto& context = item->second;
        context.accepted_connection_callback = std::move(callback);
        NEARBY_LOGS(INFO) << "Update Ble medium for accepted callback: this="
                          << this << "; medium=" << &medium
                          << "; service_id=" << service_id;
      });
}

void MediumEnvironment::UnregisterBleMedium(api::BleMedium& medium) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread([this, &medium]() {
    auto item = ble_mediums_.extract(&medium);
    if (item.empty()) return;
    NEARBY_LOGS(INFO) << "Unregistered Ble medium";
  });
}

void MediumEnvironment::CallBleAcceptedConnectionCallback(
    api::BleMedium& medium, api::BleSocket& socket,
    const std::string& service_id) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread(
      [this, &medium, &socket, service_id]() {
        auto item = ble_mediums_.find(&medium);
        if (item == ble_mediums_.end()) {
          NEARBY_LOGS(INFO)
              << "Call AcceptedConnectionCallback failed. There is no medium "
                 "registered.";
          return;
        }
        auto& info = item->second;
        info.accepted_connection_callback.accepted_cb(socket, service_id);
      });
}

void MediumEnvironment::RegisterWebRtcSignalingMessenger(
    absl::string_view self_id, OnSignalingMessageCallback message_callback,
    OnSignalingCompleteCallback complete_callback) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread([this, self_id{std::string(self_id)},
                                message_callback{std::move(message_callback)},
                                complete_callback{
                                    std::move(complete_callback)}]() {
    webrtc_signaling_message_callback_[self_id] = std::move(message_callback);
    webrtc_signaling_complete_callback_[self_id] = std::move(complete_callback);
    NEARBY_LOGS(INFO) << "Registered signaling message callback for id = "
                      << self_id;
  });
}

void MediumEnvironment::UnregisterWebRtcSignalingMessenger(
    absl::string_view self_id) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread([this, self_id{std::string(self_id)}]() {
    auto message_callback_item =
        webrtc_signaling_message_callback_.extract(self_id);
    auto complete_callback_item =
        webrtc_signaling_complete_callback_.extract(self_id);
    NEARBY_LOGS(INFO) << "Unregistered signaling message callback for id = "
                      << self_id;
  });
}

void MediumEnvironment::SendWebRtcSignalingMessage(absl::string_view peer_id,
                                                   const ByteArray& message) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread(
      [this, peer_id{std::string(peer_id)}, message]() {
        auto item = webrtc_signaling_message_callback_.find(peer_id);
        if (item == webrtc_signaling_message_callback_.end()) {
          NEARBY_LOGS(WARNING)
              << "No callback registered for peer id = " << peer_id;
          return;
        }

        item->second(message);
      });
}

void MediumEnvironment::SendWebRtcSignalingComplete(absl::string_view peer_id,
                                                    bool success) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread(
      [this, peer_id{std::string(peer_id)}, success]() {
        auto item = webrtc_signaling_complete_callback_.find(peer_id);
        if (item == webrtc_signaling_complete_callback_.end()) {
          NEARBY_LOGS(WARNING)
              << "No callback registered for peer id = " << peer_id;
          return;
        }

        item->second(success);
      });
}

void MediumEnvironment::SetUseValidPeerConnection(
    bool use_valid_peer_connection) {
  use_valid_peer_connection_ = use_valid_peer_connection;
}

bool MediumEnvironment::GetUseValidPeerConnection() {
  return use_valid_peer_connection_;
}

void MediumEnvironment::SetPeerConnectionLatency(
    absl::Duration peer_connection_latency) {
  peer_connection_latency_ = peer_connection_latency;
}

absl::Duration MediumEnvironment::GetPeerConnectionLatency() {
  return peer_connection_latency_;
}

void MediumEnvironment::RegisterWifiLanMedium(api::WifiLanMedium& medium) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread([this, &medium]() {
    wifi_lan_mediums_.insert({&medium, WifiLanMediumContext{}});
    NEARBY_LOGS(INFO) << "Registered: medium=" << &medium;
  });
}

void MediumEnvironment::UpdateWifiLanMediumForAdvertising(
    api::WifiLanMedium& medium, api::WifiLanService& wifi_lan_service,
    const std::string& service_id, bool enabled) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread(
      [this, &medium, &wifi_lan_service, service_id, enabled]() {
        auto item = wifi_lan_mediums_.find(&medium);
        if (item == wifi_lan_mediums_.end()) {
          NEARBY_LOGS(INFO)
              << "UpdateWifiLanMediumForAdvertising failed. There is no medium "
                 "registered.";
          return;
        }
        auto& context = item->second;
        context.wifi_lan_service = &wifi_lan_service;
        auto service_id_context = context.services.find(service_id);
        if (service_id_context == context.services.end()) {
          WifiLanServiceIdContext id_context{
              .advertising = enabled,
          };
          context.services.emplace(service_id, std::move(id_context));
        } else {
          service_id_context->second.advertising = enabled;
        }
        NEARBY_LOGS(INFO) << "Update WifiLan medium for advertising: this="
                          << this << "; medium=" << &medium
                          << "; service_id=" << service_id
                          << "; wifi_lan_service=" << &wifi_lan_service
                          << ", service_info_name="
                          << wifi_lan_service.GetServiceInfo().GetServiceName()
                          << "; enabled=" << enabled;
        for (auto& medium_info : wifi_lan_mediums_) {
          auto& local_medium = medium_info.first;
          auto& info = medium_info.second;
          // Do not send notification to the same medium.
          if (local_medium == &medium) continue;
          OnWifiLanServiceStateChanged(info, wifi_lan_service, service_id,
                                       enabled);
        }
      });
}

void MediumEnvironment::UpdateWifiLanMediumForDiscovery(
    api::WifiLanMedium& medium, const std::string& service_id,
    WifiLanDiscoveredServiceCallback callback, bool enabled) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread(
      [this, &medium, service_id, callback = std::move(callback), enabled]() {
        auto item = wifi_lan_mediums_.find(&medium);
        if (item == wifi_lan_mediums_.end()) {
          NEARBY_LOGS(INFO)
              << "UpdateWifiLanMediumForDiscovery failed. There is no medium "
                 "registered.";
          return;
        }
        auto& context = item->second;
        auto service_id_context = context.services.find(service_id);
        if (service_id_context == context.services.end()) {
          WifiLanServiceIdContext id_context{
              .discovery_callback = std::move(callback),
          };
          context.services.emplace(service_id, std::move(id_context));
        } else {
          service_id_context->second.discovery_callback = std::move(callback);
        }
        NEARBY_LOGS(INFO) << "Update WifiLan medium for discovery: this="
                          << this << "; medium=" << &medium
                          << "; service_id=" << service_id
                          << "; enabled=" << enabled;
        for (auto& medium_info : wifi_lan_mediums_) {
          auto& local_medium = medium_info.first;
          auto& info = medium_info.second;
          // Do not send notification to the same medium.
          if (local_medium == &medium) continue;
          // Search advertising mediums and send notification.
          for (auto& service_id_context : info.services) {
            auto& service_id = service_id_context.first;
            auto& id_context = service_id_context.second;
            if (id_context.advertising && enabled) {
              OnWifiLanServiceStateChanged(context, *(info.wifi_lan_service),
                                           service_id, enabled);
            }
          }
        }
      });
}

void MediumEnvironment::UpdateWifiLanMediumForAcceptedConnection(
    api::WifiLanMedium& medium, const std::string& service_id,
    WifiLanAcceptedConnectionCallback callback) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread(
      [this, &medium, service_id, callback = std::move(callback)]() {
        auto item = wifi_lan_mediums_.find(&medium);
        if (item == wifi_lan_mediums_.end()) {
          NEARBY_LOGS(INFO)
              << "Update WifiLan medium failed. There is no medium registered.";
          return;
        }
        auto& context = item->second;
        auto service_id_context = context.services.find(service_id);
        if (service_id_context == context.services.end()) {
          WifiLanServiceIdContext id_context{
              .accepted_connection_callback = std::move(callback),
          };
          context.services.emplace(service_id, std::move(id_context));
        } else {
          service_id_context->second.accepted_connection_callback =
              std::move(callback);
        }
        NEARBY_LOGS(INFO)
            << "Update WifiLan medium for accepted callback: this=" << this
            << "; medium=" << &medium << "; service_id=" << service_id;
      });
}

void MediumEnvironment::UnregisterWifiLanMedium(api::WifiLanMedium& medium) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread([this, &medium]() {
    auto item = wifi_lan_mediums_.extract(&medium);
    if (item.empty()) return;
    NEARBY_LOGS(INFO) << "Unregistered WifiLan medium";
  });
}

void MediumEnvironment::CallWifiLanAcceptedConnectionCallback(
    api::WifiLanMedium& medium, api::WifiLanSocket& socket,
    const std::string& service_id) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread(
      [this, &medium, &socket, service_id]() {
        auto item = wifi_lan_mediums_.find(&medium);
        if (item == wifi_lan_mediums_.end()) {
          NEARBY_LOGS(INFO)
              << "Call AcceptedConnectionCallback failed.. There is no medium "
                 "registered.";
          return;
        }
        auto& info = item->second;
        auto service_id_context = info.services.find(service_id);
        if (service_id_context != info.services.end()) {
          service_id_context->second.accepted_connection_callback.accepted_cb(
              socket, service_id);
        }
      });
}

api::WifiLanService* MediumEnvironment::GetWifiLanService(
    const std::string& ip_address, int port) {
  api::WifiLanService* remote_wifi_lan_service = nullptr;
  CountDownLatch latch(1);
  RunOnMediumEnvironmentThread(
      [this, &remote_wifi_lan_service, &ip_address, port, &latch]() {
        for (auto& item : wifi_lan_mediums_) {
          auto* wifi_lan_service = item.second.wifi_lan_service;
          if (!wifi_lan_service) continue;
          std::string remote_ip_address =
              remote_wifi_lan_service->GetServiceInfo().GetIPAddress();
          int remote_port = remote_wifi_lan_service->GetServiceInfo().GetPort();
          if (remote_ip_address == ip_address && remote_port == port) {
            remote_wifi_lan_service = wifi_lan_service;
            break;
          }
        }
        latch.CountDown();
      });
  latch.Await();
  return remote_wifi_lan_service;
}

void MediumEnvironment::RegisterWifiLanMediumV2(api::WifiLanMediumV2& medium) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread([this, &medium]() {
    wifi_lan_mediums_v2_.insert({&medium, WifiLanMediumV2Context{}});
    NEARBY_LOG(INFO, "Registered: medium=%p", &medium);
  });
}

void MediumEnvironment::UpdateWifiLanMediumV2ForAdvertising(
    api::WifiLanMediumV2& medium, const NsdServiceInfo& service_info,
    bool enabled) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread([this, &medium, service_info = service_info,
                                enabled]() {
    std::string service_type = service_info.GetServiceType();
    NEARBY_LOGS(INFO) << "Update WifiLan medium for advertising: this=" << this
                      << "; medium=" << &medium
                      << "; service_name=" << service_info.GetServiceName()
                      << "; service_type=" << service_type
                      << ", enabled=" << enabled;
    for (auto& medium_info : wifi_lan_mediums_v2_) {
      auto& local_medium = medium_info.first;
      auto& info = medium_info.second;
      // Do not send notification to the same medium but update
      // service info map.
      if (local_medium == &medium) {
        if (enabled) {
          info.advertising_services.insert({service_type, service_info});
        } else {
          info.advertising_services.erase(service_type);
        }
        continue;
      }
    }
  });
}

void MediumEnvironment::UnregisterWifiLanMediumV2(
    api::WifiLanMediumV2& medium) {
  if (!enabled_) return;
  RunOnMediumEnvironmentThread([this, &medium]() {
    auto item = wifi_lan_mediums_v2_.extract(&medium);
    if (item.empty()) return;
    NEARBY_LOG(INFO, "Unregistered WifiLan medium");
  });
}

void MediumEnvironment::SetFeatureFlags(const FeatureFlags::Flags& flags) {
  const_cast<FeatureFlags&>(FeatureFlags::GetInstance()).SetFlags(flags);
}

}  // namespace nearby
}  // namespace location
