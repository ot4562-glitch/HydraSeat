#pragma once

#include "hydra/directinput_policy.hpp"
#include "hydra/virtual_xinput_state.hpp"
#include "hydra/workspace_manager.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace hydra::controller {

inline constexpr std::size_t kMaxControllerSources = 64u;
inline constexpr std::size_t kMaxControllerPersistentIdChars = 512u;
inline constexpr std::size_t kMaxControllerDisplayNameChars = 512u;

enum class ApiSurface : std::uint8_t {
    XInput = 1,
    DirectInput = 2,
    GameInput = 3,
};

enum class IdentityQuality : std::uint8_t {
    RuntimeOnly = 0,
    Stable = 1,
};

struct SourceDescriptor {
    std::string runtimeKey;
    std::optional<std::wstring> persistentId;
    std::wstring displayName;
    ApiSurface api{ApiSurface::XInput};
    IdentityQuality identityQuality{IdentityQuality::RuntimeOnly};
    std::uint8_t runtimeXInputSlotHint{gatec::kNoRuntimeXInputSlot};
    std::optional<directinput::DirectInputInstanceId> directInputInstanceId;
    bool connected{false};
    std::uint64_t sourceGeneration{0};
    bool stateAvailable{false};
    bool vibrationSupported{false};
    gatec::NormalizedXInputGamepad gamepad{};
    gatec::NormalizedXInputCapabilities capabilities{};
    gatec::NormalizedXInputBattery battery{};

    bool operator==(const SourceDescriptor&) const = default;
};

struct SourceSnapshot {
    std::uint64_t generation{0};
    std::uint64_t pollSequence{0};
    std::vector<SourceDescriptor> sources;

    bool operator==(const SourceSnapshot&) const = default;
};

class SourceBackend {
public:
    virtual ~SourceBackend() = default;

    virtual bool scan(std::vector<SourceDescriptor>& sources,
                      std::string& error) noexcept = 0;
    virtual bool setVibration(std::string_view runtimeKey,
                              std::uint16_t leftMotor,
                              std::uint16_t rightMotor,
                              std::string& error) noexcept = 0;
};

std::shared_ptr<SourceBackend> makeNativeControllerSourceBackend();

class PollWorker {
public:
    explicit PollWorker(std::shared_ptr<SourceBackend> backend);
    ~PollWorker();

    PollWorker(const PollWorker&) = delete;
    PollWorker& operator=(const PollWorker&) = delete;

    bool pollOnce(std::string* error = nullptr);
    bool start(std::chrono::milliseconds interval, std::string* error = nullptr);
    void stop() noexcept;

    SourceSnapshot snapshot() const;
    bool running() const noexcept;

private:
    void loop(std::chrono::milliseconds interval) noexcept;

    std::shared_ptr<SourceBackend> backend_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    SourceSnapshot snapshot_;
    std::unordered_map<std::string, bool> previousConnected_;
    std::unordered_map<std::string, std::uint8_t> previousRuntimeSlots_;
    std::unordered_map<std::string, std::uint64_t> sourceGenerations_;
    std::thread thread_;
    bool stopRequested_{false};
    bool running_{false};
};

struct SeatBindingRequest {
    SeatId seatId{0};
    ApiSurface api{ApiSurface::XInput};
    std::optional<std::wstring> persistentControllerId;
    // XInput slots are explicitly session-only hints. They are never serialized
    // as a persistent Seat controller identity by this layer.
    std::optional<std::uint8_t> runtimeXInputSlot;

    bool operator==(const SeatBindingRequest&) const = default;
};

struct SeatBinding {
    SeatId seatId{0};
    ApiSurface api{ApiSurface::XInput};
    std::string runtimeKey;
    std::optional<std::wstring> persistentControllerId;
    std::uint64_t sourceKey{0};
    std::uint64_t sourceGeneration{0};
    std::uint8_t runtimeXInputSlotHint{gatec::kNoRuntimeXInputSlot};

    bool operator==(const SeatBinding&) const = default;
};

enum class BindingIssueCode : std::uint8_t {
    InvalidSeat = 0,
    DuplicateSeat = 1,
    V1SeatLimitExceeded = 2,
    MissingPersistentIdentity = 3,
    SourceNotFound = 4,
    SourceDisconnected = 5,
    SourceApiMismatch = 6,
    SourceAlreadyAssigned = 7,
    RuntimeSlotNotSessionExplicit = 8,
    AmbiguousSource = 9,
};

struct BindingIssue {
    BindingIssueCode code{BindingIssueCode::InvalidSeat};
    SeatId seatId{0};
    std::wstring controllerId;

    bool operator==(const BindingIssue&) const = default;
};

struct BindingPlan {
    bool valid{true};
    std::vector<SeatBinding> bindings;
    std::vector<BindingIssue> issues;
};

// Stable persistent IDs are preferred. Runtime-only XInput sources require an
// explicit session slot selection; auto-binding by slot is forbidden.
BindingPlan planSeatBindings(std::span<const SeatBindingRequest> requests,
                             const SourceSnapshot& sources);

class SeatControllerRuntime {
public:
    SeatControllerRuntime(std::shared_ptr<PollWorker> worker,
                          std::shared_ptr<SourceBackend> backend);

    bool configure(std::span<const SeatBindingRequest> requests,
                   std::string* error = nullptr);
    bool refresh(std::string* error = nullptr);

    gatec::VirtualXInputResult mapSeatToContext(
        SeatId seatId,
        gatec::VirtualXInputContext& context,
        std::uint8_t logicalSlot = 0);
    gatec::VirtualXInputResult updateSeatContext(
        SeatId seatId,
        gatec::VirtualXInputContext& context);

    bool requestVibration(SeatId seatId,
                          gatec::VirtualXInputContext& context,
                          std::uint8_t logicalSlot,
                          std::uint16_t leftMotor,
                          std::uint16_t rightMotor,
                          std::string* error = nullptr);

    std::optional<SeatBinding> binding(SeatId seatId) const;
    std::uint64_t sourceGeneration(std::string_view runtimeKey) const noexcept;

private:
    std::optional<SourceDescriptor> findSource(std::string_view runtimeKey) const;
    std::uint64_t nextSequence() noexcept;

    std::shared_ptr<PollWorker> worker_;
    std::shared_ptr<SourceBackend> backend_;
    SourceSnapshot snapshot_;
    std::unordered_map<SeatId, SeatBinding> bindings_;
    std::uint64_t sequence_{0};
};

std::wstring formatDirectInputPersistentId(
    const directinput::DirectInputInstanceId& value);
std::string_view apiSurfaceName(ApiSurface api) noexcept;
std::string_view bindingIssueCodeName(BindingIssueCode code) noexcept;

} // namespace hydra::controller
