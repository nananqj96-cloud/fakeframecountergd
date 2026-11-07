#include "../src/core/Categories.hpp"
#include "../src/core/Macro.hpp"
#include "../src/core/MacroPlayer.hpp"
#include "../src/core/MomentDetector.hpp"
#include "../src/core/RealAnalyzer.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

using namespace ffc;

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool cond, char const* expr, char const* file, int line) {
    if (cond) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] " << file << ":" << line << " -> " << expr << "\n";
    }
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)

void appendLe32(std::vector<uint8_t>& buf, uint32_t v) {
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
}

void appendLeFloat(std::vector<uint8_t>& buf, float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, 4);
    appendLe32(buf, u);
}

void appendBeFloat(std::vector<uint8_t>& buf, float f) {
    uint8_t b[4];
    std::memcpy(b, &f, 4);
    for (int i = 3; i >= 0; --i) buf.push_back(b[i]);
}

void appendBeDouble(std::vector<uint8_t>& buf, double d) {
    uint8_t b[8];
    std::memcpy(b, &d, 8);
    for (int i = 7; i >= 0; --i) buf.push_back(b[i]);
}

void appendVarint(std::vector<uint8_t>& buf, uint64_t v) {
    do {
        uint8_t byte = static_cast<uint8_t>(v & 0x7Fu);
        v >>= 7;
        if (v != 0) byte |= 0x80u;
        buf.push_back(byte);
    } while (v != 0);
}

void appendVarstr(std::vector<uint8_t>& buf, std::string const& s) {
    appendVarint(buf, s.size());
    buf.insert(buf.end(), s.begin(), s.end());
}

void testCategoriesAndSounds() {
    CategoryConfig cfg;
    auto asc = cfg.enabledCategoriesAscending();
    CHECK(asc.size() == 12);
    CHECK(asc.front().frames == 1);
    CHECK(asc.back().frames == 12);
    CHECK(asc.front().shortLabel() == "1F");
    CHECK(asc.back().shortLabel() == "12F");

    // Descending guide order: 12F down to 1F
    auto desc = cfg.enabledCategoriesDescending();
    CHECK(desc.size() == 12);
    CHECK(desc[0].frames == 12);
    CHECK(desc[0].guideDescription() == "12-frame window");
    CHECK(desc[11].frames == 1);
    CHECK(desc[11].guideDescription() == "1-frame window");

    // Verify default color tiers: 1F-4F Red, 5F-6F Orange, 7F-9F Yellow, 10F-12F Blue
    RGBColor red{255, 59, 48};
    RGBColor orange{255, 149, 0};
    RGBColor yellow{255, 204, 0};
    RGBColor blue{50, 173, 230};
    for (int f = 1; f <= 4; ++f) CHECK(cfg.colorForFrames(f) == red);
    for (int f = 5; f <= 6; ++f) CHECK(cfg.colorForFrames(f) == orange);
    for (int f = 7; f <= 9; ++f) CHECK(cfg.colorForFrames(f) == yellow);
    for (int f = 10; f <= 12; ++f) CHECK(cfg.colorForFrames(f) == blue);

    // Verify pitch order: 12F has highest pitch, progressively lower down to 1F (lowest pitch)
    for (int f = 12; f >= 2; --f) {
        CHECK(cfg.frequencyHzForFrames(f) > cfg.frequencyHzForFrames(f - 1));
        CHECK(cfg.fmodPitchMultiplierForFrames(f) > cfg.fmodPitchMultiplierForFrames(f - 1));
    }
    CHECK(cfg.builtinSoundResourceForFrames(12) == "ding_12f.wav");
    CHECK(cfg.builtinSoundResourceForFrames(1) == "ding_1f.wav");

    // Custom categories, disabling, custom colors, custom sounds, and serialization roundtrip
    CHECK(cfg.setCategoryEnabled(2, false));
    CHECK(cfg.setCategoryEnabled(11, false));
    CHECK(cfg.addCustomCategory(15, true));
    CHECK(cfg.setCategoryCustomColor(1, RGBColor{255, 0, 128}));
    CHECK(cfg.setCategoryCustomSound(4, "C:/Users/Test/my_ding_4f.ogg"));

    std::string serialized = cfg.serialize();
    CategoryConfig restored;
    CHECK(restored.deserialize(serialized));
    CHECK(!restored.isCategoryEnabled(2));
    CHECK(!restored.isCategoryEnabled(11));
    CHECK(restored.isCategoryEnabled(15));
    CHECK(restored.colorForFrames(1) == (RGBColor{255, 0, 128}));
    CHECK(restored.getCategoryCustomSound(4) == "C:/Users/Test/my_ding_4f.ogg");
    CHECK(restored.removeCustomCategory(15));
    CHECK(!restored.isCategoryEnabled(15));
}

