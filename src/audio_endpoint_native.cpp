#include "hydra/audio_endpoint_inventory.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#endif

namespace hydra::audio {
namespace {

#if defined(_WIN32)

std::string hresultError(const char* prefix, HRESULT value) {
    return std::string(prefix) + ": 0x" + [] (HRESULT hr) {
        char buffer[16]{};
        std::snprintf(buffer, sizeof(buffer), "%08lx",
                      static_cast<unsigned long>(hr));
        return std::string(buffer);
    }(value);
}

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* value) noexcept : value_(value) {}
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }

    T* get() const noexcept { return value_; }
    T* operator->() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }

    T** put() noexcept {
        reset();
        return &value_;
    }

    void reset(T* value = nullptr) noexcept {
        if (value_ != nullptr) value_->Release();
        value_ = value;
    }

private:
    T* value_{nullptr};
};

class ComApartment {
public:
    ComApartment() noexcept {
        result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        usable_ = SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
        owns_ = SUCCEEDED(result_);
    }
    ~ComApartment() {
        if (owns_) CoUninitialize();
    }

    bool usable() const noexcept { return usable_; }
    HRESULT result() const noexcept { return result_; }

private:
    HRESULT result_{E_FAIL};
    bool usable_{false};
    bool owns_{false};
};

class ScopedCoTaskString {
public:
    ~ScopedCoTaskString() {
        if (value_ != nullptr) CoTaskMemFree(value_);
    }
    LPWSTR* put() noexcept { return &value_; }
    LPCWSTR get() const noexcept { return value_; }
private:
    LPWSTR value_{nullptr};
};

class ScopedPropVariant {
public:
    ScopedPropVariant() { PropVariantInit(&value_); }
    ~ScopedPropVariant() { PropVariantClear(&value_); }
    PROPVARIANT* get() noexcept { return &value_; }
    const PROPVARIANT& value() const noexcept { return value_; }
private:
    PROPVARIANT value_{};
};

std::wstring canonicalId(std::wstring_view value) {
    std::wstring result(value);
    for (auto& character : result) {
        character = static_cast<wchar_t>(std::towupper(character));
    }
    return result;
}

bool createEnumerator(ComPtr<IMMDeviceEnumerator>& enumerator,
                      std::string& error) {
    const HRESULT result = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(enumerator.put()));
    if (FAILED(result)) {
        error = hresultError("MMDeviceEnumerator creation failed", result);
        return false;
    }
    return true;
}

std::uint8_t roleBit(ERole role) noexcept {
    switch (role) {
        case eConsole: return kDefaultRoleConsole;
        case eMultimedia: return kDefaultRoleMultimedia;
        case eCommunications: return kDefaultRoleCommunications;
        default: return 0;
    }
}

bool readFriendlyName(IMMDevice* device, std::wstring& name) {
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, store.put()))) return false;
    ScopedPropVariant value;
    if (FAILED(store->GetValue(PKEY_Device_FriendlyName, value.get()))) return false;
    if (value.value().vt == VT_LPWSTR && value.value().pwszVal != nullptr) {
        name = value.value().pwszVal;
        if (name.size() > kMaxFriendlyNameChars) {
            name.resize(kMaxFriendlyNameChars);
        }
        return true;
    }
    name.clear();
    return true;
}

bool enumerateFlow(IMMDeviceEnumerator* enumerator,
                   EDataFlow nativeFlow,
                   DataFlow flow,
                   std::vector<EndpointRecord>& endpoints,
                   std::string& error) {
    ComPtr<IMMDeviceCollection> collection;
    const HRESULT listed = enumerator->EnumAudioEndpoints(
        nativeFlow, DEVICE_STATEMASK_ALL, collection.put());
    if (FAILED(listed)) {
        error = hresultError("audio endpoint enumeration failed", listed);
        return false;
    }

    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) {
        error = "audio endpoint collection count query failed";
        return false;
    }
    if (static_cast<std::size_t>(count) + endpoints.size() > kMaxAudioEndpoints) {
        error = "native audio endpoint count exceeds the bounded inventory";
        return false;
    }

    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(index, device.put()))) {
            error = "audio endpoint collection item query failed";
            return false;
        }

        ScopedCoTaskString endpointId;
        if (FAILED(device->GetId(endpointId.put())) || endpointId.get() == nullptr) {
            error = "audio endpoint stable ID query failed";
            return false;
        }
        const std::wstring id(endpointId.get());
        if (id.empty() || id.size() > kMaxEndpointIdChars) {
            error = "native audio endpoint stable ID exceeds the bound";
            return false;
        }

        DWORD state = 0;
        if (FAILED(device->GetState(&state)) || state == 0 ||
            (state & ~kKnownEndpointStateMask) != 0) {
            error = "native audio endpoint state is malformed or unsupported";
            return false;
        }

        std::wstring friendlyName;
        (void)readFriendlyName(device.get(), friendlyName);
        endpoints.push_back(
            {id, std::move(friendlyName), flow,
             static_cast<std::uint32_t>(state), 0});
    }
    return true;
}

