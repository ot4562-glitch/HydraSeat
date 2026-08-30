#include "hydra/host_protocol.hpp"
#include "hydra/production_launch_runtime.hpp"
#include "hydra/runtime_host.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace hydra::hostipc;
using namespace hydra::runtime;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

HostRuntimeSnapshot sampleSnapshot() {
    HostRuntimeSnapshot snapshot;
    snapshot.hostPhase = HostLifecyclePhase::Running;
    snapshot.sessionPhase = SeatSessionPhase::Active;
    snapshot.sessionId.bytes[0] = 0x12;
    snapshot.sessionId.bytes[15] = 0x34;
    snapshot.generation = 7;
    snapshot.transitionSequence = 9;
    snapshot.connectedControlClients = 2;
    snapshot.managementSeatId = 2;
    snapshot.profileLoaded = true;
    snapshot.mutationInProgress = false;
    snapshot.seats = {
        {1, SeatSessionPhase::Active, "seat-one"},
        {2, SeatSessionPhase::Active, "seat-two"},
    };
    snapshot.seatGames = {
        {1, SeatGamePhase::Playing, SeatGameBinding{"player-a", "game-a"}, 2, "playing"},
        {2, SeatGamePhase::Idle, std::nullopt, 1, "idle"},
    };
    hydra::SeatConfig first;
    first.seatId = 1;
    first.name = L"Seat One";
    hydra::SeatConfig second;
    second.seatId = 2;
    second.name = L"Seat Two";
    snapshot.configuredSeats = {first, second};
    RuntimeTransition transition;
    transition.sequence = 9;
    transition.correlationId = 44;
    transition.command = RuntimeCommand::Start;
    transition.from = SeatSessionPhase::Prepared;
    transition.to = SeatSessionPhase::Active;
    transition.result = RuntimeResultCode::Ok;
    transition.seatId = 2;
    transition.diagnostic = "started";
    snapshot.lastTransition = transition;
    snapshot.diagnostic = "active";
    return snapshot;
}

void testFrameAndVersionValidation() {
    Frame frame;
    frame.type = MessageType::GetSnapshot;
    frame.correlationId = 42;
    const auto encoded = encodeFrame(frame);
    DecodeResult decodedResult;
    const auto decoded = decodeFrame(encoded, &decodedResult);
    check(decoded && decodedResult.ok && decoded->type == MessageType::GetSnapshot &&
              decoded->correlationId == 42 && decoded->payload.empty(),
          "fixed frame round-trips");

    auto future = encoded;
    future[4] = static_cast<std::byte>(kHostProtocolVersion + 1u);
    const auto rejectedFuture = decodeFrame(future, &decodedResult);
    check(!rejectedFuture && decodedResult.error == ErrorCode::VersionMismatch,
          "future protocol version is rejected explicitly");

    auto reserved = encoded;
    reserved[8] = static_cast<std::byte>(1);
    const auto rejectedReserved = decodeFrame(reserved, &decodedResult);
    check(!rejectedReserved && decodedResult.error == ErrorCode::Malformed,
          "nonzero reserved header fields are rejected");

    Frame zeroCorrelation = frame;
    zeroCorrelation.correlationId = 0;
    check(encodeFrame(zeroCorrelation).empty(), "zero correlation cannot be encoded");
}

