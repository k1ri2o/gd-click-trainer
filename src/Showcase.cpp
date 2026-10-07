// Developer tool for the README screenshots, only active when GD is launched with
//   --geode:ct-showcase=<level id>   (1 = Stereo Madness; online levels must be saved)
// It opens the level, auto-plays its chart with the guide on, cycles the
// views, saves screenshots to the mod's save folder and quits. Nothing it changes
// is saved (settings are overridden in memory only).

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include "Trainer.hpp"
#include "TrainerPopup.hpp"
#include "ManiaOverlay.hpp"

using namespace geode::prelude;

namespace {
    struct Shot {
        int64_t tick;
        DisplayStyle style;
        std::string name;
    };

    std::string s_prefix = "level";
    int s_levelID = 1;

    void capture(std::string const& shotName) {
        auto name = s_prefix + "-" + shotName;
        auto dir = Mod::get()->getSaveDir() / "showcase";
        (void)file::createDirectoryAll(dir);
        auto size = CCDirector::get()->getWinSize();
        auto rt = CCRenderTexture::create(static_cast<int>(size.width), static_cast<int>(size.height));
        rt->begin();
        CCScene::get()->visit();
        rt->end();
        if (auto img = rt->newCCImage(true)) {
            img->saveToFile(string::pathToString(dir / (name + ".png")).c_str(), false);
            img->release();
        }
        log::info("SHOWCASE: saved {}", name);
    }

    void setStyle(DisplayStyle style) {
        auto& t = Trainer::get();
        t.settings.style = style;
        if (t.overlay) t.overlay->rebuild();
    }

    class ShowcaseDriver : public CCNode {
    public:
        static ShowcaseDriver* create() {
            auto ret = new ShowcaseDriver();
            ret->init();
            ret->autorelease();
            return ret;
        }

        void update(float dt) override {
            m_frames++;
            m_time += dt;
            auto pl = PlayLayer::get();
            auto& t = Trainer::get();

            if (m_stage == 0) {
                if (!pl || m_time < 2.5f) return;
                if (!t.hasChart || t.path.empty()) {
                    log::error("SHOWCASE: this level needs a chart with a route");
                    game::exit(false);
                    return;
                }
                // In-memory only: guide on, default look
                t.settings.enabled = true;
                t.settings.practiceOnly = false;
                t.settings.trailOpacity = .25f;
                t.autoplay = true;
                planShots();
                setStyle(m_shots.front().style);
                pl->resetLevelFromStart();
                m_stage = 1;
                m_stageTime = m_time;
                return;
            }

            if (m_stage == 1) {
                int64_t tick = pl ? static_cast<int64_t>(pl->m_gameState.m_currentProgress) : 0;
                if (m_next < m_shots.size()) {
                    auto const& shot = m_shots[m_next];
                    if (t.settings.style != shot.style && tick >= shot.tick - 400) setStyle(shot.style);
                    if (tick >= shot.tick) {
                        auto name = shot.name;
                        // Next frame: everything has been redrawn for this tick by then.
                        Loader::get()->queueInMainThread([name] { capture(name); });
                        m_next++;
                    }
                }
                if (m_time - m_lastLog > 2.f) {
                    m_lastLog = m_time;
                    log::info("SHOWCASE: tick {} next shot {}/{}", tick, m_next, m_shots.size());
                }
                if (m_next >= m_shots.size() || m_time - m_stageTime > 150.f) {
                    // Menu screenshots only once (from Stereo Madness)
                    m_stage = s_levelID == 1 ? 2 : 3;
                    m_stageTime = m_time;
                }
                return;
            }

            // Menus: the Clicks popup and the sections editor (example sections, not saved)
            float since = m_time - m_stageTime;
            auto once = [&](float at) {
                if (since >= at && m_done < at) {
                    m_done = at;
                    return true;
                }
                return false;
            };
            if (m_stage == 2) {
                if (once(.5f) && pl) {
                    setStyle(DisplayStyle::Gates);
                    pl->pauseGame(false);
                }
                if (once(1.f) && pl) TrainerPopup::create(pl->m_level)->show();
                if (once(1.6f)) capture("menu");
                if (once(2.f)) {
                    t.sections = {{12.f, 31.5f}, {58.f, 74.f}};
                    t.sectionsOnly = true;
                    SectionsPopup::create()->show();
                }
                if (once(2.6f)) capture("sections");
                if (once(3.2f)) game::exit(false);
            }
            if (m_stage == 3 && once(1.f)) game::exit(false);
        }

    private:
        int m_stage = 0;
        int m_frames = 0;
        float m_time = 0.f;
        float m_stageTime = 0.f;
        float m_lastLog = 0.f;
        float m_done = -1.f;
        size_t m_next = 0;
        std::vector<Shot> m_shots;

        void planShots() {
            auto& t = Trainer::get();
            std::vector<int64_t> presses;
            for (auto const& n : t.chart.notes) {
                if (n.start > 600) presses.push_back(t.chartToGame(n.start));
            }
            // Spread through the level: just before a click (ring / needle almost there)
            // and right at one (green pop)
            auto at = [&](float fraction) {
                size_t i = static_cast<size_t>(fraction * static_cast<float>(presses.size()));
                return presses[std::min(i, presses.size() - 1)];
            };
            m_shots = {
                {at(.12f) - 90, DisplayStyle::Gates, "gates"},
                {at(.2f) + 8, DisplayStyle::Gates, "gates-flash"},
                {at(.32f) - 110, DisplayStyle::TargetRings, "rings"},
                {at(.4f) + 8, DisplayStyle::TargetRings, "rings-flash"},
                {at(.55f) - 90, DisplayStyle::ClickTrack, "track"},
                {at(.7f) - 60, DisplayStyle::SideLane, "lane"},
            };
            std::sort(m_shots.begin(), m_shots.end(), [](auto const& a, auto const& b) { return a.tick < b.tick; });
        }
    };

    bool s_started = false;
}

class $modify(CTShowcaseMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto arg = Loader::get()->getLaunchArgument("ct-showcase");
        if (!arg || s_started) return true;
        s_started = true;
        if (auto id = numFromString<int>(*arg)) s_levelID = id.unwrap();
        Loader::get()->queueInMainThread([] {
            auto glm = GameLevelManager::sharedState();
            auto level = s_levelID <= 30 ? glm->getMainLevel(s_levelID, false) : glm->getSavedLevel(s_levelID);
            if (!level) {
                log::error("SHOWCASE: level {} isn't saved on this PC", s_levelID);
                game::exit(false);
                return;
            }
            // File name prefix from the level name: "Through The Decay" -> "through-the-decay"
            s_prefix.clear();
            for (char c : std::string(level->m_levelName)) {
                if (std::isalnum(static_cast<unsigned char>(c))) s_prefix += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                else if (!s_prefix.empty() && s_prefix.back() != '-') s_prefix += '-';
            }
            auto scene = PlayLayer::scene(level, false, false);
            // The driver lives on the scene so it keeps running while the level is paused.
            scene->addChild(ShowcaseDriver::create());
            scene->getChildByType<ShowcaseDriver>(0)->scheduleUpdate();
            CCDirector::get()->replaceScene(CCTransitionFade::create(.5f, scene));
        });
        return true;
    }
};
