#include "hydra/runtime_host.hpp"

#include <iterator>
#include <utility>

namespace hydra::runtime {

namespace {

hostipc::SeatSnapshot toHostSnapshot(const SeatRuntimeSnapshot& snapshot) noexcept {
    return {
        snapshot.seatId,
        snapshot.generation,
        snapshot.active,
        snapshot.uiLeaseActive,
        snapshot.gameLeaseActive,
        snapshot.process.has_value(),
        snapshot.targetHwnd != 0,
        snapshot.controllerBinding.has_value(),
        snapshot.process ? snapshot.process->pid : 0u,
        snapshot.process ? snapshot.process->creationIdentity : 0u,
        static_cast<std::uint64_t>(snapshot.targetHwnd),
    };
}

} // namespace

RuntimeHost::RuntimeHost(std::filesystem::path seatConfigPath) {
    seatHardwareStore_.emplace(std::move(seatConfigPath));
}

bool RuntimeHost::loadPersistentSeatHardware(std::string* error) {
    std::lock_guard lock(mutex_);
    if (!seatHardwareStore_) return true;

    auto candidate = hardwareConfigurations_;
    if (!seatHardwareStore_->load(candidate, error)) {
        return false;
    }

    hardwareConfigurations_ = std::move(candidate);
    return true;
}

hostipc::HostSnapshot RuntimeHost::snapshot() const noexcept {
    std::lock_guard lock(mutex_);
    hostipc::HostSnapshot result;
    result.authorityRevision = authorityRevision_;
    const auto seat1 = controller_.snapshot(1);
    const auto seat2 = controller_.snapshot(2);
    if (seat1) result.seats[0] = toHostSnapshot(*seat1);
    if (seat2) result.seats[1] = toHostSnapshot(*seat2);
    return result;
}

std::optional<SeatRuntimeSnapshot> RuntimeHost::seatSnapshot(
    std::uint32_t seatId) const noexcept {
    std::lock_guard lock(mutex_);
    return controller_.snapshot(seatId);
}

controller::InventorySnapshot RuntimeHost::controllerInventorySnapshot() noexcept {
    std::lock_guard lock(mutex_);
    return controllerInventory_.scan();
}

std::vector<DeviceInfo> RuntimeHost::hardwareInventory() {
    std::lock_guard lock(mutex_);

    std::vector<DeviceInfo> result;
    const auto append = [&result](std::vector<DeviceInfo> devices) {
        result.insert(
            result.end(),
            std::make_move_iterator(devices.begin()),
            std::make_move_iterator(devices.end()));
    };

    append(hardwareDetector_.detectDisplays());
    append(hardwareDetector_.detectKeyboards());
    append(hardwareDetector_.detectMice());
    append(hardwareDetector_.detectControllers());
    return result;
}

std::optional<SeatHardwareConfiguration>
RuntimeHost::seatHardwareConfiguration(std::uint32_t seatId) const {
    if (seatId == 0 || seatId > hardwareConfigurations_.size()) {
        return std::nullopt;
    }
    std::lock_guard lock(mutex_);
    return hardwareConfigurations_[seatId - 1u];
}

bool RuntimeHost::configureSeatHardware(
    const ActivationToken& uiLease,
    const SeatHardwareConfiguration& configuration,
    std::string* error) {
    const auto fail = [error](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };

    if (uiLease.leaseClass != LeaseClass::UiConfiguration ||
        configuration.seatId != uiLease.seatId ||
        configuration.seatId == 0 ||
        configuration.seatId > hardwareConfigurations_.size()) {
        return fail("invalid Seat hardware mutation authority");
    }

    std::lock_guard lock(mutex_);
    const auto seat = controller_.snapshot(uiLease.seatId);
    if (!seat || !seat->uiLeaseActive ||
        seat->generation != uiLease.generation) {
        return fail("Seat UI lease is stale or no longer owned");
    }

    const auto displays = hardwareDetector_.detectDisplays();
    const auto keyboards = hardwareDetector_.detectKeyboards();
    const auto mice = hardwareDetector_.detectMice();

    const auto contains = [](const std::vector<DeviceInfo>& devices,
                             const std::wstring& stableId) {
        if (stableId.empty()) return true;
        for (const auto& device : devices) {
            if (device.id == stableId) return true;
        }
        return false;
    };

    if (!contains(displays, configuration.displayId)) {
        return fail("selected display is no longer connected");
    }
    if (!contains(keyboards, configuration.keyboardId)) {
        return fail("selected keyboard is no longer connected");
    }
    if (!contains(mice, configuration.mouseId)) {
        return fail("selected mouse is no longer connected");
    }

    const auto otherIndex = configuration.seatId == 1u ? 1u : 0u;
    const auto& other = hardwareConfigurations_[otherIndex];
    const auto conflicts = [](const std::wstring& requested,
                              const std::wstring& existing) {
        return !requested.empty() && requested == existing;
    };
    if (conflicts(configuration.displayId, other.displayId)) {
        return fail("selected display already belongs to the other Seat");
    }
    if (conflicts(configuration.keyboardId, other.keyboardId)) {
        return fail("selected keyboard already belongs to the other Seat");
    }
    if (conflicts(configuration.mouseId, other.mouseId)) {
        return fail("selected mouse already belongs to the other Seat");
    }

    const auto index = configuration.seatId - 1u;
    if (hardwareConfigurations_[index] == configuration) return true;

    auto candidate = hardwareConfigurations_;
    candidate[index] = configuration;

    // Persist before publishing the new in-memory state. A failed disk write
    // must not leave the live authority ahead of the last durable config.
    if (seatHardwareStore_ &&
        !seatHardwareStore_->save(candidate, error)) {
        return false;
    }

    hardwareConfigurations_ = std::move(candidate);
    noteMutationLocked(true);
    return true;
}

std::optional<std::uint32_t> RuntimeHost::seatForProcess(
    const ProcessIdentity& process) const noexcept {
    if (!process.valid()) return std::nullopt;

    std::lock_guard lock(mutex_);
    std::optional<std::uint32_t> owner;
    for (const std::uint32_t seatId : {1u, 2u}) {
        const auto snapshot = controller_.snapshot(seatId);
        if (!snapshot || !snapshot->gameLeaseActive || !snapshot->process ||
            *snapshot->process != process) {
            continue;
        }
        if (owner) return std::nullopt;
        owner = seatId;
    }
    return owner;
}

ActivationToken RuntimeHost::acquireUiLease(std::uint32_t seatId) noexcept {
    std::lock_guard lock(mutex_);
    const auto lease =
        controller_.acquireSeatLease(seatId, LeaseClass::UiConfiguration);
    noteMutationLocked(lease.valid());
    return lease;
}

bool RuntimeHost::releaseUiLease(const ActivationToken& token) noexcept {
    if (token.leaseClass != LeaseClass::UiConfiguration) return false;
    std::lock_guard lock(mutex_);
    const bool changed = controller_.releaseSeatLease(token);
    noteMutationLocked(changed);
    return changed;
}

bool RuntimeHost::pairController(
    const ActivationToken& uiLease,
    const std::string& persistentControllerId,
    std::uint8_t runtimeXInputSlot) noexcept {
    if (uiLease.leaseClass != LeaseClass::UiConfiguration ||
        persistentControllerId.empty() ||
        runtimeXInputSlot >= controller::kXInputSlotCount) {
        return false;
    }

    std::lock_guard lock(mutex_);
    const auto snapshot = controllerInventory_.scan();
    if (!snapshot.authoritative) return false;

    std::wstring persistentId(
        persistentControllerId.begin(), persistentControllerId.end());
    const auto paired = controller::pairPhysicalControllerToXInput(
        uiLease.seatId, persistentId, runtimeXInputSlot, snapshot);
    if (paired.status != controller::PairingStatus::Ok || !paired.binding) {
        return false;
    }

    const bool changed =
        controller_.bindController(uiLease, *paired.binding, snapshot);
    noteMutationLocked(changed);
    return changed;
}

AudioRouteStatus RuntimeHost::routeAudio(
    const ActivationToken& uiLease,
    const ProcessIdentity& expectedProcess,
    const AudioEndpointIdentity& endpoint,
    AudioRouter& router) noexcept {
    if (uiLease.leaseClass != LeaseClass::UiConfiguration ||
        !expectedProcess.valid() || !endpoint.valid()) {
        return AudioRouteStatus::InvalidProcess;
    }

    // Keep the host authority lock through the OS mutation. This intentionally
    // serializes lifecycle changes with audio mutation so a Seat cannot release
    // or replace the exact process between ownership verification and routing.
    std::lock_guard lock(mutex_);
    const auto snapshot = controller_.snapshot(uiLease.seatId);
    if (!snapshot || !snapshot->uiLeaseActive || !snapshot->gameLeaseActive ||
        snapshot->generation != uiLease.generation || !snapshot->process ||
        *snapshot->process != expectedProcess) {
        return AudioRouteStatus::InvalidProcess;
    }

    return router.assignEndpoint(expectedProcess, endpoint);
}

AudioRouteStatus RuntimeHost::resetAudio(
    const ActivationToken& uiLease,
    const ProcessIdentity& expectedProcess,
    AudioRouter& router) noexcept {
    if (uiLease.leaseClass != LeaseClass::UiConfiguration ||
        !expectedProcess.valid()) {
        return AudioRouteStatus::InvalidProcess;
    }

    std::lock_guard lock(mutex_);
    const auto snapshot = controller_.snapshot(uiLease.seatId);
    if (!snapshot || !snapshot->uiLeaseActive || !snapshot->gameLeaseActive ||
        snapshot->generation != uiLease.generation || !snapshot->process ||
        *snapshot->process != expectedProcess) {
        return AudioRouteStatus::InvalidProcess;
    }

    return router.clearAssignment(expectedProcess);
}

ActivationToken RuntimeHost::beginSeatActivation(std::uint32_t seatId) noexcept {
    std::lock_guard lock(mutex_);
    const auto activation = controller_.beginSeatActivation(seatId);
    noteMutationLocked(activation.valid());
    return activation;
}

bool RuntimeHost::publishProcess(const ActivationToken& activation,
                                 const ProcessIdentity& process) noexcept {
    std::lock_guard lock(mutex_);
    const bool changed = controller_.publishProcess(activation, process);
    noteMutationLocked(changed);
    return changed;
}

bool RuntimeHost::bindTargetWindow(const ActivationToken& activation,
                                   const ProcessIdentity& owner,
                                   std::uintptr_t hwnd) noexcept {
    std::lock_guard lock(mutex_);
    const bool changed = controller_.bindTargetWindow(activation, owner, hwnd);
    noteMutationLocked(changed);
    return changed;
}

bool RuntimeHost::bindController(
    const ActivationToken& activation,
    const controller::SeatBinding& binding,
    const controller::InventorySnapshot& inventory) noexcept {
    std::lock_guard lock(mutex_);
    const bool changed = controller_.bindController(activation, binding, inventory);
    noteMutationLocked(changed);
    return changed;
}

controller::PollResult RuntimeHost::pollController(
    const ActivationToken& activation,
    const controller::InventorySnapshot& inventory) noexcept {
    std::lock_guard lock(mutex_);
    return controller_.pollController(activation, inventory);
}

controller::IoStatus RuntimeHost::setControllerVibration(
    const ActivationToken& activation,
    const controller::InventorySnapshot& inventory,
    std::uint16_t lowFrequencyMotor,
    std::uint16_t highFrequencyMotor) noexcept {
    std::lock_guard lock(mutex_);
    return controller_.setControllerVibration(
        activation, inventory, lowFrequencyMotor, highFrequencyMotor);
}

std::optional<controller::VirtualXInputMapping>
RuntimeHost::virtualXInputMapping(const ActivationToken& activation) const noexcept {
    std::lock_guard lock(mutex_);
    return controller_.virtualXInputMapping(activation);
}

bool RuntimeHost::endSeatActivation(const ActivationToken& activation) noexcept {
    std::lock_guard lock(mutex_);
    const bool changed = controller_.endSeatActivation(activation);
    noteMutationLocked(changed);
    return changed;
}

void RuntimeHost::noteMutationLocked(bool changed) noexcept {
    if (changed && authorityRevision_ != UINT64_MAX) {
        ++authorityRevision_;
    }
}

} // namespace hydra::runtime
