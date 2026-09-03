#pragma once

#include "hydra/gate_c_transport.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace hydra::production::detail {

inline constexpr std::uint32_t kProductionBridgeMappingMagic = 0x31424750u;
inline constexpr std::uint32_t kProductionBridgeMappingVersion = 1u;
inline constexpr std::size_t kProductionBridgePipeNameChars = 256u;

enum class ProductionBridgeDllState : LONG {
    Configured = 0,
    Starting = 1,
    Active = 2,
    Stopping = 3,
    Stopped = 4,
    Failed = 5,
};

#pragma pack(push, 1)
struct ProductionBridgeMappingV1 {
    std::uint32_t structSize{sizeof(ProductionBridgeMappingV1)};
    std::uint32_t magic{kProductionBridgeMappingMagic};
    std::uint32_t version{kProductionBridgeMappingVersion};
    std::uint32_t seatId{0};
    std::uint32_t requiredApiMask{0};
    std::uint32_t reserved0{0};
    gatec::SessionToken token{};
    wchar_t pipeName[kProductionBridgePipeNameChars]{};
    LONG lifecycle{static_cast<LONG>(ProductionBridgeDllState::Configured)};
    LONG lastResult{0};
    LONG rollbackComplete{0};
    LONG reserved1{0};
};
#pragma pack(pop)

inline std::wstring productionBridgeMappingName(std::uint32_t processId) {
    return L"Local\\HydraSeat.ProductionGateC." + std::to_wstring(processId);
}

inline void publishBridgeState(ProductionBridgeMappingV1* mapping,
                               ProductionBridgeDllState state,
                               LONG result,
                               bool rollbackComplete) noexcept {
    if (mapping == nullptr) return;
    InterlockedExchange(&mapping->lastResult, result);
    InterlockedExchange(&mapping->rollbackComplete, rollbackComplete ? 1 : 0);
    InterlockedExchange(&mapping->lifecycle, static_cast<LONG>(state));
    MemoryBarrier();
}

} // namespace hydra::production::detail
#endif
