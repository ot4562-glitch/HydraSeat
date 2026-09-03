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
    {TextId::AddExecutable, L"Add Game...", L"게임 추가...", L"添加游戏..."},
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
    {TextId::SetupCreatedEvidencePending, L"Two-player setup created. Compatibility still needs to be tested.", L"2인 설정을 만들었습니다. 호환성 테스트가 아직 필요합니다.", L"双人设置已创建，仍需进行兼容性测试。"},
    {TextId::PlayPlanReady, L"Ready to start the selected game.", L"선택한 게임을 시작할 준비가 됐습니다.", L"已准备好启动所选游戏。"},
    {TextId::PlayDialogTitle, L"HydraSeat Play", L"HydraSeat 플레이", L"HydraSeat 游戏"},
    {TextId::GameLibraryRegisterFailed, L"The game library window could not be registered.", L"게임 라이브러리 창을 등록할 수 없습니다.", L"无法注册游戏库窗口。"},
    {TextId::GameLibraryInitializeFailed, L"The game library could not initialize from local state.", L"로컬 상태에서 게임 라이브러리를 초기화할 수 없습니다.", L"无法从本地状态初始化游戏库。"},
    {TextId::SteamRefreshed, L"Steam library refreshed.", L"Steam 라이브러리를 새로 고쳤습니다.", L"Steam 游戏库已刷新。"},
    {TextId::PlayerAdded, L"Player added.", L"플레이어를 추가했습니다.", L"已添加玩家。"},
    {TextId::PlayerRenamed, L"Player renamed.", L"플레이어 이름을 변경했습니다.", L"已重命名玩家。"},
    {TextId::PlayerRemoved, L"Player removed.", L"플레이어를 삭제했습니다.", L"已移除玩家。"},
    {TextId::PlayerProfilesLoadFailed, L"Player profiles could not be loaded. Existing data was left untouched.", L"플레이어 프로필을 불러올 수 없습니다. 기존 데이터는 변경하지 않았습니다.", L"无法加载玩家资料。现有数据未被修改。"},
    {TextId::PlayerProfilesSaveFailed, L"Player profile changes could not be saved, so the previous roster was kept.", L"플레이어 프로필 변경을 저장할 수 없어 이전 목록을 유지했습니다.", L"无法保存玩家资料更改，因此保留了之前的玩家列表。"},
    {TextId::ChooseRosterPlayer, L"Choose a Player from the roster first.", L"먼저 목록에서 플레이어를 선택하세요.", L"请先从列表中选择玩家。"},
    {TextId::ManualExecutableIdentityMissing, L"This game executable could not be identified reliably.", L"이 게임 실행 파일을 안정적으로 식별할 수 없습니다.", L"无法可靠识别此游戏可执行文件。"},
    {TextId::ExecutableAdded, L"Game added.", L"게임을 추가했습니다.", L"已添加游戏。"},
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
    {TextId::NotificationProtectionReview, L"Protected / Experimental game: review and confirm the warning.", L"보호됨 / 실험적 게임입니다. 경고를 확인하고 동의하세요.", L"受保护 / 实验性游戏：请查看并确认警告。"},
    {TextId::NotificationProviderUnavailable, L"The game provider is unavailable. Refresh and try again.", L"게임 제공자를 사용할 수 없습니다. 새로 고친 뒤 다시 시도하세요.", L"游戏提供方不可用。请刷新后重试。"},
    {TextId::NotificationPlayerAccount, L"Choose the Player account to use for this game.", L"이 게임에 사용할 플레이어 계정을 선택하세요.", L"请选择此游戏要使用的玩家账户。"},
    {TextId::NotificationPreflightInformation, L"The selected game has setup details to review.", L"선택한 게임의 설정 세부 정보를 검토하세요.", L"所选游戏有需要检查的设置详情。"},
    {TextId::NotificationPreflightReview, L"The selected game cannot start until its setup is reviewed.", L"설정을 검토하기 전에는 선택한 게임을 시작할 수 없습니다.", L"检查设置之前无法启动所选游戏。"},
    {TextId::SetLater, L"Set later", L"나중에 설정", L"稍后设置"},
    {TextId::OptionalSeatSetup, L"You can finish Seat setup later. HydraSeat will ask only for devices this game needs.", L"좌석 설정은 나중에 완료해도 됩니다. 이 게임에 필요한 장치만 안내합니다.", L"席位设置可稍后完成。HydraSeat 只会提示此游戏需要的设备。"},
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
    {TextId::RecoveryHelpText, L"Refresh this Seat first. If recovery still fails, run hydra_reset.exe from Windows.", L"먼저 이 좌석 상태를 새로 고치세요. 그래도 복구되지 않으면 Windows에서 hydra_reset.exe를 실행하세요.", L"请先刷新此席位。若仍无法恢复，请从 Windows 运行 hydra_reset.exe。"},
    {TextId::EmergencyResetHelpText, L"A Seat shortcut never runs emergency reset. If normal recovery fails, run hydra_reset.exe from Windows.", L"좌석 단축키로 긴급 초기화를 실행하지 않습니다. 정상 복구가 실패하면 Windows에서 hydra_reset.exe를 실행하세요.", L"席位快捷键不会执行紧急重置。正常恢复失败时，请从 Windows 运行 hydra_reset.exe。"},
    {TextId::LauncherSubtitle, L"Two-player local gaming", L"두 사람이 함께하는 로컬 게이밍", L"双人本地游戏"},
    {TextId::SelectedGame, L"SELECTED GAME", L"선택한 게임", L"已选游戏"},
    {TextId::NoGameSelected, L"Choose a game from your library", L"라이브러리에서 게임을 선택하세요", L"请从游戏库选择游戏"},
    {TextId::ReadyToPlay, L"● Ready to start", L"● 시작 준비됨", L"● 可以启动"},
    {TextId::NeedsSetup, L"○ Review setup before playing", L"○ 플레이 전 설정 확인 필요", L"○ 开始游戏前请检查设置"},
    {TextId::CheckCompatibility, L"Check compatibility", L"호환성 검사", L"检查兼容性"},
    {TextId::CompatibilityCheckRunning, L"Checking the selected game locally...", L"선택한 게임을 로컬에서 검사하는 중...", L"正在本地检查所选游戏..."},
    {TextId::CompatibilityCheckControlledComplete, L"Controlled compatibility evidence was saved. Physical validation is still required before Play can become ready.", L"제어된 호환성 증거를 저장했습니다. Play가 준비되려면 실제 하드웨어 검증이 추가로 필요합니다.", L"已保存受控兼容性证据。Play 可用前仍需要真实硬件验证。"},
    {TextId::CompatibilityReviewPrompt, L"Review these requirements for the selected game. Continue only if they match how you intend to play.\n\n{0}", L"선택한 게임의 요구사항을 검토하세요. 실제 플레이 방식과 일치할 때만 계속하세요.\n\n{0}", L"请检查所选游戏的要求。仅当它们与您的实际玩法一致时继续。\n\n{0}"},
    {TextId::CompatibilityRiskReviewPrompt, L"Before the local check, classify the selected game.\n\nDoes this title use anti-cheat or other protection-sensitive technology, or are you intentionally testing a Protected / Experimental path?\n\nYes = Protected / Experimental (the check will be blocked)\nNo = reviewed as Standard for this check\nCancel = unknown; do not run", L"로컬 검사 전에 선택한 게임의 위험 분류를 확인하세요.\n\n이 게임이 안티치트 또는 기타 보호 민감 기술을 사용하거나, 보호됨 / 실험적 경로를 의도적으로 시험하는 게임인가요?\n\n예 = 보호됨 / 실험적(검사를 차단함)\n아니요 = 이번 검사에서 일반(Standard)으로 검토함\n취소 = 알 수 없음; 실행하지 않음", L"本地检查前，请确认所选游戏的风险分类。\n\n该游戏是否使用反作弊或其他对保护机制敏感的技术，或者您是否在有意测试受保护 / 实验性路径？\n\n是 = 受保护 / 实验性（检查将被阻止）\n否 = 本次检查已审核为标准游戏\n取消 = 未知；不运行"},
    {TextId::CompatibilityRiskProtectedBlocked, L"HydraSeat does not run the local compatibility check for a Protected / Experimental target. No game was launched.", L"HydraSeat는 보호됨 / 실험적 대상으로 로컬 호환성 검사를 실행하지 않습니다. 게임을 실행하지 않았습니다.", L"HydraSeat 不会对受保护 / 实验性目标运行本地兼容性检查。未启动游戏。"},
    {TextId::CompatibilityRiskUnknownBlocked, L"The game's protection risk is still unknown. Review the risk before running a local compatibility check. No game was launched.", L"게임의 보호 위험 분류가 아직 확인되지 않았습니다. 로컬 호환성 검사를 실행하기 전에 위험을 검토하세요. 게임을 실행하지 않았습니다.", L"该游戏的保护风险仍未知。运行本地兼容性检查前请先审核风险。未启动游戏。"},
    {TextId::CompatibilityRequirementAudioOutput, L"Audio output", L"오디오 출력", L"音频输出"},
    {TextId::CompatibilityRequirementWindowOwnership, L"Window ownership", L"창 소유권", L"窗口所有权"},
    {TextId::PhysicalEvidenceSelectPrompt, L"Normal Play requires accepted P3-HW physical evidence. If you already completed the Phase 3 hardware acceptance, select its phase3-hardware-manifest.json now. HydraSeat will save only a reference and will revalidate the evidence every time it is used.\n\nSelect a completed manifest now?", L"일반 Play에는 승인된 P3-HW 실제 하드웨어 증거가 필요합니다. Phase 3 하드웨어 검증을 이미 완료했다면 해당 phase3-hardware-manifest.json을 지금 선택하세요. HydraSeat는 참조 경로만 저장하고 사용할 때마다 증거를 다시 검증합니다.\n\n완료된 manifest를 지금 선택할까요?", L"正常 Play 需要已接受的 P3-HW 真实硬件证据。如果您已经完成 Phase 3 硬件验收，请现在选择其 phase3-hardware-manifest.json。HydraSeat 只保存引用路径，并会在每次使用时重新验证证据。\n\n现在选择已完成的 manifest 吗？"},
    {TextId::PhysicalEvidenceDialogTitle, L"Select P3-HW hardware evidence", L"P3-HW 하드웨어 증거 선택", L"选择 P3-HW 硬件证据"},
    {TextId::PhysicalEvidenceSelectionFailed, L"The selected P3-HW evidence was not accepted. Nothing was promoted to Physical.\n\n{0}", L"선택한 P3-HW 증거를 승인할 수 없습니다. 어떤 증거도 Physical로 승격하지 않았습니다.\n\n{0}", L"所选 P3-HW 证据未被接受。没有任何证据被提升为 Physical。\n\n{0}"},
    {TextId::PhysicalEvidenceAcceptedNoProfile, L"The physical hardware evidence is accepted and will be revalidated by Host, but this HydraSeat build does not yet include a reviewed Gate C profile for the selected game. Play remains unavailable.", L"실제 하드웨어 증거를 승인했으며 Host가 다시 검증합니다. 하지만 이 HydraSeat 빌드에는 선택한 게임에 대해 검토 완료된 Gate C 프로필이 아직 없습니다. Play는 계속 사용할 수 없습니다.", L"真实硬件证据已被接受，并会由 Host 重新验证，但此 HydraSeat 版本尚未包含针对所选游戏的已审核 Gate C 配置。Play 仍不可用。"},
    {TextId::PhysicalEvidencePrerequisitesReady, L"Accepted physical evidence and a reviewed game profile are available. A real guided physical game validation is still required before Play can become ready.", L"승인된 실제 하드웨어 증거와 검토된 게임 프로필이 준비되었습니다. Play가 준비되려면 실제 게임을 사용하는 guided physical 검증이 아직 필요합니다.", L"已具备接受的真实硬件证据和已审核的游戏配置。在 Play 可用之前，仍需要进行真实游戏的 guided physical 验证。"},
    {TextId::SetupAndDiagnostics, L"Setup & Diagnostics", L"설정 및 진단", L"设置与诊断"},
    {TextId::BackToGames, L"Back to Games", L"게임으로 돌아가기", L"返回游戏"},
    {TextId::SectionSeats, L"01 · PLAYERS", L"01 · 플레이어", L"01 · 玩家"},
    {TextId::SectionLibrary, L"02 · LIBRARY", L"02 · 라이브러리", L"02 · 游戏库"},
    {TextId::SectionCompatibility, L"03 · COMPATIBILITY", L"03 · 호환성", L"03 · 兼容性"},
    {TextId::RuntimeLaunchUnavailable, L"HydraSeat can't start this game right now. Nothing was launched.", L"지금은 HydraSeat에서 이 게임을 시작할 수 없습니다. 아무것도 실행하지 않았습니다.", L"HydraSeat 目前无法启动此游戏。未启动任何内容。"},
    {TextId::Configure, L"Configure", L"구성", L"配置"},
    {TextId::SeatHardwareSetup, L"Seat Setup", L"좌석 설정", L"席位设置"},
    {TextId::UseSeatOne, L"Use Seat 1", L"좌석 1 사용", L"使用席位 1"},
    {TextId::UseSeatTwo, L"Use Seat 2", L"좌석 2 사용", L"使用席位 2"},
    {TextId::UseBothSeats, L"Use Both", L"두 좌석 사용", L"使用两个席位"},
    {TextId::ChooseSeatForSelectedGame, L"Choose Player 1. Add Player 2 only for two-player play.", L"플레이어 1을 선택하세요. 2인 플레이일 때만 플레이어 2를 선택합니다.", L"请选择玩家 1。仅在双人游戏时选择玩家 2。"},
    {TextId::AssignedToSeat, L"Selected for Player {0}", L"플레이어 {0}에 선택됨", L"已选择用于玩家 {0}"},
    {TextId::AssignedToBothSeats, L"Selected for both Players", L"두 플레이어에 선택됨", L"已选择用于两位玩家"},
    {TextId::StatusNotSelected, L"○ Not used", L"○ 사용 안 함", L"○ 未使用"},
    {TextId::LaunchSelectionEmpty, L"Choose a game and Player 1. Choose Player 2 only for two-player play.", L"게임과 플레이어 1을 선택하세요. 2인 플레이일 때만 플레이어 2를 선택합니다.", L"请选择游戏和玩家 1。仅在双人游戏时选择玩家 2。"},
    {TextId::AddPlayerToContinue, L"Add a Player to continue.", L"계속하려면 플레이어를 추가하세요.", L"请先添加玩家以继续。"},
    {TextId::ChooseSecondPlayerToContinue, L"Choose the other Seat's Player to continue with both Seats.", L"두 좌석에서 계속하려면 다른 좌석의 플레이어를 선택하세요.", L"要继续使用两个席位，请选择另一个席位的玩家。"},
    {TextId::PrivacyHeading, L"Privacy", L"개인정보", L"隐私"},
    {TextId::CommunitySharing, L"Allow sharing compatibility results", L"호환성 결과 공유 허용", L"允许分享兼容性结果"},
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
    {TextId::ReturnToWindows, L"Stop / Return to Windows", L"중지 / Windows로 돌아가기", L"停止 / 返回 Windows"},
    {TextId::Reconfigure, L"Reconfigure", L"다시 설정", L"重新设置"},
    {TextId::ApplySetup, L"Apply Setup", L"설정 적용", L"应用设置"},
    {TextId::ReloadSetup, L"Reload Setup", L"설정 다시 불러오기", L"重新加载设置"},
    {TextId::WorkspaceProfileSaveFailed, L"Seat setup could not be saved. Your previous setup is unchanged. Check access to Local App Data, then choose Apply Setup again.", L"좌석 설정을 저장할 수 없어 이전 설정을 유지했습니다. 로컬 앱 데이터 접근을 확인한 뒤 설정 적용을 다시 선택하세요.", L"无法保存席位设置，已保留之前的设置。请检查本地应用数据访问权限，然后再次选择“应用设置”。"},
    {TextId::WorkspaceProfileApplyFailed, L"Seat setup was saved, but could not be activated. Stop any running game, choose Reconfigure, then apply the setup again.", L"좌석 설정은 저장했지만 활성화하지 못했습니다. 실행 중인 게임을 중지하고 다시 설정을 선택한 뒤 설정을 다시 적용하세요.", L"席位设置已保存，但无法启用。请停止正在运行的游戏，选择“重新设置”，然后再次应用设置。"},
    {TextId::WorkspaceProfileSaved, L"Seat setup was saved and is ready to use.", L"좌석 설정을 저장했으며 사용할 준비가 됐습니다.", L"席位设置已保存，可以使用。"},
    {TextId::WorkspaceProfileSavedNeedsReconfigure, L"Seat setup was saved. Choose Reconfigure before starting a game.", L"좌석 설정을 저장했습니다. 게임을 시작하기 전에 다시 설정을 선택하세요.", L"席位设置已保存。开始游戏前请选择“重新设置”。"},
    {TextId::WorkspaceProfileLoadFailed, L"Saved Seat setup could not be loaded. The current setup is unchanged. Check Local App Data, then try again.", L"저장된 좌석 설정을 불러올 수 없어 현재 설정을 유지했습니다. 로컬 앱 데이터를 확인한 뒤 다시 시도하세요.", L"无法加载已保存的席位设置，当前设置保持不变。请检查本地应用数据后重试。"},
    {TextId::WorkspaceProfileLoadApplyFailed, L"Seat setup was loaded, but could not be activated. Stop any running game, choose Reconfigure, then load it again.", L"좌석 설정을 불러왔지만 활성화하지 못했습니다. 실행 중인 게임을 중지하고 다시 설정을 선택한 뒤 다시 불러오세요.", L"席位设置已加载，但无法启用。请停止正在运行的游戏，选择“重新设置”，然后再次加载。"},
    {TextId::WorkspaceProfileLoaded, L"Seat setup was loaded and is ready to use.", L"좌석 설정을 불러왔으며 사용할 준비가 됐습니다.", L"席位设置已加载，可以使用。"},
    {TextId::WorkspaceProfileLoadedNeedsReconfigure, L"Seat setup was loaded. Choose Reconfigure before starting a game.", L"좌석 설정을 불러왔습니다. 게임을 시작하기 전에 다시 설정을 선택하세요.", L"席位设置已加载。开始游戏前请选择“重新设置”。"},
    {TextId::DetectingHardware, L"Detecting connected hardware...", L"연결된 장치 확인 중...", L"正在检测已连接设备..."},
    {TextId::ConnectedHardwareCount, L"Connected devices: {0}", L"연결된 장치: {0}개", L"已连接设备：{0}"},
    {TextId::AvailableHardware, L"Available hardware", L"사용 가능한 장치", L"可用设备"},
    {TextId::AvailableHardwareHint, L"Select a device, then assign it to Display 1 or Display 2.", L"장치를 선택한 뒤 디스플레이 1 또는 디스플레이 2에 배정하세요.", L"选择设备，然后分配到显示器 1 或显示器 2。"},
    {TextId::SeatHardwareLabel, L"Display {0} devices", L"디스플레이 {0} 장치", L"显示器 {0} 设备"},
    {TextId::AssignToSeat, L"Assign to Display {0}", L"디스플레이 {0}에 배정", L"分配到显示器 {0}"},
    {TextId::UnassignDevice, L"Unassign", L"배정 해제", L"取消分配"},
    {TextId::DeviceDisplay, L"Display", L"디스플레이", L"显示器"},
    {TextId::DeviceKeyboard, L"Keyboard", L"키보드", L"键盘"},
    {TextId::DeviceMouse, L"Mouse", L"마우스", L"鼠标"},
    {TextId::DeviceTouchpad, L"Touchpad", L"터치패드", L"触控板"},
    {TextId::DeviceController, L"Controller", L"컨트롤러", L"手柄"},
    {TextId::PrimaryDisplay, L"Primary display", L"주 디스플레이", L"主显示器"},
    {TextId::IdentifyKeyboard, L"Press a key to find keyboard", L"키를 눌러 키보드 찾기", L"按键查找键盘"},
    {TextId::IdentifyMouse, L"Click to find mouse", L"클릭하여 마우스 찾기", L"点击查找鼠标"},
    {TextId::CancelIdentification, L"Cancel identification", L"식별 취소", L"取消识别"},
    {TextId::IdentificationWaitingKeyboard, L"Press any key on the keyboard you want to identify.", L"찾을 키보드에서 아무 키나 누르세요.", L"请在要识别的键盘上按任意键。"},
    {TextId::IdentificationWaitingMouse, L"Click a button on the mouse you want to identify. Movement and wheel input are ignored.", L"찾을 마우스의 버튼을 클릭하세요. 이동과 휠 입력은 무시됩니다.", L"请点击要识别的鼠标按钮。移动和滚轮输入会被忽略。"},
    {TextId::IdentificationTimedOut, L"No device was identified. Try again.", L"장치를 찾지 못했습니다. 다시 시도하세요.", L"未识别到设备。请重试。"},
    {TextId::IdentificationNotMapped, L"The input was detected, but it could not be matched to one physical device.", L"입력은 감지했지만 하나의 실제 장치와 일치시키지 못했습니다.", L"已检测到输入，但无法匹配到唯一的物理设备。"},
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

