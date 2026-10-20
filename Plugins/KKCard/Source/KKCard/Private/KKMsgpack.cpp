#include "KKMsgpack.h"

namespace
{
	class FCursor
	{
	public:
		const uint8* Ptr;
		const uint8* End;

		bool Need(int32 N) const { return N >= 0 && static_cast<uint64>(N) <= static_cast<uint64>(End - Ptr); }

		bool ReadByte(uint8& Out)
		{
			if (!Need(1))
			{
				return false;
			}
			Out = *Ptr++;
			return true;
		}

		bool ReadBe16(uint16& Out)
		{
			if (!Need(2))
			{
				return false;
			}
			Out = static_cast<uint16>((Ptr[0] << 8) | Ptr[1]);
			Ptr += 2;
			return true;
		}

		bool ReadBe32(uint32& Out)
		{
			if (!Need(4))
			{
				return false;
			}
			Out = (static_cast<uint32>(Ptr[0]) << 24)
				| (static_cast<uint32>(Ptr[1]) << 16)
				| (static_cast<uint32>(Ptr[2]) << 8)
				| static_cast<uint32>(Ptr[3]);
			Ptr += 4;
			return true;
		}

		bool ReadBytes(int32 N, TArray<uint8>& Out)
		{
			if (N < 0 || !Need(N))
			{
				return false;
			}
			Out.SetNumUninitialized(N);
			FMemory::Memcpy(Out.GetData(), Ptr, N);
			Ptr += N;
			return true;
		}
	};

	bool Unpack(FCursor& C, FKKMsgValue& Out, FString& Err);

    // Iterative traversal avoids allocating unrequested texture blobs and
    // avoids stack growth when skipping deeply nested plugin data.
    bool SkipValue(FCursor& C)
    {
        uint64 Pending=1;
        while(Pending--)
        {
            uint8 Code=0;if(!C.ReadByte(Code))return false;
            uint64 Bytes=0,Children=0;uint32 N=0;uint16 W=0;uint8 B=0;
            if(Code<=0x7f||Code>=0xe0){}
            else if((Code&0xe0)==0xa0)Bytes=Code&0x1f;
            else if((Code&0xf0)==0x90)Children=Code&0x0f;
            else if((Code&0xf0)==0x80)Children=2ull*(Code&0x0f);
            else switch(Code)
            {
                case 0xc0:case 0xc2:case 0xc3:break;
                case 0xcc:case 0xd0:Bytes=1;break;
                case 0xcd:case 0xd1:Bytes=2;break;
                case 0xce:case 0xd2:case 0xca:Bytes=4;break;
                case 0xcf:case 0xd3:case 0xcb:Bytes=8;break;
                case 0xc4:case 0xd9:case 0xc7:if(!C.ReadByte(B))return false;Bytes=uint64(B)+(Code==0xc7);break;
                case 0xc5:case 0xda:case 0xc8:if(!C.ReadBe16(W))return false;Bytes=uint64(W)+(Code==0xc8);break;
                case 0xc6:case 0xdb:case 0xc9:if(!C.ReadBe32(N))return false;Bytes=uint64(N)+(Code==0xc9);break;
                case 0xd4:Bytes=2;break;case 0xd5:Bytes=3;break;case 0xd6:Bytes=5;break;case 0xd7:Bytes=9;break;case 0xd8:Bytes=17;break;
                case 0xdc:case 0xde:if(!C.ReadBe16(W))return false;Children=uint64(W)*(Code==0xde?2:1);break;
                case 0xdd:case 0xdf:if(!C.ReadBe32(N))return false;Children=uint64(N)*(Code==0xdf?2:1);break;
                default:return false;
            }
            if(Bytes>static_cast<uint64>(C.End-C.Ptr))return false;
            C.Ptr+=Bytes;
            if(Children>static_cast<uint64>(C.End-C.Ptr)||Pending>static_cast<uint64>(C.End-C.Ptr)-Children)return false;
            Pending+=Children;
        }
        return true;
    }

