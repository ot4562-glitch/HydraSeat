#include "hydra/steam_provider.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace hydra;
using namespace hydra::provider;
using namespace hydra::provider::steam;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string libraryFolders() {
    return R"vdf("libraryfolders"
{
    "0" { "path" "C:\\Steam" "apps" { "100" "1" } }
    "1" { "path" "D:\\SteamLibrary" "apps" { "200" "1" } }
})vdf";
}

std::string manifest(std::string appId,
                     std::string name,
                     std::string installDir,
                     std::string buildId) {
    return "\"AppState\"\n{\n"
           "\"appid\" \"" + appId + "\"\n"
           "\"name\" \"" + name + "\"\n"
           "\"installdir\" \"" + installDir + "\"\n"
           "\"buildid\" \"" + buildId + "\"\n"
           "}\n";
}

class FakeSteamSource final : public SteamMetadataSource {
public:
    SteamSourceResult locateResult{SteamSourceResult::Success};
    std::wstring root{L"C:\\Steam"};
    std::map<std::wstring, std::string> files;
    std::vector<std::wstring> manifests;
    std::map<std::wstring, std::vector<std::wstring>> executables;
    std::vector<ProviderProcessEvidence> observedProcesses;
    bool failRead{false};
    bool failManifestList{false};
    bool failExecutableList{false};
    bool failProcessObservation{false};
    int processObservationCalls{0};

    SteamSourceResult locateInstallation(std::wstring& steamRoot,
                                         std::string& error) noexcept override {
        if (locateResult == SteamSourceResult::Success) steamRoot = root;
        else error = locateResult == SteamSourceResult::NotInstalled
                         ? "Steam is absent" : "Steam discovery failed";
        return locateResult;
    }

    bool readTextFile(const std::wstring& path,
                      std::size_t maximumBytes,
                      std::string& bytes,
                      std::string& error) noexcept override {
        if (failRead || !files.contains(path)) {
            error = "fixture file missing";
            return false;
        }
        if (files[path].size() > maximumBytes) {
            error = "fixture file too large";
            return false;
        }
        bytes = files[path];
        return true;
    }

    bool listManifestFiles(std::span<const std::wstring> roots,
                           std::vector<std::wstring>& paths,
                           std::string& error) noexcept override {
        if (failManifestList || roots.size() != 2u) {
            error = "fixture manifest enumeration failed";
            return false;
        }
        paths = manifests;
        return true;
    }

    bool listExecutableHints(const std::wstring& installRoot,
                             std::vector<std::wstring>& paths,
                             std::string& error) noexcept override {
        if (failExecutableList || !executables.contains(installRoot)) {
            error = "fixture executable enumeration failed";
            return false;
        }
        paths = executables[installRoot];
        return true;
    }

    bool observeProcesses(std::span<const std::wstring> executablePaths,
                          std::vector<ProviderProcessEvidence>& processes,
                          std::string& error) noexcept override {
        ++processObservationCalls;
        if (failProcessObservation || executablePaths.empty()) {
            error = "fixture process observation failed";
            return false;
        }
        processes = observedProcesses;
        return true;
    }
};

std::shared_ptr<FakeSteamSource> validSource() {
    auto source = std::make_shared<FakeSteamSource>();
    source->files[L"C:\\Steam\\steamapps\\libraryfolders.vdf"] = libraryFolders();
    source->manifests = {
        L"D:\\SteamLibrary\\steamapps\\appmanifest_200.acf",
        L"C:\\Steam\\steamapps\\appmanifest_100.acf",
    };
    source->files[source->manifests[0]] = manifest("200", "Second Game", "Second", "22");
    source->files[source->manifests[1]] = manifest("100", "First Game", "First", "11");
    source->executables[L"C:\\Steam\\steamapps\\common\\First"] = {
        L"C:\\Steam\\steamapps\\common\\First\\first.exe"};
    source->executables[L"D:\\SteamLibrary\\steamapps\\common\\Second"] = {
        L"D:\\SteamLibrary\\steamapps\\common\\Second\\launcher.exe",
        L"D:\\SteamLibrary\\steamapps\\common\\Second\\second.exe"};
    source->observedProcesses = {
        {321u, 654u, L"C:\\Steam\\steamapps\\common\\First\\first.exe", true}};
    return source;
}

