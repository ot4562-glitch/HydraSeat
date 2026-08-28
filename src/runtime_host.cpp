#include "hydra/runtime_host.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <unordered_set>
#include <utility>

namespace hydra::runtime {
namespace {

constexpr std::size_t kMaximumDiagnosticBytes = 2048;

void appendDiagnostic(std::string& destination, std::string_view value) {
    if (value.empty() || destination.size() >= kMaximumDiagnosticBytes) return;
    if (!destination.empty()) destination.append("; ");
    const auto remaining = kMaximumDiagnosticBytes - destination.size();
    destination.append(value.substr(0, remaining));
}

class UnavailableSeatGameFactory final : public ISeatGameInstanceFactory {
public:
    std::unique_ptr<ISeatGameInstance> create(SeatId, std::string& error) override {
        error = "no validated game launch plan is installed for this host";
        return {};
    }
};

} // namespace

RuntimeHost::RuntimeHost(
    std::vector<std::shared_ptr<IRuntimeBackend>> backends,
    std::shared_ptr<ISeatGameInstanceFactory> seatGameFactory)
    : backends_(std::move(backends)),
      seatGameFactory_(seatGameFactory ? std::move(seatGameFactory)
                                       : std::make_shared<UnavailableSeatGameFactory>()) {
    backends_.erase(
        std::remove(backends_.begin(), backends_.end(), nullptr),
        backends_.end());
    hostPhase_ = HostLifecyclePhase::Running;
    diagnostic_ = "host is running and session is idle";
}

RuntimeHost::~RuntimeHost() {
    std::lock_guard mutationLock(mutationMutex_);
    std::size_t rollbackCount = 0;
    bool sessionWasIdle = false;
    {
        std::lock_guard lock(mutex_);
        sessionWasIdle = sessionPhase_ == SeatSessionPhase::Idle;
        mutationInProgress_ = true;
        rollbackCount = preparedBackendCount_;
        if (!sessionWasIdle) {
            setSessionPhaseLocked(SeatSessionPhase::RollingBack,
                                  "host destruction requested rollback");
        }
    }

    std::string diagnostic;
    bool seatsCleaned = true;
    if (seatGameLifecycle_) {
        const auto seatResult = seatGameLifecycle_->shutdown();
        seatsCleaned = seatResult.succeeded();
        if (!seatsCleaned) appendDiagnostic(diagnostic, seatResult.diagnostic);
    }
    const bool rolledBack = rollbackBackends(rollbackCount, diagnostic);
    std::lock_guard lock(mutex_);
    if (!seatsCleaned || !rolledBack) {
        setSessionPhaseLocked(SeatSessionPhase::RecoveryRequired, diagnostic);
    } else {
        preparedBackendCount_ = 0;
        startedBackendCount_ = 0;
        setSessionPhaseLocked(SeatSessionPhase::Idle,
                              sessionWasIdle
                                  ? "host destruction Seat cleanup verified"
                                  : "host destruction rollback verified");
    }
    mutationInProgress_ = false;
    hostPhase_ = HostLifecyclePhase::Stopped;
}

HostRuntimeSnapshot RuntimeHost::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshotLocked();
}

std::vector<RuntimeTransition> RuntimeHost::transitionEventsAfter(
    std::uint64_t sequence, std::size_t maxEvents, bool& overflow) const {
    std::lock_guard lock(mutex_);
    overflow = false;
    maxEvents = std::min<std::size_t>(maxEvents, 128u);
    std::vector<RuntimeTransition> result;
    if (maxEvents == 0u || transitionEvents_.empty()) return result;

    const auto firstSequence = transitionEvents_.front().sequence;
    if (sequence > transitionSequence_ ||
        (firstSequence > 1u && sequence < firstSequence - 1u)) {
        overflow = true;
        return result;
    }
    result.reserve(std::min(maxEvents, transitionEvents_.size()));
    for (const auto& event : transitionEvents_) {
        if (event.sequence <= sequence) continue;
        if (result.size() == maxEvents) {
            overflow = true;
            break;
        }
        result.push_back(event);
    }
    return result;
}

RuntimeCommandResult RuntimeHost::loadProfile(std::vector<SeatConfig> seats,
                                               std::uint64_t correlationId) {
    return loadProfile(std::move(seats), 1, correlationId);
}

