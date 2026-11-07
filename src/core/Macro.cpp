#include "Macro.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace ffc {

namespace {

MacroLoadResult fail(std::string const& msg) {
    MacroLoadResult r;
    r.ok = false;
    r.error = msg;
    return r;
}

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool endsWith(std::string const& s, std::string const& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string trim(std::string const& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> split(std::string const& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, sep)) {
        out.push_back(item);
    }
    return out;
}

struct ByteReader {
    uint8_t const* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    bool bad = false;

    ByteReader(std::vector<uint8_t> const& buf, size_t start = 0)
        : data(buf.data()), size(buf.size()), pos(std::min(start, buf.size())) {}

    size_t left() const { return pos < size ? size - pos : 0; }

    uint8_t u8() {
        if (pos >= size) { bad = true; return 0; }
        return data[pos++];
    }

    void skip(size_t n) {
        if (left() < n) { bad = true; pos = size; return; }
        pos += n;
    }

    template <typename T>
    T le() {
        if (left() < sizeof(T)) { bad = true; return T{}; }
        uint8_t b[sizeof(T)];
        for (size_t i = 0; i < sizeof(T); ++i) b[i] = data[pos++];
        T val{};
        std::memcpy(&val, b, sizeof(T));
        return val;
    }

    template <typename T>
    T be() {
        if (left() < sizeof(T)) { bad = true; return T{}; }
        uint8_t b[sizeof(T)];
        for (size_t i = 0; i < sizeof(T); ++i) b[sizeof(T) - 1 - i] = data[pos++];
        T val{};
        std::memcpy(&val, b, sizeof(T));
        return val;
    }

    uint64_t varint() {
        uint64_t result = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            uint8_t byte = u8();
            if (bad) return 0;
            result |= static_cast<uint64_t>(byte & 0x7Fu) << shift;
            if ((byte & 0x80u) == 0) return result;
        }
        bad = true;
        return 0;
    }

    std::string varstr() {
        uint64_t len = varint();
        if (bad || len > 0x100000 || left() < static_cast<size_t>(len)) {
            bad = true;
            return "";
        }
        std::string s(reinterpret_cast<char const*>(data + pos), static_cast<size_t>(len));
        pos += static_cast<size_t>(len);
        return s;
    }
};

// Lightweight unified value tree for JSON & MessagePack macro files
struct JVal {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
    std::vector<JVal> arrVal;
    std::map<std::string, JVal> objVal;

    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    bool isNumber() const { return type == Type::Number; }
    bool isBool() const { return type == Type::Bool; }
    bool isString() const { return type == Type::String; }

    bool contains(std::string const& k) const {
        return type == Type::Object && objVal.find(k) != objVal.end();
    }

    JVal const* get(std::string const& k) const {
        if (type != Type::Object) return nullptr;
        auto it = objVal.find(k);
        return it != objVal.end() ? &it->second : nullptr;
    }

    double asDouble(double def = 0.0) const {
        if (type == Type::Number) return numVal;
        if (type == Type::Bool) return boolVal ? 1.0 : 0.0;
        return def;
    }

    int64_t asInt(int64_t def = 0) const {
        if (type == Type::Number) return static_cast<int64_t>(std::llround(numVal));
        if (type == Type::Bool) return boolVal ? 1 : 0;
        return def;
    }

    bool asBool(bool def = false) const {
        if (type == Type::Bool) return boolVal;
        if (type == Type::Number) return numVal != 0.0;
        return def;
    }

    std::string asString(std::string const& def = "") const {
        return type == Type::String ? strVal : def;
    }
};

struct JsonReader {
    std::string const& s;
    size_t p = 0;
    bool bad = false;

    explicit JsonReader(std::string const& src) : s(src) {}

    void skipWs() {
        while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
    }

