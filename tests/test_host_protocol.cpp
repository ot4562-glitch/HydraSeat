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
    snapshot.profileLoaded = true;
    snapshot.mutationInProgress = false;
    snapshot.seats = {
        {1, SeatSessionPhase::Active, "seat-one"},
        {2, SeatSessionPhase::Active, "seat-two"},
    };
    RuntimeTransition transition;
    transition.sequence = 9;
    transition.correlationId = 44;
    transition.command = RuntimeCommand::Start;
    transition.from = SeatSessionPhase::Prepared;
    transition.to = SeatSessionPhase::Active;
    transition.result = RuntimeResultCode::Ok;
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
    future[4] = static_cast<std::byte>(2);
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
    const Hello hello{ClientRole::Control};
    const auto decodedHello = decodeHello(encodeHello(hello));
    check(decodedHello && decodedHello->role == ClientRole::Control,
          "hello role round-trips");

    const HelloAck ack{ClientRole::ReadOnly, 1234, 7};
    const auto decodedAck = decodeHelloAck(encodeHelloAck(ack));
    check(decodedAck && decodedAck->role == ClientRole::ReadOnly &&
              decodedAck->serverProcessId == 1234 && decodedAck->windowsSessionId == 7,
          "hello acknowledgement round-trips");

    const auto snapshot = sampleSnapshot();
    const auto decodedSnapshot = decodeSnapshot(encodeSnapshot(snapshot));
    check(decodedSnapshot && decodedSnapshot->hostPhase == snapshot.hostPhase &&
              decodedSnapshot->sessionPhase == snapshot.sessionPhase &&
              decodedSnapshot->sessionId == snapshot.sessionId &&
              decodedSnapshot->generation == snapshot.generation &&
              decodedSnapshot->transitionSequence == snapshot.transitionSequence &&
              decodedSnapshot->seats == snapshot.seats &&
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

} // namespace

int main() {
    testFrameAndVersionValidation();
    testPayloadRoundTripsAndBounds();
    testTransitionRingOverflow();
    if (failures != 0) {
        std::cerr << failures << " host protocol test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Host protocol tests passed.\n";
    return EXIT_SUCCESS;
}
