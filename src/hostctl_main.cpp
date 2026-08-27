#include "hydra/host_transport.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using hydra::hostipc::ClientRole;
using hydra::hostipc::HostControlClient;
using hydra::hostipc::MessageType;

std::string jsonEscape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8u);
    for (const char ch : value) {
        switch (ch) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) >= 0x20u) result.push_back(ch);
                break;
        }
    }
    return result;
}

void printSnapshot(const hydra::runtime::HostRuntimeSnapshot& snapshot, bool json) {
    if (json) {
        std::cout << "{\"schema_version\":" << snapshot.schemaVersion
                  << ",\"host_phase\":\""
                  << hydra::runtime::hostLifecyclePhaseName(snapshot.hostPhase)
                  << "\",\"session_phase\":\""
                  << hydra::runtime::seatSessionPhaseName(snapshot.sessionPhase)
                  << "\",\"session_id\":\""
                  << hydra::runtime::runtimeSessionIdHex(snapshot.sessionId)
                  << "\",\"generation\":" << snapshot.generation
                  << ",\"transition_sequence\":" << snapshot.transitionSequence
                  << ",\"profile_loaded\":" << (snapshot.profileLoaded ? "true" : "false")
                  << ",\"mutation_in_progress\":"
                  << (snapshot.mutationInProgress ? "true" : "false")
                  << ",\"connected_control_clients\":"
                  << snapshot.connectedControlClients
                  << ",\"seat_count\":" << snapshot.seats.size()
                  << ",\"diagnostic\":\"" << jsonEscape(snapshot.diagnostic) << "\"}\n";
        return;
    }
    std::cout << "host=" << hydra::runtime::hostLifecyclePhaseName(snapshot.hostPhase)
              << " session=" << hydra::runtime::seatSessionPhaseName(snapshot.sessionPhase)
              << " id=" << hydra::runtime::runtimeSessionIdHex(snapshot.sessionId)
              << " generation=" << snapshot.generation
              << " seq=" << snapshot.transitionSequence
              << " seats=" << snapshot.seats.size()
              << " clients=" << snapshot.connectedControlClients;
    if (!snapshot.diagnostic.empty()) std::cout << " diagnostic=\"" << snapshot.diagnostic << '"';
    std::cout << '\n';
}

void printResult(const hydra::runtime::RuntimeCommandResult& result, bool json) {
    if (json) {
        std::cout << "{\"result\":\""
                  << hydra::runtime::runtimeResultCodeName(result.code)
                  << "\",\"diagnostic\":\"" << jsonEscape(result.diagnostic)
                  << "\",\"snapshot\":";
        const auto& s = result.snapshot;
        std::cout << "{\"host_phase\":\""
                  << hydra::runtime::hostLifecyclePhaseName(s.hostPhase)
                  << "\",\"session_phase\":\""
                  << hydra::runtime::seatSessionPhaseName(s.sessionPhase)
                  << "\",\"session_id\":\""
                  << hydra::runtime::runtimeSessionIdHex(s.sessionId)
                  << "\",\"generation\":" << s.generation
                  << ",\"transition_sequence\":" << s.transitionSequence
                  << "}}\n";
        return;
    }
    std::cout << hydra::runtime::runtimeResultCodeName(result.code)
              << ": " << result.diagnostic << '\n';
    printSnapshot(result.snapshot, false);
}

void usage() {
    std::cout
        << "HydraSeat host control client\n\n"
        << "Usage: hydra_hostctl [--json] <command>\n"
        << "Commands: snapshot, plan, start, stop, reconfigure, reset, exit, ping, watch\n";
}

