#pragma once

#include "Trainer.hpp"

// Click markers drawn inside the level (child of the object layer), on the
// route the winning run took: a faint path line, a dot where to click, a thick
// bar for holds ending in a release ring, and osu!-style approach rings.
class WorldMarkers : public cocos2d::CCNode {
public:
    static WorldMarkers* create();
    void update(float dt) override;

protected:
    bool init() override;

    cocos2d::CCDrawNode* m_draw = nullptr;

    void drawGates(PlayLayer* pl, int64_t now, int64_t ahead);
};
