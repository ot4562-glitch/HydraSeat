#pragma once

#include "hydra/audio_endpoint_inventory.hpp"
#include "hydra/process_group.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::audio {

inline constexpr std::size_t kMaxAudioSessions = 512u;
inline constexpr std::size_t kMaxSessionIdentifierChars = 4096u;

enum class SessionState : std::uint8_t {
    Inactive = 0,
    Active = 1,
    Expired = 2,
};

struct SessionRecord {
    std::wstring endpointId;
    std::wstring sessionInstanceId;
    std::wstring sessionIdentifier;
    std::uint32_t processId{0};
    std::uint64_t processCreationTime100ns{0};
    std::wstring executablePath;
    SessionState state{SessionState::Inactive};
    bool processIdentityVerified{false};
    bool spansMultipleProcesses{false};
    bool systemSoundsSession{false};

    bool operator==(const SessionRecord&) const = default;
};

struct SessionSnapshot {
    std::uint64_t sourceGeneration{0};
    std::vector<SessionRecord> sessions;

    bool operator==(const SessionSnapshot&) const = default;
};

// Read-only observer. Implementations may enumerate sessions and process
// identity, but must never change volume, mute, endpoint choice, or policy.
class SessionSource {
public:
    virtual ~SessionSource() = default;

    virtual bool enumerate(std::span<const EndpointRecord> endpoints,
                           std::vector<SessionRecord>& sessions,
                           std::string& error) noexcept = 0;
    virtual std::uint64_t changeGeneration() const noexcept = 0;
};

std::shared_ptr<SessionSource> makeNativeSessionSource();

class SessionInventory {
public:
    explicit SessionInventory(std::shared_ptr<SessionSource> source);

    bool refresh(const EndpointSnapshot& endpoints, std::string* error = nullptr);
    bool needsRefresh() const noexcept;
    const std::optional<SessionSnapshot>& current() const noexcept {
        return current_;
    }

private:
    std::shared_ptr<SessionSource> source_;
    std::optional<SessionSnapshot> current_;
};

enum class RouteBackendKind : std::uint8_t {
    ObserveOnly = 0,
    ProviderManaged = 1,
    ProcessLoopbackRelayExperimental = 2,
};

enum class RouteCapability : std::uint8_t {
    Unsupported = 0,
    SatisfiedWithoutMutation = 1,
    Mutable = 2,
};

struct RouteRequest {
    SeatId seatId{0};
    process::ProcessTreeSnapshot processTree;
    std::wstring targetEndpointId;

    bool operator==(const RouteRequest&) const = default;
};

struct OwnedSessionEvidence {
    std::vector<SessionRecord> ownedSessions;
    std::vector<SessionRecord> pidMatchesWithoutVerifiedIdentity;
    bool allOwnedSessionsOnTarget{false};

    bool operator==(const OwnedSessionEvidence&) const = default;
};

enum class RoutePhase : std::uint8_t {
    Unprepared = 0,
    WaitingForSession = 1,
    Ready = 2,
    Satisfied = 3,
    Applied = 4,
    Unsupported = 5,
    Failed = 6,
    RecoveryRequired = 7,
};

enum class RouteError : std::uint8_t {
    None = 0,
    InvalidSeat = 1,
    InvalidProcessTree = 2,
    TargetEndpointMissing = 3,
    TargetEndpointUnavailable = 4,
    OwnershipUnverified = 5,
    BackendUnsupported = 6,
    SnapshotFailed = 7,
    ApplyFailed = 8,
    VerificationFailed = 9,
    RollbackFailed = 10,
    ExternalDrift = 11,
};

struct BackendState {
    std::vector<std::uint8_t> opaque;

    bool operator==(const BackendState&) const = default;
};

