#include "hydra/process_group.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace hydra::process {
namespace {

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
    HANDLE release() noexcept {
        HANDLE handle = value;
        value = nullptr;
        return handle;
    }
    void reset(HANDLE replacement = nullptr) noexcept {
        if (valid()) CloseHandle(value);
        value = replacement;
    }
};

std::string win32Error(const char* prefix, DWORD code = GetLastError()) {
    return std::string(prefix) + " (Win32=" + std::to_string(code) + ")";
}

std::uint64_t fileTimeValue(const FILETIME& value) noexcept {
    ULARGE_INTEGER result{};
    result.LowPart = value.dwLowDateTime;
    result.HighPart = value.dwHighDateTime;
    return result.QuadPart;
}

ProcessIdentity processIdentity(HANDLE process, std::uint32_t processId) {
    ProcessIdentity identity;
    identity.processId = processId;

    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE) {
        identity.creationTime100ns = fileTimeValue(created);
    }

    std::wstring path(32768u, L'\0');
    DWORD chars = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(process, 0, path.data(), &chars) != FALSE) {
        path.resize(chars);
        identity.executablePath = std::move(path);
    }
    return identity;
}

std::uint32_t queryParentProcessId(std::uint32_t processId) {
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.value, &entry) == FALSE) return 0;
    do {
        if (entry.th32ProcessID == processId) return entry.th32ParentProcessID;
    } while (Process32NextW(snapshot.value, &entry) != FALSE);
    return 0;
}

struct CloseWindowContext {
    const std::vector<ProcessIdentity>* processes{nullptr};
};

BOOL CALLBACK closeOwnedWindow(HWND window, LPARAM parameter) {
    auto* context = reinterpret_cast<CloseWindowContext*>(parameter);
    if (context == nullptr || context->processes == nullptr) return TRUE;
    DWORD processId = 0;
    (void)GetWindowThreadProcessId(window, &processId);
    const auto found = std::find_if(
        context->processes->begin(), context->processes->end(),
        [processId](const ProcessIdentity& identity) {
            return identity.processId == processId;
        });
    if (found == context->processes->end()) return TRUE;

    UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                     FALSE, processId));
    if (!process.valid()) return TRUE;
    const auto observed = processIdentity(process.value, processId);
    if (observed.sameInstance(*found)) {
        (void)PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

#endif

} // namespace

std::size_t ProcessTreeSnapshot::runningCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        processes.begin(), processes.end(),
        [](const ProcessRecord& process) { return !process.exited; }));
}

class SeatProcessGroup::Impl {
public:
    explicit Impl(SeatId ownerSeat) : seat(ownerSeat) {}

    ~Impl() {
#ifdef _WIN32
        stopWorker.store(true, std::memory_order_release);
        if (completion.valid()) {
            (void)PostQueuedCompletionStatus(completion.value, 0, 0, nullptr);
        }
        if (worker.joinable()) worker.join();

        if (job.valid()) {
            (void)TerminateJobObject(job.value, 0x48594458u); // "HYDX"
        } else {
            HANDLE rootHandle = nullptr;
            {
                std::lock_guard lock(mutex);
                for (const auto& entry : entries) {
                    if (entry.record.root && !entry.record.exited && entry.handle.valid()) {
                        rootHandle = entry.handle.value;
                        break;
                    }
                }
            }
            if (rootHandle != nullptr && WaitForSingleObject(rootHandle, 0) == WAIT_TIMEOUT) {
                (void)TerminateProcess(rootHandle, 0x48594458u);
                (void)WaitForSingleObject(rootHandle, 2000u);
            }
        }
#endif
    }

    struct Entry {
        ProcessRecord record;
#ifdef _WIN32
        UniqueHandle handle;
#endif
    };

    SeatId seat{0};
    ChildTrackingCapability tracking{ChildTrackingCapability::RootOnly};
    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    ProcessIdentity root;
    std::vector<Entry> entries;
    std::uint64_t sequence{0};

#ifdef _WIN32
    UniqueHandle job;
    UniqueHandle completion;
    UniqueHandle rootWaitHandle;
    std::atomic<bool> stopWorker{false};
    std::thread worker;

