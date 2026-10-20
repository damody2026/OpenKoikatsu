#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "KKCardReader.h"
#include "KKCardWriter.h"
#include "KKTestFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FString TestCardPath()
	{
		FString Err;
		EnsureMinimalKksCard(Err);
		return KKTestCardPath();
	}

	FString TempOutPath()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("TestCards/roundtrip.png"));
	}

	FString HashBytes(const TArray<uint8>& Bytes)
	{
		return FMD5::HashBytes(Bytes.GetData(), Bytes.Num());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKKCardSaveUnchangedTest,
	"OpenKoikatsu.KKCard.SaveUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKCardSaveUnchangedTest::RunTest(const FString& Parameters)
{
	FKKCharacterFile File;
	FString Err;
	TestTrue(TEXT("load"), UKKCardReader::LoadFromFile(TestCardPath(), File, Err));
	const FString OutPath = TempOutPath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPath), true);
	TestTrue(TEXT("save"), UKKCardWriter::SaveUnchanged(File, OutPath, Err));
	TArray<uint8> A;
	TArray<uint8> B;
	FFileHelper::LoadFileToArray(A, *TestCardPath());
	FFileHelper::LoadFileToArray(B, *OutPath);
	TestEqual(TEXT("hash"), HashBytes(A), HashBytes(B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKKCardSaveNicknameTest,
	"OpenKoikatsu.KKCard.SaveNickname",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKCardSaveNicknameTest::RunTest(const FString& Parameters)
{
	FKKCharacterFile File;
	FString Err;
	TestTrue(TEXT("load"), UKKCardReader::LoadFromFile(TestCardPath(), File, Err));
	const TArray<uint8> CustomBefore = File.RawBlocks.FindRef(TEXT("Custom"));
	File.Nickname = TEXT("changed");
	const FString OutPath = TempOutPath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPath), true);
	TestTrue(TEXT("save edits"), UKKCardWriter::SaveWithEdits(File, OutPath, Err));

	FKKCharacterFile Reloaded;
	TestTrue(TEXT("reload"), UKKCardReader::LoadFromFile(OutPath, Reloaded, Err));
	TestEqual(TEXT("nickname"), Reloaded.Nickname, FString(TEXT("changed")));
	TestEqual(TEXT("lastname"), Reloaded.LastName, FString(TEXT("Test")));
	const TArray<uint8>* CustomAfter = Reloaded.RawBlocks.Find(TEXT("Custom"));
	TestTrue(TEXT("custom exists"), CustomAfter != nullptr);
	if (CustomAfter)
	{
		TestEqual(TEXT("custom bytes"), HashBytes(CustomBefore), HashBytes(*CustomAfter));
	}
	return true;
}

#endif
