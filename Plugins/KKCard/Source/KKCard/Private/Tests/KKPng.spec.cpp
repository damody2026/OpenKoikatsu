#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "KKPng.h"
#include "KKCardReader.h"
#include "KKTestFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKKPngIendTest,
	"OpenKoikatsu.KKCard.Iend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKPngIendTest::RunTest(const FString& Parameters)
{
	FString EnsureErr;
	TestTrue(TEXT("fixture"), EnsureMinimalKksCard(EnsureErr));
	const FString Path = KKTestCardPath();

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		AddError(FString::Printf(TEXT("missing test card: %s"), *Path));
		return false;
	}

	int32 End = -1;
	TestTrue(TEXT("IEND found"), FindPngIendEnd(Bytes, End));
	TestTrue(TEXT("payload after IEND"), End > 0 && End < Bytes.Num());

	FKKCardHeader Header;
	TArray<FKKBlockInfo> Blocks;
	FString Err;
	const bool bOk = ParseCardHeader(Bytes, Header, Blocks, Err);
	TestTrue(FString::Printf(TEXT("header parses: %s"), *Err), bOk);
	TestEqual(TEXT("kind"), Header.Kind, EKKCardKind::KKS);
	TestEqual(TEXT("product"), Header.ProductNo, 100);
	TestTrue(
		TEXT("has Custom"),
		Blocks.ContainsByPredicate([](const FKKBlockInfo& B) { return B.Name == TEXT("Custom"); }));
	TestTrue(
		TEXT("has Parameter"),
		Blocks.ContainsByPredicate([](const FKKBlockInfo& B) { return B.Name == TEXT("Parameter"); }));
	const ANSICHAR* Decoys = "KoiKatuClothes KoiKatuCharaSP sceneInfo";
	Bytes.Append(reinterpret_cast<const uint8*>(Decoys), FCStringAnsi::Strlen(Decoys));
	TestTrue(TEXT("embedded marker-like data does not invalidate header"), ParseCardHeader(Bytes, Header, Blocks, Err));
	TestEqual(TEXT("serialized KKS marker wins over embedded decoys"), Header.Kind, EKKCardKind::KKS);
	TestEqual(TEXT("classification uses the serialized marker"), ClassifyCard(Bytes), EKKCardKind::KKS);
	FMemoryReader Original(Bytes, true);
	Original.Seek(End + sizeof(int32));
	FString OriginalMarker, Version;
	TestTrue(TEXT("read fixture marker"), Read7BitString(Original, OriginalMarker));
	TestTrue(TEXT("read fixture version"), Read7BitString(Original, Version));
	const int32 Tail = static_cast<int32>(Original.Tell());
	const TArray<TPair<FString, EKKCardKind>> Kinds = {
		{TEXT("【KoiKatuCharaSun】"), EKKCardKind::KKS},
		{TEXT("【KoiKatuCharaSP】"), EKKCardKind::KKSP},
		{TEXT("【KoiKatuChara】"), EKKCardKind::KK},
		{TEXT("【KoiKatuClothes】"), EKKCardKind::Coordinate}
	};
	for (const auto& Kind : Kinds)
	{
		TArray<uint8> Reframed;
		Reframed.Append(Bytes.GetData(), End);
		WriteInt32LE(Reframed, Header.ProductNo);
		Write7BitString(Reframed, Kind.Key);
		Write7BitString(Reframed, Version);
		Reframed.Append(Bytes.GetData() + Tail, Bytes.Num() - Tail);
		TMap<FString, TArray<uint8>> Raw;
		TestTrue(TEXT("view parser accepts all supported markers with decoys"), ParseCardHeader(Reframed, Header, Blocks, Raw, Err));
		TestEqual(TEXT("each actual marker wins"), Header.Kind, Kind.Value);
		TestTrue(TEXT("view parser retains Custom payload"), Raw.Contains(TEXT("Custom")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKKCardClassifyTest,
	"OpenKoikatsu.KKCard.Classify",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKCardClassifyTest::RunTest(const FString& Parameters)
{
	const auto ToBytes = [](const ANSICHAR* Text)
	{
		TArray<uint8> Bytes;
		const int32 Len = FCStringAnsi::Strlen(Text);
		Bytes.Append(reinterpret_cast<const uint8*>(Text), Len);
		return Bytes;
	};

	TestEqual(TEXT("KKS"), ClassifyCard(ToBytes("xxKoiKatuCharaSunyy")), EKKCardKind::KKS);
	TestEqual(TEXT("KKSP"), ClassifyCard(ToBytes("xxKoiKatuCharaSPyy")), EKKCardKind::KKSP);
	TestEqual(TEXT("KK"), ClassifyCard(ToBytes("xxKoiKatuCharayy")), EKKCardKind::KK);
	TestEqual(TEXT("coord"), ClassifyCard(ToBytes("KoiKatuClothes")), EKKCardKind::Coordinate);
	return true;
}

#endif
