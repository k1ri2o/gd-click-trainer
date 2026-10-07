#pragma once

#include <Geode/Geode.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// GD's tick counter (m_gameState.m_currentProgress) advances 2 per physics step,
// i.e. 480 per second. All chart ticks use that unit.
constexpr double kTicksPerSecond = 480.0;

// One raw input, same shape as a GDR input.
struct ChartInput {
    int64_t frame = 0;
    uint8_t button = 1; // 1 = jump, 2 = left, 3 = right
    bool player2 = false;
    bool down = false;
};

enum class Judgement { None, Perfect, Great, Good, Ok, Miss, Extra };

inline bool isHit(Judgement j) {
    return j == Judgement::Perfect || j == Judgement::Great || j == Judgement::Good || j == Judgement::Ok;
}

struct Lane {
    bool player2 = false;
    uint8_t button = 1;
    std::string label;
    cocos2d::ccColor4F color;
};

struct Note {
    int lane = 0;
    int64_t start = 0; // press tick
    int64_t end = 0;   // release tick
    bool isHold = false;
    Judgement press = Judgement::None;
    Judgement release = Judgement::None; // only used for hold notes
    bool shown = true; // false when it's outside the sections you chose for this level
};

// Where the players were on each tick of the winning run (object-layer coordinates).
struct PathPoint {
    float x1 = 0.f, y1 = 0.f;
    float x2 = 0.f, y2 = 0.f;
};

class ChartPath {
public:
    std::vector<PathPoint> points; // index = tick
    bool dual = false;

    bool empty() const { return points.empty(); }
    int64_t length() const { return static_cast<int64_t>(points.size()); }
    bool has(int64_t tick) const { return tick >= 0 && tick < length(); }
    cocos2d::CCPoint at(int64_t tick, bool player2) const;

    void set(int64_t tick, PathPoint p);
    void truncate(int64_t tick);

    static geode::Result<ChartPath> load(std::filesystem::path const& path);
    geode::Result<> save(std::filesystem::path const& path) const;
};

class Chart {
public:
    std::vector<ChartInput> inputs;
    std::vector<Lane> lanes;
    std::vector<Note> notes; // sorted by start
    double framerate = kTicksPerSecond;
    std::string source;

    // Turns raw inputs into lanes + tap/hold notes.
    void build(int holdThreshold);

    int laneFor(bool player2, uint8_t button) const;
    bool empty() const { return notes.empty(); }

    // Clears judgements; notes before `tick` count as already played.
    void resetJudgements(int64_t tick);

    static geode::Result<Chart> load(std::filesystem::path const& path);
    // Name of the level a .gdr2 file was made on (empty if unknown).
    static std::string levelNameOf(std::filesystem::path const& path);
    geode::Result<> save(std::filesystem::path const& path, std::string const& levelName, uint32_t levelID) const;
};

// Where charts for each level live.
std::filesystem::path chartsDir();
std::string levelKey(GJGameLevel* level);
std::filesystem::path chartPathFor(GJGameLevel* level);
std::filesystem::path pathFileFor(GJGameLevel* level);
