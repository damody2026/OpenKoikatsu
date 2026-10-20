#include "KKPng.h"
#include "KKMsgpack.h"
#include "Serialization/MemoryReader.h"

namespace
{
	uint32 ReadBe32(const uint8* P)
	{
		return (static_cast<uint32>(P[0]) << 24)
			| (static_cast<uint32>(P[1]) << 16)
			| (static_cast<uint32>(P[2]) << 8)
			| static_cast<uint32>(P[3]);
	}

	bool ContainsAscii(const TArray<uint8>& Bytes, const ANSICHAR* Needle)
	{
		const int32 NeedleLen = FCStringAnsi::Strlen(Needle);
		if (NeedleLen <= 0 || Bytes.Num() < NeedleLen)
		{
			return false;
		}
		const uint8* Hay = Bytes.GetData();
		for (int32 i = 0; i <= Bytes.Num() - NeedleLen; ++i)
		{
			if (FMemory::Memcmp(Hay + i, Needle, NeedleLen) == 0)
			{
				return true;
			}
		}
		return false;
	}

	EKKCardKind KindFromBytes(const TArray<uint8>& Bytes)
	{
		if (ContainsAscii(Bytes, "KoiKatuClothes"))
		{
			return EKKCardKind::Coordinate;
		}
		const bool bChara = ContainsAscii(Bytes, "KoiKatuChara");
		if (!bChara)
		{
			return EKKCardKind::Unknown;
		}
		if (ContainsAscii(Bytes, "KoiKatuCharaSP"))
		{
			return EKKCardKind::KKSP;
		}
		if (ContainsAscii(Bytes, "KoiKatuCharaSun"))
		{
			return EKKCardKind::KKS;
		}
		if (ContainsAscii(Bytes, "sceneInfo"))
		{
			return EKKCardKind::Scene;
		}
		return EKKCardKind::KK;
	}
}

bool FindPngIendEnd(const TArray<uint8>& Bytes, int32& OutEnd)
{
	OutEnd = -1;
	if (Bytes.Num() < 8)
	{
		return false;
	}
	int32 Pos = 8;
	while (Pos + 12 <= Bytes.Num())
	{
		const uint32 Length = ReadBe32(Bytes.GetData() + Pos);
		const uint8* Type = Bytes.GetData() + Pos + 4;
		const int64 Next = static_cast<int64>(Pos) + 12 + static_cast<int64>(Length);
		if (Next > Bytes.Num())
		{
			return false;
		}
		Pos = static_cast<int32>(Next);
		if (Type[0] == 'I' && Type[1] == 'E' && Type[2] == 'N' && Type[3] == 'D')
		{
			OutEnd = Pos;
			return true;
		}
	}
	return false;
}

EKKCardKind ClassifyCard(const TArray<uint8>& Bytes)
{
	FKKCardHeader Header;
	TArray<FKKBlockInfo> Blocks;
	FString Error;
	if (ParseCardHeader(Bytes, Header, Blocks, Error)) return Header.Kind;
	return KindFromBytes(Bytes);
}

bool Read7BitInt(FArchive& Reader, int32& OutValue)
{
	int32 Result = 0;
	int32 Shift = 0;
	for (int32 i = 0; i < 5; ++i)
	{
		if (Reader.AtEnd())
		{
			return false;
		}
		uint8 Byte = 0;
		Reader.Serialize(&Byte, 1);
		Result |= static_cast<int32>(Byte & 0x7F) << Shift;
		if ((Byte & 0x80) == 0)
		{
			OutValue = Result;
			return true;
		}
		Shift += 7;
	}
	return false;
}

void Write7BitInt(TArray<uint8>& Out, int32 Value)
{
	uint32 V = static_cast<uint32>(Value);
	while (true)
	{
		uint8 Byte = static_cast<uint8>(V & 0x7F);
		V >>= 7;
		if (V != 0)
		{
			Byte |= 0x80;
		}
		Out.Add(Byte);
		if (V == 0)
		{
			break;
		}
	}
}

