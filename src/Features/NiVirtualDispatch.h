#pragma once

#include "RE/NetImmerse/NiAVObject.h"
#include "RE/NetImmerse/NiUpdateData.h"
#include "RE/NetImmerse/NiSmartPointer.h"

namespace NiVirtualDispatch
{
	namespace detail
	{
		template <class Fn>
		[[nodiscard]] inline Fn Slot(const void* object, std::size_t slot) noexcept
		{
			const auto* vtable = *reinterpret_cast<const void* const* const*>(object);
			return reinterpret_cast<Fn>(vtable[slot]);
		}
	}

	namespace detail
	{
		template <class T>
		[[nodiscard]] inline T* Raw(T* pointer) noexcept { return pointer; }
		template <class T>
		[[nodiscard]] inline T* Raw(const RE::NiPointer<T>& pointer) noexcept { return pointer.get(); }
	}

	inline constexpr std::size_t kVRSlot_SetAppCulled = 48;         
	inline constexpr std::size_t kVRSlot_GetObjectByName = 49;      
	inline constexpr std::size_t kVRSlot_UpdateDownwardPass = 51;   
	inline constexpr std::size_t kVRSlot_UpdateWorldData = 55;      
	inline constexpr std::size_t kVRSlot_CreateClone = 28;          
	inline constexpr std::size_t kVRSlot_ProcessClone = 34;         

	inline RE::NiObject* CreateClone(RE::NiObject* object, RE::NiCloningProcess& process)
	{
		if (!object) return nullptr;
		if (REL::Module::IsVR()) {
			using Fn = RE::NiObject* (*)(RE::NiObject*, RE::NiCloningProcess&);
			return detail::Slot<Fn>(object, kVRSlot_CreateClone)(object, process);
		}
		return object->CreateClone(process);
	}

	inline void ProcessClone(RE::NiObject* object, RE::NiCloningProcess& process)
	{
		if (!object) return;
		if (REL::Module::IsVR()) {
			using Fn = void (*)(RE::NiObject*, RE::NiCloningProcess&);
			detail::Slot<Fn>(object, kVRSlot_ProcessClone)(object, process);
			return;
		}
		object->ProcessClone(process);
	}

	template <class P>
	inline void SetAppCulled(const P& pointer, bool appCulled) noexcept
	{
		RE::NiAVObject* object = detail::Raw(pointer);
		if (!object)
			return;
		if (REL::Module::IsVR()) {
			using Fn = void (*)(RE::NiAVObject*, bool);
			detail::Slot<Fn>(object, kVRSlot_SetAppCulled)(object, appCulled);
			return;
		}
		object->SetAppCulled(appCulled);
	}

	template <class P>
	[[nodiscard]] inline RE::NiAVObject* GetObjectByName(const P& pointer, const RE::BSFixedString& name) noexcept
	{
		RE::NiAVObject* object = detail::Raw(pointer);
		if (!object)
			return nullptr;
		if (REL::Module::IsVR()) {
			using Fn = RE::NiAVObject* (*)(RE::NiAVObject*, const RE::BSFixedString&);
			return detail::Slot<Fn>(object, kVRSlot_GetObjectByName)(object, name);
		}
		return object->GetObjectByName(name);
	}

	template <class P>
	inline void UpdateDownwardPass(const P& pointer, RE::NiUpdateData& data, std::uint32_t flags) noexcept
	{
		RE::NiAVObject* object = detail::Raw(pointer);
		if (!object)
			return;
		if (REL::Module::IsVR()) {
			using Fn = void (*)(RE::NiAVObject*, RE::NiUpdateData&, std::uint32_t);
			detail::Slot<Fn>(object, kVRSlot_UpdateDownwardPass)(object, data, flags);
			return;
		}
		object->UpdateDownwardPass(data, flags);
	}

	template <class P>
	inline void UpdateWorldData(const P& pointer, RE::NiUpdateData* data) noexcept
	{
		RE::NiAVObject* object = detail::Raw(pointer);
		if (!object)
			return;
		if (REL::Module::IsVR()) {
			using Fn = void (*)(RE::NiAVObject*, RE::NiUpdateData*);
			detail::Slot<Fn>(object, kVRSlot_UpdateWorldData)(object, data);
			return;
		}
		object->UpdateWorldData(data);
	}
}