    bool containsRunningProcessLocked() const {
        return std::any_of(entries.begin(), entries.end(),
                           [](const Entry& entry) { return !entry.record.exited; });
    }

    bool refreshExitedLocked() {
        bool refreshed = false;
        for (auto& entry : entries) {
            if (entry.record.exited || !entry.handle.valid()) continue;
            if (WaitForSingleObject(entry.handle.value, 0) != WAIT_OBJECT_0) continue;

            DWORD exitCode = 0;
            if (GetExitCodeProcess(entry.handle.value, &exitCode) != FALSE &&
                exitCode != STILL_ACTIVE) {
                entry.record.exitCode = exitCode;
            }
            entry.record.exited = true;
            entry.handle.reset();
            ++sequence;
            refreshed = true;
        }
        if (refreshed) changed.notify_all();
        return refreshed;
    }

    void refreshExited() {
        std::lock_guard lock(mutex);
        (void)refreshExitedLocked();
    }

    void addOwnedHandle(HANDLE handle, std::uint32_t processId, bool isRoot) {
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE || processId == 0) {
            if (handle != nullptr && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            return;
        }
        ProcessIdentity identity = processIdentity(handle, processId);
        if (!identity.valid()) {
            CloseHandle(handle);
            return;
        }

        std::lock_guard lock(mutex);
        for (const auto& entry : entries) {
            if (!entry.record.exited && entry.record.identity.sameInstance(identity)) {
                CloseHandle(handle);
                return;
            }
        }

        Entry entry;
        entry.record.identity = identity;
        entry.record.parentProcessId = isRoot ? 0u : queryParentProcessId(processId);
        entry.record.root = isRoot;
        entry.handle = UniqueHandle(handle);
        if (isRoot) root = identity;
        entries.push_back(std::move(entry));
        ++sequence;
        changed.notify_all();
    }

    void addChild(std::uint32_t processId) {
        UniqueHandle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, processId));
        if (!process.valid()) return;
        if (job.valid()) {
            BOOL inJob = FALSE;
            if (IsProcessInJob(process.value, job.value, &inJob) == FALSE || inJob == FALSE) {
                return;
            }
        }
        addOwnedHandle(process.release(), processId, false);
    }

    void markExited(std::uint32_t processId) {
        std::lock_guard lock(mutex);
        for (auto& entry : entries) {
            if (entry.record.identity.processId != processId || entry.record.exited ||
                !entry.handle.valid()) {
                continue;
            }

            // JOB_OBJECT_MSG_EXIT_PROCESS is authoritative for this Job Object
            // membership. The process handle can become signaled a few scheduler
            // ticks after the completion packet is dequeued, so do not discard the
            // one-shot notification just because a zero-time wait races it.
            (void)WaitForSingleObject(entry.handle.value, 1000u);
            DWORD exitCode = 0;
            if (GetExitCodeProcess(entry.handle.value, &exitCode) != FALSE &&
                exitCode != STILL_ACTIVE) {
                entry.record.exitCode = exitCode;
            }
            entry.record.exited = true;
            entry.handle.reset();
            ++sequence;
            changed.notify_all();
            return;
        }
    }

    void completionLoop() {
        while (!stopWorker.load(std::memory_order_acquire)) {
            DWORD message = 0;
            ULONG_PTR key = 0;
            LPOVERLAPPED value = nullptr;
            const BOOL ok = GetQueuedCompletionStatus(completion.value, &message, &key,
                                                       &value, 250u);
            if (stopWorker.load(std::memory_order_acquire)) break;
            if (ok == FALSE && value == nullptr) continue;
            const auto processId = static_cast<std::uint32_t>(
                reinterpret_cast<ULONG_PTR>(value));
            switch (message) {
                case JOB_OBJECT_MSG_NEW_PROCESS:
                    addChild(processId);
                    break;
                case JOB_OBJECT_MSG_EXIT_PROCESS:
                case JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS:
                    markExited(processId);
                    break;
                case JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO:
                    // Individual exit packets can race handle signaling. The
                    // empty-job notification is a safe point to reconcile every
                    // exact handle that this Seat already owns.
                    refreshExited();
                    break;
                default:
                    break;
            }
        }
    }

    void rootOnlyWaitLoop() {
        if (!rootWaitHandle.valid()) return;
        while (!stopWorker.load(std::memory_order_acquire)) {
            const DWORD wait = WaitForSingleObject(rootWaitHandle.value, 250u);
            if (wait == WAIT_OBJECT_0) {
                markExited(root.processId);
                return;
            }
            if (wait != WAIT_TIMEOUT) return;
        }
    }

    std::vector<ProcessIdentity> runningProcessIdentities() const {
        std::lock_guard lock(mutex);
        std::vector<ProcessIdentity> identities;
        identities.reserve(entries.size());
        for (const auto& entry : entries) {
            if (!entry.record.exited) identities.push_back(entry.record.identity);
        }
        return identities;
    }
