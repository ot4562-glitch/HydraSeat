#pragma once

#include "hydra/game_catalog.hpp"
#include "hydra/plan_preflight.hpp"
#include "hydra/two_player_setup_editor.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::launcher_ui {

inline constexpr std::size_t kMaximumRecentGames = 16u;
inline constexpr std::size_t kMaximumUiProviders = 128u;
inline constexpr std::size_t kMaximumRuntimeRequirements = profile::kMaximumGames;

enum class UiResult : std::uint8_t {
    Success = 0,
    InvalidSeatDocument = 1,
    InvalidLibrary = 2,
    InvalidSetupDocument = 3,
    InvalidPlayer = 4,
    DuplicatePlayer = 5,
    PlayerInUse = 6,
    MissingSeat = 7,
    InactiveSeat = 8,
    MissingGame = 9,
    MissingSetup = 10,
    InvalidSetup = 11,
    InvalidProvider = 12,
    InvalidRequirement = 13,
    InvalidSelection = 14,
};

struct UiDiagnostic {
    UiResult result{UiResult::Success};
    std::string message;

    bool succeeded() const noexcept { return result == UiResult::Success; }
};

// Presentation-only Player preferences. Passwords, tokens, and provider
// credentials remain impossible to represent here or in PlayerProfile.
struct PlayerPresentation {
    std::string playerId;
    std::optional<std::wstring> avatarPath;
    std::vector<std::string> recentGameIds;
    std::optional<SeatId> preferredSeat;

    bool operator==(const PlayerPresentation&) const = default;
};

struct PlayPreview {
    plan::PlanCompileResult compileResult;
    preflight::Summary summary;
};

class LauncherUiModel final {
public:
    UiDiagnostic initialize(
        profile::SeatConfigDocument seats,
        catalog::LocalGameCatalog library,
        profile::TwoPlayerSetupDocument setups,
        std::vector<plan::ProviderAdapterBinding> providers,
        std::vector<plan::GameRuntimeRequirement> requirements);
    UiDiagnostic initializeShared(
        profile::SeatConfigDocument seats,
        catalog::LocalGameCatalog library,
        profile::PlayerProfileDocument players,
        std::vector<PlayerPresentation> playerPresentation,
        profile::TwoPlayerSetupDocument setups,
        std::vector<plan::ProviderAdapterBinding> providers,
        std::vector<plan::GameRuntimeRequirement> requirements);

    UiDiagnostic replaceLibrary(catalog::LocalGameCatalog library);
    UiDiagnostic replaceRequirements(
        std::vector<plan::GameRuntimeRequirement> requirements);
    UiDiagnostic attachProvider(plan::ProviderAdapterBinding binding);

    UiDiagnostic createPlayer(std::wstring displayName,
                              std::string preferredLocale,
                              std::optional<std::wstring> avatarPath,
                              std::string& playerId);
    UiDiagnostic renamePlayer(std::string_view playerId,
                              std::wstring displayName);
    UiDiagnostic removePlayer(std::string_view playerId);
    UiDiagnostic setPlayerAccount(
        std::string_view playerId,
        std::string providerId,
        std::optional<std::string> accountReference);

    UiDiagnostic selectGame(SeatId seatId,
                            std::string playerId,
                            std::string gameId);
    UiDiagnostic selectBoth(std::string gameId,
                            std::string firstPlayerId,
                            std::string secondPlayerId);
    UiDiagnostic clearSeat(SeatId seatId);
    UiDiagnostic chooseSetup(std::string_view setupId);
    UiDiagnostic createSetup(const setup::GenerateSetupInput& input,
                             std::vector<setup::MutationIntent>& mutations);

    PlayPreview preview(
        std::span<const preflight::PlannedMutation> mutations = {}) const;
    UiDiagnostic recordActivatedPlan(const plan::ProviderAwareLaunchPlan& plan);
    UiDiagnostic recordActivatedSeat(
        const plan::ProviderAwareLaunchPlan& plan, SeatId seatId);

    const profile::SeatConfigDocument& seats() const noexcept { return seats_; }
    const catalog::LocalGameCatalog& library() const noexcept { return library_; }
    const profile::PlayerProfileDocument& players() const noexcept { return players_; }
    const std::vector<PlayerPresentation>& playerPresentation() const noexcept {
        return playerPresentation_;
    }
    const profile::TwoPlayerSetupDocument& setups() const noexcept { return setups_; }
    const profile::RuntimeSessionSelection& selection() const noexcept {
        return selection_;
    }
    const std::vector<plan::GameRuntimeRequirement>& requirements() const noexcept {
        return requirements_;
    }

private:
    profile::GameRecordDocument gameDocument() const;
    UiDiagnostic validateLibrary(const catalog::LocalGameCatalog& library) const;
    UiDiagnostic normalizeSelection(profile::RuntimeSessionSelection& selection) const;
    UiDiagnostic validateProviderBinding(
        const plan::ProviderAdapterBinding& binding) const;

    profile::SeatConfigDocument seats_;
    catalog::LocalGameCatalog library_;
    profile::PlayerProfileDocument players_;
    std::vector<PlayerPresentation> playerPresentation_;
    profile::TwoPlayerSetupDocument setups_;
    profile::RuntimeSessionSelection selection_;
    std::vector<plan::ProviderAdapterBinding> providers_;
    std::vector<plan::GameRuntimeRequirement> requirements_;
    std::uint64_t nextPlayerId_{1u};
};

std::string_view uiResultName(UiResult result) noexcept;

} // namespace hydra::launcher_ui
