#include "hydra/workspace_manager.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace hydra {
namespace {

struct JsonNumber { std::string text; };
struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;
    std::variant<std::nullptr_t, bool, JsonNumber, std::string, Array, Object> value;
};

class JsonParser {
public:
    explicit JsonParser(std::string text) : text_(std::move(text)) {}

    JsonValue parse() {
        skip();
        JsonValue value = parseValue(0);
        skip();
        if (pos_ != text_.size()) fail("trailing content");
        return value;
    }

private:
    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error(std::string(message) + " at byte " + std::to_string(pos_));
    }

    void skip() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }

    bool consume(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    void expect(char c) {
        if (!consume(c)) fail("unexpected character");
    }

    void literal(std::string_view value) {
        if (text_.substr(pos_, value.size()) != value) fail("invalid literal");
        pos_ += value.size();
    }

    static void appendCodePoint(std::string& out, std::uint32_t cp) {
        if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    }

    std::uint32_t hex4() {
        std::uint32_t cp = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos_ >= text_.size()) fail("truncated unicode escape");
            const char c = text_[pos_++];
            cp <<= 4;
            if (c >= '0' && c <= '9') cp += static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') cp += static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') cp += static_cast<unsigned>(c - 'A' + 10);
            else fail("invalid unicode escape");
        }
        return cp;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (pos_ < text_.size()) {
            unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') return out;
            if (c < 0x20) fail("control character in string");
            if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
            if (pos_ >= text_.size()) fail("truncated escape");
            const char e = text_[pos_++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                std::uint32_t cp = hex4();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (pos_ + 2 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u')
                        fail("missing low surrogate");
                    pos_ += 2;
                    const std::uint32_t low = hex4();
                    if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                    fail("unexpected low surrogate");
                }
                appendCodePoint(out, cp);
                break;
            }
            default: fail("invalid escape");
            }
        }
        fail("unterminated string");
    }

    JsonNumber parseNumber() {
        const std::size_t start = pos_;
        consume('-');
        if (consume('0')) {
            if (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])))
                fail("leading zero");
        } else {
            if (pos_ >= text_.size() || text_[pos_] < '1' || text_[pos_] > '9') fail("invalid number");
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E'))
            fail("integer expected");
        return JsonNumber{text_.substr(start, pos_ - start)};
    }

    JsonValue parseValue(unsigned depth) {
        if (depth > 64) fail("nesting too deep");
        skip();
        if (pos_ >= text_.size()) fail("value expected");
        if (text_[pos_] == '"') return JsonValue{parseString()};
        if (text_[pos_] == '{') {
            ++pos_;
            JsonValue::Object object;
            skip();
            if (consume('}')) return JsonValue{std::move(object)};
            while (true) {
                skip();
                if (pos_ >= text_.size() || text_[pos_] != '"') fail("object key expected");
                std::string key = parseString();
                skip(); expect(':');
                if (!object.emplace(std::move(key), parseValue(depth + 1)).second) fail("duplicate object key");
                skip();
                if (consume('}')) break;
                expect(',');
            }
            return JsonValue{std::move(object)};
        }
        if (text_[pos_] == '[') {
            ++pos_;
            JsonValue::Array array;
            skip();
            if (consume(']')) return JsonValue{std::move(array)};
            while (true) {
                array.push_back(parseValue(depth + 1));
                skip();
                if (consume(']')) break;
                expect(',');
            }
            return JsonValue{std::move(array)};
        }
        if (text_.compare(pos_, 4, "true") == 0) { literal("true"); return JsonValue{true}; }
        if (text_.compare(pos_, 5, "false") == 0) { literal("false"); return JsonValue{false}; }
        if (text_.compare(pos_, 4, "null") == 0) { literal("null"); return JsonValue{nullptr}; }
        if (text_[pos_] == '-' || std::isdigit(static_cast<unsigned char>(text_[pos_])))
            return JsonValue{parseNumber()};
        fail("invalid value");
    }

    std::string text_;
    std::size_t pos_{0};
};

