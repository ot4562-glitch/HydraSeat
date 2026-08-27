#include "hydra/window_tracker.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hydra::windowing {
namespace {

constexpr std::size_t kMaxProfileRules = 64u;
constexpr std::size_t kMaxRuleTextChars = 512u;
constexpr std::size_t kMaxWindowTextChars = 2048u;
constexpr std::size_t kMaxWindowClassChars = 512u;
constexpr std::size_t kMaxCallbackQueueCapacity = 4096u;
constexpr std::size_t kMaxEventHistoryCapacity = 4096u;
constexpr std::size_t kDuplicateScanDepth = 16u;

#ifdef _WIN32

struct UniqueHandle {
    HANDLE value{nullptr};
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : value(handle) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : value(other.value) {
        other.value = nullptr;
    }
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    bool valid() const noexcept {
        return value != nullptr && value != INVALID_HANDLE_VALUE;
    }
    void reset(HANDLE replacement = nullptr) noexcept {
        if (valid()) CloseHandle(value);
        value = replacement;
    }
};

std::uint64_t fileTimeValue(const FILETIME& value) noexcept {
    ULARGE_INTEGER result{};
    result.LowPart = value.dwLowDateTime;
    result.HighPart = value.dwHighDateTime;
    return result.QuadPart;
}

process::ProcessIdentity queryProcessIdentity(HANDLE processHandle,
                                              std::uint32_t processId) {
    process::ProcessIdentity identity;
    identity.processId = processId;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(processHandle, &created, &exited, &kernel, &user) != FALSE) {
        identity.creationTime100ns = fileTimeValue(created);
    }
    std::wstring path(32768u, L'\0');
    DWORD chars = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(processHandle, 0, path.data(), &chars) != FALSE) {
        path.resize(chars);
        identity.executablePath = std::move(path);
    }
    return identity;
}

std::wstring windowText(HWND window) {
    const int rawLength = GetWindowTextLengthW(window);
    const std::size_t requested = rawLength > 0
        ? std::min<std::size_t>(static_cast<std::size_t>(rawLength),
                                kMaxWindowTextChars)
        : 0u;
    std::wstring value(requested + 1u, L'\0');
    const int copied = GetWindowTextW(window, value.data(),
                                      static_cast<int>(value.size()));
    if (copied <= 0) return {};
    value.resize(static_cast<std::size_t>(copied));
    return value;
}

std::wstring windowClass(HWND window) {
    std::wstring value(kMaxWindowClassChars + 1u, L'\0');
    const int copied = GetClassNameW(window, value.data(),
                                     static_cast<int>(value.size()));
    if (copied <= 0) return {};
    value.resize(static_cast<std::size_t>(copied));
    return value;
}

std::wstring lowercase(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return result;
}

bool containsInsensitive(std::wstring_view value, std::wstring_view needle) {
    if (needle.empty()) return true;
    const auto haystackLower = lowercase(value);
    const auto needleLower = lowercase(needle);
    return haystackLower.find(needleLower) != std::wstring::npos;
}

bool equalsInsensitive(std::wstring_view left, std::wstring_view right) {
    if (left.size() != right.size()) return false;
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                                right.data(), static_cast<int>(right.size()),
                                TRUE) == CSTR_EQUAL;
}

#endif

} // namespace

std::string_view windowRoleName(WindowRole role) noexcept {
    switch (role) {
        case WindowRole::PrimaryGame: return "primary-game";
        case WindowRole::Launcher: return "launcher";
        case WindowRole::Dialog: return "dialog";
        case WindowRole::Overlay: return "overlay";
        case WindowRole::ChildOwnedPopup: return "child-owned-popup";
        case WindowRole::Ignored: return "ignored";
    }
    return "unknown";
}

class WindowTracker::Impl {
public:
    struct RawEvent {
        std::uintptr_t nativeHandle{0};
        WindowChangeHint hint{WindowChangeHint::Rescan};
    };

    struct OwnerMatch {
        SeatId seatId{0};
        bool rootProcess{false};
    };

    explicit Impl(WindowTrackerOptions requested) {
        options.callbackQueueCapacity = std::clamp<std::size_t>(
            requested.callbackQueueCapacity, 1u, kMaxCallbackQueueCapacity);
        options.eventHistoryCapacity = std::clamp<std::size_t>(
            requested.eventHistoryCapacity, 1u, kMaxEventHistoryCapacity);
#ifdef _WIN32
        wakeEvent = UniqueHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr));
