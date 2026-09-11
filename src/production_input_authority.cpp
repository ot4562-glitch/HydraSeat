#include "hydra/production_input_authority.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hydra::production {
namespace {

PhysicalEvidenceSelectionDiagnostic failure(
    PhysicalEvidenceSelectionCode code,
    std::string message) {
    return {code, std::move(message)};
}

bool pathToUtf8(const std::filesystem::path& path, std::string& output) {
#ifdef _WIN32
    const auto& native = path.native();
    if (native.empty()) return false;
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                          native.data(),
                                          static_cast<int>(native.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return false;
    output.assign(static_cast<std::size_t>(bytes), '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                               native.data(), static_cast<int>(native.size()),
                               output.data(), bytes, nullptr, nullptr) == bytes;
#else
    const auto native = path.u8string();
    output.assign(reinterpret_cast<const char*>(native.data()), native.size());
    return !output.empty();
#endif
}

bool utf8ToPath(std::string_view input, std::filesystem::path& output) {
    if (input.empty()) return false;
    for (const unsigned char ch : input) {
        if (ch == 0u || ch == static_cast<unsigned char>('\n') ||
            ch == static_cast<unsigned char>('\r')) {
            return false;
        }
    }
#ifdef _WIN32
    const int characters = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()),
        nullptr, 0);
    if (characters <= 0) return false;
    std::wstring wide(static_cast<std::size_t>(characters), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            input.data(), static_cast<int>(input.size()),
                            wide.data(), characters) != characters) {
        return false;
    }
    output = std::filesystem::path(std::move(wide));
#else
    std::u8string utf8;
    utf8.reserve(input.size());
    for (const unsigned char ch : input) utf8.push_back(static_cast<char8_t>(ch));
    output = std::filesystem::path(std::move(utf8));
#endif
    return !output.empty();
}

PhysicalEvidenceSelectionDiagnostic acceptedManifest(
    const std::filesystem::path& candidate,
    std::filesystem::path& canonical,
    std::optional<Phase3HardwareAcceptanceEvidence>& evidence) {
    std::error_code error;
    if (candidate.empty() || !candidate.is_absolute()) {
        return failure(PhysicalEvidenceSelectionCode::InvalidPath,
                       "physical evidence selection must be an absolute manifest path");
    }
    canonical = std::filesystem::weakly_canonical(candidate, error);
    if (error || canonical.empty() || !std::filesystem::is_regular_file(canonical, error) ||
        error) {
        return failure(PhysicalEvidenceSelectionCode::InvalidPath,
                       "selected P3-HW manifest path is missing or cannot be canonicalized");
    }
    auto loaded = loadPhase3HardwareAcceptanceEvidence(canonical);
    if (!loaded.accepted()) {
        return failure(PhysicalEvidenceSelectionCode::EvidenceRejected,
                       "selected P3-HW evidence is not currently accepted: " +
                           loaded.diagnostic);
    }
    evidence = std::move(loaded.evidence);
    return {};
}

} // namespace

std::filesystem::path ProductionPhysicalEvidenceSelectionStore::temporaryPath() const {
    auto result = path_;
    result += L".tmp";
    return result;
}

PhysicalEvidenceSelectionDiagnostic ProductionPhysicalEvidenceSelectionStore::load(
    std::filesystem::path& manifestPath,
    std::optional<Phase3HardwareAcceptanceEvidence>& evidence) const {
    manifestPath.clear();
    evidence.reset();

    std::error_code error;
    const bool exists = std::filesystem::exists(path_, error);
    if (error) {
        return failure(PhysicalEvidenceSelectionCode::ReadFailed,
                       "failed to inspect physical evidence selection store");
    }
    if (!exists) {
        return {PhysicalEvidenceSelectionCode::Missing,
                "physical evidence selection is missing"};
    }
    const auto bytes = std::filesystem::file_size(path_, error);
    if (error) {
        return failure(PhysicalEvidenceSelectionCode::ReadFailed,
                       "failed to read physical evidence selection size");
    }
    if (bytes == 0u || bytes > kMaximumPhysicalEvidenceSelectionBytes) {
        return failure(PhysicalEvidenceSelectionCode::TooLarge,
                       "physical evidence selection has an invalid bounded size");
    }

    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        return failure(PhysicalEvidenceSelectionCode::ReadFailed,
                       "failed to open physical evidence selection store");
    }
    std::string encoded((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof()) {
        return failure(PhysicalEvidenceSelectionCode::ReadFailed,
                       "failed while reading physical evidence selection store");
    }
    std::filesystem::path selected;
    if (!utf8ToPath(encoded, selected)) {
        return failure(PhysicalEvidenceSelectionCode::InvalidEncoding,
                       "physical evidence selection is not one canonical UTF-8 path");
    }

    std::filesystem::path canonical;
    auto accepted = acceptedManifest(selected, canonical, evidence);
    if (!accepted.succeeded() || !accepted.found()) return accepted;
    manifestPath = std::move(canonical);
    return {};
}

