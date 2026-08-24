#include "hydra/input_observation.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace hydra {
namespace {

constexpr std::uint16_t kLeftDown = 0x0001;
constexpr std::uint16_t kLeftUp = 0x0002;
constexpr std::uint16_t kRightDown = 0x0004;
constexpr std::uint16_t kRightUp = 0x0008;
constexpr std::uint16_t kMiddleDown = 0x0010;
constexpr std::uint16_t kMiddleUp = 0x0020;
constexpr std::uint16_t kButton4Down = 0x0040;
constexpr std::uint16_t kButton4Up = 0x0080;
constexpr std::uint16_t kButton5Down = 0x0100;
constexpr std::uint16_t kButton5Up = 0x0200;

constexpr std::array<std::pair<std::uint16_t, std::uint32_t>, 5>
    kMouseDownTransitions{{
        {kLeftDown, 1u << 0},
        {kRightDown, 1u << 1},
        {kMiddleDown, 1u << 2},
        {kButton4Down, 1u << 3},
        {kButton5Down, 1u << 4},
    }};

constexpr std::array<std::pair<std::uint16_t, std::uint32_t>, 5>
    kMouseUpTransitions{{
        {kLeftUp, 1u << 0},
        {kRightUp, 1u << 1},
        {kMiddleUp, 1u << 2},
        {kButton4Up, 1u << 3},
        {kButton5Up, 1u << 4},
    }};

std::wstring fallbackKey(std::uintptr_t handle) {
    std::wostringstream out;
    out << L"SessionHandle:0x" << std::hex << std::uppercase << handle;
    return out.str();
}

void appendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7f) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    } else if (codePoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    }
}

std::string wideToUtf8(std::wstring_view value) {
    std::string result;
    for (std::size_t index = 0; index < value.size(); ++index) {
        std::uint32_t codePoint = static_cast<std::uint32_t>(value[index]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (codePoint >= 0xd800 && codePoint <= 0xdbff) {
                if (++index >= value.size()) {
                    codePoint = 0xfffd;
                } else {
                    const auto low = static_cast<std::uint32_t>(value[index]);
                    if (low >= 0xdc00 && low <= 0xdfff) {
                        codePoint = 0x10000 + ((codePoint - 0xd800) << 10) +
                                    (low - 0xdc00);
                    } else {
                        codePoint = 0xfffd;
                        --index;
                    }
                }
            } else if (codePoint >= 0xdc00 && codePoint <= 0xdfff) {
                codePoint = 0xfffd;
            }
        }
        appendUtf8(result, codePoint);
    }
    return result;
}

std::string jsonQuote(std::wstring_view value) {
    const auto utf8 = wideToUtf8(value);
    std::ostringstream output;
    output << '"';
    static constexpr char hex[] = "0123456789abcdef";
    for (const char raw : utf8) {
        const auto character = static_cast<unsigned char>(raw);
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20) {
                output << "\\u00" << hex[character >> 4]
                       << hex[character & 0x0f];
            } else {
                output << static_cast<char>(character);
            }
        }
    }
    output << '"';
    return output.str();
}

bool looksLikeKeyboardEvent(const RawInputEvent& event) noexcept {
    return event.keyTransition != RawKeyTransition::None || event.vkey != 0;
}

} // namespace

std::wstring InputObservationLedger::normalize(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t character) {
                       return static_cast<wchar_t>(
                           std::towlower(static_cast<wint_t>(character)));
                   });
    return result;
}

std::wstring InputObservationLedger::eventDeviceKey(
    const RawInputEvent& event) {
    if (!event.deviceId.empty()) {
        return normalize(event.deviceId);
    }
    if (!event.devicePath.empty()) {
        return normalize(event.devicePath);
    }
    return normalize(fallbackKey(event.deviceHandle));
}

