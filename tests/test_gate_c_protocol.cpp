#include "hydra/gate_c_protocol.hpp"
#include "hydra/gate_c_transport.hpp"
#include "hydra/virtual_input_state.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

hydra::gatec::DecodedFrame decodeOrFail(
    const std::vector<std::byte>& bytes, std::string_view message) {
    const auto decoded = hydra::gatec::decodeFrame(bytes);
    check(static_cast<bool>(decoded), message);
    return *decoded.frame;
}

void testProtocolRoundTrips() {
    using namespace hydra::gatec;

    const auto rawCapabilities = testCapabilityBits(
        kControlledRawInputProbeCapabilities);
    check((rawCapabilities & testCapabilityBits(
               TestCapability::RawInputApiShim)) != 0 &&
              (rawCapabilities & testCapabilityBits(
               TestCapability::PollingApiShim)) != 0 &&
              (rawCapabilities & testCapabilityBits(
               TestCapability::CursorFocusApiShim)) == 0,
          "Raw Input probe capability is explicit and preserves polling without enabling cursor/focus");

    HelloMessage hello;
    for (std::size_t index = 0; index < hello.token.size(); ++index) {
        hello.token[index] = static_cast<std::uint8_t>(index * 7 + 3);
    }
    hello.seatId = 2;
    hello.processId = 12345;
    hello.architectureBits = 64;
    hello.targetWindow = 0x123456789abcdef0ull;
    const auto helloFrame = decodeOrFail(
        encodeHello(11, hello), "Hello frame decodes");
    HelloMessage decodedHello;
    std::string error;
    check(decodeHello(helloFrame, decodedHello, &error) &&
              decodedHello == hello && helloFrame.sequence == 11,
          "Hello payload round-trips exactly");

    HelloAckMessage ack;
    ack.accepted = true;
    ack.serverProcessId = 9876;
    ack.grantedCapabilities = 0x445566778899aabbull;
    ack.errorCode = 0;
    const auto ackFrame = decodeOrFail(
        encodeHelloAck(12, ack), "HelloAck frame decodes");
    HelloAckMessage decodedAck;
    check(decodeHelloAck(ackFrame, decodedAck, &error) &&
              decodedAck == ack,
          "HelloAck payload round-trips exactly");

    InputEventMessage input;
    input.kind = InputKind::Keyboard;
    input.keyTransition = KeyTransition::Down;
    input.timestampMicros = 55555;
    input.vkey = 0x41;
    input.scanCode = 0x1e;
    input.keyboardFlags = 2;
    const auto inputFrame = decodeOrFail(
        encodeInputEvent(13, input), "Input frame decodes");
    InputEventMessage decodedInput;
    check(decodeInputEvent(inputFrame, decodedInput, &error) &&
              decodedInput == input,
          "Input payload round-trips exactly");

    ControlStateMessage control;
    control.cursorX = 900;
    control.cursorY = -12;
    control.clipEnabled = true;
    control.virtualForeground = true;
    control.virtualCapture = true;
    control.clipLeft = 10;
    control.clipTop = 20;
    control.clipRight = 800;
    control.clipBottom = 600;
    const auto controlFrame = decodeOrFail(
        encodeControlState(14, control), "Control frame decodes");
    ControlStateMessage decodedControl;
    check(decodeControlState(controlFrame, decodedControl, &error) &&
              decodedControl == control,
          "Control payload round-trips exactly");

    StateSnapshotMessage snapshot;
    snapshot.lastAppliedSequence = 91;
    snapshot.keyDownBits[8] = 0x02;
    snapshot.keyPressedEdgeBits[8] = 0x02;
    snapshot.mouseButtonsDown = 5;
    snapshot.wheelAccumulator = -240;
    snapshot.probeVkey = 0x41;
    snapshot.asyncKeyStateValue = 0x8001u;
    snapshot.keyboardStateByte = 0x80u;
    snapshot.cursorX = 123;
    snapshot.cursorY = 456;
    snapshot.clipEnabled = true;
    snapshot.virtualForeground = true;
    snapshot.virtualCapture = false;
    snapshot.clipLeft = 0;
    snapshot.clipTop = 0;
    snapshot.clipRight = 1920;
    snapshot.clipBottom = 1080;
    const auto snapshotFrame = decodeOrFail(
        encodeStateSnapshot(15, snapshot), "Snapshot frame decodes");
    StateSnapshotMessage decodedSnapshot;
    check(decodeStateSnapshot(snapshotFrame, decodedSnapshot, &error) &&
              decodedSnapshot == snapshot,
          "Snapshot payload round-trips exactly");

    QuerySnapshotMessage query;
    query.probeVkey = 0x41;
    QuerySnapshotMessage decodedQuery;
    check(decodeQuerySnapshot(
              decodeOrFail(encodeQuerySnapshot(16, query),
                           "QuerySnapshot frame decodes"),
              decodedQuery, &error) && decodedQuery == query,
          "QuerySnapshot probe payload round-trips exactly");
    check(decodeShutdown(
              decodeOrFail(encodeShutdown(17), "Shutdown frame decodes"),
              &error),
          "Shutdown requires an empty payload");

    ErrorMessage protocolError{77};
    const auto errorFrame = decodeOrFail(
        encodeError(18, protocolError), "Error frame decodes");
    ErrorMessage decodedProtocolError;
    check(decodeError(errorFrame, decodedProtocolError, &error) &&
              decodedProtocolError == protocolError,
          "Error payload round-trips exactly");
}

