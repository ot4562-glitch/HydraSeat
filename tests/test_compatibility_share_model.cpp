#include "hydra/compatibility_share_model.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace hydra;
using namespace hydra::community;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

compat::CompatibilityResult resultFixture(std::string resultId,
                                          bool protectedExperimental = false) {
    compat::CompatibilityResult value;
    value.resultId = std::move(resultId);
    value.timestampClass = compat::TimestampClass::MonthBucket;
    value.timestampBucket = "2026-08";
    value.gameId = "game:share";
    value.providerId = "custom";
    value.providerAppId = "share-app";
    value.gameVersion = "1.0";
    value.hydraSeatVersion = "0.1.0";
    value.hydraSeatBuild = "share-build";
    value.windowsBuildClass = "win10-2009";
    value.architecture = "x64";
    value.scenario = protectedExperimental ? compat::Scenario::ProtectedExperiment
                                           : compat::Scenario::DifferentGames;
    value.protectedExperimental = protectedExperimental;
    value.launch = compat::EvidenceStatus::Pass;
    value.inputIsolation = compat::EvidenceStatus::Pass;
    value.cleanExit = compat::EvidenceStatus::Pass;
    value.origin = compat::ResultOrigin::ControlledProcess;
    value.provenanceId = "share-fixture";
    value.provenanceRevision = 5u;
    return value;
}

class FakeTransport final : public CommunitySubmissionTransport {
public:
    TransportAvailability availabilityValue{TransportAvailability::Available};
    SubmissionReceipt response{SubmissionTransportResult::Accepted, "receipt-share", true};
    std::size_t calls{0u};
    std::vector<SubmissionEnvelope> seen;

    TransportAvailability availability() const noexcept override { return availabilityValue; }
    SubmissionReceipt submit(const SubmissionEnvelope& envelope) noexcept override {
        ++calls;
        try {
            seen.push_back(envelope);
        } catch (...) {
            return {SubmissionTransportResult::RetryableFailure, std::nullopt, false};
        }
        return response;
    }
};

void testLocalPreviewDeclineFlowKeepsTechnicalResult() {
    CompatibilityShareModel model;
    check(model.state() == CompatibilityShareState::TestNotRun && model.active() == nullptr,
          "share model starts in TestNotRun without synthetic evidence");

    const auto local = resultFixture("share-1", true);
    check(model.recordLocalResult(local).succeeded() &&
              model.state() == CompatibilityShareState::LocalResultAvailable,
          "valid local test creates LocalResultAvailable state");

    SubmissionPreview preview;
    check(model.preparePreview(preview).succeeded() &&
              model.state() == CompatibilityShareState::PreviewReady &&
              preview.protectedExperimental,
          "preview is generated locally and retains Protected / Experimental marker");
    check(model.active()->result == local,
          "preparing a sharing preview does not change technical result truth");

    check(model.declineSharing().succeeded() &&
              model.state() == CompatibilityShareState::UserDeclined &&
              model.active()->result == local,
          "declining optional sharing preserves the local result intact");

    SubmissionPreview secondPreview;
    check(model.preparePreview(secondPreview).succeeded() && secondPreview == preview,
          "declined result can later be previewed again with identical canonical payload");
}

void testOfflineFailureAndExplicitRetryRemainLocalFirst() {
    CompatibilityShareModel model;
    const auto local = resultFixture("share-offline");
    model.recordLocalResult(local);
    SubmissionPreview preview;
    model.preparePreview(preview);
    check(model.beginSubmission(preview.exactRedactedJson).succeeded() &&
              model.state() == CompatibilityShareState::SubmitPending,
          "explicit exact-preview consent moves the record to SubmitPending");

    FakeTransport transport;
    transport.availabilityValue = TransportAvailability::Offline;
    SubmissionReceipt receipt{SubmissionTransportResult::Accepted, "sentinel", true};
    const auto failed = model.completeSubmission(transport, receipt);
    check(failed.code == ShareModelCode::SubmissionFailed &&
              failed.submissionCode == SubmissionCode::TransportOffline &&
              model.state() == CompatibilityShareState::SubmitFailed && transport.calls == 0u,
          "offline community transport becomes SubmitFailed without transport invocation");
    check(model.active()->result == local && receipt.receiptId == "sentinel",
          "offline failure preserves both local technical result and caller receipt sentinel");

    check(model.beginSubmission(preview.exactRedactedJson).succeeded(),
          "retry requires another explicit transition from SubmitFailed");
    transport.availabilityValue = TransportAvailability::Available;
    check(model.completeSubmission(transport, receipt).succeeded() &&
              model.state() == CompatibilityShareState::SubmitSucceeded &&
              transport.calls == 1u && receipt.receiptId == "receipt-share",
          "later explicit retry may succeed without rerunning the local compatibility test");
    check(model.active()->result == local,
          "successful upload state still does not mutate technical result truth");
}

void testMismatchedPreviewCannotBeginSubmission() {
    CompatibilityShareModel model;
    model.recordLocalResult(resultFixture("share-mismatch"));
    SubmissionPreview preview;
    model.preparePreview(preview);
    auto changed = preview.exactRedactedJson;
    changed += " ";
    const auto rejected = model.beginSubmission(changed);
    check(rejected.code == ShareModelCode::PreviewRequired &&
              rejected.submissionCode == SubmissionCode::PreviewMismatch &&
              model.state() == CompatibilityShareState::PreviewReady,
          "changed preview bytes cannot be converted into user consent");

    FakeTransport transport;
    SubmissionReceipt receipt;
    check(model.completeSubmission(transport, receipt).code == ShareModelCode::InvalidState &&
              transport.calls == 0u,
          "transport cannot be invoked without the explicit SubmitPending state");
}

void testNewLocalResultSupersedesOldRecordWithoutDeletingHistory() {
    CompatibilityShareModel model;
    const auto oldResult = resultFixture("share-old");
    const auto newResult = resultFixture("share-new", true);
    model.recordLocalResult(oldResult);
    SubmissionPreview preview;
    model.preparePreview(preview);
    model.declineSharing();

    check(model.recordLocalResult(newResult).succeeded() && model.history().size() == 2u &&
              model.state() == CompatibilityShareState::LocalResultAvailable,
          "new local test creates a new active share record");
    check(model.history()[0].state == CompatibilityShareState::Superseded &&
              model.history()[0].result == oldResult &&
              model.history()[0].supersededByResultId == "share-new",
          "older local result remains inspectable and explicitly marked Superseded");
    check(model.active()->result == newResult && model.active()->protectedExperimental(),
          "new active result preserves its own Protected / Experimental semantics");
}

void testInvalidResultNeverCreatesHistory() {
    CompatibilityShareModel model;
    auto invalid = resultFixture("share-invalid");
    invalid.redaction.personalPathsExcluded = false;
    check(model.recordLocalResult(invalid).code == ShareModelCode::InvalidResult &&
              model.history().empty() && model.state() == CompatibilityShareState::TestNotRun,
          "privacy-invalid local result cannot become shareable history");
}

} // namespace

int main() {
    testLocalPreviewDeclineFlowKeepsTechnicalResult();
    testOfflineFailureAndExplicitRetryRemainLocalFirst();
    testMismatchedPreviewCannotBeginSubmission();
    testNewLocalResultSupersedesOldRecordWithoutDeletingHistory();
    testInvalidResultNeverCreatesHistory();
    if (failures != 0) {
        std::cerr << failures << " compatibility share model test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Compatibility share model tests passed.\n";
    return EXIT_SUCCESS;
}
