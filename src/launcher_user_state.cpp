#include "hydra/launcher_user_state.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hydra::launcher_state {
namespace {

constexpr std::size_t kMaximumRootCodeUnits = 32767u;
constexpr std::wstring_view kStageSuffix = L".stage";

UserStateDiagnostic ok() {
    return {};
}

UserStateDiagnostic fail(UserStateResult result,
                         std::string message,
                         std::uint32_t systemError = 0u) {
    return {result, systemError, std::move(message)};
}

bool validRoot(const std::filesystem::path& root) {
    if (root.empty()) {
        return false;
    }
#ifdef _WIN32
    return root.native().size() <= kMaximumRootCodeUnits;
#else
    return root.native().size() <= kMaximumRootCodeUnits;
#endif
}

std::filesystem::path stagePathFor(const std::filesystem::path& target) {
    auto stage = target;
    stage += kStageSuffix;
    return stage;
}

bool isMissingError(const std::error_code& error) {
    return error == std::errc::no_such_file_or_directory;
}

struct BoundedReadResult {
    UserStateDiagnostic diagnostic;
    bool missing{false};
    std::string bytes;
};

BoundedReadResult readBoundedFile(const std::filesystem::path& path,
                                  std::size_t maximumBytes) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error && !isMissingError(error)) {
        return {fail(UserStateResult::IoError,
                     "failed to inspect launcher user-state file",
                     static_cast<std::uint32_t>(error.value())), false, {}};
    }
    if (isMissingError(error) || status.type() == std::filesystem::file_type::not_found) {
        BoundedReadResult result;
        result.missing = true;
        return result;
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return {fail(UserStateResult::InvalidData,
                     "launcher user-state path is not a regular file"), false, {}};
    }

    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return {fail(UserStateResult::IoError,
                     "failed to determine launcher user-state file size",
                     static_cast<std::uint32_t>(error.value())), false, {}};
    }
    if (size == 0u) {
        return {fail(UserStateResult::InvalidData,
                     "launcher user-state file is empty"), false, {}};
    }
    if (size > maximumBytes) {
        return {fail(UserStateResult::FileTooLarge,
                     "launcher user-state file exceeds its size bound"), false, {}};
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {fail(UserStateResult::IoError,
                     "failed to open launcher user-state file"), false, {}};
    }

    std::string bytes(static_cast<std::size_t>(size), '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return {fail(UserStateResult::IoError,
                     "failed to read launcher user-state file completely"), false, {}};
    }

    BoundedReadResult result;
    result.bytes = std::move(bytes);
    return result;
}

UserStateDiagnostic ensureRootDirectory(const std::filesystem::path& root) {
    if (!validRoot(root)) {
        return fail(UserStateResult::InvalidArgument,
                    "launcher user-state root is empty or too long");
    }

    std::error_code error;
    const auto status = std::filesystem::symlink_status(root, error);
    if (!error && status.type() != std::filesystem::file_type::not_found) {
        if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) {
            return fail(UserStateResult::InvalidArgument,
                        "launcher user-state root must be a real directory");
        }
        return ok();
    }
    if (error && !isMissingError(error)) {
        return fail(UserStateResult::IoError,
                    "failed to inspect launcher user-state root",
                    static_cast<std::uint32_t>(error.value()));
    }

    std::filesystem::create_directories(root, error);
    if (error) {
        return fail(UserStateResult::IoError,
                    "failed to create launcher user-state root",
                    static_cast<std::uint32_t>(error.value()));
    }

    const auto createdStatus = std::filesystem::symlink_status(root, error);
    if (error || std::filesystem::is_symlink(createdStatus) ||
        !std::filesystem::is_directory(createdStatus)) {
        return fail(UserStateResult::IoError,
                    "launcher user-state root was not created as a directory",
                    static_cast<std::uint32_t>(error.value()));
    }
    return ok();
}

#ifdef _WIN32

