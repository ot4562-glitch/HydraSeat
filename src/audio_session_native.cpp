#include "hydra/audio_routing.hpp"

#include <atomic>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#endif

namespace hydra::audio {
namespace {

#if defined(_WIN32)

#ifndef AUDCLNT_S_NO_SINGLE_PROCESS
#define AUDCLNT_S_NO_SINGLE_PROCESS _HRESULT_TYPEDEF_(0x0889000DL)
#endif

std::string hresultError(const char* prefix, HRESULT value) {
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%08lx",
                  static_cast<unsigned long>(value));
    return std::string(prefix) + ": 0x" + buffer;
}

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept
        : value_(std::exchange(other.value_, nullptr)) {}
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    T* get() const noexcept { return value_; }
    T* operator->() const noexcept { return value_; }
    T** put() noexcept {
        reset();
        return &value_;
    }
    explicit operator bool() const noexcept { return value_ != nullptr; }
    void reset() noexcept {
        if (value_ != nullptr) value_->Release();
        value_ = nullptr;
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

std::uint64_t fileTimeValue(const FILETIME& value) noexcept {
    ULARGE_INTEGER combined{};
    combined.LowPart = value.dwLowDateTime;
    combined.HighPart = value.dwHighDateTime;
    return combined.QuadPart;
}

bool queryExactProcessIdentity(std::uint32_t processId,
                               std::uint64_t& creationTime100ns,
                               std::wstring& executablePath) noexcept {
    creationTime100ns = 0;
    executablePath.clear();
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) return false;

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    const BOOL gotTimes =
        GetProcessTimes(process, &creation, &exit, &kernel, &user);

    std::vector<wchar_t> path(4096u, L'\0');
    DWORD pathChars = static_cast<DWORD>(path.size());
    const BOOL gotPath = QueryFullProcessImageNameW(
        process, 0, path.data(), &pathChars);
    CloseHandle(process);

    if (gotTimes == FALSE || gotPath == FALSE || pathChars == 0 ||
        pathChars >= path.size()) {
        return false;
    }
    creationTime100ns = fileTimeValue(creation);
    executablePath.assign(path.data(), pathChars);
    return creationTime100ns != 0 && !executablePath.empty();
}

SessionState sessionState(AudioSessionState value) noexcept {
    switch (value) {
        case AudioSessionStateInactive: return SessionState::Inactive;
        case AudioSessionStateActive: return SessionState::Active;
        case AudioSessionStateExpired: return SessionState::Expired;
    }
    return SessionState::Inactive;
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

bool enumerateEndpointSessions(IMMDeviceEnumerator* enumerator,
                               const EndpointRecord& endpoint,
                               std::vector<SessionRecord>& sessions,
                               std::string& error) {
    if (endpoint.flow != DataFlow::Render ||
        !isEndpointCurrentlyAvailable(endpoint)) {
        return true;
    }

    ComPtr<IMMDevice> device;
    HRESULT result = enumerator->GetDevice(endpoint.endpointId.c_str(), device.put());
    if (FAILED(result)) {
        error = hresultError("render endpoint lookup failed", result);
        return false;
    }

    ComPtr<IAudioSessionManager2> manager;
    result = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER,
                              nullptr, reinterpret_cast<void**>(manager.put()));
    if (FAILED(result)) {
        error = hresultError("audio session manager activation failed", result);
        return false;
    }

    ComPtr<IAudioSessionEnumerator> sessionEnumerator;
    result = manager->GetSessionEnumerator(sessionEnumerator.put());
    if (FAILED(result)) {
        error = hresultError("audio session enumerator query failed", result);
        return false;
    }

    int count = 0;
    result = sessionEnumerator->GetCount(&count);
    if (FAILED(result) || count < 0 ||
        static_cast<std::size_t>(count) + sessions.size() > kMaxAudioSessions) {
        error = FAILED(result)
            ? hresultError("audio session count query failed", result)
            : "native audio session count exceeds the bounded inventory";
        return false;
    }

    for (int index = 0; index < count; ++index) {
        ComPtr<IAudioSessionControl> control;
        result = sessionEnumerator->GetSession(index, control.put());
        if (FAILED(result)) {
            error = hresultError("audio session item query failed", result);
            return false;
        }

        ComPtr<IAudioSessionControl2> control2;
        result = control->QueryInterface(__uuidof(IAudioSessionControl2),
                                         reinterpret_cast<void**>(control2.put()));
        if (FAILED(result)) {
            error = hresultError("audio session control2 query failed", result);
            return false;
        }

        ScopedCoTaskString instanceId;
        result = control2->GetSessionInstanceIdentifier(instanceId.put());
        if (FAILED(result) || instanceId.get() == nullptr) {
            error = hresultError("audio session instance ID query failed", result);
            return false;
        }
        ScopedCoTaskString sessionId;
        result = control2->GetSessionIdentifier(sessionId.put());
        if (FAILED(result) || sessionId.get() == nullptr) {
            error = hresultError("audio session identifier query failed", result);
            return false;
        }

        AudioSessionState nativeState = AudioSessionStateInactive;
        result = control2->GetState(&nativeState);
        if (FAILED(result)) {
            error = hresultError("audio session state query failed", result);
            return false;
        }

        const HRESULT systemResult = control2->IsSystemSoundsSession();
        if (FAILED(systemResult)) {
            error = hresultError("audio system-session query failed", systemResult);
            return false;
        }
        const bool systemSounds = systemResult == S_OK;

        DWORD processId = 0;
        const HRESULT processResult = control2->GetProcessId(&processId);
        const bool spansMultiple = processResult == AUDCLNT_S_NO_SINGLE_PROCESS;
        if (FAILED(processResult) && !spansMultiple) {
            error = hresultError("audio session process ID query failed", processResult);
            return false;
        }

        SessionRecord record;
        record.endpointId = endpoint.endpointId;
        record.sessionInstanceId = instanceId.get();
        record.sessionIdentifier = sessionId.get();
        record.processId = static_cast<std::uint32_t>(processId);
        record.state = sessionState(nativeState);
        record.spansMultipleProcesses = spansMultiple;
        record.systemSoundsSession = systemSounds;
        if (!systemSounds && processId != 0 && !spansMultiple) {
            record.processIdentityVerified = queryExactProcessIdentity(
                record.processId, record.processCreationTime100ns,
                record.executablePath);
        }
        sessions.push_back(std::move(record));
    }
    return true;
}

class NativeSessionSource final : public SessionSource {
public:
    bool enumerate(std::span<const EndpointRecord> endpoints,
                   std::vector<SessionRecord>& sessions,
                   std::string& error) noexcept override {
        try {
            ComApartment apartment;
            if (!apartment.usable()) {
                error = hresultError("COM initialization failed", apartment.result());
                return false;
            }
            ComPtr<IMMDeviceEnumerator> enumerator;
            if (!createEnumerator(enumerator, error)) return false;

            sessions.clear();
            for (const auto& endpoint : endpoints) {
                if (!enumerateEndpointSessions(enumerator.get(), endpoint,
                                               sessions, error)) {
                    return false;
                }
            }
            error.clear();
            return true;
        } catch (...) {
            error = "audio session enumeration raised an unexpected exception";
            return false;
        }
    }

    // Session creation is intentionally observed by explicit bounded refreshes.
    // P5-AUD-02 does not keep COM session callbacks alive across endpoint
    // replacement yet; a later production runtime may add that optimization.
    std::uint64_t changeGeneration() const noexcept override { return 1u; }
};

#else

class NativeSessionSource final : public SessionSource {
public:
    bool enumerate(std::span<const EndpointRecord>,
                   std::vector<SessionRecord>& sessions,
                   std::string& error) noexcept override {
        sessions.clear();
        error = "Windows audio session observation is unavailable on this platform";
        return false;
    }
    std::uint64_t changeGeneration() const noexcept override { return 0u; }
};

#endif

} // namespace

std::shared_ptr<SessionSource> makeNativeSessionSource() {
    return std::make_shared<NativeSessionSource>();
}

} // namespace hydra::audio
