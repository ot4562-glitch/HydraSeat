#include "hydra/seat_launcher_model.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace hydra::seatui {
namespace {

bool fail(std::string message, std::string* error) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

bool validIdentifier(const std::string& value) {
    return !value.empty() && value.size() <= runtime::kSeatGameIdentifierMaxBytes &&
           value.find('\0') == std::string::npos;
}

bool validGameList(const std::vector<std::string>& values) {
    if (values.size() > kMaximumPresentedGames) return false;
    std::set<std::string> unique;
    return std::all_of(values.begin(), values.end(), [&](const std::string& value) {
        return validIdentifier(value) && unique.insert(value).second;
    });
}

SeatLauncherPhase viewPhase(runtime::SeatGamePhase phase) {
    switch (phase) {
        case runtime::SeatGamePhase::Idle: return SeatLauncherPhase::Idle;
        case runtime::SeatGamePhase::Planning: return SeatLauncherPhase::Planning;
        case runtime::SeatGamePhase::Starting: return SeatLauncherPhase::Starting;
        case runtime::SeatGamePhase::Playing: return SeatLauncherPhase::Playing;
        case runtime::SeatGamePhase::Stopping: return SeatLauncherPhase::Stopping;
        case runtime::SeatGamePhase::Degraded: return SeatLauncherPhase::Warning;
        case runtime::SeatGamePhase::RecoveryRequired: return SeatLauncherPhase::Recovery;
    }
    return SeatLauncherPhase::Recovery;
}

bool uniqueSeatShape(const runtime::HostRuntimeSnapshot& snapshot) {
    std::set<SeatId> configured;
    for (const auto& seat : snapshot.configuredSeats) {
        if (seat.seatId == 0 || !configured.insert(seat.seatId).second) return false;
    }
    std::set<SeatId> runtimeSeats;
    for (const auto& seat : snapshot.seats) {
        if (seat.seatId == 0 || !runtimeSeats.insert(seat.seatId).second) return false;
    }
    std::set<SeatId> gameSeats;
    for (const auto& seat : snapshot.seatGames) {
        if (seat.seatId == 0 || !gameSeats.insert(seat.seatId).second) return false;
    }
    return true;
}

} // namespace

SeatLauncherPresentation seatLauncherPresentation(
    const SeatLauncherState& state) noexcept {
    SeatLauncherPresentation presentation;
    presentation.compact = state.phase == SeatLauncherPhase::Playing &&
                           state.nonIntrusiveWhilePlaying;
    presentation.showEndPlaying = state.canEndPlaying;
    presentation.showReconnect = state.canReconnect &&
                                 (state.phase == SeatLauncherPhase::Disconnected ||
                                  state.phase == SeatLauncherPhase::Warning ||
                                  state.phase == SeatLauncherPhase::Recovery);
    presentation.showNotification =
        state.phase == SeatLauncherPhase::Disconnected ||
        state.phase == SeatLauncherPhase::Warning ||
        state.phase == SeatLauncherPhase::Recovery;
    presentation.showPlayer = state.connected && !presentation.compact &&
                              state.phase != SeatLauncherPhase::Disconnected;
    presentation.showGame = presentation.showPlayer;
    return presentation;
}

SeatLauncherModel::SeatLauncherModel(SeatId seatId) : seatId_(seatId) {
    state_.seatId = seatId;
    state_.status = seatId == 0 ? "Invalid Seat identity" : "Waiting for HydraSeat host";
}