#endif
    }

    ~Impl() {
        stop();
    }

    WindowTrackerOptions options;
    std::atomic<bool> running{false};
    std::atomic<bool> stopRequested{false};
    std::atomic<std::uint64_t> droppedCallbackEvents{0};
    std::atomic<bool> overflowDirty{false};

    mutable std::mutex configMutex;
    std::vector<process::ProcessTreeSnapshot> processTrees;
    WindowProfileRules profileRules;

    mutable std::mutex queueMutex;
    std::deque<RawEvent> queue;

    mutable std::mutex stateMutex;
    std::vector<TrackedWindow> windows;
    std::deque<WindowTrackerEvent> history;
    std::uint64_t sequence{0};
    std::uint64_t nextIdentityGeneration{1};

    std::thread worker;
    std::mutex readyMutex;
    std::condition_variable readyCv;
    bool ready{false};
    bool setupSucceeded{false};
    std::string setupError;

#ifdef _WIN32
    UniqueHandle wakeEvent;
    HWINEVENTHOOK lifecycleHook{nullptr};
    HWINEVENTHOOK propertyHook{nullptr};

    inline static thread_local Impl* callbackOwner = nullptr;

    static void CALLBACK winEventCallback(HWINEVENTHOOK hook, DWORD event, HWND window,
                                          LONG objectId, LONG childId, DWORD, DWORD) {
        if (window == nullptr || objectId != OBJID_WINDOW || childId != CHILDID_SELF) return;
        Impl* owner = callbackOwner;
        if (owner == nullptr ||
            (hook != owner->lifecycleHook && hook != owner->propertyHook)) {
            return;
        }

        WindowChangeHint hint;
        switch (event) {
            case EVENT_OBJECT_CREATE: hint = WindowChangeHint::Created; break;
            case EVENT_OBJECT_DESTROY: hint = WindowChangeHint::Destroyed; break;
            case EVENT_OBJECT_SHOW: hint = WindowChangeHint::Shown; break;
            case EVENT_OBJECT_HIDE: hint = WindowChangeHint::Hidden; break;
            case EVENT_OBJECT_NAMECHANGE: hint = WindowChangeHint::TitleChanged; break;
            case EVENT_OBJECT_LOCATIONCHANGE: hint = WindowChangeHint::LocationChanged; break;
            default: return;
        }
        (void)owner->enqueue(reinterpret_cast<std::uintptr_t>(window), hint);
    }

    static BOOL CALLBACK initialWindowCallback(HWND window, LPARAM parameter) {
        auto* self = reinterpret_cast<Impl*>(parameter);
        if (self != nullptr) self->observe(window, WindowChangeHint::Rescan);
        return TRUE;
    }
