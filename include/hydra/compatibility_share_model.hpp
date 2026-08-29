#pragma once

#include "hydra/community_submission.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::community {

enum class CompatibilityShareState : std::uint8_t {
    TestNotRun = 0,
    LocalResultAvailable = 1,
    PreviewReady = 2,
    UserDeclined = 3,
    SubmitPending = 4,
    SubmitSucceeded = 5,
    SubmitFailed = 6,
    Superseded = 7,
};

struct CompatibilityShareEntry {
    compat::CompatibilityResult result;
    CompatibilityShareState state{CompatibilityShareState::LocalResultAvailable};
    std::optional<SubmissionPreview> preview;
    std::optional<SubmissionReceipt> receipt;
    std::optional<SubmissionCode> lastSubmissionCode;
    std::optional<std::string> supersededByResultId;

    bool protectedExperimental() const noexcept { return result.protectedExperimental; }
};

enum class ShareModelCode : std::uint8_t {
    Success = 0,
    InvalidResult,
    NoActiveResult,
    InvalidState,
    PreviewRequired,
    SubmissionFailed,
};

struct ShareModelDiagnostic {
    ShareModelCode code{ShareModelCode::Success};
    std::optional<SubmissionCode> submissionCode;
    std::string message;

    bool succeeded() const noexcept { return code == ShareModelCode::Success; }
};

// UI-independent local-first state machine. Results live in history independently
// of transport state, so failed/declined submissions cannot erase technical truth.
class CompatibilityShareModel final {
public:
    ShareModelDiagnostic recordLocalResult(const compat::CompatibilityResult& result);
    ShareModelDiagnostic preparePreview(SubmissionPreview& preview);
    ShareModelDiagnostic declineSharing();
    ShareModelDiagnostic beginSubmission(std::string_view exactPreviewJson);
    ShareModelDiagnostic completeSubmission(CommunitySubmissionTransport& transport,
                                            SubmissionReceipt& receipt);

    CompatibilityShareState state() const noexcept;
    const CompatibilityShareEntry* active() const noexcept;
    const std::vector<CompatibilityShareEntry>& history() const noexcept { return history_; }

private:
    CompatibilityShareEntry* activeMutable() noexcept;

    std::vector<CompatibilityShareEntry> history_;
    std::optional<std::size_t> activeIndex_;
    SubmissionSession submission_;
};

std::string_view compatibilityShareStateName(CompatibilityShareState value) noexcept;
std::string_view shareModelCodeName(ShareModelCode value) noexcept;

} // namespace hydra::community
