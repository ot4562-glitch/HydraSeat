#include "hydra/launcher_ui_model.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace hydra::launcher_ui {
namespace {

UiDiagnostic fail(UiResult result, std::string message) {
    return {result, std::move(message)};
}

bool validIdentifier(std::string_view value) noexcept {
    if (value.empty() || value.size() > profile::kMaximumIdentifierBytes) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.' || ch == ':';
    });
}

bool absoluteWindowsPath(std::wstring_view value) noexcept {
    const auto alpha = [](wchar_t ch) {
        return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
    };
    return (value.size() >= 3u && alpha(value[0]) && value[1] == L':' &&
            (value[2] == L'\\' || value[2] == L'/')) ||
           (value.size() >= 3u &&
            ((value[0] == L'\\' && value[1] == L'\\') ||
             (value[0] == L'/' && value[1] == L'/')));
}

UiDiagnostic validateRequirementSnapshot(
    const std::vector<plan::GameRuntimeRequirement>& requirements) {
    if (requirements.size() > kMaximumRuntimeRequirements) {
        return fail(UiResult::InvalidRequirement,
                    "runtime requirement snapshot exceeds the UI bound");
    }
    std::set<std::string> games;
    for (const auto& requirement : requirements) {
        if (!validIdentifier(requirement.gameId) || requirement.revision == 0u ||
            !games.insert(requirement.gameId).second) {
            return fail(UiResult::InvalidRequirement,
                        "runtime requirements must have unique stable revisions");
        }
    }
    return {};
}

template <typename Value, typename Predicate>
Value* findPtr(std::vector<Value>& range, Predicate predicate) {
    const auto found = std::find_if(range.begin(), range.end(), predicate);
    return found == range.end() ? nullptr : &*found;
}

template <typename Value, typename Predicate>
const Value* findPtr(const std::vector<Value>& range, Predicate predicate) {
    const auto found = std::find_if(range.begin(), range.end(), predicate);
    return found == range.end() ? nullptr : &*found;
}

} // namespace

profile::GameRecordDocument LauncherUiModel::gameDocument() const {
    profile::GameRecordDocument document;
    document.games.reserve(library_.entries.size());
    for (const auto& entry : library_.entries) document.games.push_back(entry.game);
    std::sort(document.games.begin(), document.games.end(),
              [](const auto& left, const auto& right) {
                  return left.gameId < right.gameId;
              });
    return document;
}

UiDiagnostic LauncherUiModel::validateLibrary(
    const catalog::LocalGameCatalog& library) const {
    profile::GameRecordDocument document;
    std::set<std::string> identities;
    document.games.reserve(library.entries.size());
    for (const auto& entry : library.entries) {
        if (!identities.insert(entry.game.gameId).second) {
            return fail(UiResult::InvalidLibrary,
                        "game library contains a duplicate stable identity");
        }
        document.games.push_back(entry.game);
    }
    const auto diagnostic = profile::validateGameRecordDocument(document);
    if (!diagnostic.succeeded()) {
        return fail(UiResult::InvalidLibrary,
                    "game library is invalid: " + diagnostic.message);
    }
    return {};
}

UiDiagnostic LauncherUiModel::validateProviderBinding(
    const plan::ProviderAdapterBinding& binding) const {
    if (!validIdentifier(binding.providerId) || binding.adapter == nullptr ||
        (binding.providerAppId && !validIdentifier(*binding.providerAppId))) {
        return fail(UiResult::InvalidProvider,
                    "provider binding identity is missing or invalid");
    }
    const auto descriptor = binding.adapter->descriptor();
    if (descriptor.providerId != binding.providerId) {
        return fail(UiResult::InvalidProvider,
                    "provider binding does not match its adapter descriptor");
    }
    return {};
}

UiDiagnostic LauncherUiModel::initialize(
    profile::SeatConfigDocument seats,
    catalog::LocalGameCatalog library,
    profile::TwoPlayerSetupDocument setups,
    std::vector<plan::ProviderAdapterBinding> providers,
    std::vector<plan::GameRuntimeRequirement> requirements) {
    return initializeShared(std::move(seats), std::move(library), {}, {},
                            std::move(setups), std::move(providers),
                            std::move(requirements));
}