struct RouteStatus {
    RoutePhase phase{RoutePhase::Unprepared};
    RouteError error{RouteError::None};
    RouteBackendKind backendKind{RouteBackendKind::ObserveOnly};
    RouteCapability capability{RouteCapability::Unsupported};
    OwnedSessionEvidence evidence;
    bool mutated{false};
    bool rollbackVerified{false};

    bool operator==(const RouteStatus&) const = default;
};

// A mutable backend is deliberately abstract. P5-AUD-02 does not grant
// permission to use undocumented Windows audio-policy COM interfaces. A native
// backend may be added only for a documented provider/Windows path whose state
// can be captured, verified, and rolled back.
class RouteBackend {
public:
    virtual ~RouteBackend() = default;

    virtual RouteBackendKind kind() const noexcept = 0;
    virtual RouteCapability capability(
        const RouteRequest& request,
        const OwnedSessionEvidence& evidence) const noexcept = 0;
    virtual bool captureState(const RouteRequest& request,
                              const OwnedSessionEvidence& evidence,
                              BackendState& state,
                              std::string& error) noexcept = 0;
    virtual bool apply(const RouteRequest& request,
                       const BackendState& before,
                       std::string& error) noexcept = 0;
    virtual bool rollback(const RouteRequest& request,
                          const BackendState& before,
                          std::string& error) noexcept = 0;
};

class ObserveOnlyRouteBackend final : public RouteBackend {
public:
    RouteBackendKind kind() const noexcept override {
        return RouteBackendKind::ObserveOnly;
    }
    RouteCapability capability(
        const RouteRequest& request,
        const OwnedSessionEvidence& evidence) const noexcept override;
    bool captureState(const RouteRequest& request,
                      const OwnedSessionEvidence& evidence,
                      BackendState& state,
                      std::string& error) noexcept override;
    bool apply(const RouteRequest& request,
               const BackendState& before,
               std::string& error) noexcept override;
    bool rollback(const RouteRequest& request,
                  const BackendState& before,
                  std::string& error) noexcept override;
};

// Coordinates read-only ownership evidence with an optional typed mutable
// backend. `attempt()` is intentionally repeatable so a game that creates its
// audio session after launch can move WaitingForSession -> Satisfied/Applied.
class RouteTransaction {
public:
    RouteTransaction(RouteRequest request,
                     std::shared_ptr<SessionInventory> sessions,
                     std::shared_ptr<RouteBackend> backend);

    RouteStatus attempt(const EndpointSnapshot& endpoints,
                        std::string* error = nullptr);
    RouteStatus rollback(const EndpointSnapshot& endpoints,
                         std::string* error = nullptr);

    const RouteStatus& status() const noexcept { return status_; }
    const RouteRequest& request() const noexcept { return request_; }

private:
    bool validateRequest(const EndpointSnapshot& endpoints,
                         std::string& error);
    bool refreshEvidence(const EndpointSnapshot& endpoints,
                         OwnedSessionEvidence& evidence,
                         std::string& error);
    bool verifyApplied(const EndpointSnapshot& endpoints,
                       OwnedSessionEvidence& evidence,
                       std::string& error);
    bool verifyRollback(const EndpointSnapshot& endpoints,
                        OwnedSessionEvidence& evidence,
                        std::string& error);

    RouteRequest request_;
    std::shared_ptr<SessionInventory> sessions_;
    std::shared_ptr<RouteBackend> backend_;
    std::optional<BackendState> before_;
    std::optional<OwnedSessionEvidence> beforeEvidence_;
    RouteStatus status_;
};

OwnedSessionEvidence collectOwnedSessionEvidence(
    const RouteRequest& request,
    std::span<const SessionRecord> sessions);

bool sessionMatchesOwnedProcess(const process::ProcessTreeSnapshot& processTree,
                                const SessionRecord& session) noexcept;
std::string_view routePhaseName(RoutePhase value) noexcept;
std::string_view routeErrorName(RouteError value) noexcept;
std::string_view routeBackendKindName(RouteBackendKind value) noexcept;

} // namespace hydra::audio
