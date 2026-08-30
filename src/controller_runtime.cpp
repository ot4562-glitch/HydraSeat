#include "hydra/controller_runtime.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace hydra::controller {
namespace {

constexpr auto kMinimumPollInterval = std::chrono::milliseconds(4);
constexpr auto kMaximumPollInterval = std::chrono::seconds(5);
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::wstring canonicalWide(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
    return result;
}

std::uint64_t sourceKeyFor(std::string_view runtimeKey) noexcept {
    std::uint64_t hash = kFnvOffset;
    for (const char raw : runtimeKey) {
        const auto ch = static_cast<unsigned char>(raw);
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= kFnvPrime;
    }
    return hash == 0 ? 1u : hash;
}

bool validRuntimeKey(std::string_view key) noexcept {
    return !key.empty() && key.size() <= kMaxControllerPersistentIdChars &&
           key.find('\0') == std::string_view::npos;
}

bool validSource(const SourceDescriptor& source, std::string& error) {
    if (!validRuntimeKey(source.runtimeKey)) {
        error = "controller source runtime key is empty or exceeds the bound";
        return false;
    }
    if (source.api != ApiSurface::XInput &&
        source.api != ApiSurface::DirectInput &&
        source.api != ApiSurface::GameInput) {
        error = "controller source API surface is invalid";
        return false;
    }
    if (source.identityQuality != IdentityQuality::RuntimeOnly &&
        source.identityQuality != IdentityQuality::Stable) {
        error = "controller source identity quality is invalid";
        return false;
    }
    if (source.displayName.size() > kMaxControllerDisplayNameChars ||
        source.displayName.find(L'\0') != std::wstring::npos) {
        error = "controller source display name exceeds the bound or is malformed";
        return false;
    }
    if (source.persistentId &&
        (source.persistentId->empty() ||
         source.persistentId->size() > kMaxControllerPersistentIdChars ||
         source.persistentId->find(L'\0') != std::wstring::npos)) {
        error = "controller persistent ID is empty or exceeds the bound";
        return false;
    }
    if (source.identityQuality == IdentityQuality::Stable && !source.persistentId) {
        error = "stable controller source has no persistent ID";
        return false;
    }
    if (source.api == ApiSurface::XInput &&
        source.runtimeXInputSlotHint != gatec::kNoRuntimeXInputSlot &&
        source.runtimeXInputSlotHint >= gatec::kVirtualXInputSlotCount) {
        error = "controller source has an invalid XInput slot hint";
        return false;
    }
    if (!source.connected &&
        (source.stateAvailable || source.capabilitiesAvailable ||
         source.batteryInformationAvailable || source.vibrationSupported)) {
        error = "disconnected controller source exposes live state or metadata";
        return false;
    }
    if (source.capabilitiesAvailable &&
        !gatec::validXInputCapabilities(source.capabilities)) {
        error = "controller source capabilities are malformed";
        return false;
    }
    if (source.batteryInformationAvailable &&
        !gatec::validXInputBattery(source.battery)) {
        error = "controller source battery information is malformed";
        return false;
    }
    if (source.vibrationSupported &&
        (!source.capabilitiesAvailable ||
         !source.capabilities.vibrationSupported)) {
        error = "controller source vibration support has no matching capabilities";
        return false;
    }
    if (source.sourceGeneration == std::numeric_limits<std::uint64_t>::max()) {
        error = "controller source generation cannot advance safely";
        return false;
    }
    return true;
}

bool normalizeSources(std::vector<SourceDescriptor>& sources, std::string& error) {
    if (sources.size() > kMaxControllerSources) {
        error = "controller source count exceeds the bounded inventory";
        return false;
    }

    std::set<std::string> runtimeKeys;
    std::set<std::pair<ApiSurface, std::wstring>> persistentKeys;
    for (const auto& source : sources) {
        if (!validSource(source, error)) return false;
        if (!runtimeKeys.insert(source.runtimeKey).second) {
            error = "controller source inventory contains a duplicate runtime key";
            return false;
        }
        if (source.persistentId) {
            const auto key = std::make_pair(source.api,
                                            canonicalWide(*source.persistentId));
            if (!persistentKeys.insert(key).second) {
                error = "controller source inventory contains a duplicate stable identity";
                return false;
            }
        }
    }

    std::sort(sources.begin(), sources.end(),
              [](const SourceDescriptor& left, const SourceDescriptor& right) {
                  const std::wstring leftPersistent = left.persistentId
                      ? canonicalWide(*left.persistentId) : std::wstring{};
                  const std::wstring rightPersistent = right.persistentId
                      ? canonicalWide(*right.persistentId) : std::wstring{};
                  return std::tuple{left.api, leftPersistent, left.runtimeKey} <
                         std::tuple{right.api, rightPersistent, right.runtimeKey};
              });
    return true;
}

const SourceDescriptor* findPersistentSource(const SourceSnapshot& snapshot,
                                             ApiSurface api,
                                             std::wstring_view persistentId,
                                             std::size_t& matches) {
    matches = 0;
    const auto wanted = canonicalWide(persistentId);
    const SourceDescriptor* selected = nullptr;
    for (const auto& source : snapshot.sources) {
        if (source.api != api || source.identityQuality != IdentityQuality::Stable ||
            !source.persistentId) {
            continue;
        }
        if (canonicalWide(*source.persistentId) != wanted) continue;
        ++matches;
        selected = &source;
    }
    return selected;
}

const SourceDescriptor* findRuntimeXInputSource(const SourceSnapshot& snapshot,
                                                std::uint8_t slot,
                                                std::size_t& matches) {
    matches = 0;
    const SourceDescriptor* selected = nullptr;
    for (const auto& source : snapshot.sources) {
        if (source.api != ApiSurface::XInput ||
            source.runtimeXInputSlotHint != slot) {
            continue;
        }
        ++matches;
        selected = &source;
    }
    return selected;
}

void addIssue(BindingPlan& plan, BindingIssueCode code, SeatId seatId,
              std::wstring controllerId = {}) {
    plan.valid = false;
    plan.issues.push_back({code, seatId, std::move(controllerId)});
}

gatec::ControllerSourceIdentity gateSource(const SeatBinding& binding) noexcept {
    gatec::ControllerSourceIdentity result;
    result.kind = gatec::ControllerSourceKind::ProfileSelected;
    result.runtimeXInputSlotHint = binding.runtimeXInputSlotHint;
    result.sourceKey = binding.sourceKey;
    return result;
}

bool sourceMatchesBindingAuthority(const SeatBinding& binding,
                                   const SourceDescriptor& source) {
    if (source.runtimeKey != binding.runtimeKey || source.api != binding.api) {
        return false;
    }
    if (binding.persistentControllerId) {
        return source.identityQuality == IdentityQuality::Stable &&
               source.persistentId &&
               canonicalWide(*source.persistentId) ==
                   canonicalWide(*binding.persistentControllerId);
    }
    return binding.api == ApiSurface::XInput &&
           source.runtimeXInputSlotHint == binding.runtimeXInputSlotHint;
}

} // namespace

