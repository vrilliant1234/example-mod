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
    DECO_AGGRESSIVE = 3
};

// Purely local, visual-only state. Nothing is saved to the level or sent anywhere.
static int g_mode = DECO_SHOWN;

static char const* modeName(int mode) {
    switch (mode) {
        case DECO_LOW: return "Decorations: Low";
        case DECO_NORMAL: return "Decorations: Normal";
        case DECO_AGGRESSIVE: return "Decorations: Aggressive";
        default: return "Decorations: Shown";
    }
}

// Normal and Aggressive also hide particles of hidden objects
static bool hidesParticles(int mode) {
    return mode >= DECO_NORMAL;
}

// Aggressive also hides the glow layers
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

    // AGGRESSIVE: "High Detail" objects (the ones Low Detail Mode hides).
    // Hazards are never hidden this way, so spikes and saws stay visible.
    if (mode == DECO_AGGRESSIVE && obj->m_isHighDetail) {
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

// ----- Keybind: ] cycles the mode, only while inside a level -----
class $modify(HideDecoKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
        if (down && !repeat && key == KEY_RightBracket) {
            auto pl = PlayLayer::get();
            if (pl) {
                int prev = g_mode;
                g_mode = (g_mode + 1) % 4;

                // Restore objects that the previous mode hid but the new one doesn't
                if (pl->m_objects) {
                    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
                        if (!obj) continue;

                        bool wasHidden = isHideable(obj, prev);
                        bool nowHidden = isHideable(obj, g_mode);

                        if (wasHidden && !nowHidden) {
                            obj->setVisible(true);
                        }

                        if (obj->m_particle && wasHidden) {
                            bool particleStillHidden = nowHidden && hidesParticles(g_mode);
                            if (!particleStillHidden) {
                                obj->m_particle->setVisible(true);
                            }
                        }
                    }
                }

                if (hidesGlowLayers(prev) && !hidesGlowLayers(g_mode)) {
                    setGlowLayersVisible(pl, true);
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

// ----- Per-frame pass: particles, glow layers, and anything the hook missed -----
class $modify(HideDecoPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (g_mode == DECO_SHOWN) return;

        if (hidesGlowLayers(g_mode)) {
            setGlowLayersVisible(this, false);
        }

        if (!m_objects) return;

        bool hideParticles = hidesParticles(g_mode);

        for (auto obj : CCArrayExt<GameObject*>(m_objects)) {
            if (!isHideable(obj, g_mode)) continue;

            obj->setVisible(false);

            if (hideParticles && obj->m_particle) {
                obj->m_particle->setVisible(false);
            }
        }
    }
};