std::optional<MessageType> mutationFor(std::string_view command) {
    if (command == "plan") return MessageType::PlanSession;
    if (command == "start") return MessageType::StartSession;
    if (command == "stop") return MessageType::StopAndReturnToWindows;
    if (command == "reconfigure") return MessageType::BeginReconfigure;
    if (command == "reset") return MessageType::EmergencyReset;
    if (command == "exit") return MessageType::ExitHostWhenIdle;
    return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
    bool json = false;
    int index = 1;
    if (index < argc && std::string_view(argv[index]) == "--json") {
        json = true;
        ++index;
    }
    if (index >= argc || index + 1 != argc) {
        usage();
        return EXIT_FAILURE;
    }
    const std::string_view command(argv[index]);
    const bool mutating = mutationFor(command).has_value();
    if (!mutating && command != "snapshot" && command != "ping" && command != "watch") {
        usage();
        return EXIT_FAILURE;
    }

    HostControlClient client;
    std::string error;
    if (!client.connect(mutating ? ClientRole::Control : ClientRole::ReadOnly,
                        hydra::hostipc::kDefaultHostIpcTimeoutMs, &error)) {
        std::cerr << "host connection failed: " << error << '\n';
        return EXIT_FAILURE;
    }

    if (command == "snapshot") {
        const auto snapshot = client.getSnapshot(hydra::hostipc::kDefaultHostIpcTimeoutMs,
                                                 &error);
        if (!snapshot) {
            std::cerr << "snapshot failed: " << error << '\n';
            return EXIT_FAILURE;
        }
        printSnapshot(*snapshot, json);
        return EXIT_SUCCESS;
    }
    if (command == "ping") {
        if (!client.ping(0x4859445241534541ull,
                         hydra::hostipc::kDefaultHostIpcTimeoutMs, &error)) {
            std::cerr << "ping failed: " << error << '\n';
            return EXIT_FAILURE;
        }
        std::cout << (json ? "{\"pong\":true}\n" : "pong\n");
        return EXIT_SUCCESS;
    }
    if (command == "watch") {
        const auto snapshot = client.getSnapshot(hydra::hostipc::kDefaultHostIpcTimeoutMs,
                                                 &error);
        if (!snapshot) {
            std::cerr << "initial snapshot failed: " << error << '\n';
            return EXIT_FAILURE;
        }
        printSnapshot(*snapshot, json);
        const auto subscription = client.beginSubscription(
            snapshot->transitionSequence, static_cast<std::uint32_t>(hydra::hostipc::kHostProtocolMaxEvents),
            hydra::hostipc::kDefaultHostIpcTimeoutMs, &error);
        if (!subscription) {
            std::cerr << "subscription failed: " << error << '\n';
            return EXIT_FAILURE;
        }
        const auto event = client.readSubscriptionEvent(30000u, &error);
        if (event.error) {
            std::cerr << "subscription requires resnapshot: " << event.error->diagnostic << '\n';
            return EXIT_FAILURE;
        }
        if (!event.event) {
            std::cerr << "subscription event failed: " << error << '\n';
            return EXIT_FAILURE;
        }
        if (json) {
            std::cout << "{\"event_sequence\":" << event.event->sequence
                      << ",\"command\":\""
                      << hydra::runtime::runtimeCommandName(event.event->command)
                      << "\",\"result\":\""
                      << hydra::runtime::runtimeResultCodeName(event.event->result)
                      << "\"}\n";
        } else {
            std::cout << "event seq=" << event.event->sequence
                      << " command=" << hydra::runtime::runtimeCommandName(event.event->command)
                      << " result=" << hydra::runtime::runtimeResultCodeName(event.event->result)
                      << '\n';
        }
        return EXIT_SUCCESS;
    }

    std::optional<hydra::hostipc::ErrorPayload> protocolError;
    const auto result = client.command(*mutationFor(command),
                                       hydra::hostipc::kDefaultHostIpcTimeoutMs,
                                       &error, &protocolError);
    if (!result) {
        if (protocolError) {
            std::cerr << hydra::hostipc::errorCodeName(protocolError->code)
                      << ": " << protocolError->diagnostic << '\n';
        } else {
            std::cerr << "host command failed: " << error << '\n';
        }
        return EXIT_FAILURE;
    }
    printResult(*result, json);
    return result->succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
}
