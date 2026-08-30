#include "hydra/production_launch_installer.hpp"

#include <algorithm>
#include <limits>

namespace hydra::production {
namespace {

const runtime::SeatGameState* findSeatGame(
    const runtime::HostRuntimeSnapshot& snapshot, SeatId seatId) noexcept {
    const auto found = std::find_if(snapshot.seatGames.begin(),
                                    snapshot.seatGames.end(),
                                    [seatId](const runtime::SeatGameState& state) {
                                        return state.seatId == seatId;
                                    });
    return found == snapshot.seatGames.end() ? nullptr : &*found;
}

const plan::SeatProviderLaunchPlan* findPlanSeat(
    const plan::ProviderAwareLaunchPlan& plan, SeatId seatId) noexcept {
    const auto found = std::find_if(plan.seats.begin(), plan.seats.end(),
                                    [seatId](const auto& seat) {
                                        return seat.seatId == seatId;
                                    });
    return found == plan.seats.end() ? nullptr : &*found;
}

std::uint64_t singleSeatDigest(const plan::SeatProviderLaunchPlan& seat) noexcept {
    plan::ProviderAwareLaunchPlan single;
    single.schemaVersion = plan::kProviderLaunchPlanSchemaVersion;
    single.seats = {seat};
    return providerPlanFingerprint(single);
}

void setProtocolError(std::string& error,
                      const std::optional<hostipc::ErrorPayload>& protocolError,
                      std::string fallback) {
    if (protocolError && !protocolError->diagnostic.empty()) {
        error = protocolError->diagnostic;
    } else {
        error = std::move(fallback);
    }
}

} // namespace

bool HostProviderPlanInstaller::install(
    SeatId seatId,
    const plan::ProviderAwareLaunchPlan& fullPlan,
    const plan::SeatProviderLaunchPlan& seatPlan,
    std::string& error) {
    error.clear();
    if (seatId == 0 || seatPlan.seatId != seatId ||
        fullPlan.schemaVersion != plan::kProviderLaunchPlanSchemaVersion ||
        fullPlan.fingerprint == 0 ||
        providerPlanFingerprint(fullPlan) != fullPlan.fingerprint) {
        error = "provider plan installer received a malformed immutable plan";
        return false;
    }
    const auto* selected = findPlanSeat(fullPlan, seatId);
    if (selected == nullptr || singleSeatDigest(*selected) != singleSeatDigest(seatPlan)) {
        error = "Seat plan does not match the exact Seat entry in the immutable full plan";
        return false;
    }

    std::optional<hostipc::ErrorPayload> protocolError;
    auto snapshot = client_.getSnapshot(hostipc::kDefaultHostIpcTimeoutMs, &error);
    if (!snapshot) return false;
    if (snapshot->sessionPhase != runtime::SeatSessionPhase::Active &&
        snapshot->sessionPhase != runtime::SeatSessionPhase::Degraded) {
        error = "provider plan installation requires an active host session";
        return false;
    }
    if (snapshot->sessionId.empty() || snapshot->generation == 0 ||
        !snapshot->profileLoaded) {
        error = "host snapshot has no authoritative profile/session identity";
        return false;
    }
    const auto profileFingerprint = runtime::runtimeProfileFingerprint(
        snapshot->configuredSeats, snapshot->managementSeatId);
    const auto* game = findSeatGame(*snapshot, seatId);
    if (game == nullptr || game->phase != runtime::SeatGamePhase::Idle ||
        game->binding || game->generation == std::numeric_limits<std::uint64_t>::max()) {
        error = "provider plan may be installed only for an authoritative Idle Seat";
        return false;
    }

    auto registry = client_.providerPlanRegistry(
        hostipc::kDefaultHostIpcTimeoutMs, &error, &protocolError);
    if (!registry) {
        setProtocolError(error, protocolError,
                         "host provider-plan registry is unavailable");
        return false;
    }
    if (registry->profileFingerprint != profileFingerprint ||
        registry->sessionId != snapshot->sessionId ||
        registry->sessionGeneration != snapshot->generation) {
        error = "host registry changed relative to the authoritative runtime snapshot";
        return false;
    }

    ProviderPlanInstallRequest request;
    request.seatId = seatId;
    request.expectedRegistryRevision = registry->registryRevision;
    request.planFingerprint = fullPlan.fingerprint;
    request.planRevision = providerPlanRevision(*selected);
    request.profileFingerprint = profileFingerprint;
    request.sessionId = snapshot->sessionId;
    request.sessionGeneration = snapshot->generation;
    request.seatGameGeneration = game->generation + 1u;
    request.plan = fullPlan;

    protocolError.reset();
    const auto installed = client_.installProviderPlan(
        request, hostipc::kDefaultHostIpcTimeoutMs, &error, &protocolError);
    if (!installed) {
        setProtocolError(error, protocolError,
                         "provider plan install transaction failed");
        return false;
    }
    if (!installed->succeeded()) {
        error = installed->diagnostic.empty()
            ? std::string(providerPlanInstallCodeName(installed->code))
            : installed->diagnostic;
        return false;
    }
    return true;
}

bool HostProviderPlanInstaller::rollback(SeatId seatId,
                                         std::string& error) noexcept {
    try {
        error.clear();
        if (seatId == 0) {
            error = "provider plan rollback requires a nonzero Seat";
            return false;
        }
        std::optional<hostipc::ErrorPayload> protocolError;
        const auto snapshot = client_.getSnapshot(
            hostipc::kDefaultHostIpcTimeoutMs, &error);
        if (!snapshot) return false;
        const auto* game = findSeatGame(*snapshot, seatId);
        if (game == nullptr || game->phase != runtime::SeatGamePhase::Idle || game->binding) {
            error = "provider plan rollback requires the target Seat to be Idle";
            return false;
        }
        const auto profileFingerprint = runtime::runtimeProfileFingerprint(
            snapshot->configuredSeats, snapshot->managementSeatId);
        auto registry = client_.providerPlanRegistry(
            hostipc::kDefaultHostIpcTimeoutMs, &error, &protocolError);
        if (!registry) {
            setProtocolError(error, protocolError,
                             "host provider-plan registry is unavailable during rollback");
            return false;
        }
        if (registry->profileFingerprint != profileFingerprint ||
            registry->sessionId != snapshot->sessionId ||
            registry->sessionGeneration != snapshot->generation) {
            error = "host registry changed before provider plan rollback";
            return false;
        }
        const auto found = std::find_if(
            registry->entries.begin(), registry->entries.end(),
            [seatId](const ProviderPlanRegistryEntry& entry) {
                return entry.seatId == seatId;
            });
        if (found == registry->entries.end()) return true;

        ProviderPlanRemoveRequest request;
        request.seatId = seatId;
        request.expectedRegistryRevision = registry->registryRevision;
        request.planFingerprint = found->planFingerprint;
        request.planRevision = found->planRevision;
        request.profileFingerprint = registry->profileFingerprint;
        request.sessionId = registry->sessionId;
        request.sessionGeneration = registry->sessionGeneration;
        request.seatGameGeneration = found->seatGameGeneration;
        protocolError.reset();
        const auto removed = client_.removeProviderPlan(
            request, hostipc::kDefaultHostIpcTimeoutMs, &error, &protocolError);
        if (!removed) {
            setProtocolError(error, protocolError,
                             "provider plan rollback transaction failed");
            return false;
        }
        if (!removed->succeeded()) {
            error = removed->diagnostic.empty()
                ? std::string(providerPlanInstallCodeName(removed->code))
                : removed->diagnostic;
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = std::string("provider plan rollback exception: ") + exception.what();
        return false;
    } catch (...) {
        error = "provider plan rollback failed with an unknown exception";
        return false;
    }
}

} // namespace hydra::production
