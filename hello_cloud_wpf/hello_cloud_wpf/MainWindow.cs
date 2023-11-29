﻿using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Input;
using static HelloCloudWpf.NearbyConnections;

namespace HelloCloudWpf {
    public class MainModel {
        public Collection<EndpointModel> endpoints = new();

        public string LocalEndpointName { get; set; }

        public string LocalEndpointId { get; set; }

        public MainModel() {
            LocalEndpointName = string.Empty;
            LocalEndpointId = string.Empty;
        }
    }

    public class MainViewModel : INotifyPropertyChanged, IViewModel<MainModel> {
        public static MainViewModel Instance;

        private static readonly string serviceId = "com.google.location.nearby.apps.helloconnections";

        // Medium selection for advertising and connection
        private static readonly BooleanMediumSelector advertisingMediumSelector = new() {
            ble = true,
            bluetooth = true,
            webRtc = true,
            wifiDirect = true,
            wifiHotspot = true,
            wifiLan = true,
        };

        private static readonly BooleanMediumSelector discoveryMediumSelector = new() {
            ble = true,
            bluetooth = true,
            webRtc = true,
            wifiDirect = true,
            wifiHotspot = true,
            wifiLan = true,
        };

        private static readonly BooleanMediumSelector connectionMediumSelector = new() {
            ble = false,
            bluetooth = true,
            webRtc = true,
            wifiDirect = true,
            wifiHotspot = true,
            wifiLan = true,
        };

        public ObservableCollection<EndpointViewModel> Endpoints => endpoints;

        public EndpointViewModel? SelectedEndpoint {
            get => selectedEndpoint;
            set {
                selectedEndpoint = value;
                PropertyChanged?.Invoke(this, new(nameof(SelectedEndpoint)));
            }
        }

        public static EndpointViewModel NullSelectedEndpoint => nullSelectedEndpoint!;

        public Cursor Cursor {
            get => busy ? Cursors.Wait : Cursors.Arrow;
        }

        public string LocalEndpointId => Model!.LocalEndpointId;

        public string LocalEndpointName {
            get => Model!.LocalEndpointName;
            set {
                Model!.LocalEndpointName = value;
                int len = Encoding.UTF8.GetByteCount(value);
                localEndpointInfo = new byte[len + 1];
                Encoding.UTF8.GetBytes(value, 0, value.Length, localEndpointInfo, 0);
                localEndpointInfo[len] = 0;
            }
        }

        // Enable endpoint name editing if true
        public bool IsNotAdvertising => !IsAdvertising;

