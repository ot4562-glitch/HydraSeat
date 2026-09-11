#include "hydra/launcher_win32.hpp"

#ifdef _WIN32

#include "hydra/custom_executable_provider.hpp"
#include "hydra/local_compatibility_evidence.hpp"
#include "hydra/local_compatibility_runner.hpp"
#include "hydra/production_input_authority.hpp"
#include "hydra/launcher_ui_model.hpp"
#include "hydra/launcher_user_state.hpp"
#include "hydra/runtime_requirement_authority.hpp"
#include "hydra/steam_provider.hpp"
#include "hydra/ui_localization.hpp"

#include <commdlg.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hydra::launcher_ui {
namespace {

constexpr wchar_t kWindowClass[] = L"HydraSeatGameLibraryWindow";
constexpr int kGameList = 2101;
constexpr int kRefresh = 2102;
constexpr int kAddExe = 2103;
constexpr int kPlayerName = 2104;
constexpr int kAddPlayer = 2105;
constexpr int kPrivacyRetention = 2131;
constexpr int kSeat1Player = 2110;
constexpr int kSeat1Status = 2111;
constexpr int kSeat2Player = 2112;
constexpr int kSeat2Status = 2113;
constexpr int kPlay = 2121;
constexpr int kConfigure = 2152;

bool activateFocusedButtonWithEnter(HWND root, const MSG& message) noexcept {
    if (message.message != WM_KEYDOWN || message.wParam != VK_RETURN || root == nullptr) {
        return false;
    }
    HWND focused = GetFocus();
    if (focused == nullptr || (focused != root && IsChild(root, focused) == FALSE) ||
        IsWindowEnabled(focused) == FALSE) {
        return false;
    }
    wchar_t className[16]{};
    if (GetClassNameW(focused, className, static_cast<int>(std::size(className))) == 0 ||
        _wcsicmp(className, L"Button") != 0) {
        return false;
    }
    SendMessageW(focused, BM_CLICK, 0, 0);
    return true;
}

std::wstring widen(std::string_view value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return L"Invalid text";
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), output.data(), size) != size) {
        return L"Invalid text";
    }
    return output;
}

std::optional<std::string> narrowUtf8(std::wstring_view value) {
    if (value.empty()) return std::string{};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) return std::nullopt;
    std::string output(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), output.data(), size,
                            nullptr, nullptr) != size) {
        return std::nullopt;
    }
    return output;
}

std::string hexValue(std::uint64_t value) {
    char buffer[17]{};
    const auto converted = std::to_chars(std::begin(buffer), std::end(buffer) - 1,
                                         value, 16);
    if (converted.ec != std::errc{}) return "0";
    return std::string(buffer, converted.ptr);
}

void mixFingerprint(std::uint64_t& value, std::string_view text) noexcept {
    constexpr std::uint64_t kPrime = 1099511628211ull;
    for (const char raw : text) {
        value ^= static_cast<std::uint64_t>(static_cast<unsigned char>(raw));
        value *= kPrime;
    }
}

std::uint64_t localCheckFingerprint(const catalog::LocalGameCatalogEntry& game,
                                    std::wstring_view executablePath) {
    std::uint64_t value = 1469598103934665603ull;
    mixFingerprint(value, game.game.gameId);
    mixFingerprint(value, game.game.providerId);
    if (game.game.providerAppId) mixFingerprint(value, *game.game.providerAppId);
    const auto path = narrowUtf8(executablePath);
    if (path) mixFingerprint(value, *path);
    return value == 0u ? 1u : value;
}

std::string localResultId(std::uint64_t fingerprint) {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER timestamp{};
    timestamp.LowPart = now.dwLowDateTime;
    timestamp.HighPart = now.dwHighDateTime;
    return "local-" + hexValue(fingerprint) + "-" + hexValue(timestamp.QuadPart);
}

