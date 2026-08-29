#pragma once

#include "hydra/seat_launcher_model.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace hydra::seatui {

enum class SeatHotkeyChord : std::uint8_t {
    RefreshStatus = 0,
    EndPlaying = 1,
    RecoveryHelp = 2,
    EmergencyResetHelp = 3,
};

enum class SeatHotkeyAction : std::uint8_t {
    None = 0,
    Resnapshot = 1,
    ConfirmEndPlaying = 2,
    ShowRecoveryHelp = 3,
    ShowEmergencyResetHelp = 4,
};

struct SeatHotkeyContext {
    SeatId seatId{0};
    SeatLauncherPhase phase{SeatLauncherPhase::Disconnected};
    std::uint64_t authorityGeneration{0};
    std::uint64_t transitionSequence{0};
    bool canEndPlaying{false};
    bool canReconnect{false};
};

struct SeatHotkeyDecision {
    SeatHotkeyAction action{SeatHotkeyAction::None};
    SeatId seatId{0};
    bool requiresConfirmation{false};
};

// Pure Seat-local policy. It receives already-scoped semantic key chords from the
// Seat Launcher window/input boundary and can never construct global host commands.
// Emergency reset is surfaced as help only; the independently validated reset tool
// retains its own ownership/confirmation boundary.
class SeatHotkeyModel {
public:
    explicit SeatHotkeyModel(SeatId seatId) : seatId_(seatId) {}

    bool evaluate(const SeatHotkeyContext& context,
                  SeatHotkeyChord chord,
                  SeatHotkeyDecision& output,
                  std::string* error = nullptr);

private:
    SeatId seatId_{0};
    std::uint64_t authorityGeneration_{0};
    std::uint64_t transitionSequence_{0};
};

std::string_view seatHotkeyChordName(SeatHotkeyChord chord) noexcept;
std::string_view seatHotkeyActionName(SeatHotkeyAction action) noexcept;

} // namespace hydra::seatui
