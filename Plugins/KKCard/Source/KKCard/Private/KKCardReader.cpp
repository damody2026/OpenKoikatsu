#include "KKCardReader.h"
#include "KKPng.h"
#include "KKMsgpack.h"
#include "Misc/FileHelper.h"

namespace
{
	int32 MsgInt(const FKKMsgValue* Value, int32 Default = 0)
	{
		return Value ? Value->AsInt(Default) : Default;
	}

	FString MsgStr(const FKKMsgValue* Value)
	{
		return (Value && Value->Kind == EKKMsgKind::String) ? Value->s : FString();
	}

	void ReadFloatArray(const FKKMsgValue* Value, TArray<float>& Out)
	{
		Out.Reset();
		if (!Value || Value->Kind != EKKMsgKind::Array)
		{
			return;
		}
		Out.Reserve(Value->Array.Num());
		for (const TSharedPtr<FKKMsgValue>& Item : Value->Array)
		{
			Out.Add(Item.IsValid() ? Item->AsFloat() : 0.0f);
		}
	}

	void ParseFace(const FKKMsgValue* Face, FKKFace& Out)
	{
		if (!Face || Face->Kind != EKKMsgKind::Map)
		{
			return;
		}
		ReadFloatArray(Face->Find(TEXT("shapeValueFace")), Out.ShapeValueFace);
		Out.HeadId = MsgInt(Face->Find(TEXT("headId")));
		Out.SkinId = MsgInt(Face->Find(TEXT("skinId")));
		Out.DetailId = MsgInt(Face->Find(TEXT("detailId")));
		Out.EyebrowId = MsgInt(Face->Find(TEXT("eyebrowId")));
		Out.NoseId = MsgInt(Face->Find(TEXT("noseId")));
		Out.HlUpId = MsgInt(Face->Find(TEXT("hlUpId")));
		Out.HlDownId = MsgInt(Face->Find(TEXT("hlDownId")));
		Out.EyelineUpId = MsgInt(Face->Find(TEXT("eyelineUpId")));
		Out.EyelineDownId = MsgInt(Face->Find(TEXT("eyelineDownId")));
		Out.WhiteId = MsgInt(Face->Find(TEXT("whiteId")));
		Out.MoleId = MsgInt(Face->Find(TEXT("moleId")));
		Out.LipLineId = MsgInt(Face->Find(TEXT("lipLineId")));
		Out.ForegroundEyes = MsgInt(Face->Find(TEXT("foregroundEyes")));
		Out.ForegroundEyebrow = MsgInt(Face->Find(TEXT("foregroundEyebrow")));
	}

	void ParseBody(const FKKMsgValue* Body, FKKBody& Out)
	{
		if (!Body || Body->Kind != EKKMsgKind::Map)
		{
			return;
		}
		ReadFloatArray(Body->Find(TEXT("shapeValueBody")), Out.ShapeValueBody);
  if(const auto* V=Body->Find(TEXT("bustSoftness")))Out.BustSoftness=V->AsFloat(.5f);
  if(const auto* V=Body->Find(TEXT("bustWeight")))Out.BustWeight=V->AsFloat(.5f);
		Out.SkinId = MsgInt(Body->Find(TEXT("skinId")));
		Out.DetailId = MsgInt(Body->Find(TEXT("detailId")));
	}

	FKKHairPart ParseHairPart(const FKKMsgValue* Part)
	{
		FKKHairPart Out;
		if (Part && Part->Kind == EKKMsgKind::Map)
		{
			Out.Id = MsgInt(Part->Find(TEXT("id")));
		}
		return Out;
	}

	void ParseHair(const FKKMsgValue* Hair, FKKHair& Out)
	{
		if (!Hair || Hair->Kind != EKKMsgKind::Map)
		{
			return;
		}
		if (const FKKMsgValue* Parts = Hair->Find(TEXT("parts")))
		{
			if (Parts->Kind == EKKMsgKind::Array)
			{
				if (Parts->Array.Num() > 0)
				{
					Out.Front = ParseHairPart(Parts->Array[0].Get());
				}
				if (Parts->Array.Num() > 1)
				{
					Out.Back = ParseHairPart(Parts->Array[1].Get());
				}
				if (Parts->Array.Num() > 2)
				{
					Out.Side = ParseHairPart(Parts->Array[2].Get());
				}
				if (Parts->Array.Num() > 3)
				{
					Out.Extension = ParseHairPart(Parts->Array[3].Get());
				}
			}
		}
	}

