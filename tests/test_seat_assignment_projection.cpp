#include "hydra/seat_assignment_projection.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

hydra::WorkspaceManager twoSeats(std::wstring first = L"Seat 1",
                                 std::wstring second = L"Seat 2") {
    hydra::WorkspaceManager manager;
    check(manager.createSeat(first) == 1u, "fixture creates Seat 1");
    check(manager.createSeat(second) == 2u, "fixture creates Seat 2");
    return manager;
}

void testEmptyBaseBuildsExactlyTwoSeats() {
    hydra::WorkspaceManager base;
    hydra::WorkspaceManager output;
    const std::vector<hydra::VisibleSeatDeviceAssignment> visible{
        {hydra::SeatDeviceType::Display, L"Display:A", 1u, true},
        {hydra::SeatDeviceType::Keyboard, L"Keyboard:B", 2u, false},
    };
    std::string error;
    check(hydra::projectVisibleSeatAssignments(base, visible, output, &error),
          "empty base projects successfully");
    check(error.empty(), "successful projection clears the error string");
    const auto seats = output.getAllSeats();
    check(seats.size() == 2u && seats[0].seatId == 1u && seats[1].seatId == 2u,
          "projection creates exactly Seat 1 and Seat 2");
    check(seats[0].name == L"Seat 1" && seats[1].name == L"Seat 2",
          "projection uses Seat terminology for generated names");
    check(output.findDisplayOwner(L"display:a") == 1u,
          "display assignment is case-insensitively addressable");
    check(output.findKeyboardOwner(L"KEYBOARD:B") == 2u,
          "keyboard is projected to Seat 2");
    check(output.getSeat(1u)->primaryDisplayId == std::optional<std::wstring>(L"Display:A"),
          "explicit primary display is retained");
}

void testVisibleMovesPreserveHiddenState() {
    auto base = twoSeats(L"Custom Left", L"Custom Right");
    check(base.setManagementSeatId(2u), "fixture moves Management Seat to Seat 2");
    check(base.assignKeyboard(1u, L"Keyboard:Move"), "fixture assigns visible keyboard");
    check(base.assignController(1u, L"Controller:Disconnected"),
          "fixture assigns disconnected controller");
    check(base.assignAudioOutput(1u, L"Audio:Headset"),
          "fixture assigns nonvisual audio output");
    check(base.assignAudioInput(2u, L"Audio:Mic"),
          "fixture assigns nonvisual audio input");
    check(base.setActive(2u, false), "fixture preserves inactive Seat state");

    hydra::WorkspaceManager output;
    const std::vector<hydra::VisibleSeatDeviceAssignment> visible{
        {hydra::SeatDeviceType::Keyboard, L"keyboard:move", 2u, false},
    };
    check(hydra::projectVisibleSeatAssignments(base, visible, output),
          "visible move projects successfully");
    check(output.managementSeatId() == 2u, "Management Seat is preserved");
    check(output.findKeyboardOwner(L"Keyboard:Move") == 2u,
          "connected keyboard moves from Seat 1 to Seat 2");
    check(output.findControllerOwner(L"Controller:Disconnected") == 1u,
          "disconnected device assignment is preserved");
    check(output.findAudioOutputOwner(L"Audio:Headset") == 1u &&
              output.findAudioInputOwner(L"Audio:Mic") == 2u,
          "nonvisual audio assignments are preserved");
    check(output.getSeat(1u)->name == L"Custom Left" &&
              output.getSeat(2u)->name == L"Custom Right",
          "custom Seat names are preserved");
    check(!output.getSeat(2u)->active, "inactive Seat state is preserved");
}

void testPoolAndPrimaryDisplayProjection() {
    auto base = twoSeats();
    check(base.assignKeyboard(1u, L"Keyboard:PoolMe"), "fixture assigns keyboard");
    check(base.assignDisplay(1u, L"Display:A", true), "fixture assigns primary display A");
    check(base.assignDisplay(1u, L"Display:B"), "fixture assigns display B");

    hydra::WorkspaceManager output;
    const std::vector<hydra::VisibleSeatDeviceAssignment> visible{
        {hydra::SeatDeviceType::Keyboard, L"Keyboard:PoolMe", 0u, false},
        {hydra::SeatDeviceType::Display, L"Display:A", 1u, false},
        {hydra::SeatDeviceType::Display, L"Display:B", 1u, true},
    };
    check(hydra::projectVisibleSeatAssignments(base, visible, output),
          "Pool/primary projection succeeds");
    check(!output.findKeyboardOwner(L"Keyboard:PoolMe"),
          "seatId zero moves a visible device to the Pool");
    check(output.getSeat(1u)->primaryDisplayId == std::optional<std::wstring>(L"Display:B"),
          "visible primary-display selection is projected deterministically");
}