class UniqueHandle final {
public:
    explicit UniqueHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
    ~UniqueHandle() { close(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    HANDLE get() const noexcept { return handle_; }
    explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    void close() noexcept {
        if (*this) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

struct WindowsPathState {
    bool exists{false};
    DWORD attributes{0u};
    UserStateDiagnostic diagnostic;
};

WindowsPathState inspectWindowsMutationPath(const std::filesystem::path& path,
                                            std::string_view purpose) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return {};
        }
        return {false, 0u,
                fail(UserStateResult::IoError,
                     std::string("failed to inspect ") + std::string(purpose), error)};
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u) {
        return {true, attributes,
                fail(UserStateResult::InvalidArgument,
                     std::string(purpose) + " must not be a directory or reparse point")};
    }
    return {true, attributes, ok()};
}

void deleteOwnedStage(const std::filesystem::path& stage) noexcept {
    (void)DeleteFileW(stage.c_str());
}

UserStateDiagnostic writeFileAtomically(const std::filesystem::path& target,
                                        std::string_view bytes,
                                        std::size_t maximumBytes) {
    if (bytes.empty() || bytes.size() > maximumBytes) {
        return fail(UserStateResult::InvalidArgument,
                    "launcher user-state payload is empty or exceeds its size bound");
    }

    const auto stage = stagePathFor(target);
    const auto targetState = inspectWindowsMutationPath(target, "launcher user-state target");
    if (!targetState.diagnostic.succeeded()) {
        return targetState.diagnostic;
    }
    const auto stageState = inspectWindowsMutationPath(stage, "launcher user-state stage");
    if (!stageState.diagnostic.succeeded()) {
        return stageState.diagnostic;
    }

    UniqueHandle file(CreateFileW(stage.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr));
    if (!file) {
        return fail(UserStateResult::IoError,
                    "failed to create launcher user-state staging file", GetLastError());
    }

    std::size_t offset = 0u;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0u;
        if (!WriteFile(file.get(), bytes.data() + offset, chunk, &written, nullptr) ||
            written == 0u) {
            const DWORD error = GetLastError();
            file.close();
            deleteOwnedStage(stage);
            return fail(UserStateResult::IoError,
                        "failed to write launcher user-state staging file", error);
        }
        offset += written;
    }

    if (!FlushFileBuffers(file.get())) {
        const DWORD error = GetLastError();
        file.close();
        deleteOwnedStage(stage);
        return fail(UserStateResult::IoError,
                    "failed to flush launcher user-state staging file", error);
    }
    file.close();

    if (targetState.exists) {
        if (!ReplaceFileW(target.c_str(), stage.c_str(), nullptr,
                          0u, nullptr, nullptr)) {
            const DWORD error = GetLastError();
            deleteOwnedStage(stage);
            return fail(UserStateResult::AtomicReplaceFailed,
                        "failed to atomically replace launcher user-state file", error);
        }
    } else if (!MoveFileExW(stage.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        deleteOwnedStage(stage);
        return fail(UserStateResult::AtomicReplaceFailed,
                    "failed to atomically install launcher user-state file", error);
    }

    return ok();
}

#else

UserStateDiagnostic writeFileAtomically(const std::filesystem::path& target,
                                        std::string_view bytes,
                                        std::size_t maximumBytes) {
    if (bytes.empty() || bytes.size() > maximumBytes) {
        return fail(UserStateResult::InvalidArgument,
                    "launcher user-state payload is empty or exceeds its size bound");
    }

    const auto stage = stagePathFor(target);
    std::error_code error;
    for (const auto& path : {target, stage}) {
        error.clear();
        const auto status = std::filesystem::symlink_status(path, error);
        if (error && !isMissingError(error)) {
            return fail(UserStateResult::IoError,
                        "failed to inspect launcher user-state mutation path",
                        static_cast<std::uint32_t>(error.value()));
        }
        const bool missing = isMissingError(error) ||
                             status.type() == std::filesystem::file_type::not_found;
        if (!missing &&
            (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))) {
            return fail(UserStateResult::InvalidArgument,
                        "launcher user-state mutation path must be a regular file");
        }
    }

