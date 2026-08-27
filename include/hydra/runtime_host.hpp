#pragma once

#include "hydra/runtime_state.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::runtime {

enum class RuntimeBackendKind : std::uint8_t {
    Hardware = 0,
    Input = 1,
    Process = 2,
    Window = 3,
    Display = 4,
    Audio = 5
};

class IRuntimeBackend {
public:
    virtual ~IRuntimeBackend() = default;

    virtual RuntimeBackendKind kind() const noexcept = 0;
    virtual std::string_view name() const noexcept = 0;
    virtual bool prepare(const RuntimeSessionId& sessionId,
                         std::span<const SeatConfig> seats,
                         std::string& error) = 0;
    virtual bool start(std::string& error) = 0;
    virtual bool rollback(std::string& error) noexcept = 0;
    virtual bool verifySafe(std::string& error) noexcept = 0;
};

// RuntimeHost is the single writer for production session state. Mutation calls
// are serialized with a non-blocking try-lock and backend work runs only on the
// caller's control thread. Snapshot readers take a coherent copy under the same
// mutex. No latency-sensitive input callback may call a mutation method.
class RuntimeHost {
public:
    explicit RuntimeHost(std::vector<std::shared_ptr<IRuntimeBackend>> backends = {});
    ~RuntimeHost();

    RuntimeHost(const RuntimeHost&) = delete;
    RuntimeHost& operator=(const RuntimeHost&) = delete;

    HostRuntimeSnapshot snapshot() const;
    std::vector<RuntimeTransition> transitionEventsAfter(
        std::uint64_t sequence, std::size_t maxEvents, bool& overflow) const;
    RuntimeCommandResult loadProfile(std::vector<SeatConfig> seats,
                                     std::uint64_t correlationId);
    RuntimeCommandResult plan(std::uint64_t correlationId);
    RuntimeCommandResult prepare(std::uint64_t correlationId);
    RuntimeCommandResult start(std::uint64_t correlationId);
    RuntimeCommandResult stopAndReturnToWindows(std::uint64_t correlationId);
    RuntimeCommandResult reset(std::uint64_t correlationId);
    RuntimeCommandResult exitHostWhenIdle(std::uint64_t correlationId);
    RuntimeCommandResult markDegraded(std::string diagnostic,
                                      std::uint64_t correlationId);

    void controlClientConnected();
    void controlClientDisconnected();

private:
    RuntimeCommandResult busyResult(RuntimeCommand command,
                                    std::uint64_t correlationId) const;
    RuntimeCommandResult finishLocked(RuntimeCommand command,
                                      SeatSessionPhase from,
                                      RuntimeResultCode code,
                                      std::string diagnostic,
                                      std::uint64_t correlationId);
    void setSessionPhaseLocked(SeatSessionPhase phase, std::string_view diagnostic = {});
    bool rollbackBackends(std::size_t rollbackCount,
                          std::string& diagnostic) noexcept;
    HostRuntimeSnapshot snapshotLocked() const;
    static bool validateProfile(std::span<const SeatConfig> seats, std::string& error);
    static RuntimeSessionId newSessionId(std::uint64_t generation);

    // mutationMutex_ rejects only concurrent writers. mutex_ protects coherent
    // state snapshots; a short-lived reader must never cause a command to fail.
    std::mutex mutationMutex_;
    mutable std::mutex mutex_;
    HostLifecyclePhase hostPhase_{HostLifecyclePhase::Starting};
    SeatSessionPhase sessionPhase_{SeatSessionPhase::Idle};
    RuntimeSessionId sessionId_{};
    std::uint64_t generation_{0};
    std::uint64_t transitionSequence_{0};
    std::uint32_t controlClients_{0};
    bool mutationInProgress_{false};
    std::vector<SeatConfig> profile_;
    std::vector<SeatRuntimeState> seats_;
    std::vector<std::shared_ptr<IRuntimeBackend>> backends_;
    std::size_t preparedBackendCount_{0};
    std::size_t startedBackendCount_{0};
    std::optional<RuntimeTransition> lastTransition_;
    std::deque<RuntimeTransition> transitionEvents_;
    std::string diagnostic_;
};

} // namespace hydra::runtime