void testLegacyNamesAndShareablePolicy() {
    auto base = twoSeats(L"Player 1", L"Player 2");
    check(base.assignKeyboard(1u, L"Keyboard:Shared", true),
          "fixture creates shareable keyboard on Seat 1");
    check(base.assignKeyboard(2u, L"Keyboard:Shared"),
          "fixture shares keyboard with Seat 2");

    hydra::WorkspaceManager output;
    const std::vector<hydra::VisibleSeatDeviceAssignment> visible{
        {hydra::SeatDeviceType::Keyboard, L"KEYBOARD:SHARED", 1u, false},
    };
    check(hydra::projectVisibleSeatAssignments(base, visible, output),
          "shareable projection succeeds");
    check(output.getSeat(1u)->name == L"Seat 1" && output.getSeat(2u)->name == L"Seat 2",
          "exact legacy Player names migrate to Seat names");
    check(output.isDeviceShareable(hydra::SeatDeviceType::Keyboard, L"keyboard:shared"),
          "shareable policy is preserved");
    const auto owners = output.findDeviceOwners(
        hydra::SeatDeviceType::Keyboard, L"Keyboard:Shared");
    check(owners.size() == 2u && owners[0] == 1u && owners[1] == 2u,
          "legacy single-owner tile view does not collapse multi-owner policy");
}

void testInvalidProjectionIsTransactional() {
    hydra::WorkspaceManager base = twoSeats();
    hydra::WorkspaceManager output;
    check(output.createSeat(L"Sentinel") == 1u, "output sentinel fixture is created");
    check(output.assignKeyboard(1u, L"Keyboard:Sentinel"),
          "output sentinel device is created");
    const auto before = output.getAllSeats();
    const auto managementBefore = output.managementSeatId();

    const std::vector<hydra::VisibleSeatDeviceAssignment> duplicate{
        {hydra::SeatDeviceType::Keyboard, L"Keyboard:A", 1u, false},
        {hydra::SeatDeviceType::Keyboard, L"keyboard:a", 2u, false},
    };
    std::string error;
    check(!hydra::projectVisibleSeatAssignments(base, duplicate, output, &error),
          "case-insensitive duplicate visible ids are rejected");
    check(!error.empty(), "invalid projection reports a diagnostic");
    check(output.getAllSeats() == before && output.managementSeatId() == managementBefore,
          "failed projection leaves output unchanged");

    const std::vector<hydra::VisibleSeatDeviceAssignment> invalidSeat{
        {hydra::SeatDeviceType::Mouse, L"Mouse:A", 3u, false},
    };
    check(!hydra::projectVisibleSeatAssignments(base, invalidSeat, output, &error),
          "unsupported Seat id is rejected");
    check(output.getAllSeats() == before, "invalid Seat id remains transactional");

    const std::vector<hydra::VisibleSeatDeviceAssignment> invalidPrimary{
        {hydra::SeatDeviceType::Keyboard, L"Keyboard:B", 1u, true},
    };
    check(!hydra::projectVisibleSeatAssignments(base, invalidPrimary, output, &error),
          "non-display primary flag is rejected");
    check(output.getAllSeats() == before, "invalid primary flag remains transactional");

    const std::vector<hydra::VisibleSeatDeviceAssignment> invalidType{
        {hydra::SeatDeviceType::AudioOutput, L"Audio:Visible", 1u, false},
    };
    check(!hydra::projectVisibleSeatAssignments(base, invalidType, output, &error),
          "non-tile audio assignment is rejected");
    check(output.getAllSeats() == before, "invalid device type remains transactional");
}

void testVisibleAssignmentBound() {
    auto base = twoSeats();
    hydra::WorkspaceManager output;
    std::vector<hydra::VisibleSeatDeviceAssignment> visible;
    visible.reserve(257u);
    for (std::size_t index = 0u; index < 257u; ++index) {
        visible.push_back({hydra::SeatDeviceType::Keyboard,
                           L"Keyboard:" + std::to_wstring(index), 0u, false});
    }
    check(!hydra::projectVisibleSeatAssignments(base, visible, output),
          "visible assignment list is bounded");
}

} // namespace

int main() {
    testEmptyBaseBuildsExactlyTwoSeats();
    testVisibleMovesPreserveHiddenState();
    testPoolAndPrimaryDisplayProjection();
    testLegacyNamesAndShareablePolicy();
    testInvalidProjectionIsTransactional();
    testVisibleAssignmentBound();
    std::cout << "Seat assignment projection tests passed.\n";
    return EXIT_SUCCESS;
}
