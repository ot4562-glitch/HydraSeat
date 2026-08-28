#include "hydra/provider_launch_plan.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hydra::plan {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

class StableHash final {
public:
    void byte(std::uint8_t value) noexcept {
        value_ ^= static_cast<std::uint64_t>(value);
        value_ *= kFnvPrime;
    }

    void boolean(bool value) noexcept { byte(value ? 1u : 0u); }

    void u32(std::uint32_t value) noexcept {
        for (unsigned int shift = 0; shift < 32u; shift += 8u) {
            byte(static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void u64(std::uint64_t value) noexcept {
        for (unsigned int shift = 0; shift < 64u; shift += 8u) {
            byte(static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void text(std::string_view value) noexcept {
        u64(static_cast<std::uint64_t>(value.size()));
        for (const char ch : value) {
            byte(static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
        }
    }

    void wide(std::wstring_view value) noexcept {
        u64(static_cast<std::uint64_t>(value.size()));
        for (const wchar_t ch : value) u32(static_cast<std::uint32_t>(ch));
    }

    std::uint64_t value() const noexcept { return value_; }

private:
    std::uint64_t value_{kFnvOffset};
};

PlanCompileResult failure(PlanIssueCode code, SeatId seatId, std::string detail) {
    PlanCompileResult result;
    result.issues.push_back({code, seatId, std::move(detail)});
    return result;
}

const profile::PersistedSeatConfig* findSeat(const profile::SeatConfigDocument& document,
                                             SeatId seatId) noexcept {
    for (const auto& seat : document.seats) {
        if (seat.seatId == seatId) return &seat;
    }
    return nullptr;
}

const profile::PlayerProfile* findPlayer(const profile::PlayerProfileDocument& document,
                                         std::string_view playerId) noexcept {
    for (const auto& player : document.players) {
        if (player.playerId == playerId) return &player;
    }
    return nullptr;
}

const profile::GameRecord* findGame(const profile::GameRecordDocument& document,
                                    std::string_view gameId) noexcept {
    for (const auto& game : document.games) {
        if (game.gameId == gameId) return &game;
    }
    return nullptr;
}

const profile::TwoPlayerSetup* findSetup(const profile::TwoPlayerSetupDocument& document,
                                         std::string_view setupId) noexcept {
    for (const auto& setup : document.setups) {
        if (setup.setupId == setupId) return &setup;
    }
    return nullptr;
}

const GameRuntimeRequirement* findRequirement(
    std::span<const GameRuntimeRequirement> requirements,
    std::string_view gameId,
    bool& duplicate) noexcept {
    const GameRuntimeRequirement* found = nullptr;
    duplicate = false;
    for (const auto& requirement : requirements) {
        if (requirement.gameId != gameId) continue;
        if (found != nullptr) {
            duplicate = true;
            return nullptr;
        }
        found = &requirement;
    }
    return found;
}

provider::LauncherProviderAdapter* findProvider(
    std::span<const ProviderAdapterBinding> providers,
    std::string_view providerId,
    bool& duplicate) noexcept {
    provider::LauncherProviderAdapter* found = nullptr;
    duplicate = false;
    for (const auto& binding : providers) {
        if (binding.providerId != providerId) continue;
        if (found != nullptr) {
            duplicate = true;
            return nullptr;
        }
        found = binding.adapter;
    }
    return found;
}

std::optional<std::string> resolveAccount(const profile::PlayerProfile& player,
                                          std::string_view providerId,
                                          bool& ambiguous) {
    std::optional<std::string> selected;
    ambiguous = false;
    for (const auto& account : player.providerAccounts) {
        if (account.providerId != providerId) continue;
        if (selected) {
            ambiguous = true;
            return std::nullopt;
        }
        selected = account.accountRef;
    }
    return selected;
}

void hashCompatibility(StableHash& hash,
                       const std::optional<profile::CompatibilityReference>& value) noexcept {
    hash.boolean(value.has_value());
    if (!value) return;
    hash.text(value->recordId);
    hash.text(value->provenance);
    hash.u32(value->evidenceRevision);
}

void hashRecipe(StableHash& hash,
                const std::optional<profile::InstanceRecipe>& recipe) noexcept {
    hash.boolean(recipe.has_value());
    if (!recipe) return;
    hash.u64(static_cast<std::uint64_t>(recipe->arguments.size()));
    for (const auto& argument : recipe->arguments) hash.wide(argument);
    hash.boolean(recipe->workingDirectory.has_value());
    if (recipe->workingDirectory) hash.wide(*recipe->workingDirectory);
    hash.boolean(recipe->dataRoot.has_value());
    if (recipe->dataRoot) hash.wide(*recipe->dataRoot);
}

void hashRequirements(StableHash& hash, const launch::Requirements& value) noexcept {
    hash.boolean(value.display);
    hash.boolean(value.keyboard);
    hash.boolean(value.mouse);
    hash.boolean(value.controller);
    hash.boolean(value.audioOutput);
    hash.boolean(value.windowOwnership);
    hash.boolean(value.recovery);
    hash.boolean(value.highRisk);
}

void hashCapabilities(StableHash& hash, const launch::Capabilities& value) noexcept {
    hash.boolean(value.process);
    hash.boolean(value.window);
    hash.boolean(value.display);
    hash.boolean(value.input);
    hash.boolean(value.controller);
    hash.boolean(value.audio);
    hash.boolean(value.recovery);
}

template <typename Range>
void hashSortedWideRange(StableHash& hash, const Range& range) {
    std::vector<std::wstring> values(range.begin(), range.end());
    std::sort(values.begin(), values.end());
    hash.u64(static_cast<std::uint64_t>(values.size()));
    for (const auto& value : values) hash.wide(value);
}

std::uint64_t hardwareFingerprint(const profile::PersistedSeatConfig& seat) {
    StableHash hash;
    hash.u32(seat.seatId);
    hashSortedWideRange(hash, seat.displayIds);
    hash.boolean(seat.primaryDisplayId.has_value());
    if (seat.primaryDisplayId) hash.wide(*seat.primaryDisplayId);
    hashSortedWideRange(hash, seat.keyboardIds);
    hashSortedWideRange(hash, seat.mouseIds);
    hashSortedWideRange(hash, seat.controllerIds);
    hash.boolean(seat.audioOutputEndpointId.has_value());
    if (seat.audioOutputEndpointId) hash.wide(*seat.audioOutputEndpointId);
    hash.boolean(seat.audioInputEndpointId.has_value());
    if (seat.audioInputEndpointId) hash.wide(*seat.audioInputEndpointId);
    return hash.value();
}

std::optional<PlanIssue> preflightSeat(const profile::PersistedSeatConfig& seat,
                                       const GameRuntimeRequirement& requirement) {
    const auto& needs = requirement.requirements;
    const auto& caps = requirement.capabilities;
    if (needs.display && seat.displayIds.empty()) {
        return PlanIssue{PlanIssueCode::MissingDisplay, seat.seatId,
                         "selected game requires a display"};
    }
    if (seat.primaryDisplayId &&
        std::find(seat.displayIds.begin(), seat.displayIds.end(), *seat.primaryDisplayId) ==
            seat.displayIds.end()) {
        return PlanIssue{PlanIssueCode::MissingDisplay, seat.seatId,
                         "primary display is outside the Seat display group"};
    }
    if (needs.keyboard && seat.keyboardIds.empty()) {
        return PlanIssue{PlanIssueCode::MissingKeyboard, seat.seatId,
                         "selected game requires a keyboard"};
    }
    if (needs.mouse && seat.mouseIds.empty()) {
        return PlanIssue{PlanIssueCode::MissingMouse, seat.seatId,
                         "selected game requires a pointing device"};
    }
    if (needs.controller && seat.controllerIds.empty()) {
        return PlanIssue{PlanIssueCode::MissingController, seat.seatId,
                         "selected game requires a controller"};
    }
    if (needs.audioOutput && !seat.audioOutputEndpointId) {
        return PlanIssue{PlanIssueCode::MissingAudioOutput, seat.seatId,
                         "selected game requires an audio output"};
    }
    if (!caps.process || (needs.windowOwnership && !caps.window) ||
        (needs.display && !caps.display) ||
        ((needs.keyboard || needs.mouse) && !caps.input) ||
        (needs.controller && !caps.controller) ||
        (needs.audioOutput && !caps.audio) || (needs.recovery && !caps.recovery)) {
        return PlanIssue{PlanIssueCode::MissingCapability, seat.seatId,
                         "required runtime capability is unavailable"};
    }
    if (needs.highRisk && !requirement.highRiskApproved) {
        return PlanIssue{PlanIssueCode::HighRiskApprovalRequired, seat.seatId,
                         "selected compatibility path requires explicit approval"};
    }
    return std::nullopt;
}

std::optional<PlanIssue> preflightExclusiveHardware(
    const std::vector<const profile::PersistedSeatConfig*>& seats) {
    std::set<std::wstring> displays;
    std::set<std::wstring> keyboards;
    std::set<std::wstring> mice;
    std::set<std::wstring> controllers;
    std::set<std::wstring> audio;
    for (const auto* seat : seats) {
        for (const auto& id : seat->displayIds) {
            if (!displays.insert(id).second) {
                return PlanIssue{PlanIssueCode::DuplicateExclusiveHardware, seat->seatId,
                                 "display identity is assigned to multiple selected Seats"};
            }
        }
        for (const auto& id : seat->keyboardIds) {
            if (!keyboards.insert(id).second) {
                return PlanIssue{PlanIssueCode::DuplicateExclusiveHardware, seat->seatId,
                                 "keyboard identity is assigned to multiple selected Seats"};
            }
        }
        for (const auto& id : seat->mouseIds) {
            if (!mice.insert(id).second) {
                return PlanIssue{PlanIssueCode::DuplicateExclusiveHardware, seat->seatId,
                                 "mouse identity is assigned to multiple selected Seats"};
            }
        }
        for (const auto& id : seat->controllerIds) {
            if (!controllers.insert(id).second) {
                return PlanIssue{PlanIssueCode::DuplicateExclusiveHardware, seat->seatId,
                                 "controller identity is assigned to multiple selected Seats"};
            }
        }
        if (seat->audioOutputEndpointId &&
            !audio.insert(*seat->audioOutputEndpointId).second) {
            return PlanIssue{PlanIssueCode::DuplicateExclusiveHardware, seat->seatId,
                             "audio output is assigned to multiple selected Seats"};
        }
    }
    return std::nullopt;
}

std::uint64_t fingerprint(const std::vector<SeatProviderLaunchPlan>& seats) noexcept {
    StableHash hash;
    hash.u32(kProviderLaunchPlanSchemaVersion);
    hash.u64(static_cast<std::uint64_t>(seats.size()));
    for (const auto& seat : seats) {
        hash.u32(seat.seatId);
        hash.text(seat.playerId);
        hash.text(seat.gameId);
        hash.boolean(seat.setupId.has_value());
        if (seat.setupId) hash.text(*seat.setupId);
        hash.u32(seat.instanceIndex);
        hash.u64(seat.requirementRevision);
        hashCompatibility(hash, seat.compatibility);
        hashRecipe(hash, seat.instanceRecipe);
        hash.u64(seat.hardwareFingerprint);
        hashRequirements(hash, seat.requirements);
        hashCapabilities(hash, seat.capabilities);
        const auto& request = seat.launchRequest;
        hash.text(request.providerId);
        hash.text(request.gameId);
        hash.boolean(request.providerAppId.has_value());
        if (request.providerAppId) hash.text(*request.providerAppId);
        hash.boolean(request.accountRef.has_value());
        if (request.accountRef) hash.text(*request.accountRef);
        hash.u64(request.metadataRevision);
        hash.u32(static_cast<std::uint32_t>(request.targetKind));
        hash.wide(request.target);
        hash.u64(static_cast<std::uint64_t>(request.arguments.size()));
        for (const auto& argument : request.arguments) hash.wide(argument);
        hash.boolean(request.workingDirectory.has_value());
        if (request.workingDirectory) hash.wide(*request.workingDirectory);
        hash.text(request.launchCorrelationId);
    }
    return hash.value();
}

PlanCompileResult schemaFailure(const profile::SchemaDiagnostic& diagnostic,
                                PlanIssueCode code,
                                std::string_view label) {
    return failure(code, 0u, std::string(label) + ": " + diagnostic.message);
}

} // namespace

PlanCompileResult compileProviderAwareLaunchPlan(
    const profile::SeatConfigDocument& seats,
    const profile::PlayerProfileDocument& players,
    const profile::GameRecordDocument& games,
    const profile::TwoPlayerSetupDocument& setups,
    const profile::RuntimeSessionSelection& selection,
    std::span<const ProviderAdapterBinding> providers,
    std::span<const GameRuntimeRequirement> requirements) {
    try {
        auto diagnostic = profile::validateSeatConfigDocument(seats);
        if (!diagnostic.succeeded()) {
            return schemaFailure(diagnostic, PlanIssueCode::InvalidSeatDocument, "Seat document");
        }
        diagnostic = profile::validatePlayerProfileDocument(players);
        if (!diagnostic.succeeded()) {
            return schemaFailure(diagnostic, PlanIssueCode::InvalidPlayerDocument,
                                 "Player document");
        }
        diagnostic = profile::validateGameRecordDocument(games);
        if (!diagnostic.succeeded()) {
            return schemaFailure(diagnostic, PlanIssueCode::InvalidGameDocument, "Game document");
        }
        diagnostic = profile::validateTwoPlayerSetupDocument(setups);
        if (!diagnostic.succeeded()) {
            return schemaFailure(diagnostic, PlanIssueCode::InvalidSetupDocument, "Setup document");
        }
        diagnostic = profile::validateRuntimeSessionSelection(selection, seats, players, games,
                                                               setups);
        if (!diagnostic.succeeded()) {
            return schemaFailure(diagnostic, PlanIssueCode::InvalidRuntimeSelection,
                                 "Runtime selection");
        }
        if (selection.bindings.empty() || selection.bindings.size() > 2u) {
            return failure(PlanIssueCode::ActiveSeatCount, 0u,
                           "provider-aware plan requires one or two selected Seats");
        }

        std::vector<profile::RuntimeBinding> bindings = selection.bindings;
        std::sort(bindings.begin(), bindings.end(), [](const auto& left, const auto& right) {
            return left.seatId < right.seatId;
        });

        const bool sameGame = bindings.size() == 2u &&
                              bindings[0].gameId == bindings[1].gameId;
        if (sameGame) {
            if (!bindings[0].setupId || !bindings[1].setupId ||
                *bindings[0].setupId != *bindings[1].setupId) {
                return failure(PlanIssueCode::MissingTwoPlayerSetup, 0u,
                               "same-game two-Seat selection requires one shared setup" );
            }
            if (bindings[0].instanceIndex == bindings[1].instanceIndex) {
                return failure(PlanIssueCode::InvalidTwoPlayerSetup, 0u,
                               "same-game Seats must select different setup instances");
            }
        } else if (bindings.size() == 2u &&
                   (bindings[0].setupId.has_value() || bindings[1].setupId.has_value())) {
            return failure(PlanIssueCode::InvalidTwoPlayerSetup, 0u,
                           "two-player setup cannot bind two different games");
        }

        std::vector<const profile::PersistedSeatConfig*> selectedSeats;
        selectedSeats.reserve(bindings.size());
        for (const auto& binding : bindings) {
            const auto* seat = findSeat(seats, binding.seatId);
            if (seat == nullptr) {
                return failure(PlanIssueCode::MissingSeat, binding.seatId,
                               "selected Seat is missing");
            }
            if (!seat->active) {
                return failure(PlanIssueCode::InactiveSeat, binding.seatId,
                               "selected Seat is inactive");
            }
            selectedSeats.push_back(seat);
        }
        if (const auto issue = preflightExclusiveHardware(selectedSeats)) {
            PlanCompileResult result;
            result.issues.push_back(*issue);
            return result;
        }

        ProviderAwareLaunchPlan compiled;
        compiled.seats.reserve(bindings.size());
        for (std::size_t index = 0; index < bindings.size(); ++index) {
            const auto& binding = bindings[index];
            const auto* seat = selectedSeats[index];
            const auto* player = findPlayer(players, binding.playerId);
            if (player == nullptr) {
                return failure(PlanIssueCode::MissingPlayer, binding.seatId,
                               "selected Player is missing");
            }
            const auto* game = findGame(games, binding.gameId);
            if (game == nullptr) {
                return failure(PlanIssueCode::MissingGame, binding.seatId,
                               "selected Game is missing");
            }

            bool duplicateRequirement = false;
            const auto* requirement = findRequirement(requirements, game->gameId,
                                                      duplicateRequirement);
            if (duplicateRequirement) {
                return failure(PlanIssueCode::DuplicateRequirement, binding.seatId,
                               "multiple runtime requirement records match the selected Game");
            }
            if (requirement == nullptr) {
                return failure(PlanIssueCode::MissingRequirement, binding.seatId,
                               "selected Game has no exact runtime requirement record");
            }
            if (requirement->revision == 0u || requirement->compatibility != game->compatibility) {
                return failure(PlanIssueCode::StaleCompatibility, binding.seatId,
                               "runtime requirement evidence does not match the selected Game");
            }
            if (const auto issue = preflightSeat(*seat, *requirement)) {
                PlanCompileResult result;
                result.issues.push_back(*issue);
                return result;
            }

            bool duplicateProvider = false;
            auto* adapter = findProvider(providers, game->providerId, duplicateProvider);
            if (duplicateProvider) {
                return failure(PlanIssueCode::DuplicateProvider, binding.seatId,
                               "multiple provider adapters match the selected Game");
            }
            if (adapter == nullptr) {
                return failure(PlanIssueCode::MissingProvider, binding.seatId,
                               "selected Game provider is unavailable");
            }
            const auto descriptor = adapter->descriptor();
            if (descriptor.providerId != game->providerId ||
                descriptor.availability != provider::ProviderAvailability::Available) {
                return failure(PlanIssueCode::ProviderUnavailable, binding.seatId,
                               "provider snapshot is absent, offline, or mismatched");
            }

            bool ambiguousAccount = false;
            auto account = resolveAccount(*player, game->providerId, ambiguousAccount);
            if (ambiguousAccount) {
                return failure(PlanIssueCode::AmbiguousAccountReference, binding.seatId,
                               "Player has multiple account references for this provider");
            }

            std::optional<profile::InstanceRecipe> recipe;
            if (binding.setupId) {
                const auto* setup = findSetup(setups, *binding.setupId);
                if (setup == nullptr || setup->gameId != game->gameId ||
                    setup->instances.size() != 2u || binding.instanceIndex >= 2u) {
                    return failure(PlanIssueCode::InvalidTwoPlayerSetup, binding.seatId,
                                   "selected setup does not match the Game/instance" );
                }
                if (setup->compatibility && setup->compatibility != game->compatibility) {
                    return failure(PlanIssueCode::StaleCompatibility, binding.seatId,
                                   "two-player setup compatibility evidence is stale");
                }
                recipe = setup->instances[binding.instanceIndex];
            }

            provider::LaunchSelection providerSelection;
            providerSelection.providerId = game->providerId;
            providerSelection.gameId = game->gameId;
            providerSelection.providerAppId = game->providerAppId;
            providerSelection.accountRef = std::move(account);
            providerSelection.expectedMetadataRevision = descriptor.metadataRevision;
            if (recipe) providerSelection.instanceArguments = recipe->arguments;

            provider::ProviderLaunchRequest request;
            const auto providerDiagnostic =
                provider::buildLaunchRequest(*adapter, providerSelection, request);
            if (!providerDiagnostic.succeeded()) {
                return failure(PlanIssueCode::ProviderLaunchRejected, binding.seatId,
                               std::string(provider::providerResultName(providerDiagnostic.result)) +
                                   ": " + providerDiagnostic.message);
            }

            SeatProviderLaunchPlan plannedSeat;
            plannedSeat.seatId = binding.seatId;
            plannedSeat.playerId = binding.playerId;
            plannedSeat.gameId = binding.gameId;
            plannedSeat.setupId = binding.setupId;
            plannedSeat.instanceIndex = binding.instanceIndex;
            plannedSeat.requirementRevision = requirement->revision;
            plannedSeat.compatibility = requirement->compatibility;
            plannedSeat.instanceRecipe = std::move(recipe);
            plannedSeat.hardwareFingerprint = hardwareFingerprint(*seat);
            plannedSeat.requirements = requirement->requirements;
            plannedSeat.capabilities = requirement->capabilities;
            plannedSeat.launchRequest = std::move(request);
            compiled.seats.push_back(std::move(plannedSeat));
        }

        compiled.fingerprint = fingerprint(compiled.seats);
        PlanCompileResult result;
        result.plan = std::move(compiled);
        return result;
    } catch (...) {
        return failure(PlanIssueCode::InvalidRuntimeSelection, 0u,
                       "provider-aware launch plan compilation failed unexpectedly");
    }
}

std::string_view planIssueCodeName(PlanIssueCode code) noexcept {
    switch (code) {
    case PlanIssueCode::InvalidSeatDocument: return "InvalidSeatDocument";
    case PlanIssueCode::InvalidPlayerDocument: return "InvalidPlayerDocument";
    case PlanIssueCode::InvalidGameDocument: return "InvalidGameDocument";
    case PlanIssueCode::InvalidSetupDocument: return "InvalidSetupDocument";
    case PlanIssueCode::InvalidRuntimeSelection: return "InvalidRuntimeSelection";
    case PlanIssueCode::ActiveSeatCount: return "ActiveSeatCount";
    case PlanIssueCode::MissingSeat: return "MissingSeat";
    case PlanIssueCode::InactiveSeat: return "InactiveSeat";
    case PlanIssueCode::MissingPlayer: return "MissingPlayer";
    case PlanIssueCode::MissingGame: return "MissingGame";
    case PlanIssueCode::MissingRequirement: return "MissingRequirement";
    case PlanIssueCode::DuplicateRequirement: return "DuplicateRequirement";
    case PlanIssueCode::StaleCompatibility: return "StaleCompatibility";
    case PlanIssueCode::MissingProvider: return "MissingProvider";
    case PlanIssueCode::DuplicateProvider: return "DuplicateProvider";
    case PlanIssueCode::ProviderUnavailable: return "ProviderUnavailable";
    case PlanIssueCode::ProviderLaunchRejected: return "ProviderLaunchRejected";
    case PlanIssueCode::AmbiguousAccountReference: return "AmbiguousAccountReference";
    case PlanIssueCode::MissingTwoPlayerSetup: return "MissingTwoPlayerSetup";
    case PlanIssueCode::InvalidTwoPlayerSetup: return "InvalidTwoPlayerSetup";
    case PlanIssueCode::MissingDisplay: return "MissingDisplay";
    case PlanIssueCode::MissingKeyboard: return "MissingKeyboard";
    case PlanIssueCode::MissingMouse: return "MissingMouse";
    case PlanIssueCode::MissingController: return "MissingController";
    case PlanIssueCode::MissingAudioOutput: return "MissingAudioOutput";
    case PlanIssueCode::MissingCapability: return "MissingCapability";
    case PlanIssueCode::HighRiskApprovalRequired: return "HighRiskApprovalRequired";
    case PlanIssueCode::DuplicateExclusiveHardware: return "DuplicateExclusiveHardware";
    }
    return "Unknown";
}

} // namespace hydra::plan
