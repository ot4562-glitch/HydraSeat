#include "hydra/hidhide_probe.hpp"
#include "hydra/hidhide_session_backend.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

class ReadOnlySessionPlatform final : public hydra::HidHideSessionPlatform {
public:
    bool readState(hydra::HidHideSessionSnapshot& snapshot,
                   std::string& error) noexcept override {
        snapshot = state_;
        error.clear();
        return true;
    }

    bool writeState(const hydra::HidHideSessionSnapshot&,
                    std::string& error) noexcept override {
        error = "native HidHide mutation is intentionally unavailable in the automated lab";
        return false;
    }

    bool addSessionBlacklist(std::span<const std::wstring>,
                             std::string& error) noexcept override {
        error = "native HidHide session blacklist is intentionally unavailable in the automated lab";
        return false;
    }

    bool clearSessionBlacklist(std::string& error) noexcept override {
        error = "native HidHide session blacklist is intentionally unavailable in the automated lab";
        return false;
    }

    bool mutationSupported() const noexcept override { return false; }
    bool sessionBlacklistSupported() const noexcept override { return false; }

private:
    hydra::HidHideSessionSnapshot state_{};
};

int runSelfTest() {
    auto platform = std::make_shared<ReadOnlySessionPlatform>();
    hydra::HidHideSessionTransaction transaction(platform);

    hydra::HidHideSessionRequest request;
    request.deviceInstanceIds = {L"HID\\HYDRASEAT_SYNTHETIC_DEVICE"};
    request.allowedApplications = {
        L"\\Device\\HarddiskVolume3\\HydraSeat\\hydra_host.exe"};
    request.replacementPathVerified = true;
    request.recoveryReady = true;
    request.spareRecoveryInputPresent = false;
    request.expiryMilliseconds = 5'000;
    request.generation = 1;

    const auto prepared = transaction.prepare(request, 1'000);
    if (!prepared.succeeded() || prepared.phase != hydra::HidHideSessionPhase::Prepared) {
        std::cerr << "HidHide session lab self-test failed during read-only prepare.\n";
        return EXIT_FAILURE;
    }
    const auto blocked = transaction.activate(1'100);
    if (blocked.code != hydra::HidHideSessionResultCode::PhysicalGateRequired ||
        transaction.phase() != hydra::HidHideSessionPhase::Prepared) {
        std::cerr << "HidHide session lab self-test failed to preserve the physical gate.\n";
        return EXIT_FAILURE;
    }
    if (!transaction.rollback().succeeded() ||
        transaction.phase() != hydra::HidHideSessionPhase::Idle) {
        std::cerr << "HidHide session lab self-test failed to cancel the dry-run plan.\n";
        return EXIT_FAILURE;
    }

    std::cout << "HidHide session lab self-test passed; native mutation remained disabled.\n";
    return EXIT_SUCCESS;
}

int showStatus() {
    const auto report = hydra::probeHidHide();
    std::cout << hydra::formatHidHideProbeReport(report);
    std::cout << "session_transaction: available\n";
    std::cout << "native_mutation: disabled_pending_physical_gate\n";
    std::cout << "physical_device_cloaking_claim: unvalidated\n";
    return EXIT_SUCCESS;
}

void usage() {
    std::cout
        << "HydraSeat guarded HidHide session lab\n"
        << "  --status     read-only HidHide capability/session status\n"
        << "  --self-test  synthetic transaction/gate self-test\n"
        << "\n"
        << "Native device-list mutation is intentionally not exposed while "
           "P3-HW-01 physical acceptance is deferred.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        usage();
        return argc == 1 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    const std::string argument = argv[1];
    if (argument == "--status") return showStatus();
    if (argument == "--self-test") return runSelfTest();
    if (argument == "--help" || argument == "-h") {
        usage();
        return EXIT_SUCCESS;
    }
    usage();
    return EXIT_FAILURE;
}
