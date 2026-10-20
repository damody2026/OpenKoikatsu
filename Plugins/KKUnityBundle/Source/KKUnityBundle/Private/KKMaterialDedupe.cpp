#include "KKMaterialDedupe.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "KKTextureRuntime.h"
#include "Misc/SecureHash.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace OKMaterialDedupePrivate
{
bool DedupeEnabled()
{
	// On by default; OK_MATERIAL_DEDUPE=0 turns it off.
	static const bool bOn = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MATERIAL_DEDUPE")) != TEXT("0");
	return bOn;
}

// Properties that differ between two equal graphs (identity, derived caches), not what renders.
bool Skipped(const FProperty* P)
{
	static const TSet<FName> Names = {TEXT("StateId"), TEXT("LightingGuid"), TEXT("MaterialExpressionGuid"),
		TEXT("CachedExpressionData"), TEXT("ExpressionGUID"), TEXT("ReferencedTextureGuids"),
		TEXT("MaterialExpressionEditorX"), TEXT("MaterialExpressionEditorY"), TEXT("Desc"), TEXT("bCollapsed")};
	return Names.Contains(P->GetFName());
}

// A scalar or vector parameter's default: left out of a graph-only key (OKFindEqualMaterialGraph puts each graph's
// own values on a MID).
bool ParameterDefault(const UObject* Object, const FProperty* P)
{
	return P->GetFName() == TEXT("DefaultValue") && (Object->IsA<UMaterialExpressionScalarParameter>() || Object->IsA<UMaterialExpressionVectorParameter>());
}

bool GIgnoreParameterDefaults = false;

void ExportProperties(UObject* Object, FString& Out)
{
	Out += Object->GetClass()->GetName();
	Out += TEXT("{");
	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		const FProperty* P = *It;
		if (Skipped(P) || (GIgnoreParameterDefaults && ParameterDefault(Object, P))) continue;
		Out += P->GetName();
		Out += TEXT("=");
		for (int32 I = 0; I < P->ArrayDim; ++I)
		{
			P->ExportText_InContainer(I, Out, Object, nullptr, Object, PPF_None, nullptr);
		}
		Out += TEXT(";");
	}
	Out += TEXT("}");
}

struct FEntry { TStrongObjectPtr<UMaterial> Material; uint64 LastUse = 0; };
TMap<FString, FEntry> Cache;
uint64 Clock = 0;
FOKMaterialDedupeStats Stats;
constexpr int32 MaxEntries = 768;

// The graph and settings as text, with this material's own objects (expressions, editor data) named by their
// position instead of their unique object names. Textures and other outside objects keep their path: two graphs
// only match when they sample the very same texture objects.
FString Signature(UMaterial* Mat, const FString& Extra)
{
	FString Text = Extra;
	TArray<TPair<FString, FString>> Local;
	Local.Emplace(Mat->GetPathName(), TEXT("#M"));
#if WITH_EDITORONLY_DATA
	if (UObject* Editor = Mat->GetEditorOnlyData())
	{
		Local.Emplace(Editor->GetPathName(), TEXT("#D"));
	}
	TArray<UMaterialExpression*> Expressions;
	for (UMaterialExpression* E : Mat->GetExpressions()) if (E) Expressions.Add(E);
	for (int32 I = 0; I < Expressions.Num(); ++I) Local.Emplace(Expressions[I]->GetPathName(), FString::Printf(TEXT("#E%d"), I));
	// A runtime texture is named by what it was made from: a card opened again or another outfit gets new texture
	// objects with the same texels, and the material made with the earlier ones looks the same.
	for (UMaterialExpression* E : Expressions)
	{
		if (const auto* Sample = Cast<UMaterialExpressionTextureBase>(E))
		{
			const FString Content = OKRuntimeTextureContentKey(Sample->Texture);
			if (!Content.IsEmpty()) Local.Emplace(Sample->Texture->GetPathName(), TEXT("#T") + Content);
		}
	}
	ExportProperties(Mat, Text);
	if (UObject* Editor = Mat->GetEditorOnlyData()) ExportProperties(Editor, Text);
	for (UMaterialExpression* E : Expressions) ExportProperties(E, Text);
#else
	ExportProperties(Mat, Text);
#endif
	// Longest paths first, so a path that prefixes another is not replaced inside it.
	Local.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B) { return A.Key.Len() > B.Key.Len(); });
	for (const auto& Pair : Local) Text.ReplaceInline(*Pair.Key, *Pair.Value, ESearchCase::CaseSensitive);
	FSHAHash Hash;
	FSHA1::HashBuffer(*Text, Text.Len() * sizeof(TCHAR), Hash.Hash);
	return Hash.ToString() + FString::Printf(TEXT("/%d"), Text.Len());
}
}