void testProtocolRejectsMalformedFrames() {
    using namespace hydra::gatec;

    auto bytes = encodeQuerySnapshot(1);
    bytes[0] = std::byte{0};
    check(!decodeFrame(bytes), "wrong protocol magic is rejected");

    bytes = encodeQuerySnapshot(1);
    bytes[4] = std::byte{2};
    check(!decodeFrame(bytes), "unsupported protocol version is rejected");

    bytes = encodeQuerySnapshot(1);
    bytes.push_back(std::byte{0});
    check(!decodeFrame(bytes), "frame size mismatch is rejected");

    bytes = encodeHello(2, HelloMessage{});
    auto frame = decodeOrFail(bytes, "baseline Hello decodes");
    HelloMessage message;
    std::string error;
    check(!decodeHello(frame, message, &error),
          "invalid Hello architecture is rejected");

    InputEventMessage input;
    input.kind = InputKind::Keyboard;
    input.keyTransition = KeyTransition::Down;
    input.vkey = 300;
    frame = decodeOrFail(encodeInputEvent(3, input),
                         "out-of-range key frame decodes structurally");
    check(!decodeInputEvent(frame, input, &error),
          "out-of-range virtual key is rejected semantically");

    input = {};
    input.kind = InputKind::Keyboard;
    input.keyTransition = KeyTransition::None;
    input.vkey = 0x41;
    frame = decodeOrFail(encodeInputEvent(31, input),
                         "transitionless key frame decodes structurally");
    check(!decodeInputEvent(frame, input, &error),
          "keyboard input without a down/up transition is rejected");

    input = {};
    input.kind = InputKind::Mouse;
    input.keyTransition = KeyTransition::Down;
    frame = decodeOrFail(encodeInputEvent(32, input),
                         "mouse key-transition frame decodes structurally");
    check(!decodeInputEvent(frame, input, &error),
          "mouse input carrying a key transition is rejected");

    QuerySnapshotMessage invalidQuery;
    invalidQuery.probeVkey = 300;
    frame = decodeOrFail(encodeQuerySnapshot(33, invalidQuery),
                         "invalid snapshot query decodes structurally");
    check(!decodeQuerySnapshot(frame, invalidQuery, &error),
          "out-of-range snapshot probe is rejected");

    StateSnapshotMessage invalidSnapshot;
    invalidSnapshot.probeVkey = 300;
    frame = decodeOrFail(encodeStateSnapshot(34, invalidSnapshot),
                         "invalid snapshot response decodes structurally");
    check(!decodeStateSnapshot(frame, invalidSnapshot, &error),
          "out-of-range snapshot response probe is rejected");

    ControlStateMessage control;
    control.clipEnabled = true;
    control.clipLeft = 10;
    control.clipRight = 10;
    control.clipTop = 0;
    control.clipBottom = 100;
    frame = decodeOrFail(encodeControlState(4, control),
                         "invalid clip frame decodes structurally");
    check(!decodeControlState(frame, control, &error),
          "invalid clip rectangle is rejected");

    const auto token = tokenFromHex("00112233445566778899aabbccddeeff");
    check(token.has_value() &&
              tokenToHex(*token) == "00112233445566778899aabbccddeeff",
          "session token hex conversion round-trips");
    check(!tokenFromHex("0011"), "short session token is rejected");
    check(!tokenFromHex("00112233445566778899aabbccddeefg"),
          "non-hex session token is rejected");
}

