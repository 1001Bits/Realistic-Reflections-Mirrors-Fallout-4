#include "MirrorDefinitionRegistry.h"

#include <array>

namespace MirrorDefinitionRegistry
{
	namespace
	{
		constexpr std::string_view kFallout4{ "Fallout4.esm" };
		constexpr std::string_view kNukaWorld{ "DLCNukaWorld.esm" };

		constexpr TransformMetadata kReferencePlacement{
			TransformPolicy::kReferencePlacement, {}, {}
		};
		constexpr TransformMetadata kBathroomDoor{
			TransformPolicy::kAnimatedNodeThenPhysicsThenReference,
			"B_DOOR",
			{ 22.5458984f, 6.7092323f, -0.01838684f }
		};

		constexpr LocalPane MakeFlatYPane(
			Float3 center,
			float halfWidth,
			float halfHeight,
			Float3 boundsCenter,
			float boundsHalfThickness,
			float boundsHalfWidth,
			float boundsHalfHeight) noexcept
		{
			return {
				center,
				{ 0.0f, 1.0f, 0.0f },
				{ 1.0f, 0.0f, 0.0f },
				{ 0.0f, 0.0f, -1.0f },
				halfWidth,
				halfHeight,
				{ boundsCenter, boundsHalfThickness, boundsHalfWidth, boundsHalfHeight }
			};
		}

		constexpr LocalPane kMirror01Pane =
			MakeFlatYPane(
				{ 0.0f, -0.00118494f, 0.0f }, 21.125f, 31.21875f,
				{ 0.0f, -0.96396923f, 0.0f }, 0.96278858f, 21.125f, 31.21875f);
		constexpr LocalPane kMirror01aPane =
			MakeFlatYPane(
				{ 0.0f, -0.000422001f, 0.0f }, 21.125f, 31.21875f,
				{ 0.0f, -0.96309948f, 0.0f }, 0.96268177f, 21.125f, 31.21875f);
		constexpr LocalPane kMirror02Pane =
			MakeFlatYPane(
				{ -0.0078125f, -0.000014305f, -0.0078125f }, 18.0078125f, 18.4296875f,
				{ -0.0078125f, -0.76856184f, -0.0078125f }, 0.768547535f, 18.0078125f, 18.4296875f);
		constexpr LocalPane kMirror03Pane =
			MakeFlatYPane(
				{ 0.0f, 0.000484467f, 0.0f }, 11.734375f, 18.40625f,
				{ 0.0f, -0.768312454f, 0.0f }, 0.768796921f, 11.734375f, 18.40625f);

		constexpr LocalPane kMirror03aPane =
			MakeFlatYPane(
				{ 0.0f, -0.042451859f, 0.0f }, 14.6796875f, 21.109375f,
				{ 0.0f, -0.042451859f, 0.0f }, 0.042936325f, 14.6796875f, 21.109375f);
		constexpr LocalPane kMirror04Pane =
			MakeFlatYPane(
				{ 0.0f, 0.323486328f, 0.0f }, 8.7578125f, 16.6875f,
				{ 0.0f, -0.367553711f, 0.0f }, 0.691040039f, 8.7578125f, 16.6875f);
		constexpr LocalPane kMirror04aPane =
			MakeFlatYPane(
				{ 0.0f, -0.000009179f, 0.0f }, 16.796875f, 16.6875f,
				{ 0.0f, -0.690922558f, 0.0f }, 0.690913379f, 16.796875f, 16.6875f);

		constexpr LocalPane kBathroomCabinetPane{
			{ -0.62109375f, -9.0078125f, 0.0078125f },
			{ 0.0f, -1.0f, 0.0f },
			{ 1.0f, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f },
			15.73828125f,
			21.1484375f,
			{ { -0.62109375f, -8.62109375f, 0.0078125f }, 0.38671875f, 15.73828125f, 21.1484375f }
		};

		constexpr LocalPane kBathroomSinkPane{
			{ 32.0788f, -29.3184f, 136.3333f },
			{ 0.74246f, -0.50792f, 0.43677f },
			{ -0.17785f, -0.77806f, -0.60248f },
			{ 0.64585f, 0.36964f, -0.66802f },
			8.60f,
			8.60f,
			{ { 31.42939498f, -28.84637224f, 135.95196833f },
				1.0f, 8.60f, 8.60f }
		};