    JVal parseValue(int depth = 0) {
        if (depth > 64) { bad = true; return {}; }
        skipWs();
        if (p >= s.size()) { bad = true; return {}; }
        char c = s[p];
        if (c == '{') return parseObject(depth + 1);
        if (c == '[') return parseArray(depth + 1);
        if (c == '"') {
            JVal v;
            v.type = JVal::Type::String;
            v.strVal = parseString();
            return v;
        }
        if (c == 't' && s.compare(p, 4, "true") == 0) {
            p += 4;
            JVal v; v.type = JVal::Type::Bool; v.boolVal = true; return v;
        }
        if (c == 'f' && s.compare(p, 5, "false") == 0) {
            p += 5;
            JVal v; v.type = JVal::Type::Bool; v.boolVal = false; return v;
        }
        if (c == 'n' && s.compare(p, 4, "null") == 0) {
            p += 4;
            return {};
        }
        if (c == '-' || c == '+' || (c >= '0' && c <= '9')) {
            char const* start = s.c_str() + p;
            char* end = nullptr;
            double d = std::strtod(start, &end);
            if (end == start) { bad = true; return {}; }
            p = static_cast<size_t>(end - s.c_str());
            JVal v; v.type = JVal::Type::Number; v.numVal = d; return v;
        }
        bad = true;
        return {};
    }

