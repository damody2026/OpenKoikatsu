#include "KKModIndex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "zlib.h"

namespace
{
	uint16 ReadU16(const uint8* P)
	{
		return static_cast<uint16>(P[0] | (P[1] << 8));
	}

	uint32 ReadU32(const uint8* P)
	{
		return static_cast<uint32>(P[0] | (P[1] << 8) | (P[2] << 16) | (P[3] << 24));
	}

	bool InflateRaw(const uint8* Src, int32 SrcLen, TArray<uint8>& Out, int32 Uncompressed)
	{
		Out.SetNumUninitialized(Uncompressed);
		z_stream Stream;
		FMemory::Memzero(Stream);
		Stream.next_in = const_cast<Bytef*>(Src);
		Stream.avail_in = static_cast<uInt>(SrcLen);
		Stream.next_out = Out.GetData();
		Stream.avail_out = static_cast<uInt>(Uncompressed);
		if (inflateInit2(&Stream, -MAX_WBITS) != Z_OK)
		{
			return false;
		}
		const int32 Status = inflate(&Stream, Z_FINISH);
		inflateEnd(&Stream);
		return Status == Z_STREAM_END;
	}

	bool ReadZipEntry(
		const TArray<uint8>& Bytes,
		int32& Pos,
		FString& OutName,
		TArray<uint8>& OutData)
	{
		if (Pos + 30 > Bytes.Num())
		{
			return false;
		}
		const uint32 Sig = ReadU32(Bytes.GetData() + Pos);
		if (Sig == 0x02014b50 || Sig == 0x06054b50)
		{
			return false;
		}
		if (Sig != 0x04034b50)
		{
			return false;
		}
		const uint16 Method = ReadU16(Bytes.GetData() + Pos + 8);
		const uint32 CompSize = ReadU32(Bytes.GetData() + Pos + 18);
		const uint32 UncompSize = ReadU32(Bytes.GetData() + Pos + 22);
		const uint16 NameLen = ReadU16(Bytes.GetData() + Pos + 26);
		const uint16 ExtraLen = ReadU16(Bytes.GetData() + Pos + 28);
		const int32 NameAt = Pos + 30;
		if (NameAt + NameLen + ExtraLen > Bytes.Num())
		{
			return false;
		}
		FUTF8ToTCHAR NameConv(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + NameAt), NameLen);
		OutName = FString(NameConv.Length(), NameConv.Get());
		const int32 DataAt = NameAt + NameLen + ExtraLen;
		if (DataAt + static_cast<int32>(CompSize) > Bytes.Num())
		{
			return false;
		}
		if (Method == 0)
		{
			OutData.SetNumUninitialized(UncompSize);
			FMemory::Memcpy(OutData.GetData(), Bytes.GetData() + DataAt, UncompSize);
		}
		else if (Method == 8)
		{
			if (!InflateRaw(Bytes.GetData() + DataAt, CompSize, OutData, UncompSize))
			{
				Pos = DataAt + CompSize;
				OutData.Reset();
				return true;
			}
		}
		else
		{
			OutData.Reset();
		}
		Pos = DataAt + CompSize;
		return true;
	}

	FString ExtractTag(const FString& Xml, const TCHAR* Tag)
	{
		const FString Open = FString::Printf(TEXT("<%s>"), Tag);
		const FString Close = FString::Printf(TEXT("</%s>"), Tag);
		int32 A = Xml.Find(Open, ESearchCase::IgnoreCase);
		int32 B = Xml.Find(Close, ESearchCase::IgnoreCase);
		if (A == INDEX_NONE || B == INDEX_NONE || B <= A)
		{
			return FString();
		}
		A += Open.Len();
		return Xml.Mid(A, B - A).TrimStartAndEnd();
	}

	bool PathInModpack(const FString& Path, const FString& ModsDir)
	{
		FString Rel = Path;
		if (FPaths::MakePathRelativeTo(Rel, *ModsDir))
		{
			return Rel.Contains(TEXT("Sideloader Modpack"));
		}
		return Path.Contains(TEXT("Sideloader Modpack"));
	}
}

