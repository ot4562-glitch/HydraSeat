#include "hydra/seat_hotkey_model.hpp"

#include <utility>

namespace hydra::seatui {

bool SeatHotkeyModel::evaluate(const SeatHotkeyContext& context,
                               SeatHotkeyChord chord,
                               SeatHotkeyDecision& output,
                               std::string* error) {
    if (seatId_ == 0u || context.seatId != seatId_) {
        if (error != nullptr) *error = "hotkey source does not match assigned Seat";
        return false;
    }
    if (context.phase != SeatLauncherPhase::Disconnected &&
        (context.authorityGeneration < authorityGeneration_ ||
         context.transitionSequence < transitionSequence_)) {
        if (error != nullptr) *error = "stale Seat hotkey context was rejected";
        return false;
    }

    SeatHotkeyDecision next;
    next.seatId = seatId_;
    switch (chord) {
        case SeatHotkeyChord::RefreshStatus:
            if (context.canReconnect || context.phase == SeatLauncherPhase::Disconnected ||
                context.phase == SeatLauncherPhase::Warning ||
                context.phase == SeatLauncherPhase::Recovery) {
                next.action = SeatHotkeyAction::Resnapshot;
            }
            break;
        case SeatHotkeyChord::EndPlaying:
            if (context.canEndPlaying) {
                next.action = SeatHotkeyAction::ConfirmEndPlaying;
                next.requiresConfirmation = true;
            }
            break;
        case SeatHotkeyChord::RecoveryHelp:
            if (context.phase == SeatLauncherPhase::Warning ||
                context.phase == SeatLauncherPhase::Recovery ||
                context.phase == SeatLauncherPhase::Disconnected) {
                next.action = SeatHotkeyAction::ShowRecoveryHelp;
            }
            break;
        case SeatHotkeyChord::EmergencyResetHelp:
            next.action = SeatHotkeyAction::ShowEmergencyResetHelp;
            next.requiresConfirmation = true;
            break;
    }

    if (context.phase == SeatLauncherPhase::Disconnected) {
        authorityGeneration_ = 0u;
        transitionSequence_ = 0u;
    } else {
        authorityGeneration_ = context.authorityGeneration;
        transitionSequence_ = context.transitionSequence;
    }
    output = next;
    if (error != nullptr) error->clear();
    return true;
}

std::string_view seatHotkeyChordName(SeatHotkeyChord chord) noexcept {
    switch (chord) {
        case SeatHotkeyChord::RefreshStatus: return "RefreshStatus";
        case SeatHotkeyChord::EndPlaying: return "EndPlaying";
        case SeatHotkeyChord::RecoveryHelp: return "RecoveryHelp";
        case SeatHotkeyChord::EmergencyResetHelp: return "EmergencyResetHelp";
    }
    return "Unknown";
}

std::string_view seatHotkeyActionName(SeatHotkeyAction action) noexcept {
    switch (action) {
        case SeatHotkeyAction::None: return "None";
        case SeatHotkeyAction::Resnapshot: return "Resnapshot";
        case SeatHotkeyAction::ConfirmEndPlaying: return "ConfirmEndPlaying";
        case SeatHotkeyAction::ShowRecoveryHelp: return "ShowRecoveryHelp";
        case SeatHotkeyAction::ShowEmergencyResetHelp: return "ShowEmergencyResetHelp";
    }
    return "Unknown";
}

} // namespace hydra::seatui