const JsonValue::Object& objectOf(const JsonValue& v) {
    const auto* p = std::get_if<JsonValue::Object>(&v.value);
    if (!p) throw std::runtime_error("object expected");
    return *p;
}
const JsonValue::Array& arrayOf(const JsonValue& v) {
    const auto* p = std::get_if<JsonValue::Array>(&v.value);
    if (!p) throw std::runtime_error("array expected");
    return *p;
}
const std::string& stringOf(const JsonValue& v) {
    const auto* p = std::get_if<std::string>(&v.value);
    if (!p) throw std::runtime_error("string expected");
    return *p;
}
bool boolOf(const JsonValue& v) {
    const auto* p = std::get_if<bool>(&v.value);
    if (!p) throw std::runtime_error("boolean expected");
    return *p;
}
std::uint64_t uintOf(const JsonValue& v) {
    const auto* p = std::get_if<JsonNumber>(&v.value);
    if (!p || p->text.empty() || p->text.front() == '-') throw std::runtime_error("unsigned integer expected");
    std::uint64_t result = 0;
    const auto conv = std::from_chars(p->text.data(), p->text.data() + p->text.size(), result);
    if (conv.ec != std::errc{} || conv.ptr != p->text.data() + p->text.size())
        throw std::runtime_error("integer out of range");
    return result;
}
const JsonValue& required(const JsonValue::Object& o, const char* key) {
    const auto it = o.find(key);
    if (it == o.end()) throw std::runtime_error(std::string("missing field: ") + key);
    return it->second;
}
const JsonValue* optional(const JsonValue::Object& o, const char* key) {
    const auto it = o.find(key);
    return it == o.end() ? nullptr : &it->second;
}

std::wstring fromUtf8(const std::string& text) {
    std::wstring out;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[i++]);
        std::uint32_t cp = 0;
        unsigned continuation = 0;
        if (first < 0x80) cp = first;
        else if ((first & 0xe0) == 0xc0) { cp = first & 0x1f; continuation = 1; }
        else if ((first & 0xf0) == 0xe0) { cp = first & 0x0f; continuation = 2; }
        else if ((first & 0xf8) == 0xf0) { cp = first & 0x07; continuation = 3; }
        else throw std::runtime_error("invalid UTF-8");
        if (i + continuation > text.size()) throw std::runtime_error("truncated UTF-8");
        for (unsigned n = 0; n < continuation; ++n) {
            const unsigned char c = static_cast<unsigned char>(text[i++]);
            if ((c & 0xc0) != 0x80) throw std::runtime_error("invalid UTF-8 continuation");
            cp = (cp << 6) | (c & 0x3f);
        }
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) throw std::runtime_error("invalid UTF-8 code point");
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp <= 0xffff) out.push_back(static_cast<wchar_t>(cp));
            else {
                cp -= 0x10000;
                out.push_back(static_cast<wchar_t>(0xd800 + (cp >> 10)));
                out.push_back(static_cast<wchar_t>(0xdc00 + (cp & 0x3ff)));
            }
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

std::string toUtf8(const std::wstring& text) {
    std::string out;
    auto append = [&](std::uint32_t cp) {
        if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t cp = static_cast<std::uint32_t>(text[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0xd800 && cp <= 0xdbff) {
                if (++i >= text.size()) throw std::runtime_error("unpaired surrogate");
                const std::uint32_t low = static_cast<std::uint32_t>(text[i]);
                if (low < 0xdc00 || low > 0xdfff) throw std::runtime_error("unpaired surrogate");
                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
            } else if (cp >= 0xdc00 && cp <= 0xdfff) throw std::runtime_error("unpaired surrogate");
        }
        append(cp);
    }
    return out;
}

std::string quote(const std::wstring& w) {
    const std::string text = toUtf8(w);
    std::ostringstream out;
    out << '"';
    static constexpr char hex[] = "0123456789abcdef";
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) out << "\\u00" << hex[c >> 4] << hex[c & 0x0f];
            else out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}

void writeStringArray(std::ostream& out, const std::vector<std::wstring>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << ", ";
        out << quote(values[i]);
    }
    out << ']';
}

std::vector<std::wstring> readStringArray(const JsonValue& v) {
    std::vector<std::wstring> out;
    for (const auto& item : arrayOf(v)) out.push_back(fromUtf8(stringOf(item)));
    return out;
}