bool ReadZipmodGuid(const FString& ZipPath, FString& OutGuid, FString& OutName)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *ZipPath))
	{
		return false;
	}
	int32 Pos = 0;
	while (Pos + 30 <= Bytes.Num())
	{
		FString Name;
		TArray<uint8> Data;
		if (!ReadZipEntry(Bytes, Pos, Name, Data))
		{
			break;
		}
		if (!Name.EndsWith(TEXT("manifest.xml"), ESearchCase::IgnoreCase) || Data.Num() == 0)
		{
			continue;
		}
		FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Data.GetData()), Data.Num());
		const FString Xml = FString(Conv.Length(), Conv.Get());
		OutGuid = ExtractTag(Xml, TEXT("guid"));
		OutName = ExtractTag(Xml, TEXT("name"));
		return !OutGuid.IsEmpty();
	}
	return false;
}

bool FKKModIndex::Build(const FString& InModsDir, FString& OutError)
{
	GuidToMod.Reset();
	ModsDir = InModsDir;
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *InModsDir, TEXT("*.zipmod"), true, false);
	for (const FString& Path : Files)
	{
		FString Guid;
		FString Name;
		if (!ReadZipmodGuid(Path, Guid, Name))
		{
			continue;
		}
		FKKZipmod Mod;
		Mod.Guid = Guid;
		Mod.Path = Path;
		Mod.Name = Name;
		Mod.bInSideloaderModpack = PathInModpack(Path, InModsDir);
		GuidToMod.Add(Guid, MoveTemp(Mod));
	}
	if (GuidToMod.Num() == 0 && Files.Num() > 0)
	{
		OutError = TEXT("scanned zipmods but found no GUIDs");
		LastError = OutError;
		return false;
	}
	return true;
}

bool FKKModIndex::LoadCache(const FString& CachePath)
{
	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *CachePath))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	ModsDir = Root->GetStringField(TEXT("mods_dir"));
	GuidToMod.Reset();
	const TSharedPtr<FJsonObject> Guids = Root->GetObjectField(TEXT("guids"));
	if (!Guids.IsValid())
	{
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Guids->Values)
	{
		FKKZipmod Mod;
		Mod.Guid = Pair.Key;
		if (Pair.Value.IsValid())
		{
			Mod.Path = Pair.Value->AsString();
		}
		GuidToMod.Add(Mod.Guid, MoveTemp(Mod));
	}
	return true;
}

bool FKKModIndex::SaveCache(const FString& CachePath) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("mods_dir"), ModsDir);
	TSharedRef<FJsonObject> Guids = MakeShared<FJsonObject>();
	for (const TPair<FString, FKKZipmod>& Pair : GuidToMod)
	{
		Guids->SetStringField(Pair.Key, Pair.Value.Path);
	}
	Root->SetObjectField(TEXT("guids"), Guids);
	FString JsonStr;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
	FJsonSerializer::Serialize(Root, Writer);
	return FFileHelper::SaveStringToFile(JsonStr, *CachePath);
}

const FKKZipmod* FKKModIndex::Find(const FString& Guid) const
{
	return GuidToMod.Find(Guid);
}

TArray<FKKResolveInfo> FKKModIndex::Missing(const TArray<FKKResolveInfo>& Resolves) const
{
	TArray<FKKResolveInfo> Out;
	TSet<FString> Seen;
	for (const FKKResolveInfo& Info : Resolves)
	{
		if (Info.ModID.IsEmpty() || Seen.Contains(Info.ModID))
		{
			continue;
		}
		Seen.Add(Info.ModID);
		if (!GuidToMod.Contains(Info.ModID))
		{
			Out.Add(Info);
		}
	}
	return Out;
}
