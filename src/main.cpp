#include <Geode/Geode.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/ui/Notification.hpp>

using namespace geode::prelude;

enum DecoMode {
    DECO_SHOWN = 0,
    DECO_LOW = 1,
    DECO_NORMAL = 2,
    DECO_AGGRESSIVE = 3,
    DECO_LAYOUT = 4
};

static int const MODE_COUNT = 5;

// Purely local, visual-only state. Nothing is saved to the level or sent anywhere.
static int g_mode = DECO_SHOWN;

// ----- Layout mode colours (edit these to taste) -----
static ccColor3B const LAYOUT_OBJECT_COLOR = ccc3(255, 255, 255);
static ccColor3B const LAYOUT_DETAIL_COLOR = ccc3(140, 140, 140);
static ccColor3B const LAYOUT_HAZARD_COLOR = ccc3(255, 255, 255); // try ccc3(255, 60, 60) for red spikes
static ccColor3B const LAYOUT_BG_COLOR = ccc3(40, 125, 255);
static ccColor3B const LAYOUT_GROUND_COLOR = ccc3(0, 102, 255);
static ccColor3B const LAYOUT_LINE_COLOR = ccc3(255, 255, 255);

static char const* modeName(int mode) {
    switch (mode) {
        case DECO_LOW: return "Decorations: Low";
        case DECO_NORMAL: return "Decorations: Normal";
        case DECO_AGGRESSIVE: return "Decorations: Aggressive";
        case DECO_LAYOUT: return "Decorations: Layout";
        default: return "Decorations: Shown";
    }
}

// Normal and above also hide particles of hidden objects
static bool hidesParticles(int mode) {
    return mode >= DECO_NORMAL;
}

// Aggressive and Layout also hide the glow layers
static bool hidesGlowLayers(int mode) {
    return mode >= DECO_AGGRESSIVE;
}

static bool isHideableRaw(GameObject* obj, int mode) {
    if (!obj || mode == DECO_SHOWN) return false;

    // Never touch triggers or start positions
    if (obj->m_isTrigger || obj->m_isStartPos) return false;

    // LOW+: regular decoration objects
    if (obj->m_objectType == GameObjectType::Decoration) return true;
    if (mode == DECO_LOW) return false;

    // NORMAL+: "No Touch" and "Passable" objects (they can't affect the player)
    if (obj->m_isNoTouch || obj->m_isPassable) return true;

    // AGGRESSIVE+: "High Detail" objects (the ones Low Detail Mode hides).
    // Hazards are never hidden this way, so spikes and saws stay visible.
    if (mode >= DECO_AGGRESSIVE && obj->m_isHighDetail) {
        return obj->m_objectType != GameObjectType::Hazard;
    }

    return false;
}

static bool isHideable(GameObject* obj, int mode) {
    if (!isHideableRaw(obj, mode)) return false;

    // Never hide the player icons
    if (typeinfo_cast<PlayerObject*>(obj)) return false;

    return true;
}

static void setGlowLayersVisible(PlayLayer* pl, bool visible) {
    CCNode* layers[] = {
        pl->m_glowLayerT4, pl->m_glowLayerT3, pl->m_glowLayerT2, pl->m_glowLayerT1,
        pl->m_glowLayerB1, pl->m_glowLayerB2, pl->m_glowLayerB3, pl->m_glowLayerB4,
        pl->m_glowLayerB5
    };
    for (auto layer : layers) {
        if (layer) layer->setVisible(visible);
    }
}

// ----- Layout mode helpers -----

// Flat colour for each object: hazards get the hazard colour, everything else the object colour
static void applyPlainColors(GameObject* obj) {
    if (!obj || obj->m_isTrigger || obj->m_isStartPos) return;
    if (typeinfo_cast<PlayerObject*>(obj)) return;

    bool hazard = obj->m_objectType == GameObjectType::Hazard;
    obj->setObjectColor(hazard ? LAYOUT_HAZARD_COLOR : LAYOUT_OBJECT_COLOR);
    obj->setChildColor(LAYOUT_DETAIL_COLOR);
}

// Let the game recompute the object's real colours from its colour channels
static void restoreColors(GameObject* obj) {
    if (!obj || obj->m_isTrigger || obj->m_isStartPos) return;
    if (typeinfo_cast<PlayerObject*>(obj)) return;

    obj->updateMainColor();
    if (obj->hasSecondaryColor()) {
        obj->updateSecondaryColor();
    }
}

