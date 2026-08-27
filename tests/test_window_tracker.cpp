#include "hydra/process_launcher.hpp"
#include "hydra/window_tracker.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace hydra::process;
using namespace hydra::windowing;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

#ifdef _WIN32

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                             static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path fixtureExecutable() {
    return executableDirectory() / L"hydra_window_test_app.exe";
}

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

std::optional<TrackedWindow> findTitle(const WindowTrackerSnapshot& snapshot,
                                       std::wstring_view title) {
    const auto found = std::find_if(snapshot.windows.begin(), snapshot.windows.end(),
                                    [title](const TrackedWindow& window) {
                                        return window.title == title;
                                    });
    if (found == snapshot.windows.end()) return std::nullopt;
    return *found;
}

struct FindWindowContext {
    std::uint32_t processId{0};
    std::wstring title;
    HWND result{nullptr};
};

BOOL CALLBACK findFixtureWindowCallback(HWND window, LPARAM parameter) {
    auto* context = reinterpret_cast<FindWindowContext*>(parameter);
    DWORD processId = 0;
    (void)GetWindowThreadProcessId(window, &processId);
    if (processId != context->processId) return TRUE;
    std::wstring title(256u, L'\0');
    const int copied = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
    if (copied <= 0) return TRUE;
    title.resize(static_cast<std::size_t>(copied));
    if (title == context->title) {
        context->result = window;
        return FALSE;
    }
    return TRUE;
}

HWND findFixtureWindow(std::uint32_t processId, std::wstring title) {
    FindWindowContext context{processId, std::move(title), nullptr};
    (void)EnumWindows(findFixtureWindowCallback, reinterpret_cast<LPARAM>(&context));
    return context.result;
}

ProcessLaunchSpec fixtureSpec(hydra::SeatId seatId, std::wstring mode) {
    ProcessLaunchSpec spec;
    spec.seatId = seatId;
    spec.executablePath = fixtureExecutable().wstring();
    spec.workingDirectory = executableDirectory().wstring();
    spec.architecture = sizeof(void*) == 8u
        ? ProcessArchitecture::X64 : ProcessArchitecture::X86;
    spec.arguments = {L"--mode", std::move(mode)};
    return spec;
}

void testQueueOverflowIsVisible() {
    WindowTrackerOptions options;
    options.callbackQueueCapacity = 1;
    options.eventHistoryCapacity = 4;
    WindowTracker tracker(options);
    check(tracker.notifyWindowChange(101u, WindowChangeHint::Created),
          "first bounded callback event is accepted");
    check(!tracker.notifyWindowChange(202u, WindowChangeHint::Shown),
          "callback queue overflow rejects excess work instead of growing unbounded");
    check(tracker.snapshot().droppedCallbackEvents == 1u,
          "callback queue overflow is visible in tracker snapshot");
}

void testProfileDiagnostics() {
    WindowTracker tracker;
    WindowProfileRules invalid;
    invalid.overrides.push_back(WindowRule{});
    std::string error;
    check(!tracker.setProfileRules(invalid, &error) && !error.empty(),
          "empty typed window override is rejected with diagnostic");
}