void testMomentDeduplicationAndAntiSpam() {
    CategoryConfig cats;
    MomentDetector det;

    // 1. Duplicate event on the same tick is rejected
    GameplayInputEvent ev{};
    ev.tick = 100;
    ev.timeSeconds = 100.0 / 240.0;
    ev.button = 1;
    ev.isDown = true;
    ev.x = 300.0f;
    ev.y = 105.0f;
    ev.gamemode = Gamemode::Cube;
    ev.nearestHazardUnits = 40.0f;

    auto r1 = det.evaluate(ev, cats);
    CHECK(r1.accepted);
    auto r1Dup = det.evaluate(ev, cats);
    CHECK(!r1Dup.accepted);
    CHECK(r1Dup.reason == FilterReason::DuplicateEvent);

    // 2. Cube release is ignored by SmartGamemode filter
    GameplayInputEvent rel = ev;
    rel.tick = 112;
    rel.timeSeconds = 112.0 / 240.0;
    rel.isDown = false;
    rel.x = 335.0f;
    auto rRelCube = det.evaluate(rel, cats);
    CHECK(!rRelCube.accepted);
    CHECK(rRelCube.reason == FilterReason::IgnoredReleaseForGamemode);

    // 3. Wave release IS accepted when separated spatially
    GameplayInputEvent wavePress = ev;
    wavePress.tick = 200;
    wavePress.timeSeconds = 200.0 / 240.0;
    wavePress.isDown = true;
    wavePress.x = 600.0f;
    wavePress.y = 120.0f;
    wavePress.gamemode = Gamemode::Wave;
    auto rWavePress = det.evaluate(wavePress, cats);
    CHECK(rWavePress.accepted);

    GameplayInputEvent waveRel = wavePress;
    waveRel.tick = 212;
    waveRel.timeSeconds = 212.0 / 240.0;
    waveRel.isDown = false;
    waveRel.x = 640.0f;
    waveRel.y = 160.0f;
    auto rWaveRel = det.evaluate(waveRel, cats);
    CHECK(rWaveRel.accepted);

    // 4. Dual-mode single-input mirror suppression
    GameplayInputEvent dualP1{};
    dualP1.tick = 350;
    dualP1.timeSeconds = 350.0 / 240.0;
    dualP1.isDown = true;
    dualP1.isPlayer2 = false;
    dualP1.isDualMode = true;
    dualP1.isTwoPlayerMode = false;
    dualP1.x = 950.0f;
    dualP1.y = 110.0f;
    dualP1.nearestHazardUnits = 35.0f;
    CHECK(det.evaluate(dualP1, cats).accepted);

    GameplayInputEvent dualP2 = dualP1;
    dualP2.isPlayer2 = true;
    dualP2.y = 220.0f;
    auto rDualP2 = det.evaluate(dualP2, cats);
    CHECK(!rDualP2.accepted);
    CHECK(rDualP2.reason == FilterReason::DualMirrorDuplicate);

    // 5. Rapid sequence preservation vs excessive spam cap
    det.reset();
    DetectionConfig dcfg;
    dcfg.maxMomentsPerSecond = 8.0f;
    dcfg.burstAllowance = 4;
    dcfg.minDistanceUnits = 15.0f;
    det.setConfig(dcfg);

    int burstAccepted = 0;
    // Simulate a fast 4-click wave burst within 0.32s (80ms apart, moving 30 units each)
    for (int i = 0; i < 4; ++i) {
        GameplayInputEvent p{};
        p.tick = 500 + i * 20;
        p.timeSeconds = static_cast<double>(p.tick) / 240.0;
        p.isDown = true;
        p.gamemode = Gamemode::Wave;
        p.x = 1200.0f + i * 35.0f;
        p.y = 150.0f + (i % 2) * 25.0f;
        p.nearestHazardUnits = 28.0f;
        if (det.evaluate(p, cats).accepted) ++burstAccepted;

        // Release in between to reset button state (at same x/y so release is spatially skipped)
        GameplayInputEvent r = p;
        r.tick += 5;
        r.timeSeconds = static_cast<double>(r.tick) / 240.0;
        r.isDown = false;
        det.evaluate(r, cats);
    }
    // All 4 rapid clicks in the burst must be preserved!
    CHECK(burstAccepted == 4);

    // Now simulate 20 more clicks within the same 1.0s window: total in 1s must never exceed maxMomentsPerSecond (8)
    for (int i = 4; i < 24; ++i) {
        GameplayInputEvent p{};
        p.tick = 500 + i * 10;
        p.timeSeconds = static_cast<double>(p.tick) / 240.0;
        p.isDown = true;
        p.gamemode = Gamemode::Cube;
        p.x = 1200.0f + i * 30.0f;
        p.y = 150.0f;
        p.nearestHazardUnits = 30.0f;
        if (det.evaluate(p, cats).accepted) ++burstAccepted;

        GameplayInputEvent r = p;
        r.tick += 3;
        r.timeSeconds = static_cast<double>(r.tick) / 240.0;
        r.isDown = false;
        det.evaluate(r, cats);
    }
    CHECK(burstAccepted <= 8);

    // 6. Jumping close to a spike keeps ONLY the ground circle (never spawns a 2nd circle in the air)
    det.reset();
    GameplayInputEvent groundJump{};
    groundJump.tick = 1000;
    groundJump.timeSeconds = 1000.0 / 240.0;
    groundJump.isDown = true;
    groundJump.isOnGround = true;
    groundJump.gamemode = Gamemode::Cube;
    groundJump.x = 500.0f;
    groundJump.y = 105.0f;
    groundJump.nearestHazardUnits = 12.0f;
    auto rGround = det.evaluate(groundJump, cats);
    CHECK(rGround.accepted);

    // Mid-air release above the spike (even if AllReleases is enabled) must NOT spawn a 2nd circle in air
    GameplayInputEvent airRelease = groundJump;
    airRelease.tick = 1014;
    airRelease.timeSeconds = 1014.0 / 240.0;
    airRelease.isDown = false;
    airRelease.isOnGround = false;
    airRelease.x = 528.0f;
    airRelease.y = 148.0f;
    airRelease.yVelocity = 8.4;
    airRelease.nearestHazardUnits = 9.0f;
    auto rAirRel = det.evaluate(airRelease, cats);
    CHECK(!rAirRel.accepted);

    // Mid-air extra click without an orb during that jump over the spike must also be suppressed
    GameplayInputEvent airPressNoOrb = airRelease;
    airPressNoOrb.tick = 1024;
    airPressNoOrb.timeSeconds = 1024.0 / 240.0;
    airPressNoOrb.isDown = true;
    airPressNoOrb.x = 545.0f;
    airPressNoOrb.y = 162.0f;
    airPressNoOrb.touchedRingOrPortal = false;
    airPressNoOrb.isDashing = false;
    auto rAirPressNoOrb = det.evaluate(airPressNoOrb, cats);
    CHECK(!rAirPressNoOrb.accepted);

    // Mid-air click that DOES hit an orb above the spike IS accepted
    GameplayInputEvent airOrbPress = airPressNoOrb;
    airOrbPress.tick = 1036;
    airOrbPress.timeSeconds = 1036.0 / 240.0;
    airOrbPress.x = 570.0f;
    airOrbPress.y = 168.0f;
    airOrbPress.touchedRingOrPortal = true;
    auto rAirOrb = det.evaluate(airOrbPress, cats);
    CHECK(rAirOrb.accepted);
}

