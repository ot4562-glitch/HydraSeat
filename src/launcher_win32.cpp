#include "hydra/launcher_win32.hpp"

#ifdef _WIN32

#include "hydra/compatibility_local_store.hpp"
#include "hydra/compatibility_share_model.hpp"
#include "hydra/custom_executable_provider.hpp"
#include "hydra/launcher_ui_model.hpp"
#include "hydra/launcher_layout.hpp"
#include "hydra/steam_provider.hpp"
#include "hydra/ui_localization.hpp"

#include <commdlg.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
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
constexpr int kSeat1Status = 2111;
constexpr int kSeat2Player = 2112;
constexpr int kSeat2Status = 2113;
constexpr int kCreateSetup = 2120;
constexpr int kPlay = 2121;
constexpr int kDiagnostics = 2122;
constexpr int kPrivacySharing = 2130;
constexpr int kPrivacyRetention = 2131;
constexpr int kPrivacySave = 2132;
constexpr int kLocalResults = 2140;
constexpr int kExportLocalResult = 2141;
constexpr int kDeleteLocalResult = 2142;
constexpr int kClearLocalResults = 2143;
constexpr int kSetupNavigation = 2150;
constexpr int kBackToGames = 2151;
constexpr int kConfigure = 2152;
constexpr int kUseSelectedSeatOne = 2153;
constexpr int kUseSelectedSeatTwo = 2154;
constexpr int kUseSelectedBothSeats = 2155;

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