void testPayloadRoundTripsAndBounds() {
    const Hello hello{ClientRole::Control, 2};
    const auto decodedHello = decodeHello(encodeHello(hello));
    check(decodedHello && decodedHello->role == ClientRole::Control &&
              decodedHello->seatId == 2,
          "hello role and requesting Seat round-trip");
    check(!decodeHello(encodeHello(Hello{ClientRole::Control, 0})),
          "control hello without a Seat identity is rejected");
    const Hello seatHello{ClientRole::SeatControl, 1};
    const auto decodedSeatHello = decodeHello(encodeHello(seatHello));
    check(decodedSeatHello && decodedSeatHello->role == ClientRole::SeatControl &&
              decodedSeatHello->seatId == 1,
          "Seat-scoped control identity round-trips");
    check(!decodeHello(encodeHello(Hello{ClientRole::SeatControl, 0})),
          "Seat-scoped control without a Seat identity is rejected");

    const HelloAck ack{ClientRole::ReadOnly, 0, 2, 1234, 7};
    const auto decodedAck = decodeHelloAck(encodeHelloAck(ack));
    check(decodedAck && decodedAck->role == ClientRole::ReadOnly &&
              decodedAck->seatId == 0 && decodedAck->managementSeatId == 2 &&
              decodedAck->serverProcessId == 1234 && decodedAck->windowsSessionId == 7,
          "hello acknowledgement carries Management Seat authority");

    const auto snapshot = sampleSnapshot();
    const auto decodedSnapshot = decodeSnapshot(encodeSnapshot(snapshot));
    check(decodedSnapshot && decodedSnapshot->hostPhase == snapshot.hostPhase &&
              decodedSnapshot->sessionPhase == snapshot.sessionPhase &&
              decodedSnapshot->sessionId == snapshot.sessionId &&
              decodedSnapshot->generation == snapshot.generation &&
              decodedSnapshot->transitionSequence == snapshot.transitionSequence &&
              decodedSnapshot->managementSeatId == snapshot.managementSeatId &&
              decodedSnapshot->seats == snapshot.seats &&
              decodedSnapshot->seatGames == snapshot.seatGames &&
              decodedSnapshot->wholeMachineReturnRequested ==
                  snapshot.wholeMachineReturnRequested &&
              decodedSnapshot->configuredSeats == snapshot.configuredSeats &&
              decodedSnapshot->lastTransition == snapshot.lastTransition,
          "runtime snapshot round-trips without pointer-size assumptions");

    RuntimeCommandResult command;
    command.code = RuntimeResultCode::RecoveryRequired;
    command.snapshot = snapshot;
    command.diagnostic = std::string(kHostProtocolMaxStringBytes, 'd');
    const auto decodedCommand = decodeCommandResult(encodeCommandResult(command));
    check(decodedCommand && decodedCommand->code == command.code &&
              decodedCommand->snapshot.sessionId == snapshot.sessionId &&
              decodedCommand->diagnostic == command.diagnostic,
          "command result round-trips at maximum diagnostic size");

    auto mismatchedLifecycle = snapshot;
    mismatchedLifecycle.seatGames.pop_back();
    check(encodeSnapshot(mismatchedLifecycle).empty(),
          "snapshot rejects Seat lifecycle identities that do not match active profile Seats");

    ErrorPayload error{ErrorCode::PermissionDenied, "read-only client"};
    const auto decodedError = decodeError(encodeError(error));
    check(decodedError && decodedError->code == error.code &&
              decodedError->diagnostic == error.diagnostic,
          "error payload round-trips");

    SubscribeRequest subscription{12, 64};
    const auto decodedSubscription = decodeSubscribeRequest(
        encodeSubscribeRequest(subscription));
    check(decodedSubscription && decodedSubscription->afterSequence == 12 &&
              decodedSubscription->maxEvents == 64,
          "bounded subscription request round-trips");

    SubscribeRequest invalidSubscription{0, 0};
    check(!decodeSubscribeRequest(encodeSubscribeRequest(invalidSubscription)),
          "zero-sized subscription is rejected");
}

std::vector<hydra::SeatConfig> testSeats() {
    hydra::SeatConfig seat;
    seat.seatId = 1;
    seat.name = L"Seat";
    return {seat};
}

void testProfilePayloadRoundTripAndBounds() {
    hydra::SeatConfig first;
    first.seatId = 1;
    first.name = L"관리 Seat";
    first.displayIds = {L"monitor-stable-a", L"monitor-stable-b"};
    first.primaryDisplayId = L"monitor-stable-a";
    first.keyboardIds = {L"keyboard-stable-a"};
    first.mouseIds = {L"mouse-stable-a"};
    first.controllerIds = {L"xinput:0"};
    first.audioOutputEndpointId = L"audio-out-a";
    first.audioInputEndpointId = L"audio-in-a";
    first.targetHwnd = 0x12345678ull;

    hydra::SeatConfig second;
    second.seatId = 2;
    second.name = L"Seat 2";
    second.displayIds = {L"monitor-stable-c"};
    second.primaryDisplayId = L"monitor-stable-c";
    second.active = true;

    ProfilePayload profile;
    profile.managementSeatId = 2;
    profile.seats = {first, second};
    const auto encoded = encodeProfilePayload(profile);
    const auto decoded = decodeProfilePayload(encoded);
    check(!encoded.empty() && decoded && decoded->managementSeatId == 2 &&
              decoded->seats == profile.seats,
          "bounded profile payload round-trips Unicode and stable resource identities");

    ProfilePayload empty;
    empty.managementSeatId = 1;
    check(encodeProfilePayload(empty).empty(),
          "profile payload requires at least one configured Seat");

    auto oversized = profile;
    oversized.seats.front().keyboardIds = {
        std::wstring(kHostProtocolMaxStringBytes + 1u, L'x')};
    check(encodeProfilePayload(oversized).empty(),
          "profile payload rejects stable IDs larger than the protocol string bound");

    auto truncated = encoded;
    if (!truncated.empty()) truncated.pop_back();
    check(!decodeProfilePayload(truncated),
          "truncated profile payload is rejected instead of partially applying configuration");

    auto malformedUtf8 = encoded;
    // The first Seat name starts after the fixed profile/Seat header. Replace the
    // first two name bytes with an overlong UTF-8 sequence; the strict decoder
    // must reject it even though the byte pattern has continuation shape.
    constexpr std::size_t firstNameOffset = 8u + 4u + 4u + 8u + 4u;
    if (malformedUtf8.size() > firstNameOffset + 1u) {
        malformedUtf8[firstNameOffset] = static_cast<std::byte>(0xc0u);
        malformedUtf8[firstNameOffset + 1u] = static_cast<std::byte>(0x80u);
    }
    check(!decodeProfilePayload(malformedUtf8),
          "overlong UTF-8 in profile payload is rejected");
}

