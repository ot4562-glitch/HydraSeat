#include "hydra/host_transport.hpp"
#include "hydra/runtime_host.hpp"
#include "hydra/workspace_manager.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void printUsage() {
    std::cout
        << "HydraSeat production runtime host skeleton\n\n"
        << "Usage:\n"
        << "  hydra_host --self-test\n"
        << "  hydra_host --snapshot-json\n"
        << "  hydra_host --serve [--profile <workspace_config.json>]\n"
        << "  hydra_host --profile <workspace_config.json> --plan-only\n\n"
        << "The host remains authoritative while UI clients connect and disconnect.\n";
}

void printSnapshotJson(const hydra::runtime::HostRuntimeSnapshot& snapshot) {
    std::cout << "{\"schema_version\":" << snapshot.schemaVersion
              << ",\"host_phase\":\""
              << hydra::runtime::hostLifecyclePhaseName(snapshot.hostPhase)
              << "\",\"session_phase\":\""
              << hydra::runtime::seatSessionPhaseName(snapshot.sessionPhase)
              << "\",\"session_id\":\""
              << hydra::runtime::runtimeSessionIdHex(snapshot.sessionId)
              << "\",\"generation\":" << snapshot.generation
              << ",\"profile_loaded\":"
              << (snapshot.profileLoaded ? "true" : "false")
              << ",\"connected_control_clients\":"
              << snapshot.connectedControlClients
              << ",\"seat_count\":" << snapshot.seats.size() << "}\n";
}

std::vector<hydra::SeatConfig> selfTestSeats() {
    hydra::SeatConfig first;
    first.seatId = 1;
    first.name = L"Self-test Seat 1";
    hydra::SeatConfig second;
    second.seatId = 2;
    second.name = L"Self-test Seat 2";
    return {first, second};
}

bool runSelfTest() {
    hydra::runtime::RuntimeHost host;
    host.controlClientConnected();
    if (!host.loadProfile(selfTestSeats(), 1).succeeded()) return false;
    if (!host.plan(2).succeeded()) return false;
    if (!host.prepare(3).succeeded()) return false;
    if (!host.start(4).succeeded()) return false;
    host.controlClientDisconnected();
    if (host.snapshot().sessionPhase != hydra::runtime::SeatSessionPhase::Active) {
        return false;
    }
    if (!host.stopAndReturnToWindows(5).succeeded()) return false;
    const auto exit = host.exitHostWhenIdle(6);
    return exit.succeeded() &&
           exit.snapshot.hostPhase == hydra::runtime::HostLifecyclePhase::ExitRequested &&
           exit.snapshot.sessionPhase == hydra::runtime::SeatSessionPhase::Idle;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
        if (!runSelfTest()) {
            std::cerr << "hydra_host self-test failed\n";
            return EXIT_FAILURE;
        }
        std::cout << "hydra_host self-test passed\n";
        return EXIT_SUCCESS;
    }

    hydra::runtime::RuntimeHost host;
    if (argc == 2 && std::string_view(argv[1]) == "--snapshot-json") {
        printSnapshotJson(host.snapshot());
        return EXIT_SUCCESS;
    }

    bool serve = false;
    bool planOnly = false;
    std::string profilePath;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--serve") {
            serve = true;
        } else if (argument == "--plan-only") {
            planOnly = true;
        } else if (argument == "--profile" && index + 1 < argc) {
            profilePath = argv[++index];
        } else {
            printUsage();
            return EXIT_FAILURE;
        }
    }

    if (!profilePath.empty()) {
        hydra::WorkspaceManager profiles;
        if (!profiles.loadFromFile(profilePath)) {
            std::cerr << "Profile validation failed: " << profiles.lastError() << '\n';
            return EXIT_FAILURE;
        }
        const auto loaded = host.loadProfile(profiles.getAllSeats(), 1);
        if (!loaded.succeeded()) {
            std::cerr << "Runtime profile rejection: " << loaded.diagnostic << '\n';
            return EXIT_FAILURE;
        }
    }

    if (planOnly) {
        if (profilePath.empty() || serve) {
            printUsage();
            return EXIT_FAILURE;
        }
        const auto planned = host.plan(2);
        printSnapshotJson(planned.snapshot);
        return planned.succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (serve) {
        hydra::hostipc::HostControlServer server(host);
        std::string error;
        if (!server.serve(&error)) {
            std::cerr << "Host IPC server failed: " << error << '\n';
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    printUsage();
    return argc == 1 ? EXIT_SUCCESS : EXIT_FAILURE;
}