	FString Utf8Bytes(const TArray<uint8>& Bytes)
	{
		if (Bytes.Num() == 0)
		{
			return FString();
		}
		FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		return FString(Conv.Length(), Conv.Get());
	}

	bool UnpackMap(FCursor& C, uint32 Count, FKKMsgValue& Out, FString& Err)
	{
		Out.Kind = EKKMsgKind::Map;
		for (uint32 i = 0; i < Count; ++i)
		{
			FKKMsgValue Key;
			TSharedPtr<FKKMsgValue> Value = MakeShared<FKKMsgValue>();
			if (!Unpack(C, Key, Err))
			{
				return false;
			}
			FString MapKey;
			if (Key.Kind == EKKMsgKind::String)
			{
				MapKey = Key.s;
			}
			else if (Key.Kind == EKKMsgKind::Int)
			{
				MapKey = FString::FromInt(static_cast<int32>(Key.i));
			}
			else
			{
				Err = TEXT("msgpack map key is not a string or int");
				return false;
			}
			if (!Unpack(C, *Value, Err))
			{
				return false;
			}
			Out.Map.Add(MapKey, Value);
			Out.MapKeys.Add(MapKey);
		}
		return true;
	}

	bool UnpackArray(FCursor& C, uint32 Count, FKKMsgValue& Out, FString& Err)
	{
		Out.Kind = EKKMsgKind::Array;
		Out.Array.Reserve(static_cast<int32>(Count));
		for (uint32 i = 0; i < Count; ++i)
		{
			TSharedPtr<FKKMsgValue> Item = MakeShared<FKKMsgValue>();
			if (!Unpack(C, *Item, Err))
			{
				return false;
			}
			Out.Array.Add(Item);
		}
		return true;
	}