void testTransitionRingOverflow() {
    RuntimeHost host;
    check(host.loadProfile(testSeats(), 1).succeeded(), "event test profile loads");
    for (std::uint64_t correlation = 2; correlation < 145; ++correlation) {
        (void)host.start(correlation); // invalid-state transitions still must be observable.
    }
    const auto snapshot = host.snapshot();
    check(snapshot.transitionSequence >= 140, "transition ring receives command results");

    bool overflow = false;
    const auto tooOld = host.transitionEventsAfter(0, kHostProtocolMaxEvents, overflow);
    check(overflow && tooOld.empty(),
          "subscription older than retained ring requires resnapshot");

    const auto after = snapshot.transitionSequence - 4;
    const auto recent = host.transitionEventsAfter(after, 8, overflow);
    check(!overflow && recent.size() == 4 && recent.front().sequence == after + 1,
          "recent transition subscription returns ordered retained events");

    const auto bounded = host.transitionEventsAfter(after, 2, overflow);
    check(overflow && bounded.size() == 2,
          "subscriber capacity overflow is explicit instead of silently dropping events");
}

class FakeTrustedRequirementSource final : public hydra::requirement::ITrustedRequirementSource {
public:
    hydra::requirement::RequirementSnapshotDiagnostic resolveCurrent(
        hydra::requirement::TrustedRequirementSnapshot& output) override {
        ++resolveCount;
        if (!diagnostic.succeeded()) return diagnostic;
        output = snapshot;
        return {};
    }

    hydra::requirement::TrustedRequirementSnapshot snapshot;
    hydra::requirement::RequirementSnapshotDiagnostic diagnostic;
    std::size_t resolveCount{0u};
};

hydra::SeatConfig providerRegistrySeat() {
    hydra::SeatConfig seat;
    seat.seatId = 1u;
    seat.name = L"Provider Seat";
    seat.active = true;
    return seat;
}

hydra::requirement::TrustedRequirementSnapshot trustedRuntimeRequirements() {
    hydra::requirement::TrustedRequirementSnapshot snapshot;
    snapshot.referenceMonth = "2026-08";
    snapshot.staleAfterMonths = 2u;
    snapshot.trust = hydra::requirement::LocalEvidenceTrust::PhysicalOnly;

    hydra::plan::GameRuntimeRequirement requirement;
    requirement.gameId = "game:a";
    requirement.revision = 13u;
    requirement.requirements.display = false;
    requirement.requirements.keyboard = false;
    requirement.requirements.mouse = false;
    requirement.requirements.controller = false;
    requirement.requirements.audioOutput = false;
    requirement.requirements.windowOwnership = false;
    requirement.requirements.recovery = false;
    requirement.capabilities = {};

    hydra::requirement::TrustedGameRuntimeAuthority authority;
    authority.requirement = requirement;
    authority.providerId = "fake";
    authority.providerAppId = "app-100";
    authority.providerMetadataRevision = 41u;
    authority.gameVersionUtf8 = "1.0.7";
    authority.executableSha256 =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    authority.executableCandidates = {L"C:\\Games\\Fixture\\fixture.exe"};
    authority.evidenceResultId = "result-local-a";
    authority.evidenceProvenanceId = "local-runtime-evidence";
    authority.evidenceProvenanceRevision = 7u;
    authority.evidenceTimestampBucket = "2026-08";
    authority.evidenceOrigin = hydra::compat::ResultOrigin::Physical;
    snapshot.authorities = {authority};
    snapshot.requirements = {requirement};
    return snapshot;
}

