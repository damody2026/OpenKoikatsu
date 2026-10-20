#include "KKAnimationCatalog.h"
#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKImportedCatalogTest,
	"OpenKoikatsu.AnimationCatalog.ImportedData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKImportedCatalogTest::RunTest(const FString& Parameters)
{
	UKKAnimationCatalog* Catalog = UKKAnimationCatalog::LoadDefaultCatalog();
	if (!TestNotNull(TEXT("persistent imported catalog"), Catalog)) return false;
	TestEqual(TEXT("all tables"), Catalog->Tables.Num(), 8);
	TestEqual(TEXT("clips include duplicate asset names"), Catalog->GetRowCount(TEXT("animation_clips")), 29956);
	TestEqual(TEXT("Studio catalog"), Catalog->GetRowCount(TEXT("studio_catalog")), 9214);
	TestEqual(TEXT("pattern rows"), Catalog->GetRowCount(TEXT("fbs_patterns")), 2492);
	TestEqual(TEXT("channels"), Catalog->GetRowCount(TEXT("fbs_channels")), 3227);
	TestEqual(TEXT("headerless animation scan retains first row"), Catalog->GetRowCount(TEXT("mod_anim_bundles")), 79);
	TestEqual(TEXT("headerless Studio scan"), Catalog->GetRowCount(TEXT("mod_studio_info")), 4980);
	TestEqual(TEXT("headerless head scan"), Catalog->GetRowCount(TEXT("mod_head_prefabs")), 2);
	const TArray<FName> Patterns = Catalog->GetExpressionPatterns(TEXT("chara/bo_head_00.unity3d"),
		TEXT("p_cf_head_00"), TEXT("MouthCtrl"), 0);
	TestEqual(TEXT("base head's actual authored count"), Patterns.Num(), 43);
	if (Patterns.Num() > 0)
	{
		TestEqual(TEXT("default close slot"), Catalog->GetCell(TEXT("fbs_patterns"), Patterns[0], TEXT("close")), FString(TEXT("28")));
		TestEqual(TEXT("default open slot"), Catalog->GetCell(TEXT("fbs_patterns"), Patterns[0], TEXT("open")), FString(TEXT("29")));
	}
	TestEqual(TEXT("unrelated mod head cannot use base mapping"), Catalog->GetExpressionPatterns(
		TEXT("mods/unknown.zipmod!abdata/chara/bo_head_00.unity3d"), TEXT("p_cf_head_00"), TEXT("MouthCtrl"), 0).Num(), 0);
	const TArray<FString> Sources = Catalog->GetAnimationSources();
	TestTrue(TEXT("mod sources use root-relative exact ZIP identity"), Sources.Contains(
		TEXT("mods/Sideloader Modpack - Animations/kkstub.behind.zipmod!abdata/h/anim/female/custom_911_3.unity3d")));
	TestTrue(TEXT("male sources are retained"), Sources.Contains(TEXT("h/anim/male/01_01_00.unity3d")));
	const TArray<FName> WalkRows = Catalog->FindRows(TEXT("studio_catalog"), TEXT("clip"), TEXT("f_aruki_00_00_01"));
	if (!TestTrue(TEXT("Studio walk inventory"), WalkRows.Num() > 0)) return false;
	FString Key, Error;
	TestTrue(TEXT("exact controller selects state"), Catalog->ResolveStudioAnimation(WalkRows[0],
		{TEXT("wrong / [Studio layer 0] Base Layer.f_aruki_00_00_01"), TEXT("walk_run / [Studio layer 0] Base Layer.f_aruki_00_00_01"),
		 TEXT("walk_run / [Studio layer 1] Other.f_aruki_00_00_01")}, Key, Error));
	TestFalse(TEXT("two state paths with same leaf stay ambiguous"), Catalog->ResolveStudioAnimation(WalkRows[0],
		{TEXT("walk_run / [Studio layer 0] Base Layer.f_aruki_00_00_01"), TEXT("walk_run / [Studio layer 0] Base Layer.Sub.f_aruki_00_00_01")}, Key, Error));
	UKKAnimationCatalog* Scratch=NewObject<UKKAnimationCatalog>();FKKExportCatalogTable Table;
	Table.Name=TEXT("studio_catalog");Table.Columns={TEXT("kind"),TEXT("options")};Table.Data=NewObject<UDataTable>(Scratch);
	Table.Data->RowStruct=FKKExportCatalogRow::StaticStruct();Scratch->Tables.Add(Table);
	const TArray<FString> Integers={TEXT(""),TEXT("-1"),TEXT("0"),TEXT("79"),TEXT("+79"),TEXT(" 79 "),TEXT("79bad"),TEXT("1.0"),TEXT("2147483648"),TEXT("-2147483649")};
	const TArray<int32> Expected={0,0,0,79,79,79,0,0,0,0};
	for(int32 I=0;I<Integers.Num();++I) {
		FKKExportCatalogRow Entry;Entry.Values={TEXT("hanime"),Integers[I]+TEXT(",FALSE,FALSE, true ")};Table.Data->AddRow(TEXT("case"),Entry);
		bool IsH,IsMotion;int32 Layer;
		if(!TestTrue(TEXT("source TryParse semantics"),Scratch->StudioSettings(TEXT("case"),IsH,Layer,IsMotion))) return false;
		TestEqual(TEXT("TryParse result"),Layer,Expected[I]);TestTrue(TEXT("source Bool.Parse accepts whitespace"),IsMotion&&IsH);
	}
	for(const auto& Case:TArray<TPair<FString,int32>>{{TEXT(",FALSE,FALSE,TRUE, true ,garbage,TRUE,"),5},{TEXT(",FALSE,FALSE,TRUE"),0},{TEXT(",FALSE,FALSE,TRUE,TRUE,TRUE,TRUE,TRUE"),15}}) {
		FKKExportCatalogRow Entry;Entry.Values={TEXT("hanime"),Case.Key};Table.Data->AddRow(TEXT("case"),Entry);
		TestEqual(TEXT("PV Bool.TryParse missing/invalid semantics"),Scratch->StudioPVMask(TEXT("case")),Case.Value);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKImportedCatalogPlaybackTest,
	"OpenKoikatsu.AnimationCatalog.Playback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKImportedCatalogPlaybackTest::RunTest(const FString& Parameters)
{
	UKKAnimationCatalog* Catalog = UKKAnimationCatalog::LoadDefaultCatalog();
	if (!TestNotNull(TEXT("catalog"), Catalog)) return false;
	FOkSession Session; FString Error;
	if (!TestTrue(TEXT("real card loads"), Session.Open(TEXT("C:/portable/KoikatuSunshine"),
		TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/Alice_keiyuu.png"), 0, Error))) return false;
	const TArray<FName> Rows = Catalog->FindRows(TEXT("studio_catalog"), TEXT("clip"), TEXT("f_aruki_00_00_01"));
	if (Rows.IsEmpty()) return false;
	const FString Source = Catalog->StudioSource(Rows[0]);
	FString Key;
	if (!TestTrue(TEXT("Studio source resolves actual controller state"), Catalog->ResolveStudioAnimation(
		Rows[0], Session.ClipNamesForSource(Source), Key, Error))) { AddError(Error); return false; }
	TestTrue(TEXT("imported Studio motion plays"), Session.SetStudioAnimation(Key,false,0.f,0.f,false,0.f));
	TestTrue(TEXT("motion frame sampled"), Session.ApplyAnimFrame(0.4f, 1.f / 60.f));
	const FString ModSource = TEXT("mods/Sideloader Modpack - Animations/kkstub.behind.zipmod!abdata/h/anim/female/custom_911_3.unity3d");
	const TArray<FString> ModClips = Session.ClipNamesForSource(ModSource);
	if (!TestTrue(TEXT("imported mod source has actual clips"), ModClips.Num() > 0)) return false;
	TestTrue(TEXT("exact mod clip plays"), Session.SetAnimClip(ModSource + TEXT("|") + ModClips[0]));
	TestTrue(TEXT("mod frame sampled"), Session.ApplyAnimFrame(0.25f, 1.f / 60.f));
	UE_LOG(LogTemp, Display, TEXT("KK catalog playback Studio=%s mod=%s|%s"), *Key, *ModSource, *ModClips[0]);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKHandShapesPlaybackTest,
	"OpenKoikatsu.AnimationCatalog.HandShapes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKHandShapesPlaybackTest::RunTest(const FString& Parameters)
{
	FOkSession Session; FString Error;
	if (!TestTrue(TEXT("actual card session"), Session.Open(TEXT("C:/portable/KoikatuSunshine"),
		TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/Alice_keiyuu.png"), 0, Error))) return false;
	if (!TestEqual(TEXT("actual KKS hand key count"), Session.HandShapeCount(), 24)) return false;
	if (!Session.SetAnimClip(TEXT("states:studio/anime/00.unity3d|walk_run / Base Layer.f_aruki_00_00_01"))) return false;
	Session.SetAnimSceneMode(4);
	Session.ApplyAnimFrame(0.4f, 0.f);
	auto Capture = [&Session]()
	{
		TMap<FString, FQuat> Result;
		for (int32 I = 0; I < Session.SkelBoneCount(); ++I)
		{
			OkBoneView Bone;
			if (Session.SkelBoneAt(I, Bone) && Bone.layer == 1)
				Result.Add(UTF8_TO_TCHAR(Bone.name), FQuat(Bone.pose_rot[0], Bone.pose_rot[1], Bone.pose_rot[2], Bone.pose_rot[3]));
		}
		return Result;
	};
	const TMap<FString, FQuat> Baseline = Capture();
	TestTrue(TEXT("left override selection"), Session.SetHandShape(0, true, 1, 23, 0.3f));
	Session.ApplyAnimFrame(0.4f, 0.f);
	const TMap<FString, FQuat> Left = Capture();
	int32 Changed = 0, Right = 0;
	for (const auto& Bone : Baseline)
	{
		bool bFinger = false;
		for (const TCHAR* Stem : {TEXT("index"), TEXT("little"), TEXT("middle"), TEXT("ring"), TEXT("thumb")})
			for (int32 Joint = 1; Joint <= 3; ++Joint)
				for (const TCHAR* Side : {TEXT("L"), TEXT("R")})
					bFinger |= Bone.Key == FString::Printf(TEXT("cf_j_%s%02d_%s"), Stem, Joint, Side);
		const FQuat* Actual = Left.Find(Bone.Key);
		if (!TestNotNull(TEXT("same palette bone remains"), Actual)) return false;
		if (bFinger && Bone.Key.EndsWith(TEXT("_L"))) Changed += !Actual->Equals(Bone.Value, 1e-6);
		else TestTrue(TEXT("opposite hand and other Animator joints retained"), Actual->Equals(Bone.Value, 1e-6));
		if (bFinger && Bone.Key.EndsWith(TEXT("_R"))) ++Right;
	}
	TestTrue(TEXT("positive left finger change"), Changed > 0);
	TestEqual(TEXT("opposite hand coverage"), Right, 15);
	TestFalse(TEXT("invalid side"), Session.SetHandShape(2, true, 1, 0, 0.f));
	TestFalse(TEXT("invalid key"), Session.SetHandShape(0, true, 24, 0, 0.f));
	Session.ApplyAnimFrame(0.4f, 0.f);
	const auto AfterInvalid = Capture();
	for (const auto& Bone : Left) TestTrue(TEXT("failed selection preserves current hand"), AfterInvalid[Bone.Key].Equals(Bone.Value, 1e-6));
	TestTrue(TEXT("disable override"), Session.SetHandShape(0, false, 0, 0, 0.f));
	Session.ApplyAnimFrame(0.4f, 0.f);
	const auto Restored = Capture();
	for (const auto& Bone : Baseline) TestTrue(TEXT("disable restores sampled Animator frame"), Restored[Bone.Key].Equals(Bone.Value, 1e-6));
	return true;
}
#endif
