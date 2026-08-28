#pragma once

#include "hydra/two_player_setup_editor.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::portable {

inline constexpr std::uint32_t kSetupPackageVersion = 1u;
inline constexpr std::size_t kMaximumSetupPackageBytes = 4u * 1024u * 1024u;
inline constexpr std::size_t kMaximumPathVariables = 4u;

struct Provenance {
    std::string sourceId;
    std::uint64_t sourceRevision{0};
    std::string exportedBy;

    bool operator==(const Provenance&) const = default;
};

enum class PathField : std::uint8_t {
    WorkingDirectory = 0,
    DataRoot = 1,
};

struct PathVariable {
    std::string variableName;
    std::uint32_t instanceIndex{0};
    PathField field{PathField::DataRoot};

    bool operator==(const PathVariable&) const = default;
};

struct SetupPackage {
    std::uint32_t version{kSetupPackageVersion};
    Provenance provenance;
    profile::TwoPlayerSetup redactedSetup;
    std::vector<PathVariable> pathVariables;

    bool operator==(const SetupPackage&) const = default;
};

struct PathBinding {
    std::string variableName;
    std::wstring localPath;

    bool operator==(const PathBinding&) const = default;
};

enum class PackageResult : std::uint8_t {
    Success = 0,
    InvalidSetup = 1,
    InvalidProvenance = 2,
    InvalidPackage = 3,
    PackageTooLarge = 4,
    UnsupportedVersion = 5,
    MissingBinding = 6,
    DuplicateBinding = 7,
    UnexpectedBinding = 8,
    InvalidLocalPath = 9,
};

struct PackageDiagnostic {
    PackageResult result{PackageResult::Success};
    std::string message;

    bool succeeded() const noexcept { return result == PackageResult::Success; }
};

// Export is pure: source absolute working/data paths are replaced with typed
// variables before the package is returned. Player names, provider credentials,
// authentication material, device identities, scripts, and binaries are not representable.
PackageDiagnostic exportSetup(const profile::TwoPlayerSetup& setup,
                              const profile::GameRecord& game,
                              Provenance provenance,
                              SetupPackage& output);

PackageDiagnostic importSetup(const SetupPackage& package,
                              const profile::GameRecord& localGame,
                              std::span<const PathBinding> bindings,
                              profile::TwoPlayerSetup& output);

PackageDiagnostic encodePackage(const SetupPackage& package, std::string& output);
PackageDiagnostic decodePackage(std::string_view bytes, SetupPackage& output);

std::string_view packageResultName(PackageResult result) noexcept;
std::string_view pathFieldName(PathField field) noexcept;

} // namespace hydra::portable