		constexpr LocalPane kBathroomShaveDrawerPane{
			{ -0.29296875f, -4.969721686f, 18.777342907f },
			{ 0.0f, -0.985398f, 0.170265f },
			{ 1.0f, 0.0f, 0.0f },
			{ 0.0f, 0.170265052f, 0.985398301f },
			10.92578125f,
			6.853985731f,
			{ { -0.29296875f, -4.82332218f, 18.76616216f },
				0.14617254f, 11.14453125f, 7.00106226f }
		};

		constexpr LocalPane kDLC04OperatorPane =
			MakeFlatYPane(
				{ 0.0f, 0.000484467f, 43.859375f }, 11.734375f, 62.265625f,
				{ 0.0f, -0.768312454f, 43.859375f }, 0.768796921f, 11.734375f, 62.265625f);

		constexpr BaseFormKey Form(std::string_view plugin, std::uint32_t localFormID) noexcept
		{
			return { plugin, localFormID };
		}

		constexpr std::array<BaseFormKey, kMaximumBaseForms> Forms(
			BaseFormKey first,
			BaseFormKey second = {},
			BaseFormKey third = {}) noexcept
		{
			return { first, second, third };
		}

		constexpr LocalPane MakeWorkshopWallPane(float depth, float halfWidth, float halfHeight, float scale = 1.0f) noexcept
		{
			const Float3 center{ 0.0f, depth * scale, 0.0f };
			return { center, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
				halfWidth * scale, halfHeight * scale,
				{ center, 0.4f * scale, halfWidth * scale, halfHeight * scale } };
		}

		constexpr LocalPane WorkshopCabinetPane(float scale = 1.0f) noexcept
		{
			return MakeWorkshopWallPane(-0.7734375f, 15.73828125f, 42.296875f, scale);
		}

		constexpr LocalPane WorkshopRoundPane(float scale = 1.0f) noexcept
		{
			return MakeWorkshopWallPane(-3.636843504f, 17.2f, 17.2f, scale);
		}

