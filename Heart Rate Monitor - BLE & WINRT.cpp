#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <iostream>
#include <unordered_set>
#include <map>
#include <functional>
#include <thread>
#include <atomic>

using namespace winrt;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Storage::Streams;

class BluetoothLEManager {
public:
    BluetoothLEManager() {
        watcher.ScanningMode(BluetoothLEScanningMode::Active);
        onHeartRateMeasurementReceived = std::bind(&BluetoothLEManager::PrintHeartRateMeasurement, this, std::placeholders::_1, std::placeholders::_2);
    }

    void StartScanning() {
        StartWatcher();
    }

    void StopScanning() {
        StopWatcher();
    }

    void ConnectToDevice(int index) {
        if (indexedDevices.count(index)) {
            auto connectedDevice = Connect(indexedDevices.at(index));
            if (connectedDevice) {
                SubscribeToHeartRateMeasurement(connectedDevice);
            }
            else {
                std::wcout << L"Failed to connect to the device." << std::endl;
            }
        }
        else {
            std::wcout << L"Invalid index selected." << std::endl;
        }
    }

    void StopSubscription() {
        continueRunning.store(false);
    }

private:
    std::function<void(GattCharacteristic, GattValueChangedEventArgs)> onHeartRateMeasurementReceived;

    BluetoothLEAdvertisementWatcher watcher;
    std::unordered_set<uint64_t> uniqueDevices;
    std::map<int, uint64_t> indexedDevices;
    int deviceIndex = 1;
    std::atomic<bool> continueRunning{ true };

    void StartWatcher() {
        watcher.Received([&](const auto&, const BluetoothLEAdvertisementReceivedEventArgs& args) {
            HandleAdvertisement(args);
            });

        watcher.Start();
        std::wcout << L"Scanning for devices. Press Enter to stop scanning." << std::endl;
    }

    void StopWatcher() {
        std::wstring input;
        std::getline(std::wcin, input);
        watcher.Stop();
    }

    void HandleAdvertisement(const BluetoothLEAdvertisementReceivedEventArgs& args) {
        auto deviceAddress = args.BluetoothAddress();
        if (uniqueDevices.insert(deviceAddress).second) {
            indexedDevices[deviceIndex] = deviceAddress;
            auto localName = args.Advertisement().LocalName();
            localName = localName.empty() ? L"Unknown" : localName;
            std::wcout << L"[" << deviceIndex << L"] Device found: " << localName.c_str() << L" (" << deviceAddress << L")" << std::endl;
            deviceIndex++;
        }
    }

    BluetoothLEDevice Connect(uint64_t bluetoothAddress) {
        BluetoothLEDevice bleDevice = nullptr;
        try {
            auto bleDeviceOperation = BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress).get();
            if (bleDeviceOperation) {
                std::wcout << L"Connected to device: " << bleDeviceOperation.DeviceId().c_str() << std::endl;
                bleDevice = bleDeviceOperation;
            }
            else {
                std::wcout << L"Failed to connect to device." << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
        return bleDevice;
    }

    void PrintHeartRateMeasurement(const GattCharacteristic&, const GattValueChangedEventArgs& args) {
        DataReader reader = DataReader::FromBuffer(args.CharacteristicValue());
        if (reader.UnconsumedBufferLength() > 0) {
            uint8_t flags = reader.ReadByte();
            uint16_t heartRateValue = 0;

            if (flags & 0x01) { // Heart Rate Value Format is in the second bit of flags
                heartRateValue = reader.ReadUInt16();
            }
            else {
                heartRateValue = reader.ReadByte();
            }

            std::wcout << L"Heart Rate Measurement: " << heartRateValue << L" bpm" << std::endl;
        }
    }

    void SubscribeToHeartRateMeasurement(BluetoothLEDevice& device) {
        auto hrServiceUuid = BluetoothUuidHelper::FromShortId(0x180D);
        auto hrMeasurementCharUuid = BluetoothUuidHelper::FromShortId(0x2A37);

        auto hrServiceResult = device.GetGattServicesForUuidAsync(hrServiceUuid).get();
        if (hrServiceResult.Status() != GattCommunicationStatus::Success) {
            std::wcout << L"Failed to find Heart Rate service." << std::endl;
            return;
        }

        auto hrService = hrServiceResult.Services().GetAt(0);
        auto hrCharResult = hrService.GetCharacteristicsForUuidAsync(hrMeasurementCharUuid).get();
        if (hrCharResult.Status() != GattCommunicationStatus::Success) {
            std::wcout << L"Failed to find Heart Rate Measurement characteristic." << std::endl;
            return;
        }

        auto hrChar = hrCharResult.Characteristics().GetAt(0);
        hrChar.ValueChanged(onHeartRateMeasurementReceived);

        auto status = hrChar.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
        if (status != GattCommunicationStatus::Success) {
            std::wcout << L"Failed to subscribe to Heart Rate Measurement notifications." << std::endl;
        }
        else {
            std::wcout << L"Subscribed to Heart Rate Measurement notifications." << std::endl;
        }

        // Start the event loop
        while (continueRunning.load())
        {
            std::this_thread::yield(); // Yield to other threads or processes
        }
    }
};

int main() {
    init_apartment();

    BluetoothLEManager manager;
    manager.StartScanning();
    manager.StopScanning();

    std::wcout << L"Select a device to connect (enter index): ";
    int selectedIndex;
    std::wcin >> selectedIndex;
    manager.ConnectToDevice(selectedIndex);

    return 0;
}
