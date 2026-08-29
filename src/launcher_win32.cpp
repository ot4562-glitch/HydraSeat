#include "hydra/launcher_win32.hpp"

#ifdef _WIN32

#include "hydra/custom_executable_provider.hpp"
#include "hydra/launcher_ui_model.hpp"
#include "hydra/steam_provider.hpp"
#include "hydra/ui_localization.hpp"

#include <commdlg.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
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
constexpr int kPlayerRoster = 2106;
constexpr int kRenamePlayer = 2107;
constexpr int kRemovePlayer = 2108;
constexpr int kSeat1Player = 2110;
constexpr int kSeat1Game = 2111;
constexpr int kSeat2Player = 2112;
constexpr int kSeat2Game = 2113;
constexpr int kCreateSetup = 2120;
constexpr int kPlay = 2121;
constexpr int kDiagnostics = 2122;

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

class WindowState final {
public:
    WindowState(profile::SeatConfigDocument seatDocument,
                std::vector<plan::GameRuntimeRequirement> requirementSnapshot)
        : seats(std::move(seatDocument)),
          requirements(std::move(requirementSnapshot)),
          steam(provider::steam::makeNativeSteamMetadataSource()) {}

    bool initialize(HWND value) {
        hwnd = value;
        providers.push_back({"steam", &steam});
        refreshSteam();
        if (!rebuildLibrary()) return false;
        const auto initialized = model.initialize(
            seats, library, {}, providers, requirements);
        if (!initialized.succeeded()) {
            lastDiagnostic = widen(initialized.message);
            return false;
        }
        createControls();
        refreshControls();
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
        font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        headingFont = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH, L"Segoe UI");

        create(L"STATIC", t(hydra::ui::TextId::GamesHeading), SS_LEFT,
               20, 18, 180, 30, 0, headingFont);
        create(L"STATIC", t(hydra::ui::TextId::InstalledTitles), SS_LEFT,
               20, 50, 340, 22);
        gameList = create(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER,
                          20, 82, 405, 440, kGameList);
        refreshButton = create(L"BUTTON", t(hydra::ui::TextId::Refresh), BS_PUSHBUTTON,
                               20, 535, 115, 34, kRefresh);
        addExeButton = create(L"BUTTON", t(hydra::ui::TextId::AddExecutable), BS_PUSHBUTTON,
                              145, 535, 125, 34, kAddExe);

        create(L"STATIC", t(hydra::ui::TextId::PlayersAndSeats), SS_LEFT,
               455, 18, 300, 30, 0, headingFont);
        create(L"STATIC", t(hydra::ui::TextId::PlayerName), SS_LEFT, 455, 58, 130, 22);
        playerName = create(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL,
                            455, 82, 190, 30, kPlayerName);
        addPlayerButton = create(L"BUTTON", t(hydra::ui::TextId::AddPlayer), BS_PUSHBUTTON,
                                 655, 82, 95, 30, kAddPlayer);
        playerRoster = create(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                              760, 82, 155, 220, kPlayerRoster);
        renamePlayerButton = create(L"BUTTON", t(hydra::ui::TextId::Rename), BS_PUSHBUTTON,
                                    925, 82, 75, 30, kRenamePlayer);
        removePlayerButton = create(L"BUTTON", t(hydra::ui::TextId::Remove), BS_PUSHBUTTON,
                                    1010, 82, 80, 30, kRemovePlayer);

        const auto seatOne = hydra::ui::formatOne(hydra::ui::TextId::SeatLabel, locale, L"1");
        const auto seatTwo = hydra::ui::formatOne(hydra::ui::TextId::SeatLabel, locale, L"2");
        create(L"STATIC", seatOne.c_str(), SS_LEFT, 455, 140, 90, 24, 0, headingFont);
        create(L"STATIC", t(hydra::ui::TextId::Player), SS_LEFT, 455, 178, 80, 22);
        seat1Player = create(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                             540, 174, 305, 250, kSeat1Player);
        create(L"STATIC", t(hydra::ui::TextId::Game), SS_LEFT, 455, 220, 80, 22);
        seat1Game = create(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                           540, 216, 305, 250, kSeat1Game);

        create(L"STATIC", seatTwo.c_str(), SS_LEFT, 455, 274, 90, 24, 0, headingFont);
        create(L"STATIC", t(hydra::ui::TextId::Player), SS_LEFT, 455, 312, 80, 22);
        seat2Player = create(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                             540, 308, 305, 250, kSeat2Player);
        create(L"STATIC", t(hydra::ui::TextId::Game), SS_LEFT, 455, 354, 80, 22);
        seat2Game = create(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                           540, 350, 305, 250, kSeat2Game);