		constexpr std::array<Definition, kDefinitionCount> kDefinitions{
			Definition{
				DefinitionId::kMirror01, Family::kMirror01, "Mirror01",
				R"(Props\Mirrors\Mirror01.nif)", kMirror01Pane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror01Shards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kFallout4, 0x0014BF74)), 1 },
			Definition{
				DefinitionId::kMirror01a, Family::kMirror01, "Mirror01a",
				R"(Props\Mirrors\Mirror01a.nif)", kMirror01aPane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror01aShards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kFallout4, 0x0014BF8F)), 1 },
			Definition{
				DefinitionId::kMirror02, Family::kMirror02, "Mirror02",
				R"(Props\Mirrors\Mirror02.nif)", kMirror02Pane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror02Shards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kFallout4, 0x0014BF75)), 1 },
			Definition{
				DefinitionId::kMirror02a, Family::kMirror02, "Mirror02a",
				R"(Props\Mirrors\Mirror02a.nif)", kMirror02Pane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror02aShards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kFallout4, 0x0014BF91)), 1 },
			Definition{
				DefinitionId::kMirror03, Family::kMirror03, "Mirror03",
				R"(Props\Mirrors\Mirror03.nif)", kMirror03Pane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror03Shards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kFallout4, 0x0014BF76)), 1 },
			Definition{
				DefinitionId::kMirror03a, Family::kMirror03, "Mirror03a",
				R"(Props\Mirrors\Mirror03a.nif)", kMirror03aPane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror03aShards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kFallout4, 0x0014BF92)), 1 },
			Definition{
				DefinitionId::kMirror04, Family::kMirror04, "Mirror04",
				R"(Props\Mirrors\Mirror04.nif)", kMirror04Pane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror04, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kFallout4, 0x00152075)), 1 },
			Definition{
				DefinitionId::kMirror04a, Family::kMirror04, "Mirror04a",
				R"(Props\Mirrors\Mirror04a.nif)", kMirror04aPane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror04a, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kFallout4, 0x0015207F)), 1 },
			Definition{
				DefinitionId::kPlayerHouseBathroomMirror02, Family::kPlayerHouseBathroomCabinet,
				"PlayerHouse_BathroomMirror02",
				R"(SetDressing\PlayerHouse\PlayerHouse_BathroomMirror02.nif)", kBathroomCabinetPane,
				MaskKind::kRectangle, AuthoredMask::kNone, kBathroomDoor,
				CaptureClass::kHero4096, 1,
				Forms(Form(kFallout4, 0x0002044A), Form(kFallout4, 0x001AD2DF)), 2,
				VisualTwinPreference::kFallbackToPreferredTwin },
			Definition{
				DefinitionId::kPlayerHouseBathroomMirrorNoCol, Family::kPlayerHouseBathroomCabinet,
				"PlayerHouse_BathroomMirrorNoCol",
				R"(SetDressing\PlayerHouse\PlayerHouse_BathroomMirrorNoCol.nif)", kBathroomCabinetPane,
				MaskKind::kRectangle, AuthoredMask::kNone, kBathroomDoor,
				CaptureClass::kHero4096, 1, Forms(Form(kFallout4, 0x00247E89)), 1,
				VisualTwinPreference::kPreferOverTwin },
			Definition{
				DefinitionId::kPlayerHouseBathroomSink01, Family::kPlayerHouseBathroomSink,
				"PlayerHouse_BathroomSink01",
				R"(SetDressing\PlayerHouse\PlayerHouse_BathroomSink01.nif)", kBathroomSinkPane,
				MaskKind::kEllipse, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kFallout4, 0x0002044B)), 1,
				VisualTwinPreference::kFallbackToPreferredTwin, 512 },
			Definition{
				DefinitionId::kPlayerHouseBathroomSinkNoCol, Family::kPlayerHouseBathroomSink,
				"PlayerHouse_BathroomSink_NoCol",
				R"(SetDressing\PlayerHouse\PlayerHouse_BathroomSink_NoCol.nif)", kBathroomSinkPane,
				MaskKind::kEllipse, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kFallout4, 0x00247E8A)), 1,
				VisualTwinPreference::kPreferOverTwin, 512 },
			Definition{
				DefinitionId::kPlayerHouseBathroomShaveDrawer01, Family::kPlayerHouseBathroomShaveDrawer,
				"PlayerHouse_BathroomShaveDrawer01",
				R"(SetDressing\PlayerHouse\PlayerHouse_BathroomShaveDrawer01.nif)", kBathroomShaveDrawerPane,
				MaskKind::kRectangle, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kFallout4, 0x00050AA9)), 1 },
			Definition{
				DefinitionId::kPlayerHouseRuinBathroomMirror02, Family::kPlayerHouseRuinBathroomCabinet,
				"PlayerHouse_Ruin_BathroomMirror02",
				R"(SetDressing\PlayerHouse_Ruin\PlayerHouse_Ruin_BathroomMirror02.nif)", kBathroomCabinetPane,
				MaskKind::kRectangle, AuthoredMask::kNone, kBathroomDoor,
				CaptureClass::kStandard1024, 1,
				Forms(Form(kFallout4, 0x00091431), Form(kFallout4, 0x000BB844), Form(kFallout4, 0x0018C552)), 3 },
			Definition{
				DefinitionId::kPlayerHouseRuinBathroomSink01, Family::kPlayerHouseRuinBathroomSink,
				"PlayerHouse_Ruin_BathroomSink01",
				R"(SetDressing\PlayerHouse_Ruin\PlayerHouse_Ruin_BathroomSink01.nif)", kBathroomSinkPane,
				MaskKind::kEllipse, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kFallout4, 0x0009192F)), 1,
				VisualTwinPreference::kNoPreference, 512 },
			Definition{
				DefinitionId::kDLC04MirrorOperator01, Family::kDLC04Operator, "DLC04MirrorOperator01",
				R"(DLC04\SetDressing\Mirrors\DLC04MirrorOperator01.nif)", kDLC04OperatorPane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kDLC04Operator01Shards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kNukaWorld, 0x0004D9A5)), 1 },
			Definition{
				DefinitionId::kDLC04MirrorOperator01b, Family::kDLC04Operator, "DLC04MirrorOperator01b",
				R"(DLC04\SetDressing\Mirrors\DLC04MirrorOperator01b.nif)", kDLC04OperatorPane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kDLC04Operator01Shards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kNukaWorld, 0x0004D9A8)), 1 },
			Definition{
				DefinitionId::kDLC04MirrorOperator02, Family::kDLC04Operator, "DLC04MirrorOperator02",
				R"(DLC04\SetDressing\Mirrors\DLC04MirrorOperator02.nif)", kDLC04OperatorPane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kDLC04Operator02Shards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kNukaWorld, 0x0004D9A6)), 1 },
			Definition{
				DefinitionId::kDLC04MirrorOperator02B, Family::kDLC04Operator, "DLC04MirrorOperator02B",
				R"(DLC04\SetDressing\Mirrors\DLC04MirrorOperator02B.nif)", kDLC04OperatorPane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kDLC04Operator02Shards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form(kNukaWorld, 0x0004D9A7)), 1 },
			Definition{
				DefinitionId::kWorkshopCabinetWall, Family::kWorkshopCabinet, "Workshop CabinetWall",
				R"(MirrorsOfFallout\Workshop\CabinetWall.nif)",
				WorkshopCabinetPane(),
				MaskKind::kRectangle, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000800)), 1 },
			Definition{
				DefinitionId::kWorkshopCabinetStanding, Family::kWorkshopCabinet, "Workshop CabinetStanding",
				R"(MirrorsOfFallout\Workshop\CabinetStanding.nif)",
				{ { 0.000000000f, 0.000000000f, 118.000000000f }, { 0.000000000f, -1.000000000f, 0.000000000f }, { 1.000000000f, 0.000000000f, 0.000000000f }, { 0.000000000f, 0.000000000f, 1.000000000f }, 15.738281250f, 21.148437500f, { { 0.000000000f, 0.000000000f, 118.000000000f }, 0.4f, 15.738281250f, 21.148437500f } },
				MaskKind::kRectangle, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000801)), 1 },
			Definition{
				DefinitionId::kWorkshopRoundWall, Family::kWorkshopRound, "Workshop RoundWall",
				R"(MirrorsOfFallout\Workshop\RoundWall.nif)",
				WorkshopRoundPane(),
				MaskKind::kEllipse, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000802)), 1 },
			Definition{
				DefinitionId::kWorkshopRoundStanding, Family::kWorkshopRound, "Workshop RoundStanding",
				R"(MirrorsOfFallout\Workshop\RoundStanding.nif)",
				{ { 0.000000000f, 0.000000000f, 118.000000000f }, { 0.000000000f, -1.000000000f, 0.000000000f }, { 1.000000000f, 0.000000000f, 0.000000000f }, { 0.000000000f, 0.000000000f, 1.000000000f }, 8.600000000f, 8.600000000f, { { 0.000000000f, 0.000000000f, 118.000000000f }, 0.4f, 8.600000000f, 8.600000000f } },
				MaskKind::kEllipse, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000803)), 1 },
			Definition{
				DefinitionId::kWorkshopVanityWall, Family::kWorkshopVanity, "Workshop VanityWall",
				R"(MirrorsOfFallout\Workshop\VanityWall.nif)",
				{ { 0.000000000f, -1.709558658f, 0.000000000f }, { 0.000000000f, -1.000000000f, 0.000000000f }, { 1.000000000f, 0.000000000f, 0.000000000f }, { 0.000000000f, 0.000000000f, 1.000000000f }, 10.925781250f, 6.853985731f, { { 0.000000000f, -1.709558658f, 0.000000000f }, 0.4f, 10.925781250f, 6.853985731f } },
				MaskKind::kRectangle, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000804)), 1 },
			Definition{
				DefinitionId::kWorkshopVanityStanding, Family::kWorkshopVanity, "Workshop VanityStanding",
				R"(MirrorsOfFallout\Workshop\VanityStanding.nif)",
				{ { 0.000000000f, 0.000000000f, 118.000000000f }, { 0.000000000f, -1.000000000f, 0.000000000f }, { 1.000000000f, 0.000000000f, 0.000000000f }, { 0.000000000f, 0.000000000f, 1.000000000f }, 10.925781250f, 6.853985731f, { { 0.000000000f, 0.000000000f, 118.000000000f }, 0.4f, 10.925781250f, 6.853985731f } },
				MaskKind::kRectangle, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000805)), 1 },
			Definition{
				DefinitionId::kWorkshopCabinetWallMedium, Family::kWorkshopCabinet, "Workshop CabinetWall Medium",
				R"(MirrorsOfFallout\Workshop\CabinetWallMedium.nif)", WorkshopCabinetPane(1.5f),
				MaskKind::kRectangle, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000806)), 1 },
			Definition{
				DefinitionId::kWorkshopCabinetWallLarge, Family::kWorkshopCabinet, "Workshop CabinetWall Large",
				R"(MirrorsOfFallout\Workshop\CabinetWallLarge.nif)", WorkshopCabinetPane(2.0f),
				MaskKind::kRectangle, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000807)), 1 },
			Definition{
				DefinitionId::kWorkshopRoundWallMedium, Family::kWorkshopRound, "Workshop RoundWall Medium",
				R"(MirrorsOfFallout\Workshop\RoundWallMedium.nif)", WorkshopRoundPane(1.5f),
				MaskKind::kEllipse, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000808)), 1 },
			Definition{
				DefinitionId::kWorkshopRoundWallLarge, Family::kWorkshopRound, "Workshop RoundWall Large",
				R"(MirrorsOfFallout\Workshop\RoundWallLarge.nif)", WorkshopRoundPane(2.0f),
				MaskKind::kEllipse, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000809)), 1 },
			Definition{
				DefinitionId::kWorkshopCabinetUnitWall, Family::kWorkshopCabinet, "Workshop Bathroom Cupboard",
				R"(MirrorsOfFallout\Workshop\CabinetUnitWall.nif)",
				MakeWorkshopWallPane(-8.794799805f, 15.73828125f, 21.1484375f),
				MaskKind::kRectangle, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x0000080A)), 1 },
			Definition{
				DefinitionId::kWorkshopRoundMetalWall, Family::kWorkshopRound, "Workshop Round Metal Assembly",
				R"(MirrorsOfFallout\Workshop\RoundMetalWall.nif)",
				{ { -6.9837f, -51.740275f, 19.67705f },
					{ 0.742460887f, -0.507920607f, 0.436770522f },
					{ -0.177851274f, -0.778063566f, -0.602483205f },
					{ 0.645848864f, 0.369640021f, -0.668016092f }, 8.6f, 8.6f,
					{ { -6.9837f, -51.740275f, 19.67705f }, 0.4f, 8.6f, 8.6f } },
				MaskKind::kEllipse, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x0000080B)), 1 },
			Definition{
				DefinitionId::kWorkshopCabinetWallVeryLarge, Family::kWorkshopCabinet, "Workshop CabinetWall Very Large",
				R"(MirrorsOfFallout\Workshop\CabinetWallVeryLarge.nif)", WorkshopCabinetPane(4.0f),
				MaskKind::kRectangle, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x0000080C)), 1 },
			Definition{
				DefinitionId::kWorkshopCabinetWallGigantic, Family::kWorkshopCabinet, "Workshop CabinetWall Gigantic",
				R"(MirrorsOfFallout\Workshop\CabinetWallGigantic.nif)",
				MakeWorkshopWallPane(-0.7734375f, 42.296875f, 15.73828125f, 8.0f),
				MaskKind::kRectangle, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x0000080D)), 1 },
			Definition{
				DefinitionId::kWorkshopRoundWallVeryLarge, Family::kWorkshopRound, "Workshop RoundWall Very Large",
				R"(MirrorsOfFallout\Workshop\RoundWallVeryLarge.nif)", WorkshopRoundPane(4.0f),
				MaskKind::kEllipse, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x0000080E)), 1 },
			Definition{
				DefinitionId::kWorkshopRoundWallGigantic, Family::kWorkshopRound, "Workshop RoundWall Gigantic",
				R"(MirrorsOfFallout\Workshop\RoundWallGigantic.nif)", WorkshopRoundPane(8.0f),
				MaskKind::kEllipse, AuthoredMask::kNone, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x0000080F)), 1 },

			Definition{
				DefinitionId::kWorkshopCrackedMirror01, Family::kWorkshopCracked, "Workshop Cracked Mirror01",
				R"(MirrorsOfFallout\Workshop\Cracked\Mirror01.nif)", kMirror01Pane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror01Shards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000810)), 1 },
			Definition{
				DefinitionId::kWorkshopCrackedMirror01a, Family::kWorkshopCracked, "Workshop Cracked Mirror01a",
				R"(MirrorsOfFallout\Workshop\Cracked\Mirror01a.nif)", kMirror01aPane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror01aShards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000811)), 1 },
			Definition{
				DefinitionId::kWorkshopCrackedMirror02, Family::kWorkshopCracked, "Workshop Cracked Mirror02",
				R"(MirrorsOfFallout\Workshop\Cracked\Mirror02.nif)", kMirror02Pane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror02Shards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000812)), 1 },
			Definition{
				DefinitionId::kWorkshopCrackedMirror02a, Family::kWorkshopCracked, "Workshop Cracked Mirror02a",
				R"(MirrorsOfFallout\Workshop\Cracked\Mirror02a.nif)", kMirror02Pane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror02aShards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000813)), 1 },
			Definition{
				DefinitionId::kWorkshopCrackedMirror03, Family::kWorkshopCracked, "Workshop Cracked Mirror03",
				R"(MirrorsOfFallout\Workshop\Cracked\Mirror03.nif)", kMirror03Pane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror03Shards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000814)), 1 },
			Definition{
				DefinitionId::kWorkshopCrackedMirror03a, Family::kWorkshopCracked, "Workshop Cracked Mirror03a",
				R"(MirrorsOfFallout\Workshop\Cracked\Mirror03a.nif)", kMirror03aPane,
				MaskKind::kAuthoredTriangles, AuthoredMask::kMirror03aShards, kReferencePlacement,
				CaptureClass::kStandard1024, 1, Forms(Form("Realistic Reflections - Mirrors.esm", 0x00000815)), 1 }
		};

		constexpr bool DefinitionsAreSelfConsistent() noexcept
		{
			for (std::size_t index = 0; index < kDefinitions.size(); ++index) {
				const auto& definition = kDefinitions[index];
				if (static_cast<std::size_t>(definition.id) != index ||
					definition.name.empty() || definition.modelPathSuffix.empty() ||
					definition.modelPathSuffix.find('\\') == std::string_view::npos ||
					definition.pane.halfWidth <= 0.0f || definition.pane.halfHeight <= 0.0f ||
					definition.pane.bounds.halfThickness < 0.0f ||
					definition.pane.bounds.halfWidth <= 0.0f || definition.pane.bounds.halfHeight <= 0.0f ||
					definition.refreshIntervalFrames == 0 || definition.baseFormCount > kMaximumBaseForms)
					return false;
				if ((definition.mask == MaskKind::kAuthoredTriangles) !=
					(definition.authoredMask != AuthoredMask::kNone))
					return false;
				if ((definition.transform.policy == TransformPolicy::kAnimatedNodeThenPhysicsThenReference) !=
					!definition.transform.animatedNode.empty())
					return false;
			}
			return true;
		}
		static_assert(DefinitionsAreSelfConsistent());

		constexpr char FoldPathCharacter(char character) noexcept
		{
			if (character == '/')
				return '\\';
			if (character >= 'A' && character <= 'Z')
				return static_cast<char>(character + ('a' - 'A'));
			return character;
		}

		constexpr bool IsPathSeparator(char character) noexcept
		{
			return character == '/' || character == '\\';
		}
	}

	std::span<const Definition> Definitions() noexcept
	{
		return kDefinitions;
	}

	const Definition* Get(DefinitionId id) noexcept
	{
		if (id == DefinitionId::kConcordSpeakeasyFrame) {

			static constexpr Definition framed{
				.id = DefinitionId::kConcordSpeakeasyFrame, .family = Family::kMirror04,
				.name = "Concord Speakeasy framed mirror",
				.modelPathSuffix = R"(SetDressing\ConcMuseum\PictureFrame02.nif)",
				.pane = { {-0.2109375f, -1.50f, -0.0859375f}, {0,-1,0}, {1,0,0}, {0,0,1},
					28.3f, 28.4f, {{-0.2109375f,-1.50f,-0.0859375f}, 0.25f, 28.3f, 28.4f} }
			};
			return &framed;
		}
		if (id == DefinitionId::kAuthoring) {
			static constexpr Definition authoring{ .id = DefinitionId::kAuthoring,
				.family = Family::kAuthoring, .name = "CK Mirror Surface" };
			return &authoring;
		}
		const auto index = static_cast<std::size_t>(id);
		return index < kDefinitions.size() ? &kDefinitions[index] : nullptr;
	}

	bool MatchesModelPathSuffix(std::string_view modelPath, std::string_view canonicalSuffix) noexcept
	{
		if (canonicalSuffix.empty() || canonicalSuffix.size() > modelPath.size())
			return false;

		const std::size_t start = modelPath.size() - canonicalSuffix.size();
		if (start != 0 && !IsPathSeparator(modelPath[start - 1]))
			return false;

		for (std::size_t index = 0; index < canonicalSuffix.size(); ++index) {
			if (FoldPathCharacter(modelPath[start + index]) != FoldPathCharacter(canonicalSuffix[index]))
				return false;
		}
		return true;
	}

	const Definition* Find(std::string_view modelPath) noexcept
	{
		for (const auto& definition : kDefinitions) {
			if (MatchesModelPathSuffix(modelPath, definition.modelPathSuffix))
				return &definition;
		}
		return nullptr;
	}

	const Definition* FindForReference(std::string_view modelPath, std::uint32_t formID) noexcept
	{

		if (formID == 0x001BBE04u) return nullptr;  
		if (formID == 0x001BBE1Cu) {
			const auto* framed = Get(DefinitionId::kConcordSpeakeasyFrame);
			return MatchesModelPathSuffix(modelPath, framed->modelPathSuffix) ? framed : nullptr;
		}
		return Find(modelPath);
	}

	bool ReferenceScoped(std::uint32_t formID) noexcept
	{
		return formID == 0x001BBE04u || formID == 0x001BBE1Cu;
	}

	bool MatchesModelFileName(std::string_view modelPath, std::string_view canonicalSuffix) noexcept
	{
		const auto fileName = [](std::string_view path) noexcept {
			std::size_t start = path.size();
			while (start > 0 && !IsPathSeparator(path[start - 1]))
				--start;
			return path.substr(start);
		};
		const auto file = fileName(modelPath);
		const auto canonical = fileName(canonicalSuffix);
		if (canonical.empty() || file.size() != canonical.size())
			return false;
		for (std::size_t index = 0; index < file.size(); ++index) {
			if (FoldPathCharacter(file[index]) != FoldPathCharacter(canonical[index]))
				return false;
		}
		return true;
	}

	bool DescribesGameAsset(const Definition& definition) noexcept
	{
		if (definition.baseFormCount == 0 || definition.modelPathSuffix.empty())
			return false;
		for (std::uint8_t index = 0; index < definition.baseFormCount; ++index) {
			if (definition.baseForms[index].plugin == "Realistic Reflections - Mirrors.esm")
				return false;
		}
		return true;
	}

	bool SameSurface(const Definition& first, const Definition& second) noexcept
	{
		const auto same = [](const Float3& a, const Float3& b) noexcept {
			return a.x == b.x && a.y == b.y && a.z == b.z;
		};
		const auto& a = first.pane;
		const auto& b = second.pane;
		return same(a.center, b.center) && same(a.normal, b.normal) && same(a.tangent, b.tangent) &&
			same(a.bitangent, b.bitangent) && a.halfWidth == b.halfWidth && a.halfHeight == b.halfHeight &&
			same(a.bounds.center, b.bounds.center) && a.bounds.halfThickness == b.bounds.halfThickness &&
			a.bounds.halfWidth == b.bounds.halfWidth && a.bounds.halfHeight == b.bounds.halfHeight &&
			first.mask == second.mask && first.authoredMask == second.authoredMask &&
			first.transform.policy == second.transform.policy &&
			first.transform.animatedNode == second.transform.animatedNode &&
			same(first.transform.inverseBindTranslation, second.transform.inverseBindTranslation);
	}
}
