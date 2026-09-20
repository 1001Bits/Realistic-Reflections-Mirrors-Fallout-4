#include "PCH.h"
#include "MirrorFrameworkMenu.h"
#include "MirrorFrameworkWidgets.h"
#include "MirrorSettings.h"
#include <mutex>

namespace
{
    MirrorFrameworkWidgets::API ui;
    std::mutex mutex;
    MirrorSettingsMenu::Values original, edited;
    bool installed{}, loaded{}, saveFailed{};

    bool Save()
    {
        if (!loaded || !edited.ChangesFrom(original)) { saveFailed = false; return true; }
        if (!MirrorSettings::SaveMenuValues(edited, edited.ChangesFrom(original))) {
            saveFailed = true;
            return false;
        }
        original = edited;
        saveFailed = false;
        return true;
    }

    void __stdcall Render()
    {
        std::lock_guard lock(mutex);
        if (!loaded) {
            if (!MirrorSettings::ReadMenuValues(edited)) {
                ui.text("Mirror settings will be available when initialization finishes.", nullptr);
                return;
            }
            original = edited;
            loaded = true;
        }
        ui.text("Realistic Reflections - Mirrors", nullptr);
        ui.separator();
        MirrorFrameworkWidgets::Draw(ui, edited);
        ui.separator();
        ui.wrapped("%s", "Changes are saved and applied when you close the framework menu. Your manual choices are kept when changing quality modes.");
        if (ui.button("Apply")) Save();
        if (saveFailed)
            ui.wrapped("%s", "Settings could not be saved. Check that the settings file is writable, then press Apply to retry.");
    }
}

void MirrorFrameworkMenu::Install(HMODULE framework)
{
    if (installed || !framework) return;
    MirrorFrameworkWidgets::API candidate;
    if (!candidate.Load([&](const char* name) { return GetProcAddress(framework, name); })) {
        logger::warn("[MirrorSettings] Menu Framework settings page unavailable: required widget API missing");
        return;
    }
    ui = candidate;
    ui.addSection("Realistic Reflections - Mirrors/Settings", Render);
    installed = true;
    logger::info("[MirrorSettings] Menu Framework settings page registered; shared MCM preferences and quality modes");
}

void MirrorFrameworkMenu::Open()
{
    std::lock_guard lock(mutex);
    if (!saveFailed) loaded = false;
}

void MirrorFrameworkMenu::Close()
{
    std::lock_guard lock(mutex);
    if (Save()) loaded = false;
}