RuntimeCommandResult RuntimeHost::loadProfile(std::vector<SeatConfig> seats, SeatId managementSeatId,
                                              std::uint64_t correlationId) {
    std::unique_lock mutationLock(mutationMutex_, std::try_to_lock);
    if (!mutationLock.owns_lock()) return busyResult(RuntimeCommand::LoadProfile, correlationId);
    std::lock_guard lock(mutex_);
    const auto from = sessionPhase_;
    mutationInProgress_ = true;
    if (hostPhase_ != HostLifecyclePhase::Running || sessionPhase_ != SeatSessionPhase::Idle) {
        return finishLocked(RuntimeCommand::LoadProfile, from,
                            RuntimeResultCode::InvalidState,
                            "profiles may be loaded only while the host is running and idle",
                            correlationId);
    }
    if (seatGameLifecycle_) {
        const auto gameStates = seatGameLifecycle_->snapshot();
        const bool anyBoundOrActive = std::any_of(
            gameStates.begin(), gameStates.end(), [](const SeatGameState& state) {
                return state.phase != SeatGamePhase::Idle || state.binding.has_value();
            });
        if (anyBoundOrActive) {
            return finishLocked(RuntimeCommand::LoadProfile, from,
                                RuntimeResultCode::InvalidState,
                                "profile replacement requires every Seat game to be Idle",
                                correlationId);
        }
    }
    std::string error;
    if (!validateProfile(seats, error)) {
        return finishLocked(RuntimeCommand::LoadProfile, from,
                            RuntimeResultCode::InvalidProfile, std::move(error),
                            correlationId);
    }
    if (managementSeatId == 0 ||
        std::none_of(seats.begin(), seats.end(), [managementSeatId](const SeatConfig& seat) {
            return seat.seatId == managementSeatId;
        })) {
        return finishLocked(RuntimeCommand::LoadProfile, from,
                            RuntimeResultCode::InvalidProfile,
                            "Management Seat must reference a configured Seat",
                            correlationId);
    }
    managementSeatId_ = managementSeatId;
    profile_ = std::move(seats);
    seats_.clear();
    std::vector<SeatId> activeSeatIds;
    for (const auto& seat : profile_) {
        if (seat.active) {
            seats_.push_back({seat.seatId, SeatSessionPhase::Idle, {}});
            activeSeatIds.push_back(seat.seatId);
        }
    }
    seatGameLifecycle_ = std::make_unique<SeatGameLifecycle>(activeSeatIds,
                                                             seatGameFactory_);
    return finishLocked(RuntimeCommand::LoadProfile, from, RuntimeResultCode::Ok,
                        "validated profile loaded without activating backends",
                        correlationId);
}

RuntimeCommandResult RuntimeHost::plan(std::uint64_t correlationId) {
    std::unique_lock mutationLock(mutationMutex_, std::try_to_lock);
    if (!mutationLock.owns_lock()) return busyResult(RuntimeCommand::Plan, correlationId);
    std::lock_guard lock(mutex_);
    const auto from = sessionPhase_;
    mutationInProgress_ = true;
    if (hostPhase_ != HostLifecyclePhase::Running || profile_.empty()) {
        return finishLocked(RuntimeCommand::Plan, from,
                            RuntimeResultCode::InvalidState,
                            "a validated profile and running host are required",
                            correlationId);
    }
    if (sessionPhase_ == SeatSessionPhase::Planning) {
        return finishLocked(RuntimeCommand::Plan, from,
                            RuntimeResultCode::AlreadySatisfied,
                            "session is already planned", correlationId);
    }
    if (sessionPhase_ != SeatSessionPhase::Idle) {
        return finishLocked(RuntimeCommand::Plan, from,
                            RuntimeResultCode::InvalidState,
                            "plan requires an idle session", correlationId);
    }
    ++generation_;
    sessionId_ = newSessionId(generation_);
    preparedBackendCount_ = 0;
    startedBackendCount_ = 0;
    setSessionPhaseLocked(SeatSessionPhase::Planning, "plan compiled");
    return finishLocked(RuntimeCommand::Plan, from, RuntimeResultCode::Ok,
                        "immutable session plan created", correlationId);
}