PollWorker::PollWorker(std::shared_ptr<SourceBackend> backend)
    : backend_(std::move(backend)) {}

PollWorker::~PollWorker() {
    stop();
}

void PollWorker::invalidateSnapshot() noexcept {
    std::lock_guard lock(mutex_);
    for (auto& [runtimeKey, wasConnected] : previousConnected_) {
        if (!wasConnected) continue;
        wasConnected = false;
        auto& generation = sourceGenerations_[runtimeKey];
        if (generation != std::numeric_limits<std::uint64_t>::max()) {
            ++generation;
        }
    }
    if (snapshot_.pollSequence != std::numeric_limits<std::uint64_t>::max()) {
        ++snapshot_.pollSequence;
    }
    if (snapshot_.generation != std::numeric_limits<std::uint64_t>::max()) {
        ++snapshot_.generation;
    }
    snapshot_.authoritative = false;
    snapshot_.sources.clear();
}

bool PollWorker::pollOnce(std::string* error) {
    if (error != nullptr) error->clear();
    if (!backend_) {
        if (error != nullptr) *error = "controller source backend is unavailable";
        invalidateSnapshot();
        return false;
    }

    std::vector<SourceDescriptor> sources;
    std::string localError;
    if (!backend_->scan(sources, localError)) {
        if (error != nullptr) {
            *error = localError.empty() ? "controller source scan failed"
                                        : std::move(localError);
        }
        invalidateSnapshot();
        return false;
    }
    if (!normalizeSources(sources, localError)) {
        if (error != nullptr) *error = std::move(localError);
        invalidateSnapshot();
        return false;
    }

    std::lock_guard lock(mutex_);
    std::set<std::string> seen;
    for (auto& source : sources) {
        seen.insert(source.runtimeKey);
        const auto previous = previousConnected_.find(source.runtimeKey);
        const auto previousSlot = previousRuntimeSlots_.find(source.runtimeKey);
        const auto previousAuthority = previousAuthorities_.find(source.runtimeKey);
        SourceAuthoritySnapshot currentAuthority;
        currentAuthority.api = source.api;
        currentAuthority.identityQuality = source.identityQuality;
        if (source.persistentId) {
            currentAuthority.persistentId = canonicalWide(*source.persistentId);
        }
        auto generation = sourceGenerations_[source.runtimeKey];
        if (generation == 0) generation = 1;
        const bool connectionChanged =
            previous != previousConnected_.end() &&
            previous->second != source.connected;
        const bool runtimeRouteChanged =
            previousSlot != previousRuntimeSlots_.end() &&
            previousSlot->second != source.runtimeXInputSlotHint;
        const bool authorityChanged =
            previousAuthority != previousAuthorities_.end() &&
            previousAuthority->second != currentAuthority;
        if (connectionChanged || runtimeRouteChanged || authorityChanged) {
            if (generation == std::numeric_limits<std::uint64_t>::max()) {
                if (error != nullptr) {
                    *error = "controller source generation overflow";
                }
                snapshot_.authoritative = false;
                snapshot_.sources.clear();
                return false;
            }
            ++generation;
        }
        sourceGenerations_[source.runtimeKey] = generation;
        previousConnected_[source.runtimeKey] = source.connected;
        previousRuntimeSlots_[source.runtimeKey] = source.runtimeXInputSlotHint;
        previousAuthorities_[source.runtimeKey] = std::move(currentAuthority);
        source.sourceGeneration = generation;
    }
    for (auto& [runtimeKey, wasConnected] : previousConnected_) {
        if (!seen.contains(runtimeKey) && wasConnected) {
            wasConnected = false;
            auto& generation = sourceGenerations_[runtimeKey];
            if (generation == std::numeric_limits<std::uint64_t>::max()) {
                if (error != nullptr) {
                    *error = "controller source generation overflow";
                }
                snapshot_.authoritative = false;
                snapshot_.sources.clear();
                return false;
            }
            ++generation;
        }
    }
    if (snapshot_.pollSequence == std::numeric_limits<std::uint64_t>::max() ||
        snapshot_.generation == std::numeric_limits<std::uint64_t>::max()) {
        if (error != nullptr) *error = "controller poll snapshot generation overflow";
        snapshot_.authoritative = false;
        snapshot_.sources.clear();
        return false;
    }
    snapshot_.pollSequence += 1u;
    snapshot_.generation += 1u;
    snapshot_.authoritative = true;
    snapshot_.sources = std::move(sources);
    return true;
}

