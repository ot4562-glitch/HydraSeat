#pragma once

#include "hydra/process_group.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::windowing {

enum class WindowRole : std::uint8_t {
    PrimaryGame = 0,
    Launcher = 1,
    Dialog = 2,
    Overlay = 3,
    ChildOwnedPopup = 4,
    Ignored = 5,
};

struct WindowIdentity {
    std::uintptr_t nativeHandle{0};
    process::ProcessIdentity process;
    std::uint32_t threadId{0};
    std::uint64_t trackerGeneration{0};

    bool valid() const noexcept {
        return nativeHandle != 0 && threadId != 0 && trackerGeneration != 0 &&
               process.valid();
    }

    bool sameInstance(const WindowIdentity& other) const noexcept {
        return nativeHandle == other.nativeHandle &&
               threadId == other.threadId &&
               trackerGeneration == other.trackerGeneration &&
               process.sameInstance(other.process);
    }

    friend bool operator==(const WindowIdentity&, const WindowIdentity&) = default;
};

struct WindowRule {
    WindowRole role{WindowRole::PrimaryGame};
    std::wstring titleContains;
    std::wstring classNameEquals;
    std::wstring executablePathContains;
    bool rootProcessOnly{false};
};

struct WindowProfileRules {
    WindowRole defaultRole{WindowRole::PrimaryGame};
    std::vector<WindowRule> overrides;
};

std::string_view windowRoleName(WindowRole role) noexcept;

} // namespace hydra::windowing