RuntimeCommandResult RuntimeHost::prepare(std::uint64_t correlationId) {
    std::unique_lock mutationLock(mutationMutex_, std::try_to_lock);
    if (!mutationLock.owns_lock()) return busyResult(RuntimeCommand::Prepare, correlationId);

    SeatSessionPhase from = SeatSessionPhase::Idle;
    RuntimeSessionId sessionId;
    std::vector<SeatConfig> profile;
    {
        std::lock_guard lock(mutex_);
        from = sessionPhase_;
        mutationInProgress_ = true;
        if (sessionPhase_ == SeatSessionPhase::Prepared) {
            return finishLocked(RuntimeCommand::Prepare, from,
                                RuntimeResultCode::AlreadySatisfied,
                                "session is already prepared", correlationId);
        }
        if (sessionPhase_ != SeatSessionPhase::Planning) {
            return finishLocked(RuntimeCommand::Prepare, from,
                                RuntimeResultCode::InvalidState,
                                "prepare requires a planned session", correlationId);
        }
        preparedBackendCount_ = 0;
        startedBackendCount_ = 0;
        sessionId = sessionId_;
        profile = profile_;
    }

    for (std::size_t index = 0; index < backends_.size(); ++index) {
        const auto& backend = backends_[index];
        std::string error;
        if (!backend->prepare(sessionId, profile, error)) {
            // A backend may have mutated before reporting failure. Include the
            // failing backend in the reverse rollback/verification set.
            const auto rollbackCount = index + 1u;
            std::string diagnostic = std::string(backend->name()) + " prepare failed";
            appendDiagnostic(diagnostic, error);
            {
                std::lock_guard lock(mutex_);
                preparedBackendCount_ = rollbackCount;
                setSessionPhaseLocked(SeatSessionPhase::RollingBack, diagnostic);
            }
            std::string rollbackDiagnostic;
            const bool rolledBack = rollbackBackends(rollbackCount, rollbackDiagnostic);
            std::lock_guard lock(mutex_);
            if (!rolledBack) {
                appendDiagnostic(diagnostic, rollbackDiagnostic);
                setSessionPhaseLocked(SeatSessionPhase::RecoveryRequired, diagnostic);
                return finishLocked(RuntimeCommand::Prepare, from,
                                    RuntimeResultCode::RollbackFailure,
                                    std::move(diagnostic), correlationId);
            }
            preparedBackendCount_ = 0;
            startedBackendCount_ = 0;
            setSessionPhaseLocked(SeatSessionPhase::Idle,
                                  "prepare failure rolled back safely");
            return finishLocked(RuntimeCommand::Prepare, from,
                                RuntimeResultCode::BackendFailure,
                                std::move(diagnostic), correlationId);
        }
        std::lock_guard lock(mutex_);
        preparedBackendCount_ = index + 1u;
    }

    std::lock_guard lock(mutex_);
    setSessionPhaseLocked(SeatSessionPhase::Prepared, "all backends prepared");
    return finishLocked(RuntimeCommand::Prepare, from, RuntimeResultCode::Ok,
                        "replacement and recovery paths are prepared", correlationId);
}

RuntimeCommandResult RuntimeHost::start(std::uint64_t correlationId) {
    std::unique_lock mutationLock(mutationMutex_, std::try_to_lock);
    if (!mutationLock.owns_lock()) return busyResult(RuntimeCommand::Start, correlationId);

    SeatSessionPhase from = SeatSessionPhase::Idle;
    std::size_t backendCount = 0;
    {
        std::lock_guard lock(mutex_);
        from = sessionPhase_;
        mutationInProgress_ = true;
        if (sessionPhase_ == SeatSessionPhase::Active) {
            return finishLocked(RuntimeCommand::Start, from,
                                RuntimeResultCode::AlreadySatisfied,
                                "session is already active", correlationId);
        }
        if (sessionPhase_ != SeatSessionPhase::Prepared) {
            return finishLocked(RuntimeCommand::Start, from,
                                RuntimeResultCode::InvalidState,
                                "start requires a prepared session", correlationId);
        }
        setSessionPhaseLocked(SeatSessionPhase::Starting, "backend activation started");
        startedBackendCount_ = 0;
        backendCount = preparedBackendCount_;
    }

    for (std::size_t index = 0; index < backendCount; ++index) {
        std::string error;
        if (!backends_[index]->start(error)) {
            std::string diagnostic = std::string(backends_[index]->name()) + " start failed";
            appendDiagnostic(diagnostic, error);
            {
                std::lock_guard lock(mutex_);
                setSessionPhaseLocked(SeatSessionPhase::RollingBack, diagnostic);
            }
            std::string rollbackDiagnostic;
            const bool rolledBack = rollbackBackends(backendCount, rollbackDiagnostic);
            std::lock_guard lock(mutex_);
            if (!rolledBack) {
                appendDiagnostic(diagnostic, rollbackDiagnostic);
                setSessionPhaseLocked(SeatSessionPhase::RecoveryRequired, diagnostic);
                return finishLocked(RuntimeCommand::Start, from,
                                    RuntimeResultCode::RollbackFailure,
                                    std::move(diagnostic), correlationId);
            }
            preparedBackendCount_ = 0;
            startedBackendCount_ = 0;
            setSessionPhaseLocked(SeatSessionPhase::Idle,
                                  "start failure rolled back safely");
            return finishLocked(RuntimeCommand::Start, from,
                                RuntimeResultCode::BackendFailure,
                                std::move(diagnostic), correlationId);
        }
        std::lock_guard lock(mutex_);
        startedBackendCount_ = index + 1u;
    }

    std::lock_guard lock(mutex_);
    setSessionPhaseLocked(SeatSessionPhase::Active, "session activation verified");
    return finishLocked(RuntimeCommand::Start, from, RuntimeResultCode::Ok,
                        "session is active", correlationId);
}

