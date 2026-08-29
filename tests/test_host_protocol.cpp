#include "hydra/host_protocol.hpp"
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
    testSeatGameStateCodec();
    testTransitionRingOverflow();
    if (failures != 0) {
        std::cerr << failures << " host protocol test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Host protocol tests passed.\n";
    return EXIT_SUCCESS;
}