#endif

    bool enqueue(std::uintptr_t nativeHandle, WindowChangeHint hint) noexcept {
        if (hint != WindowChangeHint::Rescan && nativeHandle == 0) return false;
        {
            std::lock_guard lock(queueMutex);
            std::size_t inspected = 0;
            for (auto iterator = queue.rbegin(); iterator != queue.rend() &&
                 inspected < kDuplicateScanDepth; ++iterator, ++inspected) {
                if (iterator->nativeHandle == nativeHandle && iterator->hint == hint) {
                    return true;
                }
            }
            if (queue.size() >= options.callbackQueueCapacity) {
                droppedCallbackEvents.fetch_add(1, std::memory_order_relaxed);
                overflowDirty.store(true, std::memory_order_release);
#ifdef _WIN32
                if (wakeEvent.valid()) SetEvent(wakeEvent.value);
#endif
                return false;
            }
            queue.push_back(RawEvent{nativeHandle, hint});
        }
#ifdef _WIN32
        if (wakeEvent.valid()) SetEvent(wakeEvent.value);
#endif
        return true;
    }

    void publishEvent(WindowChangeHint hint, std::uintptr_t nativeHandle,
                      std::optional<TrackedWindow> window) {
        ++sequence;
        WindowTrackerEvent event;
        event.sequence = sequence;
        event.hint = hint;
        event.nativeHandle = nativeHandle;
        event.window = std::move(window);
        event.droppedCallbackEvents = droppedCallbackEvents.load(std::memory_order_relaxed);
        history.push_back(std::move(event));
        while (history.size() > options.eventHistoryCapacity) history.pop_front();
    }

    bool publishOverflowIfNeeded() {
        if (!overflowDirty.exchange(false, std::memory_order_acq_rel)) return false;
        std::lock_guard lock(stateMutex);
        publishEvent(WindowChangeHint::Overflow, 0, std::nullopt);
        const auto staleWindows = windows;
        windows.clear();
        for (const auto& stale : staleWindows) {
            publishEvent(WindowChangeHint::Destroyed,
                         stale.identity.nativeHandle, stale);
        }
        return true;
    }

    std::optional<OwnerMatch> ownerFor(const process::ProcessIdentity& identity) const {
        std::lock_guard lock(configMutex);
        for (const auto& tree : processTrees) {
            for (const auto& record : tree.processes) {
                if (record.exited || !record.identity.sameInstance(identity)) continue;
                return OwnerMatch{tree.seatId, record.root};
            }
        }
        return std::nullopt;
    }

    WindowRole classify(const TrackedWindow& window, bool rootProcess) const {
        WindowProfileRules rules;
        {
            std::lock_guard lock(configMutex);
            rules = profileRules;
        }

#ifdef _WIN32
        for (const auto& rule : rules.overrides) {
            if (rule.rootProcessOnly && !rootProcess) continue;
            if (!rule.titleContains.empty() &&
                !containsInsensitive(window.title, rule.titleContains)) continue;
            if (!rule.classNameEquals.empty() &&
                !equalsInsensitive(window.className, rule.classNameEquals)) continue;
            if (!rule.executablePathContains.empty() &&
                !containsInsensitive(window.identity.process.executablePath,
                                     rule.executablePathContains)) continue;
            return rule.role;
        }

        // A profile that chooses Ignored as its default is an explicit
        // whitelist: no heuristic is allowed to re-adopt an unspecified helper,
        // IME, overlay, or framework window from the same controlled process.
        if (rules.defaultRole == WindowRole::Ignored) return WindowRole::Ignored;

        if (equalsInsensitive(window.className, L"#32770")) return WindowRole::Dialog;
        if (window.ownerHandle != 0) return WindowRole::ChildOwnedPopup;
        const auto hwnd = reinterpret_cast<HWND>(window.identity.nativeHandle);
        const LONG_PTR extended = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if ((extended & (WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)) != 0) {
            return WindowRole::Overlay;
        }
#else
        (void)window;
        (void)rootProcess;
#endif
        return rules.defaultRole;
    }

#ifdef _WIN32
    std::optional<TrackedWindow> inspect(HWND window) const {
        if (window == nullptr || IsWindow(window) == FALSE || GetAncestor(window, GA_ROOT) != window) {
            return std::nullopt;
        }
        DWORD processId = 0;
        const DWORD threadId = GetWindowThreadProcessId(window, &processId);
        if (threadId == 0 || processId == 0) return std::nullopt;

        UniqueHandle processHandle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                               FALSE, processId));
        if (!processHandle.valid()) return std::nullopt;
        auto processIdentity = queryProcessIdentity(processHandle.value, processId);
        if (!processIdentity.valid()) return std::nullopt;
        const auto owner = ownerFor(processIdentity);
        if (!owner) return std::nullopt;

        TrackedWindow result;
        result.identity.nativeHandle = reinterpret_cast<std::uintptr_t>(window);
        result.identity.process = std::move(processIdentity);
        result.identity.threadId = threadId;
        result.seatId = owner->seatId;
        result.ownerHandle = reinterpret_cast<std::uintptr_t>(GetWindow(window, GW_OWNER));
        result.visible = IsWindowVisible(window) != FALSE;
        RECT rectangle{};
        if (GetWindowRect(window, &rectangle) != FALSE) {
            result.bounds.left = rectangle.left;
            result.bounds.top = rectangle.top;
            result.bounds.right = rectangle.right;
            result.bounds.bottom = rectangle.bottom;
        }
        result.title = windowText(window);
        result.className = windowClass(window);
        result.role = classify(result, owner->rootProcess);
        if (result.role == WindowRole::Ignored) return std::nullopt;
        return result;
    }