UiDiagnostic LauncherUiModel::initializeShared(
    profile::SeatConfigDocument seats,
    catalog::LocalGameCatalog library,
    profile::PlayerProfileDocument players,
    std::vector<PlayerPresentation> playerPresentation,
    profile::TwoPlayerSetupDocument setups,
    std::vector<plan::ProviderAdapterBinding> providers,
    std::vector<plan::GameRuntimeRequirement> requirements) {
    const auto seatDiagnostic = profile::validateSeatConfigDocument(seats);
    if (!seatDiagnostic.succeeded()) {
        return fail(UiResult::InvalidSeatDocument, seatDiagnostic.message);
    }
    if (const auto diagnostic = validateLibrary(library); !diagnostic.succeeded()) {
        return diagnostic;
    }
    const auto setupDiagnostic = profile::validateTwoPlayerSetupDocument(setups);
    if (!setupDiagnostic.succeeded()) {
        return fail(UiResult::InvalidSetupDocument, setupDiagnostic.message);
    }
    for (const auto& binding : providers) {
        if (const auto diagnostic = validateProviderBinding(binding);
            !diagnostic.succeeded()) return diagnostic;
    }
    if (providers.size() > kMaximumUiProviders) {
        return fail(UiResult::InvalidProvider,
                    "provider snapshot exceeds the UI bound");
    }
    if (const auto diagnostic = validateRequirementSnapshot(requirements);
        !diagnostic.succeeded()) return diagnostic;

    const auto playerDiagnostic = profile::validatePlayerProfileDocument(players);
    if (!playerDiagnostic.succeeded()) {
        return fail(UiResult::InvalidPlayer, playerDiagnostic.message);
    }
    if (playerPresentation.size() != players.players.size()) {
        return fail(UiResult::InvalidPlayer,
                    "Player presentation snapshot must cover every Player exactly once");
    }
    std::set<std::string> presentationPlayers;
    for (const auto& presentation : playerPresentation) {
        const auto* player = findPtr(players.players, [&](const auto& value) {
            return value.playerId == presentation.playerId;
        });
        if (player == nullptr ||
            !presentationPlayers.insert(presentation.playerId).second ||
            (presentation.avatarPath &&
             (!absoluteWindowsPath(*presentation.avatarPath) ||
              presentation.avatarPath->size() > profile::kMaximumPathCodeUnits)) ||
            presentation.recentGameIds.size() > kMaximumRecentGames) {
            return fail(UiResult::InvalidPlayer,
                        "Player presentation identity, avatar, or recent list is invalid");
        }
        std::set<std::string> recent;
        for (const auto& gameId : presentation.recentGameIds) {
            if (!recent.insert(gameId).second ||
                findPtr(library.entries, [&](const auto& entry) {
                    return entry.game.gameId == gameId;
                }) == nullptr) {
                return fail(UiResult::InvalidPlayer,
                            "Player recent games must be unique local library entries");
            }
        }
        if (presentation.preferredSeat &&
            findPtr(seats.seats, [&](const auto& seat) {
                return seat.active && seat.seatId == *presentation.preferredSeat;
            }) == nullptr) {
            return fail(UiResult::InvalidPlayer,
                        "Player preferred Seat is unavailable or inactive");
        }
    }

    seats_ = std::move(seats);
    library_ = std::move(library);
    players_ = std::move(players);
    playerPresentation_ = std::move(playerPresentation);
    setups_ = std::move(setups);
    providers_ = std::move(providers);
    requirements_ = std::move(requirements);
    selection_ = {};
    nextPlayerId_ = 1u;
    return {};
}

UiDiagnostic LauncherUiModel::replaceLibrary(catalog::LocalGameCatalog library) {
    if (const auto diagnostic = validateLibrary(library); !diagnostic.succeeded()) {
        return diagnostic;
    }
    for (const auto& binding : selection_.bindings) {
        const auto found = std::find_if(library.entries.begin(), library.entries.end(),
                                        [&](const auto& entry) {
                                            return entry.game.gameId == binding.gameId;
                                        });
        if (found == library.entries.end()) {
            return fail(UiResult::InvalidSelection,
                        "refreshed library no longer contains a selected game");
        }
    }
    library_ = std::move(library);
    return {};
}

