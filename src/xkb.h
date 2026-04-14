#pragma once

#include <iostream>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <sys/mman.h>
#include <unistd.h>

namespace
{
    struct XKBStateDeleter {
        void operator()(struct xkb_state *state) const
        {
            return xkb_state_unref(state);
        }
    };
    struct XKBKeymapDeleter {
        void operator()(struct xkb_keymap *keymap) const
        {
            return xkb_keymap_unref(keymap);
        }
    };
    struct XKBContextDeleter {
        void operator()(struct xkb_context *context) const
        {
            return xkb_context_unref(context);
        }
    };
    using ScopedXKBState = std::unique_ptr<struct xkb_state, XKBStateDeleter>;
    using ScopedXKBKeymap = std::unique_ptr<struct xkb_keymap, XKBKeymapDeleter>;
    using ScopedXKBContext = std::unique_ptr<struct xkb_context, XKBContextDeleter>;
}

class Xkb
{
public:
    struct Code {
        const uint32_t level;
        const uint32_t code;
        const uint32_t layout;
    };
    std::optional<Code> keycodeFromKeysym(xkb_keysym_t keysym, uint32_t preferredLayout = 0)
    {
        // The offset between KEY_* numbering, and keycodes in the XKB evdev dataset.
        static const uint EVDEV_OFFSET = 8;

        auto num_layouts = xkb_keymap_num_layouts(m_keymap.get());
        const xkb_keycode_t max = xkb_keymap_max_keycode(m_keymap.get());

        // Search the preferred (current) layout first to minimize layout switches.
        // Characters common to multiple layouts (e.g. ASCII letters) will be found
        // on the current layout, avoiding an unnecessary switch and the race condition
        // between FakeInput (Wayland) and setLayout (DBus).
        for (uint32_t i = 0; i < num_layouts; i++) {
            uint32_t layout = (preferredLayout + i) % num_layouts;
            for (xkb_keycode_t keycode = xkb_keymap_min_keycode(m_keymap.get()); keycode < max; keycode++) {
                uint levelCount = xkb_keymap_num_levels_for_key(m_keymap.get(), keycode, layout);
                for (uint currentLevel = 0; currentLevel < levelCount; currentLevel++) {
                    const xkb_keysym_t *syms;
                    uint num_syms = xkb_keymap_key_get_syms_by_level(m_keymap.get(), keycode, layout, currentLevel, &syms);
                    for (uint sym = 0; sym < num_syms; sym++) {
                        if (syms[sym] == keysym) {
                            return Code{currentLevel, keycode - EVDEV_OFFSET, layout};
                        }
                    }
                }
            }
        }
        return {};
    }

    void keyboardKeymap(int32_t fd, uint32_t size)
    {
        char *map_str = static_cast<char *>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
        if (map_str == MAP_FAILED) {
            close(fd);
            return;
        }

        m_keymap.reset(xkb_keymap_new_from_string(m_ctx.get(), map_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS));
        munmap(map_str, size);
        close(fd);

        if (m_keymap)
            m_state.reset(xkb_state_new(m_keymap.get()));
        else
            m_state.reset(nullptr);
    }

    static Xkb *self()
    {
        static Xkb self;
        return &self;
    }

private:
    Xkb()
    {
        m_ctx.reset(xkb_context_new(XKB_CONTEXT_NO_FLAGS));
        if (!m_ctx) {
            std::cout << "Failed to create xkb context";
            return;
        }
        m_keymap.reset(xkb_keymap_new_from_names(m_ctx.get(), nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS));
        if (!m_keymap) {
            std::cout << "Failed to create the keymap";
            return;
        }
        m_state.reset(xkb_state_new(m_keymap.get()));
        if (!m_state) {
            std::cout << "Failed to create the xkb state";
            return;
        }
    }

    ScopedXKBContext m_ctx;
    ScopedXKBKeymap m_keymap;
    ScopedXKBState m_state;
};