hydra::plan::ProviderAwareLaunchPlan providerRegistryPlan(const hydra::SeatConfig& seat) {
    hydra::plan::ProviderAwareLaunchPlan plan;
    hydra::plan::SeatProviderLaunchPlan plannedSeat;
    plannedSeat.seatId = seat.seatId;
    plannedSeat.playerId = "player-a";
    plannedSeat.gameId = "game:a";
    plannedSeat.requirementRevision = 13u;
    plannedSeat.hardwareFingerprint = hydra::production::seatHardwareFingerprint(seat);
    plannedSeat.requirements.display = false;
    plannedSeat.requirements.keyboard = false;
    plannedSeat.requirements.mouse = false;
    plannedSeat.requirements.controller = false;
    plannedSeat.requirements.audioOutput = false;
    plannedSeat.requirements.windowOwnership = false;
    plannedSeat.requirements.recovery = false;
    plannedSeat.capabilities = {};
    plannedSeat.launchRequest.providerId = "fake";
    plannedSeat.launchRequest.gameId = "game:a";
    plannedSeat.launchRequest.providerAppId = "app-100";
    plannedSeat.launchRequest.metadataRevision = 41u;
    plannedSeat.launchRequest.targetKind = hydra::provider::LaunchTargetKind::Executable;
    plannedSeat.launchRequest.target = L"C:\\Games\\Fixture\\fixture.exe";
    plannedSeat.launchRequest.launchCorrelationId = "host-authority-test";
    plan.seats = {plannedSeat};
    plan.fingerprint = hydra::production::providerPlanFingerprint(plan);
    return plan;
}

hydra::production::ProviderPlanInstallRequest providerRegistryRequest(
    hydra::production::HostProviderPlanRegistry& registry,
    const std::vector<hydra::SeatConfig>& seats,
    const RuntimeSessionId& sessionId,
    const hydra::plan::ProviderAwareLaunchPlan& plan) {
    hydra::production::ProviderPlanInstallRequest request;
    request.seatId = 1u;
    request.expectedRegistryRevision = registry.registrySnapshot().registryRevision;
    request.planFingerprint = plan.fingerprint;
    request.planRevision = hydra::production::providerPlanRevision(plan.seats.front());
    request.profileFingerprint = hydra::runtime::runtimeProfileFingerprint(seats, 1u);
    request.sessionId = sessionId;
    request.sessionGeneration = 7u;
    request.seatGameGeneration = 1u;
    request.plan = plan;
    return request;
}