RuntimeCommandResult RuntimeHost::stopAndReturnToWindows(std::uint64_t correlationId) {
    return stopForCommand(RuntimeCommand::StopAndReturnToWindows, correlationId, false);
}

RuntimeCommandResult RuntimeHost::beginReconfigure(std::uint64_t correlationId) {
    return stopForCommand(RuntimeCommand::BeginReconfigure, correlationId, true);
}

RuntimeCommandResult RuntimeHost::stopForCommand(RuntimeCommand command,
                                                 std::uint64_t correlationId,
                                                 bool reconfigure) {
    std::unique_lock mutationLock(mutationMutex_, std::try_to_lock);
    if (!mutationLock.owns_lock()) {
        return busyResult(command, correlationId);
    }

    SeatSessionPhase from = SeatSessionPhase::Idle;
    std::size_t rollbackCount = 0;
    SeatGameLifecycle* seatLifecycle = nullptr;
    bool seatGamesWereActive = false;
    {
        std::lock_guard lock(mutex_);
        from = sessionPhase_;
        mutationInProgress_ = true;
        seatLifecycle = seatGameLifecycle_.get();
        if (seatLifecycle) {
            const auto states = seatLifecycle->snapshot();
            seatGamesWereActive = std::any_of(
                states.begin(), states.end(), [](const SeatGameState& state) {
                    return state.phase != SeatGamePhase::Idle || state.binding.has_value();
                });
        }
        if (sessionPhase_ == SeatSessionPhase::Idle && !seatGamesWereActive) {
            return finishLocked(command, from,
                                RuntimeResultCode::AlreadySatisfied,
                                reconfigure
                                    ? "session is already idle; configuration editor may open"
                                    : "ordinary Windows state is already verified",
                                correlationId);
        }
        if (sessionPhase_ == SeatSessionPhase::RecoveryRequired) {
            return finishLocked(command, from,
                                RuntimeResultCode::RecoveryRequired,
                                "reset is required because prior rollback was unverified",
                                correlationId);
        }
        rollbackCount = preparedBackendCount_;
        setSessionPhaseLocked(SeatSessionPhase::Stopping,
                              reconfigure ? "session stopped for reconfiguration"
                                          : "new session work stopped");
        setSessionPhaseLocked(SeatSessionPhase::RollingBack, "reverse rollback started");
    }

    std::string diagnostic;
    bool seatGamesStopped = true;
    if (seatLifecycle && seatGamesWereActive) {
        const auto seatResult = seatLifecycle->emergencyStopAll(correlationId);
        seatGamesStopped = seatResult.succeeded();
        if (!seatGamesStopped) appendDiagnostic(diagnostic, seatResult.diagnostic);
    }
    const bool rolledBack = rollbackBackends(rollbackCount, diagnostic);
    std::lock_guard lock(mutex_);
    if (!rolledBack || !seatGamesStopped) {
        setSessionPhaseLocked(SeatSessionPhase::RecoveryRequired, diagnostic);
        return finishLocked(command, from,
                            seatGamesStopped ? RuntimeResultCode::RollbackFailure
                                             : RuntimeResultCode::RecoveryRequired,
                            std::move(diagnostic), correlationId);
    }
    preparedBackendCount_ = 0;
    startedBackendCount_ = 0;
    setSessionPhaseLocked(
        SeatSessionPhase::Idle,
        reconfigure ? "rollback verified; configuration editor may open"
                    : "ordinary Windows postconditions verified");
    return finishLocked(command, from,
                        RuntimeResultCode::Ok,
                        reconfigure
                            ? "session stopped safely; configuration editor may open"
                            : "session stopped; host remains running and idle",
                        correlationId);
}

