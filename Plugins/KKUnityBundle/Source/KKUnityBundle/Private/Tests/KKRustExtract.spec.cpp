#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "KKRustExtract.h"
#include "ok_extract.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKKRustExtractAbiTest,
	"OpenKoikatsu.KKUnityBundle.RustExtractAbi",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKRustExtractAbiTest::RunTest(const FString& Parameters)
{
	char Err[256];
	Err[0] = 0;
	const int32 Rc = ok_extract_card(nullptr, nullptr, nullptr, Err, 256);
	TestEqual(TEXT("null args rc"), Rc, 1);
	TestTrue(TEXT("null args error"), Err[0] != 0);

	OkUnrealBindings Unreal;
	FMemory::Memzero(Unreal);
	OkRustBindings Rust;
	FMemory::Memzero(Rust);
	TestEqual(TEXT("register"), ok_register_bindings(Unreal, &Rust), 0);
	TestTrue(TEXT("session_open fn"), Rust.session_open != nullptr);
	TestTrue(TEXT("apply_anim_frame fn"), Rust.apply_anim_frame != nullptr);
	if (Rust.apply_anim_frame)
	{
		TestEqual(TEXT("frame null session rc"), Rust.apply_anim_frame(nullptr, 0.f, 1.f / 60.f), 1);
	}

	FOkSession Sess;
	FString OpenErr;
	TestFalse(TEXT("open missing game"), Sess.Open(TEXT("C:/portable/OpenKoikatsu/Saved/no_such_game"), FString(), 0, OpenErr));
	TestTrue(TEXT("open error text"), !OpenErr.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKKRustExtractVanillaTest,
	"OpenKoikatsu.KKUnityBundle.RustExtractVanilla",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKRustExtractVanillaTest::RunTest(const FString& Parameters)
{
	const FString GameRoot = TEXT("C:/portable/KoikatuSunshine");
	if (!IFileManager::Get().DirectoryExists(*(GameRoot / TEXT("abdata"))))
	{
		AddWarning(TEXT("skip: GameRoot abdata not present"));
		return true;
	}

	const FString OutDir = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Converted/_ffi_vanilla"));
	IFileManager::Get().DeleteDirectory(*OutDir, false, true);

	FString Err;
	const bool bOk = OKExtractCard(GameRoot, FString(), OutDir, Err);
	TestTrue(FString::Printf(TEXT("extract: %s"), *Err), bOk);
	TestTrue(TEXT("body.obj"), FPaths::FileExists(OutDir / TEXT("body.obj")));
	TestTrue(TEXT("head.obj"), FPaths::FileExists(OutDir / TEXT("head.obj")));
	TestTrue(TEXT("body.png"), FPaths::FileExists(OutDir / TEXT("body.png")));
	TestTrue(TEXT("head.png"), FPaths::FileExists(OutDir / TEXT("head.png")));
	TestTrue(TEXT("manifest.json"), FPaths::FileExists(OutDir / TEXT("manifest.json")));

	const int64 BodySize = IFileManager::Get().FileSize(*(OutDir / TEXT("body.obj")));
	const int64 HeadSize = IFileManager::Get().FileSize(*(OutDir / TEXT("head.obj")));
	TestTrue(TEXT("body size"), BodySize > 10000);
	TestTrue(TEXT("head size"), HeadSize > 10000);

	FString Manifest;
	TestTrue(TEXT("read manifest"), FFileHelper::LoadFileToString(Manifest, *(OutDir / TEXT("manifest.json"))));
	TestTrue(TEXT("manifest body"), Manifest.Contains(TEXT("\"role\": \"body\"")));
	TestTrue(TEXT("manifest head"), Manifest.Contains(TEXT("\"role\": \"head\"")));

	FString BodyObj;
	TestTrue(TEXT("read body obj"), FFileHelper::LoadFileToString(BodyObj, *(OutDir / TEXT("body.obj"))));
	bool bNonZero = false;
	TArray<FString> Lines;
	BodyObj.ParseIntoArrayLines(Lines, false);
	for (const FString& Line : Lines)
	{
		if (!Line.StartsWith(TEXT("v ")))
		{
			continue;
		}
		TArray<FString> Parts;
		Line.ParseIntoArrayWS(Parts);
		if (Parts.Num() >= 4)
		{
			const float X = FCString::Atof(*Parts[1]);
			const float Y = FCString::Atof(*Parts[2]);
			const float Z = FCString::Atof(*Parts[3]);
			if (FMath::Abs(X) + FMath::Abs(Y) + FMath::Abs(Z) > 0.001f)
			{
				bNonZero = true;
				break;
			}
		}
	}
	TestTrue(TEXT("body vertices not all zero"), bNonZero);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKKRustExtractAnimationIdentityTest,
	"OpenKoikatsu.KKUnityBundle.AnimationClipIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKRustExtractAnimationIdentityTest::RunTest(const FString& Parameters)
{
	const FString GameRoot = TEXT("C:/portable/KoikatuSunshine");
	const FString Card = GameRoot / TEXT("UserData/chara/female/KK_384283.png");
	if (!FPaths::FileExists(Card))
	{
		AddWarning(TEXT("skip: K384 card unavailable"));
		return true;
	}
	FOkSession Session;
	FString Error;
	if (!TestTrue(TEXT("open animation identity fixture"), Session.Open(GameRoot, Card, 0, Error)))
	{
		AddError(Error);
		return false;
	}
	const FString Source = TEXT("studio_k/anime/04.unity3d");
	const TArray<FString> Labels = Session.ClipNamesForSource(Source);
	TestEqual(TEXT("all clip identities cross the C ABI"), Labels.Num(), 66);
	int32 StandingVariants = 0;
	for (const FString& Label : Labels)
	{
		if (!Label.StartsWith(TEXT("tachi_00 ["))) { continue; }
		++StandingVariants;
		TestTrue(*FString::Printf(TEXT("select %s"), *Label), Session.SetAnimClip(Source + TEXT("|") + Label));
		TestTrue(TEXT("sample selected clip"), Session.ApplyAnimFrame(0.35f, 0.f));
		TestTrue(TEXT("selected duration positive"), Session.AnimDuration() > 0.f);
	}
	TestEqual(TEXT("all three same-name standing clips selectable"), StandingVariants, 3);
	TestFalse(TEXT("ambiguous bare clip rejected"), Session.SetAnimClip(Source + TEXT("|tachi_00")));
	TestTrue(TEXT("select one-frame game clip"), Session.SetAnimClip(TEXT("action/animator/00.unity3d|f_twoshot_00_02")));
	TestTrue(TEXT("preserve authored one-frame duration"), FMath::IsNearlyEqual(Session.AnimDuration(), 1.f / 30.f, 0.00001f));
	const FString StateSource = TEXT("states:studio_k/anime/04.unity3d");
	TestEqual(TEXT("all explicit simple states cross the C ABI"), Session.ClipNamesForSource(StateSource).Num(), 66);
	for (const FString& Controller : { FString(TEXT("pose_04")), FString(TEXT("gravure_00")) })
	{
		TestTrue(TEXT("select exact controller state"), Session.SetAnimClip(StateSource + TEXT("|") + Controller + TEXT(" / Base Layer.tachi_00")));
		TestFalse(TEXT("non-looping state retains authored end hold"), Session.IsAnimLooping());
		TestTrue(TEXT("sample controller state past end"), Session.ApplyAnimFrame(1.f, 0.f));
	}
	TestTrue(TEXT("select serialized override controller state"), Session.SetAnimClip(
		TEXT("states:action/animator/00.unity3d|c33 / Base Layer.Main.Idle")));
	TestTrue(TEXT("overridden Idle is looping"), Session.IsAnimLooping());
	TestTrue(TEXT("state speed scales the override clip duration"),
		FMath::IsNearlyEqual(Session.AnimDuration(), (4.f / 3.f) / 0.7f, 0.00001f));
	TestTrue(TEXT("sample overridden state"), Session.ApplyAnimFrame(0.5f, 1.f / 60.f));
	TestTrue(TEXT("Studio component scene crosses C ABI"), Session.SetAnimSceneMode(4));
	TestTrue(TEXT("sample explicit Studio scene"), Session.ApplyAnimFrame(0.5f, 0.f));
	TestTrue(TEXT("restore automatic source scene"), Session.SetAnimSceneMode(0));
	TestTrue(TEXT("resample automatic gameplay scene"), Session.ApplyAnimFrame(0.5f, 0.f));
	TestFalse(TEXT("unknown scene rejected"), Session.SetAnimSceneMode(5));
	TestFalse(TEXT("blend tree is not replaced by its first leaf"), Session.SetAnimClip(
		TEXT("states:action/animator/00.unity3d|base / Base Layer.Main.Twoshot_00")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKKRustExtractRealCardTest,
	"OpenKoikatsu.KKUnityBundle.RustExtractRealCard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKRustExtractRealCardTest::RunTest(const FString& Parameters)
{
	const FString GameRoot = TEXT("C:/portable/KoikatuSunshine");
	const FString CardPath = TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/KK_091207.png");
	if (!FPaths::FileExists(CardPath) || !IFileManager::Get().DirectoryExists(*(GameRoot / TEXT("abdata"))))
	{
		AddWarning(TEXT("skip: real card or abdata not present"));
		return true;
	}
	const FString OutDir = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Converted/_user_check"));
	IFileManager::Get().DeleteDirectory(*OutDir, false, true);
	FString Err;
	const bool bOk = OKExtractCard(GameRoot, CardPath, OutDir, Err, 0);
	TestTrue(FString::Printf(TEXT("extract: %s"), *Err), bOk);
	TestTrue(TEXT("body.obj"), FPaths::FileExists(OutDir / TEXT("body.obj")));
	TestTrue(TEXT("head.obj"), FPaths::FileExists(OutDir / TEXT("head.obj")));
	TestTrue(TEXT("body.png"), FPaths::FileExists(OutDir / TEXT("body.png")));
	TestTrue(TEXT("head.png"), FPaths::FileExists(OutDir / TEXT("head.png")));
	FString Manifest;
	TestTrue(TEXT("manifest"), FFileHelper::LoadFileToString(Manifest, *(OutDir / TEXT("manifest.json"))));
	TestTrue(TEXT("clothes_top role"), Manifest.Contains(TEXT("clothes_top")));
	TArray<FString> Found;
	IFileManager::Get().FindFiles(Found, *(OutDir / TEXT("clothes_top*.obj")), true, false);
	TestTrue(TEXT("clothes_top obj"), Found.Num() > 0);
	return true;
}

#endif