bool PollWorker::start(std::chrono::milliseconds interval, std::string* error) {
    if (error != nullptr) error->clear();
    if (interval < kMinimumPollInterval || interval > kMaximumPollInterval) {
        if (error != nullptr) {
            *error = "controller poll interval is outside the bounded 4ms..5s range";
        }
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        if (running_) return true;
    }
    // Publish one deterministic snapshot before the background thread starts so
    // configure() never races an empty initial inventory.
    if (!pollOnce(error)) return false;
    {
        std::lock_guard lock(mutex_);
        if (running_) return true;
        stopRequested_ = false;
        running_ = true;
    }
    try {
        thread_ = std::thread([this, interval] { loop(interval); });
    } catch (...) {
        std::lock_guard lock(mutex_);
        running_ = false;
        if (error != nullptr) *error = "controller poll worker thread creation failed";
        return false;
    }
    return true;
}

void PollWorker::stop() noexcept {
    {
        std::lock_guard lock(mutex_);
        stopRequested_ = true;
    }
    condition_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard lock(mutex_);
    running_ = false;
}

SourceSnapshot PollWorker::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

bool PollWorker::running() const noexcept {
    std::lock_guard lock(mutex_);
    return running_;
}

void PollWorker::loop(std::chrono::milliseconds interval) noexcept {
    while (true) {
        {
            std::unique_lock lock(mutex_);
            if (condition_.wait_for(lock, interval,
                                    [this] { return stopRequested_; })) {
                break;
            }
        }
        (void)pollOnce(nullptr);
    }
    std::lock_guard lock(mutex_);
    running_ = false;
}

