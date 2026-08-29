#pragma once

#include "hydra/launcher_ui_model.hpp"
#include "hydra/seat_host_client.hpp"
#include "hydra/seat_launcher_model.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::seatui {

struct SeatLaunchContext {
    profile::SeatConfigDocument seats;
    catalog::LocalGameCatalog library;
    profile::PlayerProfileDocument players;
    std::vector<launcher_ui::PlayerPresentation> playerPresentation;
    profile::TwoPlayerSetupDocument setups;
    std::vector<plan::ProviderAdapterBinding> providers;
    std::vector<plan::GameRuntimeRequirement> requirements;
};

enum class SeatLaunchResult : std::uint8_t {
    Success = 0,
    InvalidContext = 1,
    InvalidSnapshot = 2,
    SeatNotIdle = 3,
    SelectionRejected = 4,
    PreflightBlocked = 5,
    SnapshotChanged = 6,
    PlanInstallFailed = 7,
    AssignFailed = 8,
    StartFailed = 9,
    ResultUncertain = 10,
    OtherSeatChanged = 11,
};

struct SeatLaunchDiagnostic {
    SeatLaunchResult result{SeatLaunchResult::Success};
    std::string message;

    bool succeeded() const noexcept { return result == SeatLaunchResult::Success; }
};

struct SeatLaunchPreview {
    launcher_ui::PlayPreview sharedPreview;
    std::optional<plan::SeatProviderLaunchPlan> seatPlan;
};

class ISeatLaunchHost {
public:
    virtual ~ISeatLaunchHost() = default;
    virtual std::optional<runtime::HostRuntimeSnapshot> resnapshot(
        std::string& error) = 0;
    virtual std::optional<runtime::SeatGameCommandResult> assign(
        const runtime::SeatGameBinding& binding, std::string& error) = 0;
    virtual std::optional<runtime::SeatGameCommandResult> start(
        std::string& error) = 0;
    virtual std::optional<runtime::SeatGameCommandResult> stop(
        std::string& error) = 0;
};

// Installs only a previously compiled typed plan for one idle Seat. Production
// implementations own the plan-to-runtime bridge; the Seat UI never supplies a
// shell command, arbitrary executable, or unvalidated argument vector here.
class ISeatLaunchPlanInstaller {
public:
    virtual ~ISeatLaunchPlanInstaller() = default;
    virtual bool install(
        SeatId seatId,
        const plan::ProviderAwareLaunchPlan& fullPlan,
        const plan::SeatProviderLaunchPlan& seatPlan,
        std::string& error) = 0;
    virtual bool rollback(SeatId seatId, std::string& error) noexcept = 0;
};

class SeatHostLaunchAdapter final : public ISeatLaunchHost {
public:
    explicit SeatHostLaunchAdapter(SeatHostClient& client) : client_(client) {}

    std::optional<runtime::HostRuntimeSnapshot> resnapshot(
        std::string& error) override;
    std::optional<runtime::SeatGameCommandResult> assign(
        const runtime::SeatGameBinding& binding, std::string& error) override;
    std::optional<runtime::SeatGameCommandResult> start(
        std::string& error) override;
    std::optional<runtime::SeatGameCommandResult> stop(
        std::string& error) override;

private:
    SeatHostClient& client_;
};

// Orchestrates one idle Seat through the exact shared P6 selection/compiler/
// preflight path, then invokes only Seat-local host operations.
class SeatLaunchFlow {
public:
    explicit SeatLaunchFlow(SeatId seatId);

    SeatLaunchDiagnostic initialize(SeatLaunchContext context);
    SeatLaunchDiagnostic sync(const runtime::HostRuntimeSnapshot& snapshot);
    SeatLaunchDiagnostic select(std::string playerId, std::string gameId);
    SeatLaunchPreview preview() const;
    SeatLaunchDiagnostic activate(ISeatLaunchHost& host,
                                  ISeatLaunchPlanInstaller& installer);

    SeatId seatId() const noexcept { return seatId_; }
    const SeatLauncherState& state() const noexcept { return seatModel_.state(); }
    const launcher_ui::LauncherUiModel& sharedModel() const noexcept {
        return sharedModel_;
    }

private:
    const runtime::SeatGameState* otherSeat(
        const runtime::HostRuntimeSnapshot& snapshot) const;
    bool sameOtherSeat(const runtime::HostRuntimeSnapshot& snapshot) const;
    SeatLaunchDiagnostic rollbackAfterFailure(
        SeatLaunchResult result, std::string message,
        ISeatLaunchHost& host, ISeatLaunchPlanInstaller& installer,
        bool assignmentMayExist);

    SeatId seatId_{0};
    launcher_ui::LauncherUiModel sharedModel_;
    SeatLauncherModel seatModel_;
    std::optional<runtime::HostRuntimeSnapshot> snapshot_;
    bool initialized_{false};
};

std::string_view seatLaunchResultName(SeatLaunchResult result) noexcept;

} // namespace hydra::seatui