    std::string parseString() {
        if (p >= s.size() || s[p] != '"') { bad = true; return ""; }
        ++p;
        std::string out;
        while (p < s.size()) {
            char c = s[p++];
            if (c == '"') return out;
            if (c == '\\') {
                if (p >= s.size()) { bad = true; return ""; }
                char esc = s[p++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u':
                        if (p + 4 > s.size()) { bad = true; return ""; }
                        p += 4;
                        out.push_back('?');
                        break;
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        bad = true;
        return "";
    }

    JVal parseArray(int depth) {
        ++p; // '['
        JVal v;
        v.type = JVal::Type::Array;
        skipWs();
        if (p < s.size() && s[p] == ']') { ++p; return v; }
        while (p < s.size() && !bad) {
            v.arrVal.push_back(parseValue(depth));
            if (bad) return {};
            skipWs();
            if (p < s.size() && s[p] == ',') { ++p; continue; }
            if (p < s.size() && s[p] == ']') { ++p; return v; }
            bad = true;
            return {};
        }
        bad = true;
        return {};
    }

    JVal parseObject(int depth) {
        ++p; // '{'
        JVal v;
        v.type = JVal::Type::Object;
        skipWs();
        if (p < s.size() && s[p] == '}') { ++p; return v; }
        while (p < s.size() && !bad) {
            skipWs();
            std::string key = parseString();
            if (bad) return {};
            skipWs();
            if (p >= s.size() || s[p] != ':') { bad = true; return {}; }
            ++p;
            JVal val = parseValue(depth);
            if (bad) return {};
            v.objVal.emplace(std::move(key), std::move(val));
            skipWs();
            if (p < s.size() && s[p] == ',') { ++p; continue; }
            if (p < s.size() && s[p] == '}') { ++p; return v; }
            bad = true;
            return {};
        }
        bad = true;
        return {};
    }
};

struct MsgPackReader {
    ByteReader r;
    explicit MsgPackReader(std::vector<uint8_t> const& buf) : r(buf) {}

    std::string readStr(size_t n) {
        if (r.left() < n || n > 0x100000) { r.bad = true; return ""; }
        std::string s(reinterpret_cast<char const*>(r.data + r.pos), n);
        r.pos += n;
        return s;
    }

    JVal read(int depth = 0) {
        if (depth > 64 || r.bad) { r.bad = true; return {}; }
        uint8_t b = r.u8();
        if (r.bad) return {};
        if (b <= 0x7f) { JVal v; v.type = JVal::Type::Number; v.numVal = b; return v; }
        if (b >= 0xe0) { JVal v; v.type = JVal::Type::Number; v.numVal = static_cast<int8_t>(b); return v; }
        if ((b & 0xf0) == 0x80) return readMap(b & 0x0f, depth);
        if ((b & 0xf0) == 0x90) return readArr(b & 0x0f, depth);
        if ((b & 0xe0) == 0xa0) { JVal v; v.type = JVal::Type::String; v.strVal = readStr(b & 0x1f); return v; }
        switch (b) {
            case 0xc0: return {};
            case 0xc2: { JVal v; v.type = JVal::Type::Bool; v.boolVal = false; return v; }
            case 0xc3: { JVal v; v.type = JVal::Type::Bool; v.boolVal = true; return v; }
            case 0xc4: { r.skip(r.u8()); return {}; }
            case 0xc5: { r.skip(r.be<uint16_t>()); return {}; }
            case 0xc6: { r.skip(r.be<uint32_t>()); return {}; }
            case 0xca: { JVal v; v.type = JVal::Type::Number; v.numVal = r.be<float>(); return v; }
            case 0xcb: { JVal v; v.type = JVal::Type::Number; v.numVal = r.be<double>(); return v; }
            case 0xcc: { JVal v; v.type = JVal::Type::Number; v.numVal = r.u8(); return v; }
            case 0xcd: { JVal v; v.type = JVal::Type::Number; v.numVal = r.be<uint16_t>(); return v; }
            case 0xce: { JVal v; v.type = JVal::Type::Number; v.numVal = r.be<uint32_t>(); return v; }
            case 0xcf: { JVal v; v.type = JVal::Type::Number; v.numVal = static_cast<double>(r.be<uint64_t>()); return v; }
            case 0xd0: { JVal v; v.type = JVal::Type::Number; v.numVal = static_cast<int8_t>(r.u8()); return v; }
            case 0xd1: { JVal v; v.type = JVal::Type::Number; v.numVal = r.be<int16_t>(); return v; }
            case 0xd2: { JVal v; v.type = JVal::Type::Number; v.numVal = r.be<int32_t>(); return v; }
            case 0xd3: { JVal v; v.type = JVal::Type::Number; v.numVal = static_cast<double>(r.be<int64_t>()); return v; }
            case 0xd9: { JVal v; v.type = JVal::Type::String; v.strVal = readStr(r.u8()); return v; }
            case 0xda: { JVal v; v.type = JVal::Type::String; v.strVal = readStr(r.be<uint16_t>()); return v; }
            case 0xdb: { JVal v; v.type = JVal::Type::String; v.strVal = readStr(r.be<uint32_t>()); return v; }
            case 0xdc: return readArr(r.be<uint16_t>(), depth);
            case 0xdd: return readArr(r.be<uint32_t>(), depth);
            case 0xde: return readMap(r.be<uint16_t>(), depth);
            case 0xdf: return readMap(r.be<uint32_t>(), depth);
            default: r.bad = true; return {};
        }
    }

    JVal readArr(size_t n, int depth) {
        if (n > 2000000) { r.bad = true; return {}; }
        JVal v;
        v.type = JVal::Type::Array;
        v.arrVal.reserve(std::min<size_t>(n, 4096));
        for (size_t i = 0; i < n && !r.bad; ++i) {
            v.arrVal.push_back(read(depth + 1));
        }
        return v;
    }

    JVal readMap(size_t n, int depth) {
        if (n > 2000000) { r.bad = true; return {}; }
        JVal v;
        v.type = JVal::Type::Object;
        for (size_t i = 0; i < n && !r.bad; ++i) {
            JVal k = read(depth + 1);
            JVal val = read(depth + 1);
            if (k.isString()) v.objVal.emplace(std::move(k.strVal), std::move(val));
        }
        return v;
    }
};

// 1. Mega Hack Replay JSON (.mhr.json)
MacroLoadResult parseMhrJson(JVal const& root) {
    MacroLoadResult res;
    auto& m = res.macro;
    m.format = "Mega Hack JSON (.mhr.json)";
    m.botName = "Mega Hack";

    auto const* meta = root.get("meta");
    if (!meta || !meta->isObject()) return fail("Mega Hack JSON macro is missing 'meta' object");
    auto const* fpsVal = meta->get("fps");
    m.tps = fpsVal ? fpsVal->asDouble(240.0) : 240.0;

    auto const* events = root.get("events");
    if (!events || !events->isArray()) return fail("Mega Hack JSON macro is missing 'events' array");

    for (auto const& ev : events->arrVal) {
        if (!ev.isObject()) continue;
        // Skip physics-only checkpoint entries that do not have a 'down' boolean
        auto const* downVal = ev.get("down");
        if (!downVal) continue;
        auto const* frameVal = ev.get("frame");
        if (!frameVal) continue;

        MacroInput in;
        in.frame = frameVal->asInt(0);
        in.down = downVal->asBool(false);
        if (auto const* p2Val = ev.get("p2")) {
            in.player2 = p2Val->asBool(false);
        } else if (auto const* p2Alt = ev.get("player2")) {
            in.player2 = p2Alt->asBool(false);
        }
        if (auto const* btnVal = ev.get("btn")) {
            int b = static_cast<int>(btnVal->asInt(1));
            in.button = static_cast<uint8_t>(std::clamp(b, 1, 3));
        } else if (auto const* btnAlt = ev.get("button")) {
            int b = static_cast<int>(btnAlt->asInt(1));
            in.button = static_cast<uint8_t>(std::clamp(b, 1, 3));
        }
        auto const* xVal = ev.get("x");
        auto const* yVal = ev.get("y");
        if (xVal || yVal) {
            in.hasPosition = true;
            in.x = static_cast<float>(xVal ? xVal->asDouble(0.0) : 0.0);
            in.y = static_cast<float>(yVal ? yVal->asDouble(0.0) : 0.0);
        }
        m.inputs.push_back(in);
    }

    res.ok = true;
    return res;
}

// 2. Mega Hack Replay Binary (.mhr - "HACK" header)
MacroLoadResult parseMhrBinary(std::vector<uint8_t> const& data) {
    if (data.size() < 32) {
        return fail("Mega Hack binary (.mhr) file is too small (< 32 bytes)");
    }
    ByteReader r(data, 0);
    uint32_t magic = r.be<uint32_t>();
    if (magic != 0x4841434Bu) { // "HACK"
        return fail("Invalid Mega Hack binary (.mhr) magic header (expected 'HACK')");
    }

    r.pos = 12;
    uint32_t fps = r.le<uint32_t>();
    r.pos = 28;
    uint32_t numActions = r.le<uint32_t>();
    if (r.bad) {
        return fail("Damaged Mega Hack binary (.mhr) header");
    }

    size_t requiredBytes = static_cast<size_t>(numActions) * 32u;
    if (r.left() < requiredBytes) {
        return fail("Truncated Mega Hack binary (.mhr) file: fewer action records than header specifies");
    }

    MacroLoadResult res;
    auto& m = res.macro;
    m.format = "Mega Hack Binary (.mhr)";
    m.botName = "Mega Hack";
    m.tps = fps > 0 ? static_cast<double>(fps) : 240.0;
    m.inputs.reserve(numActions);

    for (uint32_t i = 0; i < numActions; ++i) {
        r.skip(2); // 2-byte record header/flags
        uint8_t downByte = r.u8();
        uint8_t p1Byte = r.u8();
        uint32_t frame = r.le<uint32_t>();

        // 24 bytes of physics state: float x, float y, float y_accel, float rot, ...
        float x = r.le<float>();
        float y = r.le<float>();
        r.skip(16);

        if (r.bad) break;

        MacroInput in;
        in.frame = static_cast<int64_t>(frame);
        in.button = 1;
        in.down = (downByte == 1);
        in.player2 = (p1Byte != 0);
        if (std::isfinite(x) && std::isfinite(y) && (x != 0.0f || y != 0.0f)) {
            in.hasPosition = true;
            in.x = x;
            in.y = y;
        }
        m.inputs.push_back(in);
    }

    res.ok = true;
    return res;
}

// 3. GDReplayFormat v2 (.gdr2 / .gdr with "GDR" magic)
MacroLoadResult parseGdr2(std::vector<uint8_t> const& data) {
    if (data.size() < 4 || std::memcmp(data.data(), "GDR", 3) != 0) {
        return fail("Not a valid GDR2 file (missing 'GDR' magic)");
    }
    ByteReader r(data, 3);
    uint64_t version = r.varint();
    if (r.bad || version != 2) {
        return fail("Unsupported GDR binary version: " + std::to_string(version));
    }

    MacroLoadResult res;
    auto& m = res.macro;
    std::string inputTag = r.varstr();
    m.author = r.varstr();
    std::string description = r.varstr();
    (void)description;
    m.durationSeconds = static_cast<double>(r.be<float>());
    (void)r.varint(); // gameVersion
    double framerate = r.be<double>();
    (void)r.varint(); // seed
    (void)r.varint(); // coins
    (void)r.varint(); // ldm
    bool platformer = r.varint() != 0;
    m.botName = r.varstr();
    (void)r.varint(); // bot version
    m.levelId = static_cast<uint32_t>(r.varint());
    m.levelName = r.varstr();

    uint64_t extSize = r.varint();
    r.skip(static_cast<size_t>(extSize));

    uint64_t deaths = r.varint();
    for (uint64_t i = 0; i < deaths && !r.bad; ++i) {
        (void)r.varint();
    }

    uint64_t totalInputs = r.varint();
    uint64_t p1Inputs = r.varint();
    if (r.bad) {
        return fail("GDR2 header is damaged or truncated");
    }

    bool hasInputExt = !inputTag.empty();
    uint64_t prevFrame = 0;
    uint64_t p1Remaining = p1Inputs;

    m.inputs.reserve(std::min<size_t>(static_cast<size_t>(totalInputs), 500000));

    while (r.left() > 0 && m.inputs.size() < static_cast<size_t>(totalInputs)) {
        uint64_t packed = r.varint();
        if (r.bad) break;

        uint64_t delta = platformer ? (packed >> 3) : (packed >> 1);
        uint64_t frame = prevFrame + delta;
        uint8_t button = platformer ? static_cast<uint8_t>((packed >> 1) & 3u) : 1u;
        if (button == 0) button = 1;
        bool isP2 = (p1Remaining == 0);
        bool down = (packed & 1u) != 0;

        MacroInput in;
        in.frame = static_cast<int64_t>(frame);
        in.button = button;
        in.player2 = isP2;
        in.down = down;

        if (hasInputExt && r.left() > 0) {
            uint64_t extLen = r.varint();
            if (r.bad || r.left() < static_cast<size_t>(extLen)) {
                m.inputs.push_back(in);
                break;
            }
            if (inputTag == "Phys" && extLen >= 8) {
                size_t savedPos = r.pos;
                float px = r.be<float>();
                float py = r.be<float>();
                if (std::isfinite(px) && std::isfinite(py)) {
                    in.hasPosition = true;
                    in.x = px;
                    in.y = py;
                }
                r.pos = savedPos + static_cast<size_t>(extLen);
            } else {
                r.skip(static_cast<size_t>(extLen));
            }
        }

        prevFrame = frame;
        m.inputs.push_back(in);

        if (p1Remaining > 0) {
            --p1Remaining;
            if (p1Remaining == 0) {
                prevFrame = 0;
            }
        }
    }

    m.format = "GDR2 (.gdr2)";
    m.tps = (framerate > 0.0 && std::isfinite(framerate)) ? framerate : 240.0;
    m.platformer = platformer;
    m.practiceDeaths = static_cast<size_t>(deaths);
    res.ok = true;
    return res;
}

// 4. GDReplayFormat v1 (.gdr JSON or MessagePack)
MacroLoadResult parseGdrV1(JVal const& root, std::string const& fmtLabel) {
    MacroLoadResult res;
    auto& m = res.macro;
    m.format = fmtLabel;
    if (auto const* fr = root.get("framerate")) m.tps = fr->asDouble(240.0);
    else if (auto const* fps = root.get("fps")) m.tps = fps->asDouble(240.0);
    else m.tps = 240.0;

    if (auto const* dur = root.get("duration")) m.durationSeconds = dur->asDouble(0.0);
    if (auto const* auth = root.get("author")) m.author = auth->asString();

    if (auto const* bot = root.get("bot")) {
        if (bot->isObject()) {
            if (auto const* bn = bot->get("name")) m.botName = bn->asString();
        } else if (bot->isString()) {
            m.botName = bot->asString();
        }
    }
    if (auto const* lvl = root.get("level")) {
        if (lvl->isObject()) {
            if (auto const* lid = lvl->get("id")) m.levelId = static_cast<uint32_t>(lid->asInt(0));
            if (auto const* ln = lvl->get("name")) m.levelName = ln->asString();
        }
    }

    auto const* inputs = root.get("inputs");
    if (!inputs || !inputs->isArray()) return fail("GDR macro is missing 'inputs' array");

    for (auto const& item : inputs->arrVal) {
        if (!item.isObject()) continue;
        auto const* frameVal = item.get("frame");
        if (!frameVal) continue;

        MacroInput in;
        in.frame = frameVal->asInt(0);
        if (auto const* d = item.get("down")) in.down = d->asBool(false);
        else if (auto const* h = item.get("holding")) in.down = h->asBool(false);

        if (auto const* p2 = item.get("2p")) in.player2 = p2->asBool(false);
        else if (auto const* p2b = item.get("player2")) in.player2 = p2b->asBool(false);
        else if (auto const* p2c = item.get("player_2")) in.player2 = p2c->asBool(false);

        if (auto const* btn = item.get("btn")) {
            in.button = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(btn->asInt(1)), 1, 3));
        } else if (auto const* btn2 = item.get("button")) {
            in.button = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(btn2->asInt(1)), 1, 3));
        }

