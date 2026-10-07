#pragma once

#include "Trainer.hpp"

// A thin strip along the screen edge. Click marks sit exactly below the spot in
// the level where you'll be when you click and scroll with the level; the
// marker under your icon is the hit point. Nothing covers the play area except
// an optional thin guide line in the last moment before a click.
class ClickTrack : public cocos2d::CCNode {
public:
    static ClickTrack* create();
    void update(float dt) override;

    // Where the hit marker is (for placing judgement text), in this node's space.
    cocos2d::CCPoint hitPoint() const { return m_hitPoint; }

protected:
    bool init() override;

    cocos2d::CCDrawNode* m_draw = nullptr;
    cocos2d::CCPoint m_hitPoint;
};
