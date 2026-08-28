#include "hydra/host_transport.hpp"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace {

using hydra::hostipc::ClientRole;
using hydra::hostipc::HostControlClient;
using hydra::hostipc::MessageType;
using hydra::SeatId;

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
                  << ",\"whole_machine_return_requested\":"
                  << (snapshot.wholeMachineReturnRequested ? "true" : "false")
                  << ",\"seat_games\":[";
        for (std::size_t index = 0; index < snapshot.seatGames.size(); ++index) {
            const auto& seat = snapshot.seatGames[index];
            if (index != 0u) std::cout << ',';
            std::cout << "{\"seat_id\":" << seat.seatId
                      << ",\"phase\":\""
                      << hydra::runtime::seatGamePhaseName(seat.phase)
                      << "\",\"generation\":" << seat.generation
                      << ",\"diagnostic\":\"" << jsonEscape(seat.diagnostic) << "\"}";
        }
        std::cout << ']'
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
    for (const auto& seat : snapshot.seatGames) {
        std::cout << " seat" << seat.seatId << '='
                  << hydra::runtime::seatGamePhaseName(seat.phase);
    }
    if (snapshot.wholeMachineReturnRequested) {
        std::cout << " whole-machine-return=requested";
    }
    if (!snapshot.diagnostic.empty()) std::cout << " diagnostic=\"" << snapshot.diagnostic << '"';
    std::cout << '\n';
}

void printSeatResult(const hydra::runtime::SeatGameCommandResult& result, bool json) {
    if (json) {
        std::cout << "{\"result\":\""
                  << hydra::runtime::seatGameResultCodeName(result.code)
                  << "\",\"whole_machine_return_requested\":"
                  << (result.wholeMachineReturnRequested ? "true" : "false")
                  << ",\"diagnostic\":\"" << jsonEscape(result.diagnostic)
                  << "\",\"seats\":[";
        for (std::size_t index = 0; index < result.seats.size(); ++index) {
            const auto& seat = result.seats[index];
            if (index != 0u) std::cout << ',';
            std::cout << "{\"seat_id\":" << seat.seatId
                      << ",\"phase\":\""
                      << hydra::runtime::seatGamePhaseName(seat.phase)
                      << "\",\"generation\":" << seat.generation
                      << ",\"diagnostic\":\"" << jsonEscape(seat.diagnostic) << "\"}";
        }
        std::cout << "]}\n";
        return;
    }
    std::cout << hydra::runtime::seatGameResultCodeName(result.code)
              << ": " << result.diagnostic;
    for (const auto& seat : result.seats) {
        std::cout << " seat" << seat.seatId << '='
                  << hydra::runtime::seatGamePhaseName(seat.phase);
    }
    if (result.wholeMachineReturnRequested) {
        std::cout << " whole-machine-return=requested";
    }
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
        << "Usage: hydra_hostctl [--json] <command> [arguments]\n"
        << "Commands:\n"
        << "  snapshot | plan | start | stop | reconfigure | reset | exit | ping | watch\n"
        << "  seat-assign <seat-id> <player-id> <game-id>\n"
        << "  seat-start <seat-id> | seat-stop <seat-id> | seat-reconcile\n";
}

std::optional<MessageType> seatMutationFor(std::string_view command) {
    if (command == "seat-assign") return MessageType::AssignSeatGame;
    if (command == "seat-start") return MessageType::StartSeatGame;
    if (command == "seat-stop") return MessageType::StopSeatGame;
    return std::nullopt;
}