	bool Unpack(FCursor& C, FKKMsgValue& Out, FString& Err)
	{
		uint8 Code = 0;
		if (!C.ReadByte(Code))
		{
			Err = TEXT("msgpack truncated");
			return false;
		}

		if (Code <= 0x7f)
		{
			Out.Kind = EKKMsgKind::Int;
			Out.i = Code;
			return true;
		}
		if (Code >= 0xe0)
		{
			Out.Kind = EKKMsgKind::Int;
			Out.i = static_cast<int8>(Code);
			return true;
		}
		if ((Code & 0xe0) == 0xa0)
		{
			const int32 N = Code & 0x1f;
			TArray<uint8> Bytes;
			if (!C.ReadBytes(N, Bytes))
			{
				Err = TEXT("msgpack str truncated");
				return false;
			}
			Out.Kind = EKKMsgKind::String;
			Out.s = Utf8Bytes(Bytes);
			return true;
		}
		if ((Code & 0xf0) == 0x80)
		{
			return UnpackMap(C, Code & 0x0f, Out, Err);
		}
		if ((Code & 0xf0) == 0x90)
		{
			return UnpackArray(C, Code & 0x0f, Out, Err);
		}

		switch (Code)
		{
		case 0xc0:
			Out.Kind = EKKMsgKind::Nil;
			return true;
		case 0xc2:
			Out.Kind = EKKMsgKind::Bool;
			Out.b = false;
			return true;
		case 0xc3:
			Out.Kind = EKKMsgKind::Bool;
			Out.b = true;
			return true;
		case 0xc4:
		case 0xc5:
		case 0xc6:
		{
			uint32 N = 0;
			if (Code == 0xc4)
			{
				uint8 B = 0;
				if (!C.ReadByte(B))
				{
					return false;
				}
				N = B;
			}
			else if (Code == 0xc5)
			{
				uint16 W = 0;
				if (!C.ReadBe16(W))
				{
					return false;
				}
				N = W;
			}
			else
			{
				if (!C.ReadBe32(N))
				{
					return false;
				}
			}
			if (!C.ReadBytes(static_cast<int32>(N), Out.Bin))
			{
				Err = TEXT("msgpack bin truncated");
				return false;
			}
			Out.Kind = EKKMsgKind::Bin;
			return true;
		}
		case 0xca:
		{
			uint32 Bits = 0;
			if (!C.ReadBe32(Bits))
			{
				return false;
			}
			float Value = 0.0f;
			FMemory::Memcpy(&Value, &Bits, 4);
			Out.Kind = EKKMsgKind::Float;
			Out.f = Value;
			return true;
		}
		case 0xcb:
		{
			uint32 Hi = 0;
			uint32 Lo = 0;
			if (!C.ReadBe32(Hi) || !C.ReadBe32(Lo))
			{
				return false;
			}
			const uint64 Bits = (static_cast<uint64>(Hi) << 32) | Lo;
			double Value = 0.0;
			FMemory::Memcpy(&Value, &Bits, 8);
			Out.Kind = EKKMsgKind::Float;
			Out.f = Value;
			return true;
		}
		case 0xd0:
		{
			uint8 B = 0;
			if (!C.ReadByte(B))
			{
				return false;
			}
			Out.Kind = EKKMsgKind::Int;
			Out.i = static_cast<int8>(B);
			return true;
		}
		case 0xd1:
		{
			uint16 W = 0;
			if (!C.ReadBe16(W))
			{
				return false;
			}
			Out.Kind = EKKMsgKind::Int;
			Out.i = static_cast<int16>(W);
			return true;
		}
		case 0xd2:
		{
			uint32 W = 0;
			if (!C.ReadBe32(W))
			{
				return false;
			}
			Out.Kind = EKKMsgKind::Int;
			Out.i = static_cast<int32>(W);
			return true;
		}
		case 0xcc:
		{
			uint8 B = 0;
			if (!C.ReadByte(B))
			{
				return false;
			}
			Out.Kind = EKKMsgKind::Int;
			Out.i = B;
			return true;
		}
		case 0xcd:
		{
			uint16 W = 0;
			if (!C.ReadBe16(W))
			{
				return false;
			}
			Out.Kind = EKKMsgKind::Int;
			Out.i = W;
			return true;
		}
		case 0xce:
		{
			uint32 W = 0;
			if (!C.ReadBe32(W))
			{
				return false;
			}
			Out.Kind = EKKMsgKind::Int;
			Out.i = W;
			return true;
		}
		case 0xd9:
		{
			uint8 N = 0;
			if (!C.ReadByte(N))
			{
				return false;
			}
			TArray<uint8> Bytes;
			if (!C.ReadBytes(N, Bytes))
			{
				return false;
			}
			Out.Kind = EKKMsgKind::String;
			Out.s = Utf8Bytes(Bytes);
			return true;
		}
		case 0xda:
		{
			uint16 N = 0;
			if (!C.ReadBe16(N))
			{
				return false;
			}
			TArray<uint8> Bytes;
			if (!C.ReadBytes(N, Bytes))
			{
				return false;
			}
			Out.Kind = EKKMsgKind::String;
			FUTF8ToTCHAR Conv(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
			Out.s = FString(Conv.Length(), Conv.Get());
			return true;
		}
		case 0xdc:
		{
			uint16 N = 0;
			if (!C.ReadBe16(N))
			{
				return false;
			}
			return UnpackArray(C, N, Out, Err);
		}
		case 0xde:
		{
			uint16 N = 0;
			if (!C.ReadBe16(N))
			{
				return false;
			}
			return UnpackMap(C, N, Out, Err);
		}
		case 0xdd:
		{
			uint32 N = 0;
			if (!C.ReadBe32(N))
			{
				return false;
			}
			return UnpackArray(C, N, Out, Err);
		}
		case 0xdf:
		{
			uint32 N = 0;
			if (!C.ReadBe32(N))
			{
				return false;
			}
			return UnpackMap(C, N, Out, Err);
		}
		default:
			Err = FString::Printf(TEXT("unsupported msgpack code 0x%02x"), Code);
			Out.Kind = EKKMsgKind::Other;
			return false;
		}
	}
}

bool UnpackMsgpackSelectedMap(const uint8* Data,int32 Size,TConstArrayView<FString> Keys,FKKMsgValue& Out,FString& OutError)
{
    Out=FKKMsgValue();OutError.Reset();
    if(!Data||Size<=0){OutError=TEXT("msgpack map is empty");return false;}
    FCursor C{Data,Data+Size};uint8 Code=0;uint32 Count=0;uint16 W=0;
    if(!C.ReadByte(Code))return false;
    if((Code&0xf0)==0x80)Count=Code&0x0f;
    else if(Code==0xde&&C.ReadBe16(W))Count=W;
    else if(Code==0xdf&&C.ReadBe32(Count)){}
    else{OutError=TEXT("msgpack root is not a map");return false;}
    if(uint64(Count)*2>uint64(C.End-C.Ptr)){OutError=TEXT("msgpack map count exceeds remaining data");return false;}
    FKKMsgValue Result;Result.Kind=EKKMsgKind::Map;
    for(uint32 I=0;I<Count;++I)
    {
        FKKMsgValue Key;if(!Unpack(C,Key,OutError))return false;
        FString Name;
        if(Key.Kind==EKKMsgKind::String)Name=Key.s;
        else if(Key.Kind==EKKMsgKind::Int)Name=FString::FromInt(int32(Key.i));
        else{OutError=TEXT("msgpack map key is not a string or int");return false;}
        const uint8* Start=C.Ptr;
        if(!SkipValue(C)){OutError=TEXT("msgpack selected map has invalid or truncated value");return false;}
        if(Keys.Contains(Name))
        {
            auto Value=MakeShared<FKKMsgValue>();FCursor ValueCursor{Start,C.Ptr};
            if(!Unpack(ValueCursor,*Value,OutError)||ValueCursor.Ptr!=C.Ptr)return false;
            Result.Map.Add(Name,MoveTemp(Value));Result.MapKeys.Add(Name);
        }
    }
    if(C.Ptr!=C.End){OutError=TEXT("msgpack selected map has trailing bytes");return false;}
    Out=MoveTemp(Result);return true;
}

bool UnpackMsgpack(const uint8* Data, int32 Size, FKKMsgValue& Out, FString& OutError)
{
	FCursor C{Data, Data + Size};
	return Unpack(C, Out, OutError);
}

bool UnpackMsgpackExact(const uint8* Data, int32 Size, FKKMsgValue& Out, FString& OutError)
{
    if (!Data || Size <= 0) { OutError = TEXT("Empty MessagePack"); return false; }
    FCursor C{Data, Data + Size};
    if (!Unpack(C, Out, OutError)) return false;
    if (C.Ptr != C.End) { OutError = TEXT("Unsupported trailing MessagePack data"); return false; }
    return true;
}

namespace
{
	void AppendByte(TArray<uint8>& Out, uint8 B)
	{
		Out.Add(B);
	}