RuntimeCommandResult RuntimeHost::reset(std::uint64_t correlationId) {
    std::unique_lock mutationLock(mutationMutex_, std::try_to_lock);
    if (!mutationLock.owns_lock()) return busyResult(RuntimeCommand::Reset, correlationId);

    SeatSessionPhase from = SeatSessionPhase::Idle;
    std::size_t rollbackCount = 0;
    SeatGameLifecycle* seatLifecycle = nullptr;
    {
        std::lock_guard lock(mutex_);
        from = sessionPhase_;
        mutationInProgress_ = true;
        rollbackCount = preparedBackendCount_;
        seatLifecycle = seatGameLifecycle_.get();
        setSessionPhaseLocked(SeatSessionPhase::RollingBack, "verified reset started");
    }

    std::string diagnostic;
    bool seatGamesStopped = true;
    if (seatLifecycle) {
        const auto seatResult = seatLifecycle->emergencyStopAll(correlationId);
        seatGamesStopped = seatResult.succeeded();
        if (!seatGamesStopped) appendDiagnostic(diagnostic, seatResult.diagnostic);
    }
    const bool rolledBack = rollbackBackends(rollbackCount, diagnostic);
    std::lock_guard lock(mutex_);
    if (!rolledBack || !seatGamesStopped) {
        setSessionPhaseLocked(SeatSessionPhase::RecoveryRequired, diagnostic);
        return finishLocked(RuntimeCommand::Reset, from,
                            seatGamesStopped ? RuntimeResultCode::RollbackFailure
                                             : RuntimeResultCode::RecoveryRequired,
                            std::move(diagnostic), correlationId);
    }
    preparedBackendCount_ = 0;
    startedBackendCount_ = 0;
    setSessionPhaseLocked(SeatSessionPhase::Idle, "reset postconditions verified");
    return finishLocked(RuntimeCommand::Reset, from,
                        from == SeatSessionPhase::Idle
                            ? RuntimeResultCode::AlreadySatisfied
                            : RuntimeResultCode::Ok,
                        "verified safe state restored", correlationId);
}

RuntimeCommandResult RuntimeHost::exitHostWhenIdle(std::uint64_t correlationId) {
    std::unique_lock mutationLock(mutationMutex_, std::try_to_lock);
    if (!mutationLock.owns_lock()) {
        return busyResult(RuntimeCommand::ExitHostWhenIdle, correlationId);
    }
    std::lock_guard lock(mutex_);
    const auto from = sessionPhase_;
    mutationInProgress_ = true;
    bool seatGameActive = false;
    if (seatGameLifecycle_) {
        const auto states = seatGameLifecycle_->snapshot();
        seatGameActive = std::any_of(states.begin(), states.end(),
                                     [](const SeatGameState& state) {
            return state.phase != SeatGamePhase::Idle || state.binding.has_value();
        });
    }
    if (sessionPhase_ != SeatSessionPhase::Idle || seatGameActive) {
        return finishLocked(RuntimeCommand::ExitHostWhenIdle, from,
                            RuntimeResultCode::InvalidState,
                            "active/prepared session or Seat game must stop before host exit",
                            correlationId);
    }
    if (hostPhase_ == HostLifecyclePhase::ExitRequested) {
        return finishLocked(RuntimeCommand::ExitHostWhenIdle, from,
                            RuntimeResultCode::AlreadySatisfied,
                            "host exit is already requested", correlationId);
    }
    hostPhase_ = HostLifecyclePhase::ExitRequested;
    return finishLocked(RuntimeCommand::ExitHostWhenIdle, from,
                        RuntimeResultCode::Ok, "idle host exit requested",
                        correlationId);
}

