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
    std::wstring_view en;
    std::wstring_view ko;
    std::wstring_view zh;
};

constexpr Row kRows[] = {
    {L"HydraSeat - Games", L"HydraSeat - 게임", L"HydraSeat - 游戏"},
    {L"Games", L"게임", L"游戏"},
    {L"Installed and manually added titles", L"설치된 게임 및 수동 추가 항목", L"已安装和手动添加的游戏"},
    {L"Refresh", L"새로 고침", L"刷新"},
    {L"Add EXE...", L"EXE 추가...", L"添加 EXE..."},
    {L"Players and Seats", L"플레이어 및 Seat", L"玩家与 Seat"},
    {L"Player name", L"플레이어 이름", L"玩家名称"},
    {L"Add Player", L"플레이어 추가", L"添加玩家"},
    {L"Rename", L"이름 변경", L"重命名"},
    {L"Remove", L"삭제", L"移除"},
    {L"Seat {0}", L"Seat {0}", L"Seat {0}"},
    {L"Player", L"플레이어", L"玩家"},
    {L"Game", L"게임", L"游戏"},
    {L"Choose Player", L"플레이어 선택", L"选择玩家"},
    {L"Choose Game", L"게임 선택", L"选择游戏"},
    {L"Create two-player setup", L"2인 설정 만들기", L"创建双人设置"},
    {L"Play", L"플레이", L"开始游戏"},
    {L"Choose the same game for both Seats first.", L"먼저 두 Seat에 같은 게임을 선택하세요.", L"请先为两个 Seat 选择同一款游戏。"},
    {L"Two players", L"2인 플레이", L"双人游戏"},
    {L"Two-player setup created. Compatibility evidence is still required.", L"2인 설정을 만들었습니다. 호환성 근거는 아직 필요합니다.", L"双人设置已创建，仍需兼容性证据。"},
    {L"The immutable Play plan is validated and ready for the runtime activation boundary.", L"변경 불가능한 Play 계획이 검증되어 런타임 활성화 경계에서 사용할 준비가 되었습니다.", L"不可变的游戏计划已验证，可交给运行时激活边界。"},
    {L"HydraSeat Play", L"HydraSeat 플레이", L"HydraSeat 游戏"},
    {L"The game library window could not be registered.", L"게임 라이브러리 창을 등록할 수 없습니다.", L"无法注册游戏库窗口。"},
    {L"The game library could not initialize from local state.", L"로컬 상태에서 게임 라이브러리를 초기화할 수 없습니다.", L"无法从本地状态初始化游戏库。"},
    {L"Local Steam library refreshed read-only.", L"로컬 Steam 라이브러리를 읽기 전용으로 새로 고쳤습니다.", L"已以只读方式刷新本地 Steam 游戏库。"},
    {L"Player added.", L"플레이어를 추가했습니다.", L"已添加玩家。"},
    {L"Player renamed.", L"플레이어 이름을 변경했습니다.", L"已重命名玩家。"},
    {L"Player removed.", L"플레이어를 삭제했습니다.", L"已移除玩家。"},
    {L"Choose a Player from the roster first.", L"먼저 목록에서 플레이어를 선택하세요.", L"请先从列表中选择玩家。"},
    {L"Manual executable returned no stable identity.", L"수동 실행 파일에서 안정적인 식별자를 얻지 못했습니다.", L"手动添加的可执行文件没有稳定标识。"},
    {L"Executable added after read-only PE validation.", L"읽기 전용 PE 검증 후 실행 파일을 추가했습니다.", L"只读 PE 验证通过后已添加可执行文件。"},
    {L"Seat selection updated.", L"Seat 선택을 업데이트했습니다.", L"Seat 选择已更新。"},
    {L"HydraSeat - Seat {0}", L"HydraSeat - Seat {0}", L"HydraSeat - Seat {0}"},
    {L"End Playing", L"플레이 종료", L"结束游戏"},
    {L"Reconnect", L"다시 연결", L"重新连接"},
    {L"None", L"없음", L"无"},
    {L"Current / selected Player: {0}", L"현재 / 선택한 플레이어: {0}", L"当前 / 已选玩家：{0}"},
    {L"Current / selected game: {0}", L"현재 / 선택한 게임: {0}", L"当前 / 已选游戏：{0}"},
    {L"Recent games: {0}", L"최근 게임: {0}", L"最近游戏：{0}"},
    {L"Available games: {0}", L"사용 가능한 게임: {0}", L"可用游戏：{0}"},
    {L"HydraSeat is disconnected. Reconnect to refresh this Seat.", L"HydraSeat 연결이 끊겼습니다. 이 Seat 상태를 새로 고치려면 다시 연결하세요.", L"HydraSeat 已断开。请重新连接以刷新此 Seat。"},
    {L"The game is starting.", L"게임을 시작하고 있습니다.", L"游戏正在启动。"},
    {L"The game needs attention. End Playing if it does not recover.", L"게임 상태를 확인해야 합니다. 복구되지 않으면 플레이를 종료하세요.", L"游戏需要处理；若无法恢复，请结束游戏。"},
    {L"This Seat needs recovery. Refresh status before taking another action.", L"이 Seat는 복구가 필요합니다. 다른 작업 전에 상태를 새로 고치세요.", L"此 Seat 需要恢复。执行其他操作前请刷新状态。"},
    {L"This Seat needs a device setting before the game can start.", L"게임을 시작하기 전에 이 Seat의 장치 설정이 필요합니다.", L"开始游戏前，此 Seat 需要完成设备设置。"},
    {L"Game requirements need review before starting.", L"게임을 시작하기 전에 요구 사항을 검토해야 합니다.", L"开始游戏前需要检查游戏要求。"},
    {L"The two-player setup needs review.", L"2인 설정을 검토해야 합니다.", L"需要检查双人设置。"},
    {L"This Protected / Experimental setup needs acknowledgement.", L"이 보호됨 / 실험적 설정은 명시적 확인이 필요합니다.", L"此受保护 / 实验性设置需要明确确认。"},
    {L"The game provider is unavailable. Refresh and try again.", L"게임 제공자를 사용할 수 없습니다. 새로 고친 뒤 다시 시도하세요.", L"游戏提供方不可用。请刷新后重试。"},
    {L"Choose the Player account to use for this game.", L"이 게임에 사용할 플레이어 계정을 선택하세요.", L"请选择此游戏要使用的玩家账户。"},
    {L"The selected game has setup details to review.", L"선택한 게임의 설정 세부 정보를 검토하세요.", L"所选游戏有需要检查的设置详情。"},
    {L"The selected game cannot start until its setup is reviewed.", L"설정을 검토하기 전에는 선택한 게임을 시작할 수 없습니다.", L"检查设置之前无法启动所选游戏。"},
    {L"Set later", L"나중에 설정", L"稍后设置"},
    {L"Seat hardware setup is optional until the selected game requires a device.", L"선택한 게임에서 장치를 요구하기 전까지 Seat 하드웨어 설정은 선택 사항입니다.", L"在所选游戏需要设备之前，Seat 硬件设置可以暂缓。"},
    {L"I understand this is a Protected / Experimental path and may fail.", L"이 경로가 보호됨 / 실험적이며 실패할 수 있음을 이해합니다.", L"我了解这是受保护 / 实验性路径，可能失败。"},
    {L"Recovery", L"복구", L"恢复"},
    {L"Disconnected", L"연결 끊김", L"已断开"},
    {L"Ready", L"준비됨", L"就绪"},
    {L"Ready to start", L"시작 준비됨", L"可以启动"},
    {L"Starting game", L"게임 시작 중", L"正在启动游戏"},
    {L"Playing", L"플레이 중", L"游戏中"},
    {L"Ending play session", L"플레이 종료 중", L"正在结束游戏"},
    {L"Game needs attention", L"게임 상태 확인 필요", L"游戏需要处理"},
    {L"Recovery required", L"복구 필요", L"需要恢复"},
    {L"End Playing for this Seat? The other Seat will not be stopped.", L"이 Seat의 플레이를 종료할까요? 다른 Seat는 중지되지 않습니다.", L"结束此 Seat 的游戏？另一个 Seat 不会被停止。"},
    {L"Refresh this Seat first. If recovery still fails, use the independently validated reset tool from Windows.", L"먼저 이 Seat 상태를 새로 고치세요. 그래도 복구되지 않으면 Windows에서 독립 검증된 reset 도구를 사용하세요.", L"请先刷新此 Seat。若仍无法恢复，请从 Windows 使用独立验证的重置工具。"},
    {L"Emergency reset is never executed by a Seat hotkey. Use hydra_reset.exe explicitly from Windows when normal recovery cannot complete.", L"Seat 단축키는 긴급 reset을 직접 실행하지 않습니다. 정상 복구가 실패한 경우 Windows에서 hydra_reset.exe를 명시적으로 실행하세요.", L"Seat 快捷键绝不会直接执行紧急重置。正常恢复失败时，请在 Windows 中明确运行 hydra_reset.exe。"},
};

static_assert(std::size(kRows) == static_cast<std::size_t>(TextId::Count));

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
    const auto index = static_cast<std::size_t>(id);
    if (index >= std::size(kRows)) return {};
    return pick(kRows[index], locale);
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
