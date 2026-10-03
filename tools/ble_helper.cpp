/* Windows標準のC++/WinRT APIを、C側から使えるBLE補助層として実装する。 */

#include "ble_helper.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;

namespace {

constexpr wchar_t SERVICE_UUID[] = L"6e40fc00-b5a3-f393-e0a9-e50e24dcca9e";
constexpr wchar_t WRITE_UUID[] = L"6e40fc20-b5a3-f393-e0a9-e50e24dcca9e";
constexpr wchar_t NOTIFY_UUID[] = L"6e40fc21-b5a3-f393-e0a9-e50e24dcca9e";

std::once_flag g_winrt_initialized;

bool parse_address(const char *text, uint64_t *out) {
    char digits[13] = {};
    size_t count = 0;
    char *end = nullptr;
    unsigned long long value;

    if (text == nullptr || out == nullptr) {
        return false;
    }
    for (; *text != '\0'; ++text) {
        const char c = *text;
        const bool hexadecimal = (c >= '0' && c <= '9') ||
                                 (c >= 'a' && c <= 'f') ||
                                 (c >= 'A' && c <= 'F');
        if (hexadecimal) {
            if (count == 12) {
                return false;
            }
            digits[count++] = c;
        } else if (c != ':' && c != '-') {
            return false;
        }
    }
    if (count != 12) {
        return false;
    }
    value = std::strtoull(digits, &end, 16);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    *out = static_cast<uint64_t>(value);
    return true;
}

bool status_ok(GattCommunicationStatus status) {
    return status == GattCommunicationStatus::Success;
}

} // 名前空間

struct zk_ble_helper {
    BluetoothLEDevice device{nullptr};
    GattDeviceService service{nullptr};
    GattCharacteristic write_characteristic{nullptr};
    GattCharacteristic notify_characteristic{nullptr};
    event_token value_changed_token{};
    bool value_changed_registered = false;
    std::mutex mutex;
    std::condition_variable notification_ready;
    std::deque<std::vector<uint8_t>> notifications;
    std::string last_error;
};

static void set_error(zk_ble_helper *helper, const char *message) {
    if (helper != nullptr) {
        std::lock_guard<std::mutex> lock(helper->mutex);
        helper->last_error = message;
    }
}

extern "C" int ble_helper_open(zk_ble_helper **out, const char *address) {
    uint64_t bluetooth_address = 0;
    zk_ble_helper *helper = nullptr;

    if (out == nullptr || !parse_address(address, &bluetooth_address)) {
        return -1;
    }
    *out = nullptr;

    try {
        std::call_once(g_winrt_initialized, [] { winrt::init_apartment(); });
        helper = new zk_ble_helper();
        helper->device = BluetoothLEDevice::FromBluetoothAddressAsync(bluetooth_address).get();
        if (helper->device == nullptr) {
            set_error(helper, "指定したBluetoothアドレスへ接続できません");
            ble_helper_close(helper);
            return -2;
        }

        const auto service_result = helper->device.GetGattServicesForUuidAsync(
            guid{SERVICE_UUID}, BluetoothCacheMode::Uncached).get();
        if (!status_ok(service_result.Status()) || service_result.Services().Size() == 0) {
            set_error(helper, "FreeFit GATT serviceが見つかりません");
            ble_helper_close(helper);
            return -3;
        }
        helper->service = service_result.Services().GetAt(0);

        const auto write_result = helper->service.GetCharacteristicsForUuidAsync(
            guid{WRITE_UUID}, BluetoothCacheMode::Uncached).get();
        const auto notify_result = helper->service.GetCharacteristicsForUuidAsync(
            guid{NOTIFY_UUID}, BluetoothCacheMode::Uncached).get();
        if (!status_ok(write_result.Status()) || write_result.Characteristics().Size() == 0 ||
            !status_ok(notify_result.Status()) || notify_result.Characteristics().Size() == 0) {
            set_error(helper, "WriteまたはNotify characteristicが見つかりません");
            ble_helper_close(helper);
            return -4;
        }
        helper->write_characteristic = write_result.Characteristics().GetAt(0);
        helper->notify_characteristic = notify_result.Characteristics().GetAt(0);

        helper->value_changed_token = helper->notify_characteristic.ValueChanged(
            [helper](const GattCharacteristic &, const GattValueChangedEventArgs &args) {
                DataReader reader = DataReader::FromBuffer(args.CharacteristicValue());
                std::vector<uint8_t> value(reader.UnconsumedBufferLength());
                if (!value.empty()) {
                    reader.ReadBytes(array_view<uint8_t>(value.data(), value.data() + value.size()));
                }
                {
                    std::lock_guard<std::mutex> lock(helper->mutex);
                    helper->notifications.push_back(std::move(value));
                }
                helper->notification_ready.notify_all();
            });
        helper->value_changed_registered = true;

        const auto subscribe_status = helper->notify_characteristic
            .WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
        if (!status_ok(subscribe_status)) {
            set_error(helper, "Notify購読（CCCD書込み）に失敗しました");
            ble_helper_close(helper);
            return -5;
        }
        *out = helper;
        return 0;
    } catch (const hresult_error &error) {
        if (helper != nullptr) {
            const std::string message = to_string(error.message());
            set_error(helper, message.c_str());
            ble_helper_close(helper);
        }
        return -6;
    } catch (...) {
        if (helper != nullptr) {
            set_error(helper, "Windows Bluetooth APIで予期しない例外が発生しました");
            ble_helper_close(helper);
        }
        return -7;
    }
}

