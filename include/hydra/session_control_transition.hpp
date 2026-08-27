#pragma once

#include "hydra/host_protocol.hpp"
#include "hydra/management_seat.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hydra::control {

enum class SessionControlUiPhase : std::uint8_t {
    HostUnknown = 0,
    Viewing = 1,
    StopPending = 2,
    ReconfigureStopPending = 3,
    EditorReady = 4,
    ProfileApplyPending = 5,
    PlanPending = 6,
    PlanReady = 7,
    StartPending = 8,
    RecoveryRequired = 9,
};

enum class SessionControlIntent : std::uint8_t {
    StopAndReturnToWindows = 0,
    Reconfigure = 1,
    ExitHostWhenIdle = 2,
};

struct SessionControlRequest {
    bool send{false};
    bool alreadyPending{false};
    hostipc::MessageType message{hostipc::MessageType::GetSnapshot};
    std::string diagnostic;
};

struct ReconfigureDraft {
    SeatId savedManagementSeatId{1};
    std::vector<SeatConfig> savedProfile;
    SeatId draftManagementSeatId{1};
    std::vector<SeatConfig> draftProfile;
    bool dirty{false};
    bool committed{false};
};

class SessionControlTransition {
public:
    void observeSnapshot(const runtime::HostRuntimeSnapshot& snapshot);
    void markHostDisconnected(std::string diagnostic = "host disconnected");

    SessionControlRequest requestStop(const GlobalControlPermission& permission);
    SessionControlRequest requestReconfigure(const GlobalControlPermission& permission);
    SessionControlRequest requestExitHost(const GlobalControlPermission& permission);

    bool observeCommandResult(SessionControlIntent intent,
                              const runtime::RuntimeCommandResult& result);

    bool beginDraft(SeatId managementSeatId, std::vector<SeatConfig> profile,
                    std::string* error = nullptr);
    bool replaceDraft(SeatId managementSeatId, std::vector<SeatConfig> profile,
                      std::string* error = nullptr);
    void cancelDraft() noexcept;

    std::optional<hostipc::ProfilePayload> requestApplyDraft(
        const GlobalControlPermission& permission, std::string* error = nullptr);
    bool observeProfileApplied(const runtime::RuntimeCommandResult& result,
                               std::string* error = nullptr);

    SessionControlRequest requestPlanAfterSave(const GlobalControlPermission& permission);
    bool observePlanResult(const runtime::RuntimeCommandResult& result);
    SessionControlRequest requestStartAfterPlan(const GlobalControlPermission& permission);
    bool observeStartResult(const runtime::RuntimeCommandResult& result);

    SessionControlUiPhase phase() const noexcept { return phase_; }
    bool configurationEditingAllowed() const noexcept {
        return phase_ == SessionControlUiPhase::EditorReady;
    }
    bool hostStateKnown() const noexcept { return snapshot_.has_value(); }
    const std::optional<runtime::HostRuntimeSnapshot>& snapshot() const noexcept {
        return snapshot_;
    }
    const std::optional<ReconfigureDraft>& draft() const noexcept { return draft_; }
    std::uint64_t lastCommittedPlanGeneration() const noexcept {
        return lastCommittedPlanGeneration_;
    }
    const std::string& diagnostic() const noexcept { return diagnostic_; }

private:
    static bool safeInactive(const runtime::HostRuntimeSnapshot& snapshot) noexcept;
    static bool transitionInProgress(const runtime::HostRuntimeSnapshot& snapshot) noexcept;
    static bool permissionAllowed(const GlobalControlPermission& permission,
                                  const runtime::HostRuntimeSnapshot& snapshot) noexcept;
    SessionControlRequest requestMutation(hostipc::MessageType message,
                                          SessionControlUiPhase pendingPhase,
                                          const GlobalControlPermission& permission,
                                          bool requireIdle);

    SessionControlUiPhase phase_{SessionControlUiPhase::HostUnknown};
    std::optional<runtime::HostRuntimeSnapshot> snapshot_;
    std::optional<ReconfigureDraft> draft_;
    std::uint64_t generationBeforeReconfigure_{0};
    std::uint64_t lastCommittedPlanGeneration_{0};
    std::string diagnostic_{"host state is unknown"};
};

} // namespace hydra::control
