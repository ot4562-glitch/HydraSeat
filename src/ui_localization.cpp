#include "hydra/ui_localization.hpp"

#include <array>
#include <cstddef>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hydra::ui {
namespace {

struct Row {
    TextId id;
    std::wstring_view en;
    std::wstring_view ko;
    std::wstring_view zh;
};

constexpr Row kRows[] = {
    {TextId::GamesWindowTitle, L"HydraSeat - Games", L"HydraSeat - 게임", L"HydraSeat - 游戏"},
    {TextId::GamesHeading, L"Games", L"게임", L"游戏"},
    {TextId::InstalledTitles, L"Installed and manually added titles", L"설치된 게임 및 수동 추가 항목", L"已安装和手动添加的游戏"},
    {TextId::Refresh, L"Refresh", L"새로 고침", L"刷新"},
    {TextId::AddExecutable, L"Add EXE...", L"EXE 추가...", L"添加 EXE..."},
    {TextId::PlayersAndSeats, L"Players and Seats", L"플레이어 및 좌석", L"玩家与席位"},
    {TextId::PlayerName, L"Player name", L"플레이어 이름", L"玩家名称"},
    {TextId::AddPlayer, L"Add Player", L"플레이어 추가", L"添加玩家"},
    {TextId::Rename, L"Rename", L"이름 변경", L"重命名"},
    {TextId::Remove, L"Remove", L"삭제", L"移除"},
    {TextId::SeatLabel, L"Seat {0}", L"좌석 {0}", L"席位 {0}"},
    {TextId::Player, L"Player", L"플레이어", L"玩家"},
    {TextId::Game, L"Game", L"게임", L"游戏"},
    {TextId::ChoosePlayer, L"Choose Player", L"플레이어 선택", L"选择玩家"},
    {TextId::ChooseGame, L"Choose Game", L"게임 선택", L"选择游戏"},
    {TextId::CreateTwoPlayerSetup, L"Create two-player setup", L"2인 설정 만들기", L"创建双人设置"},
    {TextId::Play, L"Play", L"플레이", L"开始游戏"},
    {TextId::SameGameFirst, L"Choose the same game for both Seats first.", L"먼저 두 좌석에 같은 게임을 선택하세요.", L"请先为两个席位选择同一款游戏。"},
    {TextId::TwoPlayers, L"Two players", L"2인 플레이", L"双人游戏"},
    {TextId::SetupCreatedEvidencePending, L"Two-player setup created. Compatibility evidence is still required.", L"2인 설정을 만들었습니다. 호환성 근거는 아직 필요합니다.", L"双人设置已创建，仍需兼容性证据。"},
    {TextId::PlayPlanReady, L"The immutable Play plan is validated and ready for the runtime activation boundary.", L"변경 불가능한 Play 계획이 검증되어 런타임 활성화 경계에서 사용할 준비가 되었습니다.", L"不可变的游戏计划已验证，可交给运行时激活边界。"},
    {TextId::PlayDialogTitle, L"HydraSeat Play", L"HydraSeat 플레이", L"HydraSeat 游戏"},
    {TextId::GameLibraryRegisterFailed, L"The game library window could not be registered.", L"게임 라이브러리 창을 등록할 수 없습니다.", L"无法注册游戏库窗口。"},
    {TextId::GameLibraryInitializeFailed, L"The game library could not initialize from local state.", L"로컬 상태에서 게임 라이브러리를 초기화할 수 없습니다.", L"无法从本地状态初始化游戏库。"},
    {TextId::SteamRefreshed, L"Local Steam library refreshed read-only.", L"로컬 Steam 라이브러리를 읽기 전용으로 새로 고쳤습니다.", L"已以只读方式刷新本地 Steam 游戏库。"},
    {TextId::PlayerAdded, L"Player added.", L"플레이어를 추가했습니다.", L"已添加玩家。"},
    {TextId::PlayerRenamed, L"Player renamed.", L"플레이어 이름을 변경했습니다.", L"已重命名玩家。"},
    {TextId::PlayerRemoved, L"Player removed.", L"플레이어를 삭제했습니다.", L"已移除玩家。"},
    {TextId::PlayerProfilesLoadFailed, L"Player profiles could not be loaded. Existing data was left untouched.", L"플레이어 프로필을 불러올 수 없습니다. 기존 데이터는 변경하지 않았습니다.", L"无法加载玩家资料。现有数据未被修改。"},
    {TextId::PlayerProfilesSaveFailed, L"Player profile changes could not be saved, so the previous roster was kept.", L"플레이어 프로필 변경을 저장할 수 없어 이전 목록을 유지했습니다.", L"无法保存玩家资料更改，因此保留了之前的玩家列表。"},
    {TextId::ChooseRosterPlayer, L"Choose a Player from the roster first.", L"먼저 목록에서 플레이어를 선택하세요.", L"请先从列表中选择玩家。"},
    {TextId::ManualExecutableIdentityMissing, L"Manual executable returned no stable identity.", L"수동 실행 파일에서 안정적인 식별자를 얻지 못했습니다.", L"手动添加的可执行文件没有稳定标识。"},
    {TextId::ExecutableAdded, L"Executable added after read-only PE validation.", L"읽기 전용 PE 검증 후 실행 파일을 추가했습니다.", L"只读 PE 验证通过后已添加可执行文件。"},
    {TextId::ManualGamesLoadFailed, L"Saved manual games could not be loaded. Existing data was left untouched.", L"저장된 수동 게임을 불러올 수 없습니다. 기존 데이터는 변경하지 않았습니다.", L"无法加载已保存的手动游戏。现有数据未被修改。"},
    {TextId::ManualGamesSaveFailed, L"The manual game could not be saved, so the previous library was kept.", L"수동 게임을 저장할 수 없어 이전 라이브러리를 유지했습니다.", L"无法保存手动游戏，因此保留了之前的游戏库。"},
    {TextId::SeatSelectionUpdated, L"Seat selection updated.", L"좌석 선택을 업데이트했습니다.", L"席位选择已更新。"},
    {TextId::SeatLauncherTitle, L"HydraSeat - Seat {0}", L"HydraSeat - 좌석 {0}", L"HydraSeat - 席位 {0}"},
    {TextId::EndPlaying, L"End Playing", L"플레이 종료", L"结束游戏"},
    {TextId::Reconnect, L"Reconnect", L"다시 연결", L"重新连接"},
    {TextId::None, L"None", L"없음", L"无"},
    {TextId::CurrentSelectedPlayer, L"Current / selected Player: {0}", L"현재 / 선택한 플레이어: {0}", L"当前 / 已选玩家：{0}"},
    {TextId::CurrentSelectedGame, L"Current / selected game: {0}", L"현재 / 선택한 게임: {0}", L"当前 / 已选游戏：{0}"},
    {TextId::RecentGames, L"Recent games: {0}", L"최근 게임: {0}", L"最近游戏：{0}"},
    {TextId::AvailableGames, L"Available games: {0}", L"사용 가능한 게임: {0}", L"可用游戏：{0}"},
    {TextId::NotificationHostDisconnected, L"HydraSeat is disconnected. Reconnect to refresh this Seat.", L"HydraSeat 연결이 끊겼습니다. 이 좌석 상태를 새로 고치려면 다시 연결하세요.", L"HydraSeat 已断开。请重新连接以刷新此席位。"},
    {TextId::NotificationGameStarting, L"The game is starting.", L"게임을 시작하고 있습니다.", L"游戏正在启动。"},
    {TextId::NotificationGameWarning, L"The game needs attention. End Playing if it does not recover.", L"게임 상태를 확인해야 합니다. 복구되지 않으면 플레이를 종료하세요.", L"游戏需要处理；若无法恢复，请结束游戏。"},
    {TextId::NotificationGameRecovery, L"This Seat needs recovery. Refresh status before taking another action.", L"이 좌석은 복구가 필요합니다. 다른 작업 전에 상태를 새로 고치세요.", L"此席位需要恢复。执行其他操作前请刷新状态。"},
    {TextId::NotificationDevicesRequired, L"This Seat needs a device setting before the game can start.", L"게임을 시작하기 전에 이 좌석의 장치 설정이 필요합니다.", L"开始游戏前，此席位需要完成设备设置。"},
    {TextId::NotificationRequirementsReview, L"Game requirements need review before starting.", L"게임을 시작하기 전에 요구 사항을 검토해야 합니다.", L"开始游戏前需要检查游戏要求。"},
    {TextId::NotificationSetupReview, L"The two-player setup needs review.", L"2인 설정을 검토해야 합니다.", L"需要检查双人设置。"},
    {TextId::NotificationProtectionReview, L"This Protected / Experimental setup needs acknowledgement.", L"이 보호됨 / 실험적 설정은 명시적 확인이 필요합니다.", L"此受保护 / 实验性设置需要明确确认。"},
    {TextId::NotificationProviderUnavailable, L"The game provider is unavailable. Refresh and try again.", L"게임 제공자를 사용할 수 없습니다. 새로 고친 뒤 다시 시도하세요.", L"游戏提供方不可用。请刷新后重试。"},
    {TextId::NotificationPlayerAccount, L"Choose the Player account to use for this game.", L"이 게임에 사용할 플레이어 계정을 선택하세요.", L"请选择此游戏要使用的玩家账户。"},
    {TextId::NotificationPreflightInformation, L"The selected game has setup details to review.", L"선택한 게임의 설정 세부 정보를 검토하세요.", L"所选游戏有需要检查的设置详情。"},
    {TextId::NotificationPreflightReview, L"The selected game cannot start until its setup is reviewed.", L"설정을 검토하기 전에는 선택한 게임을 시작할 수 없습니다.", L"检查设置之前无法启动所选游戏。"},
    {TextId::SetLater, L"Set later", L"나중에 설정", L"稍后设置"},
    {TextId::OptionalSeatSetup, L"Seat hardware setup is optional until the selected game requires a device.", L"선택한 게임에서 장치를 요구하기 전까지 좌석 하드웨어 설정은 선택 사항입니다.", L"在所选游戏需要设备之前，席位硬件设置可以暂缓。"},
    {TextId::ProtectedExperimentConfirmation, L"I understand this is a Protected / Experimental path and may fail.", L"이 경로가 보호됨 / 실험적이며 실패할 수 있음을 이해합니다.", L"我了解这是受保护 / 实验性路径，可能失败。"},
    {TextId::RecoveryAction, L"Recovery", L"복구", L"恢复"},
    {TextId::StatusDisconnected, L"Disconnected", L"연결 끊김", L"已断开"},
    {TextId::StatusReady, L"Ready", L"준비됨", L"就绪"},
    {TextId::StatusReadyToStart, L"Ready to start", L"시작 준비됨", L"可以启动"},
    {TextId::StatusStartingGame, L"Starting game", L"게임 시작 중", L"正在启动游戏"},
    {TextId::StatusPlaying, L"Playing", L"플레이 중", L"游戏中"},
    {TextId::StatusEndingPlay, L"Ending play session", L"플레이 종료 중", L"正在结束游戏"},
    {TextId::StatusNeedsAttention, L"Game needs attention", L"게임 상태 확인 필요", L"游戏需要处理"},
    {TextId::StatusRecoveryRequired, L"Recovery required", L"복구 필요", L"需要恢复"},
    {TextId::ConfirmEndPlayingPrompt, L"End Playing for this Seat? The other Seat will not be stopped.", L"이 좌석의 플레이를 종료할까요? 다른 좌석은 중지되지 않습니다.", L"结束此席位的游戏？另一个席位不会被停止。"},
    {TextId::RecoveryHelpText, L"Refresh this Seat first. If recovery still fails, use the independently validated reset tool from Windows.", L"먼저 이 좌석 상태를 새로 고치세요. 그래도 복구되지 않으면 Windows에서 독립 검증된 reset 도구를 사용하세요.", L"请先刷新此席位。若仍无法恢复，请从 Windows 使用独立验证的重置工具。"},
    {TextId::EmergencyResetHelpText, L"Emergency reset is never executed by a Seat hotkey. Use hydra_reset.exe explicitly from Windows when normal recovery cannot complete.", L"좌석 단축키는 긴급 reset을 직접 실행하지 않습니다. 정상 복구가 실패한 경우 Windows에서 hydra_reset.exe를 명시적으로 실행하세요.", L"席位快捷键绝不会直接执行紧急重置。正常恢复失败时，请在 Windows 中明确运行 hydra_reset.exe。"},
    {TextId::LauncherSubtitle, L"Two-player local gaming", L"두 사람이 함께하는 로컬 게이밍", L"双人本地游戏"},
    {TextId::SelectedGame, L"SELECTED GAME", L"선택한 게임", L"已选游戏"},
    {TextId::NoGameSelected, L"Choose a game from your library", L"라이브러리에서 게임을 선택하세요", L"请从游戏库选择游戏"},
    {TextId::ReadyToPlay, L"● Ready for verified launch", L"● 검증된 실행 준비됨", L"● 已准备好进行验证启动"},
    {TextId::NeedsSetup, L"○ Needs setup or current compatibility evidence", L"○ 설정 또는 최신 호환성 근거 필요", L"○ 需要设置或当前兼容性证据"},
    {TextId::SetupAndDiagnostics, L"Setup & Diagnostics", L"설정 및 진단", L"设置与诊断"},
    {TextId::BackToGames, L"Back to Games", L"게임으로 돌아가기", L"返回游戏"},
    {TextId::SectionSeats, L"01 · SEATS", L"01 · 좌석", L"01 · 席位"},
    {TextId::SectionLibrary, L"02 · LIBRARY", L"02 · 라이브러리", L"02 · 游戏库"},
    {TextId::SectionCompatibility, L"03 · COMPATIBILITY", L"03 · 호환성", L"03 · 兼容性"},
    {TextId::RuntimeLaunchUnavailable, L"A valid plan exists, but no production runtime plan installer is connected. Nothing was launched.", L"유효한 계획은 있지만 프로덕션 런타임 계획 설치기가 연결되지 않았습니다. 아무것도 실행하지 않았습니다.", L"计划有效，但尚未连接生产运行时计划安装器。未启动任何内容。"},
    {TextId::Configure, L"Configure", L"구성", L"配置"},
    {TextId::SeatHardwareSetup, L"Seat hardware setup", L"좌석 하드웨어 설정", L"席位硬件设置"},
    {TextId::UseSeatOne, L"Use Seat 1", L"좌석 1에서 사용", L"用于席位 1"},
    {TextId::UseSeatTwo, L"Use Seat 2", L"좌석 2에서 사용", L"用于席位 2"},
    {TextId::UseBothSeats, L"Use Both", L"두 좌석에서 사용", L"用于两个席位"},
    {TextId::ChooseSeatForSelectedGame, L"Choose which Seat should use this game.", L"이 게임을 사용할 좌석을 선택하세요.", L"请选择要使用此游戏的席位。"},
    {TextId::AssignedToSeat, L"Selected for Seat {0}", L"좌석 {0}에 선택됨", L"已选择用于席位 {0}"},
    {TextId::AssignedToBothSeats, L"Selected for both Seats", L"두 좌석에 선택됨", L"已选择用于两个席位"},
    {TextId::StatusNotSelected, L"○ Not selected", L"○ 선택 안 됨", L"○ 未选择"},
    {TextId::LaunchSelectionEmpty, L"Choose a game, at least one Seat, and a Player.", L"게임과 하나 이상의 좌석, 플레이어를 선택하세요.", L"请选择游戏、至少一个席位和玩家。"},
    {TextId::AddPlayerToContinue, L"Add a Player to continue. After creation, HydraSeat will return to the game selection you started.", L"계속하려면 플레이어를 추가하세요. 생성 후 방금 시작한 게임 선택으로 돌아갑니다.", L"请先添加玩家。创建后，HydraSeat 会返回刚才开始的游戏选择流程。"},
    {TextId::ChooseSecondPlayerToContinue, L"Choose the other Seat's Player to continue with both Seats.", L"두 좌석에서 계속하려면 다른 좌석의 플레이어를 선택하세요.", L"要继续使用两个席位，请选择另一个席位的玩家。"},
    {TextId::PrivacyHeading, L"Privacy", L"개인정보", L"隐私"},
    {TextId::CommunitySharing, L"Allow optional community compatibility sharing", L"선택적 커뮤니티 호환성 공유 허용", L"允许可选的社区兼容性共享"},
    {TextId::RetainedResults, L"Retain local results", L"로컬 결과 보관 수", L"保留本地结果数"},
    {TextId::SavePrivacy, L"Save privacy", L"개인정보 설정 저장", L"保存隐私设置"},
    {TextId::PrivacySaved, L"Privacy settings saved.", L"개인정보 설정을 저장했습니다.", L"隐私设置已保存。"},
    {TextId::PrivacySettingsLoadFailed, L"Privacy settings could not be loaded. Community sharing remains disabled.", L"개인정보 설정을 불러올 수 없습니다. 커뮤니티 공유는 비활성 상태로 유지됩니다.", L"无法加载隐私设置。社区共享将保持关闭。"},
    {TextId::PrivacySettingsSaveFailed, L"Privacy settings could not be saved.", L"개인정보 설정을 저장할 수 없습니다.", L"无法保存隐私设置。"},
    {TextId::PrivacyInvalidRetention, L"Retained local results must be between 1 and 64.", L"로컬 결과 보관 수는 1~64 사이여야 합니다.", L"本地结果保留数量必须在 1 到 64 之间。"},
    {TextId::LocalResultsHeading, L"Local compatibility results", L"로컬 호환성 결과", L"本地兼容性结果"},
    {TextId::ExportSelectedResult, L"Export selected", L"선택 결과 내보내기", L"导出所选结果"},
    {TextId::DeleteSelectedResult, L"Delete selected", L"선택 결과 삭제", L"删除所选结果"},
    {TextId::ClearLocalResults, L"Clear results", L"결과 모두 삭제", L"清除结果"},
    {TextId::ChooseLocalResult, L"Choose a local result first.", L"먼저 로컬 결과를 선택하세요.", L"请先选择一个本地结果。"},
    {TextId::ResultExported, L"Local result exported.", L"로컬 결과를 내보냈습니다.", L"已导出本地结果。"},
    {TextId::ResultDeleted, L"Local result deleted.", L"로컬 결과를 삭제했습니다.", L"已删除本地结果。"},
    {TextId::ResultsCleared, L"Local results cleared.", L"로컬 결과를 모두 삭제했습니다.", L"已清除本地结果。"},
    {TextId::ResultHistoryLoadFailed, L"Local compatibility results could not be loaded.", L"로컬 호환성 결과를 불러올 수 없습니다.", L"无法加载本地兼容性结果。"},
    {TextId::ResultHistorySaveFailed, L"Local compatibility results could not be saved.", L"로컬 호환성 결과를 저장할 수 없습니다.", L"无法保存本地兼容性结果。"},
    {TextId::ResultExportFailed, L"Local result could not be exported.", L"로컬 결과를 내보낼 수 없습니다.", L"无法导出本地结果。"},
    {TextId::DeleteResultPrompt, L"Delete the selected local compatibility result?", L"선택한 로컬 호환성 결과를 삭제할까요?", L"删除所选本地兼容性结果？"},
    {TextId::ClearResultsPrompt, L"Delete all local compatibility results?", L"모든 로컬 호환성 결과를 삭제할까요?", L"删除所有本地兼容性结果？"},
};

static_assert(std::size(kRows) == static_cast<std::size_t>(TextId::Count),
              "every TextId must have exactly one localization row");

constexpr bool rowsCompleteAndUnique() noexcept {
    std::array<bool, static_cast<std::size_t>(TextId::Count)> seen{};
    for (const auto& row : kRows) {
        const auto index = static_cast<std::size_t>(row.id);
        if (index >= seen.size() || seen[index]) return false;
        seen[index] = true;
    }
    for (const bool present : seen) {
        if (!present) return false;
    }
    return true;
}

static_assert(rowsCompleteAndUnique(),
              "every TextId must have exactly one explicitly keyed localization row");

std::wstring_view pick(const Row& row, Locale locale) noexcept {
    switch (locale) {
        case Locale::KoreanKorea: return row.ko.empty() ? row.en : row.ko;
        case Locale::ChineseSimplified: return row.zh.empty() ? row.en : row.zh;
        case Locale::EnglishUnitedStates: return row.en;
    }
    return row.en;
}

void replaceAll(std::wstring& value, std::wstring_view marker, std::wstring_view replacement) {
    std::size_t position = 0;
    while ((position = value.find(marker, position)) != std::wstring::npos) {
        value.replace(position, marker.size(), replacement);
        position += replacement.size();
    }
}

} // namespace