static bool layoutSceneryColor(int colorID, ccColor3B& out) {
    switch (colorID) {
        case 1000: out = LAYOUT_BG_COLOR; return true;          // background
        case 1001: case 1009: out = LAYOUT_GROUND_COLOR; return true; // ground 1 and 2
        case 1002: out = LAYOUT_LINE_COLOR; return true;        // ground line
        default: return false;
    }
}

static void applyFlatScenery(PlayLayer* pl) {
    cocos2d::ccHSVValue hsv{};
    int const ids[] = {1000, 1001, 1002, 1009};
    for (int id : ids) {
        ccColor3B color = LAYOUT_BG_COLOR;
        layoutSceneryColor(id, color);
        pl->updateColor(color, 0.f, id, false, 1.f, hsv, 0, false, nullptr, 0, 0);
    }
}

// ----- Keybind: ] cycles the mode, only while inside a level -----
class $modify(HideDecoKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
        if (down && !repeat && key == KEY_RightBracket) {
            auto pl = PlayLayer::get();
            if (pl) {
                int prev = g_mode;
                g_mode = (g_mode + 1) % MODE_COUNT;

                bool enteringLayout = g_mode == DECO_LAYOUT;
                bool leavingLayout = prev == DECO_LAYOUT;

                if (pl->m_objects) {
                    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
                        if (!obj) continue;

                        bool wasHidden = isHideable(obj, prev);
                        bool nowHidden = isHideable(obj, g_mode);

                        // Restore objects that the previous mode hid but the new one doesn't
                        if (wasHidden && !nowHidden) {
                            obj->setVisible(true);
                        }

                        if (obj->m_particle && wasHidden) {
                            bool particleStillHidden = nowHidden && hidesParticles(g_mode);
                            if (!particleStillHidden) {
                                obj->m_particle->setVisible(true);
                            }
                        }

                        if (nowHidden) continue;

                        if (leavingLayout) restoreColors(obj);
                        if (enteringLayout) applyPlainColors(obj);
                    }
                }

                if (hidesGlowLayers(prev) && !hidesGlowLayers(g_mode)) {
                    setGlowLayersVisible(pl, true);
                }

                if (enteringLayout) {
                    applyFlatScenery(pl);
                }

                if (leavingLayout) {
                    pl->toggleMGVisibility(true);
                    pl->updateLevelColors();
                }

                Notification::create(
                    modeName(g_mode),
                    NotificationIcon::Success,
                    0.6f
                )->show();

                return true; // consume the key press while in a level
            }
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};

// ----- Stop the game from re-showing hidden objects (only inside a level) -----
class $modify(HideDecoGameObject, GameObject) {
    void setVisible(bool visible) {
        if (visible && g_mode != DECO_SHOWN && PlayLayer::get() && isHideable(this, g_mode)) {
            visible = false;
        }
        GameObject::setVisible(visible);
    }
};

// ----- Per-frame pass, plus keeping the background and ground flat in Layout mode -----
class $modify(HideDecoPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (g_mode == DECO_SHOWN) return;

        bool layout = g_mode == DECO_LAYOUT;

        if (hidesGlowLayers(g_mode)) {
            setGlowLayersVisible(this, false);
        }

        if (layout) {
            this->toggleMGVisibility(false);
        }

        if (!m_objects) return;

        bool hideParticles = hidesParticles(g_mode);

        for (auto obj : CCArrayExt<GameObject*>(m_objects)) {
            if (!obj) continue;

            if (isHideable(obj, g_mode)) {
                obj->setVisible(false);

                if (hideParticles && obj->m_particle) {
                    obj->m_particle->setVisible(false);
                }
                continue;
            }

            // Layout: flat colours, only for objects that are on screen
            if (layout && obj->isVisible()) {
                applyPlainColors(obj);
            }
        }
    }

    // Layout: the game's colour updates for background/ground become flat colours
    void updateColor(cocos2d::ccColor3B& color, float fadeTime, int colorID, bool blending, float opacity, cocos2d::ccHSVValue& copyHSV, int colorIDToCopy, bool copyOpacity, EffectGameObject* callerObject, int unk1, int unk2) {
        if (g_mode == DECO_LAYOUT) {
            ccColor3B flat = color;
            if (layoutSceneryColor(colorID, flat)) {
                PlayLayer::updateColor(flat, fadeTime, colorID, blending, opacity, copyHSV, 0, false, callerObject, unk1, unk2);
                return;
            }
        }
        PlayLayer::updateColor(color, fadeTime, colorID, blending, opacity, copyHSV, colorIDToCopy, copyOpacity, callerObject, unk1, unk2);
    }
};
