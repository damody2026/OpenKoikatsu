#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "KKLocalMods.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	void LocalModTestWriteU16(TArray<uint8>& Out, uint16 V)
	{
		Out.Add(static_cast<uint8>(V));
		Out.Add(static_cast<uint8>(V >> 8));
	}

	void LocalModTestWriteU32(TArray<uint8>& Out, uint32 V)
	{
		Out.Add(static_cast<uint8>(V));
		Out.Add(static_cast<uint8>(V >> 8));
		Out.Add(static_cast<uint8>(V >> 16));
		Out.Add(static_cast<uint8>(V >> 24));
	}

	uint32 LocalModTestCrc32(const TArray<uint8>& Data)
	{
		uint32 Crc = 0xFFFFFFFF;
		for (uint8 B : Data)
		{
			Crc ^= B;
			for (int32 i = 0; i < 8; ++i)
			{
				Crc = (Crc & 1) ? (Crc >> 1) ^ 0xEDB88320u : (Crc >> 1);
			}
		}
		return ~Crc;
	}

	bool LocalModTestWriteFakeZipmod(const FString& Path, bool Alternate = false, bool Bom = false, const FString& Version=FString())
	{
		const ANSICHAR* Xml =
			"<manifest schema-ver=\"1\">\r\n"
			"\t<guid>openkoikatsu.fake.head</guid>\r\n"
			"\t<name>Fake Head</name>\r\n"
			"</manifest>";
		TArray<uint8> Data;
		if (Bom) { Data.Add(0xEF); Data.Add(0xBB); Data.Add(0xBF); }
		FString XmlText=UTF8_TO_TCHAR(Xml);if(!Version.IsEmpty())XmlText.ReplaceInline(TEXT("</manifest>"),*(TEXT("<version>")+Version+TEXT("</version></manifest>")));
		FTCHARToUTF8 Encoded(*XmlText);Data.Append(reinterpret_cast<const uint8*>(Encoded.Get()),Encoded.Length());
		if (Alternate) Data.Add(32);
		const ANSICHAR* Name = "manifest.xml";
		const int32 NameLen = FCStringAnsi::Strlen(Name);
		const uint32 Crc = LocalModTestCrc32(Data);

		TArray<uint8> Local;
		LocalModTestWriteU32(Local, 0x04034b50);
		LocalModTestWriteU16(Local, 20);
		LocalModTestWriteU16(Local, 0);
		LocalModTestWriteU16(Local, 0);
		LocalModTestWriteU16(Local, 0);
		LocalModTestWriteU16(Local, 0);
		LocalModTestWriteU32(Local, Crc);
		LocalModTestWriteU32(Local, Data.Num());
		LocalModTestWriteU32(Local, Data.Num());
		LocalModTestWriteU16(Local, static_cast<uint16>(NameLen));
		LocalModTestWriteU16(Local, 0);
		Local.Append(reinterpret_cast<const uint8*>(Name), NameLen);
		Local.Append(Data);

		TArray<uint8> Central;
		LocalModTestWriteU32(Central, 0x02014b50);
		LocalModTestWriteU16(Central, 20);
		LocalModTestWriteU16(Central, 20);
		LocalModTestWriteU16(Central, 0);
		LocalModTestWriteU16(Central, 0);
		LocalModTestWriteU16(Central, 0);
		LocalModTestWriteU16(Central, 0);
		LocalModTestWriteU32(Central, Crc);
		LocalModTestWriteU32(Central, Data.Num());
		LocalModTestWriteU32(Central, Data.Num());
		LocalModTestWriteU16(Central, static_cast<uint16>(NameLen));
		LocalModTestWriteU16(Central, 0);
		LocalModTestWriteU16(Central, 0);
		LocalModTestWriteU16(Central, 0);
		LocalModTestWriteU16(Central, 0);
		LocalModTestWriteU32(Central, 0);
		LocalModTestWriteU32(Central, 0);
		Central.Append(reinterpret_cast<const uint8*>(Name), NameLen);

		TArray<uint8> End;
		LocalModTestWriteU32(End, 0x06054b50);
		LocalModTestWriteU16(End, 0);
		LocalModTestWriteU16(End, 0);
		LocalModTestWriteU16(End, 1);
		LocalModTestWriteU16(End, 1);
		LocalModTestWriteU32(End, Central.Num());
		LocalModTestWriteU32(End, Local.Num());
		LocalModTestWriteU16(End, 0);

		TArray<uint8> Zip;
		Zip.Append(Local);
		Zip.Append(Central);
		Zip.Append(End);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveArrayToFile(Zip, *Path);
	}
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKLocalModsTest, "OpenKoikatsu.IndependentCard.LocalZipmods",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKKLocalModsTest::RunTest(const FString& Parameters)
{
    const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("TestCards") / FGuid::NewGuid().ToString());
    const FString Card = Folder / TEXT("card.png");
    const FString Mod = Folder / TEXT("arbitrary_filename.zipmod");
    TestTrue(TEXT("write local"), LocalModTestWriteFakeZipmod(Mod));
    TestTrue(TEXT("write nested"), LocalModTestWriteFakeZipmod(Folder / TEXT("nested/ignore.zipmod"), true));
    FKKResolveInfo Ref; Ref.ModID=TEXT("openkoikatsu.fake.head");
    FKKResolveInfo Missing; Missing.ModID=TEXT("not.installed");
    FKKLocalModIndex Index; Index.BuildForCard(Card);
    TArray<FKKLocalModRequirement> Resolved = Index.Resolve({Ref,Ref,Missing});
    TestEqual(TEXT("deduplicated requests"), Resolved.Num(), 2);
    if (Resolved.Num()!=2) return false;
    TestTrue(TEXT("GUID matches arbitrary filename; nested ignored"), Resolved[0].Status==EKKLocalModStatus::Found);
    TestTrue(TEXT("missing reported"), Resolved[1].Status==EKKLocalModStatus::Missing);
    TestTrue(TEXT("write identical duplicate"), LocalModTestWriteFakeZipmod(Folder / TEXT("duplicate.zipmod")));
    Index.BuildForCard(Card);
    TestTrue(TEXT("identical duplicate is unambiguous"), Index.Resolve({Ref})[0].Status==EKKLocalModStatus::Found);
    const FString Output = Folder / TEXT("output"); IFileManager::Get().MakeDirectory(*Output,true);
    FString Error;
    TestTrue(TEXT("copy found"),Index.CopyFound({Ref},Output,Error));
    TestTrue(TEXT("repeat copy safe"),Index.CopyFound({Ref},Output,Error));
    TestTrue(TEXT("write different same GUID"),LocalModTestWriteFakeZipmod(Folder / TEXT("conflict.zipmod"),true));
    Index.BuildForCard(Card);
    TestTrue(TEXT("different content same GUID conflicts"),Index.Resolve({Ref})[0].Status==EKKLocalModStatus::Conflict);
    TArray<uint8> Corrupt; FFileHelper::LoadFileToArray(Corrupt,*Mod);
    Corrupt[14]^=1; // Local CRC is informational; central CRC is authoritative.
    const int32 Central = 30+12+FCStringAnsi::Strlen("<manifest schema-ver=\"1\">\r\n\t<guid>openkoikatsu.fake.head</guid>\r\n\t<name>Fake Head</name>\r\n</manifest>");
    Corrupt[Central+16]^=1;
    TestTrue(TEXT("write corrupt"),FFileHelper::SaveArrayToFile(Corrupt,*(Folder / TEXT("bad.zipmod"))));
    Index.BuildForCard(Card);
    TestTrue(TEXT("corruption diagnosed"),Index.Diagnostics.Num()>0);
    const FString BomFolder=Folder / TEXT("bom");
    TestTrue(TEXT("write UTF8 BOM manifest"),LocalModTestWriteFakeZipmod(BomFolder / TEXT("bom.zipmod"),false,true));
    Index.BuildForCard(BomFolder / TEXT("card.png"));
    TestTrue(TEXT("UTF8 BOM resolves GUID"),Index.Resolve({Ref})[0].Status==EKKLocalModStatus::Found);
    TestEqual(TEXT("UTF8 BOM has no diagnostic"),Index.Diagnostics.Num(),0);
    const FString Game=Folder/TEXT("game");
    IFileManager::Get().MakeDirectory(*(Game/TEXT("abdata")),true);
    IFileManager::Get().MakeDirectory(*(Game/TEXT("UserData/chara/female")),true);
    TestTrue(TEXT("write installation marker"),FFileHelper::SaveStringToFile(TEXT("fixture"),*(Game/TEXT("KoikatsuSunshine.exe"))));
    TestTrue(TEXT("write recursive game mod"),LocalModTestWriteFakeZipmod(Game/TEXT("mods/nested/same.zipmod")));
    Index.BuildForCard(Game/TEXT("UserData/chara/female/card.png"));
    TestTrue(TEXT("ancestor game detected"),FPaths::IsSamePath(Index.GameRoot,Game));
    TestEqual(TEXT("recursive game mod collected"),Index.ZipmodPaths.Num(),1);
    TestTrue(TEXT("game GUID resolved without adjacent zipmod"),Index.Resolve({Ref})[0].Status==EKKLocalModStatus::Found);
    Index.BuildForCard(Game/TEXT("UserData/chara/female/../female/card.png"));
    TestEqual(TEXT("normalized path discovers same source"),Index.ZipmodPaths.Num(),1);
    TestTrue(TEXT("write unrelated invalid game mod"),FFileHelper::SaveStringToFile(TEXT("bad zip"),*(Game/TEXT("mods/bad.zipmod"))));
    Index.BuildForCard(Game/TEXT("UserData/chara/female/card.png"));
    TestEqual(TEXT("invalid global mod skipped"),Index.SkippedGameMods.Num(),1);
    TestEqual(TEXT("invalid global mod is not fatal"),Index.Diagnostics.Num(),0);
    TestEqual(TEXT("invalid global mod not attached"),Index.ZipmodPaths.Num(),1);
    TestTrue(TEXT("write identical adjacent game copy"),LocalModTestWriteFakeZipmod(Game/TEXT("UserData/chara/female/same.zipmod")));
    Index.BuildForCard(Game/TEXT("UserData/chara/female/card.png"));
    TestEqual(TEXT("same basename in separate directories retained"),Index.ZipmodPaths.Num(),2);
    TestTrue(TEXT("identical local/game GUID deduplicated"),Index.Resolve({Ref})[0].Status==EKKLocalModStatus::Found);
    TestTrue(TEXT("write conflicting adjacent game copy"),LocalModTestWriteFakeZipmod(Game/TEXT("UserData/chara/female/conflict.zipmod"),true));
    Index.BuildForCard(Game/TEXT("UserData/chara/female/card.png"));
    TestTrue(TEXT("different local/game GUID conflicts"),Index.Resolve({Ref})[0].Status==EKKLocalModStatus::Conflict);
    Index.BuildForCard(Folder/TEXT("game-copy/card.png"));
    TestTrue(TEXT("sibling prefix cannot detect game"),Index.GameRoot.IsEmpty());
    TestEqual(TEXT("external card has no global search"),Index.ZipmodPaths.Num(),0);
    const FString NotGame=Folder/TEXT("notgame");
    IFileManager::Get().MakeDirectory(*(NotGame/TEXT("abdata")),true);
    IFileManager::Get().MakeDirectory(*(NotGame/TEXT("UserData")),true);
    TestTrue(TEXT("write non-game mod"),LocalModTestWriteFakeZipmod(NotGame/TEXT("mods/nested/mod.zipmod")));
    Index.BuildForCard(NotGame/TEXT("UserData/card.png"));
    TestTrue(TEXT("data folders alone are not installation"),Index.GameRoot.IsEmpty());
    TestEqual(TEXT("non-game mods not searched"),Index.ZipmodPaths.Num(),0);
    const FString Versions=Folder/TEXT("versions");
    IFileManager::Get().MakeDirectory(*(Versions/TEXT("abdata")),true);IFileManager::Get().MakeDirectory(*(Versions/TEXT("UserData")),true);
    TestTrue(TEXT("write version game marker"),FFileHelper::SaveStringToFile(TEXT("fixture"),*(Versions/TEXT("KoikatsuSunshine.exe"))));
    TestTrue(TEXT("write game version1.9"),LocalModTestWriteFakeZipmod(Versions/TEXT("mods/a.zipmod"),false,false,TEXT("v1.9")));
    TestTrue(TEXT("write game version1.10"),LocalModTestWriteFakeZipmod(Versions/TEXT("mods/b.zipmod"),false,false,TEXT("r1.10")));
    Index.BuildForCard(Versions/TEXT("UserData/card.png"));
    TestEqual(TEXT("game versions select one archive"),Index.ZipmodPaths.Num(),1);
    TestTrue(TEXT("numeric version10 newer than9"),Index.ZipmodPaths[0].EndsWith(TEXT("b.zipmod")));
    TestTrue(TEXT("selected game version is unambiguous"),Index.Resolve({Ref})[0].Status==EKKLocalModStatus::Found);
    TestTrue(TEXT("write no-version archive"),LocalModTestWriteFakeZipmod(Versions/TEXT("mods/c.zipmod")));
    IFileManager::Get().SetTimeStamp(*(Versions/TEXT("mods/a.zipmod")),FDateTime(2020,1,1));
    IFileManager::Get().SetTimeStamp(*(Versions/TEXT("mods/b.zipmod")),FDateTime(2020,1,2));
    IFileManager::Get().SetTimeStamp(*(Versions/TEXT("mods/c.zipmod")),FDateTime(2020,1,3));
    Index.BuildForCard(Versions/TEXT("UserData/card.png"));
    TestTrue(TEXT("missing version uses newest timestamp"),Index.ZipmodPaths[0].EndsWith(TEXT("c.zipmod")));
    return true;
}
#endif