void testProviderRegistryRequiresFreshTrustedRequirements() {
    const std::vector<hydra::SeatConfig> seats{providerRegistrySeat()};
    RuntimeSessionId sessionId;
    sessionId.bytes[0] = 0x51u;
    const auto plan = providerRegistryPlan(seats.front());
    const auto profileFingerprint = hydra::runtime::runtimeProfileFingerprint(seats, 1u);

    hydra::production::HostProviderPlanRegistry withoutSource(
        std::shared_ptr<hydra::launch::ISeatActivationResourceFactory>{});
    withoutSource.resetContext(profileFingerprint, sessionId, 7u, seats);
    const auto deniedWithoutSource = withoutSource.install(
        providerRegistryRequest(withoutSource, seats, sessionId, plan));
    check(deniedWithoutSource.code == hydra::production::ProviderPlanInstallCode::InvalidPlan &&
              deniedWithoutSource.registry.entries.empty(),
          "authoritative provider registry denies install when no trusted requirement source exists");

    auto source = std::make_shared<FakeTrustedRequirementSource>();
    source->snapshot = trustedRuntimeRequirements();
    hydra::production::HostProviderPlanRegistry registry(
        std::shared_ptr<hydra::launch::ISeatActivationResourceFactory>{}, source);
    registry.resetContext(profileFingerprint, sessionId, 7u, seats);
    const auto installed = registry.install(providerRegistryRequest(registry, seats, sessionId, plan));
    check(installed.succeeded() && installed.registry.entries.size() == 1u &&
              source->resolveCount == 1u,
          "exact trusted catalog-to-requirement authority permits immutable plan installation");

    source->snapshot.authorities.front().providerMetadataRevision = 42u;
    std::string activationError;
    const auto staleAtActivation = registry.createForBinding(
        1u, SeatGameBinding{"player-a", "game:a"}, 1u, activationError);
    check(!staleAtActivation && source->resolveCount == 2u &&
              activationError.find("trusted requirement validation") != std::string::npos,
          "Seat activation re-resolves authority and rejects provider drift after installation");

    auto staleProviderSource = std::make_shared<FakeTrustedRequirementSource>();
    staleProviderSource->snapshot = trustedRuntimeRequirements();
    hydra::production::HostProviderPlanRegistry staleProviderRegistry(
        std::shared_ptr<hydra::launch::ISeatActivationResourceFactory>{}, staleProviderSource);
    staleProviderRegistry.resetContext(profileFingerprint, sessionId, 7u, seats);
    auto staleProviderPlan = plan;
    ++staleProviderPlan.seats.front().launchRequest.metadataRevision;
    staleProviderPlan.fingerprint = hydra::production::providerPlanFingerprint(staleProviderPlan);
    const auto staleProviderResult = staleProviderRegistry.install(
        providerRegistryRequest(staleProviderRegistry, seats, sessionId, staleProviderPlan));
    check(staleProviderResult.code == hydra::production::ProviderPlanInstallCode::InvalidPlan &&
              staleProviderResult.registry.entries.empty(),
          "host rejects a self-consistent plan whose provider revision differs from trusted authority");

    auto wrongExecutableSource = std::make_shared<FakeTrustedRequirementSource>();
    wrongExecutableSource->snapshot = trustedRuntimeRequirements();
    hydra::production::HostProviderPlanRegistry wrongExecutableRegistry(
        std::shared_ptr<hydra::launch::ISeatActivationResourceFactory>{}, wrongExecutableSource);
    wrongExecutableRegistry.resetContext(profileFingerprint, sessionId, 7u, seats);
    auto wrongExecutablePlan = plan;
    wrongExecutablePlan.seats.front().launchRequest.target = L"C:\\Games\\Other\\other.exe";
    wrongExecutablePlan.fingerprint = hydra::production::providerPlanFingerprint(wrongExecutablePlan);
    const auto wrongExecutableResult = wrongExecutableRegistry.install(
        providerRegistryRequest(wrongExecutableRegistry, seats, sessionId, wrongExecutablePlan));
    check(wrongExecutableResult.code == hydra::production::ProviderPlanInstallCode::InvalidPlan &&
              wrongExecutableResult.registry.entries.empty(),
          "host rejects a recomputed plan hash when executable identity leaves the trusted Game catalog");

    auto tamperedSource = std::make_shared<FakeTrustedRequirementSource>();
    tamperedSource->snapshot = trustedRuntimeRequirements();
    hydra::production::HostProviderPlanRegistry tamperedRegistry(
        std::shared_ptr<hydra::launch::ISeatActivationResourceFactory>{}, tamperedSource);
    tamperedRegistry.resetContext(profileFingerprint, sessionId, 7u, seats);
    auto tamperedPlan = plan;
    ++tamperedPlan.seats.front().requirementRevision;
    tamperedPlan.fingerprint = hydra::production::providerPlanFingerprint(tamperedPlan);
    const auto tamperedResult = tamperedRegistry.install(
        providerRegistryRequest(tamperedRegistry, seats, sessionId, tamperedPlan));
    check(tamperedResult.code == hydra::production::ProviderPlanInstallCode::InvalidPlan &&
              tamperedResult.registry.entries.empty(),
          "host rejects recomputed plan fingerprints when requirement authority was tampered");

    auto wrongGameSource = std::make_shared<FakeTrustedRequirementSource>();
    wrongGameSource->snapshot = trustedRuntimeRequirements();
    hydra::production::HostProviderPlanRegistry wrongGameRegistry(
        std::shared_ptr<hydra::launch::ISeatActivationResourceFactory>{}, wrongGameSource);
    wrongGameRegistry.resetContext(profileFingerprint, sessionId, 7u, seats);
    auto wrongGamePlan = plan;
    wrongGamePlan.seats.front().gameId = "game:b";
    wrongGamePlan.seats.front().launchRequest.gameId = "game:b";
    wrongGamePlan.fingerprint = hydra::production::providerPlanFingerprint(wrongGamePlan);
    const auto wrongGameResult = wrongGameRegistry.install(
        providerRegistryRequest(wrongGameRegistry, seats, sessionId, wrongGamePlan));
    check(wrongGameResult.code == hydra::production::ProviderPlanInstallCode::InvalidPlan &&
              wrongGameResult.registry.entries.empty(),
          "host refuses a different Game identity even when the client recomputes its plan hash");

    auto foreignProfileSource = std::make_shared<FakeTrustedRequirementSource>();
    foreignProfileSource->snapshot = trustedRuntimeRequirements();
    hydra::production::HostProviderPlanRegistry foreignProfileRegistry(
        std::shared_ptr<hydra::launch::ISeatActivationResourceFactory>{}, foreignProfileSource);
    foreignProfileRegistry.resetContext(profileFingerprint, sessionId, 7u, seats);
    auto foreignProfileRequest = providerRegistryRequest(
        foreignProfileRegistry, seats, sessionId, plan);
    ++foreignProfileRequest.profileFingerprint;
    const auto foreignProfileResult = foreignProfileRegistry.install(foreignProfileRequest);
    check(foreignProfileResult.code == hydra::production::ProviderPlanInstallCode::InvalidProfile &&
              foreignProfileResult.registry.entries.empty(),
          "host profile fingerprint remains authoritative after trusted requirement validation");

    auto communitySource = std::make_shared<FakeTrustedRequirementSource>();
    communitySource->snapshot = trustedRuntimeRequirements();
    communitySource->snapshot.authorities.front().evidenceOrigin =
        hydra::compat::ResultOrigin::ImportedCommunity;
    hydra::production::HostProviderPlanRegistry communityRegistry(
        std::shared_ptr<hydra::launch::ISeatActivationResourceFactory>{}, communitySource);
    communityRegistry.resetContext(profileFingerprint, sessionId, 7u, seats);
    const auto communityResult = communityRegistry.install(
        providerRegistryRequest(communityRegistry, seats, sessionId, plan));
    check(communityResult.code == hydra::production::ProviderPlanInstallCode::InvalidPlan &&
              communityResult.registry.entries.empty(),
          "community-only evidence cannot authorize a production registry mutation");

    auto staleEvidenceSource = std::make_shared<FakeTrustedRequirementSource>();
    staleEvidenceSource->snapshot = trustedRuntimeRequirements();
    staleEvidenceSource->snapshot.authorities.front().evidenceTimestampBucket = "2026-01";
    hydra::production::HostProviderPlanRegistry staleEvidenceRegistry(
        std::shared_ptr<hydra::launch::ISeatActivationResourceFactory>{}, staleEvidenceSource);
    staleEvidenceRegistry.resetContext(profileFingerprint, sessionId, 7u, seats);
    const auto staleEvidenceResult = staleEvidenceRegistry.install(
        providerRegistryRequest(staleEvidenceRegistry, seats, sessionId, plan));
    check(staleEvidenceResult.code == hydra::production::ProviderPlanInstallCode::InvalidPlan &&
              staleEvidenceResult.registry.entries.empty(),
          "stale local evidence cannot authorize a production registry mutation");
}