Locale localeFromTag(std::string_view tag) noexcept {
    if (tag.size() >= 2u && (tag[0] == 'k' || tag[0] == 'K') &&
        (tag[1] == 'o' || tag[1] == 'O')) {
        return Locale::KoreanKorea;
    }
    if (tag.size() >= 2u && (tag[0] == 'z' || tag[0] == 'Z') &&
        (tag[1] == 'h' || tag[1] == 'H')) {
        return Locale::ChineseSimplified;
    }
    return Locale::EnglishUnitedStates;
}

Locale systemLocale() noexcept {
#ifdef _WIN32
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
        if ((name[0] == L'k' || name[0] == L'K') &&
            (name[1] == L'o' || name[1] == L'O')) {
            return Locale::KoreanKorea;
        }
        if ((name[0] == L'z' || name[0] == L'Z') &&
            (name[1] == L'h' || name[1] == L'H')) {
            return Locale::ChineseSimplified;
        }
    }
#endif
    return Locale::EnglishUnitedStates;
}

std::string_view localeTag(Locale locale) noexcept {
    switch (locale) {
        case Locale::EnglishUnitedStates: return "en-US";
        case Locale::KoreanKorea: return "ko-KR";
        case Locale::ChineseSimplified: return "zh-CN";
    }
    return "en-US";
}

