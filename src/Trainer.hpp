#pragma once

#include "Chart.hpp"

class ManiaOverlay;

// Timing windows in ticks (480 per second, so 1 tick ~ 2.1 ms).
constexpr int64_t kPerfectWindow = 2;  // ~4 ms
constexpr int64_t kGreatWindow = 6;    // ~12 ms
constexpr int64_t kGoodWindow = 12;    // ~25 ms
constexpr int64_t kHitWindow = 24;     // ~50 ms; anything further is not matched to a note

enum class DisplayStyle { Gates, TargetRings, ClickTrack, SideLane };

struct TrainerSettings {
    bool enabled = true;
    bool practiceOnly = false;
    DisplayStyle style = DisplayStyle::Gates;

    // Gates
    bool gateFront = false; // gate meets your icon's front edge instead of its center

    // Target rings
    bool ringOnIcon = false; // ring closes on your icon instead of the click spot
    int clicksAhead = 2;     // how many upcoming clicks are shown
    float targetScale = 1.f; // ghost target size relative to your icon
    float releaseScale = .35f; // release circle size relative to the click circle

    // Click track
    bool trackTop = false;
    float trackHeight = 28.f;
    float trackOpacity = .6f;
    bool guideLine = true;

    // On-level markers
    int64_t approachTicks = 384;   // approach ring duration
    int64_t lookAheadTicks = 1200; // how far ahead the path/markers show
    float markerOpacity = .9f;
    float trailOpacity = .5f;
    float ringOpacity = 1.f;
    bool outline = true;
    cocos2d::ccColor4F p1Color = {.2f, .9f, 1.f, 1.f};
    cocos2d::ccColor4F p2Color = {1.f, .45f, .75f, 1.f};
    cocos2d::ccColor4F releaseColor = {1.f, .55f, .15f, 1.f};
    cocos2d::ccColor4F ringColor = {1.f, 1.f, 1.f, 1.f};
    cocos2d::ccColor4F ringHitColor = {.24f, 1.f, .35f, 1.f};
    cocos2d::ccColor4F trailColor = {1.f, 1.f, 1.f, 1.f};

    // Side lane
    std::string position = "Right";
    float scrollSpeed = 120.f;
    float laneWidth = 42.f;
    float hitLineY = 60.f;
    float laneOpacity = .6f;

    int64_t offsetTicks = 0;
    int64_t visualOffsetTicks = 0; // >0: cues arrive earlier (compensates input/display delay)
    int holdThreshold = 24;
    bool showJudgements = true;

    static TrainerSettings load();
};

// A part of the level, in percent (like GD's progress bar).
struct Section {
    float from = 0.f;
    float to = 100.f;
};

struct TrainerStats {
    int perfect = 0, great = 0, good = 0, ok = 0, miss = 0, extra = 0;
    int combo = 0, maxCombo = 0;

    float accuracy() const;
};

// Global state for the current level session.
class Trainer {
public:
    static Trainer& get();

    TrainerSettings settings;
    Chart chart;
    bool hasChart = false;
    ChartPath path; // where the winning run went; empty until recorded / captured

    // Recording your own run
    bool armRecording = false; // start recording on the next reset to the level start
    bool recording = false;
    std::vector<ChartInput> recorded;
    ChartPath recordedPath;

    // Bot demo: plays the chart once to map every click onto the level
    bool armCapture = false;
    bool capturing = false;
    bool injecting = false; // true while we feed a bot input into the game
    bool autoplay = false;  // showcase: the bot plays the chart while the guide is shown
    ChartPath capturePath;

    // Per-level: show the guide on the whole level or only in chosen sections.
    bool sectionsOnly = false;
    float levelLength = 0.f; // set when the level opens, for percent math
    std::vector<Section> sections;
    void saveSections();
    void applySections();
    bool inSections(float percent) const;
    float currentPercent() const;
    // Whether the guide should show where the player is right now.
    bool guideVisibleHere() const;

    // Re-reads the mod settings (they can change while you're in a level).
    void reloadSettings();
    // Rebuilds notes / sections / judgements after a change.
    void rebuild();

    // Safe mode: true once the trainer has been visible during the current attempt.
    bool cheatedThisAttempt = false;

    TrainerStats stats;
    // Timing error of your hits this session (ticks, + = late), for calibration.
    int64_t errorSum = 0;
    int errorCount = 0;
    double averageErrorMs() const;
    // Shifts the visual offset by your average error so cues line up with how you click.
    void calibrate();
    bool held[8] = {}; // per-lane held state
    ManiaOverlay* overlay = nullptr;

    void enterLevel(GJGameLevel* level);
    void exitLevel();

    void loadChart(GJGameLevel* level);
    geode::Result<> importChart(GJGameLevel* level, std::filesystem::path const& file);
    geode::Result<> deleteChart(GJGameLevel* level);
    // Copies another level's chart (and its route, if mapped) onto this level.
    // Returns whether the route came along.
    geode::Result<bool> copyChartFrom(GJGameLevel* level, std::filesystem::path const& chartFile);

    // Start positions: line the chart up with where you spawned.
    void alignToStart(PlayLayer* pl);
    int64_t startOffset() const { return m_startOffset; }
    // GD's tick counter restarts at a start position; this is the tick as if you'd
    // played from the real start.
    int64_t levelTick(int64_t gameTick) const { return gameTick + m_startOffset; }

    void toggleEnabled();
    // Cycles the views: Gates -> Target Rings -> Click Track.
    void cycleStyle();

    // Game events
    void onReset(int64_t tick, PlayLayer* pl);
    void onTick(int64_t tick, PlayLayer* pl);
    void onInput(int64_t tick, bool down, int button, bool player2);
    void onDeath(int64_t tick);
    void onComplete(GJGameLevel* level);
    void update(int64_t tick);

    // Bot demo playback: feeds due inputs into the game.
    void injectInputs(GJBaseGameLayer* layer, int64_t tick);
    bool shouldBlockPlayerInput() const { return (capturing || autoplay) && !injecting; }
    float captureProgress(int64_t tick) const;

    // Whether any trainer visual is shown right now (i.e. the cheat is active).
    bool isActive() const;
    bool showRings() const;
    bool showGates() const;
    bool showTrack() const;
    bool showSideLane() const;

    // Chart ticks shifted by the offset setting.
    int64_t chartToGame(int64_t t) const { return t + settings.offsetTicks; }
    // "Now" as the visuals should show it.
    int64_t visualNow(int64_t tick) const { return levelTick(tick) + settings.visualOffsetTicks; }

private:
    GJGameLevel* m_level = nullptr;
    int64_t m_startOffset = 0;
    size_t m_missCursor = 0;
    size_t m_captureIndex = 0;
    std::vector<size_t> m_pendingReleases;

    void judge(Judgement j, int64_t diff);
    void resetJudgements(int64_t tick);
    void finishCapture(bool completed, int64_t tick);
};

Judgement gradeFor(int64_t diff);