std::wstring InputObservationLedger::changeDeviceKey(
    const RawInputDeviceChange& change) {
    if (!change.device.deviceId.empty()) {
        return normalize(change.device.deviceId);
    }
    if (!change.device.devicePath.empty()) {
        return normalize(change.device.devicePath);
    }
    return normalize(fallbackKey(change.device.deviceHandle));
}

InputObservationLedger::MutableDeviceState&
InputObservationLedger::stateFor(std::wstring key) {
    return m_devices[std::move(key)];
}

void InputObservationLedger::applyMouseButtonTransitions(
    DeviceObservationSnapshot& snapshot, std::uint16_t flags) {
    for (const auto& [transition, button] : kMouseDownTransitions) {
        if ((flags & transition) != 0) {
            snapshot.mouseButtonsDown |= button;
        }
    }
    for (const auto& [transition, button] : kMouseUpTransitions) {
        if ((flags & transition) != 0) {
            snapshot.mouseButtonsDown &= ~button;
        }
    }
}

void InputObservationLedger::observeInput(const RawInputEvent& event) {
    auto& state = stateFor(eventDeviceKey(event));
    auto& snapshot = state.snapshot;

    if (!event.deviceId.empty()) {
        snapshot.deviceId = event.deviceId;
    } else if (snapshot.deviceId.empty()) {
        snapshot.deviceId = !event.devicePath.empty()
                                ? event.devicePath
                                : fallbackKey(event.deviceHandle);
    }
    if (!event.devicePath.empty()) {
        snapshot.devicePath = event.devicePath;
    }
    snapshot.rawDevType = event.rawDevType;
    snapshot.isTouchpad = event.isTouchpad;
    state.onlineHandles.insert(event.deviceHandle);
    snapshot.online = true;
    ++snapshot.eventCount;
    snapshot.lastSequence = event.sequence;
    snapshot.lastTimestampMicros = event.monotonicTimestampMicros;

    if (looksLikeKeyboardEvent(event)) {
        ++snapshot.keyboardEventCount;
        snapshot.lastVkey = event.vkey;
        snapshot.lastKeyTransition = event.keyTransition;
        if (event.keyTransition == RawKeyTransition::Down) {
            state.pressedKeys.insert(event.vkey);
        } else if (event.keyTransition == RawKeyTransition::Up) {
            state.pressedKeys.erase(event.vkey);
        }
    } else {
        ++snapshot.mouseEventCount;
        snapshot.totalDeltaX += event.deltaX;
        snapshot.totalDeltaY += event.deltaY;
        snapshot.totalWheelDelta += event.wheelDelta;
        applyMouseButtonTransitions(snapshot, event.mouseButtonFlags);
    }

    ++m_totalInputEvents;
}

void InputObservationLedger::observeDeviceChange(
    const RawInputDeviceChange& change) {
    auto& state = stateFor(changeDeviceKey(change));
    auto& snapshot = state.snapshot;

    if (!change.device.deviceId.empty()) {
        snapshot.deviceId = change.device.deviceId;
    } else if (snapshot.deviceId.empty()) {
        snapshot.deviceId = !change.device.devicePath.empty()
                                ? change.device.devicePath
                                : fallbackKey(change.device.deviceHandle);
    }
    if (!change.device.devicePath.empty()) {
        snapshot.devicePath = change.device.devicePath;
    }
    snapshot.rawDevType = change.device.rawDevType;
    snapshot.isTouchpad = change.device.isTouchpad;
    snapshot.lastSequence = change.sequence;
    snapshot.lastTimestampMicros = change.monotonicTimestampMicros;

    if (change.kind == RawInputDeviceChangeKind::Arrival) {
        state.onlineHandles.insert(change.device.deviceHandle);
        ++snapshot.arrivals;
    } else {
        state.onlineHandles.erase(change.device.deviceHandle);
        ++snapshot.removals;
        if (state.onlineHandles.empty()) {
            state.pressedKeys.clear();
            snapshot.mouseButtonsDown = 0;
        }
    }
    snapshot.online = !state.onlineHandles.empty();
    ++m_totalDeviceChanges;
}