	void AppendBe16(TArray<uint8>& Out, uint16 V)
	{
		Out.Add(static_cast<uint8>(V >> 8));
		Out.Add(static_cast<uint8>(V));
	}

	void AppendBe32(TArray<uint8>& Out, uint32 V)
	{
		Out.Add(static_cast<uint8>(V >> 24));
		Out.Add(static_cast<uint8>(V >> 16));
		Out.Add(static_cast<uint8>(V >> 8));
		Out.Add(static_cast<uint8>(V));
	}

	bool Pack(const FKKMsgValue& Value, TArray<uint8>& Out, FString& Err);

	void PackUtf8(TArray<uint8>& Out, const FString& S)
	{
		FTCHARToUTF8 Conv(*S);
		const int32 N = Conv.Length();
		const uint8* Bytes = reinterpret_cast<const uint8*>(Conv.Get());
		if (N < 32)
		{
			AppendByte(Out, static_cast<uint8>(0xa0 | N));
		}
		else if (N < 256)
		{
			AppendByte(Out, 0xd9);
			AppendByte(Out, static_cast<uint8>(N));
		}
		else
		{
			AppendByte(Out, 0xda);
			AppendBe16(Out, static_cast<uint16>(N));
		}
		Out.Append(Bytes, N);
	}

	void PackInt(TArray<uint8>& Out, int64 V)
	{
		if (V >= 0 && V <= 127)
		{
			AppendByte(Out, static_cast<uint8>(V));
			return;
		}
		if (V >= -32 && V < 0)
		{
			AppendByte(Out, static_cast<uint8>(V));
			return;
		}
		if (V >= -128 && V <= 127)
		{
			AppendByte(Out, 0xd0);
			AppendByte(Out, static_cast<uint8>(V));
			return;
		}
		if (V >= -32768 && V <= 32767)
		{
			AppendByte(Out, 0xd1);
			AppendBe16(Out, static_cast<uint16>(V));
			return;
		}
		AppendByte(Out, 0xd2);
		AppendBe32(Out, static_cast<uint32>(V));
	}

