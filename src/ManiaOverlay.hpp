#pragma once

#include "Trainer.hpp"

// osu!mania-style note lane drawn over PlayLayer.
class ManiaOverlay : public cocos2d::CCNode {
public:
    static ManiaOverlay* create();
    ~ManiaOverlay() override;

    void rebuild();
    void showJudgement(Judgement j, int64_t diffTicks);
    void update(float dt) override;

protected:
    bool init() override;

    cocos2d::CCDrawNode* m_draw = nullptr;
    cocos2d::CCNode* m_laneLabels = nullptr;
    cocos2d::CCLabelBMFont* m_judgeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_offsetLabel = nullptr;
    cocos2d::CCLabelBMFont* m_comboLabel = nullptr;
    cocos2d::CCLabelBMFont* m_accLabel = nullptr;
    cocos2d::CCLabelBMFont* m_recLabel = nullptr;
    cocos2d::CCLabelBMFont* m_safeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
    cocos2d::CCNode* m_feedback = nullptr; // judgement + offset + combo, follows the player or the lane
    float m_laneX = 0.f;

    float laneAreaWidth() const;
    void drawLane(PlayLayer* pl, int64_t tick);
};
