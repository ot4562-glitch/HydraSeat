#include "hydra/audio_routing.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace hydra;
using namespace hydra::audio;

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

EndpointSnapshot endpoints() {
    EndpointSnapshot snapshot;
    snapshot.endpoints = {
        {L"OUT-A", L"Output A", DataFlow::Render,
         kEndpointStateActive, kDefaultRoleConsole},
        {L"OUT-B", L"Output B", DataFlow::Render,
         kEndpointStateActive, 0},
        {L"OUT-OFF", L"Disconnected", DataFlow::Render,
         kEndpointStateUnplugged, 0},
        {L"MIC-A", L"Mic", DataFlow::Capture,
         kEndpointStateActive, 0},
    };
    return snapshot;
}

process::ProcessIdentity identity(std::uint32_t pid,
                                  std::uint64_t creation,
                                  std::wstring path = L"C:\\Games\\game.exe") {
    return {pid, creation, std::move(path)};
}

process::ProcessTreeSnapshot tree() {
    process::ProcessTreeSnapshot result;
    result.seatId = 1;
    result.root = identity(100, 1000);
    result.processes = {
        {result.root, 0, true, false, 0},
        {identity(101, 1001, L"C:\\Games\\helper.exe"), 100, false, false, 0},
    };
    result.sequence = 7;
    return result;
}

SessionRecord session(std::wstring endpoint,
                      std::wstring instance,
                      std::uint32_t pid,
                      std::uint64_t creation,
                      bool verified = true) {
    SessionRecord result;
    result.endpointId = std::move(endpoint);
    result.sessionInstanceId = std::move(instance);
    result.sessionIdentifier = L"session-group";
    result.processId = pid;
    result.processCreationTime100ns = verified ? creation : 0;
    result.executablePath = verified ? L"C:\\Games\\game.exe" : L"";
    result.state = SessionState::Active;
    result.processIdentityVerified = verified;
    return result;
}

class FakeSessionSource final : public SessionSource {
public:
    std::vector<SessionRecord> records;
    std::uint64_t generation{1};
    bool fail{false};
    bool changeEveryRead{false};
    unsigned reads{0};

    bool enumerate(std::span<const EndpointRecord>,
                   std::vector<SessionRecord>& sessions,
                   std::string& error) noexcept override {
        ++reads;
        if (fail) {
            error = "injected session enumeration failure";
            return false;
        }
        sessions = records;
        if (changeEveryRead) ++generation;
        error.clear();
        return true;
    }

    std::uint64_t changeGeneration() const noexcept override {
        return generation;
    }
};

enum class FakeMode {
    Normal,
    FailAfterMutation,
    PretendApplied,
    RollbackFailure,
};

class FakeMutableBackend final : public RouteBackend {
public:
    explicit FakeMutableBackend(std::shared_ptr<FakeSessionSource> source,
                                FakeMode mode = FakeMode::Normal)
        : source_(std::move(source)), mode_(mode) {}

    RouteBackendKind kind() const noexcept override {
        return RouteBackendKind::ProviderManaged;
    }

    RouteCapability capability(const RouteRequest&,
                               const OwnedSessionEvidence&) const noexcept override {
        return RouteCapability::Mutable;
    }

    bool captureState(const RouteRequest& request,
                      const OwnedSessionEvidence& evidence,
                      BackendState& state,
                      std::string& error) noexcept override {
        ++captureCalls;
        captured_.clear();
        for (const auto& item : evidence.ownedSessions) {
            captured_[item.sessionInstanceId] = item.endpointId;
        }
        state.opaque = {0x48u, 0x59u, 0x44u, 0x52u};
        capturedSeat_ = request.seatId;
        error.clear();
        return true;
    }

    bool apply(const RouteRequest& request,
               const BackendState& before,
               std::string& error) noexcept override {
        ++applyCalls;
        if (before.opaque != std::vector<std::uint8_t>({0x48u, 0x59u, 0x44u, 0x52u}) ||
            capturedSeat_ != request.seatId) {
            error = "captured state mismatch";
            return false;
        }
        if (mode_ != FakeMode::PretendApplied) {
            for (auto& item : source_->records) {
                if (sessionMatchesOwnedProcess(request.processTree, item)) {
                    item.endpointId = request.targetEndpointId;
                }
            }
        }
        if (mode_ == FakeMode::FailAfterMutation) {
            error = "injected apply failure after mutation";
            return false;
        }
        error.clear();
        return true;
    }