UiDiagnostic LauncherUiModel::replaceRequirements(
    std::vector<plan::GameRuntimeRequirement> requirements) {
    if (const auto diagnostic = validateRequirementSnapshot(requirements);
        !diagnostic.succeeded()) return diagnostic;
    requirements_ = std::move(requirements);
    return {};
}

UiDiagnostic LauncherUiModel::attachProvider(plan::ProviderAdapterBinding binding) {
    if (const auto diagnostic = validateProviderBinding(binding); !diagnostic.succeeded()) {
        return diagnostic;
    }
    if (providers_.size() >= kMaximumUiProviders) {
        return fail(UiResult::InvalidProvider,
                    "provider snapshot exceeds the UI bound");
    }
    for (const auto& existing : providers_) {
        if (existing.providerId == binding.providerId &&
            existing.providerAppId == binding.providerAppId) {
            return fail(UiResult::InvalidProvider,
                        "provider binding already exists for this application");
        }
    }
    providers_.push_back(std::move(binding));
    return {};
}

UiDiagnostic LauncherUiModel::createPlayer(
    std::wstring displayName,
    std::string preferredLocale,
    std::optional<std::wstring> avatarPath,
    std::string& playerId) {
    if (displayName.empty() ||
        displayName.size() > profile::kMaximumDisplayNameCodeUnits ||
        preferredLocale.empty() || preferredLocale.size() > profile::kMaximumLocaleBytes ||
        (avatarPath && (!absoluteWindowsPath(*avatarPath) ||
                        avatarPath->size() > profile::kMaximumPathCodeUnits))) {
        return fail(UiResult::InvalidPlayer,
                    "Player name, locale, or optional avatar is invalid");
    }

    auto candidatePlayers = players_;
    auto candidatePresentation = playerPresentation_;
    std::string candidateId;
    do {
        candidateId = "player-" + std::to_string(nextPlayerId_++);
    } while (std::any_of(candidatePlayers.players.begin(), candidatePlayers.players.end(),
                         [&](const auto& player) { return player.playerId == candidateId; }));
    candidatePlayers.players.push_back(
        {candidateId, std::move(displayName), std::move(preferredLocale), {}});
    candidatePresentation.push_back({candidateId, std::move(avatarPath), {}, std::nullopt});
    const auto diagnostic = profile::validatePlayerProfileDocument(candidatePlayers);
    if (!diagnostic.succeeded()) {
        return fail(UiResult::InvalidPlayer, diagnostic.message);
    }
    players_ = std::move(candidatePlayers);
    playerPresentation_ = std::move(candidatePresentation);
    playerId = std::move(candidateId);
    return {};
}

UiDiagnostic LauncherUiModel::renamePlayer(std::string_view playerId,
                                            std::wstring displayName) {
    if (displayName.empty() ||
        displayName.size() > profile::kMaximumDisplayNameCodeUnits) {
        return fail(UiResult::InvalidPlayer, "Player display name is invalid");
    }
    auto candidate = players_;
    auto* player = findPtr(candidate.players, [&](const auto& value) {
        return value.playerId == playerId;
    });
    if (player == nullptr) return fail(UiResult::InvalidPlayer, "Player was not found");
    player->displayName = std::move(displayName);
    const auto diagnostic = profile::validatePlayerProfileDocument(candidate);
    if (!diagnostic.succeeded()) return fail(UiResult::InvalidPlayer, diagnostic.message);
    players_ = std::move(candidate);
    return {};
}

