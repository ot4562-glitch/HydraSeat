#pragma once

#include "hydra/process_launcher.hpp"
#include "hydra/seat_game_lifecycle.hpp"
#include "hydra/workspace_manager.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::launch {

inline constexpr std::size_t kMaximumGameIdBytes = 256u;
inline constexpr std::size_t kMaximumLaunchArguments = 128u;
inline constexpr std::size_t kMaximumLaunchEnvironmentOverrides = 128u;

enum class ResourceKind : std::uint8_t {
    Recovery = 0,
    Process = 1,
    Window = 2,
    Display = 3,
    Input = 4,
    Controller = 5,
    Audio = 6,
};

struct Requirements {
    bool display{true};
    bool keyboard{false};
    bool mouse{false};
    bool controller{false};
    bool audioOutput{false};
    bool windowOwnership{true};
    bool recovery{true};
    bool highRisk{false};

    bool operator==(const Requirements&) const = default;
};

struct Capabilities {
    bool process{true};
    bool window{true};
    bool display{true};
    bool input{true};
    bool controller{true};
    bool audio{true};
    bool recovery{true};

    bool operator==(const Capabilities&) const = default;
};

struct TargetSpec {
    std::string gameId;
    process::ProcessLaunchSpec process;
    Requirements requirements;
    Capabilities capabilities;
    bool highRiskApproved{false};

    bool operator==(const TargetSpec& other) const noexcept {
        return gameId == other.gameId &&
               process.seatId == other.process.seatId &&
               process.executablePath == other.process.executablePath &&
               process.arguments == other.process.arguments &&
               process.workingDirectory == other.process.workingDirectory &&
               process.environmentOverrides == other.process.environmentOverrides &&
               process.architecture == other.process.architecture &&
               process.containment == other.process.containment &&
               process.createNewConsole == other.process.createNewConsole &&
               requirements == other.requirements &&
               capabilities == other.capabilities &&
               highRiskApproved == other.highRiskApproved;
    }
};

struct SeatLaunchInput {
    SeatConfig seat;
    TargetSpec target;

    bool operator==(const SeatLaunchInput&) const = default;
};

struct SeatActivationPlan {
    SeatId seatId{0};
    SeatConfig seat;
    TargetSpec target;
    std::vector<ResourceKind> resources;
    std::uint64_t fingerprint{0};

    bool operator==(const SeatActivationPlan&) const = default;
};

struct TwoSeatLaunchPlan {
    std::uint32_t schemaVersion{1};
    std::uint64_t fingerprint{0};
    std::vector<SeatActivationPlan> seats;

    bool operator==(const TwoSeatLaunchPlan&) const = default;
};

enum class CompileIssueCode : std::uint8_t {
    ActiveSeatCount = 0,
    InvalidSeatId = 1,
    DuplicateSeatId = 2,
    InactiveSeat = 3,
    InvalidGameId = 4,
    InvalidProcessSpec = 5,
    MissingDisplay = 6,
    MissingKeyboard = 7,
    MissingMouse = 8,
    MissingController = 9,
    MissingAudioOutput = 10,
    DuplicateExclusiveDisplay = 11,
    DuplicateExclusiveKeyboard = 12,
    DuplicateExclusiveMouse = 13,
    DuplicateExclusiveController = 14,
    DuplicateExclusiveAudioOutput = 15,
    MissingCapability = 16,
    HighRiskApprovalRequired = 17,
};

struct CompileIssue {
    CompileIssueCode code{CompileIssueCode::ActiveSeatCount};
    SeatId seatId{0};
    std::string detail;

    bool operator==(const CompileIssue&) const = default;
};

struct CompileResult {
    std::optional<TwoSeatLaunchPlan> plan;
    std::vector<CompileIssue> issues;

    bool succeeded() const noexcept { return plan.has_value(); }
};

CompileResult compileTwoSeatLaunchPlan(std::span<const SeatLaunchInput> inputs);

class ISeatActivationResource {
public:
    virtual ~ISeatActivationResource() = default;

    virtual ResourceKind kind() const noexcept = 0;
    // prepare() may inspect/reserve caller-owned in-memory state but must not
    // perform an externally visible persistent/system mutation.
    virtual bool prepare(const SeatActivationPlan& plan,
                         const runtime::SeatGameBinding& binding,
                         std::string& error) = 0;
    virtual bool activate(std::string& error) = 0;
    virtual bool verifyActive(std::string& error) = 0;
    virtual bool rollback(std::string& error) noexcept = 0;
    virtual bool verifySafe(std::string& error) noexcept = 0;
    virtual bool active() const noexcept = 0;
};

class ISeatActivationResourceFactory {
public:
    virtual ~ISeatActivationResourceFactory() = default;

    virtual std::unique_ptr<ISeatActivationResource> create(
        ResourceKind kind,
        const SeatActivationPlan& plan,
        std::string& error) = 0;
};

class PlannedSeatGameInstance final : public runtime::ISeatGameInstance {
public:
    PlannedSeatGameInstance(SeatActivationPlan plan,
                            std::shared_ptr<ISeatActivationResourceFactory> factory);
    ~PlannedSeatGameInstance() override;

    bool start(const runtime::SeatGameBinding& binding,
               std::string& error) override;
    bool stop(std::string& error) noexcept override;
    bool verifyStopped(std::string& error) noexcept override;
    bool running() const noexcept override;

    const SeatActivationPlan& plan() const noexcept { return plan_; }

private:
    bool rollbackResources(std::string& error) noexcept;
    bool verifySafeResources(std::string& error) noexcept;

    SeatActivationPlan plan_;
    std::shared_ptr<ISeatActivationResourceFactory> factory_;
    std::vector<std::unique_ptr<ISeatActivationResource>> resources_;
    bool started_{false};
    bool recoveryRequired_{false};
};

class PlannedSeatGameInstanceFactory final
    : public runtime::ISeatGameInstanceFactory {
public:
    PlannedSeatGameInstanceFactory(
        TwoSeatLaunchPlan plan,
        std::shared_ptr<ISeatActivationResourceFactory> resources);

    std::unique_ptr<runtime::ISeatGameInstance> create(
        SeatId seatId, std::string& error) override;

    const TwoSeatLaunchPlan& plan() const noexcept { return plan_; }

private:
    TwoSeatLaunchPlan plan_;
    std::shared_ptr<ISeatActivationResourceFactory> resources_;
};

std::string_view resourceKindName(ResourceKind kind) noexcept;
std::string_view compileIssueCodeName(CompileIssueCode code) noexcept;

} // namespace hydra::launch