        public bool IsAdvertising {
            get => isAdvertising;
            set {
                isAdvertising = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsNotAdvertising)));
            }
        }

        public ObservableCollection<string> LogEntries { get; set; }
        public event PropertyChangedEventHandler? PropertyChanged;
        public ICommand ClearLogCommand { get { return clearLogCommand; } }

        public MainModel? Model { get; set; }

        private readonly IntPtr router = IntPtr.Zero;
        private readonly IntPtr core = IntPtr.Zero;

        private static EndpointViewModel? nullSelectedEndpoint;

        private readonly ViewModelCollection<EndpointViewModel, EndpointModel> endpoints;
        private EndpointViewModel? selectedEndpoint;
        // Local endpoint info. In our case, it's a UTF8 encoded name.
        private byte[] localEndpointInfo = Array.Empty<byte>();
        private bool isAdvertising = false;
        private bool isDiscovering = false;

        private readonly ICommand clearLogCommand;

        // Whether we are in the middle of an operation. Used for showing the hourglass cursor.
        private bool busy;

        private readonly EndpointFoundCallback endpointFoundCallback;
        private readonly EndpointLostCallback endpointLostCallback;
        private readonly EndpointDistanceChangedCallback endpointDistanceChangedCallback;

        private readonly ConnectionInitiatedCallback initiatedCallback;
        private readonly ConnectionAcceptedCallback acceptedCallback;
        private readonly ConnectionRejectedCallback rejectedCallback;
        private readonly ConnectionDisconnectedCallback disconnectedCallback;
        private readonly BandwidthUpgradedCallback bandwidthUpgradedCallback;

        private readonly PayloadInitiatedCallback payloadInitiatedCallback;
        private readonly PayloadProgressCallback payloadProgressCallback;

        private readonly List<GCHandle> callbackHandles = new();

        public MainViewModel() {
            Instance = this;

            Model = new MainModel();
            LocalEndpointName = Environment.GetEnvironmentVariable("COMPUTERNAME") ?? "Windows";

            endpoints = new(Model.endpoints);

            nullSelectedEndpoint ??= new EndpointViewModel() {
                Model = new EndpointModel(
                        id: string.Empty,
                        name: string.Empty)
            };

            // Note: any operation that changes a property that has a binding from the UI, e.g. the busy state,
            // needs to happen on the UI thread. But since some can only be triggered from the UI, e.g.
            // StartDiscovering(), we're not ensure to make these changes in the UI thread. But we probably should
            // do that rather than rely on that we're already on the UI thread. Right now it's very messy. If there
            // is any weird UI related bugs, they are probably due to this. We'll clean it up later after prototyping.
            LogEntries = new ObservableCollection<string>();

            clearLogCommand = new RelayCommand(
                _ => ClearLog(),
                _ => CanClearLog());

            EndpointViewModel endpoint = new() {
                Model = new EndpointModel("R2D2", "Debug droid", EndpointModel.State.Connected),
                Medium = Medium.kBluetooth,
            };
            Endpoints.Add(endpoint);
            SelectedEndpoint = endpoint;

            router = InitServiceControllerRouter();
            if (router == IntPtr.Zero) {
                throw new Exception("Failed: InitServiceControllerRouter");
            }

            core = InitCore(router);
            if (core == IntPtr.Zero) {
                throw new Exception("Failed: InitCore");
            }

            Model.LocalEndpointId = GetLocalEndpointId(core);

            // Allocate memory these delegates so that they don't get GCed.
            endpointFoundCallback = OnEndpointFound;
            endpointLostCallback = OnEndpointLost;
            endpointDistanceChangedCallback = OnEndpointDistanceChanged;

            callbackHandles.Add(GCHandle.Alloc(endpointFoundCallback));
            callbackHandles.Add(GCHandle.Alloc(endpointLostCallback));
            callbackHandles.Add(GCHandle.Alloc(endpointDistanceChangedCallback));

            initiatedCallback = OnConnectionInitiated;
            acceptedCallback = OnConnectionAccepted;
            rejectedCallback = OnConnectionRejected;
            disconnectedCallback = OnConnectionDisconnected;
            bandwidthUpgradedCallback = OnBandwidthUpgraded;

            callbackHandles.Add(GCHandle.Alloc(initiatedCallback));
            callbackHandles.Add(GCHandle.Alloc(acceptedCallback));
            callbackHandles.Add(GCHandle.Alloc(rejectedCallback));
            callbackHandles.Add(GCHandle.Alloc(disconnectedCallback));
            callbackHandles.Add(GCHandle.Alloc(bandwidthUpgradedCallback));

            payloadInitiatedCallback = OnPayloadInitiated;
            payloadProgressCallback = OnPayloadProgress;

            callbackHandles.Add(GCHandle.Alloc(payloadInitiatedCallback));
            callbackHandles.Add(GCHandle.Alloc(payloadProgressCallback));
        }

        public void Deinit() {
            NearbyConnections.CloseCore(core);
            NearbyConnections.CloseServiceControllerRouter(router);
            foreach (GCHandle callbackHandle in callbackHandles) {
                callbackHandle.Free();
            }
        }

        #region NearbyConnections operations.
        public void StartAdvertising() {
            Log("Starting advertising...");
            // StartAdvertising is a sync call. Show hourglass icon before it's done.
            SetBusy(true);
            IsAdvertising = true;
            AdvertisingOptions advertisingOptions = new() {
                strategy = NearbyConnections.P2pCluster,
                allowed = advertisingMediumSelector,
                autoUpgradeBandwidth = true,
                enforceTopologyConstraints = true,
                enableBluetoothListening = true,
                enableWebrtcListening = true,
                isOutOfBandConnection = false,
                lowPower = false,
                deviceInfo = IntPtr.Zero,
            };
            OperationResult result = NearbyConnections.StartAdvertising(
                core,
                serviceId,
                advertisingOptions,
                localEndpointInfo,
                initiatedCallback,
                acceptedCallback,
                rejectedCallback,
                disconnectedCallback,
                bandwidthUpgradedCallback);
            Log("StartAdvertising finished. Result: " + result);
            SetBusy(false);
        }

        public void StopAdvertising() {
            Log("Stopping advertising...");
            SetBusy(true);
            OperationResult result = NearbyConnections.StopAdvertising(core);
            Log("StopAdvertising finished. Result: " + result);
            IsAdvertising = false;
            SetBusy(false);
        }

        public void StartDiscovering() {
            Log("Starting discovering...");
            SetBusy(true);
            isDiscovering = true;
            DiscoveryOptions discoveryOptions = new() {
                strategy = NearbyConnections.P2pCluster,
                allowed = discoveryMediumSelector,
                autoUpgradeBandwidth = true,
                enforceTopologyConstraints = true,
                isOutOfBandConnection = false,
                lowPower = true,
            };
            OperationResult result = NearbyConnections.StartDiscovering(
                core,
                serviceId,
                discoveryOptions,
                endpointFoundCallback,
                endpointLostCallback,
                endpointDistanceChangedCallback);
            Log("StartDiscovering finished. Result: " + result);
            SetBusy(false);
        }

        public void StopDiscovering() {
            Log("Stopping discovering...");
            SetBusy(true);
            NearbyConnections.StopDiscovering(core);
            // Make a copy of Endpoints since we're changing it in the loop.
            // It would throw an exception if the collection is changed in the loop.
            // Alternatively, we could do a reverse loop. But we're dealing with
            // just a few endpoints. So the cost is not very high.
            foreach (EndpointViewModel endpoint in Endpoints.ToList()) {
                if (endpoint.State is EndpointModel.State.Discovered) {
                    Endpoints.Remove(endpoint);
                }
            }
            isDiscovering = false;
            SetBusy(false);
        }

        public void RequestConnection(string remoteEndpointId) {
            Log(string.Format("Requesting connection to {0} ...", remoteEndpointId));
            SetBusy(true);
            SetEndpointState(remoteEndpointId, EndpointModel.State.Pending);

            // TODO: keep alive is probably not properly set.
            ConnectionOptions connectionOptions = new() {
                strategy = NearbyConnections.P2pCluster,
                allowed = connectionMediumSelector,
                autoUpgradeBandwidth = true,
                enforceTopologyConstraints = true,
                enableBluetoothListening = true,
                enableWebrtcListening = true,
                isOutOfBandConnection = false,
                lowPower = true,
                remoteBluetoothMacAddress = new byte[32],
            };
            OperationResult result = NearbyConnections.RequestConnection(
                core,
                remoteEndpointId,
                connectionOptions,
                localEndpointInfo,
                initiatedCallback,
                acceptedCallback,
                rejectedCallback,
                disconnectedCallback,
                bandwidthUpgradedCallback);
            Log(string.Format("Request to connect to {0} completed. Result: {1}", remoteEndpointId, result));
            if (result != OperationResult.kSuccess) {
                // The request has not been sent by NC. We go back to Discovered state.
                SetEndpointState(remoteEndpointId, EndpointModel.State.Discovered);
            }
            SetBusy(false);
        }

        public void SendFiles(string endpointId, IEnumerable<OutgoingFileModel> files) {
            (int fileCount, byte[] payload) = EncodePayload(files);

            Log($"Sending {endpointId} {fileCount} file(s), {payload.Length} bytes in total ...");
            SetBusy(true);
            SetEndpointState(endpointId, EndpointModel.State.Sending);
            OperationResult result = NearbyConnections.SendPayloadBytes(core, endpointId, payload.Length, payload);
            if (result != OperationResult.kSuccess) {
                SetEndpointState(endpointId, EndpointModel.State.Discovered);
            }
            Log($"Sending files to {endpointId} finished. Result: {result}");
            SetBusy(false);
        }

        public void Disconnect(string endpointId) {
            Log("Disconnecting...");
            Log("  endpoint_id: " + endpointId);
            SetBusy(true);
            OperationResult result = NearbyConnections.Disconnect(core, endpointId);
            Log("Disconnect finished. Result: " + result);
            SetBusy(false);

        }
        #endregion

        public void SetBusy(bool value) {
            busy = value;
            PropertyChanged?.Invoke(this, new(nameof(Cursor)));
        }

        public void Log(string message) {
            Application.Current.Dispatcher.BeginInvoke(LogEntries.Add, DateTime.Now.ToString("HH:mm:ss ") + message);
        }

        private EndpointViewModel? GetEndpoint(string endpointId) =>
            Endpoints.FirstOrDefault(endpoint => endpoint.Id == endpointId);

        // TODO: does this also need to happen on the UI thread?
        private void SetEndpointState(string endpointId, EndpointModel.State state) {
            EndpointViewModel? endpoint = GetEndpoint(endpointId);
            if (endpoint == null) {
                Log(string.Format("End point {0} not found. Probably already lost.", endpointId));
                return;
            }

            Log(string.Format("Setting endpoint {0}'s state to {1}", endpointId, state));
            endpoint.State = state;

            // Refresh the states of the buttons if the endpoint is currently selected.
            if (endpointId == SelectedEndpoint?.Id) {
                EndpointViewModel.UpdateCanExecute();
            }
        }

        #region Operations on the UI thread
        private void SelectEndpoint(string endpointId) {
            // Checking for the existence of the endpoint needs to happen on the UI thread
            // instead of the caller thread here, because we could call SelectEndpoint()
            // immediately after AddEndpoint().
            // In this case, adding the endpoint has been scheduled but has not been executed.
            Application.Current.Dispatcher.BeginInvoke(
                (string endpointId) => {
                    EndpointViewModel? endpoint = GetEndpoint(endpointId);

                    if (endpoint == null) {
                        Log("Trying to select an endpoint that has already been removed. Id: " + endpointId);
                        return;
                    }
                    SelectedEndpoint = endpoint;
                },
                endpointId);
        }

        private void AddEndpoint(EndpointModel endpoint) {
            Application.Current.Dispatcher.BeginInvoke(
                (EndpointViewModel endpoint) => {
                    Endpoints.Add(endpoint);
                    SelectedEndpoint = endpoint;
                },
                new EndpointViewModel() {
                    Model = endpoint
                });
        }

        private void RemoveEndpoint(string endpointId) {
            EndpointViewModel? endpoint = GetEndpoint(endpointId);
            if (endpoint == null) {
                Log("Trying to remove an endpoint that has already been removed. Id: " + endpointId);
                return;
            }

            Application.Current.Dispatcher.BeginInvoke(
                (EndpointViewModel endpoint) => {
                    bool removingSelected = SelectedEndpoint == endpoint;
                    Endpoints.Remove(endpoint);
                    // If we have just removed the previously selected item, set selection to the first entry if it exists
                    if (removingSelected) {
                        SelectedEndpoint = Endpoints.FirstOrDefault();
                    }
                },
                endpoint);
        }

        private void Accept(string endpointId) {
            Application.Current.Dispatcher.BeginInvoke(
                (string endpointId) => {
                    Log("Accepting...");
                    Log("  endpoint_id: " + endpointId);
                    OperationResult status = NearbyConnections.AcceptConnection(
                        core, endpointId, payloadInitiatedCallback, payloadProgressCallback);
                    Log("  status: " + status.ToString());
                },
                endpointId);
        }

        private void ClearLog() {
            LogEntries.Clear();
        }

        private bool CanClearLog() {
            return LogEntries.Count > 0;
        }
        #endregion

        #region Callbacks
        private void OnEndpointFound(string endpointId, byte[] endpointInfo, int size, Medium medium, string serviceId) {
            var _ = size;
            string endpointName = Encoding.UTF8.GetString(endpointInfo);
            Log("OnEndPointFound:");
            Log("  endpoint_id: " + endpointId);
            Log("  endpoint_info: " + endpointName);
            Log("  medium: " + medium);
            Log("  service_id: " + serviceId);

            if (GetEndpoint(endpointId) != null) {
                Log("  endpoint already in the list!");
                return;
            }

            AddEndpoint(new EndpointModel(
                id: endpointId,
                name: endpointName,
                state: EndpointModel.State.Discovered) {
                medium = medium,
            });
        }

        private void OnEndpointLost(string endpointId) {
            Log("OnEndpointLost: " + endpointId);
            RemoveEndpoint(endpointId);
        }

        private void OnEndpointDistanceChanged(string endpointId, DistanceInfo distanceInfo) {
            Log("OnEndpointDistanceChanged: ");
            Log("  endpoint_id: " + endpointId);
            Log("  distance_info: " + distanceInfo);
        }

        private void OnConnectionInitiated(string endpointId, byte[] endpointInfo, int size) {
            Log("OnConnectionInitiated:");
            Log("  endpoint_id: " + endpointId);

            Application.Current.Dispatcher.BeginInvoke(
                (string endpointId) => Accept(endpointId), endpointId);

            if (GetEndpoint(endpointId) == null) {
                // We didn't discover this endpoint. Add it to the list of remote endpoints.
                string name = Encoding.UTF8.GetString(endpointInfo);
                if (String.IsNullOrEmpty(name)) {
                    Log(string.Format("Endpoint {0} has an invalid name, discarding...", endpointId));
                    return;
                }
                // Since we didn't discover this endpoint, we should remove it after we
                // disconnect from it later. So let's mark it as "incoming".
                EndpointModel endpoint = new EndpointModel(
                    id: endpointId,
                    name: name,
                    state: EndpointModel.State.Pending,
                    isIncoming: true);
                AddEndpoint(endpoint);
            } else {
                SetEndpointState(endpointId, EndpointModel.State.Pending);
            }

            SelectEndpoint(endpointId);
        }

        private void OnConnectionAccepted(string endpointId) {
            Log("OnConnectionAccepted:");
            Log("  endpoint_id: " + endpointId);

            SetEndpointState(endpointId, EndpointModel.State.Connected);
        }

        private void RemoveOrChangeState(EndpointViewModel? endpoint) {
            if (endpoint == null) {
                return;
            }

            // If the endpoint wasn't discovered by us in the first place, remove it.
            // Otherwise, keep it and change its state to Discovered.
            if (endpoint.IsIncoming || !isDiscovering) {
                Application.Current.Dispatcher.BeginInvoke(Endpoints.Remove, endpoint);
            } else {
                endpoint.State = EndpointModel.State.Discovered;
            }
        }

        private void OnConnectionRejected(string endpointId, OperationResult result) {
            Log("OnConnectionRejected:");
            Log("  endpoint_id: " + endpointId);
            Log("  result: " + result.ToString());
            RemoveOrChangeState(GetEndpoint(endpointId));
        }

        private void OnConnectionDisconnected(string endpointId) {
            Log("OnConnectionDisconnected:");
            Log("  endpoint_id: " + endpointId);

            EndpointViewModel? endpoint = GetEndpoint(endpointId);
            RemoveOrChangeState(endpoint);
            endpoint?.ClearTransfers();
            endpoint?.ClearIncomingFiles();
            endpoint?.ClearOutgoingFiles();
        }

        private void OnBandwidthUpgraded(string endpointId, Medium medium) {
            Log("OnBandwidthUpgraded:");
            Log("  endpoint_id: " + endpointId);
            Log("  medium: " + medium.ToString());

            var endpoint = GetEndpoint(endpointId);
            if (endpoint != null) {
                endpoint.Medium = medium;
            }
        }

        // It's weird that NC passes the payload content before updating the progress.
        // We are supposed to monitor the progress and wait till it's completed before
        // consuming the payload content. But I'm not sure how it'd work for managed
        // code, since we cannot hold on to the buffer.
        // TODO: investigate how this works for Dart and Java
        private void OnPayloadInitiated(string endpointId, long payloadId, long payloadSize, byte[] payloadContent) {
            Log("OnPayloadInitiated:");
            Log("  endpoint_id: " + endpointId);
            Log("  payload_id: " + payloadId);

            EndpointViewModel? endpoint = GetEndpoint(endpointId);
            if (endpoint == null) {
                Log(string.Format("End point {0} not found. Probably already lost.", endpointId));
                return;
            }

            if (endpoint.State != EndpointModel.State.Sending) {
                endpoint.State = EndpointModel.State.Receiving;
                IEnumerable<IncomingFileModel>? files = DecodePayload(payloadContent);
                if (files == null) {
                    Log("Invalid payload:");
                    Log("  endpoint_id: " + endpointId);
                    Log("  payload_id: " + payloadId);
                    return;
                }

                foreach (IncomingFileModel file in files) {
                    TransferModel transfer = new(
                        direction: TransferModel.Direction.Receive,
                        fileName: file.localPath,
                        url: file.remotePath,
                        result: TransferModel.Result.Success);
                    endpoint.AddTransfer(transfer);
                    endpoint.AddIncomingFile(file);
                }
            }
        }

        private void OnPayloadProgress(string endpointId, long payloadId, PayloadStatus status,
            long bytesTotal, long bytesTransferred) {
            Log("OnPayloadProgress:");
            Log("  endpoint_id: " + endpointId);
            Log("  paylod_id: " + payloadId.ToString());
            Log("  status: " + status.ToString());
            Log(string.Format("  bytes transferred: {0}/{1}", bytesTransferred, bytesTotal));

            bool isSending = GetEndpoint(endpointId)?.State == EndpointModel.State.Sending;
            TransferModel.Result result;
            EndpointViewModel? endpoint = GetEndpoint(endpointId);

            switch (status) {
            case PayloadStatus.kInProgress:
                return;
            case PayloadStatus.kSuccess:
                result = TransferModel.Result.Success;
                SetEndpointState(endpointId, EndpointModel.State.Connected);
                if (isSending) {
                    endpoint?.ClearOutgoingFiles();
                }
                break;
            case PayloadStatus.kFailure:
                result = TransferModel.Result.Failure;
                SetEndpointState(endpointId, EndpointModel.State.Connected);
                break;
            case PayloadStatus.kCanceled:
                result = TransferModel.Result.Canceled;
                SetEndpointState(endpointId, EndpointModel.State.Connected);
                break;
            default:
                return;
            }

            if (isSending && endpoint != null) {
                foreach (OutgoingFileViewModel file in endpoint!.OutgoingFiles) {
                    TransferModel transfer = new(
                        direction: TransferModel.Direction.Send,
                        fileName: file.LocalPath,
                        url: file.RemotePath!,
                        result: result);
                    endpoint.AddTransfer(transfer);
                }
            }
        }

        private static (int, byte[]) EncodePayload(IEnumerable<OutgoingFileModel> files) {
            // Buffer format:
            // int32: file count
            // Each file:
            //   int32: file name length including the \x0 at the end, not including this int
            //   file name string content, encoded in UTF8
            //   \x0
            //   int32: url length, including the \x0 at the end, not including this int
            //   url content, encoded in UTF8
            //   \x0
            //   int64: file size, in bytes
            // In other words, file names and URLs are in both Pascal and C styles, to make
            // decoding a bit easier.

            static void WriteStringToStream(MemoryStream stream, string s) {
                int len = s.Length;
                byte[] buffer = BitConverter.GetBytes(len + 1);
                stream.Write(buffer, 0, buffer.Length);

                buffer = Encoding.UTF8.GetBytes(s);
                stream.Write(buffer, 0, buffer.Length);
                stream.WriteByte(0);
            }

            MemoryStream stream = new();
            byte[] buffer;
            int fileCount = 0;

            // File count placeholder, since we haven't counted yet
            buffer = BitConverter.GetBytes(0);
            stream.Write(buffer, 0, buffer.Length);

            foreach (OutgoingFileModel file in files) {
                WriteStringToStream(stream, file.localPath);
                WriteStringToStream(stream, file.remotePath!);
                buffer = BitConverter.GetBytes(file.fileSize);
                stream.Write(buffer);
                fileCount++;
            }

            // Now write the actual file count
            stream.Position = 0;
            buffer = BitConverter.GetBytes(fileCount);
            stream.Write(buffer, 0, buffer.Length);
            return (fileCount, stream.ToArray());
        }

        private static IList<IncomingFileModel>? DecodePayload(byte[] payload) {
            static string ReadString(byte[] payload, ref int offset) {
                int len = BitConverter.ToInt32(payload, offset);
                offset += sizeof(int);
                string s = Encoding.UTF8.GetString(payload, offset, len - 1);
                offset += len;
                return s;
            }

            List<IncomingFileModel> files = new();

            try {
                int offset = 0;
                int fileCount = BitConverter.ToInt32(payload, 0);
                offset += sizeof(int);

                for (int i = 0; i < fileCount; i++) {
                    string path = ReadString(payload, ref offset);
                    string remotePath = ReadString(payload, ref offset);
                    long fileSize = BitConverter.ToInt64(payload, offset);
                    offset += sizeof(long);
                    files.Add(new(path, remotePath, fileSize));
                }

                return files;
            } catch (Exception) { return null; }

        }
        #endregion
    }
}
