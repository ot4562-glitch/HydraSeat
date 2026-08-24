#pragma once

#include "hydra/workspace_manager.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace hydra {

enum class InputIsolationCapability : std::uint32_t {
    None = 0,
    RawInputObservation = 1u << 0,
    PhysicalDeviceSuppression = 1u << 1,
    PerProcessKeyboardState = 1u << 2,
    PerProcessMouseState = 1u << 3,
    PerSeatCursor = 1u << 4,
    ForegroundVirtualization = 1u << 5,
    VirtualInputInjection = 1u << 6
};

constexpr InputIsolationCapability operator|(InputIsolationCapability left,
                                             InputIsolationCapability right) noexcept {
    return static_cast<InputIsolationCapability>(
        static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

constexpr bool hasCapability(InputIsolationCapability available,
                             InputIsolationCapability required) noexcept {
    return (static_cast<std::uint32_t>(available) & static_cast<std::uint32_t>(required)) ==
           static_cast<std::uint32_t>(required);
}

struct InputRouteDecision {
    std::optional<SeatId> seatId;
    std::uint64_t targetHwnd{0};
    bool consumePhysicalInput{false};

    bool operator==(const InputRouteDecision&) const = default;
};

class SeatRoutingPolicy {
public:
    bool bindDevice(std::wstring deviceId, SeatId seatId);
    bool unbindDevice(std::wstring_view deviceId);
    void clearSeat(SeatId seatId);

    std::optional<SeatId> ownerOf(std::wstring_view deviceId) const;
    InputRouteDecision route(std::wstring_view deviceId,
                             const WorkspaceManager& seats,
                             bool isolationRequested) const;

private:
    static std::wstring normalize(std::wstring_view value);
    std::unordered_map<std::wstring, SeatId> m_deviceOwners;
};

class InputIsolationBackend {
public:
    virtual ~InputIsolationBackend() = default;

    virtual std::wstring_view name() const noexcept = 0;
    virtual InputIsolationCapability capabilities() const noexcept = 0;
    virtual bool start() = 0;
    virtual void stop() noexcept = 0;

    // Phase 3 skeleton: concrete backends must later provide a proven way to
    // prevent cross-seat input bleed. Returning false means the backend cannot
    // currently enforce the requested route safely.
    virtual bool applyRoute(std::wstring_view physicalDeviceId,
                            const InputRouteDecision& decision) = 0;
};

class UnsupportedIsolationBackend final : public InputIsolationBackend {
public:
    std::wstring_view name() const noexcept override { return L"unsupported"; }
    InputIsolationCapability capabilities() const noexcept override {
        return InputIsolationCapability::RawInputObservation;
    }
    bool start() override { return true; }
    void stop() noexcept override {}
    bool applyRoute(std::wstring_view, const InputRouteDecision&) override { return false; }
};

} // namespace hydra
