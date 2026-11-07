#include "Categories.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace ffc {

namespace {

std::string hexEncode(std::string const& s) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) {
        out.push_back(kHex[(c >> 4) & 0xF]);
        out.push_back(kHex[c & 0xF]);
    }
    return out;
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

std::string hexDecode(std::string const& hex) {
    if (hex.size() % 2 != 0) return "";
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = hexVal(hex[i]);
        int lo = hexVal(hex[i + 1]);
        if (hi < 0 || lo < 0) return "";
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

std::vector<std::string> splitStr(std::string const& s, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        parts.push_back(item);
    }
    return parts;
}

} // namespace

CategoryConfig::CategoryConfig() {
    resetToDefaults();
}

void CategoryConfig::resetToDefaults() {
    m_categories.clear();
    m_tierColors = TierColors{};
    m_pitchDir = PitchDirection::High12FToLow1F;
    for (int f = 1; f <= 12; ++f) {
        FrameCategory cat;
        cat.frames = f;
        cat.enabled = true;
        cat.isCustom = false;
        cat.hasCustomColor = false;
        cat.customColor = defaultTierColorForFrames(f, m_tierColors);
        m_categories.push_back(cat);
    }
}

std::vector<FrameCategory> CategoryConfig::enabledCategoriesAscending() const {
    std::vector<FrameCategory> out;
    for (auto const& c : m_categories) {
        if (c.enabled) out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](FrameCategory const& a, FrameCategory const& b) {
        return a.frames < b.frames;
    });
    return out;
}

std::vector<FrameCategory> CategoryConfig::enabledCategoriesDescending() const {
    auto out = enabledCategoriesAscending();
    std::reverse(out.begin(), out.end());
    return out;
}

FrameCategory* CategoryConfig::findMutable(int frames) {
    for (auto& c : m_categories) {
        if (c.frames == frames) return &c;
    }
    return nullptr;
}

FrameCategory const* CategoryConfig::find(int frames) const {
    for (auto const& c : m_categories) {
        if (c.frames == frames) return &c;
    }
    return nullptr;
}

bool CategoryConfig::setCategoryEnabled(int frames, bool enabled) {
    auto* c = findMutable(frames);
    if (!c) return false;
    c->enabled = enabled;
    return true;
}

bool CategoryConfig::isCategoryEnabled(int frames) const {
    auto const* c = find(frames);
    return c && c->enabled;
}

bool CategoryConfig::addCustomCategory(int frames, bool enabled) {
    if (frames < 1 || frames > 999) return false;
    if (auto* existing = findMutable(frames)) {
        existing->enabled = enabled;
        return true;
    }
    FrameCategory cat;
    cat.frames = frames;
    cat.enabled = enabled;
    cat.isCustom = (frames < 1 || frames > 12);
    cat.hasCustomColor = false;
    cat.customColor = defaultTierColorForFrames(frames, m_tierColors);
    m_categories.push_back(cat);
    sortCategories();
    return true;
}

bool CategoryConfig::removeCustomCategory(int frames) {
    if (frames >= 1 && frames <= 12) return false; // Built-in categories are disabled, not removed
    auto it = std::remove_if(m_categories.begin(), m_categories.end(), [frames](FrameCategory const& c) {
        return c.frames == frames;
    });
    if (it == m_categories.end()) return false;
    m_categories.erase(it, m_categories.end());
    return true;
}

bool CategoryConfig::setCategoryCustomColor(int frames, RGBColor color) {
    auto* c = findMutable(frames);
    if (!c) return false;
    c->hasCustomColor = true;
    c->customColor = color;
    return true;
}

bool CategoryConfig::clearCategoryCustomColor(int frames) {
    auto* c = findMutable(frames);
    if (!c) return false;
    c->hasCustomColor = false;
    return true;
}

bool CategoryConfig::setCategoryCustomSound(int frames, std::string const& path) {
    auto* c = findMutable(frames);
    if (!c) return false;
    c->customSoundPath = path;
    return true;
}

std::string CategoryConfig::getCategoryCustomSound(int frames) const {
    auto const* c = find(frames);
    return c ? c->customSoundPath : "";
}

RGBColor CategoryConfig::defaultTierColorForFrames(int frames, TierColors const& tiers) {
    if (frames <= 4) return tiers.range1To4;   // 1F-4F: Red
    if (frames <= 6) return tiers.range5To6;   // 5F-6F: Orange
    if (frames <= 9) return tiers.range7To9;   // 7F-9F: Yellow
    return tiers.range10Plus;                  // 10F-12F+: Blue
}

RGBColor CategoryConfig::colorForFrames(int frames) const {
    if (auto const* c = find(frames)) {
        if (c->hasCustomColor) return c->customColor;
    }
    return defaultTierColorForFrames(frames, m_tierColors);
}

std::string CategoryConfig::builtinSoundResourceForFrames(int frames) const {
    int clamped = std::clamp(frames, 1, 12);
    int resourceIdx = (m_pitchDir == PitchDirection::High12FToLow1F) ? clamped : (13 - clamped);
    return "ding_" + std::to_string(resourceIdx) + "f.wav";
}

double CategoryConfig::frequencyHzForFrames(int frames) const {
    int clamped = std::clamp(frames, 1, 24);
    double step = (m_pitchDir == PitchDirection::High12FToLow1F)
        ? static_cast<double>(clamped - 1)
        : static_cast<double>(12 - clamped);
    return 440.0 * std::pow(2.0, step / 7.0);
}

