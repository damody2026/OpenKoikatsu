#include "KKTestFixtures.h"
#include "KKCardWriter.h"
#include "KKMsgpack.h"
#include "KKPng.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
	const uint8 GPng1x1[] = {
		0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
		0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,
		0xDE,0x00,0x00,0x00,0x0C,0x49,0x44,0x41,0x54,0x78,0xDA,0x63,0xF8,0xCF,0xC0,0x00,
		0x00,0x03,0x01,0x01,0x00,0xF7,0x03,0x41,0x43,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,
		0x44,0xAE,0x42,0x60,0x82
	};

	TSharedPtr<FKKMsgValue> IntVal(int32 V)
	{
		TSharedPtr<FKKMsgValue> N = MakeShared<FKKMsgValue>();
		N->Kind = EKKMsgKind::Int;
		N->i = V;
		return N;
	}

	TSharedPtr<FKKMsgValue> StrVal(const TCHAR* S)
	{
		TSharedPtr<FKKMsgValue> N = MakeShared<FKKMsgValue>();
		N->Kind = EKKMsgKind::String;
		N->s = S;
		return N;
	}

	TSharedPtr<FKKMsgValue> FltVal(float V)
	{
		TSharedPtr<FKKMsgValue> N = MakeShared<FKKMsgValue>();
		N->Kind = EKKMsgKind::Float;
		N->f = V;
		return N;
	}

	TSharedPtr<FKKMsgValue> BinVal(const TArray<uint8>& Bytes)
	{
		TSharedPtr<FKKMsgValue> N = MakeShared<FKKMsgValue>();
		N->Kind = EKKMsgKind::Bin;
		N->Bin = Bytes;
		return N;
	}

	void Put(FKKMsgValue& Map, const TCHAR* Key, TSharedPtr<FKKMsgValue> Value)
	{
		Map.Kind = EKKMsgKind::Map;
		Map.Map.Add(Key, Value);
		Map.MapKeys.Add(Key);
	}
}

FString KKTestCardPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("TestCards/minimal_kks.png"));
}

bool EnsureMinimalKksCard(FString& OutError)
{
	const FString Path = KKTestCardPath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);

	FKKMsgValue Face;
	Put(Face, TEXT("headId"), IntVal(1));
	Put(Face, TEXT("hlUpId"), IntVal(1919));
	Put(Face, TEXT("eyelineUpId"), IntVal(1880));
	TSharedPtr<FKKMsgValue> Shapes = MakeShared<FKKMsgValue>();
	Shapes->Kind = EKKMsgKind::Array;
	for (int32 i = 0; i < 52; ++i)
	{
		Shapes->Array.Add(FltVal(0.5f));
	}
	Put(Face, TEXT("shapeValueFace"), Shapes);

	FKKMsgValue Body;
	Put(Body, TEXT("skinId"), IntVal(0));

	FKKMsgValue Hair;
	TSharedPtr<FKKMsgValue> Parts = MakeShared<FKKMsgValue>();
	Parts->Kind = EKKMsgKind::Array;
	for (int32 Id : {10, 11, 12, 13})
	{
		TSharedPtr<FKKMsgValue> Part = MakeShared<FKKMsgValue>();
		Put(*Part, TEXT("id"), IntVal(Id));
		Parts->Array.Add(Part);
	}
	Put(Hair, TEXT("parts"), Parts);

	FKKMsgValue Custom;
	Put(Custom, TEXT("face"), MakeShared<FKKMsgValue>(Face));
	Put(Custom, TEXT("body"), MakeShared<FKKMsgValue>(Body));
	Put(Custom, TEXT("hair"), MakeShared<FKKMsgValue>(Hair));

	FKKMsgValue Parameter;
	Put(Parameter, TEXT("lastname"), StrVal(TEXT("Test")));
	Put(Parameter, TEXT("firstname"), StrVal(TEXT("Card")));
	Put(Parameter, TEXT("nickname"), StrVal(TEXT("nick")));
	Put(Parameter, TEXT("sex"), IntVal(1));
	Put(Parameter, TEXT("personality"), IntVal(0));

	FKKMsgValue Resolve;
	Put(Resolve, TEXT("ModID"), StrVal(TEXT("HeadMod")));
	Put(Resolve, TEXT("Slot"), IntVal(1));
	Put(Resolve, TEXT("LocalSlot"), IntVal(100));
	Put(Resolve, TEXT("Property"), StrVal(TEXT("ChaFileFace.headId")));
	Put(Resolve, TEXT("CategoryNo"), IntVal(100));
	Put(Resolve, TEXT("Author"), StrVal(TEXT("minih")));
	Put(Resolve, TEXT("Name"), StrVal(TEXT("headmod")));
	TArray<uint8> ResolveBytes;
	if (!PackMsgpack(Resolve, ResolveBytes, OutError))
	{
		return false;
	}

	TSharedPtr<FKKMsgValue> InfoList = MakeShared<FKKMsgValue>();
	InfoList->Kind = EKKMsgKind::Array;
	InfoList->Array.Add(BinVal(ResolveBytes));
	FKKMsgValue Uar;
	Put(Uar, TEXT("info"), InfoList);
	TSharedPtr<FKKMsgValue> Plugin = MakeShared<FKKMsgValue>();
	Plugin->Kind = EKKMsgKind::Array;
	Plugin->Array.Add(IntVal(0));
	Plugin->Array.Add(MakeShared<FKKMsgValue>(Uar));
	FKKMsgValue KKEx;
	Put(KKEx, TEXT("com.bepis.sideloader.universalautoresolver"), Plugin);

	TArray<uint8> CustomBytes, ParamBytes, KKExBytes;
	if (!PackMsgpack(Custom, CustomBytes, OutError)
		|| !PackMsgpack(Parameter, ParamBytes, OutError)
		|| !PackMsgpack(KKEx, KKExBytes, OutError))
	{
		return false;
	}

	FKKCharacterFile File;
	File.Header.ProductNo = 100;
	File.Header.Marker = TEXT("【KoiKatuCharaSun】");
	File.Header.Version = TEXT("0.0.0");
	File.Header.Kind = EKKCardKind::KKS;
	File.Header.PreviewPng.Append(GPng1x1, sizeof(GPng1x1));
	File.RawBlocks.Add(TEXT("Custom"), CustomBytes);
	File.RawBlocks.Add(TEXT("Parameter"), ParamBytes);
	File.RawBlocks.Add(TEXT("KKEx"), KKExBytes);
	{
		FKKBlockInfo A; A.Name = TEXT("Custom"); A.Size = CustomBytes.Num(); File.Blocks.Add(A);
		FKKBlockInfo B; B.Name = TEXT("Parameter"); B.Size = ParamBytes.Num(); File.Blocks.Add(B);
		FKKBlockInfo C; C.Name = TEXT("KKEx"); C.Size = KKExBytes.Num(); File.Blocks.Add(C);
	}
	File.LastName = TEXT("Test");
	File.FirstName = TEXT("Card");
	File.Nickname = TEXT("nick");
	return UKKCardWriter::SaveWithEdits(File, Path, OutError);
}
