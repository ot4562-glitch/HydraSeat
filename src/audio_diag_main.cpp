#include "hydra/audio_endpoint_inventory.hpp"

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

void usage() {
    std::cout
        << "HydraSeat read-only audio endpoint diagnostics\n"
        << "  --list   enumerate Core Audio render/capture endpoints\n"
        << "  --help   show this help\n"
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
    if (argument == "--help" || argument == "-h") {
        usage();
        return EXIT_SUCCESS;
    }
    usage();
    return EXIT_FAILURE;
}
