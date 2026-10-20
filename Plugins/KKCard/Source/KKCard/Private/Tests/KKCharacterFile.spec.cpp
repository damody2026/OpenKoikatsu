#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "KKCardReader.h"
#include "KKCardWriter.h"
#include "KKTestFixtures.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKKCharacterFileLoadTest,
	"OpenKoikatsu.KKCard.LoadCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKCharacterFileLoadTest::RunTest(const FString& Parameters)
{
	FString EnsureErr;
	TestTrue(TEXT("fixture"), EnsureMinimalKksCard(EnsureErr));
	const FString Path = KKTestCardPath();

	FKKCharacterFile File;
	FString Err;
	const bool bOk = UKKCardReader::LoadFromFile(Path, File, Err);
	TestTrue(FString::Printf(TEXT("load: %s"), *Err), bOk);
	TestEqual(TEXT("lastname"), File.LastName, FString(TEXT("Test")));
	TestEqual(TEXT("firstname"), File.FirstName, FString(TEXT("Card")));
	TestEqual(TEXT("nickname"), File.Nickname, FString(TEXT("nick")));
	TestEqual(TEXT("headId"), File.Face.HeadId, 1);
	TestEqual(TEXT("shapeCount"), File.Face.ShapeValueFace.Num(), 52);
	TestTrue(TEXT("shape0"), FMath::IsNearlyEqual(File.Face.ShapeValueFace[0], 0.5f, 0.001f));
	TestEqual(TEXT("hairBack"), File.Hair.Back.Id, 11);
	TestEqual(TEXT("resolveCount"), File.Resolves.Num(), 1);
	if (File.Resolves.Num() > 0)
	{
		TestEqual(TEXT("modId"), File.Resolves[0].ModID, FString(TEXT("HeadMod")));
		TestEqual(TEXT("slot"), File.Resolves[0].Slot, 1);
		TestEqual(TEXT("property"), File.Resolves[0].Property, FString(TEXT("ChaFileFace.headId")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKKRealCardCustomTest,
	"OpenKoikatsu.KKCard.RealCardCustom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKRealCardCustomTest::RunTest(const FString& Parameters)
{
	const FString Path = TEXT("C:/portable/KoikatuSunshine/UserData/chara/female/KK_091207.png");
	if (!FPaths::FileExists(Path))
	{
		AddWarning(TEXT("skip: real card not present"));
		return true;
	}
	FKKCharacterFile File;
	FString Err;
	TestTrue(FString::Printf(TEXT("load: %s"), *Err), UKKCardReader::LoadFromFile(Path, File, Err));
	TestTrue(TEXT("shape face"), File.Face.ShapeValueFace.Num() >= 52);
	TestTrue(TEXT("shape body"), File.Body.ShapeValueBody.Num() >= 32);
	TestTrue(TEXT("outfit0 clothes"), File.Outfits.Num() > 0 && File.Outfits[0].Clothes.Num() > 0);
	const float Face0 = File.Face.ShapeValueFace[0];
	File.Face.ShapeValueFace[0] = Face0 + 0.1f;
	const FString OutPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("EditedCards/shape_roundtrip.png"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPath), true);
	TestTrue(TEXT("save"), UKKCardWriter::SaveWithEdits(File, OutPath, Err));
	FKKCharacterFile Reloaded;
	TestTrue(TEXT("reload"), UKKCardReader::LoadFromFile(OutPath, Reloaded, Err));
	TestTrue(TEXT("shape wrote"), Reloaded.Face.ShapeValueFace.Num() >= 52
		&& FMath::IsNearlyEqual(Reloaded.Face.ShapeValueFace[0], Face0 + 0.1f, 0.01f));
	return true;
}

#endif