BindingPlan planSeatBindings(std::span<const SeatBindingRequest> requests,
                             const SourceSnapshot& sources) {
    BindingPlan plan;
    if (requests.size() > 2u) {
        addIssue(plan, BindingIssueCode::V1SeatLimitExceeded, 0);
        return plan;
    }

    std::set<SeatId> seats;
    std::set<std::string> assignedSources;
    std::set<std::uint64_t> assignedSourceKeys;
    for (const auto& request : requests) {
        if (request.seatId == 0) {
            addIssue(plan, BindingIssueCode::InvalidSeat, request.seatId);
            continue;
        }
        if (!seats.insert(request.seatId).second) {
            addIssue(plan, BindingIssueCode::DuplicateSeat, request.seatId);
            continue;
        }

        const SourceDescriptor* source = nullptr;
        std::size_t matches = 0;
        std::wstring issueId;
        if (request.persistentControllerId) {
            issueId = *request.persistentControllerId;
            source = findPersistentSource(sources, request.api,
                                          *request.persistentControllerId,
                                          matches);
            if (matches == 0) {
                bool foundOtherApi = false;
                const auto wanted = canonicalWide(*request.persistentControllerId);
                for (const auto& candidate : sources.sources) {
                    if (candidate.identityQuality == IdentityQuality::Stable &&
                        candidate.persistentId &&
                        canonicalWide(*candidate.persistentId) == wanted) {
                        foundOtherApi = true;
                        break;
                    }
                }
                addIssue(plan,
                         foundOtherApi ? BindingIssueCode::SourceApiMismatch
                                       : BindingIssueCode::SourceNotFound,
                         request.seatId, issueId);
                continue;
            }
        } else if (request.api == ApiSurface::XInput &&
                   request.runtimeXInputSlot) {
            if (*request.runtimeXInputSlot >= gatec::kVirtualXInputSlotCount) {
                addIssue(plan, BindingIssueCode::RuntimeSlotNotSessionExplicit,
                         request.seatId);
                continue;
            }
            source = findRuntimeXInputSource(sources,
                                             *request.runtimeXInputSlot,
                                             matches);
            if (matches == 0) {
                addIssue(plan, BindingIssueCode::SourceNotFound, request.seatId);
                continue;
            }
        } else {
            addIssue(plan, BindingIssueCode::MissingPersistentIdentity,
                     request.seatId);
            continue;
        }

        if (matches != 1 || source == nullptr) {
            addIssue(plan, BindingIssueCode::AmbiguousSource,
                     request.seatId, issueId);
            continue;
        }
        if (source->api != request.api) {
            addIssue(plan, BindingIssueCode::SourceApiMismatch,
                     request.seatId, issueId);
            continue;
        }
        if (!source->connected) {
            addIssue(plan, BindingIssueCode::SourceDisconnected,
                     request.seatId, issueId);
            continue;
        }
        if (!assignedSources.insert(source->runtimeKey).second) {
            addIssue(plan, BindingIssueCode::SourceAlreadyAssigned,
                     request.seatId, issueId);
            continue;
        }

        const auto sourceKey = sourceKeyFor(source->runtimeKey);
        if (!assignedSourceKeys.insert(sourceKey).second) {
            addIssue(plan, BindingIssueCode::AmbiguousSource,
                     request.seatId, issueId);
            continue;
        }
        plan.bindings.push_back(
            {request.seatId, source->api, source->runtimeKey,
             request.persistentControllerId ? source->persistentId : std::nullopt,
             sourceKey, source->sourceGeneration, source->runtimeXInputSlotHint});
    }

    std::sort(plan.bindings.begin(), plan.bindings.end(),
              [](const SeatBinding& left, const SeatBinding& right) {
                  return left.seatId < right.seatId;
              });
    std::sort(plan.issues.begin(), plan.issues.end(),
              [](const BindingIssue& left, const BindingIssue& right) {
                  return std::tie(left.seatId, left.code, left.controllerId) <
                         std::tie(right.seatId, right.code, right.controllerId);
              });
    return plan;
}

