#pragma once

#include "hydra/watchdog_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hydra {

inline constexpr std::size_t kHidHideSessionMaxDevices = 64u;
inline constexpr std::size_t kHidHideSessionMaxApplications = 64u;
inline constexpr std::size_t kHidHideSessionMaxRequestedDevices = 16u;
inline constexpr std::size_t kHidHideSessionMaxRequestedApplications = 8u;
inline constexpr std::size_t kHidHideSessionMaxIdentifierChars = 1024u;
inline constexpr std::uint32_t kHidHideSessionMinExpiryMs = 1'000u;
inline constexpr std::uint32_t kHidHideSessionMaxExpiryMs = 60'000u;
inline constexpr std::uint32_t kHidHideSessionNoSpareInputMaxExpiryMs = 10'000u;

// P3-D-02 is deliberately split into a portable transaction core and an
// optional native mutation platform. The transaction core can be fully tested
// without touching a physical device. Native mutation remains runtime-gated by
// physicalAcceptanceRecorded + nativeMutationApproved.
enum class HidHideSessionPhase : std::uint8_t {
    Idle = 0,
    Prepared = 1,
    Active = 2,
    RollingBack = 3,
    RecoveryRequired = 4,
};

enum class HidHideSessionResultCode : std::uint8_t {
    Ok = 0,
    AlreadySatisfied = 1,
    InvalidRequest = 2,
    UnsupportedState = 3,
    PhysicalGateRequired = 4,
    NativeMutationDisabled = 5,
    BackendFailure = 6,
    VerificationFailed = 7,
    RecoveryRequired = 8,
    Expired = 9,
    RecoveryNotArmed = 10,
};

struct HidHideSessionSnapshot {
    bool active{false};
    bool inverseWhitelist{false};
    std::vector<std::wstring> blockedDeviceInstanceIds;
    std::vector<std::wstring> allowedApplications;

    bool operator==(const HidHideSessionSnapshot&) const = default;
};

struct HidHideSessionRequest {
    std::vector<std::wstring> deviceInstanceIds;
    std::vector<std::wstring> allowedApplications;
    bool replacementPathVerified{false};
    bool recoveryReady{false};
    bool spareRecoveryInputPresent{false};
    bool physicalAcceptanceRecorded{false};
    bool nativeMutationApproved{false};
    std::uint32_t expiryMilliseconds{0};
    std::uint64_t generation{0};

    bool operator==(const HidHideSessionRequest&) const = default;
};

struct HidHideSessionPlan {
    HidHideSessionRequest request;
    HidHideSessionSnapshot before;
    HidHideSessionSnapshot applied;
    std::uint64_t preparedAtMilliseconds{0};
    std::uint64_t expiryAtMilliseconds{0};

    bool operator==(const HidHideSessionPlan&) const = default;
};

struct HidHideSessionResult {
    HidHideSessionResultCode code{HidHideSessionResultCode::Ok};
    HidHideSessionPhase phase{HidHideSessionPhase::Idle};
    std::optional<HidHideSessionPlan> plan;
    std::string diagnostic;

    bool succeeded() const noexcept {
        return code == HidHideSessionResultCode::Ok ||
               code == HidHideSessionResultCode::AlreadySatisfied ||
               code == HidHideSessionResultCode::Expired;
    }
};

class HidHideSessionPlatform {
public:
    virtual ~HidHideSessionPlatform() = default;

    // Read/write the persistent state relevant to one HydraSeat transaction.
    // blockedDeviceInstanceIds here means the pre-existing persistent HidHide
    // blacklist only; P3-D-02 never adds HydraSeat session devices to that list.
    // allowedApplications use HidHide full image names (NT device paths), not
    // arbitrary command lines or shell paths.
    virtual bool readState(HidHideSessionSnapshot& snapshot,
                           std::string& error) noexcept = 0;
    virtual bool writeState(const HidHideSessionSnapshot& snapshot,
                            std::string& error) noexcept = 0;

    // Preferred v1 isolation path: HidHide's process-lifetime session blacklist.
    // The driver owns these entries by caller PID and removes them when that
    // process exits. This avoids persistent device-blacklist mutation.
    virtual bool addSessionBlacklist(
        std::span<const std::wstring> deviceInstanceIds,
        std::string& error) noexcept = 0;
    virtual bool clearSessionBlacklist(std::string& error) noexcept = 0;
    virtual bool mutationSupported() const noexcept = 0;
    virtual bool sessionBlacklistSupported() const noexcept = 0;
};

// Production Windows implementation. It remains unreachable from the guarded
// lab activation path until the request carries real physical acceptance and
// explicit native-mutation approval. Non-Windows builds return a read-only
// unsupported platform object.
std::shared_ptr<HidHideSessionPlatform> makeNativeHidHideSessionPlatform();

class HidHideSessionTransaction {
public:
    explicit HidHideSessionTransaction(std::shared_ptr<HidHideSessionPlatform> platform);
    ~HidHideSessionTransaction();

    HidHideSessionTransaction(const HidHideSessionTransaction&) = delete;
    HidHideSessionTransaction& operator=(const HidHideSessionTransaction&) = delete;

    HidHideSessionResult prepare(HidHideSessionRequest request,
                                 std::uint64_t nowMilliseconds);
    HidHideSessionResult activate(std::uint64_t nowMilliseconds);
    HidHideSessionResult expireIfNeeded(std::uint64_t nowMilliseconds);
    HidHideSessionResult rollback();

    HidHideSessionPhase phase() const noexcept { return phase_; }
    const std::optional<HidHideSessionPlan>& plan() const noexcept { return plan_; }

private:
    HidHideSessionResult result(HidHideSessionResultCode code,
                                std::string diagnostic) const;
    HidHideSessionResult rollbackInternal(bool expired);

    std::shared_ptr<HidHideSessionPlatform> platform_;
    HidHideSessionPhase phase_{HidHideSessionPhase::Idle};
    std::optional<HidHideSessionPlan> plan_;
    bool sessionEntriesActive_{false};
};

bool validateHidHideSessionRequest(const HidHideSessionRequest& request,
                                   std::string& error);
bool equivalentHidHideSessionSnapshots(
    const HidHideSessionSnapshot& left,
    const HidHideSessionSnapshot& right);
HidHideSessionSnapshot makeHidHideSessionAppliedState(
    const HidHideSessionSnapshot& before,
    const HidHideSessionRequest& request);

watchdog::RollbackActionDescriptor makeHidHideSessionRollbackAction(
    std::uint32_t actionId,
    std::uint32_t activationOrdinal,
    std::uint32_t timeoutMilliseconds,
    std::uint64_t generation,
    std::uint64_t snapshotResourceId) noexcept;

std::string_view hidHideSessionPhaseName(HidHideSessionPhase phase) noexcept;
std::string_view hidHideSessionResultCodeName(HidHideSessionResultCode code) noexcept;

} // namespace hydra
