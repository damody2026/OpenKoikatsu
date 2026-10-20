#include "KKCardWriter.h"
#include "KKCardReader.h"
#include "KKMsgpack.h"
#include "KKPng.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
	TSharedPtr<FKKMsgValue> MakeInt(int32 V)
	{
		TSharedPtr<FKKMsgValue> N = MakeShared<FKKMsgValue>();
		N->Kind = EKKMsgKind::Int;
		N->i = V;
		return N;
	}

	TSharedPtr<FKKMsgValue> MakeStr(const FString& S)
	{
		TSharedPtr<FKKMsgValue> N = MakeShared<FKKMsgValue>();
		N->Kind = EKKMsgKind::String;
		N->s = S;
		return N;
	}

	void SetBodyPhysics(FKKMsgValue& Body,const FKKBody& Values){
  for(auto Pair:{TPair<const TCHAR*,float>(TEXT("bustSoftness"),Values.BustSoftness),TPair<const TCHAR*,float>(TEXT("bustWeight"),Values.BustWeight)}){
   auto Value=MakeShared<FKKMsgValue>();Value->Kind=EKKMsgKind::Float;Value->f=Pair.Value;Body.Map.Add(Pair.Key,Value);Body.MapKeys.AddUnique(Pair.Key);
  }
 }
 bool PackLstInfo(const TArray<FKKBlockInfo>& Blocks, const TArray<uint8>& OriginalTable, TArray<uint8>& Out, FString& Err)
	{
		FKKMsgValue Root;
		Root.Kind = EKKMsgKind::Map;
		if (OriginalTable.Num() > 0 && !UnpackMsgpackExact(OriginalTable.GetData(), OriginalTable.Num(), Root, Err)) return false;
		TMap<FString, TSharedPtr<FKKMsgValue>> OriginalItems;
		if (const FKKMsgValue* Lst = Root.Find(TEXT("lstInfo")))
		{
			for (const TSharedPtr<FKKMsgValue>& Item : Lst->Array)
				if (Item.IsValid()) if (const FKKMsgValue* Name = Item->Find(TEXT("name"))) OriginalItems.Add(Name->s, Item);
		}
		TSharedPtr<FKKMsgValue> List = MakeShared<FKKMsgValue>();
		List->Kind = EKKMsgKind::Array;
		for (const FKKBlockInfo& Block : Blocks)
		{
			TSharedPtr<FKKMsgValue> Item = MakeShared<FKKMsgValue>();
			if (const TSharedPtr<FKKMsgValue>* Existing = OriginalItems.Find(Block.Name)) *Item = *Existing->Get();
			Item->Kind = EKKMsgKind::Map;
			Item->Map.Add(TEXT("name"), MakeStr(Block.Name));
			Item->MapKeys.AddUnique(TEXT("name"));
			Item->Map.Add(TEXT("pos"), MakeInt(Block.Pos));
			Item->MapKeys.AddUnique(TEXT("pos"));
			Item->Map.Add(TEXT("size"), MakeInt(Block.Size));
			Item->MapKeys.AddUnique(TEXT("size"));
			List->Array.Add(Item);
		}
		Root.Map.Add(TEXT("lstInfo"), List);
		Root.MapKeys.AddUnique(TEXT("lstInfo"));
		return PackMsgpack(Root, Out, Err);
	}

	bool RebuildFileBytes(FKKCharacterFile& File, TArray<uint8>& Out, FString& Err)
	{
		TArray<FKKBlockInfo> NewBlocks;
		TArray<TArray<uint8>> NewPayloads;
		int32 Pos = 0;
		for (const FKKBlockInfo& Old : File.Blocks)
		{
			const TArray<uint8>* Raw = File.RawBlocks.Find(Old.Name);
			if (!Raw)
			{
				Err = FString::Printf(TEXT("missing raw block %s"), *Old.Name);
				return false;
			}
			FKKBlockInfo Info = Old;
			Info.Pos = Pos;
			Info.Size = Raw->Num();
			Pos += Info.Size;
			NewBlocks.Add(Info);
			NewPayloads.Add(*Raw);
		}

		TArray<uint8> HeaderBytes;
		if (!PackLstInfo(NewBlocks, File.Header.RawBlockTable, HeaderBytes, Err))
		{
			return false;
		}

		Out.Reset();
		if (File.Header.PreviewPng.Num() > 0)
		{
			Out.Append(File.Header.PreviewPng);
		}
		else if (File.OriginalBytes.Num() > 0 && File.Header.PngEndOffset > 0)
		{
			Out.Append(File.OriginalBytes.GetData(), File.Header.PngEndOffset);
		}
		else
		{
			Err = TEXT("no preview png to write");
			return false;
		}

		WriteInt32LE(Out, File.Header.ProductNo);
		Write7BitString(Out, File.Header.Marker);
		Write7BitString(Out, File.Header.Version);
		WriteInt32LE(Out, File.Header.FacePng.Num());
		Out.Append(File.Header.FacePng);
		WriteInt32LE(Out, HeaderBytes.Num());
		Out.Append(HeaderBytes);
		WriteInt64LE(Out, static_cast<int64>(Pos));
		for (const TArray<uint8>& Part : NewPayloads)
		{
			Out.Append(Part);
		}

		File.Blocks = NewBlocks;
		File.Header.RawBlockTable = HeaderBytes;
		return true;
	}
}