        setupButton = create(L"BUTTON", t(hydra::ui::TextId::CreateTwoPlayerSetup), BS_PUSHBUTTON,
                             455, 404, 220, 34, kCreateSetup);
        diagnostics = create(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL,
                             455, 455, 635, 114, kDiagnostics);
        playButton = create(L"BUTTON", t(hydra::ui::TextId::Play), BS_DEFPUSHBUTTON,
                            930, 590, 160, 44, kPlay);
        EnableWindow(playButton, FALSE);
    }

    const wchar_t* t(hydra::ui::TextId id) const noexcept {
        return hydra::ui::text(id, locale).data();
    }

    HWND create(const wchar_t* className, const wchar_t* text, DWORD style,
                int x, int y, int width, int height, int id = 0,
                HFONT selectedFont = nullptr) {
        HWND control = CreateWindowExW(
            0, className, text, WS_CHILD | WS_VISIBLE | style,
            x, y, width, height, hwnd,
            id == 0 ? nullptr : reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT,
                     reinterpret_cast<WPARAM>(selectedFont != nullptr ? selectedFont : font),
                     TRUE);
        return control;
    }

    void fillCombo(HWND combo, bool playersCombo) {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(playersCombo
                         ? t(hydra::ui::TextId::ChoosePlayer)
                         : t(hydra::ui::TextId::ChooseGame)));
        if (playersCombo) {
            for (const auto& player : model.players().players) {
                SendMessageW(combo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(player.displayName.c_str()));
            }
        } else {
            for (const auto& entry : model.library().entries) {
                SendMessageW(combo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(entry.game.title.c_str()));
            }
        }
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }

    void refreshControls() {
        SendMessageW(gameList, LB_RESETCONTENT, 0, 0);
        for (const auto& entry : model.library().entries) {
            std::wstring label = entry.game.title + L"  [" + widen(entry.game.providerId) + L"]";
            SendMessageW(gameList, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(label.c_str()));
        }
        fillCombo(seat1Player, true);
        fillCombo(seat2Player, true);
        fillCombo(seat1Game, false);
        fillCombo(seat2Game, false);
        fillCombo(playerRoster, true);
        syncComboSelections();
        updatePreview();
    }

    void syncComboSelections() {
        for (const auto& binding : model.selection().bindings) {
            HWND playerCombo = binding.seatId == 1u ? seat1Player : seat2Player;
            HWND gameCombo = binding.seatId == 1u ? seat1Game : seat2Game;
            const auto player = std::find_if(
                model.players().players.begin(), model.players().players.end(),
                [&](const auto& value) { return value.playerId == binding.playerId; });
            const auto game = std::find_if(
                model.library().entries.begin(), model.library().entries.end(),
                [&](const auto& value) { return value.game.gameId == binding.gameId; });
            if (player != model.players().players.end()) {
                const auto offset = std::distance(model.players().players.begin(), player);
                SendMessageW(playerCombo, CB_SETCURSEL, offset + 1, 0);
            }
            if (game != model.library().entries.end()) {
                const auto offset = std::distance(model.library().entries.begin(), game);
                SendMessageW(gameCombo, CB_SETCURSEL, offset + 1, 0);
            }
        }
    }

    void addPlayer() {
        wchar_t buffer[profile::kMaximumDisplayNameCodeUnits + 1u]{};
        GetWindowTextW(playerName, buffer, static_cast<int>(std::size(buffer)));
        std::string playerId;
        const auto diagnostic = model.createPlayer(
            buffer, std::string(hydra::ui::localeTag(locale)), std::nullopt, playerId);
        lastDiagnostic = diagnostic.succeeded()
            ? std::wstring(t(hydra::ui::TextId::PlayerAdded))
            : widen(diagnostic.message);
        if (diagnostic.succeeded()) SetWindowTextW(playerName, L"");
        refreshControls();
    }

    void renamePlayer() {
        const auto selected = SendMessageW(playerRoster, CB_GETCURSEL, 0, 0);
        wchar_t buffer[profile::kMaximumDisplayNameCodeUnits + 1u]{};
        GetWindowTextW(playerName, buffer, static_cast<int>(std::size(buffer)));
        if (selected <= 0 || static_cast<std::size_t>(selected - 1) >=
                                 model.players().players.size()) {
            lastDiagnostic = t(hydra::ui::TextId::ChooseRosterPlayer);
            updatePreview();
            return;
        }
        const auto& player = model.players().players[static_cast<std::size_t>(selected - 1)];
        const auto diagnostic = model.renamePlayer(player.playerId, buffer);
        lastDiagnostic = diagnostic.succeeded()
            ? std::wstring(t(hydra::ui::TextId::PlayerRenamed))
            : widen(diagnostic.message);
        if (diagnostic.succeeded()) SetWindowTextW(playerName, L"");
        refreshControls();
    }

    void removePlayer() {
        const auto selected = SendMessageW(playerRoster, CB_GETCURSEL, 0, 0);
        if (selected <= 0 || static_cast<std::size_t>(selected - 1) >=
                                 model.players().players.size()) {
            lastDiagnostic = t(hydra::ui::TextId::ChooseRosterPlayer);
            updatePreview();
            return;
        }
        const auto playerId =
            model.players().players[static_cast<std::size_t>(selected - 1)].playerId;
        const auto diagnostic = model.removePlayer(playerId);
        lastDiagnostic = diagnostic.succeeded()
            ? std::wstring(t(hydra::ui::TextId::PlayerRemoved))
            : widen(diagnostic.message);
        refreshControls();
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
        customCandidates.push_back(discovered[0]);
        if (!rebuildLibrary()) {
            customCandidates.pop_back();
            updatePreview();
            return;
        }
        const auto replaced = model.replaceLibrary(library);
        if (!replaced.succeeded()) {
            customCandidates.pop_back();
            (void)rebuildLibrary();
            lastDiagnostic = widen(replaced.message);
            updatePreview();
            return;
        }
        const auto attached = model.attachProvider({"custom", adapter.get(), appId});
        if (!attached.succeeded()) {
            customCandidates.pop_back();
            (void)rebuildLibrary();
            (void)model.replaceLibrary(library);
            lastDiagnostic = widen(attached.message);
            updatePreview();
            return;
        }
        customAdapters.push_back(std::move(adapter));
        lastDiagnostic = t(hydra::ui::TextId::ExecutableAdded);
        refreshControls();
    }

    void applySeatSelection(SeatId seatId, HWND playerCombo, HWND gameCombo) {
        const auto playerIndex = SendMessageW(playerCombo, CB_GETCURSEL, 0, 0);
        const auto gameIndex = SendMessageW(gameCombo, CB_GETCURSEL, 0, 0);
        if (playerIndex <= 0 || gameIndex <= 0) {
            const auto ignored = model.clearSeat(seatId);
            (void)ignored;
            updatePreview();
            return;
        }
        const auto playerOffset = static_cast<std::size_t>(playerIndex - 1);
        const auto gameOffset = static_cast<std::size_t>(gameIndex - 1);
        if (playerOffset >= model.players().players.size() ||
            gameOffset >= model.library().entries.size()) return;
        const auto diagnostic = model.selectGame(
            seatId, model.players().players[playerOffset].playerId,
            model.library().entries[gameOffset].game.gameId);
        lastDiagnostic = diagnostic.succeeded()
            ? std::wstring(t(hydra::ui::TextId::SeatSelectionUpdated))
            : widen(diagnostic.message);
        updatePreview();
    }

    void createSetup() {
        if (model.selection().bindings.size() != 2u ||
            model.selection().bindings[0].gameId != model.selection().bindings[1].gameId) {
            lastDiagnostic = t(hydra::ui::TextId::SameGameFirst);
            updatePreview();
            return;
        }
        const auto& gameId = model.selection().bindings[0].gameId;
        const auto found = std::find_if(model.library().entries.begin(),
                                        model.library().entries.end(),
                                        [&](const auto& entry) {
                                            return entry.game.gameId == gameId;
                                        });
        if (found == model.library().entries.end()) return;
        setup::GenerateSetupInput input;
        input.game = &found->game;
        input.setupId = "setup-ui-" + std::to_string(++setupCounter);
        input.displayName = t(hydra::ui::TextId::TwoPlayers);
        input.instances = {profile::InstanceRecipe{}, profile::InstanceRecipe{}};
        std::vector<setup::MutationIntent> mutations;
        const auto diagnostic = model.createSetup(input, mutations);
        lastDiagnostic = diagnostic.succeeded()
                             ? std::wstring(t(hydra::ui::TextId::SetupCreatedEvidencePending))
                             : widen(diagnostic.message);
        updatePreview();
    }

    void updatePreview() {
        SendMessageW(diagnostics, LB_RESETCONTENT, 0, 0);
        if (!lastDiagnostic.empty()) {
            SendMessageW(diagnostics, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(lastDiagnostic.c_str()));
        }
        currentPreview = model.preview();
        for (const auto& message : currentPreview.summary.messages) {
            const auto localized = hydra::ui::preflightText(message.code, locale);
            const auto line = localized.empty() ? widen(message.userMessage)
                                                : std::wstring(localized);
            SendMessageW(diagnostics, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(line.c_str()));
        }
        EnableWindow(playButton, currentPreview.summary.canActivate ? TRUE : FALSE);
    }

    void play() {
        updatePreview();
        if (!currentPreview.summary.canActivate || !currentPreview.compileResult.plan) return;
        const auto recorded = model.recordActivatedPlan(*currentPreview.compileResult.plan);
        if (!recorded.succeeded()) {
            lastDiagnostic = widen(recorded.message);
            updatePreview();
            return;
        }
        MessageBoxW(hwnd,
                    t(hydra::ui::TextId::PlayPlanReady),
                    t(hydra::ui::TextId::PlayDialogTitle), MB_OK | MB_ICONINFORMATION);
    }

    void command(int id, int notification) {
        if (id == kRefresh && notification == BN_CLICKED) {
            refreshSteam();
            if (rebuildLibrary()) {
                const auto diagnostic = model.replaceLibrary(library);
                if (!diagnostic.succeeded()) lastDiagnostic = widen(diagnostic.message);
            }
            refreshControls();
        } else if (id == kAddExe && notification == BN_CLICKED) {
            addExecutable();
        } else if (id == kAddPlayer && notification == BN_CLICKED) {
            addPlayer();
        } else if (id == kRenamePlayer && notification == BN_CLICKED) {
            renamePlayer();
        } else if (id == kRemovePlayer && notification == BN_CLICKED) {
            removePlayer();
        } else if (id == kSeat1Player || id == kSeat1Game) {
            if (notification == CBN_SELCHANGE) applySeatSelection(1u, seat1Player, seat1Game);
        } else if (id == kSeat2Player || id == kSeat2Game) {
            if (notification == CBN_SELCHANGE) applySeatSelection(2u, seat2Player, seat2Game);
        } else if (id == kCreateSetup && notification == BN_CLICKED) {
            createSetup();
        } else if (id == kPlay && notification == BN_CLICKED) {
            play();
        }
    }

    HWND hwnd{nullptr};
    HWND gameList{nullptr};
    HWND refreshButton{nullptr};
    HWND addExeButton{nullptr};
    HWND playerName{nullptr};
    HWND addPlayerButton{nullptr};
    HWND playerRoster{nullptr};
    HWND renamePlayerButton{nullptr};
    HWND removePlayerButton{nullptr};
    HWND seat1Player{nullptr};
    HWND seat1Game{nullptr};
    HWND seat2Player{nullptr};
    HWND seat2Game{nullptr};
    HWND setupButton{nullptr};
    HWND diagnostics{nullptr};
    HWND playButton{nullptr};
    HFONT font{nullptr};
    HFONT headingFont{nullptr};
    hydra::ui::Locale locale{hydra::ui::systemLocale()};
    profile::SeatConfigDocument seats;
    std::vector<plan::GameRuntimeRequirement> requirements;
    provider::steam::SteamProviderAdapter steam;
    std::vector<std::unique_ptr<provider::custom::CustomExecutableProviderAdapter>>
        customAdapters;
    std::vector<catalog::GameCatalogCandidate> steamCandidates;
    std::vector<catalog::GameCatalogCandidate> customCandidates;
    std::vector<plan::ProviderAdapterBinding> providers;
    catalog::LocalGameCatalog library;
    LauncherUiModel model;
    PlayPreview currentPreview;
    std::wstring lastDiagnostic;
    std::uint64_t setupCounter{0u};
};

