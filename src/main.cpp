#include <Geode/Geode.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/ShaderLayer.hpp>
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
static ccColor3B const LAYOUT_SCENERY_COLOR = ccc3(40, 62, 255);  // background and ground

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

static bool isHazardType(GameObject* obj) {
    return obj->m_objectType == GameObjectType::Hazard
        || obj->m_objectType == GameObjectType::AnimatedHazard;
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
        return !isHazardType(obj);
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

// The object types Layout mode keeps visible and flattens (portals, pads, orbs etc. are left alone)
static bool isLayoutShape(GameObject* obj) {
    auto t = obj->m_objectType;
    return t == GameObjectType::Solid
        || t == GameObjectType::Hazard
        || t == GameObjectType::AnimatedHazard
        || t == GameObjectType::Slope;
}

// Full opacity and flat colours, so alpha/colour effects can't hide or recolour the layout
static void applyLayoutLook(GameObject* obj) {
    if (!obj || obj->m_isTrigger || obj->m_isStartPos) return;
    if (!isLayoutShape(obj)) return;
    if (typeinfo_cast<PlayerObject*>(obj)) return;

    obj->setOpacity(255);
    obj->setObjectColor(isHazardType(obj) ? LAYOUT_HAZARD_COLOR : LAYOUT_OBJECT_COLOR);
    obj->setChildColor(LAYOUT_DETAIL_COLOR);
}

// Let the game recompute the object's real colours from its colour channels
static void restoreColors(GameObject* obj) {
    if (!obj || obj->m_isTrigger || obj->m_isStartPos) return;
    if (!isLayoutShape(obj)) return;
    if (typeinfo_cast<PlayerObject*>(obj)) return;

    obj->updateMainColor();
    if (obj->hasSecondaryColor()) {
        obj->updateSecondaryColor();
    }
}

// Background (1000) and ground (1001, 1009) channels become one flat colour
static bool layoutSceneryColor(int colorID, ccColor3B& out) {
    switch (colorID) {
        case 1000: case 1001: case 1009:
            out = LAYOUT_SCENERY_COLOR;
            return true;
        default:
            return false;
    }
}

static void applyFlatScenery(PlayLayer* pl) {
    cocos2d::ccHSVValue hsv{};
    int const ids[] = {1000, 1001, 1009};
    for (int id : ids) {
        ccColor3B color = LAYOUT_SCENERY_COLOR;
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
                        if (enteringLayout) applyLayoutLook(obj);
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

// ----- Layout: skip shader effects (the reference layout mode does the same) -----
class $modify(HideDecoShaderLayer, ShaderLayer) {
    void visit() {
        if (g_mode == DECO_LAYOUT && PlayLayer::get()) {
            CCNode::visit();
            return;
        }
        ShaderLayer::visit();
    }
};

// ----- Per-frame pass, level start/exit handling, and flat scenery in Layout mode -----
class $modify(HideDecoPlayLayer, PlayLayer) {
    // Every level starts with a clean state
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        g_mode = DECO_SHOWN;
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    // Reset before the level is torn down so none of our code runs during cleanup
    void onQuit() {
        g_mode = DECO_SHOWN;
        PlayLayer::onQuit();
    }

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

            // Layout: only objects that are currently on screen
            if (layout && obj->isVisible()) {
                applyLayoutLook(obj);
            }
        }
    }

    // Layout: the game's colour updates for background/ground become the flat colour
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