bool UKKCardWriter::SaveUnchanged(const FKKCharacterFile& File, const FString& Path, FString& OutError)
{
	if (File.OriginalBytes.Num() == 0)
	{
		OutError = TEXT("no original bytes to write");
		return false;
	}
    if (File.DeferredPreview)
    {
        // SaveUnchanged preserves the serialized parameter baseline rather than
        // applying pending typed controls, but must still resolve its preview.
        FKKCharacterFile Unchanged;
        if (!UKKCardReader::LoadFromBytes(File.OriginalBytes, Unchanged, OutError)) return false;
        Unchanged.DeferredPreview = File.DeferredPreview;
        return SaveWithEdits(Unchanged, Path, OutError);
    }
	if (!FFileHelper::SaveArrayToFile(File.OriginalBytes, *Path))
	{
		OutError = FString::Printf(TEXT("failed to write %s"), *Path);
		return false;
	}
	return true;
}

bool UKKCardWriter::SaveWithEdits(FKKCharacterFile& SourceFile, const FString& Path, FString& OutError)
{
    OutError.Reset();
    FKKCharacterFile File = SourceFile;
    // Work on the transaction copy: an export or write failure must leave the
    // source card's pinned revision available for retry. Never replace current
    // parameter blocks or typed edits with metadata from an older checkpoint.
    if (File.DeferredPreview)
    {
        TArray<uint8> Preview;
        if (!File.DeferredPreview->BuildPreviewPng(Preview, OutError)) return false;
        static const uint8 Signature[] = {137,80,78,71,13,10,26,10};
        if (Preview.Num() < UE_ARRAY_COUNT(Signature) ||
            FMemory::Memcmp(Preview.GetData(), Signature, UE_ARRAY_COUNT(Signature)) != 0)
        { OutError = TEXT("Deferred preview did not produce a PNG"); return false; }
        File.Header.PreviewPng = MoveTemp(Preview);
        File.Header.PngEndOffset = File.Header.PreviewPng.Num();
        File.DeferredPreview.Reset();
    }
    if(!FMath::IsFinite(File.Body.BustSoftness)||!FMath::IsFinite(File.Body.BustWeight)){OutError=TEXT("Nonfinite body physics value");return false;}
    for (const TArray<float>* Values : { &File.Body.ShapeValueBody, &File.Face.ShapeValueFace })
    {
        for (float Value : *Values) if (!FMath::IsFinite(Value))
        { OutError = TEXT("Nonfinite shape value"); return false; }
    }
    FKKCharacterFile Original;
    bool bBodyChanged = File.Body.ShapeValueBody.Num() > 0;
    bool bFaceChanged = File.Face.ShapeValueFace.Num() > 0;
    if (File.OriginalBytes.Num() > 0)
    {
        if (!UKKCardReader::LoadFromBytes(File.OriginalBytes, Original, OutError)) return false;
        bBodyChanged = File.Body.ShapeValueBody != Original.Body.ShapeValueBody || File.Body.BustSoftness!=Original.Body.BustSoftness || File.Body.BustWeight!=Original.Body.BustWeight;
        bFaceChanged = File.Face.ShapeValueFace != Original.Face.ShapeValueFace;
    }
	FKKMsgValue Parameter;
	const TArray<uint8>* RawParam = File.RawBlocks.Find(TEXT("Parameter"));
	if (RawParam)
	{
		if (!UnpackMsgpackExact(RawParam->GetData(), RawParam->Num(), Parameter, OutError))
		{
			return false;
		}
	}
	else
	{
		Parameter.Kind = EKKMsgKind::Map;
	}

	MsgSetString(Parameter, TEXT("lastname"), File.LastName);
	MsgSetString(Parameter, TEXT("firstname"), File.FirstName);
	MsgSetString(Parameter, TEXT("nickname"), File.Nickname);

    if (bBodyChanged || bFaceChanged)
    {
        const TArray<uint8>* CustomRaw = File.RawBlocks.Find(TEXT("Custom"));
        if (!CustomRaw) { OutError = TEXT("Missing Custom block for shape edits"); return false; }
        FKKMsgValue Custom;
        FString ParseError;
        bool bWrapped = false;
        bool bNested = UnpackMsgpackExact(CustomRaw->GetData(), CustomRaw->Num(), Custom, ParseError)
            && Custom.Kind == EKKMsgKind::Map && Custom.Find(TEXT("face"));
        if (!bNested && CustomRaw->Num() > 4)
        {
            const uint8* D = CustomRaw->GetData();
            const uint32 Length = uint32(D[0]) | (uint32(D[1]) << 8) | (uint32(D[2]) << 16) | (uint32(D[3]) << 24);
            bWrapped = Length == uint32(CustomRaw->Num() - 4) &&
                UnpackMsgpackExact(D + 4, CustomRaw->Num() - 4, Custom, ParseError) &&
                Custom.Kind == EKKMsgKind::Map && Custom.Find(TEXT("face"));
            bNested = bWrapped;
        }
        TArray<uint8> PackedCustom;
        if (bNested)
        {
            for (const TCHAR* Key : { TEXT("face"), TEXT("body") })
            {
                const bool bBody = FCString::Strcmp(Key, TEXT("body")) == 0;
                if (!(bBody ? bBodyChanged : bFaceChanged)) continue;
                TSharedPtr<FKKMsgValue>* Part = Custom.Map.Find(Key);
                if (!Part || !Part->IsValid() || (*Part)->Kind != EKKMsgKind::Map)
                { OutError = TEXT("Unsupported Custom shape map"); return false; }
                MsgSetFloatArray(*Part->Get(), bBody ? TEXT("shapeValueBody") : TEXT("shapeValueFace"),
                    bBody ? File.Body.ShapeValueBody : File.Face.ShapeValueFace);
                if(bBody)SetBodyPhysics(*Part->Get(),File.Body);
            }
            TArray<uint8> Payload;
            if (!PackMsgpack(Custom, Payload, OutError)) return false;
            if (bWrapped) WriteInt32LE(PackedCustom, Payload.Num());
            PackedCustom.Append(Payload);
        }
        else
        {
            TArray<FKKMsgValue> Parts;
            int32 At=0;
            while (At<CustomRaw->Num())
            {
                if (CustomRaw->Num()-At<4) {OutError=TEXT("Unsupported trailing Custom data");return false;}
                const uint8* D=CustomRaw->GetData()+At;
                const uint32 Length=uint32(D[0])|(uint32(D[1])<<8)|(uint32(D[2])<<16)|(uint32(D[3])<<24);
                if (Length==0 || Length>uint32(CustomRaw->Num()-At-4)) {OutError=TEXT("Invalid Custom part length");return false;}
                FKKMsgValue Part;
                if (!UnpackMsgpackExact(D+4,int32(Length),Part,OutError)||Part.Kind!=EKKMsgKind::Map) return false;
                Parts.Add(MoveTemp(Part));At+=4+int32(Length);
            }
            if (Parts.Num() < 3 || Parts[0].Kind != EKKMsgKind::Map || Parts[1].Kind != EKKMsgKind::Map)
            { OutError = TEXT("Unsupported Custom layout; shape edits were not saved"); return false; }
            if (bFaceChanged) MsgSetFloatArray(Parts[0], TEXT("shapeValueFace"), File.Face.ShapeValueFace);
            if (bBodyChanged){ MsgSetFloatArray(Parts[1], TEXT("shapeValueBody"), File.Body.ShapeValueBody);SetBodyPhysics(Parts[1],File.Body);}
            if (!PackLenPrefixedMaps(Parts, PackedCustom, OutError)) return false;
        }
        File.RawBlocks.Add(TEXT("Custom"), MoveTemp(PackedCustom));
    }

	TArray<uint8> PackedParam;
	if (!PackMsgpack(Parameter, PackedParam, OutError))
	{
		return false;
	}
	File.RawBlocks.Add(TEXT("Parameter"), PackedParam);
	if (!File.Blocks.ContainsByPredicate([](const FKKBlockInfo& B) { return B.Name == TEXT("Parameter"); }))
	{
		FKKBlockInfo Info;
		Info.Name = TEXT("Parameter");
		File.Blocks.Add(Info);
	}

	TArray<uint8> OutBytes;
	if (!RebuildFileBytes(File, OutBytes, OutError))
	{
		return false;
	}
	if (!FFileHelper::SaveArrayToFile(OutBytes, *Path))
	{
		OutError = FString::Printf(TEXT("failed to write %s"), *Path);
		return false;
	}
	File.OriginalBytes = OutBytes;
	SourceFile = MoveTemp(File);
	return true;
}

TArray<FKKCardListEntry> UKKCardLibrary::ScanFolder(const FString& Folder)
{
	TArray<FKKCardListEntry> Result;
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*.png"), true, false);
	Files.Sort();
	for (const FString& Path : Files)
	{
		FKKCharacterFile File;
		FString Err;
		if (!UKKCardReader::LoadFromFile(Path, File, Err))
		{
			continue;
		}
		if (File.Header.Kind != EKKCardKind::KK
			&& File.Header.Kind != EKKCardKind::KKS
			&& File.Header.Kind != EKKCardKind::KKSP)
		{
			continue;
		}
		FKKCardListEntry Entry;
		Entry.Path = Path;
		Entry.FileName = FPaths::GetCleanFilename(Path);
		Entry.Kind = File.Header.Kind;
		Entry.LastName = File.LastName;
		Entry.FirstName = File.FirstName;
		Entry.Nickname = File.Nickname;
		Entry.Sex = File.Sex;
		Entry.Personality = File.Personality;
		Result.Add(MoveTemp(Entry));
	}
	return Result;
}