std::wstring_view text(TextId id, Locale locale) noexcept {
    for (const auto& row : kRows) {
        if (row.id == id) return pick(row, locale);
    }
    return {};
}

std::wstring formatOne(TextId id, Locale locale, std::wstring_view value) {
    std::wstring output(text(id, locale));
    replaceAll(output, L"{0}", value);
    return output;
}

std::wstring formatTwo(TextId id, Locale locale,
                       std::wstring_view first, std::wstring_view second) {
    std::wstring output(text(id, locale));
    replaceAll(output, L"{0}", first);
    replaceAll(output, L"{1}", second);
    return output;
}

std::wstring_view notificationText(std::string_view messageId, Locale locale) noexcept {
    if (messageId == "seat.host.disconnected") return text(TextId::NotificationHostDisconnected, locale);
    if (messageId == "seat.game.starting") return text(TextId::NotificationGameStarting, locale);
    if (messageId == "seat.game.warning") return text(TextId::NotificationGameWarning, locale);
    if (messageId == "seat.game.recovery") return text(TextId::NotificationGameRecovery, locale);
    if (messageId == "seat.requirements.devices") return text(TextId::NotificationDevicesRequired, locale);
    if (messageId == "seat.requirements.review") return text(TextId::NotificationRequirementsReview, locale);
    if (messageId == "seat.setup.review") return text(TextId::NotificationSetupReview, locale);
    if (messageId == "seat.protection.review") return text(TextId::NotificationProtectionReview, locale);
    if (messageId == "seat.provider.unavailable") return text(TextId::NotificationProviderUnavailable, locale);
    if (messageId == "seat.player.account") return text(TextId::NotificationPlayerAccount, locale);
    if (messageId == "seat.preflight.information") return text(TextId::NotificationPreflightInformation, locale);
    if (messageId == "seat.preflight.review") return text(TextId::NotificationPreflightReview, locale);
    return {};
}

