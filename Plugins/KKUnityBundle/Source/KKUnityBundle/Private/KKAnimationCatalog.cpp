#include "KKAnimationCatalog.h"
#include "Serialization/JsonSerializer.h"

UKKAnimationCatalog* UKKAnimationCatalog::LoadDefaultCatalog()
{
	return LoadObject<UKKAnimationCatalog>(nullptr,
		TEXT("/Game/Koikatsu/Data/DA_AnimationAndMouth.DA_AnimationAndMouth"), nullptr, LOAD_NoWarn);
}

const FKKExportCatalogTable* UKKAnimationCatalog::FindTable(FName Name) const
{
	return Tables.FindByPredicate([Name](const FKKExportCatalogTable& Table) { return Table.Name == Name; });
}

const FKKExportCatalogRow* UKKAnimationCatalog::FindRow(FName Table, FName Row) const
{
	const FKKExportCatalogTable* Found = FindTable(Table);
	return Found && Found->Data ? Found->Data->FindRow<FKKExportCatalogRow>(Row, TEXT("KK catalog"), false) : nullptr;
}

int32 UKKAnimationCatalog::GetRowCount(FName Table) const
{
	const FKKExportCatalogTable* Found = FindTable(Table);
	return Found ? Found->RowOrder.Num() : 0;
}

FString UKKAnimationCatalog::GetCell(FName Table, FName Row, const FString& Column) const
{
	const FKKExportCatalogTable* Found = FindTable(Table);
	const FKKExportCatalogRow* Entry = FindRow(Table, Row);
	const int32 Index = Found ? Found->Columns.Find(Column) : INDEX_NONE;
	return Entry && Entry->Values.IsValidIndex(Index) ? Entry->Values[Index] : FString();
}

TArray<FName> UKKAnimationCatalog::FindRows(FName Table, const FString& Column, const FString& Value) const
{
	TArray<FName> Result;
	const FKKExportCatalogTable* Found = FindTable(Table);
	if (!Found) return Result;
	for (FName Row : Found->RowOrder)
	{
		if (GetCell(Table, Row, Column) == Value) Result.Add(Row);
	}
	return Result;
}

TArray<FString> UKKAnimationCatalog::GetAnimationSources() const
{
	TArray<FString> Result;
	const FKKExportCatalogTable* Found = FindTable(TEXT("animation_sources"));
	if (!Found) return Result;
	for (FName Row : Found->RowOrder)
	{
		const FKKExportCatalogRow* Entry = FindRow(Found->Name, Row);
		if (Entry && FCString::Atoi(*GetCell(Found->Name, Row, TEXT("clips"))) > 0)
		{
			Result.AddUnique(Entry->RuntimeSource);
			if (FCString::Atoi(*GetCell(Found->Name, Row, TEXT("animator_controllers"))) > 0
				|| FCString::Atoi(*GetCell(Found->Name, Row, TEXT("animator_overrides"))) > 0)
			{
				Result.AddUnique(TEXT("states:") + Entry->RuntimeSource);
			}
		}
	}
	Result.Sort();
	return Result;
}

TArray<FName> UKKAnimationCatalog::GetExpressionPatterns(const FString& Source, const FString& Prefab,
	const FString& Controller, int32 TargetIndex) const
{
	TArray<FName> Result;
	const FKKExportCatalogTable* Found = FindTable(TEXT("fbs_patterns"));
	if (!Found) return Result;
	for (FName Row : Found->RowOrder)
	{
		const FKKExportCatalogRow* Entry = FindRow(Found->Name, Row);
		if (Entry && Entry->RuntimeSource == Source && GetCell(Found->Name, Row, TEXT("prefab")) == Prefab
			&& GetCell(Found->Name, Row, TEXT("controller")) == Controller
			&& GetCell(Found->Name, Row, TEXT("target_index")) == FString::FromInt(TargetIndex)) Result.Add(Row);
	}
	return Result;
}