extern "C" int ble_helper_write_without_response(void *user, const uint8_t *value,
                                                   size_t value_len) {
    auto *helper = static_cast<zk_ble_helper *>(user);
    if (helper == nullptr || value == nullptr || value_len == 0) {
        return -1;
    }
    try {
        DataWriter writer;
        writer.WriteBytes(array_view<const uint8_t>(value, value + value_len));
        const auto status = helper->write_characteristic.WriteValueAsync(
            writer.DetachBuffer(), GattWriteOption::WriteWithoutResponse).get();
        if (!status_ok(status)) {
            set_error(helper, "Write Without Responseが失敗しました");
            return -2;
        }
        return 0;
    } catch (const hresult_error &error) {
        const std::string message = to_string(error.message());
        set_error(helper, message.c_str());
        return -3;
    } catch (...) {
        set_error(helper, "Write Without Response中に例外が発生しました");
        return -4;
    }
}

extern "C" int ble_helper_wait_notification(void *user, uint8_t expected_command,
                                              uint8_t *out, size_t out_capacity, size_t *out_len,
                                              uint32_t timeout_ms) {
    auto *helper = static_cast<zk_ble_helper *>(user);
    if (helper == nullptr || out == nullptr || out_len == nullptr) {
        return -1;
    }

    std::unique_lock<std::mutex> lock(helper->mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const auto it = std::find_if(helper->notifications.begin(), helper->notifications.end(),
            [expected_command](const std::vector<uint8_t> &value) {
                return value.size() >= 2 && value[0] == 0xE4 && value[1] == expected_command;
            });
        if (it != helper->notifications.end()) {
            if (it->size() > out_capacity) {
                helper->last_error = "通知バッファが小さすぎます";
                return -2;
            }
            memcpy(out, it->data(), it->size());
            *out_len = it->size();
            helper->notifications.erase(it);
            return 0;
        }
        if (helper->notification_ready.wait_until(lock, deadline) == std::cv_status::timeout) {
            helper->last_error = "Notify待機がタイムアウトしました";
            return -3;
        }
    }
}

extern "C" int ble_helper_wait_notification_prefix(void *user, const uint8_t *prefix, size_t prefix_len,
                                              uint8_t *out, size_t out_capacity, size_t *out_len,
                                              uint32_t timeout_ms) {
    auto *helper = static_cast<zk_ble_helper *>(user);
    if (helper == nullptr || out == nullptr || out_len == nullptr || prefix == nullptr || prefix_len == 0) {
        return -1;
    }

    std::unique_lock<std::mutex> lock(helper->mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const auto it = std::find_if(helper->notifications.begin(), helper->notifications.end(),
            [prefix, prefix_len](const std::vector<uint8_t> &value) {
                if (value.size() < prefix_len) return false;
                return std::equal(prefix, prefix + prefix_len, value.begin());
            });
        if (it != helper->notifications.end()) {
            if (it->size() > out_capacity) {
                helper->last_error = "通知バッファが小さすぎます";
                return -2;
            }
            memcpy(out, it->data(), it->size());
            *out_len = it->size();
            helper->notifications.erase(it);
            return 0;
        }
        if (helper->notification_ready.wait_until(lock, deadline) == std::cv_status::timeout) {
            helper->last_error = "Notify待機がタイムアウトしました";
            return -3;
        }
    }
}

extern "C" void ble_helper_sleep_ms(void *, uint32_t milliseconds) {
    Sleep(milliseconds);
}

extern "C" void ble_helper_close(zk_ble_helper *helper) {
    if (helper == nullptr) {
        return;
    }
    try {
        if (helper->value_changed_registered) {
            helper->notify_characteristic.ValueChanged(helper->value_changed_token);
        }
        helper->service.Close();
        helper->device.Close();
    } catch (...) {
        /* 終了処理では例外を呼出し元へ伝播させない。 */
    }
    delete helper;
}

extern "C" const char *ble_helper_last_error(const zk_ble_helper *helper) {
    if (helper == nullptr) {
        return "BLE接続の初期化に失敗しました";
    }
    return helper->last_error.empty() ? "詳細なエラー情報はありません" : helper->last_error.c_str();
}