	void ParseParameter(const FKKMsgValue& Root, FKKCharacterFile& Out)
	{
		Out.LastName = MsgStr(Root.Find(TEXT("lastname")));
		Out.FirstName = MsgStr(Root.Find(TEXT("firstname")));
		Out.Nickname = MsgStr(Root.Find(TEXT("nickname")));
		Out.Sex = MsgInt(Root.Find(TEXT("sex")), 1);
		Out.Personality = MsgInt(Root.Find(TEXT("personality")));
		const FKKMsgValue* Rate=Root.Find(TEXT("voiceRate"));
		const float VoiceRate=Rate?Rate->AsFloat(.5f):.5f;
		Out.VoiceRate=FMath::IsFinite(VoiceRate)?FMath::Clamp(VoiceRate,0.f,1.f):.5f;
	}

	void ParseCoordinate(const FKKMsgValue& Root, TArray<FKKCoordinateSlot>& Out)
	{
		const TArray<TSharedPtr<FKKMsgValue>>* Slots = nullptr;
		TArray<TSharedPtr<FKKMsgValue>> FromMap;
		if (Root.Kind == EKKMsgKind::Array)
		{
			Slots = &Root.Array;
		}
		else if (Root.Kind == EKKMsgKind::Map)
		{
			if (const FKKMsgValue* Inner = Root.Find(TEXT("clothes")))
			{
				FromMap.Add(MakeShared<FKKMsgValue>(Root));
				Slots = &FromMap;
			}
			else
			{
				for (int32 i = 0; i < 8; ++i)
				{
					if (const FKKMsgValue* Slot = Root.Find(*FString::FromInt(i)))
					{
						FromMap.Add(MakeShared<FKKMsgValue>(*Slot));
					}
				}
				Slots = &FromMap;
			}
		}
		if (!Slots)
		{
			return;
		}
		for (const TSharedPtr<FKKMsgValue>& SlotPtr : *Slots)
		{
			if (!SlotPtr.IsValid())
			{
				continue;
			}
			FKKMsgValue SlotValue = *SlotPtr;
			if (SlotValue.Kind == EKKMsgKind::Bin)
			{
				FString NestedErr;
				UnpackMapish(SlotValue.Bin.GetData(), SlotValue.Bin.Num(), SlotValue, NestedErr);
			}
			if (SlotValue.Kind != EKKMsgKind::Map)
			{
				continue;
			}
			const FKKMsgValue* SlotObj = &SlotValue;
			FKKCoordinateSlot Slot;
			const FKKMsgValue* Parts = nullptr;
			if (const FKKMsgValue* Clothes = SlotObj->Find(TEXT("clothes")))
			{
				Parts = Clothes->Find(TEXT("parts"));
			}
			if (!Parts)
			{
				Parts = SlotObj->Find(TEXT("parts"));
			}
			if (Parts && Parts->Kind == EKKMsgKind::Array)
			{
				for (const TSharedPtr<FKKMsgValue>& Part : Parts->Array)
				{
					FKKClothesPart Item;
					if (Part.IsValid() && Part->Kind == EKKMsgKind::Map)
					{
						Item.Id = MsgInt(Part->Find(TEXT("id")));
					}
					Slot.Clothes.Add(Item);
				}
			}
			if (const FKKMsgValue* Acc = SlotObj->Find(TEXT("accessory")))
			{
				const FKKMsgValue* AccParts = Acc->Find(TEXT("parts"));
				if (AccParts && AccParts->Kind == EKKMsgKind::Array)
				{
					for (const TSharedPtr<FKKMsgValue>& Part : AccParts->Array)
					{
						FKKAccessoryPart Item;
						if (Part.IsValid() && Part->Kind == EKKMsgKind::Map)
						{
							Item.Type = MsgInt(Part->Find(TEXT("type")));
							Item.Id = MsgInt(Part->Find(TEXT("id")));
							Item.bUsed = Item.Id != 0;
						}
						Slot.Accessories.Add(Item);
					}
				}
			}
			Out.Add(MoveTemp(Slot));
		}
	}