void testReadOnlyMultiLibraryDiscoveryAndDeterminism() {
    auto source = validSource();
    SteamProviderAdapter adapter(source);
    const auto descriptor = adapter.descriptor();
    check(descriptor.providerId == "steam" &&
              descriptor.availability == ProviderAvailability::Available &&
              descriptor.metadataRevision != 0u &&
              descriptor.capabilities.installedGameDiscovery &&
              descriptor.capabilities.launchRequests &&
              descriptor.capabilities.processIdentification &&
              !descriptor.capabilities.accountReferences &&
              !descriptor.capabilities.offlineLaunch,
          "Steam descriptor exposes only the implemented lawful capabilities");

    std::vector<catalog::GameCatalogCandidate> games;
    check(discoverInstalledGames(adapter, games).succeeded() && games.size() == 2u,
          "Steam fixture discovers two apps across two local libraries");
    if (games.size() == 2u) {
        check(games[0].providerAppId == std::optional<std::string>("100") &&
                  games[0].title == L"First Game" &&
                  games[0].installRoot == L"C:\\Steam\\steamapps\\common\\First" &&
                  games[0].localVersion == std::optional<std::wstring>(L"11") &&
                  games[0].localIconSource ==
                      std::optional<std::wstring>(
                          L"C:\\Steam\\steamapps\\common\\First\\first.exe") &&
                  games[1].providerAppId == std::optional<std::string>("200"),
              "Steam manifest fields become bounded provider-neutral catalog candidates");
    }

    const auto firstRevision = descriptor.metadataRevision;
    const auto firstGames = games;
    check(adapter.refresh().succeeded() &&
              adapter.descriptor().metadataRevision == firstRevision &&
              discoverInstalledGames(adapter, games).succeeded() && games == firstGames,
          "unchanged Steam fixture produces deterministic revision and discovery output");
}

void testSupportedLaunchAndPolicyBoundaries() {
    auto source = validSource();
    SteamProviderAdapter adapter(source);
    std::vector<catalog::GameCatalogCandidate> candidates;
    check(discoverInstalledGames(adapter, candidates).succeeded() && !candidates.empty(),
          "Steam launch test has a discovered game");
    catalog::LocalGameCatalog catalog;
    check(catalog::buildLocalGameCatalog(
              std::span<const catalog::GameCatalogCandidate>(&candidates[0], 1u), catalog)
              .succeeded(),
          "Steam launch test resolves stable game identity");
    if (catalog.entries.empty()) return;

    LaunchSelection selection{
        "steam", catalog.entries[0].game.gameId, "100", std::nullopt,
        adapter.descriptor().metadataRevision, {}};
    ProviderLaunchRequest request;
    check(buildLaunchRequest(adapter, selection, request).succeeded() &&
              request.targetKind == LaunchTargetKind::ProviderUri &&
              request.target == L"steam://run/100" && request.arguments.empty() &&
              !request.accountRef,
          "Steam adapter constructs the normal typed steam run URI without launching it");

    auto accountSelection = selection;
    accountSelection.accountRef = "account-a";
    check(buildLaunchRequest(adapter, accountSelection, request).result ==
              ProviderResult::UnsupportedOperation,
          "Steam adapter does not claim account selection or handle credentials");

    auto argumentSelection = selection;
    argumentSelection.instanceArguments = {L"--second-instance"};
    check(buildLaunchRequest(adapter, argumentSelection, request).result ==
              ProviderResult::UnsupportedOperation,
          "Steam adapter refuses undeclared instance arguments and multi-instance policy guesses");

    std::vector<profile::ProviderAccountReference> accounts;
    check(listAccountReferences(adapter, accounts).result ==
              ProviderResult::UnsupportedOperation,
          "Steam account-reference enumeration is explicitly unsupported");
}

void testProcessCandidatesRemainUnverified() {
    auto source = validSource();
    SteamProviderAdapter adapter(source);
    std::vector<catalog::GameCatalogCandidate> candidates;
    check(discoverInstalledGames(adapter, candidates).succeeded() && !candidates.empty(),
          "Steam process test has a discovered game");
    catalog::LocalGameCatalog catalog;
    if (candidates.empty() ||
        !catalog::buildLocalGameCatalog(
             std::span<const catalog::GameCatalogCandidate>(&candidates[0], 1u), catalog)
             .succeeded() || catalog.entries.empty()) {
        return;
    }
    const auto revision = adapter.descriptor().metadataRevision;
    const ProcessIdentificationQuery query{
        "steam", catalog.entries[0].game.gameId, "100",
        "steam-100-" + std::to_string(revision), revision};
    std::vector<ProviderProcessEvidence> processes;
    check(identifyProcesses(adapter, query, processes).succeeded() &&
              processes.size() == 1u && !processes[0].providerRelationshipVerified &&
              source->processObservationCalls == 1,
          "Steam path/PID/creation observation stays explicitly unverified as provider causality");
}

