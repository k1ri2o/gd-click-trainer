#include "Trainer.hpp"
#include "ManiaOverlay.hpp"

#include <algorithm>

using namespace geode::prelude;

TrainerSettings TrainerSettings::load() {
    auto mod = Mod::get();
    TrainerSettings s;
    s.enabled = mod->getSettingValue<bool>("enabled");
    s.practiceOnly = mod->getSettingValue<bool>("practice-only");

    auto style = mod->getSettingValue<std::string>("view");
    if (style == "Side Lane") s.style = DisplayStyle::SideLane;
    else if (style == "Click Track") s.style = DisplayStyle::ClickTrack;
    else if (style == "Target Rings") s.style = DisplayStyle::TargetRings;
    else s.style = DisplayStyle::Gates;
    s.gateFront = mod->getSettingValue<std::string>("gate-anchor") == "Icon Front";

    s.ringOnIcon = mod->getSettingValue<std::string>("ring-anchor") == "Your Icon";
    s.clicksAhead = static_cast<int>(mod->getSettingValue<int64_t>("clicks-ahead"));
    s.targetScale = mod->getSettingValue<int64_t>("target-size") / 100.f;
    s.releaseScale = mod->getSettingValue<int64_t>("release-size") / 100.f;

    s.trackTop = mod->getSettingValue<std::string>("track-position") == "Top";
    s.trackHeight = static_cast<float>(mod->getSettingValue<int64_t>("track-height"));
    s.trackOpacity = mod->getSettingValue<int64_t>("track-opacity") / 100.f;
    s.guideLine = mod->getSettingValue<bool>("guide-line");

    // ms -> ticks
    s.approachTicks = static_cast<int64_t>(mod->getSettingValue<int64_t>("approach-time") * kTicksPerSecond / 1000);
    s.lookAheadTicks = static_cast<int64_t>(mod->getSettingValue<int64_t>("look-ahead") * kTicksPerSecond / 1000);
    s.markerOpacity = mod->getSettingValue<int64_t>("marker-opacity") / 100.f;
    s.trailOpacity = mod->getSettingValue<int64_t>("trail-opacity") / 100.f;
    s.ringOpacity = mod->getSettingValue<int64_t>("ring-opacity") / 100.f;
    s.outline = mod->getSettingValue<bool>("outline");
    auto color = [&](char const* key) {
        auto c = mod->getSettingValue<ccColor3B>(key);
        return ccColor4F{c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f};
    };
    s.p1Color = color("p1-color");
    s.p2Color = color("p2-color");
    s.releaseColor = color("release-color");
    s.ringColor = color("ring-color");
    s.ringHitColor = color("ring-hit-color");
    s.trailColor = color("trail-color");

    s.position = mod->getSettingValue<std::string>("position");
    s.scrollSpeed = static_cast<float>(mod->getSettingValue<int64_t>("note-speed"));
    s.laneWidth = static_cast<float>(mod->getSettingValue<int64_t>("lane-width"));
    s.hitLineY = static_cast<float>(mod->getSettingValue<int64_t>("hit-line-y"));
    s.laneOpacity = mod->getSettingValue<int64_t>("lane-opacity") / 100.f;

    // These two settings are in physics steps (1/240 s); a step is 2 ticks.
    s.offsetTicks = mod->getSettingValue<int64_t>("offset-ticks") * 2;
    s.holdThreshold = static_cast<int>(mod->getSettingValue<int64_t>("hold-threshold")) * 2;
    s.showJudgements = mod->getSettingValue<bool>("show-judgements");
    s.visualOffsetTicks = static_cast<int64_t>(mod->getSettingValue<int64_t>("visual-offset") * kTicksPerSecond / 1000);
    return s;
}

float TrainerStats::accuracy() const {
    int total = perfect + great + good + ok + miss + extra;
    if (total == 0) return 100.f;
    float score = perfect * 1.f + great * .9f + good * .7f + ok * .5f;
    return score / total * 100.f;
}

Judgement gradeFor(int64_t diff) {
    auto d = std::abs(diff);
    if (d <= kPerfectWindow) return Judgement::Perfect;
    if (d <= kGreatWindow) return Judgement::Great;
    if (d <= kGoodWindow) return Judgement::Good;
    return Judgement::Ok;
}