RuntimeCommandResult RuntimeHost::markDegraded(std::string diagnostic,
                                               std::uint64_t correlationId) {
    std::unique_lock mutationLock(mutationMutex_, std::try_to_lock);
    if (!mutationLock.owns_lock()) return busyResult(RuntimeCommand::MarkDegraded, correlationId);
    std::lock_guard lock(mutex_);
    const auto from = sessionPhase_;
    mutationInProgress_ = true;
    if (sessionPhase_ != SeatSessionPhase::Active &&
        sessionPhase_ != SeatSessionPhase::Degraded) {
        return finishLocked(RuntimeCommand::MarkDegraded, from,
                            RuntimeResultCode::InvalidState,
                            "only an active session may enter degraded state",
                            correlationId);
    }
    setSessionPhaseLocked(SeatSessionPhase::Degraded, diagnostic);
    return finishLocked(RuntimeCommand::MarkDegraded, from,
                        from == SeatSessionPhase::Degraded
                            ? RuntimeResultCode::AlreadySatisfied
                            : RuntimeResultCode::Ok,
                        std::move(diagnostic), correlationId);
}

SeatGameCommandResult RuntimeHost::assignSeatGame(
    SeatId seatId, SeatGameBinding binding, std::uint64_t correlationId) {
    std::unique_lock mutationLock(mutationMutex_, std::try_to_lock);
    if (!mutationLock.owns_lock()) {
        return {SeatGameResultCode::Busy, {}, false,
                "another runtime mutation is in progress"};
    }
    std::lock_guard lock(mutex_);
    if (!seatGameLifecycle_) {
        return {SeatGameResultCode::InvalidState, {}, false,
                "a validated active-Seat profile is required"};
    }
    auto result = seatGameLifecycle_->assign(seatId, std::move(binding), correlationId);
    recordSeatTransitionLocked(RuntimeCommand::AssignSeatGame, seatId, result,
                               correlationId);
    return result;
}

SeatGameCommandResult RuntimeHost::startSeatGame(SeatId seatId,
                                                  std::uint64_t correlationId) {
    std::unique_lock mutationLock(mutationMutex_, std::try_to_lock);
    if (!mutationLock.owns_lock()) {
        return {SeatGameResultCode::Busy, {}, false,
                "another runtime mutation is in progress"};
    }
    std::lock_guard lock(mutex_);
    if (!seatGameLifecycle_) {
        return {SeatGameResultCode::InvalidState, {}, false,
                "a validated active-Seat profile is required"};
    }
    if (sessionPhase_ != SeatSessionPhase::Active &&
        sessionPhase_ != SeatSessionPhase::Degraded) {
        return {SeatGameResultCode::InvalidState,
                seatGameLifecycle_->snapshot(),
                seatGameLifecycle_->wholeMachineReturnRequested(),
                "Seat game start requires an active prepared whole-machine runtime"};
    }
    auto result = seatGameLifecycle_->start(seatId, correlationId);
    recordSeatTransitionLocked(RuntimeCommand::StartSeatGame, seatId, result,
                               correlationId);
    return result;
}

SeatGameCommandResult RuntimeHost::stopSeatGame(SeatId seatId,
                                                 std::uint64_t correlationId) {
    std::unique_lock mutationLock(mutationMutex_, std::try_to_lock);
    if (!mutationLock.owns_lock()) {
        return {SeatGameResultCode::Busy, {}, false,
                "another runtime mutation is in progress"};
    }
    std::lock_guard lock(mutex_);
    if (!seatGameLifecycle_) {
        return {SeatGameResultCode::InvalidState, {}, false,
                "a validated active-Seat profile is required"};
    }
    auto result = seatGameLifecycle_->stop(seatId, correlationId);
    recordSeatTransitionLocked(RuntimeCommand::StopSeatGame, seatId, result,
                               correlationId);
    return result;
}

SeatGameCommandResult RuntimeHost::reconcileSeatGames(std::uint64_t correlationId) {
    std::unique_lock mutationLock(mutationMutex_, std::try_to_lock);
    if (!mutationLock.owns_lock()) {
        return {SeatGameResultCode::Busy, {}, false,
                "another runtime mutation is in progress"};
    }
    std::lock_guard lock(mutex_);
    if (!seatGameLifecycle_) {
        return {SeatGameResultCode::InvalidState, {}, false,
                "a validated active-Seat profile is required"};
    }
    auto result = seatGameLifecycle_->reconcile(correlationId);
    recordSeatTransitionLocked(RuntimeCommand::ReconcileSeatGames, 0, result,
                               correlationId);
    return result;
}