DeviceObservationSnapshot InputObservationLedger::snapshotOf(
    const MutableDeviceState& state) {
    auto result = state.snapshot;
    result.pressedKeys.assign(state.pressedKeys.begin(),
                              state.pressedKeys.end());
    std::sort(result.pressedKeys.begin(), result.pressedKeys.end());
    return result;
}

std::optional<DeviceObservationSnapshot> InputObservationLedger::device(
    std::wstring_view deviceId) const {
    const auto found = m_devices.find(normalize(deviceId));
    if (found == m_devices.end()) {
        return std::nullopt;
    }
    return snapshotOf(found->second);
}

std::vector<DeviceObservationSnapshot> InputObservationLedger::devices() const {
    std::vector<DeviceObservationSnapshot> result;
    result.reserve(m_devices.size());
    for (const auto& [key, state] : m_devices) {
        (void)key;
        result.push_back(snapshotOf(state));
    }
    std::sort(result.begin(), result.end(),
              [](const DeviceObservationSnapshot& left,
                 const DeviceObservationSnapshot& right) {
                  return left.deviceId < right.deviceId;
              });
    return result;
}

InputObservationSession::InputObservationSession(
    WorkspaceManager& seats, SeatRoutingPolicy& routingPolicy,
    InputRouteDispatch dispatch)
    : m_seats(seats),
      m_routingPolicy(routingPolicy),
      m_dispatch(std::move(dispatch)) {}

std::wstring InputObservationSession::normalize(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t character) {
                       return static_cast<wchar_t>(
                           std::towlower(static_cast<wint_t>(character)));
                   });
    return result;
}

void InputObservationSession::bindExclusiveDevices(
    const SeatConfig& seat, SeatDeviceType type,
    const std::vector<std::wstring>& deviceIds,
    RoutingBindingReport& report) {
    for (const auto& deviceId : deviceIds) {
        const auto owners = m_seats.findDeviceOwners(type, deviceId);
        if (owners.size() != 1 || owners.front() != seat.seatId) {
            m_ambiguousDeviceIds.insert(normalize(deviceId));
            continue;
        }
        if (m_routingPolicy.bindDevice(deviceId, seat.seatId)) {
            ++report.boundDevices;
        }
    }
}

RoutingBindingReport InputObservationSession::rebuildBindings() {
    m_routingPolicy.clear();
    m_ambiguousDeviceIds.clear();

    RoutingBindingReport report;
    std::unordered_set<SeatId> currentSeats;
    for (const auto& seat : m_seats.getAllSeats()) {
        currentSeats.insert(seat.seatId);
        auto& metrics = m_seatMetrics[seat.seatId];
        metrics.seatId = seat.seatId;
        metrics.targetHwnd = seat.targetHwnd;

        bindExclusiveDevices(seat, SeatDeviceType::Keyboard,
                             seat.keyboardIds, report);
        bindExclusiveDevices(seat, SeatDeviceType::Mouse,
                             seat.mouseIds, report);
    }

    for (auto it = m_seatMetrics.begin(); it != m_seatMetrics.end();) {
        if (!currentSeats.contains(it->first)) {
            it = m_seatMetrics.erase(it);
        } else {
            ++it;
        }
    }

    report.ambiguousSharedDevices.assign(m_ambiguousDeviceIds.begin(),
                                         m_ambiguousDeviceIds.end());
    std::sort(report.ambiguousSharedDevices.begin(),
              report.ambiguousSharedDevices.end());
    return report;
}

