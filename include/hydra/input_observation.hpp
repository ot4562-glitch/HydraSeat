#pragma once

#include "hydra/input_isolation.hpp"
#include "hydra/input_router.hpp"
#include "hydra/workspace_manager.hpp"

#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hydra {

enum class InputRouteDisposition {
    Routed,
    UnassignedDevice,
    AmbiguousSharedDevice,
    InactiveSeat,
    MissingTargetWindow,
    DispatchFailed
};

struct DeviceObservationSnapshot {
    std::wstring deviceId;
    std::wstring devicePath;
    std::uint32_t rawDevType{0};
    bool isTouchpad{false};
    bool online{false};

    std::uint64_t arrivals{0};
    std::uint64_t removals{0};
    std::uint64_t eventCount{0};
    std::uint64_t keyboardEventCount{0};
    std::uint64_t mouseEventCount{0};

    std::uint64_t lastSequence{0};
    std::uint64_t lastTimestampMicros{0};
    std::uint32_t lastVkey{0};
    RawKeyTransition lastKeyTransition{RawKeyTransition::None};

    std::int64_t totalDeltaX{0};
    std::int64_t totalDeltaY{0};
    std::int64_t totalWheelDelta{0};
    std::uint32_t mouseButtonsDown{0};
    std::vector<std::uint32_t> pressedKeys;

    bool operator==(const DeviceObservationSnapshot&) const = default;
};

struct SeatRouteSnapshot {
    SeatId seatId{0};
    std::uint64_t targetHwnd{0};
    std::uint64_t routedEvents{0};
    std::uint64_t keyboardEvents{0};
    std::uint64_t mouseEvents{0};
    std::uint64_t dispatchFailures{0};
    std::uint64_t lastSequence{0};
    std::wstring lastDeviceId;

    bool operator==(const SeatRouteSnapshot&) const = default;
};

struct InputRouteRecord {
    std::uint64_t sequence{0};
    std::wstring deviceId;
    std::optional<SeatId> seatId;
    std::uint64_t targetHwnd{0};
    InputRouteDisposition disposition{InputRouteDisposition::UnassignedDevice};
    bool physicalSuppressionRequested{false};

    bool operator==(const InputRouteRecord&) const = default;
};

struct RoutingBindingReport {
    std::size_t boundDevices{0};
    std::vector<std::wstring> ambiguousSharedDevices;

    bool operator==(const RoutingBindingReport&) const = default;
};

class InputObservationLedger {
public:
    void observeInput(const RawInputEvent& event);
    void observeDeviceChange(const RawInputDeviceChange& change);

    std::optional<DeviceObservationSnapshot> device(
        std::wstring_view deviceId) const;
    std::vector<DeviceObservationSnapshot> devices() const;

    std::uint64_t totalInputEvents() const noexcept { return m_totalInputEvents; }
    std::uint64_t totalDeviceChanges() const noexcept { return m_totalDeviceChanges; }

private:
    struct MutableDeviceState {
        DeviceObservationSnapshot snapshot;
        std::unordered_set<std::uint32_t> pressedKeys;
        std::unordered_set<std::uintptr_t> onlineHandles;
    };

    static std::wstring normalize(std::wstring_view value);
    static std::wstring eventDeviceKey(const RawInputEvent& event);
    static std::wstring changeDeviceKey(const RawInputDeviceChange& change);
    static void applyMouseButtonTransitions(DeviceObservationSnapshot& snapshot,
                                            std::uint16_t flags);

    MutableDeviceState& stateFor(std::wstring key);
    static DeviceObservationSnapshot snapshotOf(const MutableDeviceState& state);

    std::unordered_map<std::wstring, MutableDeviceState> m_devices;
    std::uint64_t m_totalInputEvents{0};
    std::uint64_t m_totalDeviceChanges{0};
};

using InputRouteDispatch =
    std::function<bool(const RawInputEvent&, const InputRouteDecision&)>;

class InputObservationSession {
public:
    InputObservationSession(WorkspaceManager& seats,
                            SeatRoutingPolicy& routingPolicy,
                            InputRouteDispatch dispatch = {});

    RoutingBindingReport rebuildBindings();
    InputRouteRecord processInput(const RawInputEvent& event,
                                  bool requestPhysicalSuppression = false);
    void processDeviceChange(const RawInputDeviceChange& change);

    const InputObservationLedger& ledger() const noexcept { return m_ledger; }
    InputObservationLedger& ledger() noexcept { return m_ledger; }

    std::optional<SeatRouteSnapshot> seat(SeatId seatId) const;
    std::vector<SeatRouteSnapshot> seats() const;

    std::uint64_t unassignedEvents() const noexcept { return m_unassignedEvents; }
    std::uint64_t ambiguousEvents() const noexcept { return m_ambiguousEvents; }
    std::uint64_t inactiveSeatEvents() const noexcept { return m_inactiveSeatEvents; }
    std::uint64_t missingTargetEvents() const noexcept { return m_missingTargetEvents; }

    bool isAmbiguousDevice(std::wstring_view deviceId) const;

private:
    static std::wstring normalize(std::wstring_view value);
    void bindExclusiveDevices(const SeatConfig& seat,
                              SeatDeviceType type,
                              const std::vector<std::wstring>& deviceIds,
                              RoutingBindingReport& report);
    void updateSeatMetrics(const RawInputEvent& event,
                           const InputRouteRecord& route);

    WorkspaceManager& m_seats;
    SeatRoutingPolicy& m_routingPolicy;
    InputRouteDispatch m_dispatch;
    InputObservationLedger m_ledger;

    std::unordered_set<std::wstring> m_ambiguousDeviceIds;
    std::unordered_map<SeatId, SeatRouteSnapshot> m_seatMetrics;

    std::uint64_t m_unassignedEvents{0};
    std::uint64_t m_ambiguousEvents{0};
    std::uint64_t m_inactiveSeatEvents{0};
    std::uint64_t m_missingTargetEvents{0};
};

enum class InputTracePrivacyMode {
    Redacted,
    DiagnosticKeyIds
};

class InputTraceWriter {
public:
    InputTraceWriter() = default;
    explicit InputTraceWriter(
        const std::string& filePath,
        InputTracePrivacyMode privacyMode = InputTracePrivacyMode::Redacted)
        : m_privacyMode(privacyMode) {
        open(filePath);
    }

    bool open(const std::string& filePath);
    bool isOpen() const noexcept { return m_stream.is_open(); }
    const std::string& lastError() const noexcept { return m_lastError; }
    void setPrivacyMode(InputTracePrivacyMode privacyMode) noexcept {
        m_privacyMode = privacyMode;
    }
    InputTracePrivacyMode privacyMode() const noexcept { return m_privacyMode; }

    bool writeInput(const RawInputEvent& event,
                    const InputRouteRecord& route);
    bool writeDeviceChange(const RawInputDeviceChange& change);
    void flush();

private:
    bool writeLine(const std::string& line);

    std::ofstream m_stream;
    std::string m_lastError;
    InputTracePrivacyMode m_privacyMode{InputTracePrivacyMode::Redacted};
};

std::string_view inputRouteDispositionName(InputRouteDisposition disposition) noexcept;
std::string_view rawInputDeviceChangeKindName(RawInputDeviceChangeKind kind) noexcept;
std::string_view rawKeyTransitionName(RawKeyTransition transition) noexcept;

} // namespace hydra
