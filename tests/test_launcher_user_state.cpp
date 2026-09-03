#include "hydra/launcher_user_state.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class TempRoot final {
public:
    TempRoot() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
        const auto process = static_cast<unsigned long long>(GetCurrentProcessId());
#else
        const auto process = 0ull;
#endif
        path_ = std::filesystem::temp_directory_path() /
                ("hydra-launcher-user-state-" + std::to_string(process) + "-" +
                 std::to_string(static_cast<unsigned long long>(stamp)));
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    ~TempRoot() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

    void create() const {
        std::error_code error;
        std::filesystem::create_directories(path_, error);
        check(!error, "test temp root can be created");
    }

private:
    std::filesystem::path path_;
};

hydra::profile::PlayerProfileDocument samplePlayers() {
    hydra::profile::PlayerProfileDocument document;
    document.players.push_back({"player-one", L"Player One", "en-US", {}});
    document.players.push_back({"player-two", L"플레이어 2", "ko-KR", {}});
    return document;
}

void writeBytes(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    check(static_cast<bool>(output), "raw test fixture can be opened");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    check(static_cast<bool>(output), "raw test fixture can be written completely");
}

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    check(static_cast<bool>(input), "saved test state can be opened");
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void testMissingFilesAreValidDefaults() {
    TempRoot root;

    check(hydra::launcher_state::workspaceProfilePath(root.path()) ==
              root.path() / hydra::launcher_state::kWorkspaceProfileFileName,
          "workspace profile resolves inside the injected per-user state root");
    check(hydra::launcher_state::ensureUserStateRoot(root.path()).succeeded() &&
              std::filesystem::is_directory(root.path()),
          "hardware setup can prepare the same validated per-user state root");

    hydra::profile::PlayerProfileDocument players;
    players.players.push_back({"sentinel", L"Sentinel", "en-US", {}});
    const auto profileResult = hydra::launcher_state::loadPlayerProfiles(root.path(), players);
    check(profileResult.succeeded(), "missing players.json is a valid empty/default state");
    check(players == hydra::profile::PlayerProfileDocument{},
          "missing players.json resets output to the default Player document");

    std::optional<hydra::launcher_state::LastPlayerSelection> selection =
        hydra::launcher_state::LastPlayerSelection{"sentinel", std::nullopt};
    const auto selectionResult =
        hydra::launcher_state::loadLastPlayerSelection(root.path(), selection);
    check(selectionResult.succeeded(), "missing selection file is a valid empty/default state");
    check(!selection, "missing selection file clears any caller sentinel selection");
}

void testProfileRoundTripAndDeterminism() {
    TempRoot root;
    const auto expected = samplePlayers();

    check(hydra::launcher_state::savePlayerProfiles(root.path(), expected).succeeded(),
          "Player profiles save transactionally to an injected temp root");
    hydra::profile::PlayerProfileDocument loaded;
    check(hydra::launcher_state::loadPlayerProfiles(root.path(), loaded).succeeded(),
          "Player profiles load after save");
    check(loaded == expected, "Player profile document round-trips exactly");

    const auto target = hydra::launcher_state::playerProfilesPath(root.path());
    const auto firstBytes = readBytes(target);
    check(hydra::launcher_state::savePlayerProfiles(root.path(), expected).succeeded(),
          "repeated Player profile save succeeds");
    check(readBytes(target) == firstBytes,
          "repeated Player profile save produces deterministic bytes");
}

void testSelectionRoundTrips() {
    TempRoot root;

    const hydra::launcher_state::LastPlayerSelection player1Only{"player-one", std::nullopt};
    check(hydra::launcher_state::saveLastPlayerSelection(root.path(), player1Only).succeeded(),
          "Player 1-only selection saves");
    std::optional<hydra::launcher_state::LastPlayerSelection> loaded;
    check(hydra::launcher_state::loadLastPlayerSelection(root.path(), loaded).succeeded(),
          "Player 1-only selection loads");
    check(loaded && *loaded == player1Only, "Player 1-only selection round-trips exactly");

    const hydra::launcher_state::LastPlayerSelection both{"player-one", "player-two"};
    check(hydra::launcher_state::saveLastPlayerSelection(root.path(), both).succeeded(),
          "Player 1 + Player 2 selection saves");
    check(hydra::launcher_state::loadLastPlayerSelection(root.path(), loaded).succeeded(),
          "Player 1 + Player 2 selection loads");
    check(loaded && *loaded == both, "Player 1 + Player 2 selection round-trips exactly");

    const auto target = hydra::launcher_state::lastPlayerSelectionPath(root.path());
    const auto firstBytes = readBytes(target);
    check(hydra::launcher_state::saveLastPlayerSelection(root.path(), both).succeeded(),
          "repeated selection save succeeds");
    check(readBytes(target) == firstBytes,
          "repeated selection save produces deterministic bytes");

    check(hydra::launcher_state::clearLastPlayerSelection(root.path()).succeeded(),
          "explicitly clearing Player 1 removes durable selection state");
    check(!std::filesystem::exists(target),
          "cleared selection file is absent rather than encoding an invalid empty Player 1");
    check(hydra::launcher_state::loadLastPlayerSelection(root.path(), loaded).succeeded() && !loaded,
          "restart after an explicit clear restores the default empty selection");
}

