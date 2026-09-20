#include "PCH.h"
#include "MirrorQualityMenu.h"
#include "MirrorQualityControls.h"
#include "MirrorSettings.h"

namespace
{
	using Value = RE::Scaleform::GFx::Value;
	using Movie = RE::Scaleform::GFx::Movie;
	constexpr auto kStorage = "root.mirrorQualityControls";
	constexpr auto kList = "root.mcm_loader.content.mcmMenu.configPanel_mc.configList_mc";
	constexpr auto kChange = "mcmSettingsOptionItem::value_change";
	constexpr unsigned kMaxEntries = 128;

	double Number(const Value& value, double fallback = 0)
	{
		if (value.IsBoolean()) return value.GetBoolean() ? 1 : 0;
		if (value.IsNumber()) return value.GetNumber();
		if (value.IsInt()) return value.GetInt();
		if (value.IsUInt()) return value.GetUInt();
		return fallback;
	}
	std::string_view String(const Value& value) { return value.IsString() ? value.GetString() : ""; }
	Value Member(const Value& object, const char* name)
	{
		Value result;
		if (object.IsObject()) object.GetMember(name, &result);
		return result;
	}
	Value Entry(const Value& array, unsigned index)
	{
		return Member(array, std::to_string(index).c_str());
	}
	bool Owned(const Value& entry)
	{
		return String(Member(entry, "modName")) == "MirrorsOfFallout" &&
			MirrorQualityControls::Owned(String(Member(entry, "id")));
	}
	MirrorQualityControls::State ReadState(const Value& entries)
	{
		MirrorQualityControls::State state;
		if (!entries.IsArray()) return state;
		for (unsigned i = 0; i < (std::min)(entries.GetArraySize(), kMaxEntries); ++i) {
			const auto entry = Entry(entries, i);
			if (!Owned(entry)) continue;
			const auto id = Member(entry, "id");
			const auto value = Number(Member(entry, "value"));
			if (String(id) == "iQualityMode:Mirrors") state.mode = MirrorQualityMode::Clamp(static_cast<int>(value));
			else if (String(id) == "bEveryFrameRefresh:Mirrors") state.everyFrame = value != 0;
			else if (String(id) == "bShadows:Mirrors") state.shadows = value != 0;
		}
		return state;
	}

	bool SynchronizeFrameworkChanges(Movie* movie, Value& storage)
	{
		const auto revision = MirrorSettings::MenuRevision();
		if (!revision || Number(Member(storage, "frameworkRevision")) == static_cast<double>(revision)) return true;
		Value api;
		if (!movie->GetVariable(&api, "root.mcm") || !api.IsObject()) return false;
		// The saved snapshot survives a partially successful cache update. MCM
		// setters can rewrite the INI, so do not reread a half-synchronized file.
		const auto changes = MirrorSettings::MenuChanges();
		struct Cache {
			Value& api;
			bool Read(const MirrorSettingsMenu::Control& control, int& value) {
				Value arguments[]{Value("MirrorsOfFallout"), Value(control.id)}, result;
				if (!api.Invoke(control.Boolean() ? "GetModSettingBool" : "GetModSettingInt", &result, arguments, 2)) return false;
				value = static_cast<int>(Number(result, -1));
				return true;
			}
			bool Write(const MirrorSettingsMenu::Control& control, int value) {
				Value arguments[]{Value("MirrorsOfFallout"), Value(control.id),
					control.Boolean() ? Value(value != 0) : Value(value)}, result;
				return api.Invoke(control.Boolean() ? "SetModSettingBool" : "SetModSettingInt", &result, arguments, 3) &&
					Number(result) != 0;
			}
		} cache{api};
		if (!MirrorSettingsMenu::Synchronize(changes, cache)) return false;
		Value list;
		if (movie->GetVariable(&list, kList) && list.IsObject()) {
			const auto entries = Member(list, "entryList");
			if (entries.IsArray()) {
				for (unsigned index = 0; index < (std::min)(entries.GetArraySize(), kMaxEntries); ++index) {
					auto entry = Entry(entries, index);
					if (String(Member(entry, "modName")) != "MirrorsOfFallout") continue;
					const auto id = String(Member(entry, "id"));
					for (unsigned i = 0; i < MirrorSettingsMenu::Count; ++i)
						if ((changes.changed & (1u << i)) && id == MirrorSettingsMenu::controls[i].id)
							entry.SetMember("value", MirrorSettingsMenu::controls[i].Boolean() ?
								Value(changes.values[i] != 0) : Value(changes.values[i]));
				}
				list.Invoke("InvalidateData");
			}
		}
		storage.SetMember("frameworkRevision", Value(static_cast<double>(changes.revision)));
		return true;
	}

	bool SynchronizeMode(Movie* movie, Value& storage)
	{
		if (Number(Member(storage, "modeSynchronized")) != 0) return true;
		const auto mode = MirrorSettings::QualityModeForMenu();
		Value api, cached;
		if (mode < 0 || !movie->GetVariable(&api, "root.mcm") || !api.IsObject()) return false;
		Value arguments[]{Value("MirrorsOfFallout"), Value("iQualityMode:Mirrors"), Value(mode)};
		if (!api.Invoke("GetModSettingInt", &cached, arguments, 2)) return false;
		if (Number(cached, -1) != mode) {
			Value saved;
			if (!api.Invoke("SetModSettingInt", &saved, arguments, 3) || Number(saved) == 0) return false;
			logger::info("[MirrorSettings] migrated MCM quality selector to {}", MirrorQualityMode::Name(MirrorQualityMode::Clamp(mode)));
		}
		storage.SetMember("modeSynchronized", Value(true));
		return true;
	}