std::wstring_view preflightText(std::string_view code, Locale locale) noexcept {
    if (code == "plan.MissingDisplay" || code == "plan.MissingKeyboard" ||
        code == "plan.MissingMouse" || code == "plan.MissingController" ||
        code == "plan.MissingAudioOutput" || code == "plan.DuplicateExclusiveHardware") {
        return text(TextId::NotificationDevicesRequired, locale);
    }
    if (code == "plan.MissingRequirement" || code == "plan.DuplicateRequirement" ||
        code == "plan.StaleCompatibility" || code == "plan.MissingCapability") {
        return text(TextId::NotificationRequirementsReview, locale);
    }
    if (code == "plan.MissingTwoPlayerSetup" || code == "plan.InvalidTwoPlayerSetup" ||
        code.rfind("mutation.", 0u) == 0u) {
        return text(TextId::NotificationSetupReview, locale);
    }
    if (code == "plan.HighRiskApprovalRequired" || code == "risk.protected") {
        return text(TextId::NotificationProtectionReview, locale);
    }
    if (code == "plan.ProviderUnavailable" || code == "plan.MissingProvider" ||
        code == "plan.ProviderLaunchRejected") {
        return text(TextId::NotificationProviderUnavailable, locale);
    }
    if (code == "plan.AmbiguousAccountReference") return text(TextId::NotificationPlayerAccount, locale);
    if (code.rfind("requires.", 0u) == 0u || code == "setup.two-player") {
        return text(TextId::NotificationPreflightInformation, locale);
    }
    return text(TextId::NotificationPreflightReview, locale);
}

} // namespace hydra::ui
