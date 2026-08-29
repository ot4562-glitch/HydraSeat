#pragma once

#include "hydra/host_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hydra::seatui {

constexpr std::size_t kMaximumPresentedGames = 128u;

enum class SeatLauncherPhase : std::uint8_t {
    Disconnected = 0,
    Idle = 1,
    Planning = 2,
    Starting = 3,
    Playing = 4,
    Stopping = 5,
    Warning = 6,
    Recovery = 7,
};

struct SeatLauncherChoices {
    std::string selectedPlayerId;
    std::string selectedGameId;
    std::vector<std::string> recentGameIds;
    std::vector<std::string> availableGameIds;

    bool operator==(const SeatLauncherChoices&) const = default;
};

struct SeatLauncherState {
    SeatId seatId{0};
    bool connected{false};
    SeatLauncherPhase phase{SeatLauncherPhase::Disconnected};
    std::wstring seatName;
    std::vector<std::wstring> assignedDisplayIds;
    std::optional<std::wstring> primaryDisplayId;
    std::optional<runtime::SeatGameBinding> currentBinding;
    SeatLauncherChoices choices;
    std::uint64_t authorityGeneration{0};
    std::uint64_t transitionSequence{0};
    std::uint64_t seatGameGeneration{0};
    std::string status;
    std::string warning;
    bool canAssign{false};
    bool canStart{false};
    bool canEndPlaying{false};
    bool canReconnect{true};
    bool nonIntrusiveWhilePlaying{false};
};

// Pure presentation/policy model for exactly one configured Seat. It consumes
// complete host snapshots and can only construct Seat-local game commands.
class SeatLauncherModel {
public:
    explicit SeatLauncherModel(SeatId seatId);

    bool applySnapshot(const runtime::HostRuntimeSnapshot& snapshot,
                       std::string* error = nullptr);
    void markDisconnected(std::string diagnostic = {});
    bool setChoices(SeatLauncherChoices choices, std::string* error = nullptr);

    std::optional<hostipc::SeatGameCommandPayload> assignCommand(
        std::string* error = nullptr) const;
    std::optional<hostipc::SeatGameCommandPayload> startCommand(
        std::string* error = nullptr) const;
    std::optional<hostipc::SeatGameCommandPayload> endPlayingCommand(
        std::string* error = nullptr) const;

    SeatId seatId() const noexcept { return seatId_; }
    const SeatLauncherState& state() const noexcept { return state_; }

private:
    SeatId seatId_{0};
    SeatLauncherState state_;
    std::optional<runtime::RuntimeSessionId> authoritySession_;
};

std::string_view seatLauncherPhaseName(SeatLauncherPhase phase) noexcept;

} // namespace hydra::seatui