FString UKKAnimationCatalog::StudioBundleSource(FName Row, bool bOverride) const
{
	const FString Override = GetCell(TEXT("studio_catalog"), Row, TEXT("override_bundle"));
	FString Bundle = bOverride ? Override : GetCell(TEXT("studio_catalog"), Row, TEXT("bundle"));
	Bundle.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (Bundle.StartsWith(TEXT("abdata/"))) Bundle.RightChopInline(7);
	if (Bundle.IsEmpty()) return FString();
	const FKKExportCatalogRow* StudioRow = FindRow(TEXT("studio_catalog"), Row);
	const bool bMod = GetCell(TEXT("studio_catalog"), Row, TEXT("origin")) == TEXT("mod");
	const FKKExportCatalogTable* Sources = FindTable(TEXT("animation_sources"));
	TArray<FString> Candidates, Scoped;
	FString Package, Ignored;
	if (StudioRow) StudioRow->RuntimeSource.Split(TEXT("!"), &Package, &Ignored);
	if (Sources)
	{
		for (FName SourceRow : Sources->RowOrder)
		{
			const FKKExportCatalogRow* Entry = FindRow(Sources->Name, SourceRow);
			if (!Entry) continue;
			const FString& Source = Entry->RuntimeSource;
			if (Source == Bundle || Source.EndsWith(TEXT("!abdata/") + Bundle))
			{
				Candidates.AddUnique(Source);
				if ((!bMod && Source == Bundle) || (bMod && !Package.IsEmpty()
					&& Source.StartsWith(Package + TEXT("!")))) Scoped.AddUnique(Source);
			}
		}
	}
	if (Scoped.Num() == 1) return Scoped[0];
	if (Scoped.Num() > 1 || Candidates.Num() > 1) return FString();
	if (Candidates.Num() == 1) return Candidates[0];
	// Hand/base controller bundles need not be AnimationClip inventory sources.
	return bMod ? FString() : Bundle;
}

bool UKKAnimationCatalog::StudioSettings(FName Row,bool& IsH,int32& Layer,bool& IsMotion) const
{
    if(!FindRow(TEXT("studio_catalog"),Row)) return false;
    IsH=GetCell(TEXT("studio_catalog"),Row,TEXT("kind"))==TEXT("hanime");Layer=0;IsMotion=false;
    if(!IsH) return true;
    TArray<FString> Options;GetCell(TEXT("studio_catalog"),Row,TEXT("options")).ParseIntoArray(Options,TEXT(","),false);
    if(Options.Num()<4) return false;
    // Info.HAnimeLoadInfo uses int.TryParse(out breastLayer). Empty/invalid
    // text produces zero, even though the field's declaration starts at -1.
    const FString Number=Options[0].TrimStartAndEnd();int64 Parsed=0;int32 Start=0;bool Negative=false,Valid=!Number.IsEmpty();
    if(Valid&&(Number[0]=='+'||Number[0]=='-')) {Negative=Number[0]=='-';Start=1;Valid=Number.Len()>1;}
    for(int32 I=Start;I<Number.Len()&&Valid;++I) {
        Valid=Number[I]>='0'&&Number[I]<='9';
        if(Valid) {Parsed=Parsed*10+(Number[I]-'0');Valid=Parsed<=(Negative?2147483648LL:2147483647LL);}
    }
    Layer=Valid?int32(Negative?-Parsed:Parsed):0;if(Layer==-1) Layer=0;if(Layer<0) return false;
    Options[3]=Options[3].TrimStartAndEnd();
    if(!Options[3].Equals(TEXT("TRUE"),ESearchCase::IgnoreCase)&&!Options[3].Equals(TEXT("FALSE"),ESearchCase::IgnoreCase)) return false;
    IsMotion=Options[3].Equals(TEXT("TRUE"),ESearchCase::IgnoreCase);return true;
}