        if (auto const* corr = item.get("correction")) {
            if (corr->isObject()) {
                auto const* xp = corr->get("xPos");
                auto const* yp = corr->get("yPos");
                if (xp || yp) {
                    in.hasPosition = true;
                    in.x = static_cast<float>(xp ? xp->asDouble(0.0) : 0.0);
                    in.y = static_cast<float>(yp ? yp->asDouble(0.0) : 0.0);
                }
            }
        }
        m.inputs.push_back(in);
    }

    res.ok = true;
    return res;
}

// 5. TASBot JSON (.json)
MacroLoadResult parseTasbotJson(JVal const& root) {
    MacroLoadResult res;
    auto& m = res.macro;
    m.format = "TASBot (.json)";
    m.botName = "TASBot";
    if (auto const* fps = root.get("fps")) m.tps = fps->asDouble(240.0);

    auto const* macroArr = root.get("macro");
    if (!macroArr || !macroArr->isArray()) return fail("TASBot JSON is missing 'macro' array");

    int64_t prevP1 = 0;
    int64_t prevP2 = 0;
    for (auto const& ev : macroArr->arrVal) {
        if (!ev.isObject()) continue;
        auto const* frameVal = ev.get("frame");
        if (!frameVal) continue;
        int64_t frame = frameVal->asInt(0);

        float xPos = 0.0f;
        bool hasX = false;
        int64_t p1Click = 0;
        int64_t p2Click = 0;

        if (auto const* p1 = ev.get("player_1"); p1 && p1->isObject()) {
            if (auto const* c = p1->get("click")) p1Click = c->asInt(0);
            if (auto const* x = p1->get("x_position")) { xPos = static_cast<float>(x->asDouble(0.0)); hasX = true; }
        }
        if (auto const* p2 = ev.get("player_2"); p2 && p2->isObject()) {
            if (auto const* c = p2->get("click")) p2Click = c->asInt(0);
        }

        if (p1Click == 1 || p1Click == 2) {
            if (p1Click == 1 && prevP1 == 1) {
                MacroInput rel{frame, 1, false, false, hasX, xPos, 0.0f};
                m.inputs.push_back(rel);
            }
            MacroInput in{frame, 1, false, p1Click == 1, hasX, xPos, 0.0f};
            m.inputs.push_back(in);
            prevP1 = p1Click;
        }
        if (p2Click == 1 || p2Click == 2) {
            if (p2Click == 1 && prevP2 == 1) {
                MacroInput rel{frame, 1, true, false, hasX, xPos, 0.0f};
                m.inputs.push_back(rel);
            }
            MacroInput in{frame, 1, true, p2Click == 1, hasX, xPos, 0.0f};
            m.inputs.push_back(in);
            prevP2 = p2Click;
        }
    }

    res.ok = true;
    return res;
}