PhysicalEvidenceSelectionDiagnostic ProductionPhysicalEvidenceSelectionStore::saveAccepted(
    const std::filesystem::path& manifestPath) const {
    std::filesystem::path canonical;
    std::optional<Phase3HardwareAcceptanceEvidence> evidence;
    auto accepted = acceptedManifest(manifestPath, canonical, evidence);
    if (!accepted.succeeded() || !accepted.found()) return accepted;

    std::string encoded;
    if (!pathToUtf8(canonical, encoded) || encoded.empty() ||
        encoded.size() > kMaximumPhysicalEvidenceSelectionBytes) {
        return failure(PhysicalEvidenceSelectionCode::InvalidEncoding,
                       "accepted P3-HW manifest path cannot be persisted as bounded UTF-8");
    }

    std::error_code error;
    const auto parent = path_.parent_path();
    if (parent.empty()) {
        return failure(PhysicalEvidenceSelectionCode::InvalidPath,
                       "physical evidence selection store has no parent directory");
    }
    std::filesystem::create_directories(parent, error);
    if (error) {
        return failure(PhysicalEvidenceSelectionCode::WriteFailed,
                       "failed to create physical evidence selection directory");
    }

    const auto staging = temporaryPath();
    std::filesystem::remove(staging, error);
    error.clear();
    {
        std::ofstream output(staging, std::ios::binary | std::ios::trunc);
        if (!output) {
            return failure(PhysicalEvidenceSelectionCode::WriteFailed,
                           "failed to create physical evidence selection staging file");
        }
        output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(staging, error);
            return failure(PhysicalEvidenceSelectionCode::WriteFailed,
                           "failed to flush physical evidence selection staging file");
        }
    }
#ifdef _WIN32
    if (MoveFileExW(staging.c_str(), path_.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        std::filesystem::remove(staging, error);
        return failure(PhysicalEvidenceSelectionCode::WriteFailed,
                       "failed to atomically publish physical evidence selection");
    }
#else
    std::filesystem::rename(staging, path_, error);
    if (error) {
        std::filesystem::remove(staging, error);
        return failure(PhysicalEvidenceSelectionCode::WriteFailed,
                       "failed to atomically publish physical evidence selection");
    }
#endif
    return {};
}

PhysicalEvidenceSelectionDiagnostic ProductionPhysicalEvidenceSelectionStore::remove() const {
    std::error_code error;
    const bool exists = std::filesystem::exists(path_, error);
    if (error) {
        return failure(PhysicalEvidenceSelectionCode::RemoveFailed,
                       "failed to inspect physical evidence selection store");
    }
    if (!exists) {
        return {PhysicalEvidenceSelectionCode::Missing,
                "physical evidence selection is missing"};
    }
    if (!std::filesystem::remove(path_, error) || error) {
        return failure(PhysicalEvidenceSelectionCode::RemoveFailed,
                       "failed to remove physical evidence selection store");
    }
    return {};
}

std::optional<std::filesystem::path> defaultProductionPhysicalEvidenceSelectionPath(
    std::string* error) {
#ifdef _WIN32
    std::array<wchar_t, 32768u> localAppData{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData.data(), static_cast<DWORD>(localAppData.size()));
    if (length == 0u || length >= static_cast<DWORD>(localAppData.size())) {
        if (error != nullptr) *error = "LOCALAPPDATA is unavailable";
        return std::nullopt;
    }
    if (error != nullptr) error->clear();
    return std::filesystem::path(localAppData.data()) / L"HydraSeat" /
           L"physical-input-evidence.path";
#else
    if (error != nullptr) {
        *error = "default physical evidence selection path is Windows-only";
    }
    return std::nullopt;
#endif
}

std::vector<ProductionGateCProfile> trustedProductionGateCProfiles() {
    // Intentionally empty until P3-E-02 lands an explicit reviewed profile.
    return {};
}