	bool Pack(const FKKMsgValue& Value, TArray<uint8>& Out, FString& Err)
	{
		switch (Value.Kind)
		{
		case EKKMsgKind::Nil:
			AppendByte(Out, 0xc0);
			return true;
		case EKKMsgKind::Bool:
			AppendByte(Out, Value.b ? 0xc3 : 0xc2);
			return true;
		case EKKMsgKind::Int:
			PackInt(Out, Value.i);
			return true;
		case EKKMsgKind::Float:
		{
			const float F = static_cast<float>(Value.f);
			uint32 Bits = 0;
			FMemory::Memcpy(&Bits, &F, 4);
			AppendByte(Out, 0xca);
			AppendBe32(Out, Bits);
			return true;
		}
		case EKKMsgKind::String:
			PackUtf8(Out, Value.s);
			return true;
		case EKKMsgKind::Bin:
		{
			const int32 N = Value.Bin.Num();
			if (N < 256)
			{
				AppendByte(Out, 0xc4);
				AppendByte(Out, static_cast<uint8>(N));
			}
			else
			{
				AppendByte(Out, 0xc5);
				AppendBe16(Out, static_cast<uint16>(N));
			}
			Out.Append(Value.Bin);
			return true;
		}
		case EKKMsgKind::Array:
		{
			const int32 N = Value.Array.Num();
			if (N < 16)
			{
				AppendByte(Out, static_cast<uint8>(0x90 | N));
			}
			else
			{
				AppendByte(Out, 0xdc);
				AppendBe16(Out, static_cast<uint16>(N));
			}
			for (const TSharedPtr<FKKMsgValue>& Item : Value.Array)
			{
				if (!Item.IsValid() || !Pack(*Item, Out, Err))
				{
					return false;
				}
			}
			return true;
		}
		case EKKMsgKind::Map:
		{
			TArray<FString> Keys = Value.MapKeys;
			if (Keys.Num() == 0)
			{
				Value.Map.GetKeys(Keys);
			}
			const int32 N = Keys.Num();
			if (N < 16)
			{
				AppendByte(Out, static_cast<uint8>(0x80 | N));
			}
			else
			{
				AppendByte(Out, 0xde);
				AppendBe16(Out, static_cast<uint16>(N));
			}
			for (const FString& Key : Keys)
			{
				PackUtf8(Out, Key);
				const TSharedPtr<FKKMsgValue>* Child = Value.Map.Find(Key);
				if (!Child || !Child->IsValid() || !Pack(*Child->Get(), Out, Err))
				{
					Err = TEXT("msgpack pack missing map value");
					return false;
				}
			}
			return true;
		}
		default:
			Err = TEXT("cannot pack Other msgpack value");
			return false;
		}
	}
}

bool PackMsgpack(const FKKMsgValue& Value, TArray<uint8>& Out, FString& OutError)
{
	Out.Reset();
	return Pack(Value, Out, OutError);
}

void MsgSetString(FKKMsgValue& MapValue, const TCHAR* Key, const FString& StringValue)
{
	if (MapValue.Kind != EKKMsgKind::Map)
	{
		MapValue.Kind = EKKMsgKind::Map;
	}
	TSharedPtr<FKKMsgValue>& Slot = MapValue.Map.FindOrAdd(Key);
	if (!Slot.IsValid())
	{
		Slot = MakeShared<FKKMsgValue>();
		MapValue.MapKeys.Add(Key);
	}
	Slot->Kind = EKKMsgKind::String;
	Slot->s = StringValue;
}

bool UnpackMapish(const uint8* Data, int32 Size, FKKMsgValue& Out, FString& OutError)
{
	if (Size <= 0 || !Data)
	{
		OutError = TEXT("empty msgpack");
		return false;
	}
	if (UnpackMsgpack(Data, Size, Out, OutError)
		&& (Out.Kind == EKKMsgKind::Map || Out.Kind == EKKMsgKind::Array))
	{
		return true;
	}
	if (Size > 4
		&& UnpackMsgpack(Data + 4, Size - 4, Out, OutError)
		&& (Out.Kind == EKKMsgKind::Map || Out.Kind == EKKMsgKind::Array))
	{
		return true;
	}
	return false;
}

void SplitLenPrefixedMaps(const TArray<uint8>& Raw, TArray<FKKMsgValue>& OutParts)
{
	OutParts.Reset();
	int32 Pos = 0;
	while (Pos + 4 <= Raw.Num())
	{
		int32 N = 0;
		FMemory::Memcpy(&N, Raw.GetData() + Pos, 4);
		if (N <= 0 || Pos + 4 + N > Raw.Num())
		{
			break;
		}
		Pos += 4;
		FKKMsgValue Part;
		FString Err;
		if (UnpackMsgpack(Raw.GetData() + Pos, N, Part, Err) && Part.Kind == EKKMsgKind::Map)
		{
			OutParts.Add(MoveTemp(Part));
		}
		Pos += N;
	}
}

bool PackLenPrefixedMaps(const TArray<FKKMsgValue>& Parts, TArray<uint8>& Out, FString& OutError)
{
	Out.Reset();
	for (const FKKMsgValue& Part : Parts)
	{
		TArray<uint8> Packed;
		if (!PackMsgpack(Part, Packed, OutError))
		{
			return false;
		}
		const int32 N = Packed.Num();
		const int32 Start = Out.Num();
		Out.AddUninitialized(4 + N);
		FMemory::Memcpy(Out.GetData() + Start, &N, 4);
		FMemory::Memcpy(Out.GetData() + Start + 4, Packed.GetData(), N);
	}
	return true;
}

void MsgSetFloatArray(FKKMsgValue& MapValue, const TCHAR* Key, const TArray<float>& Values)
{
	if (MapValue.Kind != EKKMsgKind::Map)
	{
		MapValue.Kind = EKKMsgKind::Map;
	}
	TSharedPtr<FKKMsgValue>& Slot = MapValue.Map.FindOrAdd(Key);
	if (!Slot.IsValid())
	{
		Slot = MakeShared<FKKMsgValue>();
		MapValue.MapKeys.Add(Key);
	}
	Slot->Kind = EKKMsgKind::Array;
	Slot->Array.Reset();
	for (float V : Values)
	{
		TSharedPtr<FKKMsgValue> Item = MakeShared<FKKMsgValue>();
		Item->Kind = EKKMsgKind::Float;
		Item->f = V;
		Slot->Array.Add(Item);
	}
}