SeatControllerRuntime::SeatControllerRuntime(
    std::shared_ptr<PollWorker> worker,
    std::shared_ptr<SourceBackend> backend)
    : worker_(std::move(worker)), backend_(std::move(backend)) {}

bool SeatControllerRuntime::configure(
    std::span<const SeatBindingRequest> requests,
    std::string* error) {
    if (error != nullptr) error->clear();
    if (!worker_ || !backend_) {
        if (error != nullptr) *error = "controller runtime backend is unavailable";
        return false;
    }
    if (!worker_->running() && !worker_->pollOnce(error)) {
        snapshot_ = worker_->snapshot();
        return false;
    }
    snapshot_ = worker_->snapshot();
    if (!snapshot_.authoritative) {
        if (error != nullptr && error->empty()) {
            *error = "controller source snapshot is not authoritative";
        }
        return false;
    }
    const auto plan = planSeatBindings(requests, snapshot_);
    if (!plan.valid) {
        if (error != nullptr) {
            *error = plan.issues.empty()
                ? "controller binding plan is invalid"
                : std::string(bindingIssueCodeName(plan.issues.front().code));
        }
        return false;
    }
    bindings_.clear();
    for (const auto& binding : plan.bindings) {
        bindings_.emplace(binding.seatId, binding);
    }
    return true;
}

bool SeatControllerRuntime::refresh(std::string* error) {
    if (error != nullptr) error->clear();
    if (!worker_) {
        if (error != nullptr) *error = "controller poll worker is unavailable";
        return false;
    }
    if (!worker_->running() && !worker_->pollOnce(error)) {
        snapshot_ = worker_->snapshot();
        return false;
    }
    snapshot_ = worker_->snapshot();
    if (!snapshot_.authoritative) {
        if (error != nullptr && error->empty()) {
            *error = "controller source snapshot is not authoritative";
        }
        return false;
    }
    return true;
}