	void ParseResolveBlob(const TArray<uint8>& Blob, TArray<FKKResolveInfo>& Out)
	{
		FKKMsgValue Root;
		FString Err;
		if (!UnpackMsgpack(Blob.GetData(), Blob.Num(), Root, Err) || Root.Kind != EKKMsgKind::Map)
		{
			return;
		}
		FKKResolveInfo Info;
		Info.ModID = MsgStr(Root.Find(TEXT("ModID")));
		Info.Slot = MsgInt(Root.Find(TEXT("Slot")));
		Info.LocalSlot = MsgInt(Root.Find(TEXT("LocalSlot")));
		Info.Property = MsgStr(Root.Find(TEXT("Property")));
		Info.CategoryNo = MsgInt(Root.Find(TEXT("CategoryNo")));
		Info.Author = MsgStr(Root.Find(TEXT("Author")));
		Info.Name = MsgStr(Root.Find(TEXT("Name")));
		if (!Info.ModID.IsEmpty())
		{
			Out.Add(MoveTemp(Info));
		}
	}

	// KK_SkinEffects' card data: PluginData [version, {HymenRegen, StretchedHymen, FragileVag}].
	void ParseSkinEffects(const FKKMsgValue& Root, FKKCharacterFile& Out)
	{
		const FKKMsgValue* Plugin = Root.Find(TEXT("Marco.SkinEffects"));
		if (!Plugin)
		{
			return;
		}
		const FKKMsgValue* Data = Plugin->Kind == EKKMsgKind::Array && Plugin->Array.Num() >= 2 ? Plugin->Array[1].Get()
			: Plugin->Kind == EKKMsgKind::Map ? (Plugin->Find(TEXT("data")) ? Plugin->Find(TEXT("data")) : Plugin->Find(TEXT("1"))) : nullptr;
		if (!Data || Data->Kind != EKKMsgKind::Map)
		{
			return;
		}
		auto Flag = [Data](const TCHAR* Key) { const FKKMsgValue* V = Data->Find(Key); return V && V->Kind == EKKMsgKind::Bool && V->b; };
		Out.bHymenRegen = Flag(TEXT("HymenRegen"));
		Out.bStretchedHymen = Flag(TEXT("StretchedHymen"));
		Out.bFragileVag = Flag(TEXT("FragileVag"));
	}

	void ParseKKEx(const FKKMsgValue& Root, TArray<FKKResolveInfo>& Out)
	{
		static const TCHAR* UarKeys[] = {
			TEXT("com.bepis.sideloader.universalautoresolver"),
			TEXT("EC.Core.Sideloader.UniversalAutoResolver")
		};
		const FKKMsgValue* Plugin = nullptr;
		for (const TCHAR* Key : UarKeys)
		{
			Plugin = Root.Find(Key);
			if (Plugin)
			{
				break;
			}
		}
		if (!Plugin)
		{
			return;
		}
		const FKKMsgValue* Data = Plugin;
		if (Plugin->Kind == EKKMsgKind::Array && Plugin->Array.Num() >= 2)
		{
			Data = Plugin->Array[1].Get();
		}
		else if (Plugin->Kind == EKKMsgKind::Map)
		{
			if (const FKKMsgValue* Inner = Plugin->Find(TEXT("1")))
			{
				Data = Inner;
			}
		}
		if (!Data)
		{
			return;
		}
		const FKKMsgValue* InfoList = (Data->Kind == EKKMsgKind::Map) ? Data->Find(TEXT("info")) : nullptr;
		if (!InfoList || InfoList->Kind != EKKMsgKind::Array)
		{
			return;
		}
		for (const TSharedPtr<FKKMsgValue>& Item : InfoList->Array)
		{
			if (!Item.IsValid())
			{
				continue;
			}
			if (Item->Kind == EKKMsgKind::Bin)
			{
				ParseResolveBlob(Item->Bin, Out);
			}
			else if (Item->Kind == EKKMsgKind::Map)
			{
				FKKResolveInfo Info;
				Info.ModID = MsgStr(Item->Find(TEXT("ModID")));
				Info.Slot = MsgInt(Item->Find(TEXT("Slot")));
				Info.LocalSlot = MsgInt(Item->Find(TEXT("LocalSlot")));
				Info.Property = MsgStr(Item->Find(TEXT("Property")));
				Info.CategoryNo = MsgInt(Item->Find(TEXT("CategoryNo")));
				Info.Author = MsgStr(Item->Find(TEXT("Author")));
				Info.Name = MsgStr(Item->Find(TEXT("Name")));
				if (!Info.ModID.IsEmpty())
				{
					Out.Add(MoveTemp(Info));
				}
			}
		}
	}