void testStaleSelectionFiltering() {
    const auto players = samplePlayers();
    hydra::launcher_state::FilteredLastPlayerSelection filtered;

    const hydra::launcher_state::LastPlayerSelection valid{"player-one", "player-two"};
    check(hydra::launcher_state::filterLastPlayerSelection(valid, players, filtered).succeeded(),
          "valid saved selection filters successfully");
    check(filtered.selection && *filtered.selection == valid &&
              !filtered.player1Stale && !filtered.player2Stale,
          "valid saved selection is preserved without stale flags");

    const hydra::launcher_state::LastPlayerSelection stalePlayer2{"player-one", "deleted-player"};
    check(hydra::launcher_state::filterLastPlayerSelection(
              stalePlayer2, players, filtered).succeeded(),
          "stale optional Player 2 filters without inventing a profile");
    check(filtered.selection && filtered.selection->player1Id == "player-one" &&
              !filtered.selection->player2Id && !filtered.player1Stale && filtered.player2Stale,
          "stale Player 2 is dropped while valid Player 1 remains restorable");

    const hydra::launcher_state::LastPlayerSelection stalePlayer1{"deleted-player", "player-two"};
    check(hydra::launcher_state::filterLastPlayerSelection(
              stalePlayer1, players, filtered).succeeded(),
          "stale required Player 1 filters deterministically");
    check(!filtered.selection && filtered.player1Stale,
          "stale Player 1 invalidates the entire restore rather than inventing a Player");

    check(hydra::launcher_state::filterLastPlayerSelection(
              std::nullopt, players, filtered).succeeded() && !filtered.selection,
          "empty saved selection remains a valid empty restore");
}

void testMalformedAndOversizeSelectionPayloadsFailClosed() {
    TempRoot root;
    root.create();
    const auto path = hydra::launcher_state::lastPlayerSelectionPath(root.path());
    std::optional<hydra::launcher_state::LastPlayerSelection> loaded;

    writeBytes(path, "version=1\nplayer1=\n");
    check(!hydra::launcher_state::loadLastPlayerSelection(root.path(), loaded).succeeded() && !loaded,
          "empty required Player 1 ID fails closed");

    const std::string embeddedNul("version=1\nplayer1=abc\0def\n", 26u);
    writeBytes(path, embeddedNul);
    check(!hydra::launcher_state::loadLastPlayerSelection(root.path(), loaded).succeeded() && !loaded,
          "embedded NUL in selection payload fails closed");

    writeBytes(path, "version=1\nplayer1=player-one\nunexpected=player-two\n");
    check(!hydra::launcher_state::loadLastPlayerSelection(root.path(), loaded).succeeded() && !loaded,
          "unknown/newline-injected selection field fails closed");

    writeBytes(path, std::string(hydra::launcher_state::kMaximumSelectionFileBytes + 1u, 'x'));
    const auto oversized = hydra::launcher_state::loadLastPlayerSelection(root.path(), loaded);
    check(!oversized.succeeded() &&
              oversized.result == hydra::launcher_state::UserStateResult::FileTooLarge && !loaded,
          "oversize selection file fails closed before parsing");

    writeBytes(path, "");
    check(!hydra::launcher_state::loadLastPlayerSelection(root.path(), loaded).succeeded() && !loaded,
          "present but empty selection file is invalid rather than treated as missing");

    check(!hydra::launcher_state::saveLastPlayerSelection(
              root.path(), {"player\none", std::nullopt}).succeeded(),
          "newline in a selection ID is rejected before write");
    check(!hydra::launcher_state::saveLastPlayerSelection(
              root.path(),
              {std::string(hydra::profile::kMaximumIdentifierBytes + 1u, 'p'), std::nullopt})
              .succeeded(),
          "oversize selection ID is rejected before write");
    check(!hydra::launcher_state::saveLastPlayerSelection(
              root.path(), {"player-one", "player-one"}).succeeded(),
          "the same Player cannot be restored into both Player slots");
}

void testMalformedPlayerDocumentFailsClosed() {
    TempRoot root;
    root.create();
    writeBytes(hydra::launcher_state::playerProfilesPath(root.path()), "{not-json");

    hydra::profile::PlayerProfileDocument loaded = samplePlayers();
    const auto result = hydra::launcher_state::loadPlayerProfiles(root.path(), loaded);
    check(!result.succeeded() &&
              result.result == hydra::launcher_state::UserStateResult::ProfileSchemaError,
          "malformed players.json delegates to profile-schema failure");
    check(loaded == hydra::profile::PlayerProfileDocument{},
          "malformed players.json leaves no partially trusted Player profiles");
}

void testAtomicStagePreconditionPreservesPreviousGoodFile() {
    TempRoot root;
    const auto original = samplePlayers();
    check(hydra::launcher_state::savePlayerProfiles(root.path(), original).succeeded(),
          "initial good Player profile file saves");

    auto changed = original;
    changed.players[0].displayName = L"Changed Player";

    auto stage = hydra::launcher_state::playerProfilesPath(root.path());
    stage += L".stage";
    std::error_code error;
    std::filesystem::create_directory(stage, error);
    check(!error, "test can create a stage-path directory precondition blocker");

    const auto failedSave = hydra::launcher_state::savePlayerProfiles(root.path(), changed);
    check(!failedSave.succeeded(), "stage-path precondition prevents replacement");

    hydra::profile::PlayerProfileDocument loaded;
    check(hydra::launcher_state::loadPlayerProfiles(root.path(), loaded).succeeded(),
          "previous good Player file remains readable after failed staged save");
    check(loaded == original,
          "failed staged save preserves the exact previous good Player file");
}

} // namespace

int main() {
    testMissingFilesAreValidDefaults();
    testProfileRoundTripAndDeterminism();
    testSelectionRoundTrips();
    testStaleSelectionFiltering();
    testMalformedAndOversizeSelectionPayloadsFailClosed();
    testMalformedPlayerDocumentFailsClosed();
    testAtomicStagePreconditionPreservesPreviousGoodFile();

    std::cout << "Launcher user-state tests passed.\n";
    return EXIT_SUCCESS;
}
