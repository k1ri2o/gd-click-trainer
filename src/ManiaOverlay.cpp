#include "ManiaOverlay.hpp"

using namespace geode::prelude;

namespace {
    constexpr float kNoteHeight = 8.f;
    constexpr float kTailHeight = 4.f;
    constexpr float kLanePad = 3.f;
    constexpr float kMargin = 12.f;

    ccColor4F withAlpha(ccColor4F c, float a) {
        c.a = a;
        return c;
    }

    void rect(CCDrawNode* d, float x0, float y0, float x1, float y1, ccColor4F fill) {
        d->drawRect({x0, y0}, {x1, y1}, fill, 0.f, {0, 0, 0, 0});
    }

    struct JudgeStyle {
        char const* text;
        ccColor3B color;
    };

    JudgeStyle styleFor(Judgement j) {
        switch (j) {
            case Judgement::Perfect: return {"PERFECT", {120, 230, 255}};
            case Judgement::Great: return {"GREAT", {120, 255, 120}};
            case Judgement::Good: return {"GOOD", {255, 230, 100}};
            case Judgement::Ok: return {"OK", {255, 160, 60}};
            case Judgement::Miss: return {"MISS", {255, 70, 70}};
            case Judgement::Extra: return {"EXTRA", {255, 70, 70}};
            default: return {"", {255, 255, 255}};
        }
    }

    CCLabelBMFont* makeLabel(char const* font, float scale, CCPoint anchor = {.5f, .5f}) {
        auto label = CCLabelBMFont::create("", font);
        label->setScale(scale);
        label->setAnchorPoint(anchor);
        return label;
    }
}