ProductionInputAuthoritySnapshot loadDefaultProductionInputAuthoritySnapshot() {
    ProductionInputAuthoritySnapshot snapshot;
    snapshot.gateCProfiles = trustedProductionGateCProfiles();

    std::string pathError;
    const auto selectionPath = defaultProductionPhysicalEvidenceSelectionPath(&pathError);
    if (!selectionPath) {
        snapshot.physicalSelection = failure(
            PhysicalEvidenceSelectionCode::ReadFailed,
            pathError.empty() ? "default physical evidence selection path is unavailable"
                              : std::move(pathError));
        return snapshot;
    }

    ProductionPhysicalEvidenceSelectionStore store(*selectionPath);
    std::filesystem::path manifest;
    std::optional<Phase3HardwareAcceptanceEvidence> evidence;
    snapshot.physicalSelection = store.load(manifest, evidence);
    if (snapshot.physicalSelection.code == PhysicalEvidenceSelectionCode::Success &&
        evidence.has_value()) {
        snapshot.inputEvidenceClass = ProductionInputEvidenceClass::Physical;
        snapshot.physicalAcceptanceEvidence = std::move(evidence);
    }
    return snapshot;
}

PhysicalEvidenceSelectionDiagnostic saveDefaultProductionPhysicalEvidenceSelection(
    const std::filesystem::path& manifestPath) {
    std::string pathError;
    const auto selectionPath = defaultProductionPhysicalEvidenceSelectionPath(&pathError);
    if (!selectionPath) {
        return failure(
            PhysicalEvidenceSelectionCode::WriteFailed,
            pathError.empty() ? "default physical evidence selection path is unavailable"
                              : std::move(pathError));
    }
    return ProductionPhysicalEvidenceSelectionStore(*selectionPath).saveAccepted(manifestPath);
}

ProductionInputAuthorityPrerequisiteDiagnostic
checkDefaultProductionInputAuthorityPrerequisites(std::string_view gameId) {
    if (gameId.empty()) {
        return {ProductionInputAuthorityPrerequisiteCode::MissingTrustedGameProfile,
                "selected Game identity is unavailable for production input authority"};
    }
    auto snapshot = loadDefaultProductionInputAuthoritySnapshot();
    if (snapshot.physicalSelection.code == PhysicalEvidenceSelectionCode::Missing) {
        return {ProductionInputAuthorityPrerequisiteCode::MissingPhysicalEvidence,
                "no accepted P3-HW physical evidence manifest is selected"};
    }
    if (snapshot.physicalSelection.code != PhysicalEvidenceSelectionCode::Success ||
        !snapshot.hasPhysicalEvidence()) {
        return {ProductionInputAuthorityPrerequisiteCode::InvalidPhysicalEvidence,
                snapshot.physicalSelection.message.empty()
                    ? "selected P3-HW physical evidence is invalid or stale"
                    : snapshot.physicalSelection.message};
    }
    const auto profile = std::find_if(
        snapshot.gateCProfiles.begin(), snapshot.gateCProfiles.end(),
        [&](const ProductionGateCProfile& candidate) {
            return candidate.gameId == gameId;
        });
    if (profile == snapshot.gateCProfiles.end()) {
        return {ProductionInputAuthorityPrerequisiteCode::MissingTrustedGameProfile,
                "no release-owned reviewed Gate-C profile exists for this exact Game"};
    }
    return {ProductionInputAuthorityPrerequisiteCode::Ready,
            "typed physical evidence and a release-owned exact Game profile are available"};
}

std::string_view physicalEvidenceSelectionCodeName(
    PhysicalEvidenceSelectionCode code) noexcept {
    switch (code) {
        case PhysicalEvidenceSelectionCode::Success: return "success";
        case PhysicalEvidenceSelectionCode::Missing: return "missing";
        case PhysicalEvidenceSelectionCode::TooLarge: return "too-large";
        case PhysicalEvidenceSelectionCode::InvalidEncoding: return "invalid-encoding";
        case PhysicalEvidenceSelectionCode::InvalidPath: return "invalid-path";
        case PhysicalEvidenceSelectionCode::EvidenceRejected: return "evidence-rejected";
        case PhysicalEvidenceSelectionCode::ReadFailed: return "read-failed";
        case PhysicalEvidenceSelectionCode::WriteFailed: return "write-failed";
        case PhysicalEvidenceSelectionCode::RemoveFailed: return "remove-failed";
    }
    return "unknown";
}

} // namespace hydra::production