#endif

    void removeTracked(std::uintptr_t nativeHandle, WindowChangeHint hint) {
        std::lock_guard lock(stateMutex);
        const auto found = std::find_if(windows.begin(), windows.end(),
                                        [nativeHandle](const TrackedWindow& window) {
                                            return window.identity.nativeHandle == nativeHandle;
                                        });
        if (found == windows.end()) return;
        const auto removed = *found;
        windows.erase(found);
        publishEvent(hint, nativeHandle, removed);
    }

#ifdef _WIN32
    void observe(HWND window, WindowChangeHint hint) {
        const auto handle = reinterpret_cast<std::uintptr_t>(window);
        if (hint == WindowChangeHint::Destroyed && IsWindow(window) == FALSE) {
            removeTracked(handle, hint);
            return;
        }

        auto current = inspect(window);
        if (!current) {
            // Once a previously tracked HWND is no longer a valid owned top-level
            // window, the authoritative state transition is removal. Raw WinEvent
            // ordering can report HIDE immediately before DESTROY, so preserving
            // that raw hint here would lose the host-facing Destroyed transition.
            removeTracked(handle, WindowChangeHint::Destroyed);
            return;
        }

        std::lock_guard lock(stateMutex);
        auto found = std::find_if(windows.begin(), windows.end(),
                                  [handle](const TrackedWindow& value) {
                                      return value.identity.nativeHandle == handle;
                                  });
        const auto sameNativeOwner = [](const WindowIdentity& left,
                                        const WindowIdentity& right) {
            return left.nativeHandle == right.nativeHandle &&
                   left.threadId == right.threadId &&
                   left.process.sameInstance(right.process);
        };
        if (found != windows.end() && !sameNativeOwner(found->identity, current->identity)) {
            const auto stale = *found;
            windows.erase(found);
            publishEvent(WindowChangeHint::Destroyed, handle, stale);
            found = windows.end();
        }
        if (found == windows.end()) {
            current->identity.trackerGeneration = nextIdentityGeneration++;
            if (nextIdentityGeneration == 0) ++nextIdentityGeneration;
            windows.push_back(*current);
            publishEvent(WindowChangeHint::Created, handle, *current);
            return;
        }
        current->identity.trackerGeneration = found->identity.trackerGeneration;
        if (*found != *current) {
            const auto previous = *found;
            *found = *current;

            bool publishedSemanticChange = false;
            if (previous.title != current->title) {
                publishEvent(WindowChangeHint::TitleChanged, handle, *current);
                publishedSemanticChange = true;
            }
            if (previous.bounds != current->bounds) {
                publishEvent(WindowChangeHint::LocationChanged, handle, *current);
                publishedSemanticChange = true;
            }
            if (previous.visible != current->visible) {
                publishEvent(current->visible ? WindowChangeHint::Shown
                                              : WindowChangeHint::Hidden,
                             handle, *current);
                publishedSemanticChange = true;
            }
            if (!publishedSemanticChange) {
                publishEvent(hint, handle, *current);
            }
        }
    }

    void rescan() {
        std::vector<std::uintptr_t> before;
        {
            std::lock_guard lock(stateMutex);
            before.reserve(windows.size());
            for (const auto& window : windows) before.push_back(window.identity.nativeHandle);
        }

        (void)EnumWindows(initialWindowCallback, reinterpret_cast<LPARAM>(this));
        for (const auto handle : before) {
            const auto window = reinterpret_cast<HWND>(handle);
            if (IsWindow(window) == FALSE || !inspect(window)) {
                removeTracked(handle, WindowChangeHint::Destroyed);
            }
        }
    }
#endif

    void processQueued() {
        std::deque<RawEvent> local;
        {
            std::lock_guard lock(queueMutex);
            local.swap(queue);
#ifdef _WIN32
            if (wakeEvent.valid()) ResetEvent(wakeEvent.value);
#endif
        }
        const bool overflowed = publishOverflowIfNeeded();
#ifdef _WIN32
        if (overflowed) rescan();
        for (const auto& event : local) {
            if (event.hint == WindowChangeHint::Rescan) {
                rescan();
            } else {
                observe(reinterpret_cast<HWND>(event.nativeHandle), event.hint);
            }
        }
#else
        (void)overflowed;
        (void)local;
#endif
    }

