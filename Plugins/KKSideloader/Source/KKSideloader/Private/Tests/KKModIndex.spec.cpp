#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "KKModIndex.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	void WriteU16(TArray<uint8>& Out, uint16 V)
	{
		Out.Add(static_cast<uint8>(V));
		Out.Add(static_cast<uint8>(V >> 8));
	}

	void WriteU32(TArray<uint8>& Out, uint32 V)
	{
		Out.Add(static_cast<uint8>(V));
		Out.Add(static_cast<uint8>(V >> 8));
		Out.Add(static_cast<uint8>(V >> 16));
		Out.Add(static_cast<uint8>(V >> 24));
	}

	uint32 Crc32(const TArray<uint8>& Data)
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

	bool WriteFakeZipmod(const FString& Path)
	{
		const ANSICHAR* Xml =
			"<manifest schema-ver=\"1\">\r\n"
			"\t<guid>openkoikatsu.fake.head</guid>\r\n"
			"\t<name>Fake Head</name>\r\n"
			"</manifest>";
		TArray<uint8> Data;
		Data.Append(reinterpret_cast<const uint8*>(Xml), FCStringAnsi::Strlen(Xml));
		const ANSICHAR* Name = "manifest.xml";
		const int32 NameLen = FCStringAnsi::Strlen(Name);
		const uint32 Crc = Crc32(Data);

		TArray<uint8> Local;
		WriteU32(Local, 0x04034b50);
		WriteU16(Local, 20);
		WriteU16(Local, 0);
		WriteU16(Local, 0);
		WriteU16(Local, 0);
		WriteU16(Local, 0);
		WriteU32(Local, Crc);
		WriteU32(Local, Data.Num());
		WriteU32(Local, Data.Num());
		WriteU16(Local, static_cast<uint16>(NameLen));
		WriteU16(Local, 0);
		Local.Append(reinterpret_cast<const uint8*>(Name), NameLen);
		Local.Append(Data);

		TArray<uint8> Central;
		WriteU32(Central, 0x02014b50);
		WriteU16(Central, 20);
		WriteU16(Central, 20);
		WriteU16(Central, 0);
		WriteU16(Central, 0);
		WriteU16(Central, 0);
		WriteU16(Central, 0);
		WriteU32(Central, Crc);
		WriteU32(Central, Data.Num());
		WriteU32(Central, Data.Num());
		WriteU16(Central, static_cast<uint16>(NameLen));
		WriteU16(Central, 0);
		WriteU16(Central, 0);
		WriteU16(Central, 0);
		WriteU16(Central, 0);
		WriteU32(Central, 0);
		WriteU32(Central, 0);
		Central.Append(reinterpret_cast<const uint8*>(Name), NameLen);

		TArray<uint8> End;
		WriteU32(End, 0x06054b50);
		WriteU16(End, 0);
		WriteU16(End, 0);
		WriteU16(End, 1);
		WriteU16(End, 1);
		WriteU32(End, Central.Num());
		WriteU32(End, Local.Num());
		WriteU16(End, 0);

		TArray<uint8> Zip;
		Zip.Append(Local);
		Zip.Append(Central);
		Zip.Append(End);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveArrayToFile(Zip, *Path);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKKZipmodGuidTest,
	"OpenKoikatsu.KKSideloader.ZipmodGuid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKZipmodGuidTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("TestCards/fake.guid.zipmod"));
	TestTrue(TEXT("write zipmod"), WriteFakeZipmod(Path));
	FString Guid;
	FString Name;
	const bool bOk = ReadZipmodGuid(Path, Guid, Name);
	TestTrue(TEXT("read guid"), bOk);
	TestEqual(TEXT("guid"), Guid, FString(TEXT("openkoikatsu.fake.head")));
	TestEqual(TEXT("name"), Name, FString(TEXT("Fake Head")));
	return true;
}

#endif