const char* typeName(SeatDeviceType type) {
    switch (type) {
    case SeatDeviceType::Display: return "display";
    case SeatDeviceType::Keyboard: return "keyboard";
    case SeatDeviceType::Mouse: return "mouse";
    case SeatDeviceType::Controller: return "controller";
    case SeatDeviceType::AudioOutput: return "audio_output";
    case SeatDeviceType::AudioInput: return "audio_input";
    }
    return "unknown";
}

SeatDeviceType parseType(const std::string& type) {
    if (type == "display") return SeatDeviceType::Display;
    if (type == "keyboard") return SeatDeviceType::Keyboard;
    if (type == "mouse") return SeatDeviceType::Mouse;
    if (type == "controller") return SeatDeviceType::Controller;
    if (type == "audio_output") return SeatDeviceType::AudioOutput;
    if (type == "audio_input") return SeatDeviceType::AudioInput;
    throw std::runtime_error("unknown shareable resource type");
}

} // namespace

std::wstring WorkspaceManager::normalizeId(const std::wstring& value) {
    std::wstring result = value;
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) {
        return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    });
    return result;
}

std::wstring WorkspaceManager::resourceKey(SeatDeviceType type, const std::wstring& deviceId) {
    return std::to_wstring(static_cast<unsigned>(type)) + L":" + normalizeId(deviceId);
}

SeatId WorkspaceManager::createSeat(const std::wstring& name) {
    const SeatId id = m_nextId++;
    SeatConfig seat;
    seat.seatId = id;
    seat.name = name.empty() ? (L"Player " + std::to_wstring(id)) : name;
    m_seats.emplace(id, std::move(seat));
    return id;
}

bool WorkspaceManager::removeSeat(SeatId seatId) {
    if (m_seats.erase(seatId) == 0) return false;
    removeUnusedShareableResources();
    return true;
}

bool WorkspaceManager::renameSeat(SeatId seatId, const std::wstring& name) {
    auto it = m_seats.find(seatId);
    if (it == m_seats.end() || name.empty()) return false;
    it->second.name = name;
    return true;
}

bool WorkspaceManager::isDeviceShareable(SeatDeviceType type, const std::wstring& deviceId) const {
    return m_shareableResources.contains(resourceKey(type, deviceId));
}

bool WorkspaceManager::setDeviceShareable(SeatDeviceType type, const std::wstring& deviceId, bool shareable) {
    if (deviceId.empty()) return false;
    const auto owners = findDeviceOwners(type, deviceId);
    if (!shareable && owners.size() > 1) return false;
    const auto key = resourceKey(type, deviceId);
    if (shareable) m_shareableResources.insert(key);
    else m_shareableResources.erase(key);
    return true;
}

std::vector<SeatId> WorkspaceManager::findDeviceOwners(SeatDeviceType type, const std::wstring& deviceId) const {
    std::vector<SeatId> owners;
    const auto wanted = normalizeId(deviceId);
    const auto contains = [&](const std::vector<std::wstring>& values) {
        return std::any_of(values.begin(), values.end(), [&](const std::wstring& item) {
            return normalizeId(item) == wanted;
        });
    };
    for (const auto& [id, seat] : m_seats) {
        bool found = false;
        switch (type) {
        case SeatDeviceType::Display: found = contains(seat.displayIds); break;
        case SeatDeviceType::Keyboard: found = contains(seat.keyboardIds); break;
        case SeatDeviceType::Mouse: found = contains(seat.mouseIds); break;
        case SeatDeviceType::Controller: found = contains(seat.controllerIds); break;
        case SeatDeviceType::AudioOutput:
            found = seat.audioOutputEndpointId && normalizeId(*seat.audioOutputEndpointId) == wanted; break;
        case SeatDeviceType::AudioInput:
            found = seat.audioInputEndpointId && normalizeId(*seat.audioInputEndpointId) == wanted; break;
        }
        if (found) owners.push_back(id);
    }
    std::sort(owners.begin(), owners.end());
    return owners;
}