	bool UnpackNamed(const TMap<FString, TArray<uint8>>& Raw, const TCHAR* Name, FKKMsgValue& Out, FString& Err)
	{
		const TArray<uint8>* Block = Raw.Find(Name);
		if (!Block)
		{
			return false;
		}
		return UnpackMsgpack(Block->GetData(), Block->Num(), Out, Err);
	}
}

bool UKKCardReader::LoadHeaderFromFile(
	const FString& Path,
	FKKCardHeader& OutHeader,
	TArray<FKKBlockInfo>& OutBlocks,
	FString& OutError)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		OutError = FString::Printf(TEXT("failed to read %s"), *Path);
		return false;
	}
	return ParseCardHeader(Bytes, OutHeader, OutBlocks, OutError);
}

bool UKKCardReader::LoadFromBytes(const TArray<uint8>& Bytes, FKKCharacterFile& OutFile, FString& OutError)
{
	OutFile = FKKCharacterFile();
	OutFile.OriginalBytes = Bytes;
	if (!ParseCardHeader(Bytes, OutFile.Header, OutFile.Blocks, OutFile.RawBlocks, OutError))
	{
		return false;
	}

	FKKMsgValue Custom;
	const TArray<uint8>* CustomRaw = OutFile.RawBlocks.Find(TEXT("Custom"));
	if (CustomRaw && UnpackMapish(CustomRaw->GetData(), CustomRaw->Num(), Custom, OutError) && Custom.Kind == EKKMsgKind::Map && Custom.Find(TEXT("face")))
	{
		ParseFace(Custom.Find(TEXT("face")), OutFile.Face);
		ParseBody(Custom.Find(TEXT("body")), OutFile.Body);
		ParseHair(Custom.Find(TEXT("hair")), OutFile.Hair);
	}
	else if (CustomRaw)
	{
		TArray<FKKMsgValue> Parts;
		SplitLenPrefixedMaps(*CustomRaw, Parts);
		if (Parts.IsValidIndex(0))
		{
			ParseFace(&Parts[0], OutFile.Face);
		}
		if (Parts.IsValidIndex(1))
		{
			ParseBody(&Parts[1], OutFile.Body);
		}
		if (Parts.IsValidIndex(2))
		{
			ParseHair(&Parts[2], OutFile.Hair);
		}
	}

	FKKMsgValue Parameter;
	if (UnpackNamed(OutFile.RawBlocks, TEXT("Parameter"), Parameter, OutError))
	{
		ParseParameter(Parameter, OutFile);
	}

	FKKMsgValue Coordinate;
	if (UnpackNamed(OutFile.RawBlocks, TEXT("Coordinate"), Coordinate, OutError))
	{
		ParseCoordinate(Coordinate, OutFile.Outfits);
	}

	FKKMsgValue KKEx;
	const TArray<uint8>* Extended=OutFile.RawBlocks.Find(TEXT("KKEx"));
    const TArray<FString> ResolveKeys={TEXT("com.bepis.sideloader.universalautoresolver"),TEXT("EC.Core.Sideloader.UniversalAutoResolver"),TEXT("Marco.SkinEffects")};
	if (Extended && UnpackMsgpackSelectedMap(Extended->GetData(),Extended->Num(),ResolveKeys,KKEx,OutError))
	{
		ParseKKEx(KKEx, OutFile.Resolves);
		ParseSkinEffects(KKEx, OutFile);
	}

	OutError.Empty();
	return true;
}

bool UKKCardReader::LoadFromFile(const FString& Path, FKKCharacterFile& OutFile, FString& OutError)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		OutError = FString::Printf(TEXT("failed to read %s"), *Path);
		return false;
	}
	return LoadFromBytes(Bytes, OutFile, OutError);
}