#ifdef _WIN32
    bool installHooks(std::string& error) {
        const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
        callbackOwner = this;
        lifecycleHook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE,
                                        nullptr, winEventCallback, 0, 0, flags);
        if (lifecycleHook == nullptr) {
            callbackOwner = nullptr;
            error = "SetWinEventHook lifecycle range failed (Win32=" +
                    std::to_string(GetLastError()) + ")";
            return false;
        }

        propertyHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_NAMECHANGE,
                                       nullptr, winEventCallback, 0, 0, flags);
        if (propertyHook == nullptr) {
            error = "SetWinEventHook property range failed (Win32=" +
                    std::to_string(GetLastError()) + ")";
            return false;
        }
        return true;
    }

    void uninstallHooks() noexcept {
        if (propertyHook != nullptr) {
            (void)UnhookWinEvent(propertyHook);
            propertyHook = nullptr;
        }
        if (lifecycleHook != nullptr) {
            (void)UnhookWinEvent(lifecycleHook);
            lifecycleHook = nullptr;
        }
        if (callbackOwner == this) callbackOwner = nullptr;
    }
#endif

    void workerMain() {
#ifdef _WIN32
        std::string error;
        const bool installed = wakeEvent.valid() && installHooks(error);
        {
            std::lock_guard lock(readyMutex);
            setupSucceeded = installed;
            setupError = installed ? std::string{} :
                (wakeEvent.valid() ? std::move(error) : "unable to create window tracker wake event");
            ready = true;
            running.store(installed, std::memory_order_release);
        }
        readyCv.notify_all();
        if (!installed) {
            uninstallHooks();
            return;
        }

        rescan();
        auto lastReconcile = std::chrono::steady_clock::now();
        while (!stopRequested.load(std::memory_order_acquire)) {
            HANDLE handles[] = {wakeEvent.value};
            const DWORD wait = MsgWaitForMultipleObjects(1, handles, FALSE, 100u, QS_ALLINPUT);
            if (wait == WAIT_OBJECT_0 + 1u) {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            processQueued();

            // WinEvent is the primary path, but some custom/toolkit windows do
            // not emit every accessibility property notification consistently.
            // Reconcile on the host worker at a bounded low rate so stale title,
            // visibility, or geometry cannot persist indefinitely.
            const auto now = std::chrono::steady_clock::now();
            if (now - lastReconcile >= std::chrono::milliseconds(250)) {
                rescan();
                lastReconcile = now;
            }
        }
        processQueued();
        uninstallHooks();
        running.store(false, std::memory_order_release);
#else
        {
            std::lock_guard lock(readyMutex);
            setupSucceeded = false;
            setupError = "window tracking is Windows-only";
            ready = true;
        }
        readyCv.notify_all();
#endif
    }

    bool start(std::string* error) {
        if (running.load(std::memory_order_acquire)) return true;
        if (worker.joinable()) worker.join();
        stopRequested.store(false, std::memory_order_release);
        {
            std::lock_guard lock(readyMutex);
            ready = false;
            setupSucceeded = false;
            setupError.clear();
        }
        worker = std::thread([this] { workerMain(); });
        std::unique_lock lock(readyMutex);
        if (!readyCv.wait_for(lock, std::chrono::seconds(3), [this] { return ready; })) {
            if (error) *error = "window tracker worker did not initialize";
            lock.unlock();
            stop();
            return false;
        }
        if (!setupSucceeded) {
            if (error) *error = setupError;
            lock.unlock();
            if (worker.joinable()) worker.join();
            return false;
        }
        return true;
    }

    void stop() noexcept {
        stopRequested.store(true, std::memory_order_release);
#ifdef _WIN32
        if (wakeEvent.valid()) SetEvent(wakeEvent.value);
#endif
        if (worker.joinable()) worker.join();
        running.store(false, std::memory_order_release);
    }
};

WindowTracker::WindowTracker(WindowTrackerOptions options)
    : impl_(std::make_unique<Impl>(options)) {}
WindowTracker::~WindowTracker() = default;
WindowTracker::WindowTracker(WindowTracker&&) noexcept = default;
WindowTracker& WindowTracker::operator=(WindowTracker&&) noexcept = default;

bool WindowTracker::start(std::string* error) {
    return impl_ && impl_->start(error);
}

void WindowTracker::stop() noexcept {
    if (impl_) impl_->stop();
}

bool WindowTracker::running() const noexcept {
    return impl_ && impl_->running.load(std::memory_order_acquire);
}

