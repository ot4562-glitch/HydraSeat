#include "hydra/window_placement.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hydra::windowing {
namespace {

#ifdef _WIN32

std::string win32Error(const char* operation, DWORD code = GetLastError()) {
    return std::string(operation) + " failed (Win32=" + std::to_string(code) + ")";
}

display::DisplayRect queryOuterRect(HWND window) noexcept {
    RECT rect{};
    if (window == nullptr || GetWindowRect(window, &rect) == FALSE) return {};
    return {rect.left, rect.top, rect.right, rect.bottom};
}

bool rectNear(const display::DisplayRect& left, const display::DisplayRect& right,
              std::uint32_t tolerance) noexcept {
    const auto within = [tolerance](std::int32_t a, std::int32_t b) {
        const auto difference = static_cast<std::int64_t>(a) - static_cast<std::int64_t>(b);
        return std::llabs(difference) <= static_cast<std::int64_t>(tolerance);
    };
    return within(left.left, right.left) && within(left.top, right.top) &&
           within(left.right, right.right) && within(left.bottom, right.bottom);
}

bool queryWindowLong(HWND window, int index, std::int64_t& value,
                     std::string* error) noexcept {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR raw = GetWindowLongPtrW(window, index);
    const DWORD code = GetLastError();
    if (raw == 0 && code != ERROR_SUCCESS) {
        if (error) *error = win32Error("GetWindowLongPtrW", code);
        return false;
    }
    value = static_cast<std::int64_t>(raw);
    return true;
}

WindowRestoreState captureRestoreState(const WindowTracker& tracker,
                                       const WindowIdentity& identity,
                                       std::string* error) noexcept {
    WindowRestoreState state;
    state.identity = identity;
    if (!tracker.validateIdentity(identity)) {
        if (error) *error = "window identity became stale before restore-state capture";
        return state;
    }
    const auto window = reinterpret_cast<HWND>(identity.nativeHandle);
    state.outerRect = queryOuterRect(window);
    if (state.outerRect.width() <= 0 || state.outerRect.height() <= 0) {
        if (error) *error = "GetWindowRect returned an empty restore rectangle";
        return state;
    }
    if (!queryWindowLong(window, GWL_STYLE, state.style, error) ||
        !queryWindowLong(window, GWL_EXSTYLE, state.extendedStyle, error)) {
        return state;
    }
    state.wasVisible = IsWindowVisible(window) != FALSE;
    state.wasIconic = IsIconic(window) != FALSE;
    state.wasZoomed = IsZoomed(window) != FALSE;
    state.valid = true;
    return state;
}

bool setWindowLongChecked(HWND window, int index, std::int64_t value,
                          std::string* error) noexcept {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(window, index, static_cast<LONG_PTR>(value));
    const DWORD code = GetLastError();
    if (previous == 0 && code != ERROR_SUCCESS) {
        if (error) *error = win32Error("SetWindowLongPtrW", code);
        return false;
    }
    return true;
}

bool applyBorderlessStyle(HWND window, std::string* error) noexcept {
    std::int64_t styleValue = 0;
    if (!queryWindowLong(window, GWL_STYLE, styleValue, error)) return false;
    auto style = static_cast<LONG_PTR>(styleValue);
    style &= ~(static_cast<LONG_PTR>(WS_CAPTION) |
               static_cast<LONG_PTR>(WS_THICKFRAME) |
               static_cast<LONG_PTR>(WS_MINIMIZEBOX) |
               static_cast<LONG_PTR>(WS_MAXIMIZEBOX) |
               static_cast<LONG_PTR>(WS_SYSMENU));
    style |= static_cast<LONG_PTR>(WS_POPUP);
    return setWindowLongChecked(window, GWL_STYLE, static_cast<std::int64_t>(style), error);
}

bool setOuterRect(HWND window, const display::DisplayRect& rect,
                  bool frameChanged, std::string* error) noexcept {
    const std::int32_t width = rect.width();
    const std::int32_t height = rect.height();
    if (width <= 0 || height <= 0) {
        if (error) *error = "requested window placement rectangle has no area";
        return false;
    }
    UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER;
    if (frameChanged) flags |= SWP_FRAMECHANGED;
    if (SetWindowPos(window, nullptr, rect.left, rect.top, width, height, flags) == FALSE) {
        if (error) *error = win32Error("SetWindowPos");
        return false;
    }
    return true;
}

#endif

} // namespace