// Names qualified, not `using namespace`: in a unity build a using-directive reaches the files after this one.
UMaterial* OKFindEqualMaterial(UMaterial* Mat, const FString& Extra, FString& OutKey)
{
	OutKey.Reset();
	if (!Mat || !OKMaterialDedupePrivate::DedupeEnabled() || !IsInGameThread()) return nullptr;
	const double Start = FPlatformTime::Seconds();
	OutKey = OKMaterialDedupePrivate::Signature(Mat, Extra);
	OKMaterialDedupePrivate::Stats.SignatureSeconds += FPlatformTime::Seconds() - Start;
	if (OKMaterialDedupePrivate::FEntry* Hit = OKMaterialDedupePrivate::Cache.Find(OutKey))
	{
		if (UMaterial* Same = Hit->Material.Get())
		{
			Hit->LastUse = ++OKMaterialDedupePrivate::Clock;
			++OKMaterialDedupePrivate::Stats.Hits;
			return Same;
		}
	}
	++OKMaterialDedupePrivate::Stats.Misses;
	return nullptr;
}

UMaterialInterface* OKFindEqualMaterialGraph(UMaterial* Mat, const FString& Extra, FString& OutKey, UObject* Outer)
{
	OKMaterialDedupePrivate::GIgnoreParameterDefaults = true;
	UMaterial* Same = OKFindEqualMaterial(Mat, TEXT("graph|") + Extra, OutKey);
	OKMaterialDedupePrivate::GIgnoreParameterDefaults = false;
	if (!Same) return nullptr;
	// The shared material keeps the defaults of the graph that made it; this graph's own values go on a MID.
	UMaterialInstanceDynamic* Own = nullptr;
#if WITH_EDITORONLY_DATA // the expressions are editor data; game builds keep the shared defaults
	for (UMaterialExpression* E : Mat->GetExpressions())
	{
		if (const auto* V = Cast<UMaterialExpressionVectorParameter>(E))
		{
			FLinearColor Have;
			if (Same->GetVectorParameterValue(FMaterialParameterInfo(V->ParameterName), Have) && Have == V->DefaultValue) continue;
			if (!Own) Own = UMaterialInstanceDynamic::Create(Same, Outer);
			Own->SetVectorParameterValue(V->ParameterName, V->DefaultValue);
		}
		else if (const auto* S = Cast<UMaterialExpressionScalarParameter>(E))
		{
			float Have = 0.f;
			if (Same->GetScalarParameterValue(FMaterialParameterInfo(S->ParameterName), Have) && Have == S->DefaultValue) continue;
			if (!Own) Own = UMaterialInstanceDynamic::Create(Same, Outer);
			Own->SetScalarParameterValue(S->ParameterName, S->DefaultValue);
		}
	}
#endif
	if (Own) return Own;
	return Same;
}

void OKRememberMaterial(const FString& Key, UMaterial* Mat)
{
	if (Key.IsEmpty() || !Mat || !OKMaterialDedupePrivate::DedupeEnabled()) return;
	while (OKMaterialDedupePrivate::Cache.Num() >= OKMaterialDedupePrivate::MaxEntries)
	{
		FString Oldest;
		uint64 Age = MAX_uint64;
		for (const auto& Pair : OKMaterialDedupePrivate::Cache) if (Pair.Value.LastUse < Age) { Age = Pair.Value.LastUse; Oldest = Pair.Key; }
		OKMaterialDedupePrivate::Cache.Remove(Oldest);
	}
	OKMaterialDedupePrivate::FEntry& Entry = OKMaterialDedupePrivate::Cache.FindOrAdd(Key);
	Entry.Material.Reset(Mat);
	Entry.LastUse = ++OKMaterialDedupePrivate::Clock;
}

FOKMaterialDedupeStats OKMaterialDedupeStats() { return OKMaterialDedupePrivate::Stats; }
void OKResetMaterialDedupeStats() { OKMaterialDedupePrivate::Stats = {}; }
