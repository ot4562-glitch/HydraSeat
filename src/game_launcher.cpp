#include "hydra/game_launcher.hpp"

#include <array>
#include <optional>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cwchar>
#endif

namespace hydra {
namespace {

constexpr std::uint32_t kFirstSeatId = 1;
constexpr std::uint32_t kLastSeatId = 2;

std::optional<std::size_t> seatIndex(std::uint32_t seatId) noexcept {
    if (seatId < kFirstSeatId || seatId > kLastSeatId) return std::nullopt;
    return static_cast<std::size_t>(seatId - kFirstSeatId);
}

#ifdef _WIN32

constexpr DWORD kForcedExitCode = 0x48594452u;
constexpr DWORD kStopTimeoutMs = 2000u;

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE value) noexcept : value_(value) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : value_(other.release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }

    HANDLE get() const noexcept { return value_; }
    HANDLE release() noexcept {
        HANDLE value = value_;
        value_ = nullptr;
        return value;
    }
    void reset(HANDLE value = nullptr) noexcept {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
        value_ = value;
    }

private:
    HANDLE value_{nullptr};
};

bool containsNul(const std::wstring& value) noexcept {
    return value.find(L'\0') != std::wstring::npos;
}

std::optional<std::uint64_t> processCreationIdentity(HANDLE process) noexcept {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(process, &creation, &exit, &kernel, &user) == FALSE) {
        return std::nullopt;
    }
    ULARGE_INTEGER identity{};
    identity.LowPart = creation.dwLowDateTime;
    identity.HighPart = creation.dwHighDateTime;
    if (identity.QuadPart == 0) return std::nullopt;
    return identity.QuadPart;
}

bool stopExactProcess(HANDLE process) noexcept {
    if (process == nullptr || process == INVALID_HANDLE_VALUE) return false;

    DWORD exitCode = 0;
    if (GetExitCodeProcess(process, &exitCode) == FALSE) return false;
    if (exitCode == STILL_ACTIVE) {
        if (TerminateProcess(process, kForcedExitCode) == FALSE) {
            if (GetExitCodeProcess(process, &exitCode) == FALSE || exitCode == STILL_ACTIVE) {
                return false;
            }
        }
    }
    return WaitForSingleObject(process, kStopTimeoutMs) == WAIT_OBJECT_0;
}

bool keyMatches(std::wstring_view entry, std::wstring_view key) noexcept {
    const auto equals = entry.find(L'=');
    if (equals == std::wstring_view::npos || equals != key.size()) return false;
    return _wcsnicmp(entry.data(), key.data(), key.size()) == 0;
}

std::optional<std::vector<wchar_t>> environmentBlock(
    const controller::VirtualXInputMapping& mapping,
    const std::wstring& pipeEndpoint) {
    if (!mapping.valid() || mapping.source.sourceGeneration == 0 ||
        pipeEndpoint.empty() || containsNul(pipeEndpoint)) {
        return std::nullopt;
    }

    const std::array<std::pair<std::wstring, std::wstring>, 4> overrides{{
        {L"HYDRA_XINPUT_PIPE", pipeEndpoint},
        {L"HYDRA_XINPUT_SEAT_ID", std::to_wstring(mapping.seatId)},
        {L"HYDRA_XINPUT_ACTIVATION_GENERATION", std::to_wstring(mapping.activationGeneration)},
        {L"HYDRA_XINPUT_SOURCE_GENERATION", std::to_wstring(mapping.source.sourceGeneration)},
    }};

    LPWCH rawEnvironment = GetEnvironmentStringsW();
    if (rawEnvironment == nullptr) return std::nullopt;

    std::vector<std::wstring> entries;
    for (const wchar_t* cursor = rawEnvironment; *cursor != L'\0';) {
        std::wstring entry(cursor);
        entries.push_back(entry);
        cursor += entry.size() + 1u;
    }
    FreeEnvironmentStringsW(rawEnvironment);

    for (const auto& [key, value] : overrides) {
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const std::wstring& entry) {
                                         return keyMatches(entry, key);
                                     }),
                      entries.end());
        entries.push_back(key + L"=" + value);
    }

    std::sort(entries.begin(), entries.end(), [](const std::wstring& left,
                                                  const std::wstring& right) {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    });

    std::vector<wchar_t> block;
    std::size_t characterCount = 1u;
    for (const auto& entry : entries) characterCount += entry.size() + 1u;
    block.reserve(characterCount);
    for (const auto& entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

std::optional<std::wstring> commandLineFor(const GameProfile& game) {
    if (game.executablePath.empty() || containsNul(game.executablePath) ||
        game.executablePath.find(L'"') != std::wstring::npos ||
        containsNul(game.launchArguments) || containsNul(game.workingDirectory)) {
        return std::nullopt;
    }

    std::wstring command = L"\"" + game.executablePath + L"\"";
    if (!game.launchArguments.empty()) {
        command.push_back(L' ');
        command += game.launchArguments;
    }
    return command;
}

#endif

} // namespace