UiDiagnostic LauncherUiModel::removePlayer(std::string_view playerId) {
    if (std::any_of(selection_.bindings.begin(), selection_.bindings.end(),
                    [&](const auto& binding) { return binding.playerId == playerId; })) {
        return fail(UiResult::PlayerInUse,
                    "clear the Player from both Seats before removing it");
    }
    auto candidate = players_;
    const auto before = candidate.players.size();
    std::erase_if(candidate.players,
                  [&](const auto& player) { return player.playerId == playerId; });
    if (candidate.players.size() == before) {
        return fail(UiResult::InvalidPlayer, "Player was not found");
    }
    std::erase_if(playerPresentation_, [&](const auto& value) {
        return value.playerId == playerId;
    });
    players_ = std::move(candidate);
    return {};
}

UiDiagnostic LauncherUiModel::setPlayerAccount(
    std::string_view playerId,
    std::string providerId,
    std::optional<std::string> accountReference) {
    if (!validIdentifier(providerId) ||
        (accountReference && !validIdentifier(*accountReference))) {
        return fail(UiResult::InvalidPlayer,
                    "provider account reference is invalid");
    }
    auto candidate = players_;
    auto* player = findPtr(candidate.players, [&](const auto& value) {
        return value.playerId == playerId;
    });
    if (player == nullptr) return fail(UiResult::InvalidPlayer, "Player was not found");
    std::erase_if(player->providerAccounts, [&](const auto& account) {
        return account.providerId == providerId;
    });
    if (accountReference) {
        player->providerAccounts.push_back(
            {std::move(providerId), std::move(*accountReference)});
    }
    const auto diagnostic = profile::validatePlayerProfileDocument(candidate);
    if (!diagnostic.succeeded()) return fail(UiResult::InvalidPlayer, diagnostic.message);
    players_ = std::move(candidate);
    return {};
}

UiDiagnostic LauncherUiModel::normalizeSelection(
    profile::RuntimeSessionSelection& selection) const {
    std::sort(selection.bindings.begin(), selection.bindings.end(),
              [](const auto& left, const auto& right) {
                  return left.seatId < right.seatId;
              });
    if (selection.bindings.size() != 2u ||
        selection.bindings[0].gameId != selection.bindings[1].gameId) {
        for (auto& binding : selection.bindings) {
            binding.setupId.reset();
            binding.instanceIndex = 0u;
        }
        return {};
    }

    const auto& gameId = selection.bindings[0].gameId;
    std::vector<const profile::TwoPlayerSetup*> matches;
    for (const auto& value : setups_.setups) {
        if (value.gameId != gameId) continue;
        const auto* game = findPtr(library_.entries, [&](const auto& entry) {
            return entry.game.gameId == gameId;
        });
        if (game != nullptr && setup::validateSetup(value, game->game).succeeded()) {
            matches.push_back(&value);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto* left, const auto* right) {
        return left->setupId < right->setupId;
    });
    if (matches.empty()) {
        selection.bindings[0].setupId.reset();
        selection.bindings[1].setupId.reset();
        selection.bindings[0].instanceIndex = 0u;
        selection.bindings[1].instanceIndex = 1u;
        return fail(UiResult::MissingSetup,
                    "this game needs a two-player setup before Play");
    }
    for (std::size_t index = 0; index < 2u; ++index) {
        selection.bindings[index].setupId = matches.front()->setupId;
        selection.bindings[index].instanceIndex = static_cast<std::uint32_t>(index);
    }
    return {};
}

UiDiagnostic LauncherUiModel::selectGame(SeatId seatId,
                                          std::string playerId,
                                          std::string gameId) {
    const auto* seat = findPtr(seats_.seats, [&](const auto& value) {
        return value.seatId == seatId;
    });
    if (seat == nullptr) return fail(UiResult::MissingSeat, "Seat was not found");
    if (!seat->active) return fail(UiResult::InactiveSeat, "Seat is not active");
    if (findPtr(players_.players, [&](const auto& value) {
            return value.playerId == playerId;
        }) == nullptr) {
        return fail(UiResult::InvalidPlayer, "Player was not found");
    }
    if (findPtr(library_.entries, [&](const auto& value) {
            return value.game.gameId == gameId;
        }) == nullptr) {
        return fail(UiResult::MissingGame, "Game was not found in the local library");
    }

    auto candidate = selection_;
    auto* binding = findPtr(candidate.bindings, [&](const auto& value) {
        return value.seatId == seatId;
    });
    if (binding == nullptr) {
        candidate.bindings.push_back(
            {seatId, std::move(playerId), std::move(gameId), std::nullopt, 0u});
    } else {
        binding->playerId = std::move(playerId);
        binding->gameId = std::move(gameId);
    }
    const auto diagnostic = normalizeSelection(candidate);
    selection_ = std::move(candidate);
    return diagnostic;
}

