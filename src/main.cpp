#include <Geode/Geode.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>

using namespace geode::prelude;

// Purely local, visual-only state. Nothing is saved to the level or sent anywhere.
static bool g_hideDecorations = false;

static bool isDecoration(GameObject* obj) {
    return obj && obj->m_objectType == GameObjectType::Decoration;
}

// ----- Keybind: ] toggles, only while inside a level -----
class $modify(HideDecoKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
        if (down && !repeat && key == KEY_RightBracket) {
            auto pl = PlayLayer::get();
            if (pl) {
                g_hideDecorations = !g_hideDecorations;

                // Restore decorations right away when turning the effect off
                if (!g_hideDecorations && pl->m_objects) {
                    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
                        if (isDecoration(obj)) obj->setVisible(true);
                    }
                }

                Notification::create(
                    g_hideDecorations ? "Decorations: Hidden" : "Decorations: Shown",
                    NotificationIcon::Success,
                    0.6f
                )->show();

                return true; // consume the key press while in a level
            }
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};

// ----- Re-apply hiding every frame (the game's culling re-shows objects) -----
class $modify(HideDecoPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!g_hideDecorations || !m_objects) return;

        for (auto obj : CCArrayExt<GameObject*>(m_objects)) {
            if (isDecoration(obj)) obj->setVisible(false);
        }
    }
};