#endif
};

SeatProcessGroup::SeatProcessGroup(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SeatProcessGroup::~SeatProcessGroup() = default;
SeatProcessGroup::SeatProcessGroup(SeatProcessGroup&&) noexcept = default;
SeatProcessGroup& SeatProcessGroup::operator=(SeatProcessGroup&&) noexcept = default;

SeatId SeatProcessGroup::seatId() const noexcept {
    return impl_ ? impl_->seat : 0;
}

ChildTrackingCapability SeatProcessGroup::capability() const noexcept {
    return impl_ ? impl_->tracking : ChildTrackingCapability::RootOnly;
}

ProcessIdentity SeatProcessGroup::rootIdentity() const {
    if (!impl_) return {};
    std::lock_guard lock(impl_->mutex);
    return impl_->root;
}

ProcessTreeSnapshot SeatProcessGroup::snapshot() const {
    ProcessTreeSnapshot result;
    if (!impl_) return result;
    std::lock_guard lock(impl_->mutex);
    result.seatId = impl_->seat;
    result.capability = impl_->tracking;
    result.root = impl_->root;
    result.sequence = impl_->sequence;
    result.processes.reserve(impl_->entries.size());
    for (const auto& entry : impl_->entries) result.processes.push_back(entry.record);
    std::sort(result.processes.begin(), result.processes.end(),
              [](const ProcessRecord& left, const ProcessRecord& right) {
                  if (left.root != right.root) return left.root;
                  if (left.identity.creationTime100ns != right.identity.creationTime100ns) {
                      return left.identity.creationTime100ns < right.identity.creationTime100ns;
                  }
                  return left.identity.processId < right.identity.processId;
              });
    return result;
}

bool SeatProcessGroup::waitForEmpty(std::uint32_t timeoutMs) const {
    if (!impl_) return true;
#ifdef _WIN32
    std::unique_lock lock(impl_->mutex);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    for (;;) {
        (void)impl_->refreshExitedLocked();
        if (!impl_->containsRunningProcessLocked()) return true;
        if (timeoutMs == 0u || std::chrono::steady_clock::now() >= deadline) return false;

        const auto nextCheck = std::min(
            deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(50));
        impl_->changed.wait_until(lock, nextCheck);
    }
#else
    (void)timeoutMs;
    return true;
#endif
}

bool SeatProcessGroup::stop(const ProcessStopPolicy& policy, std::string* error) noexcept {
    if (!impl_) return true;
#ifdef _WIN32
    const auto processes = impl_->runningProcessIdentities();
    if (processes.empty()) return true;

    CloseWindowContext context{&processes};
    (void)EnumWindows(closeOwnedWindow, reinterpret_cast<LPARAM>(&context));
    if (waitForEmpty(policy.gracefulTimeoutMs)) return true;

    if (!policy.forceTerminate) {
        if (error) *error = "owned process group did not exit before graceful timeout";
        return false;
    }

    bool terminated = false;
    if (impl_->job.valid()) {
        terminated = TerminateJobObject(impl_->job.value, policy.forcedExitCode) != FALSE;
        if (!terminated && error) *error = win32Error("TerminateJobObject failed");
    } else {
        std::lock_guard lock(impl_->mutex);
        for (auto& entry : impl_->entries) {
            if (!entry.record.root || entry.record.exited || !entry.handle.valid()) continue;
            terminated = TerminateProcess(entry.handle.value, policy.forcedExitCode) != FALSE;
            if (!terminated && error) *error = win32Error("TerminateProcess failed");
            break;
        }
    }
    if (!terminated) return false;
    if (!waitForEmpty(policy.forcedTimeoutMs)) {
        if (error) *error = "owned process group remained alive after forced termination";
        return false;
    }
    return true;
#else
    (void)policy;
    if (error) *error = "Seat process groups are Windows-only";
    return false;
#endif
}

std::unique_ptr<SeatProcessGroup> SeatProcessGroup::adoptLaunchedProcess(
    SeatId ownerSeat, std::uintptr_t processHandle, std::uint32_t processId,
    bool allowBreakawayChildren, bool allowRootOnlyFallback,
    bool forceRootOnly, std::string* error) {
#ifdef _WIN32
    if (ownerSeat == 0 || processHandle == 0 || processId == 0) {
        if (error) *error = "Seat process adoption requires nonzero Seat, handle, and PID";
        return {};
    }
    HANDLE rootHandle = reinterpret_cast<HANDLE>(processHandle);
    const auto identity = processIdentity(rootHandle, processId);
    if (!identity.valid()) {
        if (error) *error = "unable to capture root PID creation identity";
        return {};
    }

    auto impl = std::make_unique<Impl>(ownerSeat);
    UniqueHandle completion;
    UniqueHandle job;
    bool jobReady = false;
    DWORD setupError = ERROR_SUCCESS;
    if (!forceRootOnly) {
        completion = UniqueHandle(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1));
        job = UniqueHandle(CreateJobObjectW(nullptr, nullptr));
        jobReady = completion.valid() && job.valid();
        if (!jobReady) setupError = GetLastError();
    }

    if (jobReady) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (allowBreakawayChildren) {
            limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_BREAKAWAY_OK;
        }
        if (SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                    &limits, sizeof(limits)) == FALSE) {
            setupError = GetLastError();
            jobReady = false;
        }
    }
    if (jobReady) {
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT association{};
        association.CompletionKey = impl.get();
        association.CompletionPort = completion.value;
        if (SetInformationJobObject(job.value, JobObjectAssociateCompletionPortInformation,
                                    &association, sizeof(association)) == FALSE) {
            setupError = GetLastError();
            jobReady = false;
        }
    }
    if (jobReady && AssignProcessToJobObject(job.value, rootHandle) == FALSE) {
        setupError = GetLastError();
        jobReady = false;
    }

    if (!jobReady && !allowRootOnlyFallback && !forceRootOnly) {
        if (error) *error = win32Error("unable to establish required Job Object containment",
                                      setupError);
        return {};
    }

    if (jobReady) {
        impl->tracking = allowBreakawayChildren
            ? ChildTrackingCapability::JobObjectBreakawayAllowed
            : ChildTrackingCapability::FullJobObject;
        impl->completion = std::move(completion);
        impl->job = std::move(job);
    } else {
        impl->tracking = ChildTrackingCapability::RootOnly;
        job.reset();
        completion.reset();
        HANDLE duplicate = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), rootHandle, GetCurrentProcess(), &duplicate,
                            SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0) == FALSE) {
            if (error) *error = win32Error("unable to duplicate root process wait handle");
            return {};
        }
        impl->rootWaitHandle = UniqueHandle(duplicate);
    }

    // Ownership of rootHandle transfers only after every fallible setup operation
    // has succeeded. ProcessLauncher keeps responsibility on all earlier failures.
    impl->addOwnedHandle(rootHandle, processId, true);
    if (!impl->root.valid()) {
        if (error) *error = "root process identity disappeared during adoption";
        return {};
    }

    if (impl->tracking == ChildTrackingCapability::RootOnly) {
        impl->worker = std::thread([raw = impl.get()] { raw->rootOnlyWaitLoop(); });
    } else {
        impl->worker = std::thread([raw = impl.get()] { raw->completionLoop(); });
    }
    return std::unique_ptr<SeatProcessGroup>(new SeatProcessGroup(std::move(impl)));
#else
    (void)ownerSeat;
    (void)processHandle;
    (void)processId;
    (void)allowBreakawayChildren;
    (void)allowRootOnlyFallback;
    (void)forceRootOnly;
    if (error) *error = "Seat process groups are Windows-only";
    return {};
#endif
}

} // namespace hydra::process