InputRouteRecord InputObservationSession::processInput(
    const RawInputEvent& event, bool requestPhysicalSuppression) {
    m_ledger.observeInput(event);

    InputRouteRecord result;
    result.sequence = event.sequence;
    result.deviceId = event.deviceId;
    result.physicalSuppressionRequested = requestPhysicalSuppression;

    if (isAmbiguousDevice(event.deviceId)) {
        result.disposition = InputRouteDisposition::AmbiguousSharedDevice;
        ++m_ambiguousEvents;
        return result;
    }

    const auto owner = m_routingPolicy.ownerOf(event.deviceId);
    if (!owner) {
        result.disposition = InputRouteDisposition::UnassignedDevice;
        ++m_unassignedEvents;
        return result;
    }

    result.seatId = *owner;
    const auto* seat = m_seats.getSeat(*owner);
    if (seat == nullptr || !seat->active) {
        result.disposition = InputRouteDisposition::InactiveSeat;
        ++m_inactiveSeatEvents;
        updateSeatMetrics(event, result);
        return result;
    }

    result.targetHwnd = seat->targetHwnd;
    if (seat->targetHwnd == 0) {
        result.disposition = InputRouteDisposition::MissingTargetWindow;
        ++m_missingTargetEvents;
        updateSeatMetrics(event, result);
        return result;
    }

    InputRouteDecision decision;
    decision.seatId = seat->seatId;
    decision.targetHwnd = seat->targetHwnd;
    decision.consumePhysicalInput = requestPhysicalSuppression;

    bool dispatched = false;
    if (m_dispatch) {
        try {
            dispatched = m_dispatch(event, decision);
        } catch (...) {
            dispatched = false;
        }
    }

    result.disposition = dispatched ? InputRouteDisposition::Routed
                                    : InputRouteDisposition::DispatchFailed;
    updateSeatMetrics(event, result);
    return result;
}

void InputObservationSession::processDeviceChange(
    const RawInputDeviceChange& change) {
    m_ledger.observeDeviceChange(change);
}

void InputObservationSession::updateSeatMetrics(
    const RawInputEvent& event, const InputRouteRecord& route) {
    if (!route.seatId) {
        return;
    }

    auto& metrics = m_seatMetrics[*route.seatId];
    metrics.seatId = *route.seatId;
    metrics.targetHwnd = route.targetHwnd;
    metrics.lastSequence = route.sequence;
    metrics.lastDeviceId = route.deviceId;

    if (route.disposition == InputRouteDisposition::Routed) {
        ++metrics.routedEvents;
        if (looksLikeKeyboardEvent(event)) {
            ++metrics.keyboardEvents;
        } else {
            ++metrics.mouseEvents;
        }
    } else if (route.disposition == InputRouteDisposition::DispatchFailed) {
        ++metrics.dispatchFailures;
    }
}