Trainer& Trainer::get() {
    static Trainer instance;
    return instance;
}

// ---------------------------------------------------------------- visibility

bool Trainer::isActive() const {
    auto pl = PlayLayer::get();
    if (!pl || !settings.enabled || !hasChart || capturing) return false;
    if (settings.practiceOnly && !pl->m_isPracticeMode) return false;
    return true;
}

bool Trainer::showRings() const {
    return isActive() && !path.empty() && settings.style == DisplayStyle::TargetRings;
}

bool Trainer::showGates() const {
    return isActive() && !path.empty() && settings.style == DisplayStyle::Gates;
}

bool Trainer::showTrack() const {
    return isActive() && !path.empty() && settings.style == DisplayStyle::ClickTrack;
}

bool Trainer::showSideLane() const {
    // Without a mapped path, the side lane is the only way to show the chart.
    return isActive() && (settings.style == DisplayStyle::SideLane || path.empty());
}

void Trainer::toggleEnabled() {
    settings.enabled = !settings.enabled;
    Mod::get()->setSettingValue<bool>("enabled", settings.enabled);
    Notification::create(
        settings.enabled ? "Click Trainer ON" : "Click Trainer OFF",
        settings.enabled ? NotificationIcon::Success : NotificationIcon::Info,
        1.f
    )->show();
}

void Trainer::saveSections() {
    if (!m_level) return;
    std::sort(sections.begin(), sections.end(), [](auto const& a, auto const& b) { return a.from < b.from; });
    auto list = matjson::Value::array();
    for (auto const& sec : sections) {
        auto pair = matjson::Value::array();
        pair.push(sec.from);
        pair.push(sec.to);
        list.push(pair);
    }
    matjson::Value v;
    v["only"] = sectionsOnly;
    v["list"] = list;
    Mod::get()->setSavedValue("sections." + levelKey(m_level), v);
}

bool Trainer::inSections(float percent) const {
    for (auto const& sec : sections) {
        if (percent >= sec.from && percent <= sec.to) return true;
    }
    return false;
}

float Trainer::currentPercent() const {
    auto pl = PlayLayer::get();
    if (!pl || !pl->m_player1 || pl->m_levelLength <= 0.f) return 0.f;
    return std::clamp(pl->m_player1->getPositionX() / pl->m_levelLength * 100.f, 0.f, 100.f);
}

bool Trainer::guideVisibleHere() const {
    return !sectionsOnly || inSections(currentPercent());
}

void Trainer::applySections() {
    float length = levelLength;
    if (auto pl = PlayLayer::get(); pl && pl->m_levelLength > 0.f) length = pl->m_levelLength;
    int64_t lastTick = chart.notes.empty() ? 1 : std::max<int64_t>(chart.notes.back().end, 1);
    for (auto& n : chart.notes) {
        if (!sectionsOnly) {
            n.shown = true;
            continue;
        }
        // Where in the level this click happens, in percent.
        float percent;
        if (!path.empty() && length > 0.f) percent = path.at(chartToGame(n.start), false).x / length * 100.f;
        else percent = static_cast<float>(n.start) / static_cast<float>(lastTick) * 100.f;
        n.shown = inSections(percent);
    }
}

void Trainer::reloadSettings() {
    bool wasEnabled = settings.enabled;
    settings = TrainerSettings::load();
    if (!m_level) return;
    if (settings.enabled != wasEnabled && settings.enabled && isActive()) cheatedThisAttempt = true;
    rebuild();
}

double Trainer::averageErrorMs() const {
    if (errorCount == 0) return 0.0;
    return static_cast<double>(errorSum) / errorCount * 1000.0 / kTicksPerSecond;
}

void Trainer::calibrate() {
    if (errorCount < 5) {
        Notification::create("Play a bit first: need at least 5 hits to calibrate", NotificationIcon::Warning, 2.f)->show();
        return;
    }
    auto current = Mod::get()->getSettingValue<int64_t>("visual-offset");
    auto next = std::clamp<int64_t>(current + std::llround(averageErrorMs()), -150, 150);
    Mod::get()->setSettingValue<int64_t>("visual-offset", next);
    settings.visualOffsetTicks = static_cast<int64_t>(next * kTicksPerSecond / 1000);
    Notification::create(fmt::format("Visual offset: {} ms", next), NotificationIcon::Success, 2.f)->show();
    errorSum = 0;
    errorCount = 0;
}