void UKKAnimationCatalog::StudioYureSource(FName Row,FString& Source,FString& Owner) const
{
    Source.Reset();Owner.Reset();
    if(GetCell(TEXT("studio_catalog"),Row,TEXT("kind"))!=TEXT("hanime")) return;
    TArray<FString> Options;GetCell(TEXT("studio_catalog"),Row,TEXT("options")).ParseIntoArray(Options,TEXT(","),false);
    if(Options.Num()<10||Options[8].IsEmpty()||Options[9].IsEmpty()) return;
    Source=Options[8];Source.ReplaceInline(TEXT("\\"),TEXT("/"));
    if(Source.StartsWith(TEXT("abdata/"))) Source.RightChopInline(7);
    Owner=Options[9];
    // A mod row first checks its own package. The runtime may use the base
    // bundle when that package has no matching bundle; no other package guess.
    if(GetCell(TEXT("studio_catalog"),Row,TEXT("origin"))==TEXT("mod")) {
        const auto* Entry=FindRow(TEXT("studio_catalog"),Row);FString Package,Ignored;
        if(Entry&&Entry->RuntimeSource.Split(TEXT("!"),&Package,&Ignored)) Source=Package+TEXT("!abdata/")+Source;
    }
}

int32 UKKAnimationCatalog::StudioPVMask(FName Row) const
{
    if(GetCell(TEXT("studio_catalog"),Row,TEXT("kind"))!=TEXT("hanime")) return 15;
    TArray<FString> Options;GetCell(TEXT("studio_catalog"),Row,TEXT("options")).ParseIntoArray(Options,TEXT(","),false);
    int32 Mask=0;
    // HAnimeLoadInfo uses bool.TryParse: missing/invalid fields become false.
    for(int32 I=0;I<4;++I) if(Options.IsValidIndex(I+4)&&Options[I+4].TrimStartAndEnd().Equals(TEXT("true"),ESearchCase::IgnoreCase)) Mask|=1<<I;
    return Mask;
}

FString UKKAnimationCatalog::StudioSource(FName Row) const
{
    const FString Base=StudioBundleSource(Row,false),Owner=GetCell(TEXT("studio_catalog"),Row,TEXT("file"));
    const FString OverOwner=GetCell(TEXT("studio_catalog"),Row,TEXT("override_bundle")).IsEmpty()?FString():GetCell(TEXT("studio_catalog"),Row,TEXT("override_file"));
    const FString Over=OverOwner.IsEmpty()?FString():StudioBundleSource(Row,true);
    if(Base.IsEmpty()||Owner.IsEmpty()||(!OverOwner.IsEmpty()&&Over.IsEmpty())) return FString();
    TArray<TSharedPtr<FJsonValue>> Values;
    for(const FString& Value:{Base,Owner,Over,OverOwner}) Values.Add(MakeShared<FJsonValueString>(Value));
    FString Json;FJsonSerializer::Serialize(Values,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
    FTCHARToUTF8 Utf8(*Json);
    return TEXT("states:studio@")+BytesToHex(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());
}

bool UKKAnimationCatalog::ResolveStudioAnimation(FName Row, const TArray<FString>& StateLabels,
	FString& OutKey, FString& OutError) const
{
	OutKey.Reset(); OutError.Reset();
	const FString Source = StudioSource(Row);
	const FString Owner = GetCell(TEXT("studio_catalog"), Row, TEXT("file"));
	const FString State = GetCell(TEXT("studio_catalog"), Row, TEXT("clip"));
	bool IsH,IsMotion;int32 Layer;
	if(!StudioSettings(Row,IsH,Layer,IsMotion)) {OutError=TEXT("Invalid Studio source options");return false;}
	const FString Prefix=FString::Printf(TEXT("[Studio layer %d] "),Layer);
	TArray<FString> Matches;
	for (const FString& Label : StateLabels)
	{
		FString Controller, Path;
		if (!Label.Split(TEXT(" / "), &Controller, &Path) || Controller != Owner) continue;
		if(!Path.RemoveFromStart(Prefix)) continue;
		if (Path == State || Path.EndsWith(TEXT(".") + State)) Matches.Add(Label);
	}
	if (Source.IsEmpty() || State.IsEmpty() || Matches.Num() != 1)
	{
		OutError = FString::Printf(TEXT("Studio state %s/%s: %d exact controller matches"), *Owner, *State, Matches.Num());
		return false;
	}
	OutKey = Source + TEXT("|") + Matches[0];
	return true;
}