    bool rollback(const RouteRequest& request,
                  const BackendState& before,
                  std::string& error) noexcept override {
        ++rollbackCalls;
        if (mode_ == FakeMode::RollbackFailure) {
            error = "injected rollback failure";
            return false;
        }
        if (before.opaque.empty() || capturedSeat_ != request.seatId) {
            error = "missing captured rollback state";
            return false;
        }
        for (auto& item : source_->records) {
            if (!sessionMatchesOwnedProcess(request.processTree, item)) continue;
            const auto found = captured_.find(item.sessionInstanceId);
            if (found != captured_.end()) item.endpointId = found->second;
        }
        error.clear();
        return true;
    }

    int captureCalls{0};
    int applyCalls{0};
    int rollbackCalls{0};

private:
    std::shared_ptr<FakeSessionSource> source_;
    FakeMode mode_{FakeMode::Normal};
    std::map<std::wstring, std::wstring> captured_;
    SeatId capturedSeat_{0};
};

RouteRequest request(std::wstring target = L"OUT-B") {
    RouteRequest result;
    result.seatId = 1;
    result.processTree = tree();
    result.targetEndpointId = std::move(target);
    return result;
}

void testWaitingThenSatisfiedWithoutMutation() {
    auto source = std::make_shared<FakeSessionSource>();
    auto inventory = std::make_shared<SessionInventory>(source);
    auto backend = std::make_shared<ObserveOnlyRouteBackend>();
    RouteTransaction transaction(request(), inventory, backend);

    std::string error;
    auto status = transaction.attempt(endpoints(), &error);
    check(status.phase == RoutePhase::WaitingForSession && error.empty(),
          "route waits without mutation when the game has not created an audio session yet");

    source->records.push_back(session(L"OUT-B", L"late-session", 100, 1000));
    status = transaction.attempt(endpoints(), &error);
    check(status.phase == RoutePhase::Satisfied &&
              status.capability == RouteCapability::SatisfiedWithoutMutation &&
              status.evidence.ownedSessions.size() == 1,
          "repeatable attempt observes a later-created owned session already on the target");
}

void testObserveOnlyIsExplicitlyUnsupportedOffTarget() {
    auto source = std::make_shared<FakeSessionSource>();
    source->records = {
        session(L"OUT-A", L"owned", 100, 1000),
        session(L"OUT-B", L"unrelated", 900, 9000),
    };
    auto inventory = std::make_shared<SessionInventory>(source);
    RouteTransaction transaction(request(), inventory,
                                 std::make_shared<ObserveOnlyRouteBackend>());

    std::string error;
    const auto status = transaction.attempt(endpoints(), &error);
    check(status.phase == RoutePhase::Unsupported &&
              status.error == RouteError::BackendUnsupported &&
              !status.mutated && !error.empty(),
          "public observe-only backend refuses to pretend it can move an arbitrary process session");
    check(source->records[0].endpointId == L"OUT-A" &&
              source->records[1].endpointId == L"OUT-B",
          "unsupported route leaves owned and unrelated sessions untouched");
}

void testExactProcessIdentityIsRequired() {
    auto source = std::make_shared<FakeSessionSource>();
    source->records = {session(L"OUT-A", L"pid-only", 100, 1000, false)};
    auto inventory = std::make_shared<SessionInventory>(source);
    RouteTransaction transaction(request(), inventory,
                                 std::make_shared<ObserveOnlyRouteBackend>());

    std::string error;
    auto status = transaction.attempt(endpoints(), &error);
    check(status.phase == RoutePhase::Failed &&
              status.error == RouteError::OwnershipUnverified,
          "PID match without creation-time identity fails closed");

    source->records[0] = session(L"OUT-A", L"shared-session", 100, 1000);
    source->records[0].spansMultipleProcesses = true;
    status = transaction.attempt(endpoints(), &error);
    check(status.phase == RoutePhase::Failed &&
              status.error == RouteError::OwnershipUnverified,
          "multi-process Core Audio session is not treated as an exactly owned mutable session");

    source->records[0] = session(L"OUT-A", L"pid-reused", 100, 9999);
    status = transaction.attempt(endpoints(), &error);
    check(status.phase == RoutePhase::WaitingForSession,
          "reused PID with a different creation time is not attributed to the HydraSeat process tree");
}

