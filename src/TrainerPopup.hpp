#pragma once

#include "Trainer.hpp"

// Per-level setup: record a run, import a macro, or delete the chart.
class TrainerPopup : public geode::Popup {
public:
    static TrainerPopup* create(GJGameLevel* level);
    void refresh();

protected:
    GJGameLevel* m_level = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;

    CCMenuItemSpriteExtra* m_recordBtn = nullptr;
    CCMenuItemSpriteExtra* m_deleteBtn = nullptr;
    CCMenuItemSpriteExtra* m_mapBtn = nullptr;
    CCMenuItemSpriteExtra* m_toggleBtn = nullptr;
    CCMenuItemSpriteExtra* m_styleBtn = nullptr;

    bool init(GJGameLevel* level);

    void onRecord(cocos2d::CCObject*);
    void onImport(cocos2d::CCObject*);
    void onDelete(cocos2d::CCObject*);
    void onOpenFolder(cocos2d::CCObject*);
    void onMap(cocos2d::CCObject*);
    void onToggle(cocos2d::CCObject*);
    void onStyle(cocos2d::CCObject*);
    void onCalibrate(cocos2d::CCObject*);
    void onSettings(cocos2d::CCObject*);
    void onSections(cocos2d::CCObject*);
};

// Closes all popups and restarts the level from the very start.
void restartFromStart();
// Arms the bot demo that maps the chart's clicks onto the level, then restarts.
void startBotDemo();

// Choose where in the level the guide shows: everywhere, or only in some sections.
class SectionsPopup : public geode::Popup {
public:
    static SectionsPopup* create();

protected:
    CCMenuItemToggler* m_onlyToggle = nullptr;
    cocos2d::CCNode* m_list = nullptr;
    geode::TextInput* m_fromInput = nullptr;
    geode::TextInput* m_toInput = nullptr;
    cocos2d::CCLabelBMFont* m_hereLabel = nullptr;

    bool init() override;
    void refreshList();
    void changed();

    void onOnly(cocos2d::CCObject*);
    void onStartHere(cocos2d::CCObject*);
    void onEndHere(cocos2d::CCObject*);
    void onAdd(cocos2d::CCObject*);
    void onRemove(cocos2d::CCObject* sender);
};

// Lists .gdr2 macros found in known bot folders.
class ImportPopup : public geode::Popup {
public:
    static ImportPopup* create(GJGameLevel* level, TrainerPopup* parent);

protected:
    GJGameLevel* m_level = nullptr;
    geode::WeakRef<TrainerPopup> m_parent;
    std::vector<std::filesystem::path> m_files;
    std::vector<bool> m_own; // a chart of yours from another level

    bool init(GJGameLevel* level, TrainerPopup* parent);
    void onPick(cocos2d::CCObject* sender);
};

std::vector<std::filesystem::path> macroSearchDirs();
