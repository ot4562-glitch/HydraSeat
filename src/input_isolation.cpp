#include "hydra/input_isolation.hpp"

#include <algorithm>
#include <cwctype>

namespace hydra {

std::wstring SeatRoutingPolicy::normalize(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return result;
}

bool SeatRoutingPolicy::bindDevice(std::wstring deviceId, SeatId seatId) {
    if (deviceId.empty() || seatId == 0) return false;
    m_deviceOwners[normalize(deviceId)] = seatId;
    return true;
}

bool SeatRoutingPolicy::unbindDevice(std::wstring_view deviceId) {
    if (deviceId.empty()) return false;
    return m_deviceOwners.erase(normalize(deviceId)) != 0;
}

void SeatRoutingPolicy::clearSeat(SeatId seatId) {
    for (auto it = m_deviceOwners.begin(); it != m_deviceOwners.end();) {
        if (it->second == seatId) it = m_deviceOwners.erase(it);
        else ++it;
    }
}

std::optional<SeatId> SeatRoutingPolicy::ownerOf(std::wstring_view deviceId) const {
    if (deviceId.empty()) return std::nullopt;
    const auto it = m_deviceOwners.find(normalize(deviceId));
    if (it == m_deviceOwners.end()) return std::nullopt;
    return it->second;
}

InputRouteDecision SeatRoutingPolicy::route(std::wstring_view deviceId,
                                            const WorkspaceManager& seats,
                                            bool isolationRequested) const {
    InputRouteDecision decision;
    const auto owner = ownerOf(deviceId);
    if (!owner) return decision;

    const auto* seat = seats.getSeat(*owner);
    if (seat == nullptr || !seat->active) return decision;

    decision.seatId = seat->seatId;
    decision.targetHwnd = seat->targetHwnd;
    decision.consumePhysicalInput = isolationRequested;
    return decision;
}

} // namespace hydra
