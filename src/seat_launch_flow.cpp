#include "hydra/seat_launch_flow.hpp"

#include <algorithm>
#include <utility>

namespace hydra::seatui {
namespace {

SeatLaunchDiagnostic failure(SeatLaunchResult result, std::string message) {
    return {result, std::move(message)};
}

const runtime::SeatGameState* findGameState(
    const runtime::HostRuntimeSnapshot& snapshot, SeatId seatId) {
    const auto found = std::find_if(
        snapshot.seatGames.begin(), snapshot.seatGames.end(),
        [&](const runtime::SeatGameState& state) { return state.seatId == seatId; });
    return found == snapshot.seatGames.end() ? nullptr : &*found;
}

} // namespace

std::optional<runtime::HostRuntimeSnapshot> SeatHostLaunchAdapter::resnapshot(
    std::string& error) {
    return client_.resnapshot(hostipc::kDefaultHostIpcTimeoutMs, &error);
}

std::optional<runtime::SeatGameCommandResult> SeatHostLaunchAdapter::assign(
    const runtime::SeatGameBinding& binding, std::string& error) {
    return client_.assign(binding, hostipc::kDefaultHostIpcTimeoutMs, &error);
}

std::optional<runtime::SeatGameCommandResult> SeatHostLaunchAdapter::start(
    std::string& error) {
    return client_.start(hostipc::kDefaultHostIpcTimeoutMs, &error);
}

std::optional<runtime::SeatGameCommandResult> SeatHostLaunchAdapter::stop(
    std::string& error) {
    return client_.endPlaying(hostipc::kDefaultHostIpcTimeoutMs, &error);
}

SeatLaunchFlow::SeatLaunchFlow(SeatId seatId)
    : seatId_(seatId), seatModel_(seatId) {}

SeatLaunchDiagnostic SeatLaunchFlow::initialize(SeatLaunchContext context) {
    if (seatId_ == 0) {
        return failure(SeatLaunchResult::InvalidContext,
                       "Seat launch flow cannot use reserved Seat 0");
    }
    const auto initialized = sharedModel_.initializeShared(
        std::move(context.seats), std::move(context.library),
        std::move(context.players), std::move(context.playerPresentation),
        std::move(context.setups), std::move(context.providers),
        std::move(context.requirements));
    if (!initialized.succeeded()) {
        return failure(SeatLaunchResult::InvalidContext, initialized.message);
    }
    initialized_ = true;
    snapshot_.reset();
    seatModel_.markDisconnected();
    return {};
}

SeatLaunchDiagnostic SeatLaunchFlow::sync(
    const runtime::HostRuntimeSnapshot& snapshot) {
    if (!initialized_) {
        return failure(SeatLaunchResult::InvalidContext,
                       "Seat launch context is not initialized");
    }
    std::string error;
    if (!seatModel_.applySnapshot(snapshot, &error)) {
        return failure(SeatLaunchResult::InvalidSnapshot, std::move(error));
    }
    snapshot_ = snapshot;
    return {};
}

const runtime::SeatGameState* SeatLaunchFlow::otherSeat(
    const runtime::HostRuntimeSnapshot& snapshot) const {
    const auto found = std::find_if(
        snapshot.seatGames.begin(), snapshot.seatGames.end(),
        [&](const runtime::SeatGameState& state) { return state.seatId != seatId_; });
    return found == snapshot.seatGames.end() ? nullptr : &*found;
}

bool SeatLaunchFlow::sameOtherSeat(
    const runtime::HostRuntimeSnapshot& snapshot) const {
    if (!snapshot_) return false;
    const auto* before = otherSeat(*snapshot_);
    const auto* after = otherSeat(snapshot);
    return (before == nullptr && after == nullptr) ||
           (before != nullptr && after != nullptr && *before == *after);
}

SeatLaunchDiagnostic SeatLaunchFlow::select(
    std::string playerId, std::string gameId) {
    if (!snapshot_) {
        return failure(SeatLaunchResult::InvalidSnapshot,
                       "an authoritative host snapshot is required before selection");
    }
    if (seatModel_.state().phase != SeatLauncherPhase::Idle) {
        return failure(SeatLaunchResult::SeatNotIdle,
                       "only an authoritative idle Seat may choose a new game");
    }

    while (!sharedModel_.selection().bindings.empty()) {
        const auto seat = sharedModel_.selection().bindings.front().seatId;
        (void)sharedModel_.clearSeat(seat);
    }

    const auto* other = otherSeat(*snapshot_);
    if (other != nullptr && other->phase != runtime::SeatGamePhase::Idle) {
        if (!other->binding) {
            return failure(SeatLaunchResult::InvalidSnapshot,
                           "active other Seat has no authoritative Player/game binding");
        }
        const auto selectedOther = sharedModel_.selectGame(
            other->seatId, other->binding->playerId, other->binding->gameId);
        if (!selectedOther.succeeded() &&
            selectedOther.result != launcher_ui::UiResult::MissingSetup) {
            return failure(SeatLaunchResult::SelectionRejected,
                           "other Seat binding is unavailable in the shared context: " +
                               selectedOther.message);
        }
    }

    const auto selected = sharedModel_.selectGame(
        seatId_, std::move(playerId), std::move(gameId));
    if (!selected.succeeded() &&
        selected.result != launcher_ui::UiResult::MissingSetup) {
        return failure(SeatLaunchResult::SelectionRejected, selected.message);
    }

    const auto binding = std::find_if(
        sharedModel_.selection().bindings.begin(),
        sharedModel_.selection().bindings.end(),
        [&](const profile::RuntimeBinding& value) { return value.seatId == seatId_; });
    if (binding == sharedModel_.selection().bindings.end()) {
        return failure(SeatLaunchResult::SelectionRejected,
                       "shared selection did not retain the assigned Seat");
    }
    SeatLauncherChoices choices;
    choices.selectedPlayerId = binding->playerId;
    choices.selectedGameId = binding->gameId;
    choices.availableGameIds.reserve(sharedModel_.library().entries.size());
    for (const auto& entry : sharedModel_.library().entries) {
        choices.availableGameIds.push_back(entry.game.gameId);
    }
    const auto presentation = std::find_if(
        sharedModel_.playerPresentation().begin(),
        sharedModel_.playerPresentation().end(),
        [&](const launcher_ui::PlayerPresentation& value) {
            return value.playerId == binding->playerId;
        });
    if (presentation != sharedModel_.playerPresentation().end()) {
        choices.recentGameIds = presentation->recentGameIds;
    }
    std::string choiceError;
    if (!seatModel_.setChoices(std::move(choices), &choiceError)) {
        return failure(SeatLaunchResult::InvalidContext, std::move(choiceError));
    }

    const auto selectedPreview = preview();
    if (!selectedPreview.sharedPreview.summary.canActivate) {
        return failure(SeatLaunchResult::PreflightBlocked,
                       selected.succeeded() ? "shared plan/preflight blocked activation"
                                            : selected.message);
    }
    return {};
}

SeatLaunchPreview SeatLaunchFlow::preview() const {
    SeatLaunchPreview result;
    result.sharedPreview = sharedModel_.preview();
    if (!result.sharedPreview.compileResult.plan) return result;
    const auto found = std::find_if(
        result.sharedPreview.compileResult.plan->seats.begin(),
        result.sharedPreview.compileResult.plan->seats.end(),
        [&](const plan::SeatProviderLaunchPlan& seat) {
            return seat.seatId == seatId_;
        });
    if (found != result.sharedPreview.compileResult.plan->seats.end()) {
        result.seatPlan = *found;
    }
    return result;
}

SeatLaunchDiagnostic SeatLaunchFlow::rollbackAfterFailure(
    SeatLaunchResult result, std::string message,
    ISeatLaunchHost& host, ISeatLaunchPlanInstaller& installer,
    bool assignmentMayExist) {
    std::string rollbackError;
    bool stopped = true;
    if (assignmentMayExist) {
        const auto stopResult = host.stop(rollbackError);
        stopped = stopResult && stopResult->succeeded();
    }
    const bool planRolledBack = installer.rollback(seatId_, rollbackError);
    if (!stopped || !planRolledBack) {
        if (!message.empty()) message += "; ";
        message += "Seat-local rollback could not be verified: " + rollbackError;
        return failure(SeatLaunchResult::ResultUncertain, std::move(message));
    }
    return failure(result, std::move(message));
}

SeatLaunchDiagnostic SeatLaunchFlow::activate(
    ISeatLaunchHost& host, ISeatLaunchPlanInstaller& installer) {
    if (!snapshot_ || seatModel_.state().phase != SeatLauncherPhase::Idle) {
        return failure(SeatLaunchResult::SeatNotIdle,
                       "only an authoritative idle Seat can activate a selection");
    }
    const auto selectedPreview = preview();
    if (!selectedPreview.sharedPreview.summary.canActivate ||
        !selectedPreview.sharedPreview.compileResult.plan ||
        !selectedPreview.seatPlan) {
        return failure(SeatLaunchResult::PreflightBlocked,
                       "current shared plan/preflight does not permit activation");
    }

    std::string error;
    const auto fresh = host.resnapshot(error);
    if (!fresh || fresh->sessionId != snapshot_->sessionId ||
        fresh->generation != snapshot_->generation ||
        fresh->transitionSequence != snapshot_->transitionSequence ||
        fresh->configuredSeats != snapshot_->configuredSeats ||
        !sameOtherSeat(*fresh)) {
        return failure(SeatLaunchResult::SnapshotChanged,
                       error.empty() ?
                           "host authority or another Seat changed before activation" : error);
    }
    const auto* ownFresh = findGameState(*fresh, seatId_);
    if (ownFresh == nullptr || ownFresh->phase != runtime::SeatGamePhase::Idle ||
        ownFresh->generation != seatModel_.state().seatGameGeneration) {
        return failure(SeatLaunchResult::SnapshotChanged,
                       "assigned Seat changed before activation");
    }

    const auto& fullPlan = *selectedPreview.sharedPreview.compileResult.plan;
    if (!installer.install(seatId_, fullPlan, *selectedPreview.seatPlan, error)) {
        return rollbackAfterFailure(SeatLaunchResult::PlanInstallFailed,
                                    std::move(error), host, installer, false);
    }

    const runtime::SeatGameBinding binding{
        selectedPreview.seatPlan->playerId, selectedPreview.seatPlan->gameId};
    const auto assigned = host.assign(binding, error);
    if (!assigned || !assigned->succeeded()) {
        return rollbackAfterFailure(SeatLaunchResult::AssignFailed,
                                    std::move(error), host, installer, true);
    }
    const auto started = host.start(error);
    if (!started || !started->succeeded()) {
        return rollbackAfterFailure(SeatLaunchResult::StartFailed,
                                    std::move(error), host, installer, true);
    }

    const auto resultSnapshot = host.resnapshot(error);
    if (!resultSnapshot) {
        return failure(SeatLaunchResult::ResultUncertain,
                       "Seat start succeeded but authoritative resnapshot failed: " + error);
    }
    if (!sameOtherSeat(*resultSnapshot)) {
        return failure(SeatLaunchResult::OtherSeatChanged,
                       "other Seat changed during Seat-local activation");
    }
    const auto* ownResult = findGameState(*resultSnapshot, seatId_);
    if (ownResult == nullptr || ownResult->phase != runtime::SeatGamePhase::Playing ||
        ownResult->binding != std::optional<runtime::SeatGameBinding>(binding)) {
        return failure(SeatLaunchResult::ResultUncertain,
                       "host did not confirm the selected Seat as Playing");
    }

    const auto recorded = sharedModel_.recordActivatedSeat(fullPlan, seatId_);
    if (!recorded.succeeded()) {
        return failure(SeatLaunchResult::ResultUncertain, recorded.message);
    }
    if (const auto synced = sync(*resultSnapshot); !synced.succeeded()) return synced;
    return {};
}

std::string_view seatLaunchResultName(SeatLaunchResult result) noexcept {
    switch (result) {
        case SeatLaunchResult::Success: return "Success";
        case SeatLaunchResult::InvalidContext: return "InvalidContext";
        case SeatLaunchResult::InvalidSnapshot: return "InvalidSnapshot";
        case SeatLaunchResult::SeatNotIdle: return "SeatNotIdle";
        case SeatLaunchResult::SelectionRejected: return "SelectionRejected";
        case SeatLaunchResult::PreflightBlocked: return "PreflightBlocked";
        case SeatLaunchResult::SnapshotChanged: return "SnapshotChanged";
        case SeatLaunchResult::PlanInstallFailed: return "PlanInstallFailed";
        case SeatLaunchResult::AssignFailed: return "AssignFailed";
        case SeatLaunchResult::StartFailed: return "StartFailed";
        case SeatLaunchResult::ResultUncertain: return "ResultUncertain";
        case SeatLaunchResult::OtherSeatChanged: return "OtherSeatChanged";
    }
    return "Unknown";
}

} // namespace hydra::seatui