WindowPlacementResult WindowPlacementEngine::apply(
    const TrackedWindow& window,
    const display::SeatDisplayGroup& displayGroup,
    const WindowPlacementPolicy& policy) const {
    WindowPlacementResult result;
    result.plan = computeWindowPlacementPlan(window, displayGroup, policy);
    result.diagnostics = result.plan.diagnostics;
    if (!result.plan.valid) {
        result.status = result.plan.degraded
            ? WindowPlacementStatus::Degraded : WindowPlacementStatus::Rejected;
        return result;
    }
    if (result.plan.leaveNative || !result.plan.actionable) {
        result.status = result.plan.degraded
            ? WindowPlacementStatus::Degraded : WindowPlacementStatus::NoChange;
        return result;
    }
    result.requestedRect = result.plan.desiredRect;

#ifdef _WIN32
    if (!tracker_.validateIdentity(window.identity)) {
        result.status = WindowPlacementStatus::Rejected;
        result.diagnostics.push_back("window ownership/identity validation failed before placement");
        return result;
    }
    if (policy.initialDelayMs != 0u) {
        std::this_thread::sleep_for(std::chrono::milliseconds(policy.initialDelayMs));
        if (!tracker_.validateIdentity(window.identity)) {
            result.status = WindowPlacementStatus::Rejected;
            result.diagnostics.push_back("window identity became stale during placement delay");
            return result;
        }
    }

    std::string error;
    result.restoreState = captureRestoreState(tracker_, window.identity, &error);
    if (!result.restoreState.valid) {
        result.status = WindowPlacementStatus::Rejected;
        result.diagnostics.push_back(error.empty() ? "unable to capture rollback state" : error);
        return result;
    }

    const auto hwnd = reinterpret_cast<HWND>(window.identity.nativeHandle);
    if (result.plan.borderless) {
        if (!tracker_.validateIdentity(window.identity) ||
            !applyBorderlessStyle(hwnd, &error)) {
            result.status = WindowPlacementStatus::Rejected;
            result.diagnostics.push_back(error.empty()
                ? "window identity became stale before borderless style change" : error);
            return result;
        }
    }

    const std::uint32_t maxAttempts = policy.retryCount + 1u;
    for (std::uint32_t attempt = 1; attempt <= maxAttempts; ++attempt) {
        result.attempts = attempt;
        if (!tracker_.validateIdentity(window.identity)) {
            result.status = WindowPlacementStatus::Rejected;
            result.diagnostics.push_back("window identity became stale during placement retry loop");
            return result;
        }
        if (!setOuterRect(hwnd, result.requestedRect,
                          result.plan.borderless && attempt == 1u, &error)) {
            result.status = WindowPlacementStatus::Rejected;
            result.diagnostics.push_back(error);
            return result;
        }

        // Give the target a bounded chance to process WM_WINDOWPOSCHANGED and
        // reveal whether it is actively fighting/reversing HydraSeat placement.
        const std::uint32_t settleDelay = std::max<std::uint32_t>(10u, policy.retryDelayMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(settleDelay));
        if (!tracker_.validateIdentity(window.identity)) {
            result.status = WindowPlacementStatus::Rejected;
            result.diagnostics.push_back("window was recreated or destroyed after placement request");
            return result;
        }
        result.observedRect = queryOuterRect(hwnd);
        if (rectNear(result.requestedRect, result.observedRect,
                     policy.placementTolerancePixels)) {
            result.status = WindowPlacementStatus::Applied;
            return result;
        }
        if (attempt < maxAttempts && policy.retryDelayMs != 0u) {
            std::this_thread::sleep_for(std::chrono::milliseconds(policy.retryDelayMs));
        }
    }

    result.status = WindowPlacementStatus::Degraded;
    result.fightingApplication = true;
    result.diagnostics.push_back(
        "target did not converge to requested placement within bounded retries");
    return result;
#else
    result.status = WindowPlacementStatus::Rejected;
    result.diagnostics.push_back("window placement is Windows-only");
    return result;
#endif
}

bool WindowPlacementEngine::rollback(const WindowRestoreState& state,
                                     std::string* error) const noexcept {
    if (!state.valid) {
        if (error) *error = "window rollback state is invalid";
        return false;
    }
#ifdef _WIN32
    if (!tracker_.validateIdentity(state.identity)) {
        if (error) *error = "window rollback refused because identity is stale or unowned";
        return false;
    }
    const auto hwnd = reinterpret_cast<HWND>(state.identity.nativeHandle);
    std::string localError;
    if (!setWindowLongChecked(hwnd, GWL_STYLE, state.style, &localError) ||
        !setWindowLongChecked(hwnd, GWL_EXSTYLE, state.extendedStyle, &localError) ||
        !setOuterRect(hwnd, state.outerRect, true, &localError)) {
        if (error) *error = localError;
        return false;
    }

    int showCommand = SW_HIDE;
    if (state.wasIconic) {
        showCommand = SW_SHOWMINNOACTIVE;
    } else if (state.wasZoomed) {
        showCommand = SW_SHOWMAXIMIZED;
    } else if (state.wasVisible) {
        showCommand = SW_SHOWNOACTIVATE;
    }
    (void)ShowWindowAsync(hwnd, showCommand);
    return tracker_.validateIdentity(state.identity);
#else
    if (error) *error = "window rollback is Windows-only";
    return false;
#endif
}

} // namespace hydra::windowing