void testFakeDistributionAndEnabledCategories() {
    CategoryConfig cats;
    MomentDetector det;
    DetectionConfig cfg;
    cfg.maxMomentsPerSecond = 25.0f;
    cfg.minDistanceUnits = 5.0f;
    det.setConfig(cfg);

    std::map<int, int> counts;
    int total = 360;
    for (int i = 0; i < total; ++i) {
        GameplayInputEvent ev{};
        ev.tick = 100 + i * 60;
        ev.timeSeconds = static_cast<double>(ev.tick) / 240.0;
        ev.isDown = true;
        ev.gamemode = static_cast<Gamemode>(i % 7);
        ev.x = 100.0f + i * 95.0f;
        ev.y = 105.0f + (i % 5) * 22.0f;
        ev.yVelocity = (i % 9) * 1.8;
        ev.nearestHazardUnits = 15.0f + (i % 12) * 18.0f;

        auto res = det.evaluate(ev, cats);
        CHECK(res.accepted);
        CHECK(res.isSimulatedEstimate);
        counts[res.categoryFrames]++;

        // Release
        GameplayInputEvent rel = ev;
        rel.tick += 15;
        rel.timeSeconds = static_cast<double>(rel.tick) / 240.0;
        rel.isDown = false;
        det.evaluate(rel, cats);
    }

    // Verify all 12 categories (1F..12F) are generated and no single category monopolizes the output
    CHECK(counts.size() == 12);
    for (int f = 1; f <= 12; ++f) {
        CHECK(counts[f] >= 8);
        CHECK(counts[f] <= 65);
    }

    // Verify physical tightness bias: close-call moments produce lower mean frame numbers than wide-open moments
    det.reset();
    double sumTight = 0.0, sumOpen = 0.0;
    int nSamples = 120;
    for (int i = 0; i < nSamples; ++i) {
        GameplayInputEvent tight{};
        tight.tick = 100 + i * 80;
        tight.timeSeconds = static_cast<double>(tight.tick) / 240.0;
        tight.isDown = true;
        tight.x = 100.0f + i * 90.0f;
        tight.y = 120.0f;
        tight.isMini = true;
        tight.touchedRingOrPortal = true;
        tight.yVelocity = 16.0;
        tight.nearestHazardUnits = 8.0f;
        auto rt = det.evaluate(tight, cats);
        CHECK(rt.accepted);
        sumTight += rt.categoryFrames;

        GameplayInputEvent rel = tight;
        rel.tick += 5;
        rel.timeSeconds = static_cast<double>(rel.tick) / 240.0;
        rel.isDown = false;
        det.evaluate(rel, cats);
    }
    det.reset();
    for (int i = 0; i < nSamples; ++i) {
        GameplayInputEvent open{};
        open.tick = 100 + i * 80;
        open.timeSeconds = static_cast<double>(open.tick) / 240.0;
        open.isDown = true;
        open.x = 100.0f + i * 90.0f;
        open.y = 120.0f;
        open.isMini = false;
        open.touchedRingOrPortal = false;
        open.yVelocity = 1.0;
        open.nearestHazardUnits = 240.0f;
        auto ro = det.evaluate(open, cats);
        CHECK(ro.accepted);
        sumOpen += ro.categoryFrames;

        GameplayInputEvent rel = open;
        rel.tick += 35;
        rel.timeSeconds = static_cast<double>(rel.tick) / 240.0;
        rel.isDown = false;
        det.evaluate(rel, cats);
    }
    CHECK((sumTight / nSamples) < (sumOpen / nSamples));

    // Now test restricting enabled categories to ONLY {1F, 6F, 12F, 18F (custom)}
    CategoryConfig subset;
    for (int f = 1; f <= 12; ++f) {
        subset.setCategoryEnabled(f, f == 1 || f == 6 || f == 12);
    }
    subset.addCustomCategory(18, true);

    det.reset();
    for (int i = 0; i < 80; ++i) {
        GameplayInputEvent ev{};
        ev.tick = 100 + i * 60;
        ev.timeSeconds = static_cast<double>(ev.tick) / 240.0;
        ev.isDown = true;
        ev.x = 100.0f + i * 80.0f;
        ev.y = 120.0f;
        auto res = det.evaluate(ev, subset);
        CHECK(res.accepted);
        CHECK(res.categoryFrames == 1 || res.categoryFrames == 6 || res.categoryFrames == 12 || res.categoryFrames == 18);

        GameplayInputEvent rel = ev;
        rel.tick += 10;
        rel.timeSeconds = static_cast<double>(rel.tick) / 240.0;
        rel.isDown = false;
        det.evaluate(rel, subset);
    }
}