std::optional<SeatId> parseSeatId(std::string_view text) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value == 0 || value > std::numeric_limits<SeatId>::max()) {
        return std::nullopt;
    }
    return static_cast<SeatId>(value);
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
    if (index >= argc) {
        usage();
        return EXIT_FAILURE;
    }
    const std::string_view command(argv[index]);
    const auto globalMutation = mutationFor(command);
    const auto seatMutation = seatMutationFor(command);
    const bool reconcile = command == "seat-reconcile";
    const bool mutating = globalMutation.has_value() || seatMutation.has_value() || reconcile;
    const int argumentCount = argc - index - 1;
    const bool validArguments = command == "seat-assign" ? argumentCount == 3
        : (command == "seat-start" || command == "seat-stop") ? argumentCount == 1
        : argumentCount == 0;
    if (!validArguments ||
        (!mutating && command != "snapshot" && command != "ping" && command != "watch")) {
        usage();
        return EXIT_FAILURE;
    }

    SeatId seatId = 0;
    if (seatMutation) {
        const auto parsed = parseSeatId(argv[index + 1]);
        if (!parsed) {
            std::cerr << "invalid nonzero Seat ID\n";
            return EXIT_FAILURE;
        }
        seatId = *parsed;
    }

    HostControlClient client;
    std::string error;
    if (mutating) {
        if (!client.connect(ClientRole::ReadOnly,
                            hydra::hostipc::kDefaultHostIpcTimeoutMs, &error)) {
            std::cerr << "host authority discovery failed: " << error << '\n';
            return EXIT_FAILURE;
        }
        const auto authority = client.getSnapshot(
            hydra::hostipc::kDefaultHostIpcTimeoutMs, &error);
        client.close();
        if (!authority || authority->managementSeatId == 0) {
            std::cerr << "host authority discovery failed: " << error << '\n';
            return EXIT_FAILURE;
        }
        if (!client.connectForSeat(ClientRole::Control, authority->managementSeatId,
                                   hydra::hostipc::kDefaultHostIpcTimeoutMs, &error)) {
            std::cerr << "host control connection failed: " << error << '\n';
            return EXIT_FAILURE;
        }
    } else if (!client.connect(ClientRole::ReadOnly,
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
                      << "\",\"seat_id\":" << event.event->seatId
                      << "}\n";
        } else {
            std::cout << "event seq=" << event.event->sequence
                      << " command=" << hydra::runtime::runtimeCommandName(event.event->command)
                      << " result=" << hydra::runtime::runtimeResultCodeName(event.event->result)
                      << " seat=" << event.event->seatId
                      << '\n';
        }
        return EXIT_SUCCESS;
    }

    if (seatMutation || reconcile) {
        std::optional<hydra::hostipc::ErrorPayload> protocolError;
        std::optional<hydra::runtime::SeatGameCommandResult> result;
        if (reconcile) {
            result = client.reconcileSeatGames(
                hydra::hostipc::kDefaultHostIpcTimeoutMs, &error, &protocolError);
        } else {
            hydra::hostipc::SeatGameCommandPayload payload;
            payload.seatId = seatId;
            if (command == "seat-assign") {
                const std::string_view player(argv[index + 2]);
                const std::string_view game(argv[index + 3]);
                if (player.empty() || game.empty() ||
                    player.size() > hydra::runtime::kSeatGameIdentifierMaxBytes ||
                    game.size() > hydra::runtime::kSeatGameIdentifierMaxBytes) {
                    std::cerr << "Player/Game identifiers must be nonempty and bounded\n";
                    return EXIT_FAILURE;
                }
                payload.binding = hydra::runtime::SeatGameBinding{
                    std::string(player), std::string(game)};
            }
            result = client.seatGameCommand(
                *seatMutation, payload, hydra::hostipc::kDefaultHostIpcTimeoutMs,
                &error, &protocolError);
        }
        if (!result) {
            if (protocolError) {
                std::cerr << hydra::hostipc::errorCodeName(protocolError->code)
                          << ": " << protocolError->diagnostic << '\n';
            } else {
                std::cerr << "Seat command failed: " << error << '\n';
            }
            return EXIT_FAILURE;
        }
        printSeatResult(*result, json);
        return result->succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    std::optional<hydra::hostipc::ErrorPayload> protocolError;
    const auto result = client.command(*globalMutation,
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