std::optional<std::filesystem::path> manualGamesPath() {
    wchar_t localAppData[32768]{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (length == 0u || length >= static_cast<DWORD>(std::size(localAppData))) return std::nullopt;
    return std::filesystem::path(localAppData) / L"HydraSeat" / L"manual-games.json";
}

UINT dpiForWindowCompat(HWND window) noexcept {
    using Function = UINT(WINAPI*)(HWND);
    const HMODULE user = GetModuleHandleW(L"user32.dll");
    const auto function = reinterpret_cast<Function>(
        user != nullptr ? GetProcAddress(user, "GetDpiForWindow") : nullptr);
    if (function != nullptr) {
        const UINT dpi = function(window);
        if (dpi != 0u) return dpi;
    }
    HDC dc = GetDC(window);
    if (dc == nullptr) return 96u;
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(window, dc);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96u;
}

void adjustWindowRectForDpiCompat(RECT& rect, DWORD style, DWORD extendedStyle,
                                  UINT dpi) noexcept {
    using Function = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    const HMODULE user = GetModuleHandleW(L"user32.dll");
    const auto function = reinterpret_cast<Function>(
        user != nullptr ? GetProcAddress(user, "AdjustWindowRectExForDpi") : nullptr);
    if (function != nullptr && function(&rect, style, FALSE, extendedStyle, dpi)) return;
    (void)AdjustWindowRectEx(&rect, style, FALSE, extendedStyle);
}

class WindowState final {
public:
    WindowState(profile::SeatConfigDocument seatDocument,
                std::vector<plan::GameRuntimeRequirement> requirementSnapshot,
                LauncherActivate activationBoundary,
                std::optional<std::string> resumedGameId)
        : seats(std::move(seatDocument)),
          requirements(std::move(requirementSnapshot)),
          activate(std::move(activationBoundary)),
          steam(provider::steam::makeNativeSteamMetadataSource()),
          focusedGameId(std::move(resumedGameId)) {}

    bool loadPlayerProfilesFromDisk(profile::PlayerProfileDocument& output) {
        launcherUserStateRoot = launcher_state::defaultUserStateRoot();
        if (!launcherUserStateRoot) {
            output = {};
            playerProfilesWritable = false;
            return false;
        }
        const auto loaded = launcher_state::loadPlayerProfiles(*launcherUserStateRoot, output);
        playerProfilesWritable = loaded.succeeded();
        return loaded.succeeded();
    }

    bool savePlayerProfilesToDisk(const profile::PlayerProfileDocument& document) {
        if (!playerProfilesWritable || !launcherUserStateRoot) return false;
        return launcher_state::savePlayerProfiles(*launcherUserStateRoot, document).succeeded();
    }

    bool loadManualGamesFromDisk() {
        manualGameRecords = {};
        const auto path = manualGamesPath();
        if (!path) {
            manualGamesWritable = false;
            return false;
        }

        std::error_code error;
        const bool exists = std::filesystem::exists(*path, error);
        if (error) {
            manualGamesWritable = false;
            return false;
        }
        if (!exists) {
            manualGamesWritable = true;
            return true;
        }
        const auto byteCount = std::filesystem::file_size(*path, error);
        if (error || byteCount == 0u || byteCount > profile::kMaximumSchemaDocumentBytes) {
            manualGamesWritable = false;
            return false;
        }
        std::ifstream input(*path, std::ios::binary);
        if (!input) {
            manualGamesWritable = false;
            return false;
        }
        std::string json(static_cast<std::size_t>(byteCount), '\0');
        input.read(json.data(), static_cast<std::streamsize>(json.size()));
        if (!input || input.gcount() != static_cast<std::streamsize>(json.size())) {
            manualGamesWritable = false;
            return false;
        }
        char trailing = '\0';
        if (input.get(trailing)) {
            manualGamesWritable = false;
            return false;
        }

        profile::GameRecordDocument stored;
        const auto decoded = profile::decodeGameRecordDocument(json, stored);
        if (!decoded.succeeded() ||
            stored.games.size() + 1u > kMaximumUiProviders ||
            std::any_of(stored.games.begin(), stored.games.end(), [](const auto& game) {
                return game.providerId != "custom" || game.origin != profile::GameOrigin::Manual ||
                       !game.providerAppId || game.executableCandidates.empty();
            })) {
            manualGamesWritable = false;
            return false;
        }

        std::vector<std::unique_ptr<provider::custom::CustomExecutableProviderAdapter>>
            loadedAdapters;
        std::vector<catalog::GameCatalogCandidate> loadedCandidates;
        for (const auto& game : stored.games) {
            provider::custom::CustomExecutableDefinition definition;
            definition.title = game.title;
            definition.executablePath = game.executableCandidates.front();
            if (!game.installRoot.empty()) definition.workingDirectory = game.installRoot;
            auto adapter = std::make_unique<provider::custom::CustomExecutableProviderAdapter>(
                provider::custom::makeNativeCustomExecutableSource(), std::move(definition));
            const auto refreshed = adapter->refresh();
            if (!refreshed.succeeded()) continue;
            std::vector<catalog::GameCatalogCandidate> discovered;
            const auto discovery = provider::discoverInstalledGames(*adapter, discovered);
            if (!discovery.succeeded() || discovered.size() != 1u ||
                !discovered.front().providerAppId ||
                discovered.front().providerAppId != game.providerAppId) {
                continue;
            }
            loadedCandidates.push_back(discovered.front());
            loadedAdapters.push_back(std::move(adapter));
        }

        manualGameRecords = std::move(stored);
        manualGamesWritable = true;
        for (std::size_t index = 0; index < loadedAdapters.size(); ++index) {
            providers.push_back(
                {"custom", loadedAdapters[index].get(), *loadedCandidates[index].providerAppId});
            customAdapters.push_back(std::move(loadedAdapters[index]));
        }
        customCandidates.insert(customCandidates.end(),
                                loadedCandidates.begin(), loadedCandidates.end());
        return true;
    }

    bool saveManualGamesToDisk(const profile::GameRecordDocument& document) {
        if (!manualGamesWritable) return false;
        const auto path = manualGamesPath();
        if (!path) return false;
        profile::SchemaDiagnostic diagnostic;
        const std::string json = profile::encodeGameRecordDocument(document, &diagnostic);
        if (!diagnostic.succeeded() || json.empty() ||
            json.size() > profile::kMaximumSchemaDocumentBytes) {
            return false;
        }
        std::error_code error;
        std::filesystem::create_directories(path->parent_path(), error);
        if (error) return false;
        auto staging = *path;
        staging += L".tmp";
        {
            std::ofstream output(staging, std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output.write(json.data(), static_cast<std::streamsize>(json.size()));
            output.flush();
            if (!output) {
                output.close();
                std::filesystem::remove(staging, error);
                return false;
            }
        }
        if (MoveFileExW(staging.c_str(), path->c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
            std::filesystem::remove(staging, error);
            return false;
        }
        return true;
    }

    bool initialize(HWND value) {
        hwnd = value;
        providers.push_back({"steam", &steam});
        refreshSteam();
        const bool manualGamesLoaded = loadManualGamesFromDisk();
        if (!rebuildLibrary()) return false;

        profile::PlayerProfileDocument playerProfiles;
        const bool playerProfilesLoaded = loadPlayerProfilesFromDisk(playerProfiles);
        std::vector<PlayerPresentation> playerPresentation;
        playerPresentation.reserve(playerProfiles.players.size());
        for (const auto& player : playerProfiles.players) {
            playerPresentation.push_back({player.playerId, std::nullopt, {}, std::nullopt});
        }
        const auto initialized = model.initializeShared(
            seats, library, std::move(playerProfiles), std::move(playerPresentation), {},
            providers, requirements);
        if (!initialized.succeeded()) {
            lastDiagnostic = widen(initialized.message);
            return false;
        }
        if (!manualGamesLoaded) {
            lastDiagnostic = t(hydra::ui::TextId::ManualGamesLoadFailed);
        }
        if (!playerProfilesLoaded) {
            lastDiagnostic = t(hydra::ui::TextId::PlayerProfilesLoadFailed);
        }
        createControls();
        refreshControls();

        if (launcherUserStateRoot) {
            std::optional<launcher_state::LastPlayerSelection> storedSelection;
            const auto loadedSelection = launcher_state::loadLastPlayerSelection(
                *launcherUserStateRoot, storedSelection);
            launcher_state::FilteredLastPlayerSelection filteredSelection;
            if (loadedSelection.succeeded() &&
                launcher_state::filterLastPlayerSelection(
                    storedSelection, model.players(), filteredSelection).succeeded() &&
                filteredSelection.selection) {
                selectPlayerInCombo(seat1Player, filteredSelection.selection->player1Id);
                if (filteredSelection.selection->player2Id) {
                    selectPlayerInCombo(seat2Player, *filteredSelection.selection->player2Id);
                }
            }
        }
        // Existing users who created exactly one Player before selection persistence
        // existed should not reopen into a misleading "Choose Player" state.
        if (!selectedPlayerId(seat1Player) && model.players().players.size() == 1u) {
            selectPlayerInCombo(seat1Player, model.players().players.front().playerId);
        }
        applyPrimarySelection();
        applyLayout();
        return true;
    }

    void refreshSteam() {
        steamCandidates.clear();
        const auto refreshed = steam.refresh();
        if (refreshed.succeeded()) {
            const auto discovered = provider::discoverInstalledGames(steam, steamCandidates);
            lastDiagnostic = discovered.succeeded()
                                 ? std::wstring(t(hydra::ui::TextId::SteamRefreshed))
                                 : widen(discovered.message);
        } else {
            lastDiagnostic = widen(refreshed.message);
        }
    }

    bool rebuildLibrary() {
        std::vector<catalog::GameCatalogCandidate> candidates = steamCandidates;
        candidates.insert(candidates.end(), customCandidates.begin(), customCandidates.end());
        catalog::LocalGameCatalog candidate;
        const auto diagnostic = catalog::buildLocalGameCatalog(candidates, candidate);
        if (!diagnostic.succeeded()) {
            lastDiagnostic = widen(diagnostic.message);
            return false;
        }
        library = std::move(candidate);
        return true;
    }

    void createControls() {
        const auto seatOne = std::wstring(t(hydra::ui::TextId::Player)) + L" 1";
        const auto seatTwo = std::wstring(t(hydra::ui::TextId::Player)) + L" 2";

        playerNameLabel = createPrimary(L"STATIC", t(hydra::ui::TextId::PlayerName), SS_LEFT);
        playerName = createPrimary(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                   kPlayerName);
        addPlayerButton = createPrimary(L"BUTTON", t(hydra::ui::TextId::AddPlayer),
                                        BS_PUSHBUTTON | WS_TABSTOP, kAddPlayer);
        configureButton = createPrimary(L"BUTTON", t(hydra::ui::TextId::SeatHardwareSetup),
                                        BS_PUSHBUTTON | WS_TABSTOP, kConfigure);

        seat1Label = createPrimary(L"STATIC", seatOne.c_str(), SS_LEFT | SS_NOPREFIX);
        seat1Player = createPrimary(L"COMBOBOX", L"",
                                    CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kSeat1Player);
        seat1Status = createPrimary(L"STATIC", t(hydra::ui::TextId::StatusNotSelected),
                                    SS_LEFT | SS_NOPREFIX, kSeat1Status);
        seat2Label = createPrimary(L"STATIC", seatTwo.c_str(), SS_LEFT | SS_NOPREFIX);
        seat2Player = createPrimary(L"COMBOBOX", L"",
                                    CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kSeat2Player);
        seat2Status = createPrimary(L"STATIC", t(hydra::ui::TextId::StatusNotSelected),
                                    SS_LEFT | SS_NOPREFIX, kSeat2Status);

        libraryLabel = createPrimary(L"STATIC", t(hydra::ui::TextId::SectionLibrary), SS_LEFT);
        gameList = createPrimary(L"LISTBOX", L"",
                                 LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP,
                                 kGameList);
        refreshButton = createPrimary(L"BUTTON", t(hydra::ui::TextId::Refresh),
                                      BS_PUSHBUTTON | WS_TABSTOP, kRefresh);
        addExeButton = createPrimary(L"BUTTON", t(hydra::ui::TextId::AddExecutable),
                                     BS_PUSHBUTTON | WS_TABSTOP, kAddExe);
        launchReason = createPrimary(L"STATIC", t(hydra::ui::TextId::NeedsSetup),
                                     SS_LEFT | SS_NOPREFIX);
        playButton = createPrimary(L"BUTTON", t(hydra::ui::TextId::Play),
                                   BS_DEFPUSHBUTTON | WS_TABSTOP, kPlay);
        EnableWindow(playButton, FALSE);
    }

    HWND createPrimary(const wchar_t* className, const wchar_t* label, DWORD style,
                       int id = 0) {
        return create(className, label, style, 0, 0, 1, 1, id);
    }

    void applyLayout() {
        RECT client{};
        GetClientRect(hwnd, &client);
        const UINT dpi = dpiForWindowCompat(hwnd);
        const auto px = [dpi](int logical) { return MulDiv(logical, static_cast<int>(dpi), 96); };
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        const int margin = px(12);
        const int gap = px(8);
        const int row = px(28);
        const int labelWidth = px(64);
        const int statusWidth = px(150);
        const int buttonWidth = px(120);

        int y = margin;
        MoveWindow(playerNameLabel, margin, y + px(5), px(90), row, TRUE);
        MoveWindow(playerName, margin + px(92), y, px(220), row, TRUE);
        MoveWindow(addPlayerButton, margin + px(320), y, buttonWidth, row, TRUE);
        MoveWindow(configureButton, width - margin - px(150), y, px(150), row, TRUE);

        y += row + gap;
        const int comboWidth = std::max(px(160), width - margin * 2 - labelWidth - statusWidth - gap * 2);
        MoveWindow(seat1Label, margin, y + px(5), labelWidth, row, TRUE);
        MoveWindow(seat1Player, margin + labelWidth + gap, y, comboWidth, px(160), TRUE);
        MoveWindow(seat1Status, margin + labelWidth + gap + comboWidth + gap, y + px(5),
                   statusWidth, row, TRUE);

        y += row + gap;
        MoveWindow(seat2Label, margin, y + px(5), labelWidth, row, TRUE);
        MoveWindow(seat2Player, margin + labelWidth + gap, y, comboWidth, px(160), TRUE);
        MoveWindow(seat2Status, margin + labelWidth + gap + comboWidth + gap, y + px(5),
                   statusWidth, row, TRUE);

        y += row + gap;
        MoveWindow(libraryLabel, margin, y + px(5), px(120), row, TRUE);
        MoveWindow(refreshButton, width - margin - px(248), y, px(110), row, TRUE);
        MoveWindow(addExeButton, width - margin - px(130), y, px(130), row, TRUE);

        y += row + gap;
        const int footerHeight = px(70);
        const int listHeight = std::max(px(120), height - y - footerHeight - margin);
        MoveWindow(gameList, margin, y, std::max(px(200), width - margin * 2), listHeight, TRUE);

        const int footerY = y + listHeight + gap;
        MoveWindow(launchReason, margin, footerY + px(4),
                   std::max(px(180), width - margin * 2 - px(150)), px(48), TRUE);
        MoveWindow(playButton, width - margin - px(140), footerY, px(140), px(40), TRUE);
    }

    const wchar_t* t(hydra::ui::TextId id) const noexcept {
        return hydra::ui::text(id, locale).data();
    }

    HWND create(const wchar_t* className, const wchar_t* text, DWORD style,
                int x, int y, int width, int height, int id = 0) {
        HWND control = CreateWindowExW(
            0, className, text, WS_CHILD | WS_VISIBLE | style,
            x, y, width, height, hwnd,
            id == 0 ? nullptr : reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT,
                     reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return control;
    }

    void fillPlayerCombo(HWND combo) {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        const auto* placeholder = combo == seat2Player
            ? t(hydra::ui::TextId::None)
            : t(hydra::ui::TextId::ChoosePlayer);
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(placeholder));
        for (const auto& player : model.players().players) {
            SendMessageW(combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(player.displayName.c_str()));
        }
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }

    const catalog::LocalGameCatalogEntry* selectedLibraryGame() const {
        if (gameList == nullptr) return nullptr;
        const auto selected = SendMessageW(gameList, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR || selected < 0 ||
            static_cast<std::size_t>(selected) >= model.library().entries.size()) {
            return nullptr;
        }
        return &model.library().entries[static_cast<std::size_t>(selected)];
    }

    const profile::RuntimeBinding* bindingForSeat(SeatId seatId) const {
        const auto found = std::find_if(
            model.selection().bindings.begin(), model.selection().bindings.end(),
            [&](const auto& binding) { return binding.seatId == seatId; });
        return found == model.selection().bindings.end() ? nullptr : &*found;
    }

    std::optional<std::string> selectedPlayerId(HWND combo) const {
        const auto selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (selected <= 0 ||
            static_cast<std::size_t>(selected - 1) >= model.players().players.size()) {
            return std::nullopt;
        }
        return model.players().players[static_cast<std::size_t>(selected - 1)].playerId;
    }

    bool persistPlayerSelection(const std::optional<std::string>& firstPlayer,
                                const std::optional<std::string>& secondPlayer) {
        if (!launcherUserStateRoot) return false;
        if (!firstPlayer) {
            return launcher_state::clearLastPlayerSelection(*launcherUserStateRoot).succeeded();
        }
        launcher_state::LastPlayerSelection selection;
        selection.player1Id = *firstPlayer;
        selection.player2Id = secondPlayer;
        return launcher_state::saveLastPlayerSelection(
            *launcherUserStateRoot, selection).succeeded();
    }

    void applyPrimarySelection() {
        const auto* selectedGame = selectedLibraryGame();
        const auto firstPlayer = selectedPlayerId(seat1Player);
        auto secondPlayer = selectedPlayerId(seat2Player);

        const auto apply = [&](const UiDiagnostic& diagnostic) {
            if (!diagnostic.succeeded()) {
                lastDiagnostic = widen(diagnostic.message);
                return false;
            }
            return true;
        };

        // Player 1 is the required primary identity. Player 2 is optional and must
        // never form a standalone Seat 2 plan or persist without Player 1.
        if (!firstPlayer) {
            if (!apply(model.clearSeat(1u))) return;
            if (!apply(model.clearSeat(2u))) return;
            if (secondPlayer) {
                SendMessageW(seat2Player, CB_SETCURSEL, 0, 0);
                secondPlayer.reset();
            }
            (void)persistPlayerSelection(std::nullopt, std::nullopt);
            lastDiagnostic = t(hydra::ui::TextId::LaunchSelectionEmpty);
            updatePreview();
            return;
        }

        if (selectedGame == nullptr) {
            if (!apply(model.clearSeat(1u))) return;
            if (!apply(model.clearSeat(2u))) return;
            lastDiagnostic.clear();
            if (!persistPlayerSelection(firstPlayer, secondPlayer)) {
                lastDiagnostic = t(hydra::ui::TextId::PlayerProfilesSaveFailed);
            }
            // Player choice is independent of game choice. Do not project the now-empty
            // runtime binding back into the combo boxes or the user's selection vanishes.
            updatePreview();
            return;
        }

        if (secondPlayer) {
            if (!apply(model.selectBoth(selectedGame->game.gameId,
                                        *firstPlayer, *secondPlayer))) {
                syncSeatPresentation();
                updatePreview();
                return;
            }
        } else {
            if (!apply(model.selectGame(1u, *firstPlayer, selectedGame->game.gameId))) {
                syncSeatPresentation();
                updatePreview();
                return;
            }
            if (!apply(model.clearSeat(2u))) return;
        }

        lastDiagnostic.clear();
        syncSeatPresentation();
        if (!persistPlayerSelection(firstPlayer, secondPlayer)) {
            lastDiagnostic = t(hydra::ui::TextId::PlayerProfilesSaveFailed);
        }
        updatePreview();
    }

    bool hasRuntimeRequirement(std::string_view gameId) const {
        return std::any_of(
            model.requirements().begin(), model.requirements().end(),
            [&](const auto& requirement) { return requirement.gameId == gameId; });
    }

    const plan::ProviderAdapterBinding* exactProviderBinding(
        const requirement::RequirementResolveInputs& inputs,
        const catalog::LocalGameCatalogEntry& game) const {
        const plan::ProviderAdapterBinding* exact = nullptr;
        const plan::ProviderAdapterBinding* providerWide = nullptr;
        for (const auto& binding : inputs.providers) {
            if (binding.adapter == nullptr || binding.providerId != game.game.providerId) continue;
            if (binding.providerAppId) {
                if (binding.providerAppId != game.game.providerAppId || exact != nullptr) {
                    if (binding.providerAppId == game.game.providerAppId) return nullptr;
                    continue;
                }
                exact = &binding;
            } else {
                if (providerWide != nullptr) return nullptr;
                providerWide = &binding;
            }
        }
        return exact != nullptr ? exact : providerWide;
    }

    std::optional<std::string> playerAccountReference(
        std::string_view playerId, std::string_view providerId) const {
        const auto player = std::find_if(
            model.players().players.begin(), model.players().players.end(),
            [&](const auto& candidate) { return candidate.playerId == playerId; });
        if (player == model.players().players.end()) return std::nullopt;
        const auto account = std::find_if(
            player->providerAccounts.begin(), player->providerAccounts.end(),
            [&](const auto& candidate) { return candidate.providerId == providerId; });
        return account == player->providerAccounts.end()
            ? std::nullopt
            : std::optional<std::string>{account->accountRef};
    }

    const profile::PersistedSeatConfig* configuredSeat(SeatId seatId) const {
        const auto found = std::find_if(
            seats.seats.begin(), seats.seats.end(),
            [&](const auto& seat) { return seat.seatId == seatId; });
        return found == seats.seats.end() ? nullptr : &*found;
    }

    launch::Requirements proposedRequirements(std::string_view gameId) const {
        launch::Requirements reviewed;
        reviewed.display = true;
        reviewed.windowOwnership = true;
        reviewed.recovery = true;
        reviewed.highRisk = false;

        std::vector<const profile::PersistedSeatConfig*> selectedSeats;
        for (const auto& binding : model.selection().bindings) {
            if (binding.gameId != gameId) continue;
            if (const auto* seat = configuredSeat(binding.seatId); seat != nullptr) {
                selectedSeats.push_back(seat);
            }
        }
        if (selectedSeats.empty()) return reviewed;

        const auto everySeat = [&](const auto& predicate) {
            return std::all_of(selectedSeats.begin(), selectedSeats.end(), predicate);
        };
        reviewed.keyboard = everySeat([](const auto* seat) {
            return !seat->keyboardIds.empty();
        });
        reviewed.mouse = everySeat([](const auto* seat) {
            return !seat->mouseIds.empty();
        });
        reviewed.controller = everySeat([](const auto* seat) {
            return !seat->controllerIds.empty();
        });
        reviewed.audioOutput = everySeat([](const auto* seat) {
            return seat->audioOutputEndpointId.has_value();
        });
        return reviewed;
    }

    std::wstring requirementSummary(const launch::Requirements& reviewed) const {
        std::wstring summary;
        const auto append = [&](bool required, std::wstring_view label) {
            summary.append(required ? L"[x] " : L"[ ] ");
            summary.append(label);
            summary.push_back(L'\n');
        };
        append(reviewed.display, t(hydra::ui::TextId::DeviceDisplay));
        append(reviewed.keyboard, t(hydra::ui::TextId::DeviceKeyboard));
        append(reviewed.mouse, t(hydra::ui::TextId::DeviceMouse));
        append(reviewed.controller, t(hydra::ui::TextId::DeviceController));
        append(reviewed.audioOutput,
               t(hydra::ui::TextId::CompatibilityRequirementAudioOutput));
        append(reviewed.windowOwnership,
               t(hydra::ui::TextId::CompatibilityRequirementWindowOwnership));
        append(reviewed.recovery, t(hydra::ui::TextId::RecoveryAction));
        return summary;
    }

    bool refreshTrustedRequirementProjection() {
        auto source = requirement::makeDefaultProductionTrustedRequirementSource();
        std::vector<plan::GameRuntimeRequirement> projection;
        requirement::RequirementSnapshotDiagnostic resolved;
        if (source) {
            resolved = requirement::resolveCurrentRequirementProjection(*source, projection);
        } else {
            resolved.code = requirement::RequirementSnapshotCode::InputUnavailable;
            resolved.message = "trusted runtime requirement source is unavailable";
        }

        requirements = projection;
        const auto replaced = model.replaceRequirements(requirements);
        if (!replaced.succeeded()) {
            requirements.clear();
            const auto cleared = model.replaceRequirements(requirements);
            (void)cleared;
            lastDiagnostic = widen(replaced.message);
            refreshControls();
            return false;
        }
        if (!resolved.succeeded()) lastDiagnostic = widen(resolved.message);
        refreshControls();
        return resolved.succeeded();
    }

    void showCompatibilityFailure(std::string_view message) {
        lastDiagnostic = widen(message);
        const std::wstring text = lastDiagnostic.empty()
            ? t(hydra::ui::TextId::RuntimeLaunchUnavailable)
            : lastDiagnostic;
        MessageBoxW(hwnd, text.c_str(), t(hydra::ui::TextId::SectionCompatibility),
                    MB_OK | MB_ICONWARNING);
    }

    void showLocalizedCompatibilityFailure(hydra::ui::TextId id) {
        lastDiagnostic = t(id);
        MessageBoxW(hwnd, lastDiagnostic.c_str(),
                    t(hydra::ui::TextId::SectionCompatibility),
                    MB_OK | MB_ICONWARNING);
    }

    local_compatibility::LocalCompatibilityTargetRisk reviewCompatibilityTargetRisk() {
        const int reviewed = MessageBoxW(
            hwnd, t(hydra::ui::TextId::CompatibilityRiskReviewPrompt),
            t(hydra::ui::TextId::SectionCompatibility),
            MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON3);
        if (reviewed == IDNO) {
            return local_compatibility::LocalCompatibilityTargetRisk::Standard;
        }
        if (reviewed == IDYES) {
            return local_compatibility::LocalCompatibilityTargetRisk::ProtectedOrExperimental;
        }
        return local_compatibility::LocalCompatibilityTargetRisk::Unknown;
    }

    bool selectPhysicalEvidenceManifest(std::string_view gameId) {
        wchar_t path[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd;
        dialog.lpstrFilter = L"P3-HW manifest (phase3-hardware-manifest.json)\0phase3-hardware-manifest.json\0JSON files (*.json)\0*.json\0\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = static_cast<DWORD>(std::size(path));
        dialog.lpstrTitle = t(hydra::ui::TextId::PhysicalEvidenceDialogTitle);
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog)) return false;

        const auto saved = production::saveDefaultProductionPhysicalEvidenceSelection(
            std::filesystem::path(path));
        if (saved.code != production::PhysicalEvidenceSelectionCode::Success) {
            const auto message = hydra::ui::formatOne(
                hydra::ui::TextId::PhysicalEvidenceSelectionFailed, locale,
                widen(saved.message));
            MessageBoxW(hwnd, message.c_str(),
                        t(hydra::ui::TextId::SectionCompatibility),
                        MB_OK | MB_ICONWARNING);
            return false;
        }

        const auto prerequisites =
            production::checkDefaultProductionInputAuthorityPrerequisites(gameId);
        const auto messageId = prerequisites.code ==
                    production::ProductionInputAuthorityPrerequisiteCode::Ready
            ? hydra::ui::TextId::PhysicalEvidencePrerequisitesReady
            : hydra::ui::TextId::PhysicalEvidenceAcceptedNoProfile;
        MessageBoxW(hwnd, t(messageId),
                    t(hydra::ui::TextId::SectionCompatibility),
                    MB_OK | MB_ICONINFORMATION);
        return true;
    }

    void offerPhysicalEvidenceSelection(std::string_view gameId) {
        const auto prerequisites =
            production::checkDefaultProductionInputAuthorityPrerequisites(gameId);
        switch (prerequisites.code) {
        case production::ProductionInputAuthorityPrerequisiteCode::MissingPhysicalEvidence:
            if (MessageBoxW(hwnd, t(hydra::ui::TextId::PhysicalEvidenceSelectPrompt),
                            t(hydra::ui::TextId::SectionCompatibility),
                            MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2) == IDYES) {
                (void)selectPhysicalEvidenceManifest(gameId);
            }
            return;
        case production::ProductionInputAuthorityPrerequisiteCode::InvalidPhysicalEvidence: {
            const auto failure = hydra::ui::formatOne(
                hydra::ui::TextId::PhysicalEvidenceSelectionFailed, locale,
                widen(prerequisites.message));
            MessageBoxW(hwnd, failure.c_str(),
                        t(hydra::ui::TextId::SectionCompatibility),
                        MB_OK | MB_ICONWARNING);
            if (MessageBoxW(hwnd, t(hydra::ui::TextId::PhysicalEvidenceSelectPrompt),
                            t(hydra::ui::TextId::SectionCompatibility),
                            MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2) == IDYES) {
                (void)selectPhysicalEvidenceManifest(gameId);
            }
            return;
        }
        case production::ProductionInputAuthorityPrerequisiteCode::MissingTrustedGameProfile:
            MessageBoxW(hwnd, t(hydra::ui::TextId::PhysicalEvidenceAcceptedNoProfile),
                        t(hydra::ui::TextId::SectionCompatibility),
                        MB_OK | MB_ICONINFORMATION);
            return;
        case production::ProductionInputAuthorityPrerequisiteCode::Ready:
            MessageBoxW(hwnd, t(hydra::ui::TextId::PhysicalEvidencePrerequisitesReady),
                        t(hydra::ui::TextId::SectionCompatibility),
                        MB_OK | MB_ICONINFORMATION);
            return;
        }
    }

    void runSelectedCompatibilityCheck() {
        applyPrimarySelection();
        const auto* selectedUiGame = selectedLibraryGame();
        const auto firstPlayer = selectedPlayerId(seat1Player);
        if (selectedUiGame == nullptr || !firstPlayer) {
            lastDiagnostic = t(hydra::ui::TextId::LaunchSelectionEmpty);
            updatePreview();
            return;
        }

        const auto targetRisk = reviewCompatibilityTargetRisk();
        if (targetRisk ==
            local_compatibility::LocalCompatibilityTargetRisk::ProtectedOrExperimental) {
            showLocalizedCompatibilityFailure(
                hydra::ui::TextId::CompatibilityRiskProtectedBlocked);
            updatePreview();
            return;
        }
        if (targetRisk == local_compatibility::LocalCompatibilityTargetRisk::Unknown) {
            showLocalizedCompatibilityFailure(
                hydra::ui::TextId::CompatibilityRiskUnknownBlocked);
            updatePreview();
            return;
        }

        SetWindowTextW(launchReason, t(hydra::ui::TextId::CompatibilityCheckRunning));
        EnableWindow(playButton, FALSE);
        UpdateWindow(hwnd);

        auto inputSource = requirement::makeProductionRequirementResolveInputSource();
        requirement::RequirementResolveInputs inputs;
        std::string captureError;
        if (!inputSource || !inputSource->capture(inputs, captureError)) {
            showCompatibilityFailure(captureError.empty()
                ? "current production compatibility inputs are unavailable"
                : captureError);
            (void)refreshTrustedRequirementProjection();
            return;
        }

        const auto gameIt = std::find_if(
            inputs.catalog.entries.begin(), inputs.catalog.entries.end(),
            [&](const auto& entry) { return entry.game.gameId == selectedUiGame->game.gameId; });
        if (gameIt == inputs.catalog.entries.end()) {
            showCompatibilityFailure("the selected Game is no longer present in the current local catalog");
            (void)refreshTrustedRequirementProjection();
            return;
        }
        const auto* providerBinding = exactProviderBinding(inputs, *gameIt);
        if (providerBinding == nullptr || providerBinding->adapter == nullptr) {
            showCompatibilityFailure("the selected Game has no unique current provider binding");
            (void)refreshTrustedRequirementProjection();
            return;
        }
        const auto descriptor = providerBinding->adapter->descriptor();

        provider::LaunchSelection selection;
        selection.providerId = gameIt->game.providerId;
        selection.gameId = gameIt->game.gameId;
        selection.providerAppId = gameIt->game.providerAppId;
        selection.accountRef = playerAccountReference(*firstPlayer, gameIt->game.providerId);
        selection.expectedMetadataRevision = descriptor.metadataRevision;

        provider::ProviderLaunchRequest launchRequest;
        const auto launchBuilt = provider::buildLaunchRequest(
            *providerBinding->adapter, selection, launchRequest);
        if (!launchBuilt.succeeded()) {
            showCompatibilityFailure(launchBuilt.message);
            (void)refreshTrustedRequirementProjection();
            return;
        }
        if (launchRequest.targetKind != provider::LaunchTargetKind::Executable) {
            showCompatibilityFailure(
                "local compatibility validation requires an exact native executable target; add the executable directly for this Game");
            (void)refreshTrustedRequirementProjection();
            return;
        }

        SeatId checkSeatId = 0u;
        for (const auto& binding : model.selection().bindings) {
            if (binding.gameId == gameIt->game.gameId) {
                checkSeatId = binding.seatId;
                break;
            }
        }
        if (checkSeatId == 0u) {
            showCompatibilityFailure("the selected Game is not bound to a Player Seat");
            (void)refreshTrustedRequirementProjection();
            return;
        }

        local_compatibility::LocalCompatibilityRequest check;
        check.launch.seatId = checkSeatId;
        check.launch.executablePath = launchRequest.target;
        check.launch.arguments = launchRequest.arguments;
        if (launchRequest.workingDirectory) {
            check.launch.workingDirectory = *launchRequest.workingDirectory;
        } else {
            check.launch.workingDirectory = std::filesystem::path(launchRequest.target)
                                                .parent_path().wstring();
        }
        check.launch.containment = process::ProcessContainmentPolicy::RequireJobObject;
        check.launch.createNewConsole = false;
        check.planFingerprint = localCheckFingerprint(*gameIt, launchRequest.target);
        check.targetRisk = targetRisk;

        const auto run = local_compatibility::runLocalCompatibilityCheck(check);
        if (!run.diagnostic.succeeded() || !run.report) {
            showCompatibilityFailure(run.diagnostic.message.empty()
                ? "the bounded local compatibility check did not complete"
                : run.diagnostic.message);
            (void)refreshTrustedRequirementProjection();
            return;
        }

        compat::LocalEvidenceContext evidenceContext;
        evidenceContext.resultId = localResultId(check.planFingerprint);
        evidenceContext.timestampClass = compat::TimestampClass::MonthBucket;
        evidenceContext.timestampBucket = inputs.context.referenceMonth;
        evidenceContext.gameId = gameIt->game.gameId;
        evidenceContext.providerId = gameIt->game.providerId;
        evidenceContext.providerAppId = gameIt->game.providerAppId;
        if (gameIt->game.localVersion) {
            const auto converted = narrowUtf8(*gameIt->game.localVersion);
            if (!converted) {
                showCompatibilityFailure("the selected Game version cannot be represented as UTF-8 evidence identity");
                (void)refreshTrustedRequirementProjection();
                return;
            }
            evidenceContext.gameVersion = *converted;
        }
        evidenceContext.hydraSeatVersion = inputs.context.hydraSeatVersion;
        evidenceContext.hydraSeatBuild = inputs.context.hydraSeatBuild;
        evidenceContext.windowsBuildClass = inputs.context.windowsBuildClass;
        evidenceContext.architecture = inputs.context.architecture;
        evidenceContext.scenario = model.selection().bindings.size() > 1u
            ? compat::Scenario::SameGameTwoInstance
            : compat::Scenario::DifferentGames;
        evidenceContext.protectedExperimental = false;
        evidenceContext.provenanceId = "hydraseat-guided-local-check";
        evidenceContext.provenanceRevision = 1u;

        const compat::LocalCompatibilityEvidenceWriteResult written =
            compat::writeDefaultLocalCompatibilityEvidence(evidenceContext, *run.report);
        if (!written.succeeded() || !written.result) {
            showCompatibilityFailure(written.diagnostic.message.empty()
                ? "the local compatibility result could not be persisted"
                : written.diagnostic.message);
            (void)refreshTrustedRequirementProjection();
            return;
        }

        const auto reviewed = proposedRequirements(gameIt->game.gameId);
        const auto prompt = hydra::ui::formatOne(
            hydra::ui::TextId::CompatibilityReviewPrompt, locale,
            requirementSummary(reviewed));
        const int accepted = MessageBoxW(
            hwnd, prompt.c_str(), t(hydra::ui::TextId::SectionCompatibility),
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (accepted == IDYES) {
            requirement::RuntimeRequirementAuthorityReview review;
            review.game = *gameIt;
            review.provider = descriptor;
            review.evidence = *written.result;
            review.report = *run.report;
            review.requirements = reviewed;
            const auto published =
                requirement::publishRuntimeRequirementAuthorityToDefaultStore(review);
            if (!published.succeeded()) {
                lastDiagnostic = widen(published.message);
            }
        }

        (void)refreshTrustedRequirementProjection();
        if (!hasRuntimeRequirement(gameIt->game.gameId)) {
            offerPhysicalEvidenceSelection(gameIt->game.gameId);
        }
    }

    void refreshControls() {
        const auto previousSeat1Player = selectedPlayerId(seat1Player);
        const auto previousSeat2Player = selectedPlayerId(seat2Player);

        SendMessageW(gameList, LB_RESETCONTENT, 0, 0);
        int focusedIndex = -1;
        for (std::size_t index = 0; index < model.library().entries.size(); ++index) {
            const auto& entry = model.library().entries[index];
            SendMessageW(gameList, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(entry.game.title.c_str()));
            if (focusedGameId && entry.game.gameId == *focusedGameId) {
                focusedIndex = static_cast<int>(index);
            }
        }
        if (focusedIndex >= 0) {
            SendMessageW(gameList, LB_SETCURSEL, focusedIndex, 0);
        } else {
            focusedGameId.reset();
            SendMessageW(gameList, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        }

        fillPlayerCombo(seat1Player);
        fillPlayerCombo(seat2Player);
        if (previousSeat1Player) selectPlayerInCombo(seat1Player, *previousSeat1Player);
        if (previousSeat2Player) selectPlayerInCombo(seat2Player, *previousSeat2Player);
        syncSeatPresentation();
        updatePreview();
    }

    bool seatReady(SeatId seatId) const {
        if (bindingForSeat(seatId) == nullptr || !currentPreview.compileResult.plan) {
            return false;
        }
        const auto& seats = currentPreview.compileResult.plan->seats;
        const bool included = std::any_of(
            seats.begin(), seats.end(),
            [&](const auto& seat) { return seat.seatId == seatId; });
        if (!included) return false;
        return std::none_of(
            currentPreview.summary.messages.begin(), currentPreview.summary.messages.end(),
            [&](const auto& message) {
                return message.severity == preflight::Severity::Blocking &&
                       (message.seatId == 0u || message.seatId == seatId);
            });
    }

    void updateStatus() {
        const auto* selectedGame = selectedLibraryGame();
        if (selectedGame == nullptr) {
            focusedGameId.reset();
        } else {
            focusedGameId = selectedGame->game.gameId;
        }

        const bool canActivate = currentPreview.summary.canActivate &&
                                 currentPreview.compileResult.plan.has_value();
        const auto* first = bindingForSeat(1u);
        const auto* second = bindingForSeat(2u);
        const auto firstPlayerChoice = selectedPlayerId(seat1Player);
        const auto secondPlayerChoice = selectedPlayerId(seat2Player);
        SetWindowTextW(seat1Status,
            first == nullptr
                ? (firstPlayerChoice ? t(hydra::ui::TextId::StatusNotSelected)
                                     : t(hydra::ui::TextId::ChoosePlayer))
                : (seatReady(1u) ? t(hydra::ui::TextId::StatusReady)
                                  : t(hydra::ui::TextId::StatusNeedsAttention)));
        SetWindowTextW(seat2Status,
            second == nullptr
                ? (secondPlayerChoice ? t(hydra::ui::TextId::StatusNotSelected)
                                      : t(hydra::ui::TextId::None))
                : (seatReady(2u) ? t(hydra::ui::TextId::StatusReady)
                                  : t(hydra::ui::TextId::StatusNeedsAttention)));

        if (model.players().players.empty()) {
            SetWindowTextW(launchReason, t(hydra::ui::TextId::AddPlayerToContinue));
        } else if (!firstPlayerChoice) {
            SetWindowTextW(launchReason, t(hydra::ui::TextId::ChoosePlayer));
        } else if (selectedGame == nullptr) {
            SetWindowTextW(launchReason, t(hydra::ui::TextId::NoGameSelected));
        } else if (model.selection().bindings.empty()) {
            SetWindowTextW(launchReason, t(hydra::ui::TextId::LaunchSelectionEmpty));
        } else if (canActivate && !activate) {
            SetWindowTextW(launchReason, t(hydra::ui::TextId::RuntimeLaunchUnavailable));
        } else if (canActivate) {
            SetWindowTextW(launchReason, t(hydra::ui::TextId::ReadyToPlay));
        } else if (!currentPreview.summary.messages.empty()) {
            const auto blocking = std::find_if(
                currentPreview.summary.messages.begin(), currentPreview.summary.messages.end(),
                [](const auto& message) {
                    return message.severity == preflight::Severity::Blocking;
                });
            const auto& message = blocking == currentPreview.summary.messages.end()
                ? currentPreview.summary.messages.front() : *blocking;
            const auto localized = hydra::ui::preflightText(message.code, locale);
            const auto reason = localized.empty() ? widen(message.userMessage)
                                                  : std::wstring(localized);
            SetWindowTextW(launchReason, reason.c_str());
        } else {
            SetWindowTextW(launchReason, t(hydra::ui::TextId::NeedsSetup));
        }
    }

    void syncSeatPresentation() {
        for (const auto& binding : model.selection().bindings) {
            HWND playerCombo = binding.seatId == 1u ? seat1Player : seat2Player;
            if (selectedPlayerId(playerCombo)) continue;
            const auto player = std::find_if(
                model.players().players.begin(), model.players().players.end(),
                [&](const auto& value) { return value.playerId == binding.playerId; });
            if (player != model.players().players.end()) {
                const auto offset = std::distance(model.players().players.begin(), player);
                SendMessageW(playerCombo, CB_SETCURSEL, offset + 1, 0);
            }
        }
    }

    void addPlayer() {
        const auto previousSeat1Player = selectedPlayerId(seat1Player);
        const auto previousSeat2Player = selectedPlayerId(seat2Player);
        wchar_t buffer[profile::kMaximumDisplayNameCodeUnits + 1u]{};
        GetWindowTextW(playerName, buffer, static_cast<int>(std::size(buffer)));
        std::string playerId;
        auto staged = model;
        const auto diagnostic = staged.createPlayer(
            buffer, std::string(hydra::ui::localeTag(locale)), std::nullopt, playerId);
        if (!diagnostic.succeeded()) {
            lastDiagnostic = widen(diagnostic.message);
            refreshControls();
            return;
        }
        if (!savePlayerProfilesToDisk(staged.players())) {
            lastDiagnostic = t(hydra::ui::TextId::PlayerProfilesSaveFailed);
            refreshControls();
            return;
        }
        model = std::move(staged);
        lastDiagnostic = t(hydra::ui::TextId::PlayerAdded);

        SetWindowTextW(playerName, L"");
        refreshControls();
        if (previousSeat1Player) {
            selectPlayerInCombo(seat1Player, *previousSeat1Player);
        } else {
            // A newly created Player should be immediately usable and visibly
            // selected instead of appearing to vanish into a separate settings page.
            selectPlayerInCombo(seat1Player, playerId);
        }
        if (previousSeat2Player) selectPlayerInCombo(seat2Player, *previousSeat2Player);
        applyPrimarySelection();
    }

    void addExecutable() {
        wchar_t path[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd;
        dialog.lpstrFilter = L"Windows applications (*.exe)\0*.exe\0\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = static_cast<DWORD>(std::size(path));
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog)) return;

        provider::custom::CustomExecutableDefinition definition;
        definition.executablePath = path;
        definition.title = std::filesystem::path(path).stem().wstring();
        definition.workingDirectory = std::filesystem::path(path).parent_path().wstring();
        auto adapter = std::make_unique<provider::custom::CustomExecutableProviderAdapter>(
            provider::custom::makeNativeCustomExecutableSource(), std::move(definition));
        const auto refreshed = adapter->refresh();
        if (!refreshed.succeeded()) {
            lastDiagnostic = widen(refreshed.message);
            updatePreview();
            return;
        }
        std::vector<catalog::GameCatalogCandidate> discovered;
        const auto discovery = provider::discoverInstalledGames(*adapter, discovered);
        if (!discovery.succeeded() || discovered.size() != 1u ||
            !discovered[0].providerAppId) {
            lastDiagnostic = discovery.succeeded()
                                 ? std::wstring(t(hydra::ui::TextId::ManualExecutableIdentityMissing))
                                 : widen(discovery.message);
            updatePreview();
            return;
        }
        const auto appId = *discovered[0].providerAppId;
        const bool alreadyActive = std::any_of(
            providers.begin(), providers.end(), [&](const auto& binding) {
                return binding.providerId == "custom" && binding.providerAppId &&
                       *binding.providerAppId == appId;
            });
        if (alreadyActive) {
            lastDiagnostic = t(hydra::ui::TextId::ExecutableAdded);
            refreshControls();
            return;
        }

        auto stagedCandidates = customCandidates;
        stagedCandidates.push_back(discovered[0]);
        std::vector<catalog::GameCatalogCandidate> allCandidates = steamCandidates;
        allCandidates.insert(allCandidates.end(), stagedCandidates.begin(), stagedCandidates.end());
        catalog::LocalGameCatalog stagedLibrary;
        const auto catalogDiagnostic = catalog::buildLocalGameCatalog(allCandidates, stagedLibrary);
        if (!catalogDiagnostic.succeeded()) {
            lastDiagnostic = widen(catalogDiagnostic.message);
            updatePreview();
            return;
        }

        const auto persistedEntry = std::find_if(
            stagedLibrary.entries.begin(), stagedLibrary.entries.end(), [&](const auto& entry) {
                return entry.game.providerId == "custom" && entry.game.providerAppId &&
                       *entry.game.providerAppId == appId;
            });
        if (persistedEntry == stagedLibrary.entries.end()) {
            lastDiagnostic = t(hydra::ui::TextId::ManualExecutableIdentityMissing);
            updatePreview();
            return;
        }

        auto stagedModel = model;
        const auto replaced = stagedModel.replaceLibrary(stagedLibrary);
        if (!replaced.succeeded()) {
            lastDiagnostic = widen(replaced.message);
            updatePreview();
            return;
        }
        const auto attached = stagedModel.attachProvider({"custom", adapter.get(), appId});
        if (!attached.succeeded()) {
            lastDiagnostic = widen(attached.message);
            updatePreview();
            return;
        }

        auto stagedRecords = manualGameRecords;
        const auto stored = std::find_if(
            stagedRecords.games.begin(), stagedRecords.games.end(), [&](const auto& game) {
                return game.providerId == "custom" && game.providerAppId &&
                       *game.providerAppId == appId;
            });
        if (stored == stagedRecords.games.end()) {
            stagedRecords.games.push_back(persistedEntry->game);
        } else {
            *stored = persistedEntry->game;
        }
        if (!saveManualGamesToDisk(stagedRecords)) {
            lastDiagnostic = t(hydra::ui::TextId::ManualGamesSaveFailed);
            updatePreview();
            return;
        }

        customCandidates = std::move(stagedCandidates);
        library = std::move(stagedLibrary);
        providers.push_back({"custom", adapter.get(), appId});
        customAdapters.push_back(std::move(adapter));
        manualGameRecords = std::move(stagedRecords);
        model = std::move(stagedModel);
        lastDiagnostic = t(hydra::ui::TextId::ExecutableAdded);
        refreshControls();
    }

    void selectPlayerInCombo(HWND combo, std::string_view playerId) const {
        const auto player = std::find_if(
            model.players().players.begin(), model.players().players.end(),
            [&](const auto& value) { return value.playerId == playerId; });
        if (player == model.players().players.end()) return;
        const auto offset = std::distance(model.players().players.begin(), player);
        SendMessageW(combo, CB_SETCURSEL, offset + 1, 0);
    }

    void updatePreview() {
        currentPreview = model.preview();
        const bool canActivate = currentPreview.summary.canActivate &&
                                 currentPreview.compileResult.plan.has_value();
        const bool canCheck = selectedLibraryGame() != nullptr &&
                              selectedPlayerId(seat1Player).has_value();
        SetWindowTextW(playButton, canActivate
            ? t(hydra::ui::TextId::Play)
            : t(hydra::ui::TextId::CheckCompatibility));
        EnableWindow(playButton,
                     canActivate ? (static_cast<bool>(activate) ? TRUE : FALSE)
                                 : (canCheck ? TRUE : FALSE));
        updateStatus();
    }

    void play() {
        updatePreview();
        if (!currentPreview.summary.canActivate || !currentPreview.compileResult.plan || !activate) {
            lastDiagnostic = t(hydra::ui::TextId::RuntimeLaunchUnavailable);
            updatePreview();
            return;
        }
        const auto result = activate(*currentPreview.compileResult.plan);
        if (!result.succeeded()) {
            lastDiagnostic = result.message.empty()
                ? std::wstring(t(hydra::ui::TextId::RuntimeLaunchUnavailable))
                : result.message;
            updatePreview();
            const auto userMessage = t(hydra::ui::TextId::RuntimeLaunchUnavailable);
            SetWindowTextW(launchReason, userMessage);
            applyLayout();
            MessageBoxW(hwnd, userMessage,
                        t(hydra::ui::TextId::PlayDialogTitle), MB_OK | MB_ICONERROR);
            return;
        }
        const auto recorded = model.recordActivatedPlan(*currentPreview.compileResult.plan);
        if (!recorded.succeeded()) {
            lastDiagnostic = widen(recorded.message);
            updatePreview();
            return;
        }
        lastDiagnostic = result.message.empty()
            ? std::wstring(t(hydra::ui::TextId::StatusStartingGame)) : result.message;
        const auto starting = t(hydra::ui::TextId::StatusStartingGame);
        SetWindowTextW(launchReason, starting);
        EnableWindow(playButton, FALSE);
    }

    void command(int id, int notification) {
        if (id == kRefresh && notification == BN_CLICKED) {
            refreshSteam();
            if (rebuildLibrary()) {
                const auto diagnostic = model.replaceLibrary(library);
                if (!diagnostic.succeeded()) lastDiagnostic = widen(diagnostic.message);
            }
            refreshControls();
        } else if (id == kGameList && notification == LBN_SELCHANGE) {
            applyPrimarySelection();
        } else if (id == kAddExe && notification == BN_CLICKED) {
            addExecutable();
        } else if (id == kAddPlayer && notification == BN_CLICKED) {
            addPlayer();
        } else if (id == kSeat1Player && notification == CBN_SELCHANGE) {
            applyPrimarySelection();
        } else if (id == kSeat2Player && notification == CBN_SELCHANGE) {
            applyPrimarySelection();
        } else if (id == kConfigure && notification == BN_CLICKED) {
            exitAction = LauncherExitAction::OpenHardwareSetup;
            DestroyWindow(hwnd);
        } else if (id == kPlay && notification == BN_CLICKED) {
            if (currentPreview.summary.canActivate && currentPreview.compileResult.plan) {
                play();
            } else {
                runSelectedCompatibilityCheck();
            }
        }
    }

    HWND hwnd{nullptr};
    HWND configureButton{nullptr};
    HWND seat1Label{nullptr};
    HWND seat1Status{nullptr};
    HWND seat2Label{nullptr};
    HWND seat2Status{nullptr};
    HWND libraryLabel{nullptr};
    HWND launchReason{nullptr};
    HWND playerNameLabel{nullptr};
    HWND gameList{nullptr};
    HWND refreshButton{nullptr};
    HWND addExeButton{nullptr};
    HWND playerName{nullptr};
    HWND addPlayerButton{nullptr};
    HWND seat1Player{nullptr};
    HWND seat2Player{nullptr};
    HWND playButton{nullptr};
    hydra::ui::Locale locale{hydra::ui::systemLocale()};
    profile::SeatConfigDocument seats;
    std::vector<plan::GameRuntimeRequirement> requirements;
    LauncherActivate activate;
    provider::steam::SteamProviderAdapter steam;
    std::vector<std::unique_ptr<provider::custom::CustomExecutableProviderAdapter>>
        customAdapters;
    std::vector<catalog::GameCatalogCandidate> steamCandidates;
    std::vector<catalog::GameCatalogCandidate> customCandidates;
    profile::GameRecordDocument manualGameRecords;
    bool manualGamesWritable{true};
    std::vector<plan::ProviderAdapterBinding> providers;
    catalog::LocalGameCatalog library;
    LauncherUiModel model;
    std::optional<std::filesystem::path> launcherUserStateRoot;
    bool playerProfilesWritable{true};
    PlayPreview currentPreview;
    std::optional<std::string> focusedGameId;
    std::wstring lastDiagnostic;
    LauncherExitAction exitAction{LauncherExitAction::Closed};
};

LRESULT CALLBACK windowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<WindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == DM_GETDEFID && state != nullptr) {
        if (state->playButton != nullptr &&
            state->currentPreview.summary.canActivate &&
            state->currentPreview.compileResult.plan.has_value() &&
            IsWindowVisible(state->playButton) != FALSE &&
            IsWindowEnabled(state->playButton) != FALSE) {
            return MAKELRESULT(kPlay, DC_HASDEFID);
        }
        return 0;
    }
    if (message == WM_COMMAND && state != nullptr) {
        state->command(LOWORD(wParam), HIWORD(wParam));
        return 0;
    }
    if (message == WM_SIZE && state != nullptr) {
        state->applyLayout();
        return 0;
    }
    if (message == WM_GETMINMAXINFO) {
        const UINT dpi = dpiForWindowCompat(hwnd);
        RECT outer{0, 0, MulDiv(720, static_cast<int>(dpi), 96),
                   MulDiv(500, static_cast<int>(dpi), 96)};
        adjustWindowRectForDpiCompat(outer, WS_OVERLAPPEDWINDOW, WS_EX_DLGMODALFRAME, dpi);
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = outer.right - outer.left;
        info->ptMinTrackSize.y = outer.bottom - outer.top;
        return 0;
    }
    if (message == WM_DPICHANGED && state != nullptr) {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        state->applyLayout();
        return 0;
    }
    if (message == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

LauncherExitAction showLauncherWindow(
    HWND owner,
    profile::SeatConfigDocument seats,
    std::vector<plan::GameRuntimeRequirement> requirements,
    LauncherActivate activate,
    LauncherNavigationState* navigationState) {
    const auto instance = GetModuleHandleW(nullptr);
    const auto locale = hydra::ui::systemLocale();
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBoxW(owner,
                    hydra::ui::text(hydra::ui::TextId::GameLibraryRegisterFailed, locale).data(),
                    L"HydraSeat", MB_OK | MB_ICONERROR);
        return LauncherExitAction::Closed;
    }

    const UINT dpi = dpiForWindowCompat(owner);
    RECT initial{0, 0, static_cast<LONG>((820u * dpi + 95u) / 96u),
                 static_cast<LONG>((560u * dpi + 95u) / 96u)};
    adjustWindowRectForDpiCompat(
        initial, WS_OVERLAPPEDWINDOW, WS_EX_DLGMODALFRAME, dpi);
    WindowState state(
        std::move(seats), std::move(requirements), std::move(activate),
        navigationState == nullptr ? std::nullopt : navigationState->selectedGameId);
    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kWindowClass,
        hydra::ui::text(hydra::ui::TextId::GamesWindowTitle, locale).data(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, initial.right - initial.left,
        initial.bottom - initial.top, owner, nullptr, instance, &state);
    if (window == nullptr || !state.initialize(window)) {
        if (window != nullptr) DestroyWindow(window);
        MessageBoxW(owner,
                    hydra::ui::text(hydra::ui::TextId::GameLibraryInitializeFailed, locale).data(),
                    L"HydraSeat", MB_OK | MB_ICONERROR);
        return LauncherExitAction::Closed;
    }

    if (owner != nullptr) EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    HWND initialFocus = state.gameList;
    if (state.model.players().players.empty()) {
        initialFocus = state.playerName;
    } else if (!state.selectedPlayerId(state.seat1Player)) {
        initialFocus = state.seat1Player;
    }
    if (initialFocus != nullptr && IsWindowVisible(initialFocus) != FALSE &&
        IsWindowEnabled(initialFocus) != FALSE) {
        SetFocus(initialFocus);
    }
    MSG message;
    bool quitRequested = false;
    while (IsWindow(window)) {
        const int result = static_cast<int>(GetMessageW(&message, nullptr, 0, 0));
        if (result <= 0) {
            quitRequested = result == 0;
            break;
        }
        if (activateFocusedButtonWithEnter(window, message)) continue;
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (owner != nullptr) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (navigationState != nullptr) {
        navigationState->selectedGameId = state.focusedGameId;
    }
    if (quitRequested) PostQuitMessage(static_cast<int>(message.wParam));
    return state.exitAction;
}

} // namespace hydra::launcher_ui

#endif
