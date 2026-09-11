#pragma once

#include "hydra/profile_schema.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace hydra::launcher_state {

inline constexpr wchar_t kPlayerProfilesFileName[] = L"players.json";
inline constexpr wchar_t kLastPlayerSelectionFileName[] = L"launcher-selection.state";
inline constexpr wchar_t kWorkspaceProfileFileName[] = L"workspace_config.json";
inline constexpr std::size_t kMaximumSelectionFileBytes = 1024u;

struct LastPlayerSelection {
    std::string player1Id;
    std::optional<std::string> player2Id;

    bool operator==(const LastPlayerSelection&) const = default;
};

enum class UserStateResult : std::uint8_t {
    Success = 0,
    InvalidArgument = 1,
    IoError = 2,
    FileTooLarge = 3,
    InvalidData = 4,
    ProfileSchemaError = 5,
    AtomicReplaceFailed = 6,
};

struct UserStateDiagnostic {
    UserStateResult result{UserStateResult::Success};
    std::uint32_t systemError{0};
    std::string message;

    bool succeeded() const noexcept { return result == UserStateResult::Success; }
};

struct FilteredLastPlayerSelection {
    std::optional<LastPlayerSelection> selection;
    bool player1Stale{false};
    bool player2Stale{false};
};

// Production convenience only. Tests and storage operations should pass an
// explicit root so they never touch the user's real LocalAppData implicitly.
std::optional<std::filesystem::path> defaultUserStateRoot();

// Creates and validates the per-user state root without writing any state file.
// Hardware setup uses this before its independent transactional profile write.
UserStateDiagnostic ensureUserStateRoot(const std::filesystem::path& root);

std::filesystem::path playerProfilesPath(const std::filesystem::path& root);
std::filesystem::path lastPlayerSelectionPath(const std::filesystem::path& root);
std::filesystem::path workspaceProfilePath(const std::filesystem::path& root);

UserStateDiagnostic loadPlayerProfiles(const std::filesystem::path& root,
                                       profile::PlayerProfileDocument& document);
UserStateDiagnostic savePlayerProfiles(const std::filesystem::path& root,
                                       const profile::PlayerProfileDocument& document);

// A missing selection file is a successful empty/default state. A present file
// must contain a non-empty Player 1 ID and, when present, a distinct Player 2 ID.
UserStateDiagnostic loadLastPlayerSelection(
    const std::filesystem::path& root,
    std::optional<LastPlayerSelection>& selection);
UserStateDiagnostic saveLastPlayerSelection(
    const std::filesystem::path& root,
    const LastPlayerSelection& selection);
UserStateDiagnostic clearLastPlayerSelection(const std::filesystem::path& root);

// Stale Player 1 invalidates the entire restore. Stale optional Player 2 is
// dropped while the valid Player 1 restore is retained. No profile is created.
UserStateDiagnostic filterLastPlayerSelection(
    const std::optional<LastPlayerSelection>& stored,
    const profile::PlayerProfileDocument& players,
    FilteredLastPlayerSelection& filtered);

} // namespace hydra::launcher_state