void testRuntimeHostInstallDelegatesToTrustedRegistryAuthority() {
    const std::vector<hydra::SeatConfig> seats{providerRegistrySeat()};
    auto source = std::make_shared<FakeTrustedRequirementSource>();
    source->snapshot = trustedRuntimeRequirements();
    auto registry = std::make_shared<hydra::production::HostProviderPlanRegistry>(
        std::shared_ptr<hydra::launch::ISeatActivationResourceFactory>{}, source);
    RuntimeHost host({}, registry);
    check(host.loadProfile(seats, 1u, 700u).succeeded() &&
              host.plan(701u).succeeded() && host.prepare(702u).succeeded() &&
              host.start(703u).succeeded(),
          "authoritative RuntimeHost reaches Active with the production registry installed");

    const auto runtimeSnapshot = host.snapshot();
    const auto registrySnapshot = host.providerPlanRegistrySnapshot();
    auto plan = providerRegistryPlan(seats.front());
    hydra::production::ProviderPlanInstallRequest request;
    request.seatId = 1u;
    request.expectedRegistryRevision = registrySnapshot.registryRevision;
    request.planFingerprint = plan.fingerprint;
    request.planRevision = hydra::production::providerPlanRevision(plan.seats.front());
    request.profileFingerprint = hydra::runtime::runtimeProfileFingerprint(seats, 1u);
    request.sessionId = runtimeSnapshot.sessionId;
    request.sessionGeneration = runtimeSnapshot.generation;
    request.seatGameGeneration = runtimeSnapshot.seatGames.front().generation + 1u;
    request.plan = plan;

    auto staleSession = request;
    ++staleSession.sessionGeneration;
    const auto staleSessionDenied = host.installProviderPlan(staleSession);
    check(staleSessionDenied.code ==
              hydra::production::ProviderPlanInstallCode::InvalidSession &&
              staleSessionDenied.registry.entries.empty() && source->resolveCount == 0u,
          "RuntimeHost rejects stale Host session generation before trusted plan validation");

    auto staleSeat = request;
    ++staleSeat.seatGameGeneration;
    const auto staleSeatDenied = host.installProviderPlan(staleSeat);
    check(staleSeatDenied.code ==
              hydra::production::ProviderPlanInstallCode::InvalidSeatGeneration &&
              staleSeatDenied.registry.entries.empty() && source->resolveCount == 0u,
          "RuntimeHost rejects stale/future Seat activation generation before registry mutation");

    auto foreignProfile = request;
    ++foreignProfile.profileFingerprint;
    const auto foreignProfileDenied = host.installProviderPlan(foreignProfile);
    check(foreignProfileDenied.code ==
              hydra::production::ProviderPlanInstallCode::InvalidProfile &&
              foreignProfileDenied.registry.entries.empty() && source->resolveCount == 0u,
          "RuntimeHost rejects foreign profile authority before provider-plan mutation");

    const auto installed = host.installProviderPlan(request);
    check(installed.succeeded() && installed.registry.entries.size() == 1u &&
              source->resolveCount == 1u,
          "RuntimeHost authoritative install path consumes the trusted requirement source");
    check(host.stopAndReturnToWindows(704u).succeeded(),
          "RuntimeHost trusted-install fixture returns to safe Idle state");

    auto communitySource = std::make_shared<FakeTrustedRequirementSource>();
    communitySource->snapshot = trustedRuntimeRequirements();
    communitySource->snapshot.authorities.front().evidenceOrigin =
        hydra::compat::ResultOrigin::ImportedCommunity;
    auto communityRegistry = std::make_shared<hydra::production::HostProviderPlanRegistry>(
        std::shared_ptr<hydra::launch::ISeatActivationResourceFactory>{}, communitySource);
    RuntimeHost communityHost({}, communityRegistry);
    check(communityHost.loadProfile(seats, 1u, 710u).succeeded() &&
              communityHost.plan(711u).succeeded() && communityHost.prepare(712u).succeeded() &&
              communityHost.start(713u).succeeded(),
          "community-evidence RuntimeHost fixture reaches pre-install Active state");
    const auto communityRuntime = communityHost.snapshot();
    const auto communityRegistrySnapshot = communityHost.providerPlanRegistrySnapshot();
    request.expectedRegistryRevision = communityRegistrySnapshot.registryRevision;
    request.sessionId = communityRuntime.sessionId;
    request.sessionGeneration = communityRuntime.generation;
    request.seatGameGeneration = communityRuntime.seatGames.front().generation + 1u;
    const auto denied = communityHost.installProviderPlan(request);
    check(denied.code == hydra::production::ProviderPlanInstallCode::InvalidPlan &&
              denied.registry.entries.empty(),
          "RuntimeHost cannot turn community-only evidence into a provider-plan mutation");
    check(communityHost.stopAndReturnToWindows(714u).succeeded(),
          "community-evidence RuntimeHost fixture returns to safe Idle state");
}

