#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ffc {

struct RGBColor {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;

    constexpr bool operator==(RGBColor const& o) const {
        return r == o.r && g == o.g && b == o.b;
    }
    constexpr bool operator!=(RGBColor const& o) const {
        return !(*this == o);
    }
};

enum class PitchDirection : uint8_t {
    // Default per specification: 12F has the highest pitch, getting progressively lower down to 1F (lowest pitch).
    High12FToLow1F = 0,
    // Optional alternative: 1F highest pitch down to 12F lowest pitch.
    High1FToLow12F = 1,
};

struct TierColors {
    RGBColor range1To4   = {255, 59,  48};  // 1F-4F: Red
    RGBColor range5To6   = {255, 149, 0};   // 5F-6F: Orange
    RGBColor range7To9   = {255, 204, 0};   // 7F-9F: Yellow
    RGBColor range10Plus = {50,  173, 230}; // 10F-12F+: Blue
};

struct FrameCategory {
    int frames = 1;                // e.g. 1..12 or custom (>12)
    bool enabled = true;           // whether this category is active
    bool isCustom = false;         // true if user-added outside 1..12
    bool hasCustomColor = false;   // true if overridden independently of tier color
    RGBColor customColor = {255, 255, 255};
    std::string customSoundPath;   // optional per-category custom sound file path

    // Short circle/counter label, e.g. "1F", "4F", "12F"
    std::string shortLabel() const {
        return std::to_string(frames) + "F";
    }
    std::string label() const {
        return shortLabel();
    }

    // Full sound guide label, e.g. "12-frame window", "1-frame window"
    std::string guideDescription() const {
        return std::to_string(frames) + "-frame window";
    }
};

class CategoryConfig {
public:
    CategoryConfig();

    void resetToDefaults();

    std::vector<FrameCategory> const& allCategories() const { return m_categories; }
    std::vector<FrameCategory> enabledCategoriesAscending() const;
    std::vector<FrameCategory> enabledCategoriesDescending() const;

    FrameCategory const* findCategory(int frames) const { return find(frames); }

    static std::string labelForFrames(int frames) {
        return std::to_string(frames) + "F";
    }
    static std::string guideDescriptionForFrames(int frames) {
        return std::to_string(frames) + "-frame window";
    }

    bool setCategoryEnabled(int frames, bool enabled);
    bool isCategoryEnabled(int frames) const;

    // Add a custom frame number category (e.g. 15F, 20F). Returns false if invalid (<1 or >999).
    bool addCustomCategory(int frames, bool enabled = true);
    // Remove a custom category (cannot remove built-in 1F..12F; disable them instead).
    bool removeCustomCategory(int frames);

    bool setCategoryCustomColor(int frames, RGBColor color);
    bool clearCategoryCustomColor(int frames);

    bool setCategoryCustomSound(int frames, std::string const& path);
    bool clearCategoryCustomSound(int frames) { return setCategoryCustomSound(frames, ""); }
    std::string getCategoryCustomSound(int frames) const;

    void setTierColors(TierColors const& colors) { m_tierColors = colors; }
    TierColors const& tierColors() const { return m_tierColors; }

    RGBColor colorForFrames(int frames) const;
    static RGBColor defaultTierColorForFrames(int frames, TierColors const& tiers = TierColors{});

    void setPitchDirection(PitchDirection dir) { m_pitchDir = dir; }
    PitchDirection pitchDirection() const { return m_pitchDir; }

    // Built-in resource filename (e.g. "ding_12f.wav") for a category
    std::string builtinSoundResourceForFrames(int frames) const;

    // Frequency in Hz for the synthesized built-in ding of a category
    double frequencyHzForFrames(int frames) const;
    float pitchHzForFrames(int frames) const {
        return static_cast<float>(frequencyHzForFrames(frames));
    }

    // FMOD playback speed/pitch multiplier when using a single custom sound file
    float fmodPitchMultiplierForFrames(int frames) const;

    // Snap an arbitrary continuous frame window value to the nearest enabled category.
    // Returns -1 if no categories are enabled.
    int snapToNearestEnabledCategory(double rawFrames) const;

    // Serialize/deserialize state for persistence between sessions
    std::string serialize() const;
    bool deserialize(std::string const& data);

private:
    void sortCategories();
    FrameCategory* findMutable(int frames);
    FrameCategory const* find(int frames) const;

    std::vector<FrameCategory> m_categories;
    TierColors m_tierColors;
    PitchDirection m_pitchDir = PitchDirection::High12FToLow1F;
};

} // namespace ffc