    std::ofstream output(stage, std::ios::binary | std::ios::trunc);
    if (!output) {
        return fail(UserStateResult::IoError,
                    "failed to create launcher user-state staging file");
    }
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
        output.close();
        std::filesystem::remove(stage, error);
        return fail(UserStateResult::IoError,
                    "failed to write launcher user-state staging file");
    }
    output.close();

    std::filesystem::rename(stage, target, error);
    if (error) {
        std::error_code cleanupError;
        std::filesystem::remove(stage, cleanupError);
        return fail(UserStateResult::AtomicReplaceFailed,
                    "failed to atomically replace launcher user-state file",
                    static_cast<std::uint32_t>(error.value()));
    }
    return ok();
}

#endif

bool validSelectionId(std::string_view value) {
    if (value.empty() || value.size() > profile::kMaximumIdentifierBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char raw) {
        const auto value = static_cast<unsigned char>(raw);
        const bool alphaNumeric = (value >= '0' && value <= '9') ||
                                  (value >= 'A' && value <= 'Z') ||
                                  (value >= 'a' && value <= 'z');
        return alphaNumeric || value == '.' || value == '_' || value == '-' || value == ':';
    });
}

UserStateDiagnostic validateSelection(const LastPlayerSelection& selection) {
    if (!validSelectionId(selection.player1Id)) {
        return fail(UserStateResult::InvalidData,
                    "last Player 1 ID is invalid");
    }
    if (selection.player2Id) {
        if (!validSelectionId(*selection.player2Id)) {
            return fail(UserStateResult::InvalidData,
                        "last Player 2 ID is invalid");
        }
        if (*selection.player2Id == selection.player1Id) {
            return fail(UserStateResult::InvalidData,
                        "Player 1 and Player 2 selections must be distinct");
        }
    }
    return ok();
}

UserStateDiagnostic encodeSelection(const LastPlayerSelection& selection,
                                    std::string& bytes) {
    const auto validated = validateSelection(selection);
    if (!validated.succeeded()) {
        return validated;
    }

    bytes = "version=1\nplayer1=";
    bytes.append(selection.player1Id);
    bytes.push_back('\n');
    if (selection.player2Id) {
        bytes.append("player2=");
        bytes.append(*selection.player2Id);
        bytes.push_back('\n');
    }
    if (bytes.size() > kMaximumSelectionFileBytes) {
        return fail(UserStateResult::FileTooLarge,
                    "encoded launcher selection exceeds its size bound");
    }
    return ok();
}

UserStateDiagnostic decodeSelection(std::string_view bytes,
                                    LastPlayerSelection& selection) {
    if (bytes.empty() || bytes.size() > kMaximumSelectionFileBytes ||
        bytes.find('\0') != std::string_view::npos ||
        bytes.find('\r') != std::string_view::npos) {
        return fail(UserStateResult::InvalidData,
                    "launcher selection payload is empty, oversized, or contains invalid control bytes");
    }

    std::vector<std::string_view> lines;
    std::size_t offset = 0u;
    while (offset < bytes.size()) {
        const auto end = bytes.find('\n', offset);
        if (end == std::string_view::npos) {
            return fail(UserStateResult::InvalidData,
                        "launcher selection payload is not newline terminated");
        }
        lines.push_back(bytes.substr(offset, end - offset));
        offset = end + 1u;
    }

    if ((lines.size() != 2u && lines.size() != 3u) || lines[0] != "version=1") {
        return fail(UserStateResult::InvalidData,
                    "launcher selection payload has an unsupported shape or version");
    }

    constexpr std::string_view player1Prefix = "player1=";
    constexpr std::string_view player2Prefix = "player2=";
    if (!lines[1].starts_with(player1Prefix)) {
        return fail(UserStateResult::InvalidData,
                    "launcher selection payload is missing Player 1");
    }

    LastPlayerSelection parsed;
    parsed.player1Id = std::string(lines[1].substr(player1Prefix.size()));
    if (lines.size() == 3u) {
        if (!lines[2].starts_with(player2Prefix)) {
            return fail(UserStateResult::InvalidData,
                        "launcher selection payload has an unknown field");
        }
        parsed.player2Id = std::string(lines[2].substr(player2Prefix.size()));
    }

    const auto validated = validateSelection(parsed);
    if (!validated.succeeded()) {
        return validated;
    }
    selection = std::move(parsed);
    return ok();
}