float CategoryConfig::fmodPitchMultiplierForFrames(int frames) const {
    // Centered around 6.5F so custom audio files stay natural around 1.0x pitch
    double clamped = std::clamp(static_cast<double>(frames), 1.0, 24.0);
    double delta = (m_pitchDir == PitchDirection::High12FToLow1F)
        ? (clamped - 6.5) / 11.0
        : (6.5 - clamped) / 11.0;
    double mult = std::pow(2.0, delta * 0.85);
    return static_cast<float>(std::clamp(mult, 0.5, 2.2));
}

int CategoryConfig::snapToNearestEnabledCategory(double rawFrames) const {
    auto enabled = enabledCategoriesAscending();
    if (enabled.empty()) return -1;
    int bestFrames = enabled.front().frames;
    double bestDist = std::abs(rawFrames - static_cast<double>(bestFrames));
    for (size_t i = 1; i < enabled.size(); ++i) {
        double d = std::abs(rawFrames - static_cast<double>(enabled[i].frames));
        if (d < bestDist) {
            bestDist = d;
            bestFrames = enabled[i].frames;
        }
    }
    return bestFrames;
}

void CategoryConfig::sortCategories() {
    std::sort(m_categories.begin(), m_categories.end(), [](FrameCategory const& a, FrameCategory const& b) {
        return a.frames < b.frames;
    });
}

std::string CategoryConfig::serialize() const {
    std::ostringstream ss;
    ss << "v1|pitch:" << static_cast<int>(m_pitchDir)
       << "|tiers:"
       << static_cast<int>(m_tierColors.range1To4.r) << "," << static_cast<int>(m_tierColors.range1To4.g) << "," << static_cast<int>(m_tierColors.range1To4.b) << ";"
       << static_cast<int>(m_tierColors.range5To6.r) << "," << static_cast<int>(m_tierColors.range5To6.g) << "," << static_cast<int>(m_tierColors.range5To6.b) << ";"
       << static_cast<int>(m_tierColors.range7To9.r) << "," << static_cast<int>(m_tierColors.range7To9.g) << "," << static_cast<int>(m_tierColors.range7To9.b) << ";"
       << static_cast<int>(m_tierColors.range10Plus.r) << "," << static_cast<int>(m_tierColors.range10Plus.g) << "," << static_cast<int>(m_tierColors.range10Plus.b)
       << "|cats:";
    for (size_t i = 0; i < m_categories.size(); ++i) {
        auto const& c = m_categories[i];
        if (i > 0) ss << ";";
        ss << c.frames << ","
           << (c.enabled ? 1 : 0) << ","
           << (c.isCustom ? 1 : 0) << ","
           << (c.hasCustomColor ? 1 : 0) << ","
           << static_cast<int>(c.customColor.r) << ","
           << static_cast<int>(c.customColor.g) << ","
           << static_cast<int>(c.customColor.b) << ","
           << hexEncode(c.customSoundPath);
    }
    return ss.str();
}

bool CategoryConfig::deserialize(std::string const& data) {
    if (data.empty()) return false;
    auto sections = splitStr(data, '|');
    if (sections.empty() || sections[0] != "v1") return false;

    CategoryConfig temp;
    for (size_t s = 1; s < sections.size(); ++s) {
        auto const& sec = sections[s];
        if (sec.rfind("pitch:", 0) == 0) {
            int p = std::atoi(sec.substr(6).c_str());
            temp.m_pitchDir = (p == 1) ? PitchDirection::High1FToLow12F : PitchDirection::High12FToLow1F;
        } else if (sec.rfind("tiers:", 0) == 0) {
            auto tparts = splitStr(sec.substr(6), ';');
            if (tparts.size() == 4) {
                RGBColor* targets[4] = {
                    &temp.m_tierColors.range1To4,
                    &temp.m_tierColors.range5To6,
                    &temp.m_tierColors.range7To9,
                    &temp.m_tierColors.range10Plus
                };
                for (int ti = 0; ti < 4; ++ti) {
                    auto rgb = splitStr(tparts[ti], ',');
                    if (rgb.size() == 3) {
                        targets[ti]->r = static_cast<uint8_t>(std::clamp(std::atoi(rgb[0].c_str()), 0, 255));
                        targets[ti]->g = static_cast<uint8_t>(std::clamp(std::atoi(rgb[1].c_str()), 0, 255));
                        targets[ti]->b = static_cast<uint8_t>(std::clamp(std::atoi(rgb[2].c_str()), 0, 255));
                    }
                }
            }
        } else if (sec.rfind("cats:", 0) == 0) {
            auto cparts = splitStr(sec.substr(5), ';');
            std::vector<FrameCategory> parsedCats;
            for (auto const& cp : cparts) {
                if (cp.empty()) continue;
                auto fields = splitStr(cp, ',');
                if (fields.size() < 7) continue;
                FrameCategory fc;
                fc.frames = std::clamp(std::atoi(fields[0].c_str()), 1, 999);
                fc.enabled = std::atoi(fields[1].c_str()) != 0;
                fc.isCustom = std::atoi(fields[2].c_str()) != 0 || fc.frames > 12;
                fc.hasCustomColor = std::atoi(fields[3].c_str()) != 0;
                fc.customColor.r = static_cast<uint8_t>(std::clamp(std::atoi(fields[4].c_str()), 0, 255));
                fc.customColor.g = static_cast<uint8_t>(std::clamp(std::atoi(fields[5].c_str()), 0, 255));
                fc.customColor.b = static_cast<uint8_t>(std::clamp(std::atoi(fields[6].c_str()), 0, 255));
                if (fields.size() >= 8) {
                    fc.customSoundPath = hexDecode(fields[7]);
                }
                parsedCats.push_back(fc);
            }
            if (!parsedCats.empty()) {
                temp.m_categories = std::move(parsedCats);
                temp.sortCategories();
            }
        }
    }
    *this = std::move(temp);
    return true;
}

} // namespace ffc