gatec::VirtualXInputResult SeatControllerRuntime::mapSeatToContext(
    SeatId seatId,
    gatec::VirtualXInputContext& context,
    std::uint8_t logicalSlot) {
    const auto found = bindings_.find(seatId);
    if (found == bindings_.end()) return gatec::VirtualXInputResult::NotMapped;
    const auto source = findSource(found->second.runtimeKey);
    if (!source || !source->connected) {
        return gatec::VirtualXInputResult::Disconnected;
    }
    if (!sourceMatchesBindingAuthority(found->second, *source) ||
        source->api != ApiSurface::XInput || !source->stateAvailable) {
        return gatec::VirtualXInputResult::InvalidState;
    }
    auto proposed = found->second;
    proposed.sourceGeneration = source->sourceGeneration;
    proposed.runtimeXInputSlotHint = source->runtimeXInputSlotHint;
    const auto result = context.mapLogicalSlot(nextSequence(), logicalSlot,
                                               gateSource(proposed),
                                               source->sourceGeneration);
    if (result == gatec::VirtualXInputResult::Success) {
        found->second = std::move(proposed);
    }
    return result;
}

gatec::VirtualXInputResult SeatControllerRuntime::updateSeatContext(
    SeatId seatId,
    gatec::VirtualXInputContext& context) {
    const auto found = bindings_.find(seatId);
    if (found == bindings_.end()) return gatec::VirtualXInputResult::NotMapped;
    const auto source = findSource(found->second.runtimeKey);
    const auto identity = gateSource(found->second);
    if (!source || !source->connected ||
        !sourceMatchesBindingAuthority(found->second, *source)) {
        return context.disconnectSource(nextSequence(), identity,
                                        found->second.sourceGeneration);
    }
    if (!source->stateAvailable) {
        return gatec::VirtualXInputResult::InvalidState;
    }

    if (source->api != ApiSurface::XInput) {
        return gatec::VirtualXInputResult::InvalidState;
    }

    auto proposed = found->second;
    proposed.sourceGeneration = source->sourceGeneration;
    proposed.runtimeXInputSlotHint = source->runtimeXInputSlotHint;
    const auto* capabilities = source->capabilitiesAvailable
        ? &source->capabilities
        : nullptr;
    const auto* battery = source->batteryInformationAvailable
        ? &source->battery
        : nullptr;
    const auto result = context.applySourceSnapshot(
        nextSequence(), gateSource(proposed), source->sourceGeneration,
        source->gamepad, capabilities, battery);
    if (result == gatec::VirtualXInputResult::Success) {
        found->second = std::move(proposed);
    }
    return result;
}

bool SeatControllerRuntime::requestVibration(
    SeatId seatId,
    gatec::VirtualXInputContext& context,
    std::uint8_t logicalSlot,
    std::uint16_t leftMotor,
    std::uint16_t rightMotor,
    std::string* error) {
    if (error != nullptr) error->clear();
    const auto found = bindings_.find(seatId);
    if (found == bindings_.end()) {
        if (error != nullptr) *error = "controller Seat binding is missing";
        return false;
    }
    const auto source = findSource(found->second.runtimeKey);
    if (!source || !source->connected) {
        if (error != nullptr) *error = "controller source is disconnected";
        return false;
    }
    if (!sourceMatchesBindingAuthority(found->second, *source)) {
        if (error != nullptr) {
            *error = "controller source no longer matches the Seat binding authority";
        }
        return false;
    }
    if (source->api != ApiSurface::XInput || !source->capabilitiesAvailable ||
        !source->capabilities.vibrationSupported || !source->vibrationSupported) {
        if (error != nullptr) *error = "selected controller backend does not support current routed vibration";
        return false;
    }

    const auto expectedSource = gateSource(found->second);
    gatec::VirtualXInputMapping mapping;
    if (context.getMapping(logicalSlot, mapping) !=
        gatec::VirtualXInputResult::Success) {
        if (error != nullptr) *error = "controller logical slot is not mapped";
        return false;
    }
    if (mapping.source != expectedSource ||
        mapping.sourceGeneration != source->sourceGeneration ||
        mapping.sourceGeneration != found->second.sourceGeneration) {
        if (error != nullptr) *error = "controller vibration mapping has stale or foreign source identity";
        return false;
    }

    gatec::VirtualXInputVibrationRequest request;
    request.logicalSlot = logicalSlot;
    request.leftMotor = leftMotor;
    request.rightMotor = rightMotor;
    request.expectedMappingGeneration = mapping.mappingGeneration;
    request.expectedSourceGeneration = mapping.sourceGeneration;
    gatec::VirtualXInputVibrationRoute route;
    if (context.routeVibration(nextSequence(), request, route) !=
        gatec::VirtualXInputResult::Success) {
        if (error != nullptr) *error = "controller vibration request was rejected by the Seat-local context";
        return false;
    }
    if (route.source != expectedSource ||
        route.sourceGeneration != source->sourceGeneration ||
        route.mappingGeneration != mapping.mappingGeneration) {
        if (error != nullptr) *error = "controller vibration route changed source identity unexpectedly";
        return false;
    }

    std::string localError;
    const bool routed = backend_->setVibration(
        source->runtimeKey, route.leftMotor, route.rightMotor, localError);
    if (!routed && error != nullptr) *error = std::move(localError);
    return routed;
}