bool playerExists(const profile::PlayerProfileDocument& players, std::string_view playerId) {
    return std::any_of(players.players.begin(), players.players.end(),
                       [&](const profile::PlayerProfile& player) {
                           return player.playerId == playerId;
                       });
}

} // namespace

std::optional<std::filesystem::path> defaultUserStateRoot() {
#ifdef _WIN32
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0u);
    if (required == 0u || required > kMaximumRootCodeUnits + 1u) {
        return std::nullopt;
    }

    std::vector<wchar_t> buffer(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0u || written >= buffer.size()) {
        return std::nullopt;
    }

    std::filesystem::path root(buffer.data());
    if (!validRoot(root)) {
        return std::nullopt;
    }
    root /= L"HydraSeat";
    if (!validRoot(root)) {
        return std::nullopt;
    }
    return root;
#else
    return std::nullopt;
#endif
}

UserStateDiagnostic ensureUserStateRoot(const std::filesystem::path& root) {
    return ensureRootDirectory(root);
}

std::filesystem::path playerProfilesPath(const std::filesystem::path& root) {
    return root / kPlayerProfilesFileName;
}

std::filesystem::path lastPlayerSelectionPath(const std::filesystem::path& root) {
    return root / kLastPlayerSelectionFileName;
}

std::filesystem::path workspaceProfilePath(const std::filesystem::path& root) {
    return root / kWorkspaceProfileFileName;
}

UserStateDiagnostic loadPlayerProfiles(const std::filesystem::path& root,
                                       profile::PlayerProfileDocument& document) {
    document = {};
    if (!validRoot(root)) {
        return fail(UserStateResult::InvalidArgument,
                    "launcher user-state root is empty or too long");
    }

    auto loaded = readBoundedFile(playerProfilesPath(root),
                                  profile::kMaximumSchemaDocumentBytes);
    if (!loaded.diagnostic.succeeded()) {
        return loaded.diagnostic;
    }
    if (loaded.missing) {
        return ok();
    }
    if (loaded.bytes.find('\0') != std::string::npos) {
        return fail(UserStateResult::InvalidData,
                    "Player profile document contains an embedded NUL");
    }

    profile::PlayerProfileDocument parsed;
    const auto schema = profile::decodePlayerProfileDocument(loaded.bytes, parsed);
    if (!schema.succeeded()) {
        return fail(UserStateResult::ProfileSchemaError,
                    "Player profile document failed schema validation: " + schema.message);
    }
    document = std::move(parsed);
    return ok();
}

UserStateDiagnostic savePlayerProfiles(const std::filesystem::path& root,
                                       const profile::PlayerProfileDocument& document) {
    const auto schema = profile::validatePlayerProfileDocument(document);
    if (!schema.succeeded()) {
        return fail(UserStateResult::ProfileSchemaError,
                    "Player profile document failed schema validation: " + schema.message);
    }

    profile::SchemaDiagnostic encodedDiagnostic;
    const auto bytes = profile::encodePlayerProfileDocument(document, &encodedDiagnostic);
    if (!encodedDiagnostic.succeeded() || bytes.empty()) {
        return fail(UserStateResult::ProfileSchemaError,
                    "Player profile document could not be encoded: " + encodedDiagnostic.message);
    }
    if (bytes.size() > profile::kMaximumSchemaDocumentBytes) {
        return fail(UserStateResult::FileTooLarge,
                    "encoded Player profile document exceeds its size bound");
    }

    const auto rootReady = ensureRootDirectory(root);
    if (!rootReady.succeeded()) {
        return rootReady;
    }
    return writeFileAtomically(playerProfilesPath(root), bytes,
                               profile::kMaximumSchemaDocumentBytes);
}

