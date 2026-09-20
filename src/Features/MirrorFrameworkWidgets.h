#pragma once

#include "MirrorSettingsMenu.h"
#include <type_traits>
#include <windows.h>

namespace MirrorFrameworkWidgets
{
    // Optional host ABI, verified against DCCStudios/F4SEMenuFramework's consumer API.
    // All widget calls use the host's ImGui context; no ImGui objects cross DLLs.
    struct API {
        using Render = void (__stdcall*)();
        void (*addSection)(const char*, Render){};
        void (*text)(const char*, const char*){};
        void (*wrapped)(const char*, ...){};
        void (*separator)(){};
        bool (*combo)(const char*, int*, const char*, int){};
        bool (*slider)(const char*, int*, int, int, const char*, int){};
        bool (*checkbox)(const char*, bool*){};
        bool (*button)(const char*){};
        bool (*hovered)(int){};
        void (*tooltip)(const char*, ...){};

        template<class Resolve> bool Load(Resolve resolve) {
            const auto bind = [&](auto& function, const char* name) {
                function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(resolve(name));
                return function != nullptr;
            };
            return bind(addSection, "AddSectionItem") && bind(text, "igTextUnformatted") &&
                bind(wrapped, "igTextWrapped") && bind(separator, "igSeparator") &&
                bind(combo, "igCombo_Str") && bind(slider, "igSliderInt") &&
                bind(checkbox, "igCheckbox") && bind(button, "igSmallButton") &&
                bind(hovered, "igIsItemHovered") && bind(tooltip, "igSetTooltip");
        }
    };

    inline void Draw(API& ui, MirrorSettingsMenu::Values& values) {
        using namespace MirrorSettingsMenu;
        for (unsigned i = 0; i < Count; ++i) {
            if (values.Hidden(i)) continue;
            if (i == DebugKeys) { ui.separator(); ui.text("Debug", nullptr); }
            const auto& control = controls[i];
            int value = values[i];
            bool changed = false;
            if (control.kind == Kind::Choice)
                changed = ui.combo(control.label, &value, control.choices, -1);
            else if (control.kind == Kind::Slider)
                changed = ui.slider(control.label, &value, control.minimum, control.maximum, "%d", 0);
            else if (control.kind == Kind::Toggle) {
                bool enabled = value != 0;
                changed = ui.checkbox(control.label, &enabled);
                value = enabled;
            }
            if (changed) values.Set(i, value);
            if (ui.hovered(0)) ui.tooltip("%s", control.help);
        }
    }
}