SeatGameCommandResult RuntimeHost::observeSeatGameExit(
    SeatId seatId, bool cleanExit, std::string diagnostic,
    std::uint64_t correlationId) {
    std::unique_lock mutationLock(mutationMutex_, std::try_to_lock);
    if (!mutationLock.owns_lock()) {
        return {SeatGameResultCode::Busy, {}, false,
                "another runtime mutation is in progress"};
    }
    std::lock_guard lock(mutex_);
    if (!seatGameLifecycle_) {
        return {SeatGameResultCode::InvalidState, {}, false,
                "a validated active-Seat profile is required"};
    }
    auto result = seatGameLifecycle_->observeTargetExit(
        seatId, cleanExit, std::move(diagnostic), correlationId);
    recordSeatTransitionLocked(RuntimeCommand::ObserveSeatGameExit, seatId,
                               result, correlationId);
    return result;
}

void RuntimeHost::controlClientConnected() {
    std::lock_guard lock(mutex_);
    if (controlClients_ != std::numeric_limits<std::uint32_t>::max()) ++controlClients_;
}

void RuntimeHost::controlClientDisconnected() {
    std::lock_guard lock(mutex_);
    if (controlClients_ != 0) --controlClients_;
}

RuntimeCommandResult RuntimeHost::busyResult(RuntimeCommand,
                                             std::uint64_t) const {
    std::lock_guard lock(mutex_);
    RuntimeCommandResult result;
    result.code = RuntimeResultCode::Busy;
    result.snapshot = snapshotLocked();
    // Owning the mutation lock is authoritative proof that a writer exists even
    // if the writer has not yet published its first intermediate state update.
    result.snapshot.mutationInProgress = true;
    result.diagnostic = "another runtime mutation is in progress";
    return result;
}

RuntimeCommandResult RuntimeHost::finishLocked(RuntimeCommand command,
                                               SeatSessionPhase from,
                                               RuntimeResultCode code,
                                               std::string diagnostic,
                                               std::uint64_t correlationId) {
    if (diagnostic.size() > kMaximumDiagnosticBytes) {
        diagnostic.resize(kMaximumDiagnosticBytes);
    }
    mutationInProgress_ = false;
    diagnostic_ = diagnostic;
    RuntimeTransition transition;
    transition.sequence = ++transitionSequence_;
    transition.correlationId = correlationId;
    transition.command = command;
    transition.from = from;
    transition.to = sessionPhase_;
    transition.result = code;
    transition.diagnostic = diagnostic;
    transitionEvents_.push_back(transition);
    if (transitionEvents_.size() > 128u) transitionEvents_.pop_front();
    lastTransition_ = std::move(transition);
    RuntimeCommandResult result;
    result.code = code;
    result.snapshot = snapshotLocked();
    result.diagnostic = std::move(diagnostic);
    return result;
}

void RuntimeHost::recordSeatTransitionLocked(
    RuntimeCommand command, SeatId seatId, const SeatGameCommandResult& result,
    std::uint64_t correlationId) {
    RuntimeTransition transition;
    transition.sequence = ++transitionSequence_;
    transition.correlationId = correlationId;
    transition.command = command;
    transition.from = sessionPhase_;
    transition.to = sessionPhase_;
    transition.seatId = seatId;
    switch (result.code) {
        case SeatGameResultCode::Ok: transition.result = RuntimeResultCode::Ok; break;
        case SeatGameResultCode::AlreadySatisfied:
            transition.result = RuntimeResultCode::AlreadySatisfied; break;
        case SeatGameResultCode::Busy: transition.result = RuntimeResultCode::Busy; break;
        case SeatGameResultCode::InvalidSeat:
        case SeatGameResultCode::InvalidState:
        case SeatGameResultCode::InvalidBinding:
        case SeatGameResultCode::DuplicateCorrelation:
        case SeatGameResultCode::V1SeatLimitExceeded:
            transition.result = RuntimeResultCode::InvalidState; break;
        case SeatGameResultCode::BackendFailure:
            transition.result = RuntimeResultCode::BackendFailure; break;
        case SeatGameResultCode::RecoveryRequired:
            transition.result = RuntimeResultCode::RecoveryRequired; break;
    }
    transition.diagnostic = result.diagnostic.substr(0, kMaximumDiagnosticBytes);
    transitionEvents_.push_back(transition);
    if (transitionEvents_.size() > 128u) transitionEvents_.pop_front();
    lastTransition_ = std::move(transition);
}