UserStateDiagnostic loadLastPlayerSelection(
    const std::filesystem::path& root,
    std::optional<LastPlayerSelection>& selection) {
    selection.reset();
    if (!validRoot(root)) {
        return fail(UserStateResult::InvalidArgument,
                    "launcher user-state root is empty or too long");
    }

    auto loaded = readBoundedFile(lastPlayerSelectionPath(root),
                                  kMaximumSelectionFileBytes);
    if (!loaded.diagnostic.succeeded()) {
        return loaded.diagnostic;
    }
    if (loaded.missing) {
        return ok();
    }

    LastPlayerSelection parsed;
    const auto decoded = decodeSelection(loaded.bytes, parsed);
    if (!decoded.succeeded()) {
        return decoded;
    }
    selection = std::move(parsed);
    return ok();
}

UserStateDiagnostic saveLastPlayerSelection(
    const std::filesystem::path& root,
    const LastPlayerSelection& selection) {
    std::string bytes;
    const auto encoded = encodeSelection(selection, bytes);
    if (!encoded.succeeded()) {
        return encoded;
    }

    const auto rootReady = ensureRootDirectory(root);
    if (!rootReady.succeeded()) {
        return rootReady;
    }
    return writeFileAtomically(lastPlayerSelectionPath(root), bytes,
                               kMaximumSelectionFileBytes);
}

UserStateDiagnostic clearLastPlayerSelection(const std::filesystem::path& root) {
    if (!validRoot(root)) {
        return fail(UserStateResult::InvalidArgument,
                    "launcher user-state root is empty or too long");
    }
    const auto target = lastPlayerSelectionPath(root);
#ifdef _WIN32
    const auto state = inspectWindowsMutationPath(target, "launcher selection target");
    if (!state.diagnostic.succeeded()) {
        return state.diagnostic;
    }
    if (!state.exists) {
        return ok();
    }
    if (!DeleteFileW(target.c_str())) {
        return fail(UserStateResult::IoError,
                    "failed to clear launcher selection state", GetLastError());
    }
#else
    std::error_code error;
    const auto status = std::filesystem::symlink_status(target, error);
    if (isMissingError(error) || status.type() == std::filesystem::file_type::not_found) {
        return ok();
    }
    if (error) {
        return fail(UserStateResult::IoError,
                    "failed to inspect launcher selection state",
                    static_cast<std::uint32_t>(error.value()));
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return fail(UserStateResult::InvalidData,
                    "launcher selection state is not a regular file");
    }
    if (!std::filesystem::remove(target, error) || error) {
        return fail(UserStateResult::IoError,
                    "failed to clear launcher selection state",
                    static_cast<std::uint32_t>(error.value()));
    }
#endif
    return ok();
}

UserStateDiagnostic filterLastPlayerSelection(
    const std::optional<LastPlayerSelection>& stored,
    const profile::PlayerProfileDocument& players,
    FilteredLastPlayerSelection& filtered) {
    filtered = {};

    const auto schema = profile::validatePlayerProfileDocument(players);
    if (!schema.succeeded()) {
        return fail(UserStateResult::ProfileSchemaError,
                    "Player profile document failed schema validation: " + schema.message);
    }
    if (!stored) {
        return ok();
    }

    const auto selectionDiagnostic = validateSelection(*stored);
    if (!selectionDiagnostic.succeeded()) {
        return selectionDiagnostic;
    }

    if (!playerExists(players, stored->player1Id)) {
        filtered.player1Stale = true;
        return ok();
    }

    filtered.selection = *stored;
    if (stored->player2Id && !playerExists(players, *stored->player2Id)) {
        filtered.selection->player2Id.reset();
        filtered.player2Stale = true;
    }
    return ok();
}

} // namespace hydra::launcher_state