void Write7BitString(TArray<uint8>& Out, const FString& Value)
{
	FTCHARToUTF8 Conv(*Value);
	Write7BitInt(Out, Conv.Length());
	Out.Append(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length());
}

void WriteInt32LE(TArray<uint8>& Out, int32 Value)
{
	const uint32 V = static_cast<uint32>(Value);
	Out.Add(static_cast<uint8>(V));
	Out.Add(static_cast<uint8>(V >> 8));
	Out.Add(static_cast<uint8>(V >> 16));
	Out.Add(static_cast<uint8>(V >> 24));
}

void WriteInt64LE(TArray<uint8>& Out, int64 Value)
{
	const uint64 V = static_cast<uint64>(Value);
	for (int32 i = 0; i < 8; ++i)
	{
		Out.Add(static_cast<uint8>(V >> (8 * i)));
	}
}

bool Read7BitString(FArchive& Reader, FString& OutString)
{
	int32 Len = 0;
	if (!Read7BitInt(Reader, Len) || Len < 0)
	{
		return false;
	}
	if (Len == 0)
	{
		OutString.Empty();
		return true;
	}
	if (Reader.Tell() + Len > Reader.TotalSize())
	{
		return false;
	}
	TArray<uint8> Utf8;
	Utf8.SetNumUninitialized(Len);
	Reader.Serialize(Utf8.GetData(), Len);
	FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Utf8.GetData()), Len);
	OutString = FString(Conv.Length(), Conv.Get());
	return true;
}

bool ParseCardHeader(
	const TArray<uint8>& Bytes,
	FKKCardHeader& OutHeader,
	TArray<FKKBlockInfo>& OutBlocks,
	FString& OutError)
{
	OutHeader = FKKCardHeader();
	OutBlocks.Reset();
	OutError.Empty();

	int32 PngEnd = -1;
	if (!FindPngIendEnd(Bytes, PngEnd) || PngEnd < 0 || PngEnd >= Bytes.Num())
	{
		OutError = TEXT("PNG IEND not found or no payload after IEND");
		return false;
	}

	OutHeader.PngEndOffset = PngEnd;
	OutHeader.PreviewPng.Append(Bytes.GetData(), PngEnd);
	// The payload is immutable for this call. Large plugin/texture blocks do
	// not need a second full-card copy just to read their small header.
	FMemoryReaderView Reader(FMemoryView(Bytes.GetData() + PngEnd, Bytes.Num() - PngEnd), true);
	Reader.SetByteSwapping(false);

	if (Reader.TotalSize() < 4)
	{
		OutError = TEXT("payload too small");
		return false;
	}
	Reader << OutHeader.ProductNo;

	if (!Read7BitString(Reader, OutHeader.Marker) || !Read7BitString(Reader, OutHeader.Version))
	{
		OutError = TEXT("failed to read marker/version");
		return false;
	}

	int32 FaceLen = 0;
	Reader << FaceLen;
	if (FaceLen < 0 || Reader.Tell() + FaceLen > Reader.TotalSize())
	{
		OutError = TEXT("invalid face png length");
		return false;
	}
	if (FaceLen > 0)
	{
		OutHeader.FacePng.SetNumUninitialized(FaceLen);
		Reader.Serialize(OutHeader.FacePng.GetData(), FaceLen);
	}

	int32 HeaderLen = 0;
	Reader << HeaderLen;
	if (HeaderLen <= 0 || Reader.Tell() + HeaderLen > Reader.TotalSize())
	{
		OutError = TEXT("invalid block header length");
		return false;
	}
	TArray<uint8> HeaderBytes;
	HeaderBytes.SetNumUninitialized(HeaderLen);
	Reader.Serialize(HeaderBytes.GetData(), HeaderLen);
	OutHeader.RawBlockTable = HeaderBytes;

	FKKMsgValue Root;
	if (!UnpackMsgpack(HeaderBytes.GetData(), HeaderBytes.Num(), Root, OutError))
	{
		return false;
	}
	const FKKMsgValue* Lst = Root.Find(TEXT("lstInfo"));
	if (!Lst || Lst->Kind != EKKMsgKind::Array)
	{
		OutError = TEXT("block header missing lstInfo");
		return false;
	}
	for (const TSharedPtr<FKKMsgValue>& ItemPtr : Lst->Array)
	{
		if (!ItemPtr.IsValid() || ItemPtr->Kind != EKKMsgKind::Map)
		{
			continue;
		}
		const FKKMsgValue& Item = *ItemPtr;
		FKKBlockInfo Info;
		if (const FKKMsgValue* Name = Item.Find(TEXT("name")))
		{
			Info.Name = Name->s;
		}
		if (const FKKMsgValue* Pos = Item.Find(TEXT("pos")))
		{
			Info.Pos = static_cast<int32>(Pos->i);
		}
		if (const FKKMsgValue* Size = Item.Find(TEXT("size")))
		{
			Info.Size = static_cast<int32>(Size->i);
		}
		if (!Info.Name.IsEmpty())
		{
			OutBlocks.Add(Info);
		}
	}

	// The serialized marker, rather than arbitrary embedded image/plugin
	// bytes, is authoritative. Inspect only declared block names for scenes.
	const FString& Marker = OutHeader.Marker;
	if (Marker.Contains(TEXT("KoiKatuClothes"))) OutHeader.Kind = EKKCardKind::Coordinate;
	else if (Marker.Contains(TEXT("KoiKatuCharaSP"))) OutHeader.Kind = EKKCardKind::KKSP;
	else if (Marker.Contains(TEXT("KoiKatuCharaSun"))) OutHeader.Kind = EKKCardKind::KKS;
	else if (Marker.Contains(TEXT("KoiKatuChara")))
	{
		OutHeader.Kind = OutBlocks.ContainsByPredicate([](const FKKBlockInfo& B) { return B.Name == TEXT("sceneInfo"); })
			? EKKCardKind::Scene : EKKCardKind::KK;
	}
	else
	{
		OutHeader.Kind = KindFromBytes(Bytes);
	}
	return true;
}

