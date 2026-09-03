#include "hydra/production_input_authority.hpp"

#include "phase3_hardware_evidence_fixture.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using namespace hydra;
using namespace hydra::production;

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class TempRoot final {
public:
    TempRoot() {
        const auto stamp = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
#ifdef _WIN32
        const auto process = static_cast<unsigned long long>(GetCurrentProcessId());
#else
        const auto process = 0ull;
#endif
        path_ = std::filesystem::temp_directory_path() /
                ("hydraseat-production-input-authority-" +
                 std::to_string(process) + "-" + std::to_string(stamp));
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        error.clear();
        std::filesystem::create_directories(path_, error);
        check(!error, "production input authority temp root can be created");
    }

    ~TempRoot() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void testMissingSelectionIsNonAuthoritative() {
    TempRoot root;
    ProductionPhysicalEvidenceSelectionStore store(root.path() / "selection.path");
    std::filesystem::path manifest;
    std::optional<Phase3HardwareAcceptanceEvidence> evidence;
    const auto loaded = store.load(manifest, evidence);
    check(loaded.code == PhysicalEvidenceSelectionCode::Missing,
          "missing selection is reported as Missing");
    check(!loaded.found(), "missing selection is not treated as found authority");
    check(manifest.empty() && !evidence.has_value(),
          "missing selection cannot manufacture typed physical evidence");
}

void testAcceptedSelectionRoundTripsTypedEvidence() {
    TempRoot root;
    test::SyntheticPhase3EvidenceFixture fixture;
    ProductionPhysicalEvidenceSelectionStore store(root.path() / "selection.path");

    const auto saved = store.saveAccepted(fixture.manifestPath());
    check(saved.code == PhysicalEvidenceSelectionCode::Success,
          "accepted P3-HW manifest can be selected");

    std::filesystem::path manifest;
    std::optional<Phase3HardwareAcceptanceEvidence> evidence;
    const auto loaded = store.load(manifest, evidence);
    check(loaded.code == PhysicalEvidenceSelectionCode::Success,
          "selected accepted P3-HW evidence reloads successfully");
    check(evidence.has_value(), "successful reload returns the typed evidence object");
    if (evidence) {
        check(*evidence == fixture.evidence(),
              "reloaded evidence equals the freshly validated typed manifest evidence");
    }
    std::error_code error;
    check(std::filesystem::equivalent(manifest, fixture.manifestPath(), error) && !error,
          "selection resolves back to the exact accepted manifest");
}

void testPendingAndTamperedEvidenceFailClosed() {
    TempRoot root;
    ProductionPhysicalEvidenceSelectionStore store(root.path() / "selection.path");

    test::SyntheticPhase3EvidenceFixture pending(test::SyntheticPhase3EvidenceMode::Pending);
    const auto pendingSave = store.saveAccepted(pending.manifestPath());
    check(pendingSave.code == PhysicalEvidenceSelectionCode::EvidenceRejected,
          "pending P3-HW manifest cannot be selected as production Physical authority");

    test::SyntheticPhase3EvidenceFixture accepted;
    check(store.saveAccepted(accepted.manifestPath()).code ==
              PhysicalEvidenceSelectionCode::Success,
          "accepted manifest can be selected before tamper check");
    accepted.tamperGateCTrace();

    std::filesystem::path manifest;
    std::optional<Phase3HardwareAcceptanceEvidence> evidence;
    const auto afterTamper = store.load(manifest, evidence);
    check(afterTamper.code == PhysicalEvidenceSelectionCode::EvidenceRejected,
          "tampered selected P3-HW artifacts are rejected on fresh production load");
    check(!evidence.has_value(),
          "tampered selection never exposes stale typed Physical evidence");
}

void testSelectionStoreRejectsGarbageAndRemoves() {
    TempRoot root;
    const auto storePath = root.path() / "selection.path";
    ProductionPhysicalEvidenceSelectionStore store(storePath);
    {
        std::ofstream output(storePath, std::ios::binary | std::ios::trunc);
        output << "relative\\manifest.json";
    }
    std::filesystem::path manifest;
    std::optional<Phase3HardwareAcceptanceEvidence> evidence;
    const auto relative = store.load(manifest, evidence);
    check(relative.code == PhysicalEvidenceSelectionCode::InvalidPath,
          "relative selection path is rejected");

    test::SyntheticPhase3EvidenceFixture accepted;
    check(store.saveAccepted(accepted.manifestPath()).code ==
              PhysicalEvidenceSelectionCode::Success,
          "accepted selection replaces invalid local content transactionally");
    check(store.remove().code == PhysicalEvidenceSelectionCode::Success,
          "selected physical evidence reference can be removed");
    check(store.remove().code == PhysicalEvidenceSelectionCode::Missing,
          "removing an absent selection remains a bounded no-op state");
}

void testReleaseProfileTableStartsFailClosed() {
    const auto profiles = trustedProductionGateCProfiles();
    check(profiles.empty(),
          "P3-E-02 is still blocked, so release-owned real-game Gate-C profile table stays empty");
}

} // namespace

int main() {
    testMissingSelectionIsNonAuthoritative();
    testAcceptedSelectionRoundTripsTypedEvidence();
    testPendingAndTamperedEvidenceFailClosed();
    testSelectionStoreRejectsGarbageAndRemoves();
    testReleaseProfileTableStartsFailClosed();

    if (failures != 0) {
        std::cerr << failures << " production input authority test(s) failed\n";
        return 1;
    }
    std::cout << "production input authority tests passed\n";
    return 0;
}