void testMacroFormats() {
    // 1. Mega Hack Binary (.mhr - "HACK" header)
    {
        std::vector<uint8_t> mhr(32, 0);
        mhr[0] = 'H'; mhr[1] = 'A'; mhr[2] = 'C'; mhr[3] = 'K';
        uint32_t fps = 240;
        std::memcpy(&mhr[12], &fps, 4);
        uint32_t numActions = 2;
        std::memcpy(&mhr[28], &numActions, 4);

        // Action 1: down=1, p1=0, frame=120, x=450.5, y=105.0
        std::vector<uint8_t> act1(32, 0);
        act1[2] = 1; // down
        act1[3] = 0; // P1
        uint32_t f1 = 120;
        std::memcpy(&act1[4], &f1, 4);
        float x1 = 450.5f, y1 = 105.0f;
        std::memcpy(&act1[8], &x1, 4);
        std::memcpy(&act1[12], &y1, 4);
        mhr.insert(mhr.end(), act1.begin(), act1.end());

        // Action 2: down=0, p1=0, frame=138, x=510.0, y=145.0
        std::vector<uint8_t> act2(32, 0);
        act2[2] = 0; // release
        act2[3] = 0; // P1
        uint32_t f2 = 138;
        std::memcpy(&act2[4], &f2, 4);
        float x2 = 510.0f, y2 = 145.0f;
        std::memcpy(&act2[8], &x2, 4);
        std::memcpy(&act2[12], &y2, 4);
        mhr.insert(mhr.end(), act2.begin(), act2.end());

        auto parsed = parseMacroBytes(mhr, "test_run.mhr");
        CHECK(parsed.ok);
        CHECK(parsed.macro.format == "Mega Hack Binary (.mhr)");
        CHECK(std::abs(parsed.macro.tps - 240.0) < 1e-6);
        CHECK(parsed.macro.inputs.size() == 2);
        CHECK(parsed.macro.inputs[0].frame == 120);
        CHECK(parsed.macro.inputs[0].down == true);
        CHECK(parsed.macro.inputs[0].hasPosition == true);
        CHECK(std::abs(parsed.macro.inputs[0].x - 450.5f) < 1e-3f);
        CHECK(parsed.macro.inputs[1].frame == 138);
        CHECK(parsed.macro.inputs[1].down == false);
    }

    // 2. Mega Hack JSON (.mhr.json)
    {
        std::string json = R"({
            "meta": { "fps": 360.0, "version": 8 },
            "events": [
                { "frame": 50, "x": 120.0, "y": 105.0 },
                { "frame": 90, "down": true, "x": 280.0, "y": 105.0 },
                { "frame": 115, "down": false, "p2": false, "x": 340.0, "y": 160.0 }
            ]
        })";
        std::vector<uint8_t> bytes(json.begin(), json.end());
        auto parsed = parseMacroBytes(bytes, "demon.mhr.json");
        CHECK(parsed.ok);
        CHECK(parsed.macro.format == "Mega Hack JSON (.mhr.json)");
        CHECK(std::abs(parsed.macro.tps - 360.0) < 1e-6);
        CHECK(parsed.macro.inputs.size() == 2);
        CHECK(parsed.macro.inputs[0].frame == 90);
        CHECK(parsed.macro.inputs[0].down == true);
        CHECK(parsed.macro.inputs[1].frame == 115);
        CHECK(parsed.macro.inputs[1].down == false);
    }

    // 3. GDR2 binary (.gdr2) with "Phys" extension
    {
        std::vector<uint8_t> gdr2 = {'G', 'D', 'R'};
        gdr2.reserve(256);
        appendVarint(gdr2, 2);             // version 2
        appendVarstr(gdr2, "Phys");        // extension tag
        appendVarstr(gdr2, "PlayerOne");   // author
        appendVarstr(gdr2, "Test replay"); // description
        appendBeFloat(gdr2, 12.5f);        // duration
        appendVarint(gdr2, 22081);         // gameVersion
        appendBeDouble(gdr2, 240.0);       // framerate
        appendVarint(gdr2, 0);             // seed
        appendVarint(gdr2, 3);             // coins
        appendVarint(gdr2, 0);             // ldm
        appendVarint(gdr2, 0);             // platformer = false
        appendVarstr(gdr2, "MegaHack");    // bot name
        appendVarint(gdr2, 9);             // bot version
        appendVarint(gdr2, 123456);        // level id
        appendVarstr(gdr2, "Extreme Test");// level name
        appendVarint(gdr2, 0);             // global extension size = 0
        appendVarint(gdr2, 0);             // deaths = 0
        appendVarint(gdr2, 2);             // total inputs = 2
        appendVarint(gdr2, 2);             // p1 inputs = 2

        // Input 1: frame=60, down=1 -> packed = (60 << 1) | 1 = 121
        appendVarint(gdr2, (60ULL << 1) | 1ULL);
        std::vector<uint8_t> phys1;
        appendBeFloat(phys1, 310.0f);
        appendBeFloat(phys1, 105.0f);
        appendBeFloat(phys1, 0.0f);
        appendBeDouble(phys1, 0.0);
        appendBeDouble(phys1, 0.0);
        appendVarint(gdr2, phys1.size());
        gdr2.insert(gdr2.end(), phys1.begin(), phys1.end());

        // Input 2: frame=75 (delta=15), down=0 -> packed = (15 << 1) | 0 = 30
        appendVarint(gdr2, (15ULL << 1) | 0ULL);
        appendVarint(gdr2, phys1.size());
        gdr2.insert(gdr2.end(), phys1.begin(), phys1.end());

        auto parsed = parseMacroBytes(gdr2, "run.gdr2");
        CHECK(parsed.ok);
        CHECK(parsed.macro.format == "GDR2 (.gdr2)");
        CHECK(parsed.macro.botName == "MegaHack");
        CHECK(parsed.macro.inputs.size() == 2);
        CHECK(parsed.macro.inputs[0].frame == 60);
        CHECK(parsed.macro.inputs[0].down == true);
        CHECK(parsed.macro.inputs[0].hasPosition == true);
        CHECK(std::abs(parsed.macro.inputs[0].x - 310.0f) < 1e-3f);
        CHECK(parsed.macro.inputs[1].frame == 75);
        CHECK(parsed.macro.inputs[1].down == false);
    }

    // 4. GDR v1 JSON (.gdr)
    {
        std::string gdr1 = R"({
            "framerate": 240.0,
            "bot": { "name": "GDMegaOverlay", "version": "1.0" },
            "inputs": [
                { "frame": 42, "btn": 1, "2p": false, "down": true },
                { "frame": 58, "btn": 1, "2p": false, "down": false }
            ]
        })";
        std::vector<uint8_t> bytes(gdr1.begin(), gdr1.end());
        auto parsed = parseMacroBytes(bytes, "macro.gdr");
        CHECK(parsed.ok);
        CHECK(parsed.macro.inputs.size() == 2);
        CHECK(parsed.macro.inputs[0].frame == 42);
        CHECK(parsed.macro.inputs[1].frame == 58);
    }

    // 5. zBot (.zbf), Plaintext (.txt), and xdBot (.xd)
    {
        std::vector<uint8_t> zbf;
        zbf.reserve(32);
        appendLeFloat(zbf, 1.0f / 240.0f);
        appendLeFloat(zbf, 1.0f);
        appendLe32(zbf, 80);
        zbf.push_back(0x31); // down = true
        zbf.push_back(0x31); // p1 = true
        auto parsedZbf = parseMacroBytes(zbf, "run.zbf");
        CHECK(parsedZbf.ok);
        CHECK(parsedZbf.macro.inputs.size() == 1);
        CHECK(parsedZbf.macro.inputs[0].frame == 80);
        CHECK(parsedZbf.macro.inputs[0].down == true);

        std::string txt = "240\n50 1 1 1\n64 0 1 1\n";
        std::vector<uint8_t> txtBytes(txt.begin(), txt.end());
        auto parsedTxt = parseMacroBytes(txtBytes, "run.txt");
        CHECK(parsedTxt.ok);
        CHECK(parsedTxt.macro.inputs.size() == 2);

        std::string xd = "240\n30|1|1|1\n45|0|1|1\n";
        std::vector<uint8_t> xdBytes(xd.begin(), xd.end());
        auto parsedXd = parseMacroBytes(xdBytes, "run.xd");
        CHECK(parsedXd.ok);
        CHECK(parsedXd.macro.inputs.size() == 2);
    }

    // 6. Malformed & empty file handling
    {
        std::vector<uint8_t> empty;
        CHECK(!parseMacroBytes(empty, "empty.mhr").ok);

        std::vector<uint8_t> badMhr = {'H', 'A', 'C', 'K', 1, 2, 3};
        CHECK(!parseMacroBytes(badMhr, "truncated.mhr").ok);

        std::string badJson = "{ broken json ";
        std::vector<uint8_t> badJsonBytes(badJson.begin(), badJson.end());
        CHECK(!parseMacroBytes(badJsonBytes, "bad.mhr.json").ok);
    }
}

