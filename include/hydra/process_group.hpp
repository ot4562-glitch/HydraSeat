#pragma once

#include "hydra/workspace_manager.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hydra::process {

enum class ChildTrackingCapability : std::uint8_t {
    FullJobObject = 0,
    JobObjectBreakawayAllowed = 1,
    RootOnly = 2,
};

struct ProcessIdentity {
    std::uint32_t processId{0};
    std::uint64_t creationTime100ns{0};
    std::wstring executablePath;

    bool valid() const noexcept {
        return processId != 0 && creationTime100ns != 0 && !executablePath.empty();
    }

    bool sameInstance(const ProcessIdentity& other) const noexcept {
        return processId == other.processId &&
               creationTime100ns == other.creationTime100ns &&
               processId != 0;
    }

    friend bool operator==(const ProcessIdentity&, const ProcessIdentity&) = default;
};

struct ProcessRecord {
    ProcessIdentity identity;
    std::uint32_t parentProcessId{0};
    bool root{false};
    bool exited{false};
    std::uint32_t exitCode{0};
};

struct ProcessTreeSnapshot {
    SeatId seatId{0};
    ChildTrackingCapability capability{ChildTrackingCapability::RootOnly};
    ProcessIdentity root;
    std::vector<ProcessRecord> processes;
    std::uint64_t sequence{0};

    std::size_t runningCount() const noexcept;
};

struct ProcessStopPolicy {
    std::uint32_t gracefulTimeoutMs{3000};
    std::uint32_t forcedTimeoutMs{3000};
    bool forceTerminate{true};
    std::uint32_t forcedExitCode{0x48594452u}; // "HYDR"
};

// Owns only processes that were launched/attached by ProcessLauncher. A full
// Job Object group follows compatible descendants and uses creation time as
// part of process identity so stale PIDs cannot be mistaken for a new process.
class SeatProcessGroup {
public:
    ~SeatProcessGroup();
    SeatProcessGroup(const SeatProcessGroup&) = delete;
    SeatProcessGroup& operator=(const SeatProcessGroup&) = delete;
    SeatProcessGroup(SeatProcessGroup&&) noexcept;
    SeatProcessGroup& operator=(SeatProcessGroup&&) noexcept;

    SeatId seatId() const noexcept;
    ChildTrackingCapability capability() const noexcept;
    ProcessIdentity rootIdentity() const;
    ProcessTreeSnapshot snapshot() const;

    bool waitForEmpty(std::uint32_t timeoutMs) const;
    bool stop(const ProcessStopPolicy& policy, std::string* error = nullptr) noexcept;

private:
    class Impl;
    explicit SeatProcessGroup(std::unique_ptr<Impl> impl);
    static std::unique_ptr<SeatProcessGroup> adoptLaunchedProcess(
        SeatId seatId, std::uintptr_t processHandle, std::uint32_t processId,
        bool allowBreakawayChildren, bool allowRootOnlyFallback,
        bool forceRootOnly, std::string* error);
    std::unique_ptr<Impl> impl_;

    friend class ProcessLauncher;
};

} // namespace hydra::process