	class SelectQualityMode final : public RE::Scaleform::GFx::FunctionHandler
	{
		void Call(const Params& params) override
		{
			if (params.argCount) MirrorSettings::SelectQualityModeForMenu(static_cast<int>(Number(params.args[0], -1)));
		}
	};

	class ChangeGuard final : public RE::Scaleform::GFx::FunctionHandler
	{
		void Call(const Params& params) override
		{
			if (!params.movie || !params.argCount || !params.args[0].IsObject()) return;
			auto event = params.args[0];
			auto target = Member(event, "target");

			const auto list = Member(event, "currentTarget");
			const auto entries = Member(list, "entryList");
			const auto item = Number(Member(target, "itemIndex"), -1);
			if (!entries.IsArray() || item < 0 || item >= entries.GetArraySize()) return;
			const auto entry = Entry(entries, static_cast<unsigned>(item));
			if (!Owned(entry)) return;
			const auto id = String(Member(entry, "id"));
			if (id == "iQualityMode:Mirrors") {

				const auto mode = MirrorQualityMode::Clamp(static_cast<int>(Number(Member(target, "value"))));
				if (mode != MirrorQualityMode::Mode::Automatic) {
					Value api;
					if (params.movie->GetVariable(&api, "root.mcm") && api.IsObject()) {
						Value arguments[]{Value("MirrorsOfFallout"), Value("bAutomaticQuality:Mirrors"),
							Value(mode == MirrorQualityMode::Mode::Preset)};
						api.Invoke("SetModSettingBool", nullptr, arguments, 3);
					}
				}
				return;
			}
			if (!MirrorQualityControls::Hidden(id, ReadState(entries))) return;
			event.Invoke("stopImmediatePropagation");
			event.Invoke("preventDefault");
			target.SetMember("value", Member(entry, "value"));
		}
	};

	class Refresh final : public RE::Scaleform::GFx::FunctionHandler
	{
		void Call(const Params& params) override
		{
			if (!params.movie) return;
			Value storage;
			if (!params.movie->GetVariable(&storage, kStorage) || !storage.IsObject()) return;

			if (!SynchronizeFrameworkChanges(params.movie, storage) || !SynchronizeMode(params.movie, storage)) return;
			Value list;
			if (!params.movie->GetVariable(&list, kList) || !list.IsObject()) return;
			auto registered = Member(storage, "registered");
			Value index;
			if (!registered.IsArray() || !registered.Invoke("indexOf", &index, &list, 1)) return;
			if (Number(index, -1) < 0) {
				auto guard = Member(storage, "guard");
				Value arguments[]{Value(kChange), guard, Value(true), Value(1000), Value(false)};
				if (!list.Invoke("addEventListener", nullptr, arguments, std::size(arguments))) return;
				registered.Invoke("push", nullptr, &list, 1);
				logger::info("[MirrorSettings] MCM quality controls attached: inactive settings hidden from layout and navigation");
			}
			const auto entries = Member(list, "entryList");
			if (!entries.IsArray()) return;
			const auto state = ReadState(entries);
			bool changed = false;
			for (unsigned i = 0; i < (std::min)(entries.GetArraySize(), kMaxEntries); ++i) {
				auto entry = Entry(entries, i);
				if (!Owned(entry)) continue;

				if (String(Member(entry, "filterOperator")) != "OR")
					changed |= entry.SetMember("filterOperator", Value("OR"));
				const auto flag = MirrorQualityControls::FilterFlag(String(Member(entry, "id")), state);
				if (Number(Member(entry, "filterFlag"), -1) == flag) continue;
				changed |= entry.SetMember("filterFlag", Value(flag));
			}
			if (changed) {

				list.Invoke("onFilterChange");
				list.Invoke("InvalidateData");
			}
		}
	};

	bool F4SEAPI OnMovie(Movie* movie, Value* plugin)
	{
		if (!movie) return true;
		if (plugin && plugin->IsObject()) {
			Value function;
			auto* handler = new SelectQualityMode;
			movie->CreateFunction(&function, handler); handler->Release();
			plugin->SetMember("SelectQualityMode", function);
		}
		Value url, root;
		if (!movie->GetVariable(&url, "root.loaderInfo.url") ||
			(String(url) != "Interface/MainMenu.swf" && String(url) != "Interface/world_MainMenu.swf") ||
			!movie->GetVariable(&root, "root")) return true;
		Value storage, registered, refresh, guard;
		movie->CreateObject(&storage);
		movie->CreateArray(&registered);
		auto* refresher = new Refresh;
		auto* blocker = new ChangeGuard;
		movie->CreateFunction(&refresh, refresher); refresher->Release();
		movie->CreateFunction(&guard, blocker); blocker->Release();
		storage.SetMember("guard", guard);
		storage.SetMember("registered", registered);
		if (!root.SetMember("mirrorQualityControls", storage)) return true;
		Value arguments[]{Value("enterFrame"), refresh};
		root.Invoke("addEventListener", nullptr, arguments, std::size(arguments));
		return true;
	}
}

void MirrorQualityMenu::Register()
{
	if (const auto* scaleform = F4SE::GetScaleformInterface(); scaleform && scaleform->Register("MirrorQualityControls", OnMovie))
		logger::info("[MirrorSettings] MCM quality control adapter registered");
	else logger::warn("[MirrorSettings] MCM quality control adapter unavailable");
}
