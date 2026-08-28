#include "hydra/setup_package.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hydra::portable {
namespace {

constexpr std::string_view kHeader = "HYDRASEAT_SETUP_PACKAGE\n";

PackageDiagnostic fail(PackageResult result, std::string message) {
    return {result, std::move(message)};
}

bool validOpaqueId(std::string_view value) noexcept {
    if (value.empty() || value.size() > profile::kMaximumIdentifierBytes) return false;
    for (const char raw : value) {
        const auto ch = static_cast<unsigned char>(raw);
        if (!(std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.' || ch == ':')) {
            return false;
        }
    }
    return true;
}

bool validProvenance(const Provenance& provenance) noexcept {
    return validOpaqueId(provenance.sourceId) && provenance.sourceRevision != 0u &&
           validOpaqueId(provenance.exportedBy);
}

std::wstring placeholder(std::string_view name) {
    std::wstring value = L"${";
    value.reserve(name.size() + 3u);
    for (const char ch : name) {
        value.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
    }
    value.push_back(L'}');
    return value;
}

bool placeholderMatches(std::wstring_view value, std::string_view name) {
    return value == placeholder(name);
}

const std::optional<std::wstring>* pathField(
    const profile::TwoPlayerSetup& setup,
    const PathVariable& variable) noexcept {
    if (variable.instanceIndex >= setup.instances.size()) return nullptr;
    const auto& recipe = setup.instances[variable.instanceIndex];
    switch (variable.field) {
    case PathField::WorkingDirectory: return &recipe.workingDirectory;
    case PathField::DataRoot: return &recipe.dataRoot;
    }
    return nullptr;
}

std::optional<std::wstring>* pathField(profile::TwoPlayerSetup& setup,
                                       const PathVariable& variable) noexcept {
    if (variable.instanceIndex >= setup.instances.size()) return nullptr;
    auto& recipe = setup.instances[variable.instanceIndex];
    switch (variable.field) {
    case PathField::WorkingDirectory: return &recipe.workingDirectory;
    case PathField::DataRoot: return &recipe.dataRoot;
    }
    return nullptr;
}

PackageDiagnostic validatePackageStructure(const SetupPackage& package) {
    if (package.version != kSetupPackageVersion) {
        return fail(PackageResult::UnsupportedVersion, "unsupported setup package version");
    }
    if (!validProvenance(package.provenance)) {
        return fail(PackageResult::InvalidProvenance,
                    "package provenance must use bounded opaque identifiers and nonzero revision");
    }
    if (package.pathVariables.size() > kMaximumPathVariables) {
        return fail(PackageResult::InvalidPackage, "too many portable path variables");
    }

    profile::TwoPlayerSetupDocument setupDocument;
    setupDocument.setups = {package.redactedSetup};
    const auto schema = profile::validateTwoPlayerSetupDocument(setupDocument);
    if (!schema.succeeded()) {
        return fail(PackageResult::InvalidPackage,
                    "portable setup schema is invalid: " + schema.message);
    }

    std::set<std::string> names;
    std::set<std::pair<std::uint32_t, std::uint8_t>> fields;
    for (const auto& variable : package.pathVariables) {
        if (!validOpaqueId(variable.variableName) || variable.instanceIndex >= 2u) {
            return fail(PackageResult::InvalidPackage,
                        "path variable identity or instance index is invalid");
        }
        const auto fieldValue = static_cast<std::uint8_t>(variable.field);
        if (fieldValue > static_cast<std::uint8_t>(PathField::DataRoot)) {
            return fail(PackageResult::InvalidPackage, "unknown path variable field");
        }
        if (!names.insert(variable.variableName).second ||
            !fields.insert({variable.instanceIndex, fieldValue}).second) {
            return fail(PackageResult::InvalidPackage,
                        "portable path variables must be unique by identity and field");
        }
        const auto* field = pathField(package.redactedSetup, variable);
        if (field == nullptr || !field->has_value() ||
            !placeholderMatches(**field, variable.variableName)) {
            return fail(PackageResult::InvalidPackage,
                        "portable setup path does not match its declared typed variable");
        }
    }

    for (std::size_t index = 0; index < package.redactedSetup.instances.size(); ++index) {
        const auto& recipe = package.redactedSetup.instances[index];
        const std::array<std::pair<PathField, const std::optional<std::wstring>*>, 2> values{{
            {PathField::WorkingDirectory, &recipe.workingDirectory},
            {PathField::DataRoot, &recipe.dataRoot},
        }};
        for (const auto& [fieldKind, value] : values) {
            if (!value->has_value()) continue;
            const auto& path = **value;
            if (path.size() < 4u || path.rfind(L"${", 0u) != 0u || path.back() != L'}') {
                return fail(PackageResult::InvalidPackage,
                            "portable package contains an unredacted local path");
            }
            bool declared = false;
            for (const auto& variable : package.pathVariables) {
                if (variable.instanceIndex == index && variable.field == fieldKind &&
                    placeholderMatches(path, variable.variableName)) {
                    declared = true;
                    break;
                }
            }
            if (!declared) {
                return fail(PackageResult::InvalidPackage,
                            "portable path placeholder has no typed variable declaration");
            }
        }
    }
    return {};
}

void appendLine(std::string& output, std::string_view label, std::uint64_t value) {
    output.append(label);
    output.push_back(' ');
    output.append(std::to_string(value));
    output.push_back('\n');
}

void appendBlob(std::string& output, std::string_view label, std::string_view value) {
    appendLine(output, label, static_cast<std::uint64_t>(value.size()));
    output.append(value);
    output.push_back('\n');
}

class Reader final {
public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}

    bool line(std::string_view& output) noexcept {
        if (position_ > bytes_.size()) return false;
        const auto end = bytes_.find('\n', position_);
        if (end == std::string_view::npos) return false;
        output = bytes_.substr(position_, end - position_);
        position_ = end + 1u;
        return true;
    }

    bool blob(std::size_t size, std::string_view& output) noexcept {
        if (position_ > bytes_.size() || size > bytes_.size() - position_) return false;
        if (position_ + size >= bytes_.size()) return false;
        output = bytes_.substr(position_, size);
        position_ += size;
        if (bytes_[position_] != '\n') return false;
        ++position_;
        return true;
    }

    bool done() const noexcept { return position_ == bytes_.size(); }

private:
    std::string_view bytes_;
    std::size_t position_{0};
};

bool parseNumber(std::string_view text, std::uint64_t& output) noexcept {
    if (text.empty()) return false;
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
    output = value;
    return true;
}

bool parseLabeledNumber(std::string_view line,
                        std::string_view label,
                        std::uint64_t& output) noexcept {
    if (line.size() <= label.size() + 1u || line.substr(0u, label.size()) != label ||
        line[label.size()] != ' ') {
        return false;
    }
    return parseNumber(line.substr(label.size() + 1u), output);
}

bool readBlob(Reader& reader,
              std::string_view expectedLabel,
              std::string_view& output) noexcept {
    std::string_view line;
    std::uint64_t length = 0;
    if (!reader.line(line) || !parseLabeledNumber(line, expectedLabel, length) ||
        length > static_cast<std::uint64_t>(kMaximumSetupPackageBytes) ||
        length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    return reader.blob(static_cast<std::size_t>(length), output);
}

} // namespace

PackageDiagnostic exportSetup(const profile::TwoPlayerSetup& setup,
                              const profile::GameRecord& game,
                              Provenance provenance,
                              SetupPackage& output) {
    try {
        if (!validProvenance(provenance)) {
            return fail(PackageResult::InvalidProvenance,
                        "export provenance is missing or invalid");
        }
        const auto setupValidation = setup::validateSetup(setup, game);
        if (!setupValidation.succeeded()) {
            return fail(PackageResult::InvalidSetup, setupValidation.message);
        }

        SetupPackage package;
        package.provenance = std::move(provenance);
        package.redactedSetup = setup;
        for (std::size_t index = 0; index < package.redactedSetup.instances.size(); ++index) {
            auto& recipe = package.redactedSetup.instances[index];
            if (recipe.workingDirectory) {
                PathVariable variable;
                variable.variableName = "WORKING_DIRECTORY_" + std::to_string(index);
                variable.instanceIndex = static_cast<std::uint32_t>(index);
                variable.field = PathField::WorkingDirectory;
                recipe.workingDirectory = placeholder(variable.variableName);
                package.pathVariables.push_back(std::move(variable));
            }
            if (recipe.dataRoot) {
                PathVariable variable;
                variable.variableName = "DATA_ROOT_" + std::to_string(index);
                variable.instanceIndex = static_cast<std::uint32_t>(index);
                variable.field = PathField::DataRoot;
                recipe.dataRoot = placeholder(variable.variableName);
                package.pathVariables.push_back(std::move(variable));
            }
        }

        const auto validation = validatePackageStructure(package);
        if (!validation.succeeded()) return validation;
        output = std::move(package);
        return {};
    } catch (...) {
        return fail(PackageResult::InvalidPackage, "setup export failed unexpectedly");
    }
}

PackageDiagnostic importSetup(const SetupPackage& package,
                              const profile::GameRecord& localGame,
                              std::span<const PathBinding> bindings,
                              profile::TwoPlayerSetup& output) {
    try {
        const auto validation = validatePackageStructure(package);
        if (!validation.succeeded()) return validation;
        if (package.redactedSetup.gameId != localGame.gameId) {
            return fail(PackageResult::InvalidSetup,
                        "portable setup does not match the selected local Game");
        }

        std::set<std::string> seenBindings;
        for (const auto& binding : bindings) {
            if (!validOpaqueId(binding.variableName) ||
                !seenBindings.insert(binding.variableName).second) {
                return fail(PackageResult::DuplicateBinding,
                            "local path bindings must use unique declared variables");
            }
            const auto declared = std::find_if(
                package.pathVariables.begin(), package.pathVariables.end(),
                [&binding](const auto& variable) {
                    return variable.variableName == binding.variableName;
                });
            if (declared == package.pathVariables.end()) {
                return fail(PackageResult::UnexpectedBinding,
                            "local path binding was not declared by the portable package");
            }
        }
        if (bindings.size() != package.pathVariables.size()) {
            return fail(PackageResult::MissingBinding,
                        "every portable path variable requires explicit local remapping");
        }

        profile::TwoPlayerSetup imported = package.redactedSetup;
        for (const auto& variable : package.pathVariables) {
            const auto binding = std::find_if(
                bindings.begin(), bindings.end(),
                [&variable](const auto& candidate) {
                    return candidate.variableName == variable.variableName;
                });
            if (binding == bindings.end()) {
                return fail(PackageResult::MissingBinding,
                            "portable path variable is missing a local binding");
            }
            auto* field = pathField(imported, variable);
            if (field == nullptr) {
                return fail(PackageResult::InvalidPackage,
                            "portable path variable points outside the two instances");
            }
            *field = binding->localPath;
        }

        const auto setupValidation = setup::validateSetup(imported, localGame);
        if (!setupValidation.succeeded()) {
            const auto result = setupValidation.code == setup::SetupIssueCode::InvalidPath
                                    ? PackageResult::InvalidLocalPath
                                    : PackageResult::InvalidSetup;
            return fail(result, setupValidation.message);
        }
        output = std::move(imported);
        return {};
    } catch (...) {
        return fail(PackageResult::InvalidPackage, "setup import failed unexpectedly");
    }
}

PackageDiagnostic encodePackage(const SetupPackage& package, std::string& output) {
    try {
        const auto validation = validatePackageStructure(package);
        if (!validation.succeeded()) return validation;

        profile::TwoPlayerSetupDocument document;
        document.setups = {package.redactedSetup};
        profile::SchemaDiagnostic schema;
        const auto setupJson = profile::encodeTwoPlayerSetupDocument(document, &schema);
        if (!schema.succeeded()) {
            return fail(PackageResult::InvalidPackage,
                        "portable setup JSON encoding failed: " + schema.message);
        }

        std::string encoded;
        encoded.reserve(setupJson.size() + 512u);
        encoded.append(kHeader);
        appendLine(encoded, "VERSION", package.version);
        appendBlob(encoded, "SOURCE", package.provenance.sourceId);
        appendLine(encoded, "SOURCE_REVISION", package.provenance.sourceRevision);
        appendBlob(encoded, "EXPORTED_BY", package.provenance.exportedBy);
        appendBlob(encoded, "SETUP_JSON", setupJson);
        appendLine(encoded, "VARIABLES",
                   static_cast<std::uint64_t>(package.pathVariables.size()));
        for (const auto& variable : package.pathVariables) {
            appendLine(encoded, "INSTANCE", variable.instanceIndex);
            appendLine(encoded, "FIELD", static_cast<std::uint8_t>(variable.field));
            appendBlob(encoded, "VARIABLE_NAME", variable.variableName);
        }
        encoded.append("END\n");
        if (encoded.size() > kMaximumSetupPackageBytes) {
            return fail(PackageResult::PackageTooLarge,
                        "encoded setup package exceeds the bounded maximum");
        }
        output = std::move(encoded);
        return {};
    } catch (...) {
        return fail(PackageResult::InvalidPackage, "setup package encoding failed unexpectedly");
    }
}

PackageDiagnostic decodePackage(std::string_view bytes, SetupPackage& output) {
    try {
        if (bytes.size() > kMaximumSetupPackageBytes) {
            return fail(PackageResult::PackageTooLarge,
                        "setup package exceeds the bounded maximum");
        }
        if (bytes.size() < kHeader.size() || bytes.substr(0u, kHeader.size()) != kHeader) {
            return fail(PackageResult::InvalidPackage, "setup package header is invalid");
        }

        Reader reader(bytes.substr(kHeader.size()));
        std::string_view line;
        std::uint64_t number = 0;
        if (!reader.line(line) || !parseLabeledNumber(line, "VERSION", number)) {
            return fail(PackageResult::InvalidPackage, "missing setup package version");
        }
        if (number != kSetupPackageVersion) {
            return fail(PackageResult::UnsupportedVersion, "unsupported setup package version");
        }

        SetupPackage decoded;
        decoded.version = static_cast<std::uint32_t>(number);
        std::string_view blob;
        if (!readBlob(reader, "SOURCE", blob)) {
            return fail(PackageResult::InvalidPackage, "invalid source provenance field");
        }
        decoded.provenance.sourceId.assign(blob);
        if (!reader.line(line) || !parseLabeledNumber(line, "SOURCE_REVISION", number)) {
            return fail(PackageResult::InvalidPackage, "invalid source revision field");
        }
        decoded.provenance.sourceRevision = number;
        if (!readBlob(reader, "EXPORTED_BY", blob)) {
            return fail(PackageResult::InvalidPackage, "invalid exporter provenance field");
        }
        decoded.provenance.exportedBy.assign(blob);
        if (!readBlob(reader, "SETUP_JSON", blob)) {
            return fail(PackageResult::InvalidPackage, "invalid portable setup JSON field");
        }

        profile::TwoPlayerSetupDocument setupDocument;
        const auto schema = profile::decodeTwoPlayerSetupDocument(blob, setupDocument);
        if (!schema.succeeded() || setupDocument.setups.size() != 1u) {
            return fail(PackageResult::InvalidPackage,
                        "portable package must contain exactly one valid setup");
        }
        decoded.redactedSetup = std::move(setupDocument.setups.front());

        if (!reader.line(line) || !parseLabeledNumber(line, "VARIABLES", number) ||
            number > kMaximumPathVariables) {
            return fail(PackageResult::InvalidPackage, "invalid path variable count");
        }
        decoded.pathVariables.reserve(static_cast<std::size_t>(number));
        for (std::uint64_t index = 0; index < number; ++index) {
            PathVariable variable;
            std::uint64_t instance = 0;
            std::uint64_t field = 0;
            if (!reader.line(line) || !parseLabeledNumber(line, "INSTANCE", instance) ||
                instance > std::numeric_limits<std::uint32_t>::max()) {
                return fail(PackageResult::InvalidPackage, "invalid path variable instance");
            }
            if (!reader.line(line) || !parseLabeledNumber(line, "FIELD", field) ||
                field > static_cast<std::uint64_t>(PathField::DataRoot)) {
                return fail(PackageResult::InvalidPackage, "invalid path variable field");
            }
            if (!readBlob(reader, "VARIABLE_NAME", blob)) {
                return fail(PackageResult::InvalidPackage, "invalid path variable identity");
            }
            variable.instanceIndex = static_cast<std::uint32_t>(instance);
            variable.field = static_cast<PathField>(field);
            variable.variableName.assign(blob);
            decoded.pathVariables.push_back(std::move(variable));
        }
        if (!reader.line(line) || line != "END" || !reader.done()) {
            return fail(PackageResult::InvalidPackage,
                        "setup package has trailing or incomplete data");
        }

        const auto validation = validatePackageStructure(decoded);
        if (!validation.succeeded()) return validation;
        output = std::move(decoded);
        return {};
    } catch (...) {
        return fail(PackageResult::InvalidPackage, "setup package decoding failed unexpectedly");
    }
}

std::string_view packageResultName(PackageResult result) noexcept {
    switch (result) {
    case PackageResult::Success: return "Success";
    case PackageResult::InvalidSetup: return "InvalidSetup";
    case PackageResult::InvalidProvenance: return "InvalidProvenance";
    case PackageResult::InvalidPackage: return "InvalidPackage";
    case PackageResult::PackageTooLarge: return "PackageTooLarge";
    case PackageResult::UnsupportedVersion: return "UnsupportedVersion";
    case PackageResult::MissingBinding: return "MissingBinding";
    case PackageResult::DuplicateBinding: return "DuplicateBinding";
    case PackageResult::UnexpectedBinding: return "UnexpectedBinding";
    case PackageResult::InvalidLocalPath: return "InvalidLocalPath";
    }
    return "Unknown";
}

std::string_view pathFieldName(PathField field) noexcept {
    switch (field) {
    case PathField::WorkingDirectory: return "WorkingDirectory";
    case PathField::DataRoot: return "DataRoot";
    }
    return "Unknown";
}

} // namespace hydra::portable