void testMalformedAndStaleMetadataFailsClosed() {
    auto source = validSource();
    SteamProviderAdapter adapter(source);
    std::vector<catalog::GameCatalogCandidate> games;
    check(discoverInstalledGames(adapter, games).succeeded(),
          "sentinel Steam catalog is available");
    const auto original = games;
    const auto oldRevision = adapter.descriptor().metadataRevision;

    source->files[source->manifests[0]] = manifest("200", "Changed", "Second", "23");
    check(adapter.refresh().succeeded() &&
              adapter.descriptor().metadataRevision != oldRevision,
          "material Steam metadata change advances deterministic revision");
    ProviderLaunchRequest request;
    LaunchSelection stale{"steam", "game:stale", "100", std::nullopt, oldRevision, {}};
    check(buildLaunchRequest(adapter, stale, request).result == ProviderResult::StaleMetadata,
          "old Steam selection is rejected before adapter launch planning");

    source = validSource();
    source->files[L"C:\\Steam\\steamapps\\libraryfolders.vdf"] =
        R"vdf("libraryfolders" { "0" { "path" "C:\\Steam" "path" "D:\\Other" } })vdf";
    SteamProviderAdapter malformed(source);
    check(!malformed.refresh().succeeded() &&
              discoverInstalledGames(malformed, games).result ==
                  ProviderResult::ProviderFailure && games == original,
          "duplicate KeyValues metadata fails transactionally without replacing prior output");

    source = validSource();
    source->files[source->manifests[1]] = manifest("200", "Duplicate", "First", "11");
    SteamProviderAdapter duplicate(source);
    check(!duplicate.refresh().succeeded(),
          "duplicate appid across Steam manifests fails closed");

    source = validSource();
    source->files[source->manifests[1]] = manifest("100", "Bad", "..\\Escape", "11");
    SteamProviderAdapter escaping(source);
    check(!escaping.refresh().succeeded(),
          "manifest install directory cannot escape the Steam common root");
}

void testAbsentAndSourceFailuresAreExplicit() {
    auto source = validSource();
    source->locateResult = SteamSourceResult::NotInstalled;
    SteamProviderAdapter absent(source);
    std::vector<catalog::GameCatalogCandidate> games;
    check(absent.descriptor().availability == ProviderAvailability::Absent &&
              discoverInstalledGames(absent, games).result == ProviderResult::ProviderAbsent,
          "missing Steam installation is explicit and does not invoke fallback discovery");

    source = validSource();
    source->failRead = true;
    SteamProviderAdapter failed(source);
    check(failed.descriptor().availability == ProviderAvailability::Offline &&
              discoverInstalledGames(failed, games).result == ProviderResult::ProviderFailure,
          "read failure is distinguished from an absent Steam installation");
}

int liveSmoke() {
    SteamProviderAdapter adapter(makeNativeSteamMetadataSource());
    const auto descriptor = adapter.descriptor();
    if (descriptor.availability == ProviderAvailability::Absent) {
        std::cout << "Steam is not installed; read-only live smoke skipped\n";
        return EXIT_SUCCESS;
    }
    std::vector<catalog::GameCatalogCandidate> games;
    const auto discovered = discoverInstalledGames(adapter, games);
    if (!discovered.succeeded()) {
        std::cerr << "Steam read-only live discovery failed: " << discovered.message << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Steam read-only live discovery passed for " << games.size()
              << " launchable local app(s); no launch or mutation was performed\n";
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--live-smoke") return liveSmoke();

    testReadOnlyMultiLibraryDiscoveryAndDeterminism();
    testSupportedLaunchAndPolicyBoundaries();
    testProcessCandidatesRemainUnverified();
    testMalformedAndStaleMetadataFailsClosed();
    testAbsentAndSourceFailuresAreExplicit();

    if (failures != 0) {
        std::cerr << failures << " Steam provider test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "P6-PROV-02 Steam provider tests passed\n";
    return EXIT_SUCCESS;
}