void WindowTracker::setProcessTrees(std::vector<process::ProcessTreeSnapshot> trees) {
    if (!impl_) return;
    std::sort(trees.begin(), trees.end(), [](const auto& left, const auto& right) {
        return left.seatId < right.seatId;
    });
    {
        std::lock_guard lock(impl_->configMutex);
        impl_->processTrees = std::move(trees);
    }
    (void)impl_->enqueue(0, WindowChangeHint::Rescan);
}

bool WindowTracker::setProfileRules(WindowProfileRules rules, std::string* error) {
    if (!impl_) return false;
    if (rules.overrides.size() > kMaxProfileRules) {
        if (error) *error = "window profile exceeds 64 typed overrides";
        return false;
    }
    for (const auto& rule : rules.overrides) {
        if (rule.titleContains.size() > kMaxRuleTextChars ||
            rule.classNameEquals.size() > kMaxRuleTextChars ||
            rule.executablePathContains.size() > kMaxRuleTextChars) {
            if (error) *error = "window profile override text exceeds 512 characters";
            return false;
        }
        if (rule.titleContains.empty() && rule.classNameEquals.empty() &&
            rule.executablePathContains.empty() && !rule.rootProcessOnly) {
            if (error) *error = "window profile override has no typed selector";
            return false;
        }
    }
    {
        std::lock_guard lock(impl_->configMutex);
        impl_->profileRules = std::move(rules);
    }
    (void)impl_->enqueue(0, WindowChangeHint::Rescan);
    return true;
}

bool WindowTracker::notifyWindowChange(std::uintptr_t nativeHandle,
                                       WindowChangeHint hint) noexcept {
    return impl_ && impl_->enqueue(nativeHandle, hint);
}

WindowTrackerSnapshot WindowTracker::snapshot() const {
    WindowTrackerSnapshot result;
    if (!impl_) return result;
    std::lock_guard lock(impl_->stateMutex);
    result.sequence = impl_->sequence;
    result.droppedCallbackEvents = impl_->droppedCallbackEvents.load(std::memory_order_relaxed);
    result.windows = impl_->windows;
    std::sort(result.windows.begin(), result.windows.end(),
              [](const TrackedWindow& left, const TrackedWindow& right) {
                  if (left.seatId != right.seatId) return left.seatId < right.seatId;
                  if (left.identity.process.creationTime100ns !=
                      right.identity.process.creationTime100ns) {
                      return left.identity.process.creationTime100ns <
                             right.identity.process.creationTime100ns;
                  }
                  return left.identity.nativeHandle < right.identity.nativeHandle;
              });
    return result;
}

std::vector<WindowTrackerEvent> WindowTracker::eventsAfter(std::uint64_t afterSequence,
                                                            std::size_t maxEvents,
                                                            bool& overflow) const {
    overflow = false;
    std::vector<WindowTrackerEvent> result;
    if (!impl_ || maxEvents == 0) return result;
    std::lock_guard lock(impl_->stateMutex);
    if (impl_->history.empty()) return result;
    const auto oldest = impl_->history.front().sequence;
    if (afterSequence + 1u < oldest) {
        overflow = true;
        return result;
    }
    for (const auto& event : impl_->history) {
        if (event.sequence <= afterSequence) continue;
        if (result.size() >= maxEvents) {
            overflow = true;
            break;
        }
        result.push_back(event);
    }
    return result;
}

bool WindowTracker::validateIdentity(const WindowIdentity& identity) const noexcept {
    if (!identity.valid()) return false;
#ifdef _WIN32
    const auto window = reinterpret_cast<HWND>(identity.nativeHandle);
    if (IsWindow(window) == FALSE) return false;
    DWORD processId = 0;
    const DWORD threadId = GetWindowThreadProcessId(window, &processId);
    if (threadId != identity.threadId || processId != identity.process.processId) return false;
    UniqueHandle processHandle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                           FALSE, processId));
    if (!processHandle.valid()) return false;
    const auto current = queryProcessIdentity(processHandle.value, processId);
    if (!current.sameInstance(identity.process)) return false;
    std::lock_guard lock(impl_->stateMutex);
    const auto tracked = std::find_if(
        impl_->windows.begin(), impl_->windows.end(),
        [&](const TrackedWindow& windowState) {
            return windowState.identity.nativeHandle == identity.nativeHandle;
        });
    return tracked != impl_->windows.end() && tracked->identity.sameInstance(identity);
#else
    return false;
#endif
}

} // namespace hydra::windowing
