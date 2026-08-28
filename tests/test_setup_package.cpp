#include "hydra/setup_package.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace hydra;
using namespace hydra::portable;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

profile::CompatibilityReference compatibility() {
    return {"compat-portable", "fixture", 2u};
}

profile::GameRecord game() {
    profile::GameRecord value;
    value.gameId = "game:portable";
    value.providerId = "fake";
    value.providerAppId = "300";
    value.title = L"Portable Fixture";
    value.installRoot = L"C:\\Games\\Portable";
    value.executableCandidates = {L"C:\\Games\\Portable\\game.exe"};
    value.compatibility = compatibility();
    value.origin = profile::GameOrigin::Discovered;
    return value;
}

profile::TwoPlayerSetup setup() {
    profile::TwoPlayerSetup value;
    value.setupId = "setup-portable";
    value.gameId = "game:portable";
    value.displayName = L"Portable two player";
    value.compatibility = compatibility();
    value.instances = {
        {{L"--seat=1"}, L"C:\\Users\\SourceUser\\Game", L"C:\\Users\\SourceUser\\Data1"},
        {{L"--seat=2"}, L"C:\\Users\\SourceUser\\Game", L"C:\\Users\\SourceUser\\Data2"},
    };
    return value;
}

std::vector<PathBinding> remap() {
    return {
        {"WORKING_DIRECTORY_0", L"D:\\Games\\Portable"},
        {"DATA_ROOT_0", L"D:\\HydraSeat\\Data1"},
        {"WORKING_DIRECTORY_1", L"D:\\Games\\Portable"},
        {"DATA_ROOT_1", L"D:\\HydraSeat\\Data2"},
    };
}

void testExportEncodeDecodeImportRoundTrip() {
    SetupPackage package;
    const auto sourceSetup = setup();
    check(exportSetup(sourceSetup, game(), {"community-fixture", 7u, "hydraseat-test"}, package)
              .succeeded(),
          "valid setup exports into portable package");
    check(package.pathVariables.size() == 4u,
          "all machine-specific working/data paths become typed variables");
    check(package.redactedSetup.instances[0].dataRoot == L"${DATA_ROOT_0}" &&
              package.redactedSetup.instances[1].dataRoot == L"${DATA_ROOT_1}",
          "portable setup carries placeholders rather than source paths");

    std::string encoded;
    check(encodePackage(package, encoded).succeeded(),
          "portable package encodes deterministically");
    check(encoded.find("SourceUser") == std::string::npos &&
              encoded.find("C:\\\\Users") == std::string::npos,
          "encoded export does not contain the source machine's personal absolute paths");

    SetupPackage decoded;
    check(decodePackage(encoded, decoded).succeeded() && decoded == package,
          "encoded package decodes to the same redacted typed package");

    profile::TwoPlayerSetup imported;
    check(importSetup(decoded, game(), remap(), imported).succeeded(),
          "portable package imports after explicit local path remapping");
    check(imported.instances[0].dataRoot == L"D:\\HydraSeat\\Data1" &&
              imported.instances[1].workingDirectory == L"D:\\Games\\Portable",
          "import substitutes only explicitly selected local paths");
    check(setup::validateSetup(imported, game()).succeeded(),
          "imported setup passes the normal setup validator before use");
}

void testMissingUnexpectedAndInvalidBindingsFailTransactionally() {
    SetupPackage package;
    exportSetup(setup(), game(), {"community-fixture", 7u, "hydraseat-test"}, package);
    profile::TwoPlayerSetup output;
    output.setupId = "sentinel";
    const auto sentinel = output;

    auto bindings = remap();
    bindings.pop_back();
    check(importSetup(package, game(), bindings, output).result == PackageResult::MissingBinding &&
              output == sentinel,
          "missing local remap leaves previous output unchanged");

    bindings = remap();
    bindings.push_back({"UNDECLARED", L"D:\\Other"});
    check(importSetup(package, game(), bindings, output).result ==
              PackageResult::UnexpectedBinding &&
              output == sentinel,
          "undeclared local remap fails closed");

    bindings = remap();
    bindings[1].localPath = L"relative\\private";
    check(importSetup(package, game(), bindings, output).result ==
              PackageResult::InvalidLocalPath &&
              output == sentinel,
          "relative remap cannot become a local mutation path");
}

void testMalformedPackageAndVersionFailClosed() {
    SetupPackage package;
    exportSetup(setup(), game(), {"community-fixture", 7u, "hydraseat-test"}, package);
    std::string encoded;
    encodePackage(package, encoded);

    SetupPackage output;
    output.provenance.sourceId = "sentinel";
    const auto sentinel = output;
    auto wrongVersion = encoded;
    const auto version = wrongVersion.find("VERSION 1\n");
    check(version != std::string::npos, "fixture version marker exists");
    if (version != std::string::npos) wrongVersion.replace(version, 10u, "VERSION 2\n");
    check(decodePackage(wrongVersion, output).result == PackageResult::UnsupportedVersion &&
              output == sentinel,
          "unknown package version leaves previous output unchanged");

    auto trailing = encoded + "EXTRA";
    check(decodePackage(trailing, output).result == PackageResult::InvalidPackage &&
              output == sentinel,
          "trailing package data is rejected");
}

void testPortableStructureRejectsDuplicateVariableIdentity() {
    SetupPackage package;
    exportSetup(setup(), game(), {"community-fixture", 7u, "hydraseat-test"}, package);
    package.pathVariables[1].variableName = package.pathVariables[0].variableName;
    std::string output = "sentinel";
    check(encodePackage(package, output).result == PackageResult::InvalidPackage &&
              output == "sentinel",
          "duplicate path-variable identity is rejected transactionally");
}

} // namespace

int main() {
    testExportEncodeDecodeImportRoundTrip();
    testMissingUnexpectedAndInvalidBindingsFailTransactionally();
    testMalformedPackageAndVersionFailClosed();
    testPortableStructureRejectsDuplicateVariableIdentity();
    if (failures != 0) {
        std::cerr << failures << " setup package test(s) failed\n";
        return 1;
    }
    std::cout << "setup package tests passed\n";
    return 0;
}