std::optional<SeatBinding> SeatControllerRuntime::binding(SeatId seatId) const {
    const auto found = bindings_.find(seatId);
    if (found == bindings_.end()) return std::nullopt;
    return found->second;
}

std::uint64_t SeatControllerRuntime::sourceGeneration(
    std::string_view runtimeKey) const noexcept {
    const auto found = std::find_if(
        snapshot_.sources.begin(), snapshot_.sources.end(),
        [&](const SourceDescriptor& source) {
            return source.runtimeKey == runtimeKey;
        });
    return found == snapshot_.sources.end() ? 0 : found->sourceGeneration;
}

std::optional<SourceDescriptor> SeatControllerRuntime::findSource(
    std::string_view runtimeKey) const {
    const auto found = std::find_if(
        snapshot_.sources.begin(), snapshot_.sources.end(),
        [&](const SourceDescriptor& source) {
            return source.runtimeKey == runtimeKey;
        });
    if (found == snapshot_.sources.end()) return std::nullopt;
    return *found;
}

std::uint64_t SeatControllerRuntime::nextSequence() noexcept {
    if (sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        sequence_ = 0;
    }
    return ++sequence_;
}

std::wstring formatDirectInputPersistentId(
    const directinput::DirectInputInstanceId& value) {
    std::wostringstream stream;
    stream << L"directinput:{" << std::hex << std::setfill(L'0')
           << std::setw(8) << value.data1 << L'-'
           << std::setw(4) << value.data2 << L'-'
           << std::setw(4) << value.data3 << L'-';
    for (std::size_t index = 0; index < value.data4.size(); ++index) {
        if (index == 2u) stream << L'-';
        stream << std::setw(2) << static_cast<unsigned>(value.data4[index]);
    }
    stream << L'}';
    return stream.str();
}

std::string_view apiSurfaceName(ApiSurface api) noexcept {
    switch (api) {
        case ApiSurface::XInput: return "xinput";
        case ApiSurface::DirectInput: return "directinput";
        case ApiSurface::GameInput: return "gameinput";
    }
    return "unknown";
}

std::string_view bindingIssueCodeName(BindingIssueCode code) noexcept {
    switch (code) {
        case BindingIssueCode::InvalidSeat: return "invalid-seat";
        case BindingIssueCode::DuplicateSeat: return "duplicate-seat";
        case BindingIssueCode::V1SeatLimitExceeded: return "v1-seat-limit-exceeded";
        case BindingIssueCode::MissingPersistentIdentity: return "missing-persistent-identity";
        case BindingIssueCode::SourceNotFound: return "source-not-found";
        case BindingIssueCode::SourceDisconnected: return "source-disconnected";
        case BindingIssueCode::SourceApiMismatch: return "source-api-mismatch";
        case BindingIssueCode::SourceAlreadyAssigned: return "source-already-assigned";
        case BindingIssueCode::RuntimeSlotNotSessionExplicit: return "runtime-slot-not-session-explicit";
        case BindingIssueCode::AmbiguousSource: return "ambiguous-source";
    }
    return "unknown";
}

} // namespace hydra::controller