void testPlaybackAndRealAnalyzer() {
    MacroData macro;
    macro.name = "corridor.mhr";
    macro.tps = 240.0;
    macro.inputs = {
        {40, 1, false, true, false, 0.f, 0.f},
        {55, 1, false, false, false, 0.f, 0.f},
        {100, 1, false, true, false, 0.f, 0.f},
        {118, 1, false, false, false, 0.f, 0.f},
    };

    // Test MacroPlayer tick scheduling
    MacroPlayer player;
    player.setMacro(macro, "/tmp/corridor.mhr");
    player.setFrameOffset(2); // Macro frame 40 fires at game progress 42
    player.startPlayback();
    CHECK(player.isPlaying());

    std::vector<int64_t> firedProgress;
    for (int64_t tick = 0; tick <= 130; ++tick) {
        player.onProcessTick(tick, [&](MacroInput const&, size_t) {
            firedProgress.push_back(tick);
        });
    }
    CHECK(player.isFinished());
    CHECK(firedProgress.size() == 4);
    CHECK(firedProgress[0] == 42);
    CHECK(firedProgress[1] == 57);
    CHECK(firedProgress[2] == 102);
    CHECK(firedProgress[3] == 120);

    // Test RealAnalyzer against a deterministic mock backend where:
    // - Input 0 (frame 40) survives only if pressed in [38..41] (4 ticks = 1.0 frame at 60 FPS / 240 TPS)
    // - Input 2 (frame 100) survives if pressed in [88..103] (16 ticks = 4.0 frames at 60 FPS / 240 TPS)
    class MockBackend : public SimulationBackend {
    public:
        RealAnalyzer* analyzer = nullptr;
        int64_t prog = 0;
        bool dead = false;
        bool complete = false;
        int64_t firstPressTick = -1;
        int64_t secondPressTick = -1;

        void restartFromZero() override {
            prog = 0;
            dead = false;
            complete = false;
            firstPressTick = -1;
            secondPressTick = -1;
        }
        bool saveCheckpoint() override { return true; }
        bool loadCheckpoint() override { return false; }
        int64_t currentProgress() const override { return prog; }
        void stepSingleTick() override {
            ++prog;
            if (analyzer) analyzer->onProcessCommands(*this, prog);
            if (prog == 60) {
                if (firstPressTick < 38 || firstPressTick > 41) dead = true;
            }
            if (prog == 125) {
                if (secondPressTick < 88 || secondPressTick > 103) dead = true;
            }
            if (prog >= 140 && !dead) complete = true;
        }
        SimSnapshot sampleState(bool) const override {
            return {static_cast<float>(prog) * 3.0f, 105.0f, 0.0, static_cast<double>(prog) / 240.0, true};
        }
        bool isDead() const override { return dead; }
        bool isLevelComplete() const override { return complete; }
        void clearRunFlags() override { dead = false; complete = false; }
        void injectInput(uint8_t, bool, bool down) override {
            if (down) {
                if (firstPressTick < 0) firstPressTick = prog;
                else if (secondPressTick < 0) secondPressTick = prog;
            }
        }
        void releaseAllButtons() override {}
    };

    MockBackend backend;
    RealAnalyzer analyzer;
    backend.analyzer = &analyzer;

    RealScanConfig scfg;
    scfg.tps = 240.0;
    scfg.referenceFps = 60.0;
    scfg.maxWindowFrames = 12;
    scfg.includeReleases = false;

    analyzer.begin(macro, scfg);
    while (analyzer.isActive()) {
        analyzer.step(backend);
    }
    CHECK(analyzer.phase() == RealAnalyzer::Phase::Completed);
    CHECK(analyzer.results().size() == 2);
    // Input 0: [38..41] -> 4 ticks -> 1.0 frame at 60 FPS / 240 TPS
    CHECK(analyzer.results()[0].windowTicks == 4);
    CHECK(std::abs(analyzer.results()[0].windowFrames - 1.0) < 1e-6);
    // Input 1 (macro index 2): [88..103] -> 16 ticks -> 4.0 frames at 60 FPS / 240 TPS
    CHECK(analyzer.results()[1].windowTicks == 16);
    CHECK(std::abs(analyzer.results()[1].windowFrames - 4.0) < 1e-6);
}