std::optional<std::filesystem::path> privacySettingsPath() {
    wchar_t localAppData[32768]{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (length == 0u || length >= static_cast<DWORD>(std::size(localAppData))) return std::nullopt;
    return std::filesystem::path(localAppData) / L"HydraSeat" / L"privacy-settings.json";
}

std::optional<std::filesystem::path> playerProfilesPath() {
    wchar_t localAppData[32768]{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (length == 0u || length >= static_cast<DWORD>(std::size(localAppData))) return std::nullopt;
    return std::filesystem::path(localAppData) / L"HydraSeat" / L"players.json";
}

std::optional<std::filesystem::path> manualGamesPath() {
    wchar_t localAppData[32768]{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (length == 0u || length >= static_cast<DWORD>(std::size(localAppData))) return std::nullopt;
    return std::filesystem::path(localAppData) / L"HydraSeat" / L"manual-games.json";
}

COLORREF nativeColor(std::uint32_t rgb) noexcept {
    return RGB((rgb >> 16u) & 0xffu, (rgb >> 8u) & 0xffu, rgb & 0xffu);
}

bool highContrastEnabled() noexcept {
    HIGHCONTRASTW state{};
    state.cbSize = sizeof(state);
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, state.cbSize, &state, 0) != FALSE &&
           (state.dwFlags & HCF_HIGHCONTRASTON) != 0u;
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

std::wstring controlText(HWND control) {
    if (control == nullptr) return {};
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1u, L'\0');
    const int copied = GetWindowTextW(control, value.data(), length + 1);
    if (copied <= 0) return {};
    value.resize(static_cast<std::size_t>(copied));
    return value;
}

int measuredTextWidth(HDC dc, HFONT selectedFont, std::wstring_view text, UINT dpi) {
    const int deterministicFloor = hydra::ui::launcherTextWidthFloor(text, dpi);
    if (dc == nullptr || selectedFont == nullptr || text.empty()) return deterministicFloor;
    const int saved = SaveDC(dc);
    SelectObject(dc, selectedFont);
    SIZE size{};
    const BOOL measured = GetTextExtentPoint32W(
        dc, text.data(), static_cast<int>(text.size()), &size);
    RestoreDC(dc, saved);
    return std::max(deterministicFloor,
                    measured != FALSE ? static_cast<int>(size.cx) : 0);
}

int fontLineHeight(HDC dc, HFONT selectedFont, UINT dpi) {
    if (dc == nullptr || selectedFont == nullptr) {
        return static_cast<int>((20u * dpi + 95u) / 96u);
    }
    const int saved = SaveDC(dc);
    SelectObject(dc, selectedFont);
    TEXTMETRICW metrics{};
    const BOOL measured = GetTextMetricsW(dc, &metrics);
    RestoreDC(dc, saved);
    if (measured == FALSE) return static_cast<int>((20u * dpi + 95u) / 96u);
    return std::max(
        1, static_cast<int>(metrics.tmHeight + metrics.tmExternalLeading));
}

int measuredWrappedTextHeight(HDC dc, HFONT selectedFont, std::wstring_view text,
                              int width, UINT dpi) {
    if (text.empty()) return 0;
    const int lineHeight = fontLineHeight(dc, selectedFont, dpi);
    if (dc == nullptr || selectedFont == nullptr || width <= 0) return lineHeight;
    const int saved = SaveDC(dc);
    SelectObject(dc, selectedFont);
    RECT measured{0, 0, width, 0};
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &measured,
              DT_LEFT | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    RestoreDC(dc, saved);
    return std::max(
        lineHeight, static_cast<int>(measured.bottom - measured.top));
}

struct GameRowView {
    std::wstring title;
    std::wstring metadata;
    bool requirementAvailable{false};
};

class WindowState final {
public:
    WindowState(profile::SeatConfigDocument seatDocument,
                std::vector<plan::GameRuntimeRequirement> requirementSnapshot,
                LauncherActivate activationBoundary)
        : seats(std::move(seatDocument)),
          requirements(std::move(requirementSnapshot)),
          activate(std::move(activationBoundary)),
          steam(provider::steam::makeNativeSteamMetadataSource()) {}

    bool loadPlayerProfilesFromDisk(profile::PlayerProfileDocument& output) {
        output = {};
        const auto path = playerProfilesPath();
        if (!path) {
            playerProfilesWritable = false;
            return false;
        }

        std::error_code error;
        const bool exists = std::filesystem::exists(*path, error);
        if (error) {
            playerProfilesWritable = false;
            return false;
        }
        if (!exists) {
            playerProfilesWritable = true;
            return true;
        }

        const auto byteCount = std::filesystem::file_size(*path, error);
        if (error || byteCount == 0u || byteCount > profile::kMaximumSchemaDocumentBytes) {
            playerProfilesWritable = false;
            return false;
        }
        std::ifstream input(*path, std::ios::binary);
        if (!input) {
            playerProfilesWritable = false;
            return false;
        }
        std::string json(static_cast<std::size_t>(byteCount), '\0');
        input.read(json.data(), static_cast<std::streamsize>(json.size()));
        if (!input || input.gcount() != static_cast<std::streamsize>(json.size())) {
            playerProfilesWritable = false;
            return false;
        }
        char trailing = '\0';
        if (input.get(trailing)) {
            playerProfilesWritable = false;
            return false;
        }

        profile::PlayerProfileDocument candidate;
        const auto decoded = profile::decodePlayerProfileDocument(json, candidate);
        if (!decoded.succeeded()) {
            playerProfilesWritable = false;
            return false;
        }
        output = std::move(candidate);
        playerProfilesWritable = true;
        return true;
    }

    bool savePlayerProfilesToDisk(const profile::PlayerProfileDocument& document) {
        if (!playerProfilesWritable) return false;
        const auto path = playerProfilesPath();
        if (!path) return false;

        profile::SchemaDiagnostic diagnostic;
        const std::string json = profile::encodePlayerProfileDocument(document, &diagnostic);
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
        const auto palette = hydra::ui::launcherPalette();
        canvasBrush = CreateSolidBrush(nativeColor(palette.canvasIvory));
        raisedBrush = CreateSolidBrush(nativeColor(palette.surfaceRaised));
        surfaceBrush = CreateSolidBrush(nativeColor(palette.surfaceIvory));
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
        loadPrivacySettingsFromDisk();
        loadCompatibilityHistoryFromDisk();
        refreshControls();
        applyLayout();
        return true;
    }

    void updateFontsForDpi(UINT dpi) {
        if (font != nullptr) DeleteObject(font);
        if (strongFont != nullptr) DeleteObject(strongFont);
        if (headingFont != nullptr) DeleteObject(headingFont);
        font = CreateFontW(-(std::max)(12, (16 * static_cast<int>(dpi)) / 96),
                           0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        strongFont = CreateFontW(-(std::max)(12, (16 * static_cast<int>(dpi)) / 96),
                                 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        headingFont = CreateFontW(-(std::max)(16, (24 * static_cast<int>(dpi)) / 96),
                                  0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        for (HWND control : primaryControls) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        for (HWND control : advancedControls) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        for (HWND control : {headerTitle, heroTitle, seatsLabel, seat1Label,
                             seat2Label, libraryLabel, advancedHeading, privacyHeading,
                             localResultsHeading}) {
            if (control != nullptr) {
                SendMessageW(control, WM_SETFONT,
                             reinterpret_cast<WPARAM>(headingFont), TRUE);
            }
        }
        if (heroEyebrow != nullptr) {
            SendMessageW(heroEyebrow, WM_SETFONT,
                         reinterpret_cast<WPARAM>(strongFont), TRUE);
        }
    }

    void paint() {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        const bool highContrast = highContrastEnabled();
        const auto palette = hydra::ui::launcherPalette();
        HBRUSH canvas = highContrast ? GetSysColorBrush(COLOR_WINDOW) : canvasBrush;
        HBRUSH raised = highContrast ? GetSysColorBrush(COLOR_BTNFACE) : raisedBrush;
        HBRUSH surface = highContrast ? GetSysColorBrush(COLOR_BTNFACE) : surfaceBrush;
        FillRect(dc, &client, canvas);
        if (layout.valid) {
            RECT headerRect{layout.header.x, layout.header.y,
                            layout.header.right(), layout.header.bottom()};
            FillRect(dc, &headerRect, raised);
            if (!advancedPage) {
                RECT heroRect{layout.hero.x, layout.hero.y,
                              layout.hero.right(), layout.hero.bottom()};
                RECT launchRect{layout.launchBar.x, layout.launchBar.y,
                                layout.launchBar.right(), layout.launchBar.bottom()};
                FillRect(dc, &heroRect, raised);
                FillRect(dc, &launchRect, surface);
            }

            const COLORREF structuralColor = highContrast
                ? GetSysColor(COLOR_WINDOWTEXT) : nativeColor(palette.lineDefault);
            HPEN structuralPen = CreatePen(PS_SOLID, 1, structuralColor);
            if (structuralPen != nullptr) {
                HGDIOBJ oldPen = SelectObject(dc, structuralPen);
                MoveToEx(dc, layout.header.x, layout.header.bottom() - 1, nullptr);
                LineTo(dc, layout.header.right(), layout.header.bottom() - 1);

                if (!advancedPage) {
                    MoveToEx(dc, layout.hero.x, layout.hero.y, nullptr);
                    LineTo(dc, layout.hero.right(), layout.hero.y);
                    LineTo(dc, layout.hero.right(), layout.hero.bottom());
                    LineTo(dc, layout.hero.x, layout.hero.bottom());
                    LineTo(dc, layout.hero.x, layout.hero.y);

                    MoveToEx(dc, layout.seat1Row.x, layout.seat1Row.bottom(), nullptr);
                    LineTo(dc, layout.seat1Row.right(), layout.seat1Row.bottom());
                    MoveToEx(dc, layout.seat2Row.x, layout.seat2Row.bottom(), nullptr);
                    LineTo(dc, layout.seat2Row.right(), layout.seat2Row.bottom());
                    MoveToEx(dc, layout.launchBar.x, layout.launchBar.y, nullptr);
                    LineTo(dc, layout.launchBar.right(), layout.launchBar.y);
                }
                SelectObject(dc, oldPen);
                DeleteObject(structuralPen);
            }

            if (!advancedPage && !highContrast) {
                HPEN bronzePen = CreatePen(PS_SOLID, 2, nativeColor(palette.bronze));
                if (bronzePen != nullptr) {
                    HGDIOBJ oldPen = SelectObject(dc, bronzePen);
                    const int accentLength = (std::max)(32, layout.hero.width / 7);
                    MoveToEx(dc, layout.hero.x, layout.hero.y, nullptr);
                    LineTo(dc, layout.hero.x + accentLength, layout.hero.y);
                    SelectObject(dc, oldPen);
                    DeleteObject(bronzePen);
                }
            }
        }
        EndPaint(hwnd, &paint);
    }

    HBRUSH staticColor(HDC dc, HWND control) const {
        const bool onRaisedSurface = control == headerTitle || control == headerSubtitle ||
            (!advancedPage && (control == heroEyebrow || control == heroTitle ||
                               control == heroStatus));
        const bool onControlSurface = !advancedPage && control == launchReason;
        if (highContrastEnabled()) {
            const int background = (onRaisedSurface || onControlSurface)
                ? COLOR_BTNFACE : COLOR_WINDOW;
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, GetSysColor(background));
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return GetSysColorBrush(background);
        }

        const auto palette = hydra::ui::launcherPalette();
        const std::uint32_t background = onRaisedSurface
            ? palette.surfaceRaised
            : (onControlSurface ? palette.surfaceIvory : palette.canvasIvory);
        std::uint32_t foreground = palette.inkNavy;

        if (control == heroEyebrow) {
            foreground = palette.bronze;
        } else if (control == headerTitle || control == heroTitle || control == seatsLabel ||
                   control == seat1Label || control == seat2Label || control == libraryLabel ||
                   control == advancedHeading || control == privacyHeading ||
                   control == localResultsHeading) {
            foreground = palette.brandNavy;
        } else if (control == headerSubtitle || control == heroStatus) {
            foreground = palette.mutedInk;
        } else if (control == seat1Status || control == seat2Status) {
            const SeatId seatId = control == seat1Status ? 1u : 2u;
            const auto* binding = bindingForSeat(seatId);
            foreground = binding == nullptr
                ? palette.neutral
                : (seatReady(seatId) ? palette.success : palette.warning);
        } else if (control == launchReason) {
            const bool hasPlan = currentPreview.compileResult.plan.has_value();
            const bool ready = currentPreview.summary.canActivate && hasPlan &&
                               static_cast<bool>(activate);
            const bool blocked = std::any_of(
                currentPreview.summary.messages.begin(), currentPreview.summary.messages.end(),
                [](const auto& message) {
                    return message.severity == preflight::Severity::Blocking;
                });
            foreground = model.selection().bindings.empty()
                ? palette.mutedInk
                : (ready ? palette.success : (blocked ? palette.danger : palette.warning));
        }

        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, nativeColor(background));
        SetTextColor(dc, nativeColor(foreground));
        return onRaisedSurface ? raisedBrush
                               : (onControlSurface ? surfaceBrush : canvasBrush);
    }

    static hydra::ui::LauncherButtonRole buttonRole(HWND control) noexcept {
        switch (GetDlgCtrlID(control)) {
        case kPlay:
            return hydra::ui::LauncherButtonRole::Primary;
        case kSetupNavigation:
        case kBackToGames:
            return hydra::ui::LauncherButtonRole::Quiet;
        case kRemovePlayer:
        case kDeleteLocalResult:
        case kClearLocalResults:
            return hydra::ui::LauncherButtonRole::Danger;
        default:
            return hydra::ui::LauncherButtonRole::Secondary;
        }
    }

    static void drawStatusMarker(HDC dc, RECT rect,
                                 hydra::ui::LauncherStatusMarker marker,
                                 COLORREF color,
                                 const hydra::ui::LauncherThemeMetrics& metrics) {
        const int penWidth = (std::max)(1, metrics.focusWidth);
        HPEN pen = CreatePen(PS_SOLID, penWidth, color);
        HBRUSH fill = marker == hydra::ui::LauncherStatusMarker::Ring
            ? static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH))
            : CreateSolidBrush(color);
        if (pen == nullptr || fill == nullptr) {
            if (pen != nullptr) DeleteObject(pen);
            if (marker != hydra::ui::LauncherStatusMarker::Ring && fill != nullptr) {
                DeleteObject(fill);
            }
            return;
        }
        const HGDIOBJ oldPen = SelectObject(dc, pen);
        const HGDIOBJ oldBrush = SelectObject(dc, fill);
        if (marker == hydra::ui::LauncherStatusMarker::Triangle) {
            POINT points[3]{{(rect.left + rect.right) / 2, rect.top},
                            {rect.right, rect.bottom},
                            {rect.left, rect.bottom}};
            Polygon(dc, points, 3);
        } else {
            Ellipse(dc, rect.left, rect.top, rect.right, rect.bottom);
        }
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        if (marker != hydra::ui::LauncherStatusMarker::Ring) DeleteObject(fill);
        DeleteObject(pen);
    }

    void drawButton(const DRAWITEMSTRUCT& item) const {
        const bool highContrast = highContrastEnabled();
        const bool enabled = (item.itemState & ODS_DISABLED) == 0u &&
                             IsWindowEnabled(item.hwndItem) != FALSE;
        const bool focused = (item.itemState & ODS_FOCUS) != 0u &&
                             (item.itemState & ODS_NOFOCUSRECT) == 0u;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0u;
        const auto presentation = hydra::ui::launcherButtonPresentation(
            buttonRole(item.hwndItem), enabled, focused, pressed, highContrast);
        const auto palette = hydra::ui::launcherPalette();
        const auto metrics = hydra::ui::launcherThemeMetrics(dpiForWindowCompat(hwnd));
        RECT rect = item.rcItem;
        const int saved = SaveDC(item.hDC);

        COLORREF foreground = highContrast
            ? GetSysColor(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT)
            : nativeColor(palette.brandNavy);
        if (presentation.surface == hydra::ui::LauncherButtonSurface::System) {
            UINT state = DFCS_BUTTONPUSH;
            if (pressed) state |= DFCS_PUSHED;
            if (!enabled) state |= DFCS_INACTIVE;
            DrawFrameControl(item.hDC, &rect, DFC_BUTTON, state);
        } else {
            std::uint32_t background = palette.surfaceRaised;
            std::uint32_t border = palette.lineDefault;
            int borderWidth = (std::max)(1, metrics.focusWidth / 2);
            switch (presentation.surface) {
            case hydra::ui::LauncherButtonSurface::Primary:
                background = pressed ? palette.navyStrong : palette.brandNavy;
                border = palette.brandNavy;
                foreground = nativeColor(palette.surfaceRaised);
                break;
            case hydra::ui::LauncherButtonSurface::Raised:
                background = palette.surfaceRaised;
                border = pressed ? palette.lineStrong : palette.lineDefault;
                foreground = nativeColor(palette.brandNavy);
                break;
            case hydra::ui::LauncherButtonSurface::Quiet:
                background = palette.surfaceRaised;
                border = pressed ? palette.lineStrong : palette.surfaceRaised;
                foreground = nativeColor(palette.brandNavy);
                break;
            case hydra::ui::LauncherButtonSurface::Danger:
                background = palette.surfaceRaised;
                border = palette.danger;
                borderWidth = (std::max)(1, metrics.focusWidth);
                foreground = nativeColor(palette.danger);
                break;
            case hydra::ui::LauncherButtonSurface::Disabled:
                background = palette.canvasDeep;
                border = palette.lineSubtle;
                foreground = nativeColor(palette.faintInk);
                break;
            case hydra::ui::LauncherButtonSurface::System:
                break;
            }
            HBRUSH brush = CreateSolidBrush(nativeColor(background));
            HPEN pen = CreatePen(PS_SOLID, borderWidth, nativeColor(border));
            if (brush != nullptr && pen != nullptr) {
                const HGDIOBJ oldBrush = SelectObject(item.hDC, brush);
                const HGDIOBJ oldPen = SelectObject(item.hDC, pen);
                const int diameter = (std::max)(2, metrics.controlRadius * 2);
                RoundRect(item.hDC, rect.left, rect.top, rect.right, rect.bottom,
                          diameter, diameter);
                SelectObject(item.hDC, oldPen);
                SelectObject(item.hDC, oldBrush);
            }
            if (pen != nullptr) DeleteObject(pen);
            if (brush != nullptr) DeleteObject(brush);
        }

        const auto label = controlText(item.hwndItem);
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, foreground);
        HFONT textFont = strongFont != nullptr ? strongFont : font;
        SelectObject(item.hDC, textFont);
        RECT textRect = rect;
        InflateRect(&textRect, -metrics.space2, -metrics.space1);
        RECT measuredRect = textRect;
        DrawTextW(item.hDC, label.c_str(), static_cast<int>(label.size()), &measuredRect,
                  DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        const int availableHeight = std::max(
            0, static_cast<int>(textRect.bottom - textRect.top));
        const int measuredHeight = std::max(
            0, static_cast<int>(measuredRect.bottom - measuredRect.top));
        const int textHeight = std::min(availableHeight, measuredHeight);
        textRect.top += std::max(0, (availableHeight - textHeight) / 2);
        textRect.bottom = textRect.top + textHeight;
        if (presentation.pressedOffset) {
            OffsetRect(&textRect, 0, (std::max)(1, metrics.focusWidth / 2));
        }
        DrawTextW(item.hDC, label.c_str(), static_cast<int>(label.size()), &textRect,
                  DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        if (presentation.drawFocusFrame) {
            RECT focus = rect;
            InflateRect(&focus, -metrics.focusInset, -metrics.focusInset);
            DrawFocusRect(item.hDC, &focus);
        }
        RestoreDC(item.hDC, saved);
    }

    void drawGameRow(const DRAWITEMSTRUCT& item) const {
        if (item.itemID == static_cast<UINT>(-1) || item.itemID >= gameRows.size()) return;
        const auto& row = gameRows[item.itemID];
        const bool selected = (item.itemState & ODS_SELECTED) != 0u;
        const bool focused = (item.itemState & ODS_FOCUS) != 0u &&
                             (item.itemState & ODS_NOFOCUSRECT) == 0u;
        const bool highContrast = highContrastEnabled();
        const auto presentation = hydra::ui::launcherGameRowPresentation(
            selected, focused, highContrast);
        const auto palette = hydra::ui::launcherPalette();
        const UINT windowDpi = dpiForWindowCompat(hwnd);
        const auto metrics = hydra::ui::launcherThemeMetrics(windowDpi);
        RECT rect = item.rcItem;
        const int saved = SaveDC(item.hDC);
        const COLORREF background = highContrast
            ? GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW)
            : nativeColor(selected ? palette.surfaceRaised : palette.canvasIvory);
        const COLORREF titleColor = highContrast
            ? GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT)
            : nativeColor(selected ? palette.brandNavy : palette.inkNavy);
        const COLORREF metadataColor = highContrast
            ? titleColor : nativeColor(palette.mutedInk);
        HBRUSH backgroundBrush = CreateSolidBrush(background);
        if (backgroundBrush != nullptr) {
            FillRect(item.hDC, &rect, backgroundBrush);
            DeleteObject(backgroundBrush);
        }

        HPEN divider = CreatePen(
            PS_SOLID, 1, highContrast ? GetSysColor(COLOR_WINDOWTEXT)
                                      : nativeColor(palette.lineSubtle));
        if (divider != nullptr) {
            const HGDIOBJ oldPen = SelectObject(item.hDC, divider);
            MoveToEx(item.hDC, rect.left, rect.bottom - 1, nullptr);
            LineTo(item.hDC, rect.right, rect.bottom - 1);
            SelectObject(item.hDC, oldPen);
            DeleteObject(divider);
        }
        if (presentation.drawSelectionEdge) {
            RECT edge{rect.left, rect.top, rect.left + metrics.focusWidth, rect.bottom};
            HBRUSH edgeBrush = CreateSolidBrush(
                highContrast ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                             : nativeColor(palette.brandNavy));
            if (edgeBrush != nullptr) {
                FillRect(item.hDC, &edge, edgeBrush);
                DeleteObject(edgeBrush);
            }
        }

        RECT content = rect;
        content.left += metrics.space3;
        content.right -= metrics.space3;
        const int titleLineHeight = fontLineHeight(
            item.hDC, strongFont != nullptr ? strongFont : font, windowDpi);
        RECT titleRect{content.left, content.top + metrics.space2,
                       content.right, content.top + metrics.space2 + titleLineHeight};
        RECT detailRect{content.left, titleRect.bottom + metrics.space1,
                        content.right, content.bottom - metrics.space2};
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, titleColor);
        SelectObject(item.hDC, strongFont != nullptr ? strongFont : font);
        DrawTextW(item.hDC, row.title.c_str(), -1, &titleRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        if (row.requirementAvailable) {
            SetTextColor(item.hDC, metadataColor);
            SelectObject(item.hDC, font);
            DrawTextW(item.hDC, row.metadata.c_str(), -1, &detailRect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else {
            // Missing trusted requirement evidence is launch-blocking information.
            // It replaces secondary provider metadata instead of being crushed into
            // a tiny badge at the right edge of the row.
            const COLORREF warningColor = highContrast
                ? titleColor : nativeColor(palette.warning);
            const int marker = metrics.statusMarker;
            const int firstLineHeight = fontLineHeight(item.hDC, font, windowDpi);
            const int markerTop = detailRect.top +
                std::max(0, (firstLineHeight - marker) / 2);
            RECT markerRect{detailRect.left, markerTop,
                            detailRect.left + marker, markerTop + marker};
            drawStatusMarker(item.hDC, markerRect,
                             hydra::ui::LauncherStatusMarker::Triangle,
                             warningColor, metrics);
            RECT labelRect{markerRect.right + metrics.space2, detailRect.top,
                           detailRect.right, detailRect.bottom};
            const auto warningText = hydra::ui::launcherStatusLabelText(
                t(hydra::ui::TextId::NeedsSetup));
            SetTextColor(item.hDC, warningColor);
            SelectObject(item.hDC, font);
            DrawTextW(item.hDC, warningText.data(),
                      static_cast<int>(warningText.size()), &labelRect,
                      DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        }
        if (presentation.drawFocusFrame) {
            RECT focus = rect;
            InflateRect(&focus, -metrics.focusInset, -metrics.focusInset);
            DrawFocusRect(item.hDC, &focus);
        }
        RestoreDC(item.hDC, saved);
    }

    void drawSeatStatus(const DRAWITEMSTRUCT& item, SeatId seatId) const {
        const bool configured = bindingForSeat(seatId) != nullptr;
        const bool highContrast = highContrastEnabled();
        const auto presentation = hydra::ui::launcherSeatPresentation(
            configured, configured && seatReady(seatId), highContrast);
        const auto palette = hydra::ui::launcherPalette();
        const auto metrics = hydra::ui::launcherThemeMetrics(dpiForWindowCompat(hwnd));
        RECT rect = item.rcItem;
        const int saved = SaveDC(item.hDC);
        HBRUSH background = highContrast
            ? GetSysColorBrush(COLOR_WINDOW)
            : canvasBrush;
        FillRect(item.hDC, &rect, background);

        COLORREF tone = GetSysColor(COLOR_WINDOWTEXT);
        if (!highContrast) {
            switch (presentation.state) {
            case hydra::ui::LauncherSeatState::NotConfigured:
                tone = nativeColor(palette.neutral);
                break;
            case hydra::ui::LauncherSeatState::Ready:
                tone = nativeColor(palette.success);
                break;
            case hydra::ui::LauncherSeatState::NeedsAttention:
                tone = nativeColor(palette.warning);
                break;
            }
        }
        const int marker = metrics.statusMarker;
        const int markerTop = rect.top + (rect.bottom - rect.top - marker) / 2;
        RECT markerRect{rect.left + metrics.space1, markerTop,
                        rect.left + metrics.space1 + marker, markerTop + marker};
        drawStatusMarker(item.hDC, markerRect, presentation.marker, tone, metrics);

        const auto rawLabel = controlText(item.hwndItem);
        const auto label = hydra::ui::launcherStatusLabelText(rawLabel);
        RECT labelRect{markerRect.right + metrics.space2, rect.top,
                       rect.right, rect.bottom};
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, tone);
        SelectObject(item.hDC, strongFont != nullptr ? strongFont : font);
        RECT measuredRect = labelRect;
        DrawTextW(item.hDC, label.data(), static_cast<int>(label.size()), &measuredRect,
                  DT_LEFT | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
        const int availableHeight = std::max(
            0, static_cast<int>(labelRect.bottom - labelRect.top));
        const int measuredHeight = std::max(
            0, static_cast<int>(measuredRect.bottom - measuredRect.top));
        const int textHeight = std::min(availableHeight, measuredHeight);
        labelRect.top += std::max(0, (availableHeight - textHeight) / 2);
        labelRect.bottom = labelRect.top + textHeight;
        DrawTextW(item.hDC, label.data(), static_cast<int>(label.size()), &labelRect,
                  DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        RestoreDC(item.hDC, saved);
    }

    bool drawItem(const DRAWITEMSTRUCT* item) const {
        if (item == nullptr) return false;
        if (item->CtlType == ODT_BUTTON) {
            drawButton(*item);
            return true;
        }
        if (item->hwndItem == gameList) {
            drawGameRow(*item);
            return true;
        }
        if (item->hwndItem == seat1Status) {
            drawSeatStatus(*item, 1u);
            return true;
        }
        if (item->hwndItem == seat2Status) {
            drawSeatStatus(*item, 2u);
            return true;
        }
        return false;
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

    void loadPrivacySettingsFromDisk() {
        const auto path = privacySettingsPath();
        if (!path) {
            lastDiagnostic = t(hydra::ui::TextId::PrivacySettingsLoadFailed);
            return;
        }

        std::error_code error;
        const bool exists = std::filesystem::exists(*path, error);
        if (error) {
            lastDiagnostic = t(hydra::ui::TextId::PrivacySettingsLoadFailed);
            return;
        }
        if (!exists) return;

        const auto byteCount = std::filesystem::file_size(*path, error);
        if (error || byteCount == 0u ||
            byteCount > community::kMaximumCompatibilityPrivacySettingsBytes) {
            lastDiagnostic = t(hydra::ui::TextId::PrivacySettingsLoadFailed);
            return;
        }

        std::ifstream input(*path, std::ios::binary);
        if (!input) {
            lastDiagnostic = t(hydra::ui::TextId::PrivacySettingsLoadFailed);
            return;
        }
        std::string json(static_cast<std::size_t>(byteCount), '\0');
        input.read(json.data(), static_cast<std::streamsize>(json.size()));
        if (!input || input.gcount() != static_cast<std::streamsize>(json.size())) {
            lastDiagnostic = t(hydra::ui::TextId::PrivacySettingsLoadFailed);
            return;
        }
        char unexpected = '\0';
        if (input.get(unexpected)) {
            lastDiagnostic = t(hydra::ui::TextId::PrivacySettingsLoadFailed);
            return;
        }
        if (!privacyModel.loadPrivacySettingsJson(json).succeeded()) {
            lastDiagnostic = t(hydra::ui::TextId::PrivacySettingsLoadFailed);
        }
    }

    void loadCompatibilityHistoryFromDisk() {
        std::string error;
        const auto path = community::defaultCompatibilityLocalStorePath(&error);
        if (!path) {
            compatibilityHistoryWritable = false;
            lastDiagnostic = t(hydra::ui::TextId::ResultHistoryLoadFailed);
            return;
        }

        compatibilityStore = std::make_unique<community::CompatibilityLocalStore>(*path);
        const auto loaded = compatibilityStore->load(privacyModel);
        if (!loaded.succeeded()) {
            compatibilityHistoryWritable = false;
            compatibilityHistoryCorrupt = loaded.found();
            lastDiagnostic = t(hydra::ui::TextId::ResultHistoryLoadFailed);
            return;
        }
        compatibilityHistoryWritable = true;
        compatibilityHistoryCorrupt = false;
    }

    void refreshLocalResultControls() {
        if (localResults == nullptr) return;
        SendMessageW(localResults, LB_RESETCONTENT, 0, 0);
        for (const auto& entry : privacyModel.history()) {
            const std::wstring label = widen(entry.result.gameId) + L"  ·  " +
                                       widen(entry.result.timestampBucket);
            SendMessageW(localResults, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(label.c_str()));
        }
        const BOOL hasResults = privacyModel.history().empty() ? FALSE : TRUE;
        EnableWindow(exportLocalResultButton, hasResults);
        EnableWindow(deleteLocalResultButton, hasResults && compatibilityHistoryWritable);
        EnableWindow(clearLocalResultsButton, hasResults && compatibilityHistoryWritable);
    }

    std::optional<std::size_t> selectedLocalResult() const {
        if (localResults == nullptr) return std::nullopt;
        const auto selected = SendMessageW(localResults, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR || selected < 0 ||
            static_cast<std::size_t>(selected) >= privacyModel.history().size()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(selected);
    }

    bool saveCompatibilityHistory() {
        if (!compatibilityStore || !compatibilityHistoryWritable) return false;
        return compatibilityStore->save(privacyModel).succeeded();
    }

    void exportSelectedLocalResult() {
        const auto selected = selectedLocalResult();
        if (!selected) {
            lastDiagnostic = t(hydra::ui::TextId::ChooseLocalResult);
            updatePreview();
            return;
        }
        std::string json;
        const auto& result = privacyModel.history()[*selected].result;
        if (!privacyModel.exportLocalResult(result.resultId, json).succeeded()) {
            lastDiagnostic = t(hydra::ui::TextId::ResultExportFailed);
            updatePreview();
            return;
        }

        wchar_t path[32768] = L"hydraseat-compatibility-result.json";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd;
        dialog.lpstrFilter = L"JSON (*.json)\0*.json\0\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = static_cast<DWORD>(std::size(path));
        dialog.lpstrDefExt = L"json";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&dialog)) return;
        std::ofstream output(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        output.write(json.data(), static_cast<std::streamsize>(json.size()));
        output.flush();
        lastDiagnostic = output ? t(hydra::ui::TextId::ResultExported)
                                : t(hydra::ui::TextId::ResultExportFailed);
        updatePreview();
    }

    void deleteSelectedLocalResult() {
        const auto selected = selectedLocalResult();
        if (!selected) {
            lastDiagnostic = t(hydra::ui::TextId::ChooseLocalResult);
            updatePreview();
            return;
        }
        if (MessageBoxW(hwnd, t(hydra::ui::TextId::DeleteResultPrompt), L"HydraSeat",
                        MB_YESNO | MB_ICONWARNING) != IDYES) return;
        auto staged = privacyModel;
        const auto& resultId = privacyModel.history()[*selected].result.resultId;
        if (!staged.deleteLocalResult(resultId).succeeded()) return;
        const auto previous = privacyModel;
        privacyModel = std::move(staged);
        if (!saveCompatibilityHistory()) {
            privacyModel = previous;
            lastDiagnostic = t(hydra::ui::TextId::ResultHistorySaveFailed);
        } else {
            lastDiagnostic = t(hydra::ui::TextId::ResultDeleted);
        }
        refreshLocalResultControls();
        updatePreview();
    }

    void clearLocalResults() {
        if (privacyModel.history().empty()) return;
        if (MessageBoxW(hwnd, t(hydra::ui::TextId::ClearResultsPrompt), L"HydraSeat",
                        MB_YESNO | MB_ICONWARNING) != IDYES) return;
        const auto previous = privacyModel;
        if (!privacyModel.clearLocalResults().succeeded() || !saveCompatibilityHistory()) {
            privacyModel = previous;
            lastDiagnostic = t(hydra::ui::TextId::ResultHistorySaveFailed);
        } else {
            lastDiagnostic = t(hydra::ui::TextId::ResultsCleared);
        }
        refreshLocalResultControls();
        updatePreview();
    }

    bool persistPrivacySettings(const community::CompatibilityPrivacySettings& settings) {
        const auto path = privacySettingsPath();
        if (!path) return false;

        community::CompatibilityShareModel staged;
        if (!staged.setPrivacySettings(settings).succeeded()) return false;
        std::string json;
        if (!staged.exportPrivacySettingsJson(json).succeeded()) return false;

        std::error_code error;
        std::filesystem::create_directories(path->parent_path(), error);
        if (error) return false;

        auto temporary = *path;
        temporary += L".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output.write(json.data(), static_cast<std::streamsize>(json.size()));
            output.flush();
            if (!output) {
                output.close();
                std::filesystem::remove(temporary, error);
                return false;
            }
        }

        if (!MoveFileExW(temporary.c_str(), path->c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
    }

    void refreshPrivacyControls() {
        const auto& settings = privacyModel.privacySettings();
        SendMessageW(privacySharing, BM_SETCHECK,
                     settings.communitySharingEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        const auto retained = std::to_wstring(settings.retainedLocalResults);
        SetWindowTextW(privacyRetention, retained.c_str());
    }

    void savePrivacySettings() {
        BOOL translated = FALSE;
        const UINT retained = GetDlgItemInt(hwnd, kPrivacyRetention, &translated, FALSE);
        if (!translated || retained == 0u ||
            retained > community::kMaximumRetainedCompatibilityResults) {
            lastDiagnostic = t(hydra::ui::TextId::PrivacyInvalidRetention);
            updatePreview();
            return;
        }

        community::CompatibilityPrivacySettings candidate;
        candidate.communitySharingEnabled =
            SendMessageW(privacySharing, BM_GETCHECK, 0, 0) == BST_CHECKED;
        candidate.retainedLocalResults = static_cast<std::size_t>(retained);
        const bool retentionChanged =
            candidate.retainedLocalResults != privacyModel.privacySettings().retainedLocalResults;
        if (retentionChanged && (!compatibilityStore || !compatibilityHistoryWritable)) {
            lastDiagnostic = t(hydra::ui::TextId::ResultHistorySaveFailed);
            updatePreview();
            return;
        }

        auto stagedModel = privacyModel;
        const auto applied = stagedModel.setPrivacySettings(candidate);
        if (!applied.succeeded()) {
            lastDiagnostic = t(hydra::ui::TextId::PrivacySettingsSaveFailed);
            updatePreview();
            return;
        }

        bool stagedHistorySaved = false;
        if (compatibilityStore && compatibilityHistoryWritable) {
            const auto stored = compatibilityStore->save(stagedModel);
            if (!stored.succeeded()) {
                lastDiagnostic = t(hydra::ui::TextId::ResultHistorySaveFailed);
                updatePreview();
                return;
            }
            stagedHistorySaved = true;
        }
        if (!persistPrivacySettings(candidate)) {
            if (stagedHistorySaved) {
                (void)compatibilityStore->save(privacyModel);
            }
            lastDiagnostic = t(hydra::ui::TextId::PrivacySettingsSaveFailed);
            updatePreview();
            return;
        }

        privacyModel = std::move(stagedModel);
        lastDiagnostic = t(hydra::ui::TextId::PrivacySaved);
        refreshPrivacyControls();
        refreshLocalResultControls();
        updatePreview();
    }

    void createControls() {
        const UINT dpi = dpiForWindowCompat(hwnd);
        const auto metrics = hydra::ui::launcherThemeMetrics(dpi);
        font = CreateFontW(-(std::max)(12, (16 * static_cast<int>(metrics.dpi)) / 96),
                           0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        strongFont = CreateFontW(-(std::max)(12, (16 * static_cast<int>(metrics.dpi)) / 96),
                                 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH, L"Segoe UI");
        headingFont = CreateFontW(-(std::max)(16, (24 * static_cast<int>(metrics.dpi)) / 96),
                                  0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH, L"Segoe UI");

        headerTitle = create(L"STATIC", L"HYDRASEAT", SS_LEFT, 0, 0, 1, 1,
                             0, headingFont);
        headerSubtitle = create(L"STATIC", t(hydra::ui::TextId::LauncherSubtitle),
                                SS_LEFT, 0, 0, 1, 1);
        setupNavigation = createPrimary(
            L"BUTTON", t(hydra::ui::TextId::SetupAndDiagnostics),
            BS_OWNERDRAW | WS_TABSTOP, kSetupNavigation);

        heroEyebrow = createPrimary(L"STATIC", t(hydra::ui::TextId::SelectedGame),
                                    SS_LEFT | SS_NOPREFIX, 0, strongFont);
        heroTitle = createPrimary(L"STATIC", t(hydra::ui::TextId::NoGameSelected),
                                  SS_LEFT | SS_NOPREFIX, 0, headingFont);
        heroStatus = createPrimary(L"STATIC", t(hydra::ui::TextId::ChooseSeatForSelectedGame),
                                   SS_LEFT | SS_NOPREFIX);
        useSeatOneButton = createPrimary(
            L"BUTTON", t(hydra::ui::TextId::UseSeatOne),
            BS_OWNERDRAW | WS_TABSTOP, kUseSelectedSeatOne);
        useSeatTwoButton = createPrimary(
            L"BUTTON", t(hydra::ui::TextId::UseSeatTwo),
            BS_OWNERDRAW | WS_TABSTOP, kUseSelectedSeatTwo);
        useBothSeatsButton = createPrimary(
            L"BUTTON", t(hydra::ui::TextId::UseBothSeats),
            BS_OWNERDRAW | WS_TABSTOP, kUseSelectedBothSeats);
        configureButton = createPrimary(
            L"BUTTON", t(hydra::ui::TextId::SeatHardwareSetup),
            BS_OWNERDRAW | WS_TABSTOP, kConfigure);

        seatsLabel = createPrimary(L"STATIC", t(hydra::ui::TextId::SectionSeats),
                                   SS_LEFT, 0, headingFont);
        const auto seatOne = hydra::ui::formatOne(hydra::ui::TextId::SeatLabel, locale, L"1");
        const auto seatTwo = hydra::ui::formatOne(hydra::ui::TextId::SeatLabel, locale, L"2");
        seat1Label = createPrimary(L"STATIC", seatOne.c_str(), SS_LEFT, 0, headingFont);
        seat1Player = createPrimary(L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kSeat1Player);
        seat1Game = createPrimary(L"STATIC", t(hydra::ui::TextId::None),
            SS_LEFT | SS_NOPREFIX);
        seat1Status = createPrimary(L"STATIC", t(hydra::ui::TextId::StatusNotSelected),
                                    SS_OWNERDRAW | SS_NOPREFIX, kSeat1Status);
        seat2Label = createPrimary(L"STATIC", seatTwo.c_str(), SS_LEFT, 0, headingFont);
        seat2Player = createPrimary(L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kSeat2Player);
        seat2Game = createPrimary(L"STATIC", t(hydra::ui::TextId::None),
            SS_LEFT | SS_NOPREFIX);
        seat2Status = createPrimary(L"STATIC", t(hydra::ui::TextId::StatusNotSelected),
                                    SS_OWNERDRAW | SS_NOPREFIX, kSeat2Status);

        libraryLabel = createPrimary(L"STATIC", t(hydra::ui::TextId::SectionLibrary),
                                     SS_LEFT, 0, headingFont);
        gameList = createPrimary(L"LISTBOX", L"",
            LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT |
                WS_VSCROLL | WS_TABSTOP,
            kGameList);
        refreshButton = createPrimary(L"BUTTON", t(hydra::ui::TextId::Refresh),
            BS_OWNERDRAW | WS_TABSTOP, kRefresh);
        addExeButton = createPrimary(L"BUTTON", t(hydra::ui::TextId::AddExecutable),
            BS_OWNERDRAW | WS_TABSTOP, kAddExe);
        launchReason = createPrimary(L"STATIC", t(hydra::ui::TextId::NeedsSetup),
                                     SS_LEFT | SS_NOPREFIX);
        playButton = createPrimary(L"BUTTON", t(hydra::ui::TextId::Play),
                                   BS_OWNERDRAW | WS_TABSTOP, kPlay);
        EnableWindow(playButton, FALSE);

        advancedHeading = createAdvanced(
            L"STATIC", t(hydra::ui::TextId::SetupAndDiagnostics), SS_LEFT, 0, headingFont);
        backToGames = createAdvanced(L"BUTTON", t(hydra::ui::TextId::BackToGames),
                                     BS_OWNERDRAW | WS_TABSTOP, kBackToGames);
        playerNameLabel = createAdvanced(L"STATIC", t(hydra::ui::TextId::PlayerName), SS_LEFT);
        playerName = createAdvanced(L"EDIT", L"",
            WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, kPlayerName);
        addPlayerButton = createAdvanced(L"BUTTON", t(hydra::ui::TextId::AddPlayer),
            BS_OWNERDRAW | WS_TABSTOP, kAddPlayer);
        playerRoster = createAdvanced(L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kPlayerRoster);
        renamePlayerButton = createAdvanced(L"BUTTON", t(hydra::ui::TextId::Rename),
            BS_OWNERDRAW | WS_TABSTOP, kRenamePlayer);
        removePlayerButton = createAdvanced(L"BUTTON", t(hydra::ui::TextId::Remove),
            BS_OWNERDRAW | WS_TABSTOP, kRemovePlayer);
        setupButton = createAdvanced(L"BUTTON", t(hydra::ui::TextId::CreateTwoPlayerSetup),
            BS_OWNERDRAW | WS_TABSTOP, kCreateSetup);
        diagnostics = createAdvanced(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL,
                                     kDiagnostics);

        privacyHeading = createAdvanced(L"STATIC", t(hydra::ui::TextId::PrivacyHeading),
                                        SS_LEFT, 0, headingFont);
        privacySharing = createAdvanced(L"BUTTON", t(hydra::ui::TextId::CommunitySharing),
            BS_AUTOCHECKBOX | WS_TABSTOP, kPrivacySharing);
        privacyRetentionLabel = createAdvanced(
            L"STATIC", t(hydra::ui::TextId::RetainedResults), SS_LEFT);
        privacyRetention = createAdvanced(L"EDIT", L"",
            WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, kPrivacyRetention);
        privacySave = createAdvanced(L"BUTTON", t(hydra::ui::TextId::SavePrivacy),
            BS_OWNERDRAW | WS_TABSTOP, kPrivacySave);
        localResultsHeading = createAdvanced(
            L"STATIC", t(hydra::ui::TextId::LocalResultsHeading), SS_LEFT, 0, headingFont);
        localResults = createAdvanced(L"LISTBOX", L"",
            LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP, kLocalResults);
        exportLocalResultButton = createAdvanced(
            L"BUTTON", t(hydra::ui::TextId::ExportSelectedResult),
            BS_OWNERDRAW | WS_TABSTOP, kExportLocalResult);
        deleteLocalResultButton = createAdvanced(
            L"BUTTON", t(hydra::ui::TextId::DeleteSelectedResult),
            BS_OWNERDRAW | WS_TABSTOP, kDeleteLocalResult);
        clearLocalResultsButton = createAdvanced(
            L"BUTTON", t(hydra::ui::TextId::ClearLocalResults),
            BS_OWNERDRAW | WS_TABSTOP, kClearLocalResults);
        updatePageVisibility();
    }

    HWND createPrimary(const wchar_t* className, const wchar_t* label, DWORD style,
                       int id = 0, HFONT selectedFont = nullptr) {
        HWND control = create(className, label, style, 0, 0, 1, 1, id, selectedFont);
        primaryControls.push_back(control);
        return control;
    }

    HWND createAdvanced(const wchar_t* className, const wchar_t* label, DWORD style,
                        int id = 0, HFONT selectedFont = nullptr) {
        HWND control = create(className, label, style, 0, 0, 1, 1, id, selectedFont);
        advancedControls.push_back(control);
        return control;
    }

    static void move(HWND control, const hydra::ui::PixelRect& rect) {
        if (control == nullptr) return;
        MoveWindow(control, rect.x, rect.y, rect.width, rect.height, TRUE);
    }

    hydra::ui::LauncherTextMeasurements measureLayoutText(
        const hydra::ui::LauncherLayout& baseLayout, UINT windowDpi) const {
        hydra::ui::LauncherTextMeasurements measured;
        HDC dc = GetDC(hwnd);
        if (dc == nullptr) return measured;
        const auto metrics = hydra::ui::launcherThemeMetrics(windowDpi);
        measured.heroEyebrowWidth = measuredTextWidth(
            dc, strongFont, controlText(heroEyebrow), windowDpi);
        measured.heroTitleHeight = measuredWrappedTextHeight(
            dc, headingFont, controlText(heroTitle),
            std::max(1, baseLayout.heroTitle.width), windowDpi);
        measured.heroStatusHeight = measuredWrappedTextHeight(
            dc, font, controlText(heroStatus),
            std::max(1, baseLayout.heroStatus.width), windowDpi);
        measured.useSeatOneWidth = measuredTextWidth(
            dc, strongFont, controlText(useSeatOneButton), windowDpi);
        measured.useSeatTwoWidth = measuredTextWidth(
            dc, strongFont, controlText(useSeatTwoButton), windowDpi);
        measured.useBothSeatsWidth = measuredTextWidth(
            dc, strongFont, controlText(useBothSeatsButton), windowDpi);
        measured.configureWidth = measuredTextWidth(
            dc, strongFont, controlText(configureButton), windowDpi);

        const auto reason = controlText(launchReason);
        measured.launchReasonHeight = measuredWrappedTextHeight(
            dc, font, reason, std::max(1, baseLayout.launchReason.width), windowDpi);

        const auto warning = hydra::ui::launcherStatusLabelText(
            hydra::ui::text(hydra::ui::TextId::NeedsSetup, locale));
        const int warningWidth = std::max(
            metrics.minimumTarget,
            baseLayout.gameList.width - metrics.space3 * 2 -
                metrics.statusMarker - metrics.space2);
        const int titleLineHeight = fontLineHeight(dc, strongFont, windowDpi);
        const int warningHeight = measuredWrappedTextHeight(
            dc, font, warning, warningWidth, windowDpi);
        measured.gameRowHeight = metrics.space2 + titleLineHeight + metrics.space1 +
                                 warningHeight + metrics.space2;
        ReleaseDC(hwnd, dc);
        return measured;
    }

    void applyLayout() {
        RECT client{};
        GetClientRect(hwnd, &client);
        const int clientWidth = client.right - client.left;
        const int clientHeight = client.bottom - client.top;
        const UINT windowDpi = dpiForWindowCompat(hwnd);
        const auto baseLayout = hydra::ui::computeLauncherLayout(
            clientWidth, clientHeight, windowDpi);
        if (!baseLayout.valid) {
            layout = baseLayout;
            return;
        }
        const auto requirements = hydra::ui::launcherTextRequirements(
            measureLayoutText(baseLayout, windowDpi), windowDpi);
        layout = hydra::ui::computeLauncherLayout(
            clientWidth, clientHeight, windowDpi, requirements);
        if (!layout.valid) return;
        move(headerTitle, layout.headerTitle);
        move(headerSubtitle, layout.headerSubtitle);
        move(setupNavigation, layout.setupNavigation);
        move(heroEyebrow, layout.heroEyebrow);
        move(heroTitle, layout.heroTitle);
        move(heroStatus, layout.heroStatus);
        move(useSeatOneButton, layout.useSeatOne);
        move(useSeatTwoButton, layout.useSeatTwo);
        move(useBothSeatsButton, layout.useBothSeats);
        move(configureButton, layout.configure);
        move(seatsLabel, layout.seatsLabel);
        move(seat1Label, layout.seat1Label);
        move(seat1Player, layout.seat1Player);
        move(seat1Game, layout.seat1Game);
        move(seat1Status, layout.seat1Status);
        move(seat2Label, layout.seat2Label);
        move(seat2Player, layout.seat2Player);
        move(seat2Game, layout.seat2Game);
        move(seat2Status, layout.seat2Status);
        move(libraryLabel, layout.libraryLabel);
        move(gameList, layout.gameList);
        SendMessageW(gameList, LB_SETITEMHEIGHT, 0, layout.gameRowHeight);
        move(refreshButton, layout.refresh);
        move(addExeButton, layout.addExecutable);
        move(launchReason, layout.launchReason);
        move(playButton, layout.play);
        move(advancedHeading, layout.advancedHeading);
        move(backToGames, layout.backToGames);
        move(playerNameLabel, layout.playerNameLabel);
        move(playerName, layout.playerName);
        move(addPlayerButton, layout.addPlayer);
        move(playerRoster, layout.playerRoster);
        move(renamePlayerButton, layout.renamePlayer);
        move(removePlayerButton, layout.removePlayer);
        move(setupButton, layout.setupButton);
        move(diagnostics, layout.diagnostics);
        move(privacyHeading, layout.privacyHeading);
        move(privacySharing, layout.privacySharing);
        move(privacyRetentionLabel, layout.privacyRetentionLabel);
        move(privacyRetention, layout.privacyRetention);
        move(privacySave, layout.privacySave);
        move(localResultsHeading, layout.localResultsHeading);
        move(localResults, layout.localResults);
        move(exportLocalResultButton, layout.exportLocalResult);
        move(deleteLocalResultButton, layout.deleteLocalResult);
        move(clearLocalResultsButton, layout.clearLocalResults);
        updatePageVisibility();
        InvalidateRect(hwnd, nullptr, TRUE);
    }

    void updatePageVisibility() {
        for (HWND control : primaryControls) {
            ShowWindow(control, advancedPage ? SW_HIDE : SW_SHOW);
        }
        for (HWND control : advancedControls) {
            ShowWindow(control, advancedPage ? SW_SHOW : SW_HIDE);
        }
        ShowWindow(headerTitle, SW_SHOW);
        ShowWindow(headerSubtitle, SW_SHOW);
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

    void fillPlayerCombo(HWND combo) {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(t(hydra::ui::TextId::ChoosePlayer)));
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

    bool hasRuntimeRequirement(std::string_view gameId) const {
        return std::any_of(
            model.requirements().begin(), model.requirements().end(),
            [&](const auto& requirement) { return requirement.gameId == gameId; });
    }

    static std::wstring gameRowMetadata(const catalog::LocalGameCatalogEntry& entry) {
        std::wstring metadata = widen(entry.game.providerId);
        if (entry.game.localVersion && !entry.game.localVersion->empty()) {
            if (!metadata.empty()) metadata.append(L"  \u00b7  ");
            metadata.append(*entry.game.localVersion);
        }
        return metadata;
    }

    void refreshControls() {
        SendMessageW(gameList, LB_RESETCONTENT, 0, 0);
        gameRows.clear();
        gameRows.reserve(model.library().entries.size());
        int focusedIndex = -1;
        for (std::size_t index = 0; index < model.library().entries.size(); ++index) {
            const auto& entry = model.library().entries[index];
            gameRows.push_back({entry.game.title, gameRowMetadata(entry),
                                hasRuntimeRequirement(entry.game.gameId)});
            SendMessageW(gameList, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(entry.game.title.c_str()));
            if (focusedGameId && entry.game.gameId == *focusedGameId) {
                focusedIndex = static_cast<int>(index);
            }
        }
        if (!model.library().entries.empty()) {
            if (focusedIndex < 0) {
                focusedIndex = 0;
                focusedGameId = model.library().entries.front().game.gameId;
            }
            SendMessageW(gameList, LB_SETCURSEL, focusedIndex, 0);
        } else {
            focusedGameId.reset();
        }
        fillPlayerCombo(seat1Player);
        fillPlayerCombo(seat2Player);
        fillPlayerCombo(playerRoster);
        syncSeatPresentation();
        refreshPrivacyControls();
        refreshLocalResultControls();
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

    void updateHero() {
        const auto* selectedGame = selectedLibraryGame();
        if (selectedGame == nullptr) {
            focusedGameId.reset();
            SetWindowTextW(heroTitle, t(hydra::ui::TextId::NoGameSelected));
            SetWindowTextW(heroStatus, t(hydra::ui::TextId::ChooseSeatForSelectedGame));
        } else {
            focusedGameId = selectedGame->game.gameId;
            SetWindowTextW(heroTitle, selectedGame->game.title.c_str());
            const auto* first = bindingForSeat(1u);
            const auto* second = bindingForSeat(2u);
            const bool firstUsesSelected = first != nullptr &&
                                           first->gameId == selectedGame->game.gameId;
            const bool secondUsesSelected = second != nullptr &&
                                            second->gameId == selectedGame->game.gameId;
            if (firstUsesSelected && secondUsesSelected) {
                SetWindowTextW(heroStatus, t(hydra::ui::TextId::AssignedToBothSeats));
            } else if (firstUsesSelected || secondUsesSelected) {
                const auto assigned = hydra::ui::formatOne(
                    hydra::ui::TextId::AssignedToSeat, locale,
                    firstUsesSelected ? L"1" : L"2");
                SetWindowTextW(heroStatus, assigned.c_str());
            } else {
                SetWindowTextW(heroStatus, t(hydra::ui::TextId::ChooseSeatForSelectedGame));
            }
        }

        const BOOL canAssign = selectedGame != nullptr ? TRUE : FALSE;
        EnableWindow(useSeatOneButton, canAssign);
        EnableWindow(useSeatTwoButton, canAssign);
        EnableWindow(useBothSeatsButton, canAssign);

        const bool canActivate = currentPreview.summary.canActivate &&
                                 currentPreview.compileResult.plan.has_value();
        const auto* first = bindingForSeat(1u);
        const auto* second = bindingForSeat(2u);
        SetWindowTextW(seat1Status,
            first == nullptr ? t(hydra::ui::TextId::StatusNotSelected)
                             : (seatReady(1u) ? t(hydra::ui::TextId::StatusReady)
                                              : t(hydra::ui::TextId::StatusNeedsAttention)));
        SetWindowTextW(seat2Status,
            second == nullptr ? t(hydra::ui::TextId::StatusNotSelected)
                              : (seatReady(2u) ? t(hydra::ui::TextId::StatusReady)
                                               : t(hydra::ui::TextId::StatusNeedsAttention)));

        if (model.selection().bindings.empty()) {
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
        // Blocking/readiness copy is a layout input. Re-measure after every model
        // projection change so a newly selected localized reason cannot inherit a
        // stale shorter rectangle from the previous state.
        applyLayout();
    }

    void syncSeatPresentation() {
        SendMessageW(seat1Player, CB_SETCURSEL, 0, 0);
        SendMessageW(seat2Player, CB_SETCURSEL, 0, 0);
        SetWindowTextW(seat1Game, t(hydra::ui::TextId::None));
        SetWindowTextW(seat2Game, t(hydra::ui::TextId::None));
        for (const auto& binding : model.selection().bindings) {
            HWND playerCombo = binding.seatId == 1u ? seat1Player : seat2Player;
            HWND gameLabel = binding.seatId == 1u ? seat1Game : seat2Game;
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
                SetWindowTextW(gameLabel, game->game.title.c_str());
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
        if (previousSeat1Player) selectPlayerInCombo(seat1Player, *previousSeat1Player);
        if (previousSeat2Player) selectPlayerInCombo(seat2Player, *previousSeat2Player);

        if (pendingSeatAssignment) {
            const SeatId seatId = *pendingSeatAssignment;
            HWND combo = seatId == 1u ? seat1Player : seat2Player;
            if (!selectedPlayerId(combo)) selectPlayerInCombo(combo, playerId);
            advancedPage = false;
            applyLayout();
            assignSelectedGame(seatId, combo);
            return;
        }

        if (pendingBothAssignment) {
            if (!selectedPlayerId(seat1Player)) {
                selectPlayerInCombo(seat1Player, playerId);
            } else if (!selectedPlayerId(seat2Player)) {
                selectPlayerInCombo(seat2Player, playerId);
            }
            advancedPage = false;
            applyLayout();
            if (selectedPlayerId(seat1Player) && selectedPlayerId(seat2Player)) {
                assignSelectedGameToBoth();
            } else {
                lastDiagnostic = t(hydra::ui::TextId::ChooseSecondPlayerToContinue);
                updatePreview();
                SetFocus(!selectedPlayerId(seat1Player) ? seat1Player : seat2Player);
            }
        }
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
        const auto playerId =
            model.players().players[static_cast<std::size_t>(selected - 1)].playerId;
        auto staged = model;
        const auto diagnostic = staged.renamePlayer(playerId, buffer);
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
        lastDiagnostic = t(hydra::ui::TextId::PlayerRenamed);
        SetWindowTextW(playerName, L"");
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
        auto staged = model;
        const auto diagnostic = staged.removePlayer(playerId);
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
        lastDiagnostic = t(hydra::ui::TextId::PlayerRemoved);
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

    static bool selectionApplied(const UiDiagnostic& diagnostic) noexcept {
        return diagnostic.succeeded() || diagnostic.result == UiResult::MissingSetup;
    }

    void clearPendingAssignment() noexcept {
        pendingSeatAssignment.reset();
        pendingBothAssignment = false;
    }

    void showPlayerCreationForPendingAssignment() {
        advancedPage = true;
        lastDiagnostic = t(hydra::ui::TextId::AddPlayerToContinue);
        applyLayout();
        updatePreview();
        SetFocus(playerName);
    }

    void selectPlayerInCombo(HWND combo, std::string_view playerId) const {
        const auto player = std::find_if(
            model.players().players.begin(), model.players().players.end(),
            [&](const auto& value) { return value.playerId == playerId; });
        if (player == model.players().players.end()) return;
        const auto offset = std::distance(model.players().players.begin(), player);
        SendMessageW(combo, CB_SETCURSEL, offset + 1, 0);
    }

    void updatePlayerForAssignedSeat(SeatId seatId, HWND playerCombo) {
        const auto* existing = bindingForSeat(seatId);
        const auto playerId = selectedPlayerId(playerCombo);
        if (!playerId) {
            if (existing != nullptr) {
                const auto cleared = model.clearSeat(seatId);
                lastDiagnostic = cleared.succeeded()
                    ? std::wstring(t(hydra::ui::TextId::SeatSelectionUpdated))
                    : widen(cleared.message);
            }
            if (pendingSeatAssignment && *pendingSeatAssignment == seatId) {
                pendingSeatAssignment.reset();
            }
            syncSeatPresentation();
            updatePreview();
            return;
        }
        if (existing == nullptr) {
            if (pendingBothAssignment) {
                assignSelectedGameToBoth();
                return;
            }
            if (pendingSeatAssignment && *pendingSeatAssignment == seatId) {
                assignSelectedGame(seatId, playerCombo);
                return;
            }
            lastDiagnostic = t(hydra::ui::TextId::ChooseSeatForSelectedGame);
            updatePreview();
            return;
        }

        const std::string gameId = existing->gameId;
        const auto diagnostic = model.selectGame(seatId, *playerId, gameId);
        if (selectionApplied(diagnostic)) clearPendingAssignment();
        lastDiagnostic = diagnostic.succeeded()
            ? std::wstring(t(hydra::ui::TextId::SeatSelectionUpdated))
            : widen(diagnostic.message);
        syncSeatPresentation();
        updatePreview();
    }

    void assignSelectedGame(SeatId seatId, HWND playerCombo) {
        const auto* selectedGame = selectedLibraryGame();
        if (selectedGame == nullptr) {
            clearPendingAssignment();
            lastDiagnostic = t(hydra::ui::TextId::NoGameSelected);
            updatePreview();
            return;
        }
        const auto playerId = selectedPlayerId(playerCombo);
        if (!playerId) {
            pendingSeatAssignment = seatId;
            pendingBothAssignment = false;
            if (model.players().players.empty()) {
                showPlayerCreationForPendingAssignment();
                return;
            }
            lastDiagnostic = t(hydra::ui::TextId::ChoosePlayer);
            updatePreview();
            SetFocus(playerCombo);
            return;
        }

        const std::string gameId = selectedGame->game.gameId;
        const auto diagnostic = model.selectGame(seatId, *playerId, gameId);
        if (selectionApplied(diagnostic)) clearPendingAssignment();
        lastDiagnostic = diagnostic.succeeded()
            ? std::wstring(t(hydra::ui::TextId::SeatSelectionUpdated))
            : widen(diagnostic.message);
        syncSeatPresentation();
        updatePreview();
    }

    void assignSelectedGameToBoth() {
        const auto* selectedGame = selectedLibraryGame();
        if (selectedGame == nullptr) {
            clearPendingAssignment();
            lastDiagnostic = t(hydra::ui::TextId::NoGameSelected);
            updatePreview();
            return;
        }
        const auto firstPlayer = selectedPlayerId(seat1Player);
        const auto secondPlayer = selectedPlayerId(seat2Player);
        if (!firstPlayer || !secondPlayer) {
            pendingSeatAssignment.reset();
            pendingBothAssignment = true;
            if (model.players().players.empty()) {
                showPlayerCreationForPendingAssignment();
                return;
            }
            lastDiagnostic = t(hydra::ui::TextId::ChoosePlayer);
            updatePreview();
            SetFocus(!firstPlayer ? seat1Player : seat2Player);
            return;
        }

        const std::string gameId = selectedGame->game.gameId;
        const auto diagnostic = model.selectBoth(gameId, *firstPlayer, *secondPlayer);
        if (selectionApplied(diagnostic)) clearPendingAssignment();
        lastDiagnostic = diagnostic.succeeded()
            ? std::wstring(t(hydra::ui::TextId::SeatSelectionUpdated))
            : widen(diagnostic.message);
        syncSeatPresentation();
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
        const bool canActivate = currentPreview.summary.canActivate &&
                                 currentPreview.compileResult.plan.has_value();
        EnableWindow(playButton, canActivate && static_cast<bool>(activate) ? TRUE : FALSE);
        updateHero();
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
            MessageBoxW(hwnd, lastDiagnostic.c_str(),
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
        SetWindowTextW(heroStatus, t(hydra::ui::TextId::StatusStartingGame));
        SetWindowTextW(launchReason, lastDiagnostic.c_str());
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
            clearPendingAssignment();
            updateHero();
        } else if (id == kAddExe && notification == BN_CLICKED) {
            addExecutable();
        } else if (id == kAddPlayer && notification == BN_CLICKED) {
            addPlayer();
        } else if (id == kRenamePlayer && notification == BN_CLICKED) {
            renamePlayer();
        } else if (id == kRemovePlayer && notification == BN_CLICKED) {
            removePlayer();
        } else if (id == kSeat1Player && notification == CBN_SELCHANGE) {
            updatePlayerForAssignedSeat(1u, seat1Player);
        } else if (id == kSeat2Player && notification == CBN_SELCHANGE) {
            updatePlayerForAssignedSeat(2u, seat2Player);
        } else if (id == kUseSelectedSeatOne && notification == BN_CLICKED) {
            assignSelectedGame(1u, seat1Player);
        } else if (id == kUseSelectedSeatTwo && notification == BN_CLICKED) {
            assignSelectedGame(2u, seat2Player);
        } else if (id == kUseSelectedBothSeats && notification == BN_CLICKED) {
            assignSelectedGameToBoth();
        } else if (id == kCreateSetup && notification == BN_CLICKED) {
            createSetup();
        } else if (id == kPrivacySave && notification == BN_CLICKED) {
            savePrivacySettings();
        } else if (id == kExportLocalResult && notification == BN_CLICKED) {
            exportSelectedLocalResult();
        } else if (id == kDeleteLocalResult && notification == BN_CLICKED) {
            deleteSelectedLocalResult();
        } else if (id == kClearLocalResults && notification == BN_CLICKED) {
            clearLocalResults();
        } else if (id == kSetupNavigation && notification == BN_CLICKED) {
            advancedPage = true;
            updatePageVisibility();
            applyLayout();
            SetFocus(backToGames);
        } else if (id == kBackToGames && notification == BN_CLICKED) {
            advancedPage = false;
            updatePageVisibility();
            applyLayout();
            SetFocus(setupNavigation);
        } else if (id == kConfigure && notification == BN_CLICKED) {
            exitAction = LauncherExitAction::OpenSetupAndDiagnostics;
            DestroyWindow(hwnd);
        } else if (id == kPlay && notification == BN_CLICKED) {
            play();
        }
    }

    HWND hwnd{nullptr};
    HWND headerTitle{nullptr};
    HWND headerSubtitle{nullptr};
    HWND setupNavigation{nullptr};
    HWND heroEyebrow{nullptr};
    HWND heroTitle{nullptr};
    HWND heroStatus{nullptr};
    HWND useSeatOneButton{nullptr};
    HWND useSeatTwoButton{nullptr};
    HWND useBothSeatsButton{nullptr};
    HWND configureButton{nullptr};
    HWND seatsLabel{nullptr};
    HWND seat1Label{nullptr};
    HWND seat1Status{nullptr};
    HWND seat2Label{nullptr};
    HWND seat2Status{nullptr};
    HWND libraryLabel{nullptr};
    HWND launchReason{nullptr};
    HWND advancedHeading{nullptr};
    HWND backToGames{nullptr};
    HWND playerNameLabel{nullptr};
    HWND privacyHeading{nullptr};
    HWND privacyRetentionLabel{nullptr};
    HWND localResultsHeading{nullptr};
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
    HWND privacySharing{nullptr};
    HWND privacyRetention{nullptr};
    HWND privacySave{nullptr};
    HWND localResults{nullptr};
    HWND exportLocalResultButton{nullptr};
    HWND deleteLocalResultButton{nullptr};
    HWND clearLocalResultsButton{nullptr};
    HWND playButton{nullptr};
    HFONT font{nullptr};
    HFONT strongFont{nullptr};
    HFONT headingFont{nullptr};
    HBRUSH canvasBrush{nullptr};
    HBRUSH raisedBrush{nullptr};
    HBRUSH surfaceBrush{nullptr};
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
    bool playerProfilesWritable{true};
    community::CompatibilityShareModel privacyModel;
    std::unique_ptr<community::CompatibilityLocalStore> compatibilityStore;
    bool compatibilityHistoryWritable{false};
    bool compatibilityHistoryCorrupt{false};
    PlayPreview currentPreview;
    std::vector<GameRowView> gameRows;
    std::optional<std::string> focusedGameId;
    std::optional<SeatId> pendingSeatAssignment;
    bool pendingBothAssignment{false};
    std::wstring lastDiagnostic;
    std::uint64_t setupCounter{0u};
    bool advancedPage{false};
    LauncherExitAction exitAction{LauncherExitAction::Closed};
    hydra::ui::LauncherLayout layout;
    std::vector<HWND> primaryControls;
    std::vector<HWND> advancedControls;
};

LRESULT CALLBACK windowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<WindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_DRAWITEM && state != nullptr) {
        if (state->drawItem(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam))) return TRUE;
    }
    if (message == DM_GETDEFID && state != nullptr) {
        if (!state->advancedPage && state->playButton != nullptr &&
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
        const auto minimum = hydra::ui::computeLauncherLayout(
            static_cast<int>((hydra::ui::kLauncherMinimumClientWidthLogical * dpi + 95u) / 96u),
            static_cast<int>((hydra::ui::kLauncherMinimumClientHeightLogical * dpi + 95u) / 96u), dpi);
        RECT outer{0, 0, minimum.minimumClientWidth, minimum.minimumClientHeight};
        adjustWindowRectForDpiCompat(
            outer, WS_OVERLAPPEDWINDOW, WS_EX_DLGMODALFRAME, dpi);
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
        state->updateFontsForDpi(HIWORD(wParam));
        state->applyLayout();
        return 0;
    }
    if (message == WM_CTLCOLORSTATIC && state != nullptr) {
        return reinterpret_cast<LRESULT>(state->staticColor(
            reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
    }
    if ((message == WM_SETTINGCHANGE || message == WM_SYSCOLORCHANGE) && state != nullptr) {
        RedrawWindow(hwnd, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    }
    if (message == WM_ERASEBKGND && state != nullptr) return 1;
    if (message == WM_PAINT && state != nullptr) {
        state->paint();
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
    LauncherActivate activate) {
    const auto instance = GetModuleHandleW(nullptr);
    const auto locale = hydra::ui::systemLocale();
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBoxW(owner,
                    hydra::ui::text(hydra::ui::TextId::GameLibraryRegisterFailed, locale).data(),
                    L"HydraSeat", MB_OK | MB_ICONERROR);
        return LauncherExitAction::Closed;
    }

    const UINT dpi = dpiForWindowCompat(owner);
    RECT initial{0, 0, static_cast<LONG>((980u * dpi + 95u) / 96u),
                 static_cast<LONG>((720u * dpi + 95u) / 96u)};
    adjustWindowRectForDpiCompat(
        initial, WS_OVERLAPPEDWINDOW, WS_EX_DLGMODALFRAME, dpi);
    WindowState state(std::move(seats), std::move(requirements), std::move(activate));
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
    if (state.gameList != nullptr && IsWindowVisible(state.gameList) != FALSE &&
        IsWindowEnabled(state.gameList) != FALSE) {
        SetFocus(state.gameList);
    }
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
    if (owner != nullptr) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (state.font != nullptr) DeleteObject(state.font);
    if (state.strongFont != nullptr) DeleteObject(state.strongFont);
    if (state.headingFont != nullptr) DeleteObject(state.headingFont);
    if (state.canvasBrush != nullptr) DeleteObject(state.canvasBrush);
    if (state.raisedBrush != nullptr) DeleteObject(state.raisedBrush);
    if (state.surfaceBrush != nullptr) DeleteObject(state.surfaceBrush);
    if (quitRequested) PostQuitMessage(static_cast<int>(message.wParam));
    return state.exitAction;
}

} // namespace hydra::launcher_ui

#endif