void testMutableApplyVerifyRollbackTouchesOnlyOwnedSessions() {
    auto source = std::make_shared<FakeSessionSource>();
    source->records = {
        session(L"OUT-A", L"root", 100, 1000),
        session(L"OUT-A", L"child", 101, 1001),
        session(L"OUT-A", L"unrelated", 900, 9000),
    };
    auto inventory = std::make_shared<SessionInventory>(source);
    auto backend = std::make_shared<FakeMutableBackend>(source);
    RouteTransaction transaction(request(), inventory, backend);

    std::string error;
    auto status = transaction.attempt(endpoints(), &error);
    check(status.phase == RoutePhase::Applied && status.mutated && error.empty(),
          "typed mutable backend applies and receiver observation verifies both owned sessions");
    check(source->records[0].endpointId == L"OUT-B" &&
              source->records[1].endpointId == L"OUT-B" &&
              source->records[2].endpointId == L"OUT-A",
          "apply moves only exact root/child sessions and leaves unrelated audio untouched");
    check(backend->captureCalls == 1 && backend->applyCalls == 1,
          "mutable transaction captures state before exactly one apply");

    status = transaction.rollback(endpoints(), &error);
    check(status.phase == RoutePhase::Ready && !status.mutated &&
              status.rollbackVerified && error.empty(),
          "rollback is receiver-verified against the captured endpoint evidence");
    check(source->records[0].endpointId == L"OUT-A" &&
              source->records[1].endpointId == L"OUT-A" &&
              source->records[2].endpointId == L"OUT-A",
          "rollback restores only owned session endpoint assignments");
}

void testApplyFailureRollsBackAndVerificationFailureIsContained() {
    {
        auto source = std::make_shared<FakeSessionSource>();
        source->records = {session(L"OUT-A", L"owned", 100, 1000)};
        auto inventory = std::make_shared<SessionInventory>(source);
        auto backend = std::make_shared<FakeMutableBackend>(
            source, FakeMode::FailAfterMutation);
        RouteTransaction transaction(request(), inventory, backend);
        std::string error;
        const auto status = transaction.attempt(endpoints(), &error);
        check(status.phase == RoutePhase::Failed &&
                  status.error == RouteError::ApplyFailed &&
                  status.rollbackVerified && !status.mutated,
              "partial apply failure performs and verifies rollback before returning ordinary failure");
        check(source->records[0].endpointId == L"OUT-A" &&
                  backend->rollbackCalls == 1,
              "partial apply failure leaves captured session on its original endpoint");
    }

    {
        auto source = std::make_shared<FakeSessionSource>();
        source->records = {session(L"OUT-A", L"owned", 100, 1000)};
        auto inventory = std::make_shared<SessionInventory>(source);
        auto backend = std::make_shared<FakeMutableBackend>(
            source, FakeMode::PretendApplied);
        RouteTransaction transaction(request(), inventory, backend);
        std::string error;
        const auto status = transaction.attempt(endpoints(), &error);
        check(status.phase == RoutePhase::Failed &&
                  status.error == RouteError::VerificationFailed &&
                  status.rollbackVerified && !status.mutated,
              "backend success without receiver-observed endpoint change is rejected and rolled back");
    }
}