bool WorkspaceManager::canAssign(SeatId seatId, SeatDeviceType type, const std::wstring& deviceId,
                                 bool explicitlyShareable) {
    if (!m_seats.contains(seatId) || deviceId.empty()) return false;
    if (explicitlyShareable) setDeviceShareable(type, deviceId, true);
    const auto owners = findDeviceOwners(type, deviceId);
    return owners.empty() || (owners.size() == 1 && owners.front() == seatId) || isDeviceShareable(type, deviceId);
}

bool WorkspaceManager::assignToList(SeatId seatId, SeatDeviceType type, const std::wstring& deviceId,
                                    std::vector<std::wstring> SeatConfig::*member, bool shareable) {
    if (!canAssign(seatId, type, deviceId, shareable)) return false;
    auto& list = m_seats.at(seatId).*member;
    const auto wanted = normalizeId(deviceId);
    if (std::none_of(list.begin(), list.end(), [&](const std::wstring& item) { return normalizeId(item) == wanted; }))
        list.push_back(deviceId);
    return true;
}

bool WorkspaceManager::unassignFromList(SeatId seatId, SeatDeviceType type, const std::wstring& deviceId,
                                        std::vector<std::wstring> SeatConfig::*member) {
    auto it = m_seats.find(seatId);
    if (it == m_seats.end()) return false;
    auto& list = it->second.*member;
    const auto wanted = normalizeId(deviceId);
    auto found = std::find_if(list.begin(), list.end(), [&](const std::wstring& item) { return normalizeId(item) == wanted; });
    if (found == list.end()) return false;
    list.erase(found);
    if (findDeviceOwners(type, deviceId).empty()) m_shareableResources.erase(resourceKey(type, deviceId));
    return true;
}

bool WorkspaceManager::assignDisplay(SeatId id, const std::wstring& displayId, bool makePrimary, bool shareable) {
    if (!assignToList(id, SeatDeviceType::Display, displayId, &SeatConfig::displayIds, shareable)) return false;
    auto& seat = m_seats.at(id);
    if (!seat.primaryDisplayId || makePrimary) seat.primaryDisplayId = displayId;
    return true;
}

bool WorkspaceManager::unassignDisplay(SeatId id, const std::wstring& displayId) {
    auto it = m_seats.find(id);
    if (it == m_seats.end()) return false;
    const bool primary = it->second.primaryDisplayId && normalizeId(*it->second.primaryDisplayId) == normalizeId(displayId);
    if (!unassignFromList(id, SeatDeviceType::Display, displayId, &SeatConfig::displayIds)) return false;
    if (primary) it->second.primaryDisplayId = it->second.displayIds.empty() ? std::nullopt : std::optional<std::wstring>(it->second.displayIds.front());
    return true;
}

bool WorkspaceManager::setPrimaryDisplay(SeatId id, const std::wstring& displayId) {
    auto it = m_seats.find(id);
    if (it == m_seats.end()) return false;
    const auto wanted = normalizeId(displayId);
    auto found = std::find_if(it->second.displayIds.begin(), it->second.displayIds.end(), [&](const std::wstring& item) {
        return normalizeId(item) == wanted;
    });
    if (found == it->second.displayIds.end()) return false;
    it->second.primaryDisplayId = *found;
    return true;
}

bool WorkspaceManager::assignKeyboard(SeatId id, const std::wstring& value, bool shareable) {
    return assignToList(id, SeatDeviceType::Keyboard, value, &SeatConfig::keyboardIds, shareable);
}
bool WorkspaceManager::unassignKeyboard(SeatId id, const std::wstring& value) {
    return unassignFromList(id, SeatDeviceType::Keyboard, value, &SeatConfig::keyboardIds);
}
bool WorkspaceManager::assignMouse(SeatId id, const std::wstring& value, bool shareable) {
    return assignToList(id, SeatDeviceType::Mouse, value, &SeatConfig::mouseIds, shareable);
}
bool WorkspaceManager::unassignMouse(SeatId id, const std::wstring& value) {
    return unassignFromList(id, SeatDeviceType::Mouse, value, &SeatConfig::mouseIds);
}
bool WorkspaceManager::assignController(SeatId id, const std::wstring& value, bool shareable) {
    return assignToList(id, SeatDeviceType::Controller, value, &SeatConfig::controllerIds, shareable);
}
bool WorkspaceManager::unassignController(SeatId id, const std::wstring& value) {
    return unassignFromList(id, SeatDeviceType::Controller, value, &SeatConfig::controllerIds);
}
bool WorkspaceManager::assignController(SeatId id, std::uint32_t index, bool shareable) {
    return assignController(id, L"xinput:" + std::to_wstring(index), shareable);
}
bool WorkspaceManager::unassignController(SeatId id, std::uint32_t index) {
    return unassignController(id, L"xinput:" + std::to_wstring(index));
}