// 6. zBot Frame (.zbf)
MacroLoadResult parseZbf(std::vector<uint8_t> const& data) {
    ByteReader r(data);
    float delta = r.le<float>();
    float speed = r.le<float>();
    if (r.bad || !(delta > 0.0f)) return fail("Invalid or truncated zBot (.zbf) file");
    if (!(speed > 0.0f)) speed = 1.0f;

    MacroLoadResult res;
    auto& m = res.macro;
    m.format = "zBot (.zbf)";
    m.botName = "zBot";
    m.tps = std::round(1.0 / static_cast<double>(delta) / static_cast<double>(speed));
    while (r.left() >= 6) {
        int32_t frame = r.le<int32_t>();
        uint8_t down = r.u8();
        uint8_t p1 = r.u8();
        MacroInput in;
        in.frame = frame;
        in.button = 1;
        in.player2 = (p1 != 0x31);
        in.down = (down == 0x31);
        m.inputs.push_back(in);
    }
    res.ok = true;
    return res;
}

// 7. ReplayEngine 3 (.re3)
MacroLoadResult parseRe3(std::vector<uint8_t> const& data) {
    ByteReader r(data);
    float fps = r.le<float>();
    uint32_t p1f = r.le<uint32_t>();
    uint32_t p2f = r.le<uint32_t>();
    uint32_t p1i = r.le<uint32_t>();
    uint32_t p2i = r.le<uint32_t>();
    if (r.bad) return fail("ReplayEngine 3 (.re3) file is too short");
    size_t need = static_cast<size_t>(p1f + p2f) * 32u + static_cast<size_t>(p1i + p2i) * 16u;
    if (r.left() < need) return fail("ReplayEngine 3 (.re3) file is truncated");

    r.skip(static_cast<size_t>(p1f + p2f) * 32u);
    MacroLoadResult res;
    auto& m = res.macro;
    m.format = "ReplayEngine 3 (.re3)";
    m.botName = "GDH / ReplayEngine 3";
    m.tps = fps > 0.0f ? static_cast<double>(fps) : 240.0;

    for (uint32_t s = 0; s < 2; ++s) {
        uint32_t count = (s == 0) ? p1i : p2i;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t frame = r.le<uint32_t>();
            uint8_t down = r.u8();
            r.skip(3);
            int32_t button = r.le<int32_t>();
            r.u8();
            r.skip(3);
            MacroInput in;
            in.frame = static_cast<int64_t>(frame);
            in.button = static_cast<uint8_t>(std::clamp(button, 1, 3));
            in.player2 = (s == 1);
            in.down = (down != 0);
            m.inputs.push_back(in);
        }
    }
    res.ok = true;
    return res;
}