struct PlaceholderSignature {
    std::size_t zeroCount{};
    std::size_t oneCount{};
    bool valid{true};

    constexpr bool operator==(const PlaceholderSignature&) const noexcept = default;
};

constexpr PlaceholderSignature placeholderSignature(std::wstring_view value) noexcept {
    PlaceholderSignature result;
    for (std::size_t index = 0; index < value.size();) {
        if (value[index] == L'{') {
            if (index + 2u >= value.size() || value[index + 2u] != L'}' ||
                (value[index + 1u] != L'0' && value[index + 1u] != L'1')) {
                result.valid = false;
                return result;
            }
            if (value[index + 1u] == L'0') {
                ++result.zeroCount;
            } else {
                ++result.oneCount;
            }
            index += 3u;
            continue;
        }
        if (value[index] == L'}') {
            result.valid = false;
            return result;
        }
        ++index;
    }
    return result;
}

constexpr bool rowHasExplicitLocales(const Row& row) noexcept {
    return !row.en.empty() && !row.ko.empty() && !row.zh.empty();
}

constexpr bool rowPlaceholdersMatch(const Row& row) noexcept {
    const auto english = placeholderSignature(row.en);
    const auto korean = placeholderSignature(row.ko);
    const auto chinese = placeholderSignature(row.zh);
    return english.valid && korean.valid && chinese.valid &&
           english == korean && english == chinese;
}

constexpr bool catalogTranslationsValid() noexcept {
    for (const auto& row : kRows) {
        if (!rowHasExplicitLocales(row) || !rowPlaceholdersMatch(row)) return false;
    }
    return true;
}

static_assert(catalogTranslationsValid(),
              "every shipping TextId must have explicit en-US/ko-KR/zh-CN text with matching placeholders");

const Row* findRow(TextId id) noexcept {
    for (const auto& row : kRows) {
        if (row.id == id) return &row;
    }
    return nullptr;
}

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
    const auto* row = findRow(id);
    return row ? pick(*row, locale) : std::wstring_view{};
}

bool localizationEntryComplete(TextId id) noexcept {
    const auto* row = findRow(id);
    return row && rowHasExplicitLocales(*row);
}

bool localizationPlaceholdersValid(TextId id) noexcept {
    const auto* row = findRow(id);
    return row && rowPlaceholdersMatch(*row);
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