bool WorkspaceManager::assignAudioOutput(SeatId id, const std::wstring& endpoint, bool shareable) {
    if (!canAssign(id, SeatDeviceType::AudioOutput, endpoint, shareable)) return false;
    m_seats.at(id).audioOutputEndpointId = endpoint;
    return true;
}
bool WorkspaceManager::unassignAudioOutput(SeatId id) {
    auto it = m_seats.find(id);
    if (it == m_seats.end() || !it->second.audioOutputEndpointId) return false;
    const auto old = *it->second.audioOutputEndpointId;
    it->second.audioOutputEndpointId.reset();
    if (findDeviceOwners(SeatDeviceType::AudioOutput, old).empty()) m_shareableResources.erase(resourceKey(SeatDeviceType::AudioOutput, old));
    return true;
}
bool WorkspaceManager::assignAudioInput(SeatId id, const std::wstring& endpoint, bool shareable) {
    if (!canAssign(id, SeatDeviceType::AudioInput, endpoint, shareable)) return false;
    m_seats.at(id).audioInputEndpointId = endpoint;
    return true;
}
bool WorkspaceManager::unassignAudioInput(SeatId id) {
    auto it = m_seats.find(id);
    if (it == m_seats.end() || !it->second.audioInputEndpointId) return false;
    const auto old = *it->second.audioInputEndpointId;
    it->second.audioInputEndpointId.reset();
    if (findDeviceOwners(SeatDeviceType::AudioInput, old).empty()) m_shareableResources.erase(resourceKey(SeatDeviceType::AudioInput, old));
    return true;
}

bool WorkspaceManager::assignTargetWindow(SeatId id, std::uint64_t hwnd) {
    auto it = m_seats.find(id);
    if (it == m_seats.end()) return false;
    it->second.targetHwnd = hwnd;
    return true;
}
bool WorkspaceManager::setActive(SeatId id, bool active) {
    auto it = m_seats.find(id);
    if (it == m_seats.end()) return false;
    it->second.active = active;
    return true;
}

const SeatConfig* WorkspaceManager::getSeat(SeatId id) const {
    auto it = m_seats.find(id);
    return it == m_seats.end() ? nullptr : &it->second;
}

std::vector<SeatConfig> WorkspaceManager::getAllSeats() const {
    std::vector<SeatConfig> seats;
    seats.reserve(m_seats.size());
    for (const auto& [id, seat] : m_seats) { (void)id; seats.push_back(seat); }
    std::sort(seats.begin(), seats.end(), [](const SeatConfig& a, const SeatConfig& b) { return a.seatId < b.seatId; });
    return seats;
}

std::optional<SeatId> firstOwner(const WorkspaceManager& manager, SeatDeviceType type, const std::wstring& id) {
    const auto owners = manager.findDeviceOwners(type, id);
    return owners.empty() ? std::nullopt : std::optional<SeatId>(owners.front());
}
std::optional<SeatId> WorkspaceManager::findDisplayOwner(const std::wstring& id) const { return firstOwner(*this, SeatDeviceType::Display, id); }
std::optional<SeatId> WorkspaceManager::findKeyboardOwner(const std::wstring& id) const { return firstOwner(*this, SeatDeviceType::Keyboard, id); }
std::optional<SeatId> WorkspaceManager::findMouseOwner(const std::wstring& id) const { return firstOwner(*this, SeatDeviceType::Mouse, id); }
std::optional<SeatId> WorkspaceManager::findControllerOwner(const std::wstring& id) const { return firstOwner(*this, SeatDeviceType::Controller, id); }
std::optional<SeatId> WorkspaceManager::findAudioOutputOwner(const std::wstring& id) const { return firstOwner(*this, SeatDeviceType::AudioOutput, id); }
std::optional<SeatId> WorkspaceManager::findAudioInputOwner(const std::wstring& id) const { return firstOwner(*this, SeatDeviceType::AudioInput, id); }

