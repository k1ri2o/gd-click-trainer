#include "WorldMarkers.hpp"
#include "Draw.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace {
    constexpr int64_t kPathStep = 4;     // draw lines every 4 ticks (2 physics steps)
    constexpr int64_t kFlashTicks = 120; // ~250 ms burst at the exact moment
    constexpr int64_t kPopTicks = 34;    // first ~70 ms: solid, swollen, bold
    constexpr int kRingSegments = 40;
    constexpr float kIconHalf = 15.f;    // half a block: a normal-size icon

    ccColor4F alpha(ccColor4F c, float a) {
        c.a = std::clamp(a, 0.f, 1.f);
        return c;
    }

    // Draws everything twice when outlines are on: a dark, slightly wider pass
    // underneath and the color on top, so it reads on bright and dark backgrounds.
    struct Painter {
        CCDrawNode* draw;
        bool outline;

        ccColor4F shadow(float a) const { return {0.f, 0.f, 0.f, a * .75f}; }

        void segment(CCPoint a, CCPoint b, float radius, ccColor4F color) const {
            if (color.a <= .01f) return;
            if (outline) draw->drawSegment(a, b, radius + .8f, shadow(color.a));
            draw->drawSegment(a, b, radius, color);
        }

        void ring(CCPoint c, float radius, float width, ccColor4F color) const {
            if (color.a <= .01f) return;
            auto point = [&](int i) {
                float angle = static_cast<float>(i) / kRingSegments * 2.f * static_cast<float>(M_PI);
                return CCPoint{c.x + std::cos(angle) * radius, c.y + std::sin(angle) * radius};
            };
            if (outline) {
                for (int i = 0; i < kRingSegments; i++) draw->drawSegment(point(i), point(i + 1), width / 2.f + .8f, shadow(color.a));
            }
            for (int i = 0; i < kRingSegments; i++) draw->drawSegment(point(i), point(i + 1), width / 2.f, color);
        }
    };

    struct MarkEvent {
        int64_t tick;
        Note const* note;
        bool release;
    };
}

