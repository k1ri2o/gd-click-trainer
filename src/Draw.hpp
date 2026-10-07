#pragma once

#include <Geode/Geode.hpp>
#include <cmath>

// GD's CCDrawNode::drawDot draws a square, so filled circles are drawn as polygons.
inline void fillCircle(cocos2d::CCDrawNode* draw, cocos2d::CCPoint center, float radius, cocos2d::ccColor4F color, int segments = 24) {
    if (color.a <= .01f || radius <= 0.f) return;
    cocos2d::CCPoint verts[48];
    segments = std::clamp(segments, 8, 48);
    for (int i = 0; i < segments; i++) {
        float angle = static_cast<float>(i) / segments * 2.f * static_cast<float>(M_PI);
        verts[i] = cocos2d::CCPoint{center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
    }
    draw->drawPolygon(verts, segments, color, 0.f, {0.f, 0.f, 0.f, 0.f});
}