bool applyDefaultRoles(IMMDeviceEnumerator* enumerator,
                       EDataFlow nativeFlow,
                       DataFlow flow,
                       std::vector<EndpointRecord>& endpoints,
                       std::string& error) {
    constexpr ERole roles[]{eConsole, eMultimedia, eCommunications};
    for (const auto role : roles) {
        ComPtr<IMMDevice> device;
        const HRESULT queried = enumerator->GetDefaultAudioEndpoint(
            nativeFlow, role, device.put());
        if (queried == E_NOTFOUND) continue;
        if (FAILED(queried)) {
            error = hresultError("default audio endpoint query failed", queried);
            return false;
        }
        ScopedCoTaskString endpointId;
        if (FAILED(device->GetId(endpointId.put())) || endpointId.get() == nullptr) {
            error = "default audio endpoint stable ID query failed";
            return false;
        }
        const auto wanted = canonicalId(endpointId.get());
        const auto found = std::find_if(
            endpoints.begin(), endpoints.end(),
            [&](const EndpointRecord& endpoint) {
                return endpoint.flow == flow &&
                       canonicalId(endpoint.endpointId) == wanted;
            });
        if (found == endpoints.end()) {
            error = "default audio endpoint is absent from the all-state inventory";
            return false;
        }
        found->defaultRoleMask = static_cast<std::uint8_t>(
            found->defaultRoleMask | roleBit(role));
    }
    return true;
}

bool enumerateNative(std::vector<EndpointRecord>& endpoints,
                     std::string& error) noexcept {
    try {
        ComApartment apartment;
        if (!apartment.usable()) {
            error = hresultError("COM initialization failed", apartment.result());
            return false;
        }
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (!createEnumerator(enumerator, error)) return false;

        endpoints.clear();
        if (!enumerateFlow(enumerator.get(), eRender, DataFlow::Render,
                           endpoints, error) ||
            !enumerateFlow(enumerator.get(), eCapture, DataFlow::Capture,
                           endpoints, error) ||
            !applyDefaultRoles(enumerator.get(), eRender, DataFlow::Render,
                               endpoints, error) ||
            !applyDefaultRoles(enumerator.get(), eCapture, DataFlow::Capture,
                               endpoints, error)) {
            return false;
        }
        error.clear();
        return true;
    } catch (...) {
        error = "audio endpoint enumeration raised an unexpected exception";
        return false;
    }
}

class NotificationClient final : public IMMNotificationClient {
public:
    explicit NotificationClient(std::atomic<std::uint64_t>& generation)
        : generation_(generation) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, __uuidof(IMMNotificationClient))) {
            *object = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return refs_.fetch_add(1u, std::memory_order_relaxed) + 1u;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining =
            refs_.fetch_sub(1u, std::memory_order_acq_rel) - 1u;
        if (remaining == 0u) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        changed();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {
        changed();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {
        changed();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override {
        changed();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
        changed();
        return S_OK;
    }

private:
    void changed() noexcept {
        generation_.fetch_add(1u, std::memory_order_release);
    }

    std::atomic<ULONG> refs_{1u};
    std::atomic<std::uint64_t>& generation_;
};

class NativeEndpointSource final : public EndpointSource {
public:
    NativeEndpointSource() {
        monitorThread_ = std::thread([this] { monitorLoop(); });
        std::unique_lock lock(mutex_);
        (void)condition_.wait_for(
            lock, std::chrono::seconds(2), [this] { return monitorReady_; });
    }

    ~NativeEndpointSource() override {
        {
            std::lock_guard lock(mutex_);
            stopRequested_ = true;
        }
        condition_.notify_all();
        if (monitorThread_.joinable()) monitorThread_.join();
    }

    bool enumerate(std::vector<EndpointRecord>& endpoints,
                   std::string& error) noexcept override {
        return enumerateNative(endpoints, error);
    }

    std::uint64_t changeGeneration() const noexcept override {
        return generation_.load(std::memory_order_acquire);
    }

    bool notificationsAvailable() const noexcept override {
        return notificationsAvailable_.load(std::memory_order_acquire);
    }

private:
    void monitorLoop() noexcept {
        ComApartment apartment;
        ComPtr<IMMDeviceEnumerator> enumerator;
        std::string error;
        NotificationClient* callback = nullptr;
        bool registered = false;

        if (apartment.usable() && createEnumerator(enumerator, error)) {
            callback = new NotificationClient(generation_);
            const HRESULT result =
                enumerator->RegisterEndpointNotificationCallback(callback);
            registered = SUCCEEDED(result);
            notificationsAvailable_.store(registered, std::memory_order_release);
        }

        {
            std::lock_guard lock(mutex_);
            monitorReady_ = true;
        }
        condition_.notify_all();

        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopRequested_; });
        }

        notificationsAvailable_.store(false, std::memory_order_release);
        if (registered && enumerator && callback != nullptr) {
            (void)enumerator->UnregisterEndpointNotificationCallback(callback);
        }
        if (callback != nullptr) callback->Release();
    }

    std::atomic<std::uint64_t> generation_{1u};
    std::atomic<bool> notificationsAvailable_{false};
    std::thread monitorThread_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool monitorReady_{false};
    bool stopRequested_{false};
};

#else

class NativeEndpointSource final : public EndpointSource {
public:
    bool enumerate(std::vector<EndpointRecord>& endpoints,
                   std::string& error) noexcept override {
        endpoints.clear();
        error = "Windows Core Audio endpoint inventory is unavailable on this platform";
        return false;
    }
    std::uint64_t changeGeneration() const noexcept override { return 0; }
    bool notificationsAvailable() const noexcept override { return false; }
};

#endif

} // namespace

std::shared_ptr<EndpointSource> makeNativeEndpointSource() {
    return std::make_shared<NativeEndpointSource>();
}

} // namespace hydra::audio