SeatId WorkspaceManager::findWorkspaceByKeyboardPath(const std::wstring& path) const {
    return findKeyboardOwner(path).value_or(0);
}
SeatId WorkspaceManager::findWorkspaceByMousePath(const std::wstring& path) const {
    return findMouseOwner(path).value_or(0);
}

void WorkspaceManager::removeUnusedShareableResources() {
    for (auto it = m_shareableResources.begin(); it != m_shareableResources.end();) {
        bool used = false;
        for (unsigned t = 0; t <= static_cast<unsigned>(SeatDeviceType::AudioInput) && !used; ++t) {
            const auto type = static_cast<SeatDeviceType>(t);
            const std::wstring prefix = std::to_wstring(t) + L":";
            if (it->starts_with(prefix)) {
                const std::wstring id = it->substr(prefix.size());
                used = !findDeviceOwners(type, id).empty();
            }
        }
        if (!used) it = m_shareableResources.erase(it); else ++it;
    }
}

bool WorkspaceManager::saveToFile(const std::string& filePath) const {
    m_lastError.clear();
    try {
        std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
        if (!out) { m_lastError = "could not open profile for writing"; return false; }
        out << "{\n  \"schema_version\": 2,\n  \"shareable_resources\": [";
        bool first = true;
        for (const auto& key : m_shareableResources) {
            const auto split = key.find(L':');
            if (split == std::wstring::npos) continue;
            const unsigned typeValue = static_cast<unsigned>(std::stoul(key.substr(0, split)));
            if (!first) out << ',';
            first = false;
            out << "\n    {\"type\": \"" << typeName(static_cast<SeatDeviceType>(typeValue))
                << "\", \"id\": " << quote(key.substr(split + 1)) << '}';
        }
        if (!first) out << '\n';
        out << "  ],\n  \"seats\": [";
        const auto seats = getAllSeats();
        for (std::size_t i = 0; i < seats.size(); ++i) {
            const auto& s = seats[i];
            out << (i ? ",\n" : "\n") << "    {\n"
                << "      \"id\": " << s.seatId << ",\n"
                << "      \"name\": " << quote(s.name) << ",\n"
                << "      \"active\": " << (s.active ? "true" : "false") << ",\n"
                << "      \"target_hwnd\": " << s.targetHwnd << ",\n"
                << "      \"displays\": "; writeStringArray(out, s.displayIds); out << ",\n"
                << "      \"primary_display\": ";
            if (s.primaryDisplayId) out << quote(*s.primaryDisplayId); else out << "null";
            out << ",\n      \"keyboards\": "; writeStringArray(out, s.keyboardIds);
            out << ",\n      \"mice\": "; writeStringArray(out, s.mouseIds);
            out << ",\n      \"controllers\": "; writeStringArray(out, s.controllerIds);
            out << ",\n      \"audio_output\": ";
            if (s.audioOutputEndpointId) out << quote(*s.audioOutputEndpointId); else out << "null";
            out << ",\n      \"audio_input\": ";
            if (s.audioInputEndpointId) out << quote(*s.audioInputEndpointId); else out << "null";
            out << "\n    }";
        }
        if (!seats.empty()) out << '\n';
        out << "  ]\n}\n";
        if (!out) { m_lastError = "failed while writing profile"; return false; }
        return true;
    } catch (const std::exception& e) {
        m_lastError = e.what();
        return false;
    }
}

