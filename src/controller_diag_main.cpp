#include "hydra/controller_runtime.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

const wchar_t* yesNo(bool value) noexcept {
    return value ? L"yes" : L"no";
}

const wchar_t* qualityName(hydra::controller::IdentityQuality quality) noexcept {
    switch (quality) {
        case hydra::controller::IdentityQuality::RuntimeOnly:
            return L"runtime-only";
        case hydra::controller::IdentityQuality::Stable:
            return L"stable";
    }
    return L"unknown";
}

std::wstring apiName(hydra::controller::ApiSurface api) {
    switch (api) {
        case hydra::controller::ApiSurface::XInput: return L"xinput";
        case hydra::controller::ApiSurface::DirectInput: return L"directinput";
        case hydra::controller::ApiSurface::GameInput: return L"gameinput";
    }
    return L"unknown";
}

void usage() {
    std::cout
        << "HydraSeat read-only controller diagnostics\n"
        << "  --list   scan current XInput/DirectInput controller sources\n"
        << "  --help   show this help\n"
        << "\n"
        << "XInput slot numbers are runtime hints only and are not stable persistent IDs.\n"
        << "This diagnostic never sends vibration or changes controller policy.\n";
}

int listSources() {
    auto backend = hydra::controller::makeNativeControllerSourceBackend();
    hydra::controller::PollWorker worker(backend);
    std::string error;
    if (!worker.pollOnce(&error)) {
        std::cerr << "controller scan failed: " << error << '\n';
        return EXIT_FAILURE;
    }

    const auto snapshot = worker.snapshot();
    std::cout << "generation=" << snapshot.generation
              << " poll_sequence=" << snapshot.pollSequence
              << " source_count=" << snapshot.sources.size() << '\n';
    for (const auto& source : snapshot.sources) {
        const std::wstring persistent =
            source.persistentId ? *source.persistentId : L"-";
        const std::wstring slot =
            source.runtimeXInputSlotHint == hydra::gatec::kNoRuntimeXInputSlot
                ? L"-"
                : std::to_wstring(source.runtimeXInputSlotHint);
        std::wcout
            << L"api=" << apiName(source.api)
            << L" identity=" << qualityName(source.identityQuality)
            << L" connected=" << yesNo(source.connected)
            << L" state=" << yesNo(source.stateAvailable)
            << L" vibration=" << yesNo(source.vibrationSupported)
            << L" source_generation=" << source.sourceGeneration
            << L" xinput_slot=" << slot
            << L" persistent_id=" << persistent
            << L" runtime_key="
            << std::wstring(source.runtimeKey.begin(), source.runtimeKey.end())
            << L" name=" << source.displayName << L'\n';
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
    if (argument == "--list") return listSources();
    if (argument == "--help" || argument == "-h") {
        usage();
        return EXIT_SUCCESS;
    }
    usage();
    return EXIT_FAILURE;
}