ManiaOverlay* ManiaOverlay::create() {
    auto ret = new ManiaOverlay();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

ManiaOverlay::~ManiaOverlay() {
    auto& trainer = Trainer::get();
    if (trainer.overlay == this) trainer.overlay = nullptr;
}

bool ManiaOverlay::init() {
    if (!CCNode::init()) return false;
    this->setID("hud"_spr);

    m_draw = CCDrawNode::create();
    m_draw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    this->addChild(m_draw);

    m_laneLabels = CCNode::create();
    this->addChild(m_laneLabels);

    // Feedback group: positioned next to the player (on-level) or above the hit line (lane)
    m_feedback = CCNode::create();
    this->addChild(m_feedback, 2);

    m_judgeLabel = makeLabel("bigFont.fnt", .45f);
    m_judgeLabel->setOpacity(0);
    m_feedback->addChild(m_judgeLabel);

    m_offsetLabel = makeLabel("bigFont.fnt", .28f);
    m_offsetLabel->setPosition({0.f, -13.f});
    m_offsetLabel->setOpacity(0);
    m_feedback->addChild(m_offsetLabel);

    m_comboLabel = makeLabel("bigFont.fnt", .3f);
    m_comboLabel->setPosition({0.f, 16.f});
    m_feedback->addChild(m_comboLabel);

    m_accLabel = makeLabel("chatFont.fnt", .55f, {.5f, 1.f});
    this->addChild(m_accLabel, 2);

    m_recLabel = makeLabel("bigFont.fnt", .4f, {.5f, 1.f});
    this->addChild(m_recLabel, 2);

    m_hintLabel = makeLabel("chatFont.fnt", .5f, {.5f, 1.f});
    m_hintLabel->setColor({200, 200, 200});
    m_hintLabel->setOpacity(170);
    this->addChild(m_hintLabel, 2);

    m_safeLabel = makeLabel("bigFont.fnt", .3f, {0.f, 1.f});
    m_safeLabel->setString("SAFE MODE");
    m_safeLabel->setColor({255, 90, 90});
    m_safeLabel->setOpacity(200);
    m_safeLabel->setVisible(false);
    this->addChild(m_safeLabel, 2);

    Trainer::get().overlay = this;
    this->rebuild();
    // Not scheduled: PlayLayer::postUpdate redraws us right after the physics step,
    // so markers are never a frame behind your icon.
    return true;
}

float ManiaOverlay::laneAreaWidth() const {
    auto& t = Trainer::get();
    return t.settings.laneWidth * static_cast<float>(t.chart.lanes.size());
}

void ManiaOverlay::rebuild() {
    auto& t = Trainer::get();
    auto win = CCDirector::get()->getWinSize();
    float w = laneAreaWidth();

    if (t.settings.position == "Left") m_laneX = kMargin;
    else if (t.settings.position == "Center") m_laneX = (win.width - w) / 2.f;
    else m_laneX = win.width - w - kMargin;

    m_recLabel->setPosition({win.width / 2.f, win.height - 6.f});
    m_hintLabel->setPosition({win.width / 2.f, win.height - 22.f});
    m_accLabel->setPosition({win.width / 2.f, win.height - 4.f});
    m_safeLabel->setPosition({4.f, 22.f});

    float hitY = t.settings.hitLineY;
    m_laneLabels->removeAllChildren();
    for (size_t i = 0; i < t.chart.lanes.size(); i++) {
        auto label = CCLabelBMFont::create(t.chart.lanes[i].label.c_str(), "chatFont.fnt");
        label->setScale(.5f);
        label->setOpacity(180);
        label->setPosition({m_laneX + t.settings.laneWidth * (i + .5f), hitY - 14.f});
        m_laneLabels->addChild(label);
    }
}

void ManiaOverlay::showJudgement(Judgement j, int64_t diffTicks) {
    if (!Trainer::get().settings.showJudgements) return;
    auto style = styleFor(j);

    m_judgeLabel->setString(style.text);
    m_judgeLabel->setColor(style.color);
    m_judgeLabel->stopAllActions();
    m_judgeLabel->setOpacity(255);
    m_judgeLabel->setScale(.55f);
    m_judgeLabel->runAction(CCSequence::create(
        CCScaleTo::create(.08f, .45f),
        CCDelayTime::create(.35f),
        CCFadeOut::create(.25f),
        nullptr
    ));

    m_offsetLabel->stopAllActions();
    if (isHit(j) && diffTicks != 0) {
        int ms = static_cast<int>(std::lround(std::abs(diffTicks) * 1000.0 / kTicksPerSecond));
        m_offsetLabel->setString(fmt::format("{}ms {}", ms, diffTicks < 0 ? "EARLY" : "LATE").c_str());
        m_offsetLabel->setColor(diffTicks < 0 ? ccColor3B{120, 180, 255} : ccColor3B{255, 160, 90});
        m_offsetLabel->setOpacity(255);
        m_offsetLabel->runAction(CCSequence::create(CCDelayTime::create(.45f), CCFadeOut::create(.25f), nullptr));
    }
    else if (j == Judgement::Extra) {
        m_offsetLabel->setString("no click here");
        m_offsetLabel->setColor({255, 120, 120});
        m_offsetLabel->setOpacity(255);
        m_offsetLabel->runAction(CCSequence::create(CCDelayTime::create(.45f), CCFadeOut::create(.25f), nullptr));
    }
    else {
        m_offsetLabel->setOpacity(0);
    }
}

void ManiaOverlay::update(float) {
    auto pl = PlayLayer::get();
    if (!pl) return;
    auto& t = Trainer::get();
    int64_t tick = pl->m_gameState.m_currentProgress;

    // Top status line: recording / bot demo
    if (t.capturing) {
        m_recLabel->setString(fmt::format("BOT DEMO  {:.0f}%  - mapping clicks", t.captureProgress(tick)).c_str());
        m_recLabel->setColor({120, 220, 255});
        m_recLabel->setVisible(true);
    }
    else if (t.armCapture) {
        m_recLabel->setString("BOT DEMO starts on restart");
        m_recLabel->setColor({120, 220, 255});
        m_recLabel->setVisible(true);
    }
    else if (t.recording) {
        auto clicks = std::count_if(t.recorded.begin(), t.recorded.end(), [](auto& i) { return i.down; });
        m_recLabel->setString(fmt::format("REC  {} clicks", clicks).c_str());
        m_recLabel->setColor({255, 80, 80});
        m_recLabel->setVisible(true);
    }
    else if (t.armRecording) {
        m_recLabel->setString("REC armed - restart from the start");
        m_recLabel->setColor({255, 80, 80});
        m_recLabel->setVisible(true);
    }
    else {
        m_recLabel->setVisible(false);
    }

    bool active = t.isActive();
    if ((active && t.guideVisibleHere()) || t.capturing) t.cheatedThisAttempt = true;
    m_safeLabel->setVisible(t.cheatedThisAttempt);

    bool hintNeeded = active && t.path.empty() && t.settings.style != DisplayStyle::SideLane;
    m_hintLabel->setVisible(hintNeeded);
    if (hintNeeded) m_hintLabel->setString("Pause > Clicks > Map onto level, to see the clicks in the level");

    m_draw->clear();
    m_feedback->setVisible(active);
    m_laneLabels->setVisible(false);
    m_accLabel->setVisible(false);
    if (!active) return;

    t.update(tick);

    bool lane = t.showSideLane();
    if (lane) drawLane(pl, t.visualNow(tick));

    // Feedback sits above the hit marker (click track) or follows the player (on level)
    if (t.showTrack() && pl->m_player1) {
        auto world = pl->m_player1->getParent()->convertToWorldSpace(pl->m_player1->getPosition());
        float x = this->convertToNodeSpace(world).x;
        auto win = CCDirector::get()->getWinSize();
        float y = t.settings.trackTop ? win.height - t.settings.trackHeight - 34.f : t.settings.trackHeight + 30.f;
        m_feedback->setPosition({x, y});
    }
    else if ((t.showRings() || t.showGates()) && pl->m_player1) {
        auto world = pl->m_player1->getParent()->convertToWorldSpace(pl->m_player1->getPosition());
        auto local = this->convertToNodeSpace(world);
        m_feedback->setPosition(local + CCPoint{0.f, 42.f});
    }
    else if (lane) {
        m_feedback->setPosition({m_laneX + laneAreaWidth() / 2.f, t.settings.hitLineY + 45.f});
    }

    // Combo, plus your average timing once there's enough to say something
    std::string combo = t.stats.combo > 1 ? fmt::format("{}x", t.stats.combo) : "";
    if (t.errorCount >= 5) {
        auto avg = t.averageErrorMs();
        auto timing = std::abs(avg) < 3.0 ? std::string("on time") : fmt::format("{:.0f}ms {}", std::abs(avg), avg > 0 ? "late" : "early");
        combo += (combo.empty() ? "" : "  ") + std::string("avg ") + timing;
    }
    m_comboLabel->setString(combo.c_str());
}

void ManiaOverlay::drawLane(PlayLayer* pl, int64_t tick) {
    auto& t = Trainer::get();
    auto const& s = t.settings;
    auto win = CCDirector::get()->getWinSize();
    auto const& lanes = t.chart.lanes;
    float laneW = s.laneWidth;
    float x0 = m_laneX;
    float x1 = x0 + laneAreaWidth();
    float hitY = s.hitLineY;
    float pxPerTick = s.scrollSpeed / static_cast<float>(kTicksPerSecond);

    m_laneLabels->setVisible(true);
    m_accLabel->setVisible(true);
    m_accLabel->setPosition({x0 + laneAreaWidth() / 2.f, win.height - 4.f});
    m_accLabel->setString(fmt::format("{:.2f}%", t.stats.accuracy()).c_str());

    // Lane background, separators and receptors
    rect(m_draw, x0, 0.f, x1, win.height, {0.f, 0.f, 0.f, s.laneOpacity});
    for (size_t i = 0; i < lanes.size(); i++) {
        float lx = x0 + laneW * i;
        if (i > 0) rect(m_draw, lx - .5f, 0.f, lx + .5f, win.height, {1.f, 1.f, 1.f, .12f});
        if (i < 8 && t.held[i]) {
            rect(m_draw, lx, hitY, lx + laneW, hitY + 40.f, withAlpha(lanes[i].color, .18f));
        }
    }
    rect(m_draw, x0, hitY - 1.5f, x1, hitY + 1.5f, {1.f, 1.f, 1.f, .9f});

    // Notes
    auto yFor = [&](int64_t chartTick) {
        return hitY + static_cast<float>(t.chartToGame(chartTick) - tick) * pxPerTick;
    };
    int64_t maxTick = tick + static_cast<int64_t>((win.height - hitY) / pxPerTick) + 2;
    int64_t minTick = tick - static_cast<int64_t>(hitY / pxPerTick) - 2;
    ccColor4F missColor = {.45f, .45f, .45f, .7f};
    ccColor4F releaseColor = {1.f, .55f, .15f, 1.f};

    for (auto const& n : t.chart.notes) {
        if (t.chartToGame(n.start) > maxTick) break;
        if (!n.shown) continue;
        if (t.chartToGame(n.isHold ? n.end : n.start) < minTick) continue;

        auto const& lane = lanes[n.lane];
        float lx0 = x0 + laneW * n.lane + kLanePad;
        float lx1 = x0 + laneW * (n.lane + 1) - kLanePad;
        bool missed = n.press == Judgement::Miss;
        auto color = missed ? missColor : lane.color;

        if (!n.isHold) {
            if (isHit(n.press)) continue; // hit notes disappear
            float y = yFor(n.start);
            rect(m_draw, lx0, y - kNoteHeight / 2, lx1, y + kNoteHeight / 2, color);
            continue;
        }

        if (isHit(n.release)) continue;
        float headY = yFor(n.start);
        float tailY = yFor(n.end);
        bool holding = isHit(n.press);
        if (holding) headY = std::max(headY, hitY); // body gets eaten by the hit line

        float inset = laneW * .18f;
        rect(m_draw, lx0 + inset, headY, lx1 - inset, tailY, withAlpha(color, missed ? .3f : .45f));
        rect(m_draw, lx0, tailY - kTailHeight / 2, lx1, tailY + kTailHeight / 2, missed || n.release == Judgement::Miss ? missColor : releaseColor);
        if (!holding) {
            rect(m_draw, lx0, headY - kNoteHeight / 2, lx1, headY + kNoteHeight / 2, color);
        }
    }
}