bool SeatLauncherModel::applySnapshot(const runtime::HostRuntimeSnapshot& snapshot,
                                      std::string* error) {
    if (seatId_ == 0) return fail("Seat launcher cannot use reserved Seat 0", error);
    if (snapshot.schemaVersion != 3u) {
        return fail("unsupported host snapshot schema", error);
    }
    if (!uniqueSeatShape(snapshot)) {
        return fail("host snapshot contains duplicate or invalid Seat identities", error);
    }

    const auto configured = std::find_if(
        snapshot.configuredSeats.begin(), snapshot.configuredSeats.end(),
        [&](const SeatConfig& seat) { return seat.seatId == seatId_ && seat.active; });
    const auto game = std::find_if(
        snapshot.seatGames.begin(), snapshot.seatGames.end(),
        [&](const runtime::SeatGameState& seat) { return seat.seatId == seatId_; });
    const auto runtimeSeat = std::find_if(
        snapshot.seats.begin(), snapshot.seats.end(),
        [&](const runtime::SeatRuntimeState& seat) { return seat.seatId == seatId_; });
    if (configured == snapshot.configuredSeats.end() ||
        game == snapshot.seatGames.end() || runtimeSeat == snapshot.seats.end()) {
        return fail("authoritative snapshot does not contain the assigned active Seat", error);
    }
    if (snapshot.seatGames.size() > runtime::kV1MaximumActiveSeats) {
        return fail("host snapshot exceeds the v1 active Seat limit", error);
    }
    if (authoritySession_) {
        if (*authoritySession_ != snapshot.sessionId) {
            return fail("host authority changed; disconnect before accepting a new snapshot", error);
        }
        if (snapshot.generation < state_.authorityGeneration ||
            snapshot.transitionSequence < state_.transitionSequence ||
            game->generation < state_.seatGameGeneration) {
            return fail("stale host snapshot was rejected", error);
        }
    }

    std::set<std::wstring> displayIds;
    for (const auto& displayId : configured->displayIds) {
        if (displayId.empty() || !displayIds.insert(displayId).second) {
            return fail("assigned Seat display group is malformed", error);
        }
    }
    if (configured->primaryDisplayId &&
        !displayIds.contains(*configured->primaryDisplayId)) {
        return fail("assigned Seat primary display is outside its display group", error);
    }

    SeatLauncherState next = state_;
    next.connected = true;
    next.phase = viewPhase(game->phase);
    next.seatName = configured->name;
    next.assignedDisplayIds = configured->displayIds;
    next.primaryDisplayId = configured->primaryDisplayId;
    next.currentBinding = game->binding;
    next.authorityGeneration = snapshot.generation;
    next.transitionSequence = snapshot.transitionSequence;
    next.seatGameGeneration = game->generation;
    next.warning.clear();
    next.canAssign = game->phase == runtime::SeatGamePhase::Idle;
    next.canStart = game->phase == runtime::SeatGamePhase::Planning && game->binding.has_value();
    next.canEndPlaying = game->phase != runtime::SeatGamePhase::Idle &&
                         game->phase != runtime::SeatGamePhase::Stopping;
    next.canReconnect = next.phase == SeatLauncherPhase::Warning ||
                        next.phase == SeatLauncherPhase::Recovery;
    next.nonIntrusiveWhilePlaying = game->phase == runtime::SeatGamePhase::Playing;

    switch (next.phase) {
        case SeatLauncherPhase::Idle: next.status = "Ready"; break;
        case SeatLauncherPhase::Planning: next.status = "Ready to start"; break;
        case SeatLauncherPhase::Starting: next.status = "Starting game"; break;
        case SeatLauncherPhase::Playing: next.status = "Playing"; break;
        case SeatLauncherPhase::Stopping: next.status = "Ending play session"; break;
        case SeatLauncherPhase::Warning:
            next.status = "Game needs attention";
            next.warning = game->diagnostic.empty() ? runtimeSeat->diagnostic : game->diagnostic;
            break;
        case SeatLauncherPhase::Recovery:
            next.status = "Recovery required";
            next.warning = game->diagnostic.empty() ? runtimeSeat->diagnostic : game->diagnostic;
            break;
        case SeatLauncherPhase::Disconnected: break;
    }
    if (next.warning.empty() && !game->diagnostic.empty() &&
        game->phase != runtime::SeatGamePhase::Playing) {
        next.warning = game->diagnostic;
    }

    state_ = std::move(next);
    authoritySession_ = snapshot.sessionId;
    if (error != nullptr) error->clear();
    return true;
}

void SeatLauncherModel::markDisconnected(std::string diagnostic) {
    state_.connected = false;
    state_.phase = SeatLauncherPhase::Disconnected;
    state_.currentBinding.reset();
    state_.canAssign = false;
    state_.canStart = false;
    state_.canEndPlaying = false;
    state_.canReconnect = true;
    state_.nonIntrusiveWhilePlaying = false;
    state_.status = "Disconnected";
    state_.warning = std::move(diagnostic);
    authoritySession_.reset();
}

bool SeatLauncherModel::setChoices(SeatLauncherChoices choices, std::string* error) {
    if ((!choices.selectedPlayerId.empty() && !validIdentifier(choices.selectedPlayerId)) ||
        (!choices.selectedGameId.empty() && !validIdentifier(choices.selectedGameId)) ||
        !validGameList(choices.recentGameIds) || !validGameList(choices.availableGameIds)) {
        return fail("Seat launcher choices are invalid, duplicated, or exceed bounds", error);
    }
    state_.choices = std::move(choices);
    if (error != nullptr) error->clear();
    return true;
}

std::optional<hostipc::SeatGameCommandPayload> SeatLauncherModel::assignCommand(
    std::string* error) const {
    if (!state_.connected || !state_.canAssign ||
        !validIdentifier(state_.choices.selectedPlayerId) ||
        !validIdentifier(state_.choices.selectedGameId)) {
        fail("an idle connected Seat with a Player and game selection is required", error);
        return std::nullopt;
    }
    hostipc::SeatGameCommandPayload payload;
    payload.seatId = seatId_;
    payload.binding = runtime::SeatGameBinding{
        state_.choices.selectedPlayerId, state_.choices.selectedGameId};
    if (error != nullptr) error->clear();
    return payload;
}

std::optional<hostipc::SeatGameCommandPayload> SeatLauncherModel::startCommand(
    std::string* error) const {
    if (!state_.connected || !state_.canStart) {
        fail("Seat is not ready to start its assigned game", error);
        return std::nullopt;
    }
    if (error != nullptr) error->clear();
    return hostipc::SeatGameCommandPayload{seatId_, std::nullopt};
}

std::optional<hostipc::SeatGameCommandPayload> SeatLauncherModel::endPlayingCommand(
    std::string* error) const {
    if (!state_.connected || !state_.canEndPlaying) {
        fail("Seat has no game lifecycle that can be ended", error);
        return std::nullopt;
    }
    if (error != nullptr) error->clear();
    return hostipc::SeatGameCommandPayload{seatId_, std::nullopt};
}

std::string_view seatLauncherPhaseName(SeatLauncherPhase phase) noexcept {
    switch (phase) {
        case SeatLauncherPhase::Disconnected: return "disconnected";
        case SeatLauncherPhase::Idle: return "idle";
        case SeatLauncherPhase::Planning: return "planning";
        case SeatLauncherPhase::Starting: return "starting";
        case SeatLauncherPhase::Playing: return "playing";
        case SeatLauncherPhase::Stopping: return "stopping";
        case SeatLauncherPhase::Warning: return "warning";
        case SeatLauncherPhase::Recovery: return "recovery";
    }
    return "unknown";
}

} // namespace hydra::seatui