void testSeatGameStateCodec() {
    SeatGameState first;
    first.seatId = 1;
    first.phase = SeatGamePhase::Playing;
    first.binding = SeatGameBinding{"player-a", "game-a"};
    first.generation = 7;
    first.diagnostic = "exact process ownership verified";
    SeatGameState second;
    second.seatId = 2;
    second.phase = SeatGamePhase::Idle;
    second.generation = 3;
    second.diagnostic = "Seat-local cleanup verified";

    const std::vector<SeatGameState> states{first, second};
    const auto encoded = encodeSeatGameStates(states);
    const auto decoded = decodeSeatGameStates(encoded);
    check(!encoded.empty() && decoded && *decoded == states,
          "versioned two-Seat lifecycle snapshot round-trips temporary bindings");
    const std::vector<SeatGameState> reversedStates{second, first};
    check(encodeSeatGameStates(reversedStates) == encoded,
          "Seat lifecycle encoding is canonical regardless of caller ordering");

    SeatGameCommandResult result;
    result.code = SeatGameResultCode::Ok;
    result.seats = states;
    result.wholeMachineReturnRequested = false;
    result.diagnostic = "Seat 1 remains active";
    const auto resultBytes = encodeSeatGameCommandResult(result);
    const auto resultDecoded = decodeSeatGameCommandResult(resultBytes);
    check(!resultBytes.empty() && resultDecoded && resultDecoded->code == result.code &&
              resultDecoded->seats == states &&
              !resultDecoded->wholeMachineReturnRequested,
          "Seat command result preserves reconnect state and global return policy");

    auto contradictoryResult = result;
    contradictoryResult.wholeMachineReturnRequested = true;
    check(encodeSeatGameCommandResult(contradictoryResult).empty(),
          "encoder rejects whole-machine return while any Seat is Playing");
    auto contradictoryResultBytes = resultBytes;
    if (contradictoryResultBytes.size() > 1u) {
        contradictoryResultBytes[1] = static_cast<std::byte>(1u);
    }
    check(!decodeSeatGameCommandResult(contradictoryResultBytes),
          "decoder rejects remote whole-machine return while a Seat is Playing");

    SeatGameCommandResult bothIdle;
    bothIdle.code = SeatGameResultCode::Ok;
    bothIdle.seats = {second};
    bothIdle.wholeMachineReturnRequested = true;
    check(decodeSeatGameCommandResult(encodeSeatGameCommandResult(bothIdle)).has_value(),
          "verified all-Idle state may carry the declared return request");

    auto futurePhase = encoded;
    if (futurePhase.size() > 8u) futurePhase[8] = static_cast<std::byte>(0xffu);
    check(!decodeSeatGameStates(futurePhase),
          "future Seat lifecycle phase fails closed");

    auto playingWithoutBinding = encodeSeatGameStates(
        std::vector<SeatGameState>{second});
    check(playingWithoutBinding.size() > 8u,
          "single Idle Seat fixture encodes for malformed-state mutation");
    if (playingWithoutBinding.size() > 8u) {
        playingWithoutBinding[8] = static_cast<std::byte>(SeatGamePhase::Playing);
    }
    check(!decodeSeatGameStates(playingWithoutBinding),
          "Playing without a temporary binding fails closed on decode");

    auto idleWithBinding = encodeSeatGameStates(
        std::vector<SeatGameState>{first});
    check(idleWithBinding.size() > 8u,
          "single Playing Seat fixture encodes for malformed-state mutation");
    if (idleWithBinding.size() > 8u) {
        idleWithBinding[8] = static_cast<std::byte>(SeatGamePhase::Idle);
    }
    check(!decodeSeatGameStates(idleWithBinding),
          "Idle with a stale temporary binding fails closed on decode");

    auto impossiblePlaying = second;
    impossiblePlaying.phase = SeatGamePhase::Playing;
    check(encodeSeatGameStates(std::vector<SeatGameState>{impossiblePlaying}).empty(),
          "encoder refuses Playing state without a binding");
    auto impossibleIdle = first;
    impossibleIdle.phase = SeatGamePhase::Idle;
    check(encodeSeatGameStates(std::vector<SeatGameState>{impossibleIdle}).empty(),
          "encoder refuses Idle state with a stale binding");

    auto duplicate = states;
    duplicate[1].seatId = duplicate[0].seatId;
    check(encodeSeatGameStates(duplicate).empty(),
          "duplicate Seat lifecycle identity is not transportable");

    auto tooMany = states;
    SeatGameState third;
    third.seatId = 3;
    tooMany.push_back(third);
    check(encodeSeatGameStates(tooMany).empty(),
          "protocol refuses a third v1 active Seat before serialization");

    auto truncated = resultBytes;
    if (!truncated.empty()) truncated.pop_back();
    check(!decodeSeatGameCommandResult(truncated),
          "truncated Seat command result cannot become an inferred success");

    SeatGameCommandPayload assign;
    assign.seatId = 2;
    assign.binding = SeatGameBinding{"player-b", "game-b"};
    const auto decodedAssign = decodeSeatGameCommandPayload(
        encodeSeatGameCommandPayload(assign));
    check(decodedAssign && decodedAssign->seatId == 2 &&
              decodedAssign->binding == assign.binding,
          "bounded Seat assignment command round-trips temporary identities");
    SeatGameCommandPayload stop;
    stop.seatId = 1;
    const auto decodedStop = decodeSeatGameCommandPayload(
        encodeSeatGameCommandPayload(stop));
    check(decodedStop && decodedStop->seatId == 1 && !decodedStop->binding,
          "Seat-local start/stop command transports only stable Seat identity");
}

} // namespace

int main() {
    testFrameAndVersionValidation();
    testPayloadRoundTripsAndBounds();
    testProfilePayloadRoundTripAndBounds();
    testProviderRegistryRequiresFreshTrustedRequirements();
    testRuntimeHostInstallDelegatesToTrustedRegistryAuthority();
    testSeatGameStateCodec();
    testTransitionRingOverflow();
    if (failures != 0) {
        std::cerr << failures << " host protocol test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Host protocol tests passed.\n";
    return EXIT_SUCCESS;
}