UiDiagnostic LauncherUiModel::selectBoth(std::string gameId,
                                          std::string firstPlayerId,
                                          std::string secondPlayerId) {
    std::vector<SeatId> active;
    for (const auto& seat : seats_.seats) if (seat.active) active.push_back(seat.seatId);
    std::sort(active.begin(), active.end());
    if (active.size() != 2u) {
        return fail(UiResult::MissingSeat,
                    "Both requires exactly two active Seats");
    }
    const auto previous = selection_;
    auto diagnostic = selectGame(active[0], std::move(firstPlayerId), gameId);
    if (!diagnostic.succeeded() && diagnostic.result != UiResult::MissingSetup) {
        selection_ = previous;
        return diagnostic;
    }
    diagnostic = selectGame(active[1], std::move(secondPlayerId), std::move(gameId));
    if (!diagnostic.succeeded() && diagnostic.result != UiResult::MissingSetup) {
        selection_ = previous;
    }
    return diagnostic;
}

UiDiagnostic LauncherUiModel::clearSeat(SeatId seatId) {
    const auto before = selection_.bindings.size();
    std::erase_if(selection_.bindings,
                  [&](const auto& binding) { return binding.seatId == seatId; });
    if (selection_.bindings.size() == before) {
        return fail(UiResult::MissingSeat, "Seat has no current game selection");
    }
    auto ignored = normalizeSelection(selection_);
    (void)ignored;
    return {};
}

UiDiagnostic LauncherUiModel::chooseSetup(std::string_view setupId) {
    if (selection_.bindings.size() != 2u ||
        selection_.bindings[0].gameId != selection_.bindings[1].gameId) {
        return fail(UiResult::InvalidSelection,
                    "a setup applies only when both Seats select the same game");
    }
    const auto* selected = findPtr(setups_.setups, [&](const auto& value) {
        return value.setupId == setupId &&
               value.gameId == selection_.bindings[0].gameId;
    });
    if (selected == nullptr) return fail(UiResult::MissingSetup, "setup was not found");
    const auto* game = findPtr(library_.entries, [&](const auto& value) {
        return value.game.gameId == selected->gameId;
    });
    if (game == nullptr) return fail(UiResult::MissingGame, "setup game was not found");
    const auto validation = setup::validateSetup(*selected, game->game);
    if (!validation.succeeded()) return fail(UiResult::InvalidSetup, validation.message);
    std::sort(selection_.bindings.begin(), selection_.bindings.end(),
              [](const auto& left, const auto& right) { return left.seatId < right.seatId; });
    for (std::size_t index = 0; index < 2u; ++index) {
        selection_.bindings[index].setupId = selected->setupId;
        selection_.bindings[index].instanceIndex = static_cast<std::uint32_t>(index);
    }
    return {};
}

UiDiagnostic LauncherUiModel::createSetup(
    const setup::GenerateSetupInput& input,
    std::vector<setup::MutationIntent>& mutations) {
    setup::GeneratedSetupCandidate generated;
    const auto diagnostic = setup::generateCandidate(input, generated);
    if (!diagnostic.succeeded()) return fail(UiResult::InvalidSetup, diagnostic.message);
    auto candidate = setups_;
    candidate.setups.push_back(generated.setup);
    const auto schema = profile::validateTwoPlayerSetupDocument(candidate);
    if (!schema.succeeded()) return fail(UiResult::InvalidSetup, schema.message);
    setups_ = std::move(candidate);
    mutations = std::move(generated.intendedMutations);
    if (selection_.bindings.size() == 2u &&
        selection_.bindings[0].gameId == generated.setup.gameId &&
        selection_.bindings[1].gameId == generated.setup.gameId) {
        return chooseSetup(generated.setup.setupId);
    }
    return {};
}