void testRollbackFailureRequiresRecovery() {
    auto source = std::make_shared<FakeSessionSource>();
    source->records = {session(L"OUT-A", L"owned", 100, 1000)};
    auto inventory = std::make_shared<SessionInventory>(source);
    auto backend = std::make_shared<FakeMutableBackend>(
        source, FakeMode::RollbackFailure);
    RouteTransaction transaction(request(), inventory, backend);

    std::string error;
    auto status = transaction.attempt(endpoints(), &error);
    check(status.phase == RoutePhase::Applied,
          "rollback-failure fixture first reaches an applied route");
    status = transaction.rollback(endpoints(), &error);
    check(status.phase == RoutePhase::RecoveryRequired &&
              status.error == RouteError::RollbackFailed && status.mutated,
          "unverified rollback remains RecoveryRequired and retains mutation ownership");
}

void testRequestAndTargetValidation() {
    auto source = std::make_shared<FakeSessionSource>();
    source->records = {session(L"OUT-A", L"owned", 100, 1000)};
    auto inventory = std::make_shared<SessionInventory>(source);
    auto backend = std::make_shared<ObserveOnlyRouteBackend>();

    auto badSeat = request();
    badSeat.seatId = 2;
    RouteTransaction seatTransaction(badSeat, inventory, backend);
    check(seatTransaction.attempt(endpoints()).error == RouteError::InvalidSeat,
          "route Seat must equal owned process-tree Seat");

    RouteTransaction missing(request(L"MISSING"), inventory, backend);
    check(missing.attempt(endpoints()).error == RouteError::TargetEndpointMissing,
          "missing render endpoint fails before session mutation");

    RouteTransaction unavailable(request(L"OUT-OFF"), inventory, backend);
    check(unavailable.attempt(endpoints()).error == RouteError::TargetEndpointUnavailable,
          "unplugged target endpoint is not routed to");

    auto invalidTree = request();
    invalidTree.processTree.root.creationTime100ns = 0;
    RouteTransaction invalid(invalidTree, inventory, backend);
    check(invalid.attempt(endpoints()).error == RouteError::InvalidProcessTree,
          "route requires exact root process identity rather than PID alone");
}

void testSessionInventoryBoundsAndDeterminism() {
    auto source = std::make_shared<FakeSessionSource>();
    source->records = {
        session(L"OUT-B", L"z", 200, 2000),
        session(L"OUT-A", L"b", 100, 1000),
        session(L"OUT-A", L"A", 101, 1001),
    };
    SessionInventory inventory(source);
    std::string error;
    check(inventory.refresh(endpoints(), &error),
          "fake session inventory refresh succeeds");
    check(inventory.current() && inventory.current()->sessions.size() == 3 &&
              inventory.current()->sessions[0].sessionInstanceId == L"A" &&
              inventory.current()->sessions[1].sessionInstanceId == L"b" &&
              inventory.current()->sessions[2].endpointId == L"OUT-B",
          "session inventory order is deterministic and independent of source enumeration order");

    source->records = {
        session(L"OUT-A", L"same", 100, 1000),
        session(L"out-a", L"SAME", 101, 1001),
    };
    check(!inventory.refresh(endpoints(), &error),
          "case-equivalent endpoint/session-instance duplicate fails closed");

    source->records = {session(L"UNKNOWN", L"outside", 100, 1000)};
    check(!inventory.refresh(endpoints(), &error),
          "session outside supplied render endpoint inventory is rejected");

    source->records = {session(L"OUT-A", L"changing", 100, 1000)};
    source->changeEveryRead = true;
    source->reads = 0;
    check(!inventory.refresh(endpoints(), &error) && source->reads == 3,
          "continuously changing session source is bounded to three refresh attempts");
}

} // namespace

int main() {
    testWaitingThenSatisfiedWithoutMutation();
    testObserveOnlyIsExplicitlyUnsupportedOffTarget();
    testExactProcessIdentityIsRequired();
    testMutableApplyVerifyRollbackTouchesOnlyOwnedSessions();
    testApplyFailureRollsBackAndVerificationFailureIsContained();
    testRollbackFailureRequiresRecovery();
    testRequestAndTargetValidation();
    testSessionInventoryBoundsAndDeterminism();

    if (failures != 0) {
        std::cerr << failures << " audio routing test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Audio routing tests passed.\n";
    return EXIT_SUCCESS;
}
