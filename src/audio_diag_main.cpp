#include "hydra/audio_endpoint_inventory.hpp"
#include "hydra/audio_routing.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

std::wstring stateText(std::uint32_t mask) {
    std::wstring result;
    const auto append = [&](const wchar_t* value) {
        if (!result.empty()) result += L",";
        result += value;
    };
    if ((mask & hydra::audio::kEndpointStateActive) != 0) append(L"active");
    if ((mask & hydra::audio::kEndpointStateDisabled) != 0) append(L"disabled");
    if ((mask & hydra::audio::kEndpointStateNotPresent) != 0) append(L"not-present");
    if ((mask & hydra::audio::kEndpointStateUnplugged) != 0) append(L"unplugged");
    return result;
}

std::wstring defaultRolesText(std::uint8_t mask) {
    std::wstring result;
    const auto append = [&](const wchar_t* value) {
        if (!result.empty()) result += L",";
        result += value;
    };
    if ((mask & hydra::audio::kDefaultRoleConsole) != 0) append(L"console");
    if ((mask & hydra::audio::kDefaultRoleMultimedia) != 0) append(L"multimedia");
    if ((mask & hydra::audio::kDefaultRoleCommunications) != 0) append(L"communications");
    return result.empty() ? L"-" : result;
}

const wchar_t* sessionStateText(hydra::audio::SessionState state) {
    switch (state) {
        case hydra::audio::SessionState::Inactive: return L"inactive";
        case hydra::audio::SessionState::Active: return L"active";
        case hydra::audio::SessionState::Expired: return L"expired";
    }
    return L"unknown";
}

void usage() {
    std::cout
        << "HydraSeat read-only audio endpoint diagnostics\n"
        << "  --list       enumerate Core Audio render/capture endpoints\n"
        << "  --sessions   enumerate render sessions and exact process identity where readable\n"
        << "  --help       show this help\n"
        << "\n"
        << "This tool does not change default devices, volume, or per-process routing.\n";
}

int listEndpoints() {
    hydra::audio::EndpointInventory inventory(
        hydra::audio::makeNativeEndpointSource());
    std::string error;
    if (!inventory.refresh(&error) || !inventory.current()) {
        std::cerr << "audio inventory failed: " << error << '\n';
        return EXIT_FAILURE;
    }

    const auto& snapshot = *inventory.current();
    std::cout << "notifications="
              << (snapshot.notificationsAvailable ? "available" : "unavailable")
              << " generation=" << snapshot.sourceGeneration
              << " endpoint_count=" << snapshot.endpoints.size() << '\n';
    for (const auto& endpoint : snapshot.endpoints) {
        std::wcout << L"flow="
                   << (endpoint.flow == hydra::audio::DataFlow::Render
                           ? L"render" : L"capture")
                   << L" state=" << stateText(endpoint.stateMask)
                   << L" default=" << defaultRolesText(endpoint.defaultRoleMask)
                   << L" id=" << endpoint.endpointId
                   << L" name=" << endpoint.friendlyName << L'\n';
    }
    return EXIT_SUCCESS;
}

int listSessions() {
    hydra::audio::EndpointInventory endpointInventory(
        hydra::audio::makeNativeEndpointSource());
    std::string error;
    if (!endpointInventory.refresh(&error) || !endpointInventory.current()) {
        std::cerr << "audio inventory failed: " << error << '\n';
        return EXIT_FAILURE;
    }

    hydra::audio::SessionInventory sessionInventory(
        hydra::audio::makeNativeSessionSource());
    if (!sessionInventory.refresh(*endpointInventory.current(), &error) ||
        !sessionInventory.current()) {
        std::cerr << "audio session inventory failed: " << error << '\n';
        return EXIT_FAILURE;
    }

    const auto& snapshot = *sessionInventory.current();
    std::cout << "session_count=" << snapshot.sessions.size() << '\n';
    for (const auto& session : snapshot.sessions) {
        std::wcout << L"state=" << sessionStateText(session.state)
                   << L" pid=" << session.processId
                   << L" exact_identity="
                   << (session.processIdentityVerified ? L"yes" : L"no")
                   << L" multi_process="
                   << (session.spansMultipleProcesses ? L"yes" : L"no")
                   << L" system="
                   << (session.systemSoundsSession ? L"yes" : L"no")
                   << L" endpoint=" << session.endpointId
                   << L" instance=" << session.sessionInstanceId
                   << L" process=" << session.executablePath << L'\n';
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        usage();
        return EXIT_SUCCESS;
    }
    if (argc != 2) {
        usage();
        return EXIT_FAILURE;
    }
    const std::string argument = argv[1];
    if (argument == "--list") return listEndpoints();
    if (argument == "--sessions") return listSessions();
    if (argument == "--help" || argument == "-h") {
        usage();
        return EXIT_SUCCESS;
    }
    usage();
    return EXIT_FAILURE;
}