void testBetterEstimateMode() {
    CategoryConfig cats;
    MomentDetector det;
    DetectionConfig cfg;
    cfg.betterEstimate = true;
    cfg.maxMomentsPerSecond = 20.0f;
    cfg.minDistanceUnits = 15.0f;
    det.setConfig(cfg);

    // 1. Clicking in open space (far from hazards and lethal walls) must NOT spawn any circle
    for (int i = 0; i < 10; ++i) {
        GameplayInputEvent openClick{};
        openClick.tick = 100 + i * 40;
        openClick.timeSeconds = static_cast<double>(openClick.tick) / 240.0;
        openClick.isDown = true;
        openClick.isOnGround = true;
        openClick.gamemode = Gamemode::Cube;
        openClick.x = 200.0f + i * 60.0f;
        openClick.y = 105.0f;
        openClick.nearestHazardUnits = 9999.0f;
        openClick.nearestSolidUnits = 9999.0f;
        openClick.isNearLethalObstacle = false;
        auto res = det.evaluate(openClick, cats);
        CHECK(!res.accepted);
        CHECK(res.reason == FilterReason::BelowSensitivityThreshold);

        GameplayInputEvent rel = openClick;
        rel.tick += 10;
        rel.timeSeconds = static_cast<double>(rel.tick) / 240.0;
        rel.isDown = false;
        det.evaluate(rel, cats);
    }

    // 2. Jumping close to a spike spawns ONLY 1 circle on the ground (never in the air)
    GameplayInputEvent spikeJump{};
    spikeJump.tick = 600;
    spikeJump.timeSeconds = 600.0 / 240.0;
    spikeJump.isDown = true;
    spikeJump.isOnGround = true;
    spikeJump.gamemode = Gamemode::Cube;
    spikeJump.x = 900.0f;
    spikeJump.y = 105.0f;
    spikeJump.nearestHazardUnits = 24.0f;
    spikeJump.nearestSolidUnits = 9999.0f;
    spikeJump.isNearLethalObstacle = true;
    auto rSpikeGround = det.evaluate(spikeJump, cats);
    CHECK(rSpikeGround.accepted);
    CHECK(rSpikeGround.categoryFrames >= 1 && rSpikeGround.categoryFrames <= 12);

    GameplayInputEvent spikeAirRel = spikeJump;
    spikeAirRel.tick = 612;
    spikeAirRel.timeSeconds = 612.0 / 240.0;
    spikeAirRel.isDown = false;
    spikeAirRel.isOnGround = false;
    spikeAirRel.x = 930.0f;
    spikeAirRel.y = 145.0f;
    CHECK(!det.evaluate(spikeAirRel, cats).accepted);

    GameplayInputEvent spikeAirClick = spikeAirRel;
    spikeAirClick.tick = 620;
    spikeAirClick.timeSeconds = 620.0 / 240.0;
    spikeAirClick.isDown = true;
    spikeAirClick.x = 955.0f;
    spikeAirClick.y = 160.0f;
    CHECK(!det.evaluate(spikeAirClick, cats).accepted);

    // 3. Jumping close to a lethal wall (a solid block ahead at body height that kills on impact) IS accepted
    GameplayInputEvent wallJump{};
    wallJump.tick = 700;
    wallJump.timeSeconds = 700.0 / 240.0;
    wallJump.isDown = true;
    wallJump.isOnGround = true;
    wallJump.gamemode = Gamemode::Cube;
    wallJump.x = 1200.0f;
    wallJump.y = 105.0f;
    wallJump.nearestHazardUnits = 9999.0f;
    wallJump.nearestSolidUnits = 30.0f;
    wallJump.isNearLethalObstacle = true;
    auto rWallGround = det.evaluate(wallJump, cats);
    CHECK(rWallGround.accepted);
    CHECK(rWallGround.categoryFrames >= 1 && rWallGround.categoryFrames <= 12);

    // 4. Random frame window assignment covers all enabled categories cleanly
    std::map<int, int> counts;
    for (int i = 0; i < 120; ++i) {
        GameplayInputEvent ev{};
        ev.tick = 1000 + i * 50;
        ev.timeSeconds = static_cast<double>(ev.tick) / 240.0;
        ev.isDown = true;
        ev.isOnGround = true;
        ev.gamemode = Gamemode::Cube;
        ev.x = 2000.0f + i * 80.0f;
        ev.y = 105.0f;
        ev.nearestHazardUnits = 20.0f;
        ev.isNearLethalObstacle = true;
        auto r = det.evaluate(ev, cats);
        CHECK(r.accepted);
        counts[r.categoryFrames]++;

        GameplayInputEvent rel = ev;
        rel.tick += 8;
        rel.timeSeconds = static_cast<double>(rel.tick) / 240.0;
        rel.isDown = false;
        det.evaluate(rel, cats);
    }
    CHECK(counts.size() == 12);
}

} // namespace

int main() {
    testCategoriesAndSounds();
    testMomentDeduplicationAndAntiSpam();
    testFakeDistributionAndEnabledCategories();
    testMacroFormats();
    testPlaybackAndRealAnalyzer();
    testBetterEstimateMode();

    std::cout << "Core tests completed: " << g_passed << " passed, " << g_failed << " failed.\n";
    return g_failed == 0 ? 0 : 1;
}