WorldMarkers* WorldMarkers::create() {
    auto ret = new WorldMarkers();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool WorldMarkers::init() {
    if (!CCNode::init()) return false;
    this->setID("world-markers"_spr);

    m_draw = CCDrawNode::create();
    m_draw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    this->addChild(m_draw);

    // Not scheduled: PlayLayer::postUpdate redraws us right after the physics step,
    // so markers are never a frame behind your icon.
    return true;
}

void WorldMarkers::update(float) {
    m_draw->clear();

    auto pl = PlayLayer::get();
    auto& t = Trainer::get();
    bool rings = pl && pl->m_player1 && t.showRings();
    bool gates = pl && pl->m_player1 && t.showGates();
    bool trackTrail = pl && t.showTrack() && t.settings.trailOpacity > 0.f;
    if (!rings && !gates && !trackTrail) return;
    if (t.guideVisibleHere()) t.cheatedThisAttempt = true;

    auto const& s = t.settings;
    auto const& path = t.path;
    auto const& chart = t.chart;
    Painter paint{m_draw, s.outline};

    int64_t now = t.visualNow(pl->m_gameState.m_currentProgress);
    int64_t ahead = now + std::max<int64_t>(s.lookAheadTicks, 1);

    auto drawPath = [&](int64_t from, int64_t to, bool p2, float radius, ccColor4F color) {
        for (int64_t g = from; g < to; g += kPathStep) {
            int64_t next = std::min(g + kPathStep, to);
            if (!path.has(next)) break;
            paint.segment(path.at(g, p2), path.at(next, p2), radius, color);
        }
    };

    // The route ahead (faint), only where the guide is on
    if (s.trailOpacity > 0.f && t.guideVisibleHere()) {
        drawPath(now, ahead, false, .7f, alpha(s.trailColor, s.trailOpacity));
        if (path.dual) drawPath(now, ahead, true, .7f, alpha(s.p2Color, s.trailOpacity));
    }
    if (gates) {
        this->drawGates(pl, now, ahead);
        return;
    }
    if (!rings) return;

    // Target circle where your icon will be (mini icons get smaller targets).
    // The approach ring and the target edge share one thickness, so the moment
    // the ring lands on the edge is easy to see.
    float radius = kIconHalf * 1.15f * pl->m_player1->m_vehicleSize * s.targetScale;
    constexpr float kLine = 1.5f;
    constexpr float kRingStart = 3.f; // the ring starts at 3x the target and closes onto its edge
    // Release ("let go") circles are smaller and thinner: less important, less in the way.
    float releaseRadius = std::max(radius * s.releaseScale, 3.f);
    auto radiusFor = [&](bool release) { return release ? releaseRadius : radius; };
    auto lineFor = [&](bool release) { return release ? kLine * .7f : kLine; };
    // The target is a point; the ring collapses onto it.
    auto dotFor = [&](bool release) { return (release ? 2.5f : 3.5f) * std::max(s.targetScale, .5f); };
    auto drawPoint = [&](CCPoint pos, float r, ccColor4F color) {
        if (color.a <= .01f) return;
        if (s.outline) fillCircle(m_draw, pos, r + 1.8f, {0.f, 0.f, 0.f, color.a * .8f});
        fillCircle(m_draw, pos, r + .8f, {1.f, 1.f, 1.f, color.a}); // white rim
        fillCircle(m_draw, pos, r, color);
    };

    auto laneColorOf = [&](Note const& n) {
        return chart.lanes[n.lane].player2 ? s.p2Color : s.p1Color;
    };
    auto posOf = [&](Note const& n, bool release) {
        return path.at(release ? t.chartToGame(n.end) : t.chartToGame(n.start), chart.lanes[n.lane].player2);
    };

    // Upcoming presses / releases, plus anything that just happened (for the burst).
    std::vector<MarkEvent> upcoming;
    for (auto const& n : chart.notes) {
        if (!n.shown) continue;
        int64_t start = t.chartToGame(n.start);
        int64_t end = t.chartToGame(n.end);
        if (start > ahead) break;

        bool missed = n.press == Judgement::Miss;
        // The only color change: a burst in the "now" color at the exact moment.
        for (bool release : {false, true}) {
            if (release && !n.isHold) continue;
            int64_t tick = release ? end : start;
            int64_t until = tick - now;
            if (missed || until > 0 || until <= -kFlashTicks || !path.has(tick)) continue;
            auto pos = posOf(n, release);
            float rad = dotFor(release) * 2.4f; // the point swells into a bold green disc
            float line = lineFor(release);
            int64_t since = -until;
            if (since < kPopTicks) {
                // Pop: solid filled circle, swollen, with a thick bold edge
                float q = static_cast<float>(since) / static_cast<float>(kPopTicks); // 0 -> 1
                float r = rad * (1.f + .3f * std::sin(q * static_cast<float>(M_PI) * .5f));
                if (s.outline) fillCircle(m_draw, pos, r + line + 1.5f, {0.f, 0.f, 0.f, .8f * s.ringOpacity});
                fillCircle(m_draw, pos, r, alpha(s.ringHitColor, .9f * s.ringOpacity));
                paint.ring(pos, r, line + (release ? 1.f : 2.f), alpha(s.ringHitColor, s.ringOpacity));
            }
            else {
                // Burst: the bold ring flies outward and fades
                float p = static_cast<float>(since - kPopTicks) / static_cast<float>(kFlashTicks - kPopTicks); // 0 -> 1
                float r = rad * 1.3f + radiusFor(release) * 1.2f * p;
                fillCircle(m_draw, pos, rad * 1.3f, alpha(s.ringHitColor, (1.f - p) * .45f * s.ringOpacity));
                paint.ring(pos, r, (line + (release ? 1.f : 2.f)) * (1.f - p) + .6f, alpha(s.ringHitColor, (1.f - p) * s.ringOpacity));
            }
        }

        if (n.press == Judgement::None && path.has(start)) upcoming.push_back({start, &n, false});
        if (n.isHold && !missed && n.release == Judgement::None && end <= ahead && path.has(end)) {
            upcoming.push_back({end, &n, true});
        }

        // Holds: a line along the route to the release
        if (n.isHold && !missed && n.release == Judgement::None) {
            int64_t from = std::max(start, now);
            int64_t to = std::min(end, ahead);
            bool holding = isHit(n.press);
            if (from < to) drawPath(from, to, chart.lanes[n.lane].player2, holding ? 1.8f : 1.2f,
                alpha(laneColorOf(n), (holding ? .75f : .45f) * s.markerOpacity));
        }
    }
    std::sort(upcoming.begin(), upcoming.end(), [](auto const& a, auto const& b) { return a.tick < b.tick; });
    if (static_cast<int>(upcoming.size()) > s.clicksAhead) upcoming.resize(s.clicksAhead);

    // Draw far ones first so the next click is on top.
    for (int i = static_cast<int>(upcoming.size()) - 1; i >= 0; i--) {
        auto const& e = upcoming[i];
        auto const& n = *e.note;
        bool next = i == 0;
        int64_t until = e.tick - now;
        auto pos = posOf(n, e.release);

        // Target point: press = your click color, release = orange. Colors never change here.
        auto color = e.release ? s.releaseColor : laneColorOf(n);
        float a = s.markerOpacity * (next ? 1.f : .4f);
        float rad = radiusFor(e.release);
        float line = lineFor(e.release);
        float dot = dotFor(e.release);
        drawPoint(pos, dot, alpha(color, a));

        // Approach ring: only the next click gets one. Same color and thickness the whole
        // way, shrinking at a steady speed until it sits exactly on the target's edge.
        if (!next || until < 0 || until > s.approachTicks) continue;
        float frac = static_cast<float>(until) / static_cast<float>(std::max<int64_t>(s.approachTicks, 1)); // 1 -> 0
        auto center = s.ringOnIcon ? pl->m_player1->getPosition() : pos;
        float ringAlpha = s.ringOpacity * std::clamp((1.f - frac) / .12f, 0.f, 1.f); // quick fade-in, then solid
        // Shrinks at a steady speed from 3x your icon down onto the point.
        float startR = rad * kRingStart;
        float endR = dot + line / 2.f + 1.f;
        paint.ring(center, endR + (startR - endR) * frac, line, alpha(s.ringColor, ringAlpha));
    }
}

// Gates: a thin vertical line in the level exactly where you click, and a thin
// "needle" through your icon. Click when the needle crosses the gate. Your icon
// moves at level speed, so the gap closes fast and the moment is exact; there is
// no "looks done already" phase like a ring closing onto a circle.
void WorldMarkers::drawGates(PlayLayer* pl, int64_t now, int64_t ahead) {
    auto& t = Trainer::get();
    auto const& s = t.settings;
    auto const& path = t.path;
    auto const& chart = t.chart;
    Painter paint{m_draw, s.outline};

    float iconHalf = kIconHalf * pl->m_player1->m_vehicleSize;
    float gateHalf = 45.f * s.targetScale;                          // 1.5 blocks up and down
    float releaseHalf = gateHalf * std::max(s.releaseScale, .2f);
    int64_t approach = std::max<int64_t>(s.approachTicks, 1);

    auto laneColorOf = [&](Note const& n) {
        return chart.lanes[n.lane].player2 ? s.p2Color : s.p1Color;
    };
    // Which way the route goes around a tick (+1 right, -1 left)
    auto dirAt = [&](int64_t tick, bool p2) {
        auto a = path.at(tick - 8, p2);
        auto b = path.at(tick + 8, p2);
        return b.x >= a.x ? 1.f : -1.f;
    };
    // The gate sits where your icon's center (or front edge) is at the click.
    auto gatePos = [&](int64_t tick, bool p2) {
        auto p = path.at(tick, p2);
        if (s.gateFront) p.x += dirAt(tick, p2) * iconHalf;
        return p;
    };
    auto drawPath = [&](int64_t from, int64_t to, bool p2, float radius, ccColor4F color) {
        for (int64_t g = from; g < to; g += kPathStep) {
            int64_t next = std::min(g + kPathStep, to);
            if (!path.has(next)) break;
            paint.segment(path.at(g, p2), path.at(next, p2), radius, color);
        }
    };

    std::vector<MarkEvent> upcoming;
    int64_t flashing = -1; // a gate is being crossed right now: the needle flashes too
    for (auto const& n : chart.notes) {
        if (!n.shown) continue;
        int64_t start = t.chartToGame(n.start);
        int64_t end = t.chartToGame(n.end);
        if (start > ahead) break;
        bool p2 = chart.lanes[n.lane].player2;
        bool missed = n.press == Judgement::Miss;

        // The moment: the gate pops into a thick bar in the "now" color, then fades.
        for (bool release : {false, true}) {
            if (release && !n.isHold) continue;
            int64_t tick = release ? end : start;
            int64_t since = now - tick;
            if (missed || since < 0 || since >= kFlashTicks || !path.has(tick)) continue;
            flashing = since;
            auto g = gatePos(tick, p2);
            float h = (release ? releaseHalf : gateHalf) * 1.1f;
            if (since < kPopTicks) {
                paint.segment({g.x, g.y - h}, {g.x, g.y + h}, release ? 2.f : 3.f, alpha(s.ringHitColor, s.ringOpacity));
            }
            else {
                float p = static_cast<float>(since - kPopTicks) / static_cast<float>(kFlashTicks - kPopTicks);
                m_draw->drawSegment({g.x, g.y - h}, {g.x, g.y + h}, 3.f + 6.f * p, alpha(s.ringHitColor, (1.f - p) * .35f * s.ringOpacity));
                paint.segment({g.x, g.y - h}, {g.x, g.y + h}, (release ? 2.f : 3.f) * (1.f - p) + .4f, alpha(s.ringHitColor, (1.f - p) * s.ringOpacity));
            }
        }

        if (n.press == Judgement::None && path.has(start)) upcoming.push_back({start, &n, false});
        if (n.isHold && !missed && n.release == Judgement::None && end <= ahead && path.has(end)) {
            upcoming.push_back({end, &n, true});
        }

        // Holds: a line along the route to the release
        if (n.isHold && !missed && n.release == Judgement::None) {
            int64_t from = std::max(start, now);
            int64_t to = std::min(end, ahead);
            bool holding = isHit(n.press);
            if (from < to) drawPath(from, to, p2, holding ? 1.8f : 1.2f, alpha(laneColorOf(n), (holding ? .75f : .45f) * s.markerOpacity));
        }
    }
    std::sort(upcoming.begin(), upcoming.end(), [](auto const& a, auto const& b) { return a.tick < b.tick; });
    if (static_cast<int>(upcoming.size()) > s.clicksAhead) upcoming.resize(s.clicksAhead);

    // Gates, far ones first so the next is on top. Colors never change before the moment.
    for (int i = static_cast<int>(upcoming.size()) - 1; i >= 0; i--) {
        auto const& e = upcoming[i];
        bool p2 = chart.lanes[e.note->lane].player2;
        bool next = i == 0;
        auto g = gatePos(e.tick, p2);
        auto color = e.release ? s.releaseColor : laneColorOf(*e.note);
        float h = e.release ? releaseHalf : gateHalf;
        float a = s.markerOpacity * (next ? 1.f : .35f);
        paint.segment({g.x, g.y - h}, {g.x, g.y + h}, e.release ? .6f : .9f, alpha(color, a));
        // Small dot at route height: where on the gate your icon passes
        if (s.outline) fillCircle(m_draw, g, (e.release ? 1.8f : 2.5f) + 1.f, {0.f, 0.f, 0.f, a * .8f});
        fillCircle(m_draw, g, e.release ? 1.8f : 2.5f, alpha(color, a));
    }

    // Needle through your icon (center or front edge), shown while a gate is coming up.
    bool needleWanted = flashing >= 0;
    float needleAlpha = 1.f;
    if (!upcoming.empty()) {
        int64_t until = upcoming.front().tick - now;
        if (until >= 0 && until <= approach) {
            needleWanted = true;
            needleAlpha = std::max(needleAlpha * std::clamp((1.f - static_cast<float>(until) / approach) / .2f, 0.f, 1.f), flashing >= 0 ? 1.f : 0.f);
        }
    }
    if (needleWanted) {
        auto pp = pl->m_player1->getPosition();
        float nx = pp.x + (s.gateFront ? dirAt(now, false) * iconHalf : 0.f);
        float nh = iconHalf + 8.f;
        bool popping = flashing >= 0 && flashing < kPopTicks;
        auto needleColor = popping ? s.ringHitColor : s.ringColor;
        paint.segment({nx, pp.y - nh}, {nx, pp.y + nh}, popping ? 1.8f : .7f, alpha(needleColor, needleAlpha * s.ringOpacity));
    }
}