void testRealWindowTracking() {
    std::string error;
    auto controlledSpec = fixtureSpec(21, L"launcher");
    controlledSpec.arguments.push_back(L"--spawn-child");
    auto controlled = ProcessLauncher::launch(controlledSpec, &error);
    check(controlled.group != nullptr, "controlled launcher fixture starts in Seat process group");
    if (!controlled.group) return;

    auto unrelatedSpec = fixtureSpec(99, L"unowned");
    auto unrelated = ProcessLauncher::launch(unrelatedSpec, &error);
    check(unrelated.group != nullptr, "unrelated fixture starts outside tracked Seat set");
    if (!unrelated.group) return;

    check(waitUntil([&] { return controlled.group->snapshot().processes.size() >= 2u; },
                    std::chrono::milliseconds(2000)),
          "launcher-spawned game process is captured by Seat Job Object");

    const auto unrelatedRoot = unrelated.group->rootIdentity();
    HWND unrelatedWindow = nullptr;
    check(waitUntil([&] {
        unrelatedWindow = findFixtureWindow(unrelatedRoot.processId, L"Hydra Unowned");
        return unrelatedWindow != nullptr;
    }, std::chrono::milliseconds(2000)),
          "unrelated top-level window exists for non-adoption proof");

    WindowTrackerOptions trackerOptions;
    trackerOptions.callbackQueueCapacity = 16;
    trackerOptions.eventHistoryCapacity = 256;
    WindowTracker tracker(trackerOptions);
    WindowProfileRules rules;
    rules.defaultRole = WindowRole::Ignored;

    WindowRule popupRule;
    popupRule.role = WindowRole::ChildOwnedPopup;
    popupRule.titleContains = L"Hydra Game Popup";
    rules.overrides.push_back(std::move(popupRule));
    WindowRule launcherRule;
    launcherRule.role = WindowRole::Launcher;
    launcherRule.titleContains = L"Hydra Launcher";
    launcherRule.rootProcessOnly = true;
    rules.overrides.push_back(std::move(launcherRule));

    WindowRule gameRule;
    gameRule.role = WindowRole::PrimaryGame;
    gameRule.titleContains = L"Hydra Game";
    rules.overrides.push_back(std::move(gameRule));
    check(tracker.setProfileRules(std::move(rules), &error),
          "typed launcher classification rule is accepted");
    tracker.setProcessTrees({controlled.group->snapshot()});
    check(tracker.start(&error), "WinEvent window tracker starts on dedicated pump thread");
    if (!tracker.running()) return;

    check(waitUntil([&] { return tracker.snapshot().windows.size() >= 4u; },
                    std::chrono::milliseconds(2500)),
          "initial enumeration attributes multiple launcher/game windows");
    auto initial = tracker.snapshot();
    check(initial.windows.size() == 4u,
          "only four controlled fixture windows are adopted");
    check(std::all_of(initial.windows.begin(), initial.windows.end(),
                      [](const TrackedWindow& window) { return window.seatId == 21u; }),
          "every adopted window is attributed to the controlled Seat");
    check(!findTitle(initial, L"Hydra Unowned").has_value(),
          "unowned process window is never adopted");

    const auto launcher = findTitle(initial, L"Hydra Launcher");
    const auto launcherExtra = findTitle(initial, L"Hydra Launcher Extra");
    const auto game = findTitle(initial, L"Hydra Game");
    const auto popup = findTitle(initial, L"Hydra Game Popup");
    check(launcher && launcherExtra && game && popup,
          "controlled multi-window titles are all present");
    if (!(launcher && launcherExtra && game && popup)) {
        tracker.stop();
        return;
    }

    check(launcher->role == WindowRole::Launcher &&
              launcherExtra->role == WindowRole::Launcher,
          "profile rule classifies root launcher windows deterministically");
    check(game->role == WindowRole::PrimaryGame,
          "game child window receives default primary-game role");
    check(popup->role == WindowRole::ChildOwnedPopup && popup->ownerHandle != 0,
          "owned popup is classified without being confused with unrelated windows");
    check(launcher->identity.process.processId == controlled.root.processId &&
              game->identity.process.processId != controlled.root.processId,
          "launcher-spawned child window is attributed through process-tree identity");

    const auto launcherPid = launcher->identity.process.processId;
    const auto gamePid = game->identity.process.processId;
    const auto launcherWindowCount = std::count_if(
        initial.windows.begin(), initial.windows.end(), [launcherPid](const TrackedWindow& window) {
            return window.identity.process.processId == launcherPid;
        });
    const auto gameWindowCount = std::count_if(
        initial.windows.begin(), initial.windows.end(), [gamePid](const TrackedWindow& window) {
            return window.identity.process.processId == gamePid;
        });
    check(launcherWindowCount == 2 && gameWindowCount == 2,
          "multiple top-level/owned windows from each process remain distinct");

    check(tracker.validateIdentity(game->identity),
          "live HWND validates against exact process creation identity");
    auto staleGeneration = game->identity;
    ++staleGeneration.trackerGeneration;
    check(!tracker.validateIdentity(staleGeneration),
          "tracker generation rejects stale/reused HWND identity in the same process");
    auto stale = game->identity;
    ++stale.process.creationTime100ns;
    check(!tracker.validateIdentity(stale),
          "stale/reused HWND candidate with wrong process creation identity is rejected");
    WindowIdentity fake = game->identity;
    fake.nativeHandle = 1u;
    check(!tracker.validateIdentity(fake), "fake HWND is rejected before any action");

    const auto gameHwnd = reinterpret_cast<HWND>(game->identity.nativeHandle);
    check(SetWindowTextW(gameHwnd, L"Hydra Game Renamed") != FALSE,
          "fixture title mutation request succeeds");
    check(waitUntil([&] {
        return findTitle(tracker.snapshot(), L"Hydra Game Renamed").has_value();
    }, std::chrono::milliseconds(2000)),
          "EVENT_OBJECT_NAMECHANGE updates authoritative title snapshot");

    check(SetWindowPos(gameHwnd, nullptr, 333, 222, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
          "fixture location mutation request succeeds");
    check(waitUntil([&] {
        const auto renamed = findTitle(tracker.snapshot(), L"Hydra Game Renamed");
        return renamed && renamed->bounds.left == 333 && renamed->bounds.top == 222;
    }, std::chrono::milliseconds(2000)),
          "EVENT_OBJECT_LOCATIONCHANGE updates window bounds");

    check(ShowWindowAsync(gameHwnd, SW_HIDE) != FALSE,
          "fixture hide request is accepted");
    check(waitUntil([&] {
        const auto renamed = findTitle(tracker.snapshot(), L"Hydra Game Renamed");
        return renamed && !renamed->visible;
    }, std::chrono::milliseconds(2000)),
          "EVENT_OBJECT_HIDE updates visibility");
    check(ShowWindowAsync(gameHwnd, SW_SHOWNA) != FALSE,
          "fixture show request is accepted");
    check(waitUntil([&] {
        const auto renamed = findTitle(tracker.snapshot(), L"Hydra Game Renamed");
        return renamed && renamed->visible;
    }, std::chrono::milliseconds(2000)),
          "EVENT_OBJECT_SHOW restores visibility");

    const auto popupHandle = reinterpret_cast<HWND>(popup->identity.nativeHandle);
    check(PostMessageW(popupHandle, WM_CLOSE, 0, 0) != FALSE,
          "fixture popup close request is posted");
    check(waitUntil([&] {
        return !findTitle(tracker.snapshot(), L"Hydra Game Popup").has_value();
    }, std::chrono::milliseconds(2000)),
          "destroy event removes tracked window idempotently");
    (void)tracker.notifyWindowChange(popup->identity.nativeHandle, WindowChangeHint::Destroyed);
    (void)tracker.notifyWindowChange(popup->identity.nativeHandle, WindowChangeHint::Destroyed);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    check(!findTitle(tracker.snapshot(), L"Hydra Game Popup").has_value(),
          "duplicate destroy observations do not recreate or corrupt state");

    check(unrelatedWindow != nullptr && IsWindow(unrelatedWindow) != FALSE,
          "unrelated window remains alive after controlled-window updates");
    std::wstring unrelatedTitle(64u, L'\0');
    const int copied = GetWindowTextW(unrelatedWindow, unrelatedTitle.data(),
                                      static_cast<int>(unrelatedTitle.size()));
    unrelatedTitle.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0u);
    check(unrelatedTitle == L"Hydra Unowned",
          "unrelated window title/state is untouched by tracker");

    bool historyOverflow = false;
    const auto events = tracker.eventsAfter(initial.sequence, 128u, historyOverflow);
    const auto hasHint = [&](WindowChangeHint hint) {
        return std::any_of(events.begin(), events.end(),
                           [hint](const WindowTrackerEvent& event) { return event.hint == hint; });
    };
    const bool completeHistory = !historyOverflow && hasHint(WindowChangeHint::TitleChanged) &&
              hasHint(WindowChangeHint::LocationChanged) &&
              hasHint(WindowChangeHint::Hidden) &&
              hasHint(WindowChangeHint::Shown) &&
              hasHint(WindowChangeHint::Destroyed);
    if (!completeHistory) {
        std::cerr << "Window history diagnostic: overflow=" << historyOverflow
                  << " events=" << events.size() << " hints=";
        for (const auto& event : events) {
            std::cerr << static_cast<int>(event.hint) << ',';
        }
        std::cerr << '\n';
    }
    check(completeHistory,
          "bounded host-facing event history exposes title/location/show/hide/destroy changes");

    const auto beforeOverflow = findTitle(tracker.snapshot(), L"Hydra Game Renamed");
    check(beforeOverflow.has_value(), "live controlled window exists before overflow recovery");
    tracker.stop();
    for (std::uintptr_t handle = 100001u; handle <= 100016u; ++handle) {
        check(tracker.notifyWindowChange(handle, WindowChangeHint::Created),
              "bounded callback queue accepts entries up to its configured capacity");
    }
    check(!tracker.notifyWindowChange(100017u, WindowChangeHint::Created),
          "callback queue overflow is visible and rejects excess work");
    error.clear();
    check(tracker.start(&error), "window tracker restarts after queued overflow");
    check(waitUntil([&] {
        const auto refreshed = findTitle(tracker.snapshot(), L"Hydra Game Renamed");
        const auto refreshedSnapshot = tracker.snapshot();
        return refreshed && refreshedSnapshot.droppedCallbackEvents >= 1u &&
               (!beforeOverflow || refreshed->identity.trackerGeneration !=
                                      beforeOverflow->identity.trackerGeneration);
    }, std::chrono::milliseconds(2000)),
          "overflow invalidates prior identities and full rescan issues a fresh generation");
    const auto afterOverflow = findTitle(tracker.snapshot(), L"Hydra Game Renamed");
    if (beforeOverflow && afterOverflow) {
        check(!tracker.validateIdentity(beforeOverflow->identity),
              "pre-overflow window identity fails closed after rescan");
        check(tracker.validateIdentity(afterOverflow->identity),
              "post-overflow window identity validates after authoritative rescan");
    }
    tracker.stop();
    ProcessStopPolicy cleanup;
    cleanup.gracefulTimeoutMs = 100;
    cleanup.forcedTimeoutMs = 2000;
    check(controlled.group->stop(cleanup, &error),
          "controlled launcher/game tree exits without orphan fixture processes");
    check(unrelated.group->stop(cleanup, &error),
          "unrelated fixture is cleaned only by its own process owner");
}

#endif

} // namespace

int main() {
#ifdef _WIN32
    testQueueOverflowIsVisible();
    testProfileDiagnostics();
    testRealWindowTracking();
#else
    std::cout << "Window tracker integration tests are Windows-only.\n";
#endif
    if (failures != 0) {
        std::cerr << failures << " window tracker test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Window tracker tests passed.\n";
    return EXIT_SUCCESS;
}