// 8. xdBot (.xd)
MacroLoadResult parseXd(std::string const& text) {
    MacroLoadResult res;
    auto& m = res.macro;
    m.format = "xdBot (.xd)";
    m.botName = "xdBot";
    m.tps = 240.0;

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        auto parts = split(line, '|');
        if (parts.size() == 1) {
            char* end = nullptr;
            double v = std::strtod(parts[0].c_str(), &end);
            if (end != parts[0].c_str() && v > 0.0) m.tps = v;
            continue;
        }
        if (parts.size() < 4) continue;
        try {
            MacroInput in;
            in.frame = std::stoll(parts[0]);
            in.down = (std::stoi(parts[1]) == 1);
            int b = std::stoi(parts[2]);
            in.button = static_cast<uint8_t>((b == 2 || b == 3) ? b : 1);
            in.player2 = (std::stoi(parts[3]) != 1);
            m.inputs.push_back(in);
        } catch (...) {}
    }
    res.ok = true;
    return res;
}

// 9. Plaintext (.txt: first line TPS, then `frame down button p1`)
MacroLoadResult parsePlainText(std::string const& text) {
    MacroLoadResult res;
    auto& m = res.macro;
    m.format = "Plaintext (.txt)";
    std::istringstream ss(text);
    std::string line;
    if (!std::getline(ss, line)) return fail("Plaintext macro is empty");
    line = trim(line);
    char* end = nullptr;
    double tps = std::strtod(line.c_str(), &end);
    if (end == line.c_str() || !(tps > 0.0)) {
        return fail("First line of a .txt macro must be the TPS/FPS number");
    }
    m.tps = tps;
    while (std::getline(ss, line)) {
        std::istringstream ls(trim(line));
        double frame = 0;
        int down = 0;
        int button = 1;
        int p1 = 1;
        if (!(ls >> frame >> down)) continue;
        if (!(ls >> button)) button = 1;
        if (!(ls >> p1)) p1 = 1;
        MacroInput in;
        in.frame = static_cast<int64_t>(std::llround(frame));
        in.down = (down == 1);
        in.button = static_cast<uint8_t>(std::clamp(button, 1, 3));
        in.player2 = (p1 == 0);
        m.inputs.push_back(in);
    }
    res.ok = true;
    return res;
}