void RuntimeHost::setSessionPhaseLocked(SeatSessionPhase phase,
                                        std::string_view diagnostic) {
    sessionPhase_ = phase;
    const auto bounded = diagnostic.substr(0, kMaximumDiagnosticBytes);
    for (auto& seat : seats_) {
        seat.phase = phase;
        seat.diagnostic.assign(bounded);
    }
    diagnostic_.assign(bounded);
}

bool RuntimeHost::rollbackBackends(std::size_t rollbackCount,
                                   std::string& diagnostic) noexcept {
    bool success = true;
    rollbackCount = std::min(rollbackCount, backends_.size());
    for (std::size_t count = rollbackCount; count > 0; --count) {
        std::string error;
        if (!backends_[count - 1u]->rollback(error)) {
            success = false;
            appendDiagnostic(diagnostic,
                             std::string(backends_[count - 1u]->name()) +
                                 " rollback failed: " + error);
        }
    }
    for (std::size_t index = 0; index < rollbackCount; ++index) {
        std::string error;
        if (!backends_[index]->verifySafe(error)) {
            success = false;
            appendDiagnostic(diagnostic,
                             std::string(backends_[index]->name()) +
                                 " safe-state verification failed: " + error);
        }
    }
    if (success && diagnostic.empty()) diagnostic = "all rollback postconditions verified";
    return success;
}

HostRuntimeSnapshot RuntimeHost::snapshotLocked() const {
    HostRuntimeSnapshot snapshot;
    snapshot.hostPhase = hostPhase_;
    snapshot.sessionPhase = sessionPhase_;
    snapshot.sessionId = sessionId_;
    snapshot.generation = generation_;
    snapshot.transitionSequence = transitionSequence_;
    snapshot.connectedControlClients = controlClients_;
    snapshot.managementSeatId = managementSeatId_;
    snapshot.profileLoaded = !profile_.empty();
    snapshot.mutationInProgress = mutationInProgress_;
    snapshot.seats = seats_;
    if (seatGameLifecycle_) {
        snapshot.seatGames = seatGameLifecycle_->snapshot();
        snapshot.wholeMachineReturnRequested =
            seatGameLifecycle_->wholeMachineReturnRequested();
    }
    snapshot.configuredSeats = profile_;
    snapshot.lastTransition = lastTransition_;
    snapshot.diagnostic = diagnostic_;
    return snapshot;
}

bool RuntimeHost::validateProfile(std::span<const SeatConfig> seats,
                                  std::string& error) {
    if (seats.empty() || seats.size() > 64u) {
        error = "profile must contain between 1 and 64 Seats";
        return false;
    }
    std::unordered_set<SeatId> ids;
    std::size_t activeCount = 0;
    for (const auto& seat : seats) {
        if (seat.seatId == 0 || !ids.insert(seat.seatId).second) {
            error = "Seat identifiers must be nonzero and unique";
            return false;
        }
        if (seat.active) ++activeCount;
        if (seat.primaryDisplayId &&
            std::find(seat.displayIds.begin(), seat.displayIds.end(),
                      *seat.primaryDisplayId) == seat.displayIds.end()) {
            error = "a Seat primary display must belong to that Seat display set";
            return false;
        }
    }
    if (activeCount == 0u) {
        error = "profile must contain at least one active Seat";
        return false;
    }
    if (activeCount > kV1MaximumActiveSeats) {
        error = "HydraSeat v1 rejects profiles with more than two active Seats";
        return false;
    }
    return true;
}

RuntimeSessionId RuntimeHost::newSessionId(std::uint64_t generation) {
    static std::atomic<std::uint64_t> counter{1};
    const auto now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto unique = counter.fetch_add(1, std::memory_order_relaxed);
    RuntimeSessionId id;
    const auto first = now ^ (generation * 0x9e3779b97f4a7c15ull);
    const auto second = unique ^ (first + 0xd1b54a32d192ed03ull);
    for (std::size_t index = 0; index < 8u; ++index) {
        id.bytes[index] = static_cast<std::uint8_t>(first >> (index * 8u));
        id.bytes[index + 8u] = static_cast<std::uint8_t>(second >> (index * 8u));
    }
    if (id.empty()) id.bytes[0] = 1;
    return id;
}

} // namespace hydra::runtime