bool WorkspaceManager::loadFromFile(const std::string& filePath) {
    m_lastError.clear();
    try {
        std::ifstream in(filePath, std::ios::binary);
        if (!in) { m_lastError = "could not open profile"; return false; }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        const auto root = objectOf(JsonParser(buffer.str()).parse());
        if (uintOf(required(root, "schema_version")) != 2) throw std::runtime_error("unsupported schema_version");

        WorkspaceManager temp;
        if (const auto* shares = optional(root, "shareable_resources")) {
            for (const auto& entry : arrayOf(*shares)) {
                const auto& o = objectOf(entry);
                const auto type = parseType(stringOf(required(o, "type")));
                const auto id = fromUtf8(stringOf(required(o, "id")));
                if (!temp.setDeviceShareable(type, id, true)) throw std::runtime_error("invalid shareable resource");
            }
        }

        std::unordered_set<SeatId> seenIds;
        SeatId maxId = 0;
        for (const auto& entry : arrayOf(required(root, "seats"))) {
            const auto& o = objectOf(entry);
            const auto rawId = uintOf(required(o, "id"));
            if (rawId == 0 || rawId > std::numeric_limits<SeatId>::max()) throw std::runtime_error("seat id out of range");
            const SeatId id = static_cast<SeatId>(rawId);
            if (!seenIds.insert(id).second) throw std::runtime_error("duplicate seat id");
            maxId = std::max(maxId, id);

            SeatConfig seat;
            seat.seatId = id;
            seat.name = fromUtf8(stringOf(required(o, "name")));
            if (seat.name.empty()) throw std::runtime_error("seat name must not be empty");
            seat.active = boolOf(required(o, "active"));
            seat.targetHwnd = uintOf(required(o, "target_hwnd"));
            seat.displayIds = readStringArray(required(o, "displays"));
            if (const auto& p = required(o, "primary_display"); !std::holds_alternative<std::nullptr_t>(p.value))
                seat.primaryDisplayId = fromUtf8(stringOf(p));
            seat.keyboardIds = readStringArray(required(o, "keyboards"));
            seat.mouseIds = readStringArray(required(o, "mice"));
            seat.controllerIds = readStringArray(required(o, "controllers"));
            if (const auto& a = required(o, "audio_output"); !std::holds_alternative<std::nullptr_t>(a.value))
                seat.audioOutputEndpointId = fromUtf8(stringOf(a));
            if (const auto& a = required(o, "audio_input"); !std::holds_alternative<std::nullptr_t>(a.value))
                seat.audioInputEndpointId = fromUtf8(stringOf(a));

            if (seat.primaryDisplayId) {
                const auto wanted = normalizeId(*seat.primaryDisplayId);
                if (std::none_of(seat.displayIds.begin(), seat.displayIds.end(), [&](const std::wstring& d) { return normalizeId(d) == wanted; }))
                    throw std::runtime_error("primary display is not assigned to seat");
            } else if (!seat.displayIds.empty()) {
                throw std::runtime_error("seat with displays must declare primary_display");
            }
            temp.m_seats.emplace(id, SeatConfig{});
            auto& dest = temp.m_seats.at(id);
            dest.seatId = id;
            dest.name = seat.name;
            dest.active = seat.active;
            dest.targetHwnd = seat.targetHwnd;
            for (const auto& d : seat.displayIds)
                if (!temp.assignDisplay(id, d, seat.primaryDisplayId && normalizeId(*seat.primaryDisplayId) == normalizeId(d)))
                    throw std::runtime_error("display is exclusively owned by another seat");
            for (const auto& d : seat.keyboardIds)
                if (!temp.assignKeyboard(id, d)) throw std::runtime_error("keyboard is exclusively owned by another seat");
            for (const auto& d : seat.mouseIds)
                if (!temp.assignMouse(id, d)) throw std::runtime_error("mouse is exclusively owned by another seat");
            for (const auto& d : seat.controllerIds)
                if (!temp.assignController(id, d)) throw std::runtime_error("controller is exclusively owned by another seat");
            if (seat.audioOutputEndpointId && !temp.assignAudioOutput(id, *seat.audioOutputEndpointId))
                throw std::runtime_error("audio output is exclusively owned by another seat");
            if (seat.audioInputEndpointId && !temp.assignAudioInput(id, *seat.audioInputEndpointId))
                throw std::runtime_error("audio input is exclusively owned by another seat");
        }
        temp.m_nextId = maxId + 1;
        m_nextId = temp.m_nextId;
        m_seats = std::move(temp.m_seats);
        m_shareableResources = std::move(temp.m_shareableResources);
        return true;
    } catch (const std::exception& e) {
        m_lastError = e.what();
        return false;
    }
}

} // namespace hydra