void Trainer::cycleStyle() {
    switch (settings.style) {
        case DisplayStyle::Gates: settings.style = DisplayStyle::TargetRings; break;
        case DisplayStyle::TargetRings: settings.style = DisplayStyle::ClickTrack; break;
        default: settings.style = DisplayStyle::Gates; break;
    }
    auto name = settings.style == DisplayStyle::Gates ? "Gates"
        : settings.style == DisplayStyle::TargetRings ? "Target Rings" : "Click Track";
    Mod::get()->setSettingValue<std::string>("view", name);
    Notification::create(fmt::format("View: {}", name), NotificationIcon::Info, 1.f)->show();
    if (overlay) overlay->rebuild();
}

// ---------------------------------------------------------------- level / chart

void Trainer::enterLevel(GJGameLevel* level) {
    m_level = level;
    {
        // Per-level sections
        sections.clear();
        sectionsOnly = false;
        auto saved = Mod::get()->getSavedValue<matjson::Value>("sections." + levelKey(level));
        if (saved.isObject()) {
            sectionsOnly = saved["only"].asBool().unwrapOr(false);
            if (saved["list"].isArray()) {
                for (auto const& item : saved["list"].asArray().unwrap()) {
                    if (!item.isArray() || item.size() != 2) continue;
                    sections.push_back({
                        static_cast<float>(item[0].asDouble().unwrapOr(0.0)),
                        static_cast<float>(item[1].asDouble().unwrapOr(100.0)),
                    });
                }
            }
        }
    }
    errorSum = 0;
    errorCount = 0;
    settings = TrainerSettings::load();
    cheatedThisAttempt = false;
    armRecording = recording = false;
    armCapture = capturing = injecting = false;
    recorded.clear();
    recordedPath = {};
    capturePath = {};
    stats = {};
    std::fill(std::begin(held), std::end(held), false);
    loadChart(level);
}

void Trainer::exitLevel() {
    m_level = nullptr;
    cheatedThisAttempt = false;
    armRecording = recording = false;
    armCapture = capturing = injecting = false;
    recorded.clear();
    recordedPath = {};
    capturePath = {};
    hasChart = false;
    chart = {};
    path = {};
    overlay = nullptr;
}

void Trainer::loadChart(GJGameLevel* level) {
    hasChart = false;
    chart = {};
    path = {};

    auto file = chartPathFor(level);
    if (std::filesystem::exists(file)) {
        if (auto res = Chart::load(file)) {
            chart = std::move(res).unwrap();
            hasChart = true;
        }
        else {
            log::error("Failed to load chart {}: {}", file, res.unwrapErr());
        }
    }
    auto pathFile = pathFileFor(level);
    if (hasChart && std::filesystem::exists(pathFile)) {
        if (auto res = ChartPath::load(pathFile)) {
            path = std::move(res).unwrap();
        }
        else {
            log::error("Failed to load path {}: {}", pathFile, res.unwrapErr());
        }
    }
    rebuild();
}

Result<> Trainer::importChart(GJGameLevel* level, std::filesystem::path const& file) {
    GEODE_UNWRAP_INTO(auto imported, Chart::load(file));
    if (imported.inputs.empty()) {
        return Err("That macro has no inputs");
    }
    imported.source = string::pathToString(file.filename());
    GEODE_UNWRAP(imported.save(chartPathFor(level), level->m_levelName, level->m_levelID.value()));
    // A new chart invalidates the old mapped path.
    std::error_code ec;
    std::filesystem::remove(pathFileFor(level), ec);
    loadChart(level);
    return Ok();
}