struct GameLauncher::Impl {
#ifdef _WIN32
    struct SeatSession {
        std::uint32_t seatId{0};
        UniqueHandle process;
        runtime::ProcessIdentity identity;
        runtime::ActivationToken activation;
        runtime::SessionController* authority{nullptr};
    };

    std::array<std::optional<SeatSession>, 2> sessions;
#endif
};

GameLauncher::GameLauncher() : impl_(std::make_unique<Impl>()) {}

GameLauncher::~GameLauncher() {
    if (!impl_) return;
    (void)stopWorkspaceGame(1);
    (void)stopWorkspaceGame(2);
}

bool GameLauncher::launchGameForWorkspace(
    const GameProfile& game,
    const WorkspaceConfig& workspace,
    runtime::SessionController& sessionController,
    const controller::SeatBinding& controllerBinding,
    const controller::InventorySnapshot& inventory,
    std::wstring xinputPipeEndpoint) {
#ifdef _WIN32
    const auto index = seatIndex(workspace.workspaceId);
    if (!index || impl_->sessions[*index].has_value()) return false;
    if (controllerBinding.seatId != workspace.workspaceId ||
        controllerBinding.api != controller::ApiSurface::XInput ||
        controllerBinding.sourceGeneration == 0 || xinputPipeEndpoint.empty()) {
        return false;
    }

    const auto command = commandLineFor(game);
    if (!command) return false;

    const auto activation = sessionController.beginSeatActivation(workspace.workspaceId);
    if (!activation.valid()) return false;

    bool activationOwned = true;
    auto endActivation = [&]() noexcept {
        if (activationOwned) {
            (void)sessionController.endSeatActivation(activation);
            activationOwned = false;
        }
    };

    if (!sessionController.bindController(activation, controllerBinding, inventory)) {
        endActivation();
        return false;
    }

    const auto mapping = sessionController.virtualXInputMapping(activation);
    if (!mapping || mapping->source.sourceGeneration == 0) {
        endActivation();
        return false;
    }

    auto environment = environmentBlock(*mapping, xinputPipeEndpoint);
    if (!environment) {
        endActivation();
        return false;
    }

    std::vector<wchar_t> mutableCommand(command->begin(), command->end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION processInfo{};
    const wchar_t* workingDirectory =
        game.workingDirectory.empty() ? nullptr : game.workingDirectory.c_str();
    const DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                        CREATE_NEW_CONSOLE;

    if (CreateProcessW(game.executablePath.c_str(), mutableCommand.data(),
                       nullptr, nullptr, FALSE, flags, environment->data(),
                       workingDirectory, &startup, &processInfo) == FALSE) {
        endActivation();
        return false;
    }

    UniqueHandle process(processInfo.hProcess);
    UniqueHandle thread(processInfo.hThread);

    impl_->sessions[*index].emplace(Impl::SeatSession{
        workspace.workspaceId,
        std::move(process),
        runtime::ProcessIdentity{},
        activation,
        &sessionController});
    activationOwned = false;
    auto& session = *impl_->sessions[*index];

    const auto creationIdentity = processCreationIdentity(session.process.get());
    if (!creationIdentity) {
        (void)stopWorkspaceGame(workspace.workspaceId);
        return false;
    }

    session.identity = {processInfo.dwProcessId, *creationIdentity};
    if (!sessionController.publishProcess(activation, session.identity)) {
        (void)stopWorkspaceGame(workspace.workspaceId);
        return false;
    }

    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        (void)stopWorkspaceGame(workspace.workspaceId);
        return false;
    }

    return true;
#else
    (void)game;
    (void)workspace;
    (void)sessionController;
    (void)controllerBinding;
    (void)inventory;
    (void)xinputPipeEndpoint;
#endif

    return false;
}

bool GameLauncher::stopWorkspaceGame(uint32_t workspaceId) {
#ifdef _WIN32
    const auto index = seatIndex(workspaceId);
    if (!index || !impl_->sessions[*index].has_value()) return false;

    auto session = std::move(*impl_->sessions[*index]);
    impl_->sessions[*index].reset();

    if (!stopExactProcess(session.process.get())) {
        impl_->sessions[*index].emplace(std::move(session));
        return false;
    }
    const bool activationEnded =
        session.authority != nullptr && session.authority->endSeatActivation(session.activation);
    if (!activationEnded) {
        impl_->sessions[*index].emplace(std::move(session));
        return false;
    }
    return true;
#else
    (void)workspaceId;
    return false;
#endif
}

} // namespace hydra
