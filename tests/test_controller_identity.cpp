#include "hydra/controller_identity.hpp"
#include "hydra/controller_inventory.hpp"
#include "hydra/controller_io.hpp"
#include "hydra/controller_pairing.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

namespace {

bool hasIssue(const hydra::controller::BindingPlan& plan,
              hydra::controller::BindingIssueCode code) {
    return std::any_of(plan.issues.begin(), plan.issues.end(),
                       [code](const auto& issue) { return issue.code == code; });
}

} // namespace

void testControllerIdentity() {
    using namespace hydra::controller;

    const SourceDescriptor stablePad{
        "gameinput:pad-a",
        std::wstring{L"container-a"},
        L"Stable Pad A",
        ApiSurface::GameInput,
        IdentityQuality::Stable,
        std::nullopt,
        true,
    };
    const SourceDescriptor runtimePad{
        "xinput-slot:0",
        std::nullopt,
        L"Runtime XInput Slot 0",
        ApiSurface::XInput,
        IdentityQuality::RuntimeOnly,
        std::uint8_t{0},
        true,
    };
    const std::array sources{stablePad, runtimePad};

    const std::array requests{
        SeatBindingRequest{1, ApiSurface::GameInput,
                           std::wstring{L"CONTAINER-A"}, std::nullopt},
        SeatBindingRequest{2, ApiSurface::XInput,
                           std::nullopt, std::uint8_t{0}},
    };

    const auto validPlan = planSeatBindings(requests, sources);
    assert(validPlan.valid);
    assert(validPlan.issues.empty());
    assert(validPlan.bindings.size() == 2);
    assert(validPlan.bindings[0].seatId == 1);
    assert(validPlan.bindings[0].persistentControllerId ==
           std::optional<std::wstring>{L"container-a"});
    assert(validPlan.bindings[1].seatId == 2);
    assert(validPlan.bindings[1].runtimeXInputSlot ==
           std::optional<std::uint8_t>{0});

    InventorySnapshot pairingInventory;
    pairingInventory.authoritative = true;
    pairingInventory.sources.push_back(runtimePad);
    pairingInventory.physicalControllers.push_back(
        {L"container-a", L"Physical Pad A", L"hid-path-a"});

    const auto paired = pairPhysicalControllerToXInput(
        1, L"CONTAINER-A", 0, pairingInventory);
    assert(paired.status == PairingStatus::Ok);
    assert(paired.binding.has_value());
    assert(paired.binding->persistentControllerId ==
           std::optional<std::wstring>{L"container-a"});
    assert(paired.binding->runtimeKey == "xinput-slot:0");

    auto disconnectedInventory = pairingInventory;
    disconnectedInventory.sources[0].connected = false;
    assert(pairPhysicalControllerToXInput(
               1, L"container-a", 0, disconnectedInventory).status ==
           PairingStatus::RuntimeSourceDisconnected);

    XInputPairingSnapshot beforePress;
    XInputPairingSnapshot afterPress;
    beforePress.authoritative = true;
    afterPress.authoritative = true;
    beforePress.slots[0].connected = true;
    afterPress.slots[0].connected = true;
    afterPress.slots[0].state.buttons = std::uint16_t{1};

    const auto uniquePress = detectUniqueXInputButtonPress(beforePress, afterPress);
    assert(uniquePress.status == PairingProbeStatus::UniqueButtonPress);
    assert(uniquePress.runtimeSlot == std::optional<std::uint8_t>{0});

    const auto pairedFromPress = pairPhysicalControllerFromButtonPress(
        1, L"container-a", beforePress, afterPress, pairingInventory);
    assert(pairedFromPress.status == PairingStatus::Ok);
    assert(pairedFromPress.binding.has_value());

    auto analogOnly = afterPress;
    analogOnly.slots[0].state.buttons = 0;
    analogOnly.slots[0].state.thumbLX = 100;
    assert(detectUniqueXInputButtonPress(beforePress, analogOnly).status ==
           PairingProbeStatus::NoButtonPress);

    auto ambiguousPress = afterPress;
    beforePress.slots[1].connected = true;
    ambiguousPress.slots[1].connected = true;
    ambiguousPress.slots[1].state.buttons = std::uint16_t{2};
    assert(detectUniqueXInputButtonPress(beforePress, ambiguousPress).status ==
           PairingProbeStatus::AmbiguousButtonPress);
    assert(pairPhysicalControllerFromButtonPress(
               1, L"container-a", beforePress, ambiguousPress, pairingInventory).status ==
           PairingStatus::PairingGestureAmbiguous);

    const std::array missingIdentity{
        SeatBindingRequest{1, ApiSurface::XInput, std::nullopt, std::nullopt},
    };
    const auto missingPlan = planSeatBindings(missingIdentity, sources);
    assert(!missingPlan.valid);
    assert(hasIssue(missingPlan, BindingIssueCode::MissingPersistentIdentity));

    const std::array duplicateSource{
        SeatBindingRequest{1, ApiSurface::GameInput,
                           std::wstring{L"container-a"}, std::nullopt},
        SeatBindingRequest{2, ApiSurface::GameInput,
                           std::wstring{L"container-a"}, std::nullopt},
    };
    const auto duplicatePlan = planSeatBindings(duplicateSource, sources);
    assert(!duplicatePlan.valid);
    assert(hasIssue(duplicatePlan, BindingIssueCode::SourceAlreadyAssigned));

    const std::array invalidSeat{
        SeatBindingRequest{3, ApiSurface::GameInput,
                           std::wstring{L"container-a"}, std::nullopt},
    };
    const auto invalidSeatPlan = planSeatBindings(invalidSeat, sources);
    assert(!invalidSeatPlan.valid);
    assert(hasIssue(invalidSeatPlan, BindingIssueCode::InvalidSeat));

    const std::array invalidRuntimeSlot{
        SeatBindingRequest{1, ApiSurface::XInput,
                           std::nullopt, std::uint8_t{4}},
    };
    const auto invalidSlotPlan = planSeatBindings(invalidRuntimeSlot, sources);
    assert(!invalidSlotPlan.valid);
    assert(hasIssue(invalidSlotPlan, BindingIssueCode::RuntimeSlotOutOfRange));

    std::vector<SourceDescriptor> ambiguousSources{stablePad, runtimePad, stablePad};
    ambiguousSources.back().runtimeKey = "gameinput:pad-a-duplicate";
    const std::array ambiguousRequest{
        SeatBindingRequest{1, ApiSurface::GameInput,
                           std::wstring{L"container-a"}, std::nullopt},
    };
    const auto ambiguousPlan = planSeatBindings(ambiguousRequest, ambiguousSources);
    assert(!ambiguousPlan.valid);
    assert(hasIssue(ambiguousPlan, BindingIssueCode::AmbiguousSource));

    const auto inventory = scanControllerSources();
#if defined(_WIN32)
    assert(inventory.authoritative);
    assert(inventory.sources.size() == kXInputSlotCount);
    for (std::uint8_t slot = 0; slot < kXInputSlotCount; ++slot) {
        const auto& source = inventory.sources[slot];
        assert(source.api == ApiSurface::XInput);
        assert(source.identityQuality == IdentityQuality::RuntimeOnly);
        assert(source.runtimeXInputSlot == std::optional<std::uint8_t>{slot});
        assert(!source.persistentId.has_value());
    }
    assert(captureXInputPairingSnapshot().authoritative);
#else
    assert(!inventory.authoritative);
    assert(inventory.sources.empty());
    assert(!inventory.error.empty());
    assert(!captureXInputPairingSnapshot().authoritative);
#endif

    const auto unsupportedStablePoll = pollBoundController(
        validPlan.bindings[0], pairingInventory);
    assert(unsupportedStablePoll.status == IoStatus::UnsupportedApi);
    assert(!unsupportedStablePoll.state.has_value());

    auto invalidRuntimeBinding = validPlan.bindings[1];
    invalidRuntimeBinding.runtimeKey = "xinput-slot:1";
    const auto invalidRuntimePoll = pollBoundController(
        invalidRuntimeBinding, pairingInventory);
    assert(invalidRuntimePoll.status == IoStatus::InvalidBinding);
    assert(!invalidRuntimePoll.state.has_value());

    auto staleInventory = pairingInventory;
    ++staleInventory.sources[0].sourceGeneration;
    const auto stalePoll = pollBoundController(
        validPlan.bindings[1], staleInventory);
    assert(stalePoll.status == IoStatus::StaleBinding);
    assert(!stalePoll.state.has_value());

    const auto runtimePoll = pollBoundController(
        validPlan.bindings[1], pairingInventory);
#if defined(_WIN32)
    assert(runtimePoll.status == IoStatus::Ok ||
           runtimePoll.status == IoStatus::Disconnected);
    assert(runtimePoll.state.has_value() == (runtimePoll.status == IoStatus::Ok));
    const auto vibrationStatus = setBoundControllerVibration(
        validPlan.bindings[1], pairingInventory, 0, 0);
    assert(vibrationStatus == IoStatus::Ok ||
           vibrationStatus == IoStatus::Disconnected);
#else
    assert(runtimePoll.status == IoStatus::PlatformUnavailable);
    assert(!runtimePoll.state.has_value());
    assert(setBoundControllerVibration(validPlan.bindings[1], pairingInventory, 0, 0) ==
           IoStatus::PlatformUnavailable);
#endif
}
