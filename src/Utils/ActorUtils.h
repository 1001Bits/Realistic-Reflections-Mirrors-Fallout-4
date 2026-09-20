

#pragma once
#include "RE/Fallout.h"
#include <string>
#include <vector>

namespace RE
{
	class bhkNiCollisionObject;
	class hkpShape;
	class NiPoint3;
}

namespace Util
{

	bool GetShapeBound(RE::bhkNiCollisionObject* collisionObj, RE::NiPoint3& centerPos, float& radius);

	bool ExtractShapeBound(const RE::hkpShape* shape, float& radius);

	struct ActorDisplayInfo
	{
		RE::TESObjectREFR* actor;  
		std::string name;          
		std::string formID;        
		std::string type;          
		RE::NiPoint3 pos;          
		float sqDist;              
	};

	inline bool GetRagdollCenter(RE::Actor* actor, RE::NiPoint3& outCenter)
	{
		if (!actor || !actor->IsDead(true))
			return false;
		if (auto root = actor->Get3D(false)) {
			bool found = false;
			RE::NiPoint3 ragdollCenter;

			(void)root;
			if (found) {
				outCenter = ragdollCenter;
				return true;
			}
		}
		return false;
	}

	inline bool GetActorDisplayInfo(RE::TESObjectREFR* ref, const RE::NiPoint3& eyePos, bool trackRagdolls, ActorDisplayInfo& outInfo)
	{
		if (!ref)
			return false;
		auto actor = static_cast<RE::Actor*>(ref);
		outInfo.actor = ref;
		outInfo.name = ref->GetDisplayFullName();
		outInfo.formID = std::format("{:X}", ref->GetFormID());
		outInfo.type = "Actor";
		if (actor && actor->IsDead(true)) {
			if (!trackRagdolls)
				return false;
			outInfo.type = "Actor (Dead/Ragdoll)";
			RE::NiPoint3 pos;
			if (!GetRagdollCenter(actor, pos)) {
				pos = actor->GetPosition();
			}
			outInfo.pos = pos;
		} else {
			outInfo.pos = ref->GetPosition();
		}
		outInfo.sqDist = outInfo.pos.GetSquaredDistance(eyePos);
		return true;
	}
}
