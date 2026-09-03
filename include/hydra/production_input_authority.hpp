#pragma once

#include "hydra/hidhide_session_backend.hpp"
#include "hydra/production_activation_bridges.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::production {

inline constexpr std::size_t kMaximumPhysicalEvidenceSelectionBytes = 32u * 1024u;

enum class PhysicalEvidenceSelectionCode : std::uint8_t {
    Success = 0,
    Missing,
    TooLarge,
    InvalidEncoding,
    InvalidPath,
    EvidenceRejected,
    ReadFailed,
    WriteFailed,
    RemoveFailed,
};

struct PhysicalEvidenceSelectionDiagnostic {
    PhysicalEvidenceSelectionCode code{PhysicalEvidenceSelectionCode::Success};
    std::string message;

    bool succeeded() const noexcept {
        return code == PhysicalEvidenceSelectionCode::Success ||
               code == PhysicalEvidenceSelectionCode::Missing;
    }
    bool found() const noexcept { return code != PhysicalEvidenceSelectionCode::Missing; }
};

// The persisted value is only a reference to an already completed P3-HW manifest.
// Save/load always run the typed P3-HW loader, so this store cannot manufacture a
// Phase3HardwareAcceptanceEvidence value or turn Controlled evidence into Physical.
class ProductionPhysicalEvidenceSelectionStore final {
public:
    explicit ProductionPhysicalEvidenceSelectionStore(std::filesystem::path path)
        : path_(std::move(path)) {}

    PhysicalEvidenceSelectionDiagnostic load(
        std::filesystem::path& manifestPath,
        std::optional<Phase3HardwareAcceptanceEvidence>& evidence) const;
    PhysicalEvidenceSelectionDiagnostic saveAccepted(
        const std::filesystem::path& manifestPath) const;
    PhysicalEvidenceSelectionDiagnostic remove() const;

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path temporaryPath() const;
    std::filesystem::path path_;
};

std::optional<std::filesystem::path> defaultProductionPhysicalEvidenceSelectionPath(
    std::string* error = nullptr);

// Release-owned profile authority. User state and community data cannot add entries
// to this table. The table is intentionally empty until P3-E-02 lands a reviewed,
// lawful non-protected real-game profile with exact executable/API evidence.
std::vector<ProductionGateCProfile> trustedProductionGateCProfiles();

struct ProductionInputAuthoritySnapshot {
    std::vector<ProductionGateCProfile> gateCProfiles;
    ProductionInputEvidenceClass inputEvidenceClass{ProductionInputEvidenceClass::None};
    std::optional<Phase3HardwareAcceptanceEvidence> physicalAcceptanceEvidence;
    PhysicalEvidenceSelectionDiagnostic physicalSelection;

    bool hasPhysicalEvidence() const noexcept {
        return inputEvidenceClass == ProductionInputEvidenceClass::Physical &&
               physicalAcceptanceEvidence.has_value();
    }
};

// Fresh default production snapshot used by both Host composition and guided
// validation. P3-HW evidence is reloaded and revalidated on every call; no stale
// typed object is persisted. Trusted Gate-C profiles remain release-owned.
ProductionInputAuthoritySnapshot loadDefaultProductionInputAuthoritySnapshot();

PhysicalEvidenceSelectionDiagnostic saveDefaultProductionPhysicalEvidenceSelection(
    const std::filesystem::path& manifestPath);

enum class ProductionInputAuthorityPrerequisiteCode : std::uint8_t {
    Ready = 0,
    MissingPhysicalEvidence,
    InvalidPhysicalEvidence,
    MissingTrustedGameProfile,
};

struct ProductionInputAuthorityPrerequisiteDiagnostic {
    ProductionInputAuthorityPrerequisiteCode code{
        ProductionInputAuthorityPrerequisiteCode::MissingPhysicalEvidence};
    std::string message;

    bool ready() const noexcept {
        return code == ProductionInputAuthorityPrerequisiteCode::Ready;
    }
};

// This is a prerequisite diagnostic, not a Physical compatibility verdict. Exact
// Seat device scope, process identity, receiver metrics and rollback are still
// validated by the production activation/guided-validation runtime.
ProductionInputAuthorityPrerequisiteDiagnostic
checkDefaultProductionInputAuthorityPrerequisites(std::string_view gameId);

std::string_view physicalEvidenceSelectionCodeName(
    PhysicalEvidenceSelectionCode code) noexcept;

} // namespace hydra::production