void testSessionTokenGeneration() {
    const auto token = hydra::gatec::generateSessionToken();
    check(token.has_value(), "session token generation succeeds");
    const bool allZero = std::all_of(
        token->begin(), token->end(),
        [](std::uint8_t byte) { return byte == 0; });
    check(!allZero, "session token is not the all-zero sentinel");
    const auto encoded = hydra::gatec::tokenToHex(*token);
    check(encoded.size() == 32 &&
              hydra::gatec::tokenFromHex(encoded) == token,
          "generated session token round-trips through hexadecimal form");
    const auto pipeName = hydra::gatec::makeGateCPipeName(1234, 2, *token);
    check(pipeName.find(L"HydraSeat.GateC.1234.2.") != std::wstring::npos,
          "Gate C pipe name contains the host and Seat identity");
}

void testVirtualInputState() {
    using namespace hydra::gatec;

    VirtualInputState state;
    ControlStateMessage control;
    control.cursorX = 50;
    control.cursorY = 60;
    control.clipEnabled = true;
    control.virtualForeground = true;
    control.virtualCapture = true;
    control.clipLeft = 0;
    control.clipTop = 0;
    control.clipRight = 100;
    control.clipBottom = 100;
    check(state.applyControl(1, control),
          "control state applies with a new sequence");
    check(state.cursorX() == 50 && state.cursorY() == 60 &&
              state.virtualForeground() && state.virtualCapture(),
          "control state creates a process-local cursor/focus view");

    InputEventMessage keyDown;
    keyDown.kind = InputKind::Keyboard;
    keyDown.keyTransition = KeyTransition::Down;
    keyDown.vkey = 0x41;
    check(state.applyInput(2, keyDown), "key-down input applies");
    check(state.keyDown(0x41), "key-down state is visible");
    const auto keyboardState = state.keyboardState();
    check(keyboardState[0x41] == 0x80,
          "GetKeyboardState-style high bit is generated");
    check(state.consumeAsyncKeyState(0x41) == 0x8001u,
          "first async-state query returns down and edge bits");
    check(state.consumeAsyncKeyState(0x41) == 0x8000u,
          "async edge bit is consumed exactly once");

    check(!state.applyInput(2, keyDown),
          "duplicate/stale sequence is rejected");

    InputEventMessage mouse;
    mouse.kind = InputKind::Mouse;
    mouse.deltaX = 80;
    mouse.deltaY = 80;
    mouse.mouseButtonFlags = kMouseLeftDown;
    mouse.wheelDelta = 120;
    check(state.applyInput(3, mouse), "mouse input applies");
    check(state.cursorX() == 99 && state.cursorY() == 99,
          "virtual cursor is clipped without moving the global cursor");
    check((state.mouseButtonsDown() & 1u) != 0 &&
              state.wheelAccumulator() == 120,
          "mouse button and wheel state are process-local");

    InputEventMessage keyUp = keyDown;
    keyUp.keyTransition = KeyTransition::Up;
    check(state.applyInput(4, keyUp), "key-up input applies");
    check(!state.keyDown(0x41) &&
              state.consumeAsyncKeyState(0x41) == 0,
          "key-up clears down state without inventing a press edge");

    const auto snapshot = state.snapshot();
    check(snapshot.lastAppliedSequence == 4 &&
              !snapshotKeyDown(snapshot, 0x41) &&
              snapshot.cursorX == 99 && snapshot.cursorY == 99 &&
              snapshot.virtualForeground && snapshot.virtualCapture,
          "snapshot exposes the complete virtual process state");

    state.reset();
    check(state.lastAppliedSequence() == 0 &&
              state.mouseButtonsDown() == 0 &&
              !state.virtualForeground(),
          "reset restores a safe empty process-local state");
}

} // namespace

int main() {
    testProtocolRoundTrips();
    testProtocolRejectsMalformedFrames();
    testSessionTokenGeneration();
    testVirtualInputState();
    std::cout << "Gate C protocol and virtual input-state tests passed.\n";
    return EXIT_SUCCESS;
}
