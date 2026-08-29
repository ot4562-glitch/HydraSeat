#include "hydra/compatibility_share_model.hpp"

#include <string>
#include <utility>

namespace hydra::community {
namespace {

ShareModelDiagnostic fail(ShareModelCode code, std::string message,
                          std::optional<SubmissionCode> submission = std::nullopt) {
    return {code, submission, std::move(message)};
}

} // namespace

CompatibilityShareEntry* CompatibilityShareModel::activeMutable() noexcept {
    if (!activeIndex_ || *activeIndex_ >= history_.size()) return nullptr;
    return &history_[*activeIndex_];
}

const CompatibilityShareEntry* CompatibilityShareModel::active() const noexcept {
    if (!activeIndex_ || *activeIndex_ >= history_.size()) return nullptr;
    return &history_[*activeIndex_];
}

CompatibilityShareState CompatibilityShareModel::state() const noexcept {
    const auto* entry = active();
    return entry == nullptr ? CompatibilityShareState::TestNotRun : entry->state;
}

ShareModelDiagnostic CompatibilityShareModel::recordLocalResult(
    const compat::CompatibilityResult& result) {
    auto canonical = result;
    const auto validation = compat::canonicalizeCompatibilityResult(canonical);
    if (!validation.succeeded()) {
        return fail(ShareModelCode::InvalidResult,
                    "local compatibility result is invalid: " + validation.message);
    }

    if (auto* previous = activeMutable()) {
        previous->state = CompatibilityShareState::Superseded;
        previous->supersededByResultId = canonical.resultId;
    }

    CompatibilityShareEntry entry;
    entry.result = std::move(canonical);
    entry.state = CompatibilityShareState::LocalResultAvailable;
    try {
        history_.push_back(std::move(entry));
    } catch (...) {
        return fail(ShareModelCode::InvalidResult,
                    "local compatibility result history allocation failed");
    }
    activeIndex_ = history_.size() - 1u;
    submission_ = SubmissionSession{};
    return {};
}

ShareModelDiagnostic CompatibilityShareModel::preparePreview(SubmissionPreview& preview) {
    auto* entry = activeMutable();
    if (entry == nullptr) {
        return fail(ShareModelCode::NoActiveResult,
                    "run a local compatibility test before preparing a sharing preview");
    }
    if (entry->state != CompatibilityShareState::LocalResultAvailable &&
        entry->state != CompatibilityShareState::UserDeclined &&
        entry->state != CompatibilityShareState::SubmitFailed) {
        return fail(ShareModelCode::InvalidState,
                    "sharing preview cannot be regenerated from the current state");
    }

    SubmissionPreview candidate;
    const auto prepared = submission_.prepare(entry->result, candidate);
    if (!prepared.succeeded()) {
        return fail(ShareModelCode::InvalidResult, prepared.message, prepared.code);
    }
    entry->preview = candidate;
    entry->receipt.reset();
    entry->lastSubmissionCode.reset();
    entry->state = CompatibilityShareState::PreviewReady;
    preview = std::move(candidate);
    return {};
}

ShareModelDiagnostic CompatibilityShareModel::declineSharing() {
    auto* entry = activeMutable();
    if (entry == nullptr) {
        return fail(ShareModelCode::NoActiveResult,
                    "there is no local result whose sharing can be declined");
    }
    if (entry->state != CompatibilityShareState::PreviewReady) {
        return fail(ShareModelCode::PreviewRequired,
                    "review the exact redacted preview before declining sharing");
    }
    entry->state = CompatibilityShareState::UserDeclined;
    return {};
}

ShareModelDiagnostic CompatibilityShareModel::beginSubmission(
    std::string_view exactPreviewJson) {
    auto* entry = activeMutable();
    if (entry == nullptr) {
        return fail(ShareModelCode::NoActiveResult,
                    "there is no local result available for submission");
    }
    if (entry->state != CompatibilityShareState::PreviewReady &&
        entry->state != CompatibilityShareState::SubmitFailed) {
        return fail(ShareModelCode::InvalidState,
                    "submission can begin only from PreviewReady or SubmitFailed");
    }
    const auto approved = submission_.approve(exactPreviewJson);
    if (!approved.succeeded()) {
        return fail(ShareModelCode::PreviewRequired, approved.message, approved.code);
    }
    entry->state = CompatibilityShareState::SubmitPending;
    return {};
}

ShareModelDiagnostic CompatibilityShareModel::completeSubmission(
    CommunitySubmissionTransport& transport, SubmissionReceipt& receipt) {
    auto* entry = activeMutable();
    if (entry == nullptr) {
        return fail(ShareModelCode::NoActiveResult,
                    "there is no local result available for submission");
    }
    if (entry->state != CompatibilityShareState::SubmitPending) {
        return fail(ShareModelCode::InvalidState,
                    "submission completion requires the explicit SubmitPending state");
    }

    SubmissionReceipt candidate;
    const auto submitted = submission_.submit(transport, candidate);
    entry->lastSubmissionCode = submitted.code;
    if (submitted.succeeded()) {
        entry->receipt = candidate;
        entry->state = CompatibilityShareState::SubmitSucceeded;
        receipt = std::move(candidate);
        return {};
    }

    entry->state = CompatibilityShareState::SubmitFailed;
    if (candidate.receiptId || candidate.remoteAccepted ||
        candidate.result != SubmissionTransportResult::RetryableFailure) {
        entry->receipt = candidate;
        receipt = std::move(candidate);
    }
    return fail(ShareModelCode::SubmissionFailed, submitted.message, submitted.code);
}

std::string_view compatibilityShareStateName(CompatibilityShareState value) noexcept {
    switch (value) {
        case CompatibilityShareState::TestNotRun: return "TestNotRun";
        case CompatibilityShareState::LocalResultAvailable: return "LocalResultAvailable";
        case CompatibilityShareState::PreviewReady: return "PreviewReady";
        case CompatibilityShareState::UserDeclined: return "UserDeclined";
        case CompatibilityShareState::SubmitPending: return "SubmitPending";
        case CompatibilityShareState::SubmitSucceeded: return "SubmitSucceeded";
        case CompatibilityShareState::SubmitFailed: return "SubmitFailed";
        case CompatibilityShareState::Superseded: return "Superseded";
    }
    return "Unknown";
}

std::string_view shareModelCodeName(ShareModelCode value) noexcept {
    switch (value) {
        case ShareModelCode::Success: return "Success";
        case ShareModelCode::InvalidResult: return "InvalidResult";
        case ShareModelCode::NoActiveResult: return "NoActiveResult";
        case ShareModelCode::InvalidState: return "InvalidState";
        case ShareModelCode::PreviewRequired: return "PreviewRequired";
        case ShareModelCode::SubmissionFailed: return "SubmissionFailed";
    }
    return "Unknown";
}

} // namespace hydra::community