Result<bool> Trainer::copyChartFrom(GJGameLevel* level, std::filesystem::path const& chartFile) {
    std::error_code ec;
    auto dest = chartPathFor(level);
    if (std::filesystem::equivalent(chartFile, dest, ec)) return Err("That's this level's own chart");
    GEODE_UNWRAP(file::createDirectoryAll(dest.parent_path()));
    std::filesystem::copy_file(chartFile, dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return Err("Could not copy the chart: {}", ec.message());

    // The route sits next to the chart with the same name.
    auto srcPath = chartFile;
    srcPath.replace_extension(".ctpath");
    bool withRoute = std::filesystem::exists(srcPath, ec);
    if (withRoute) {
        std::filesystem::copy_file(srcPath, pathFileFor(level), std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) withRoute = false;
    }
    if (!withRoute) std::filesystem::remove(pathFileFor(level), ec);

    loadChart(level);
    if (auto pl = PlayLayer::get()) alignToStart(pl);
    return Ok(withRoute);
}

void Trainer::alignToStart(PlayLayer* pl) {
    m_startOffset = 0;
    if (path.empty() || !pl || !pl->m_startPosObject || !pl->m_player1) return;
    // First point of the recorded route at (or past) where you spawned.
    float x = pl->m_player1->getPositionX();
    auto const& pts = path.points;
    for (size_t i = 0; i < pts.size(); i++) {
        if (pts[i].x1 >= x - .5f) {
            m_startOffset = static_cast<int64_t>(i) - static_cast<int64_t>(pl->m_gameState.m_currentProgress);
            break;
        }
    }
}

Result<> Trainer::deleteChart(GJGameLevel* level) {
    std::error_code ec;
    std::filesystem::remove(chartPathFor(level), ec);
    if (ec) return Err("Could not delete chart: {}", ec.message());
    std::filesystem::remove(pathFileFor(level), ec);
    loadChart(level);
    return Ok();
}

void Trainer::rebuild() {
    if (hasChart) {
        chart.build(settings.holdThreshold);
        hasChart = !chart.empty();
    }
    applySections();
    stats = {};
    m_missCursor = 0;
    m_pendingReleases.clear();
    int64_t tick = 0;
    if (auto pl = PlayLayer::get()) tick = pl->m_gameState.m_currentProgress;
    resetJudgements(tick);
    if (isActive()) cheatedThisAttempt = true;
    if (overlay) overlay->rebuild();
}

// ---------------------------------------------------------------- game events

namespace {
    // Whether the game currently has this button held for this input side.
    bool gameHolds(PlayLayer* pl, bool player2, uint8_t button) {
        bool twoPlayer = pl->m_levelSettings && pl->m_levelSettings->m_twoPlayerMode;
        auto player = player2 ? (twoPlayer ? pl->m_player2 : nullptr) : pl->m_player1;
        if (!player) return false;
        auto it = player->m_holdingButtons.find(button);
        return it != player->m_holdingButtons.end() && it->second;
    }
}

void Trainer::onReset(int64_t tick, PlayLayer* pl) {
    if (tick == 0) alignToStart(pl);
    if (autoplay && tick == 0) m_captureIndex = 0;

    // Bot demo
    if (armCapture && tick == 0) {
        armCapture = false;
        capturing = true;
        m_captureIndex = 0;
        capturePath = {};
    }
    else if (capturing) {
        if (tick == 0) {
            // Restarted mid-demo: start over.
            m_captureIndex = 0;
            capturePath = {};
        }
        else {
            capturing = false; // respawned at a checkpoint: the demo can't continue
        }
    }

    // A full restart is a fresh attempt; a checkpoint respawn continues the same run.
    bool cheatingNow = isActive() || capturing;
    if (tick == 0) cheatedThisAttempt = cheatingNow;
    else cheatedThisAttempt = cheatedThisAttempt || cheatingNow;

    // Recording
    if (armRecording && tick == 0) {
        if (pl->m_startPosObject) {
            // A chart has to start at the real level start to line up.
            armRecording = false;
            Notification::create("Click Trainer: turn off the start position to record", NotificationIcon::Warning, 3.f)->show();
        }
        else {
            armRecording = false;
            recording = true;
            recorded.clear();
            recordedPath = {};
        }
    }
    else if (recording) {
        // Practice respawn (or restart): everything after this point never happened.
        std::erase_if(recorded, [&](auto const& in) { return in.frame >= tick; });
        recordedPath.truncate(tick);
        // Match what the game has held after respawning, per player and button, so a
        // hold through a checkpoint (ship, wave...) stays a hold and doesn't get split.
        for (bool p2 : {false, true}) {
            for (uint8_t btn : {uint8_t(1), uint8_t(2), uint8_t(3)}) {
                auto last = std::find_if(recorded.rbegin(), recorded.rend(), [&](auto const& in) {
                    return in.player2 == p2 && in.button == btn;
                });
                bool wasDown = last != recorded.rend() && last->down;
                bool isDown = gameHolds(pl, p2, btn);
                if (wasDown != isDown) recorded.push_back({tick, btn, p2, isDown});
            }
        }
    }

    resetJudgements(tick);
}

void Trainer::onTick(int64_t tick, PlayLayer* pl) {
    if (!recording && !capturing) return;
    if (!pl->m_player1 || pl->m_player1->m_isDead) return;

    PathPoint p;
    auto p1 = pl->m_player1->getPosition();
    p.x1 = p1.x;
    p.y1 = p1.y;
    bool dual = pl->m_gameState.m_isDualMode && pl->m_player2;
    if (dual) {
        auto p2 = pl->m_player2->getPosition();
        p.x2 = p2.x;
        p.y2 = p2.y;
    }
    else {
        p.x2 = p.x1;
        p.y2 = p.y1;
    }

    auto& target = recording ? recordedPath : capturePath;
    target.set(tick, p);
    target.dual = target.dual || dual;
}

void Trainer::injectInputs(GJBaseGameLayer* layer, int64_t tick) {
    if (!capturing && !autoplay) return;
    auto const& inputs = chart.inputs;
    while (m_captureIndex < inputs.size() && chartToGame(inputs[m_captureIndex].frame) <= tick) {
        auto const& in = inputs[m_captureIndex++];
        injecting = true;
        layer->handleButton(in.down, in.button, !in.player2);
        injecting = false;
    }
}

float Trainer::captureProgress(int64_t tick) const {
    if (chart.inputs.empty()) return 0.f;
    auto last = chartToGame(chart.inputs.back().frame);
    if (last <= 0) return 100.f;
    return std::clamp(static_cast<float>(tick) / static_cast<float>(last) * 100.f, 0.f, 100.f);
}

void Trainer::finishCapture(bool completed, int64_t tick) {
    capturing = false;
    if (!m_level || capturePath.empty()) return;

    auto res = capturePath.save(pathFileFor(m_level));
    if (!res) {
        Notification::create("Click Trainer: " + res.unwrapErr(), NotificationIcon::Error)->show();
        return;
    }
    path = capturePath;
    capturePath = {};
    if (completed) {
        Notification::create("Clicks mapped onto the level!", NotificationIcon::Success)->show();
    }
    else {
        Notification::create(
            fmt::format("Bot demo died at {:.0f}% - clicks mapped up to there", captureProgress(tick)),
            NotificationIcon::Warning
        )->show();
    }
    if (overlay) overlay->rebuild();
}

void Trainer::onDeath(int64_t tick) {
    if (capturing) finishCapture(false, tick);
}

void Trainer::onInput(int64_t tick, bool down, int button, bool player2) {
    if (injecting && !autoplay) return; // the bot demo isn't you

    if (recording && !injecting) {
        recorded.push_back({tick, static_cast<uint8_t>(button), player2, down});
    }
    if (!hasChart || !isActive()) return;
    if (!guideVisibleHere()) return;

    int lane = chart.laneFor(player2, static_cast<uint8_t>(button));
    if (lane >= 0 && lane < 8) held[lane] = down;
    if (lane < 0) {
        if (down) judge(Judgement::Extra, 0);
        return;
    }

    int64_t t = levelTick(tick) - settings.offsetTicks;
    auto& notes = chart.notes;

    if (down) {
        // Closest unjudged press in this lane within the hit window.
        auto it = std::lower_bound(notes.begin(), notes.end(), t - kHitWindow, [](Note const& n, int64_t v) {
            return n.start < v;
        });
        Note* best = nullptr;
        for (; it != notes.end() && it->start <= t + kHitWindow; ++it) {
            if (it->lane != lane || it->press != Judgement::None) continue;
            if (!best || std::abs(it->start - t) < std::abs(best->start - t)) best = &*it;
        }
        if (!best) {
            judge(Judgement::Extra, 0);
            return;
        }
        int64_t diff = t - best->start;
        best->press = gradeFor(diff);
        judge(best->press, diff);
        return;
    }

    // Release: only hold notes care about it.
    Note* best = nullptr;
    Note* heldNote = nullptr;
    for (auto& n : notes) {
        if (n.start > t + kHitWindow) break;
        if (n.lane != lane || !n.isHold || n.release != Judgement::None) continue;
        if (std::abs(n.end - t) <= kHitWindow) {
            if (!best || std::abs(n.end - t) < std::abs(best->end - t)) best = &n;
        }
        else if (isHit(n.press) && n.start <= t && n.end > t) {
            heldNote = &n;
        }
    }
    if (best) {
        int64_t diff = t - best->end;
        best->release = gradeFor(diff);
        judge(best->release, diff);
    }
    else if (heldNote) {
        // Let go of a hold way too early.
        heldNote->release = Judgement::Miss;
        judge(Judgement::Miss, t - heldNote->end);
    }
}

void Trainer::resetJudgements(int64_t tick) {
    std::fill(std::begin(held), std::end(held), false);
    stats = {};
    m_pendingReleases.clear();
    if (!hasChart) return;
    int64_t chartTick = levelTick(tick) - settings.offsetTicks;
    chart.resetJudgements(chartTick);
    for (auto& n : chart.notes) {
        if (!n.shown) n.press = n.release = Judgement::Perfect;
    }
    m_missCursor = std::lower_bound(chart.notes.begin(), chart.notes.end(), chartTick, [](Note const& n, int64_t t) {
        return n.start < t;
    }) - chart.notes.begin();
}

void Trainer::update(int64_t tick) {
    if (!hasChart || !isActive()) return;
    int64_t t = levelTick(tick) - settings.offsetTicks;
    auto& notes = chart.notes;

    while (m_missCursor < notes.size() && notes[m_missCursor].start < t - kHitWindow) {
        auto& n = notes[m_missCursor];
        if (n.press == Judgement::None) {
            n.press = Judgement::Miss;
            if (n.isHold) n.release = Judgement::Miss;
            judge(Judgement::Miss, 0);
        }
        if (n.isHold && n.release == Judgement::None) {
            m_pendingReleases.push_back(m_missCursor);
        }
        m_missCursor++;
    }

    std::erase_if(m_pendingReleases, [&](size_t i) {
        auto& n = notes[i];
        if (n.release != Judgement::None) return true;
        if (n.end < t - kHitWindow) {
            // Held too long.
            n.release = Judgement::Miss;
            judge(Judgement::Miss, t - n.end);
            return true;
        }
        return false;
    });
}

void Trainer::judge(Judgement j, int64_t diff) {
    switch (j) {
        case Judgement::Perfect: stats.perfect++; break;
        case Judgement::Great: stats.great++; break;
        case Judgement::Good: stats.good++; break;
        case Judgement::Ok: stats.ok++; break;
        case Judgement::Miss: stats.miss++; break;
        case Judgement::Extra: stats.extra++; break;
        default: break;
    }
    if (isHit(j)) {
        errorSum += diff;
        errorCount++;
        stats.combo++;
        stats.maxCombo = std::max(stats.maxCombo, stats.combo);
    }
    else {
        stats.combo = 0;
    }
    if (overlay) overlay->showJudgement(j, diff);
}

void Trainer::onComplete(GJGameLevel* level) {
    if (capturing) {
        int64_t tick = 0;
        if (auto pl = PlayLayer::get()) tick = pl->m_gameState.m_currentProgress;
        finishCapture(true, tick);
        return;
    }
    if (!recording) return;
    recording = false;
    if (recorded.empty()) {
        Notification::create("Click Trainer: no clicks recorded", NotificationIcon::Warning)->show();
        return;
    }

    Chart rec;
    rec.inputs = recorded;
    rec.source = "your recorded run";
    auto res = rec.save(chartPathFor(level), level->m_levelName, level->m_levelID.value());
    if (res && !recordedPath.empty()) res = recordedPath.save(pathFileFor(level));
    if (!res) {
        Notification::create("Click Trainer: " + res.unwrapErr(), NotificationIcon::Error)->show();
        return;
    }
    Notification::create(
        fmt::format("Chart saved: {} clicks", std::count_if(recorded.begin(), recorded.end(), [](auto& i) { return i.down; })),
        NotificationIcon::Success
    )->show();
    recorded.clear();
    recordedPath = {};
    loadChart(level);
}