bool ParseCardHeader(
	const TArray<uint8>& Bytes,
	FKKCardHeader& OutHeader,
	TArray<FKKBlockInfo>& OutBlocks,
	TMap<FString, TArray<uint8>>& OutRawBlocks,
	FString& OutError)
{
	OutRawBlocks.Reset();
	if (!ParseCardHeader(Bytes, OutHeader, OutBlocks, OutError))
	{
		return false;
	}

	const uint8* Payload = Bytes.GetData() + OutHeader.PngEndOffset;
	FMemoryReaderView Reader(FMemoryView(Payload, Bytes.Num() - OutHeader.PngEndOffset), true);
	Reader.SetByteSwapping(false);
	int32 Dummy = 0;
	FString DummyStr;
	Reader << Dummy;
	if (!Read7BitString(Reader, DummyStr) || !Read7BitString(Reader, DummyStr))
	{
		return true;
	}
	int32 FaceLen = 0;
	Reader << FaceLen;
	if (FaceLen > 0)
	{
		Reader.Seek(Reader.Tell() + FaceLen);
	}
	int32 HeaderLen = 0;
	Reader << HeaderLen;
	Reader.Seek(Reader.Tell() + HeaderLen);
	int64 TotalSize = 0;
	Reader << TotalSize;
	const int64 Base = Reader.Tell();
	if (Base < 0 || Base > Reader.TotalSize())
	{
		return true;
	}
	for (const FKKBlockInfo& Info : OutBlocks)
	{
		const int64 Start = Base + Info.Pos;
		if (Info.Size < 0 || Start < 0 || Start + Info.Size > Reader.TotalSize())
		{
			continue;
		}
		TArray<uint8> Block;
		Block.SetNumUninitialized(Info.Size);
		FMemory::Memcpy(Block.GetData(), Payload + Start, Info.Size);
		OutRawBlocks.Add(Info.Name, MoveTemp(Block));
	}
	return true;
}