LRESULT CALLBACK windowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<WindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_COMMAND && state != nullptr) {
        state->command(LOWORD(wParam), HIWORD(wParam));
        return 0;
    }
    if (message == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

void showLauncherWindow(
    HWND owner,
    profile::SeatConfigDocument seats,
    std::vector<plan::GameRuntimeRequirement> requirements) {
    const auto instance = GetModuleHandleW(nullptr);
    const auto locale = hydra::ui::systemLocale();
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBoxW(owner,
                    hydra::ui::text(hydra::ui::TextId::GameLibraryRegisterFailed, locale).data(),
                    L"HydraSeat", MB_OK | MB_ICONERROR);
        return;
    }

    WindowState state(std::move(seats), std::move(requirements));
    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kWindowClass,
        hydra::ui::text(hydra::ui::TextId::GamesWindowTitle, locale).data(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 1130, 690, owner, nullptr, instance, &state);
    if (window == nullptr || !state.initialize(window)) {
        if (window != nullptr) DestroyWindow(window);
        MessageBoxW(owner,
                    hydra::ui::text(hydra::ui::TextId::GameLibraryInitializeFailed, locale).data(),
                    L"HydraSeat", MB_OK | MB_ICONERROR);
        return;
    }

    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG message;
    bool quitRequested = false;
    while (IsWindow(window)) {
        const int result = static_cast<int>(GetMessageW(&message, nullptr, 0, 0));
        if (result <= 0) {
            quitRequested = result == 0;
            break;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (state.font != nullptr) DeleteObject(state.font);
    if (state.headingFont != nullptr) DeleteObject(state.headingFont);
    if (quitRequested) PostQuitMessage(static_cast<int>(message.wParam));
}

} // namespace hydra::launcher_ui

#endif