MacroLoadResult parseJsonString(std::string const& text) {
    JsonReader jr(text);
    JVal root = jr.parseValue();
    if (jr.bad || !root.isObject()) {
        return fail("Malformed JSON syntax in macro file");
    }
    if (root.contains("meta") && root.contains("events")) {
        return parseMhrJson(root);
    }
    if (root.contains("macro") && root.contains("fps")) {
        return parseTasbotJson(root);
    }
    if (root.contains("inputs")) {
        return parseGdrV1(root, "GDR JSON (.gdr.json)");
    }
    return fail("Unrecognized JSON macro schema (expected Mega Hack .mhr.json, GDR .gdr.json, or TASBot .json)");
}

} // namespace

std::vector<std::string> supportedMacroFilterPatterns() {
    return {"*.mhr", "*.gdr2", "*.gdr", "*.json", "*.zbf", "*.re3", "*.xd", "*.txt"};
}

MacroLoadResult parseMacroBytes(std::vector<uint8_t> const& data, std::string const& fileName) {
    if (data.empty()) {
        return fail("Macro file is empty");
    }

    std::string lowerName = toLower(fileName);

    size_t firstNonWs = 0;
    while (firstNonWs < data.size() && std::isspace(data[firstNonWs])) ++firstNonWs;
    bool startsWithJsonBrace = (firstNonWs < data.size() && data[firstNonWs] == '{');

    MacroLoadResult res;

    // 1. Check magic headers first
    if (data.size() >= 4 && std::memcmp(data.data(), "HACK", 4) == 0) {
        res = parseMhrBinary(data);
    } else if (data.size() >= 3 && std::memcmp(data.data(), "GDR", 3) == 0) {
        res = parseGdr2(data);
    } else if (startsWithJsonBrace) {
        res = parseJsonString(std::string(reinterpret_cast<char const*>(data.data()), data.size()));
    } else if (endsWith(lowerName, ".mhr")) {
        res = parseMhrBinary(data);
    } else if (endsWith(lowerName, ".zbf")) {
        res = parseZbf(data);
    } else if (endsWith(lowerName, ".re3")) {
        res = parseRe3(data);
    } else if (endsWith(lowerName, ".xd")) {
        res = parseXd(std::string(reinterpret_cast<char const*>(data.data()), data.size()));
    } else if (endsWith(lowerName, ".txt")) {
        res = parsePlainText(std::string(reinterpret_cast<char const*>(data.data()), data.size()));
    } else if (endsWith(lowerName, ".gdr") || (data[0] & 0xf0u) == 0x80u || data[0] == 0xdeu || data[0] == 0xdfu) {
        MsgPackReader mp(data);
        JVal root = mp.read();
        if (mp.r.bad || !root.isObject()) {
            return fail("Could not parse .gdr file (invalid MessagePack/JSON/GDR2 data)");
        }
        res = parseGdrV1(root, "GDR MessagePack (.gdr)");
    } else {
        return fail("Unsupported macro format. Supported: Mega Hack (.mhr, .mhr.json), GDR2 (.gdr2), GDR (.gdr), TASBot (.json), zBot (.zbf), GDH (.re3), xdBot (.xd), Plaintext (.txt)");
    }

    if (!res.ok) return res;

    auto& inputs = res.macro.inputs;
    inputs.erase(
        std::remove_if(inputs.begin(), inputs.end(), [](MacroInput const& i) { return i.frame < 0; }),
        inputs.end()
    );
    std::stable_sort(inputs.begin(), inputs.end(), [](MacroInput const& a, MacroInput const& b) {
        return a.frame < b.frame;
    });

    if (inputs.empty()) {
        return fail("Macro file contains 0 valid gameplay inputs");
    }

    if (!(res.macro.tps > 0.0) || !std::isfinite(res.macro.tps)) {
        res.macro.tps = 240.0;
    }

    for (auto const& in : inputs) {
        if (in.button == 2 || in.button == 3) {
            res.macro.platformer = true;
            break;
        }
    }

    if (res.macro.durationSeconds <= 0.0) {
        res.macro.durationSeconds = static_cast<double>(inputs.back().frame) / res.macro.tps;
    }

    res.macro.name = fileName;
    return res;
}

MacroLoadResult loadMacroFromFile(std::filesystem::path const& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || std::filesystem::is_directory(path, ec)) {
        return fail("Macro file does not exist: " + path.filename().string());
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return fail("Could not open macro file: " + path.filename().string());
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parseMacroBytes(bytes, path.filename().string());
}

} // namespace ffc