std::optional<SeatRouteSnapshot> InputObservationSession::seat(
    SeatId seatId) const {
    const auto found = m_seatMetrics.find(seatId);
    if (found == m_seatMetrics.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::vector<SeatRouteSnapshot> InputObservationSession::seats() const {
    std::vector<SeatRouteSnapshot> result;
    result.reserve(m_seatMetrics.size());
    for (const auto& [seatId, snapshot] : m_seatMetrics) {
        (void)seatId;
        result.push_back(snapshot);
    }
    std::sort(result.begin(), result.end(),
              [](const SeatRouteSnapshot& left,
                 const SeatRouteSnapshot& right) {
                  return left.seatId < right.seatId;
              });
    return result;
}

bool InputObservationSession::isAmbiguousDevice(
    std::wstring_view deviceId) const {
    return m_ambiguousDeviceIds.contains(normalize(deviceId));
}

bool InputTraceWriter::open(const std::string& filePath) {
    m_stream.close();
    m_stream.clear();
    m_lastError.clear();
    m_stream.open(filePath, std::ios::binary | std::ios::out |
                                std::ios::trunc);
    if (!m_stream) {
        m_lastError = "could not open trace file";
        return false;
    }
    return true;
}

bool InputTraceWriter::writeLine(const std::string& line) {
    if (!m_stream.is_open()) {
        m_lastError = "trace file is not open";
        return false;
    }
    m_stream << line << '\n';
    if (!m_stream) {
        m_lastError = "failed while writing trace file";
        return false;
    }
    return true;
}

bool InputTraceWriter::writeInput(const RawInputEvent& event,
                                  const InputRouteRecord& route) {
    std::ostringstream output;
    output << '{'
           << "\"record\":\"input\","
           << "\"sequence\":" << event.sequence << ','
           << "\"timestamp_us\":" << event.monotonicTimestampMicros << ','
           << "\"device_id\":" << jsonQuote(event.deviceId) << ','
           << "\"device_path\":" << jsonQuote(event.devicePath) << ','
           << "\"raw_type\":" << event.rawDevType << ','
           << "\"vkey\":" << event.vkey << ','
           << "\"key_transition\":\""
           << rawKeyTransitionName(event.keyTransition) << "\","
           << "\"delta_x\":" << event.deltaX << ','
           << "\"delta_y\":" << event.deltaY << ','
           << "\"wheel_delta\":" << event.wheelDelta << ','
           << "\"touchpad\":" << (event.isTouchpad ? "true" : "false") << ','
           << "\"route\":\""
           << inputRouteDispositionName(route.disposition) << "\","
           << "\"seat_id\":";
    if (route.seatId) {
        output << *route.seatId;
    } else {
        output << "null";
    }
    output << ','
           << "\"target_hwnd\":" << route.targetHwnd << ','
           << "\"physical_suppression_requested\":"
           << (route.physicalSuppressionRequested ? "true" : "false") << ','
           << "\"isolation_guarantee\":\"diagnostic_route_only_native_os_input_not_suppressed\""
           << '}';
    return writeLine(output.str());
}

bool InputTraceWriter::writeDeviceChange(
    const RawInputDeviceChange& change) {
    std::ostringstream output;
    output << '{'
           << "\"record\":\"device_change\","
           << "\"sequence\":" << change.sequence << ','
           << "\"timestamp_us\":" << change.monotonicTimestampMicros << ','
           << "\"change\":\""
           << rawInputDeviceChangeKindName(change.kind) << "\","
           << "\"device_id\":" << jsonQuote(change.device.deviceId) << ','
           << "\"device_path\":" << jsonQuote(change.device.devicePath) << ','
           << "\"raw_type\":" << change.device.rawDevType << ','
           << "\"touchpad\":"
           << (change.device.isTouchpad ? "true" : "false") << ','
           << "\"online\":" << (change.device.online ? "true" : "false")
           << '}';
    return writeLine(output.str());
}

void InputTraceWriter::flush() {
    if (m_stream.is_open()) {
        m_stream.flush();
    }
}

std::string_view inputRouteDispositionName(
    InputRouteDisposition disposition) noexcept {
    switch (disposition) {
    case InputRouteDisposition::Routed: return "Routed";
    case InputRouteDisposition::UnassignedDevice: return "UnassignedDevice";
    case InputRouteDisposition::AmbiguousSharedDevice: return "AmbiguousSharedDevice";
    case InputRouteDisposition::InactiveSeat: return "InactiveSeat";
    case InputRouteDisposition::MissingTargetWindow: return "MissingTargetWindow";
    case InputRouteDisposition::DispatchFailed: return "DispatchFailed";
    }
    return "Unknown";
}

std::string_view rawInputDeviceChangeKindName(
    RawInputDeviceChangeKind kind) noexcept {
    switch (kind) {
    case RawInputDeviceChangeKind::Arrival: return "Arrival";
    case RawInputDeviceChangeKind::Removal: return "Removal";
    }
    return "Unknown";
}

std::string_view rawKeyTransitionName(RawKeyTransition transition) noexcept {
    switch (transition) {
    case RawKeyTransition::None: return "None";
    case RawKeyTransition::Down: return "Down";
    case RawKeyTransition::Up: return "Up";
    }
    return "Unknown";
}

} // namespace hydra
