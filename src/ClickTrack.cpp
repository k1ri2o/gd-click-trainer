#include "ClickTrack.hpp"
#include "Draw.hpp"

using namespace geode::prelude;

namespace {
    constexpr ccColor4F kClear = {0.f, 0.f, 0.f, 0.f};
    constexpr ccColor4F kMissColor = {.5f, .5f, .5f, 1.f};
    constexpr int64_t kFlashTicks = 120; // ~250 ms
    constexpr int64_t kPopTicks = 34;    // first ~70 ms: solid and wide
    constexpr int64_t kKeepBehind = 130; // longer than the flash

    ccColor4F alpha(ccColor4F c, float a) {
        c.a = std::clamp(a, 0.f, 1.f);
        return c;
    }

}

ClickTrack* ClickTrack::create() {
    auto ret = new ClickTrack();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ClickTrack::init() {
    if (!CCNode::init()) return false;
    this->setID("click-track"_spr);

    m_draw = CCDrawNode::create();
    m_draw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    this->addChild(m_draw);

    // Not scheduled: PlayLayer::postUpdate redraws us right after the physics step,
    // so markers are never a frame behind your icon.
    return true;
}

void ClickTrack::update(float) {
    m_draw->clear();

    auto pl = PlayLayer::get();
    auto& t = Trainer::get();
    if (!pl || !pl->m_player1 || !t.showTrack()) return;
    // Sections: the strip is only there inside the parts you chose
    if (!t.guideVisibleHere()) return;
    t.cheatedThisAttempt = true;

    auto const& s = t.settings;
    auto const& path = t.path;
    auto const& chart = t.chart;
    auto win = CCDirector::get()->getWinSize();
    int64_t now = t.visualNow(pl->m_gameState.m_currentProgress);
    int64_t approach = std::max<int64_t>(s.approachTicks, 1);

    auto rect = [&](float x0, float y0, float x1, float y1, ccColor4F color, bool outlined = false) {
        if (color.a <= .01f) return;
        if (outlined && s.outline) {
            m_draw->drawRect({x0 - 1.2f, y0 - 1.2f}, {x1 + 1.2f, y1 + 1.2f}, {0.f, 0.f, 0.f, color.a * .8f}, 0.f, kClear);
        }
        m_draw->drawRect({x0, y0}, {x1, y1}, color, 0.f, kClear);
    };
    auto toScreen = [&](CCPoint world) {
        return this->convertToNodeSpace(pl->m_objectLayer->convertToWorldSpace(world));
    };

    // Strip with its own dark backdrop, so marks read the same on any level
    float height = s.trackHeight;
    float y0 = s.trackTop ? win.height - height : 0.f;
    float y1 = y0 + height;
    float inner = s.trackTop ? y0 : y1; // the edge facing the play area
    rect(0.f, y0, win.width, y1, {0.f, 0.f, 0.f, s.trackOpacity});
    rect(0.f, inner - .75f, win.width, inner + .75f, {1.f, 1.f, 1.f, .25f});

    size_t laneCount = std::max<size_t>(chart.lanes.size(), 1);
    float rowH = height / static_cast<float>(laneCount);
    auto rowCenter = [&](int lane) { return y1 - rowH * (static_cast<float>(lane) + .5f); };

    // Hit marker: directly under your icon
    float rx = toScreen(pl->m_player1->getPosition()).x;
    m_hitPoint = CCPoint{rx, inner};
    for (size_t i = 0; i < chart.lanes.size() && i < 8; i++) {
        if (t.held[i]) {
            float cy = rowCenter(static_cast<int>(i));
            auto c = chart.lanes[i].player2 ? s.p2Color : s.p1Color;
            rect(rx - 7.f, cy - rowH / 2.f, rx + 7.f, cy + rowH / 2.f, alpha(c, .35f));
        }
    }

    // Notes
    int64_t ahead = now + std::max<int64_t>(s.lookAheadTicks, 1);
    for (auto const& n : chart.notes) {
        if (!n.shown) continue;
        int64_t start = t.chartToGame(n.start);
        int64_t end = t.chartToGame(n.end);
        if (start > ahead) break;
        if ((n.isHold ? end : start) < now - kKeepBehind) continue;
        if (!path.has(start)) continue;

        bool p2 = chart.lanes[n.lane].player2;
        auto laneColor = p2 ? s.p2Color : s.p1Color;
        bool missed = n.press == Judgement::Miss;
        bool pressed = isHit(n.press);
        auto color = missed ? kMissColor : laneColor;
        float cy = rowCenter(n.lane);
        auto spot = toScreen(path.at(start, p2));
        float sx = spot.x;
        int64_t untilPress = start - now;
        float op = s.markerOpacity;

        // How close the click is: 0 when it enters the approach window, 1 at the moment.
        auto closenessOf = [&](int64_t until) {
            return 1.f - std::clamp(static_cast<float>(until) / static_cast<float>(approach), 0.f, 1.f);
        };
        auto flashAt = [&](int64_t until) {
            if (until > 0 || until <= -kFlashTicks) return;
            int64_t since = -until;
            if (since < kPopTicks) {
                // Pop: the hit marker turns into a wide, solid bar
                rect(rx - 7.f, y0, rx + 7.f, y1 + (s.trackTop ? 0.f : 4.f), alpha(s.ringHitColor, .95f), true);
            }
            else {
                float p = static_cast<float>(since - kPopTicks) / static_cast<float>(kFlashTicks - kPopTicks);
                float w = 7.f + 14.f * p;
                rect(rx - w, y0, rx + w, y1, alpha(s.ringHitColor, (1.f - p) * .6f));
            }
        };

        if (n.isHold && !isHit(n.release)) {
            bool releaseMissed = missed || n.release == Judgement::Miss;
            float ex = path.has(end) ? toScreen(path.at(end, p2)).x : win.width + 20.f;
            float bx = pressed ? std::max(sx, rx) : sx; // the bar gets eaten while you hold
            if (bx < ex) rect(bx, cy - rowH * .2f, ex, cy + rowH * .2f, alpha(color, .75f * op), true);

            // Release mark: let go here
            auto relColor = releaseMissed ? kMissColor : s.releaseColor;
            float relH = rowH * .4f * std::max(s.releaseScale, .25f);
            rect(ex - 1.5f, cy - relH, ex + 1.5f, cy + relH, alpha(relColor, op), true);
            if (!releaseMissed) flashAt(end - now);
        }

        if (!missed) flashAt(untilPress);
        if (pressed) continue;

        // Click mark: one color until the moment; the marker flashes when it arrives
        float closeness = missed ? 0.f : closenessOf(untilPress);
        auto markColor = color;
        float half = 3.f;
        rect(sx - half, cy - rowH * .4f, sx + half, cy + rowH * .4f, alpha(markColor, op), true);

        // Guide: in the last moment, a thin line up to the exact spot your icon will be
        if (s.guideLine && !missed && untilPress >= 0 && untilPress <= approach) {
            float a = closeness * .55f * op;
            if (s.outline) m_draw->drawSegment({sx, inner}, spot, 1.6f, {0.f, 0.f, 0.f, a * .8f});
            m_draw->drawSegment({sx, inner}, spot, .7f, alpha(markColor, a));
            if (s.outline) fillCircle(m_draw, spot, 3.2f, {0.f, 0.f, 0.f, closeness * .8f * op});
            fillCircle(m_draw, spot, 2.2f, alpha(markColor, closeness * op));
        }
    }

    // Hit marker on top of everything
    rect(rx - 1.5f, y0, rx + 1.5f, y1 + (s.trackTop ? 0.f : 3.f), {1.f, 1.f, 1.f, .95f}, true);
}