PlayPreview LauncherUiModel::preview(
    std::span<const preflight::PlannedMutation> mutations) const {
    PlayPreview result;
    result.compileResult = plan::compileProviderAwareLaunchPlan(
        seats_, players_, gameDocument(), setups_, selection_, providers_, requirements_);
    result.summary = preflight::buildSummary(result.compileResult, mutations);
    return result;
}

UiDiagnostic LauncherUiModel::recordActivatedPlan(
    const plan::ProviderAwareLaunchPlan& plan) {
    const auto current = preview();
    if (!current.summary.canActivate || !current.compileResult.plan ||
        *current.compileResult.plan != plan) {
        return fail(UiResult::InvalidSelection,
                    "activated plan does not match the current validated Play preview");
    }
    auto candidate = playerPresentation_;
    for (const auto& seat : plan.seats) {
        auto* presentation = findPtr(candidate, [&](const auto& value) {
            return value.playerId == seat.playerId;
        });
        if (presentation == nullptr) {
            return fail(UiResult::InvalidPlayer,
                        "activated plan references an unknown Player");
        }
        std::erase(presentation->recentGameIds, seat.gameId);
        presentation->recentGameIds.insert(presentation->recentGameIds.begin(), seat.gameId);
        if (presentation->recentGameIds.size() > kMaximumRecentGames) {
            presentation->recentGameIds.resize(kMaximumRecentGames);
        }
        presentation->preferredSeat = seat.seatId;
    }
    playerPresentation_ = std::move(candidate);
    return {};
}

UiDiagnostic LauncherUiModel::recordActivatedSeat(
    const plan::ProviderAwareLaunchPlan& plan, SeatId seatId) {
    const auto current = preview();
    if (!current.summary.canActivate || !current.compileResult.plan ||
        *current.compileResult.plan != plan) {
        return fail(UiResult::InvalidSelection,
                    "activated plan does not match the current validated Play preview");
    }
    const auto* activated = findPtr(plan.seats, [&](const auto& seat) {
        return seat.seatId == seatId;
    });
    if (activated == nullptr) {
        return fail(UiResult::MissingSeat,
                    "activated plan does not contain the requested Seat");
    }
    auto candidate = playerPresentation_;
    auto* presentation = findPtr(candidate, [&](const auto& value) {
        return value.playerId == activated->playerId;
    });
    if (presentation == nullptr) {
        return fail(UiResult::InvalidPlayer,
                    "activated plan references an unknown Player");
    }
    std::erase(presentation->recentGameIds, activated->gameId);
    presentation->recentGameIds.insert(
        presentation->recentGameIds.begin(), activated->gameId);
    if (presentation->recentGameIds.size() > kMaximumRecentGames) {
        presentation->recentGameIds.resize(kMaximumRecentGames);
    }
    presentation->preferredSeat = activated->seatId;
    playerPresentation_ = std::move(candidate);
    return {};
}

std::string_view uiResultName(UiResult result) noexcept {
    switch (result) {
    case UiResult::Success: return "Success";
    case UiResult::InvalidSeatDocument: return "InvalidSeatDocument";
    case UiResult::InvalidLibrary: return "InvalidLibrary";
    case UiResult::InvalidSetupDocument: return "InvalidSetupDocument";
    case UiResult::InvalidPlayer: return "InvalidPlayer";
    case UiResult::DuplicatePlayer: return "DuplicatePlayer";
    case UiResult::PlayerInUse: return "PlayerInUse";
    case UiResult::MissingSeat: return "MissingSeat";
    case UiResult::InactiveSeat: return "InactiveSeat";
    case UiResult::MissingGame: return "MissingGame";
    case UiResult::MissingSetup: return "MissingSetup";
    case UiResult::InvalidSetup: return "InvalidSetup";
    case UiResult::InvalidProvider: return "InvalidProvider";
    case UiResult::InvalidRequirement: return "InvalidRequirement";
    case UiResult::InvalidSelection: return "InvalidSelection";
    }
    return "Unknown";
}

} // namespace hydra::launcher_ui
