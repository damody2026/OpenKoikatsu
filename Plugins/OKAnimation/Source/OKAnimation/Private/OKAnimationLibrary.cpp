#include "OKAnimationLibrary.h"
#include "OKBvh.h"
#include "OKAnimationAssets.h"
#include "OKAnimationPlayers.h"
#include "OKUnrealSkeleton.h"
#include "KKOkAnimation.h"
#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	template <typename T>
	T* NewAsset(UObject* Outer, FName Name)
	{
		UObject* Owner = Outer ? Outer : GetTransientPackage();
		const FName Unique = Name.IsNone() ? MakeUniqueObjectName(Owner, T::StaticClass()) : (Outer ? Name : MakeUniqueObjectName(Owner, T::StaticClass(), Name));
		return NewObject<T>(Owner, Unique, Outer ? RF_Public | RF_Standalone | RF_Transactional : RF_Transient);
	}

	FString BuiltInFamilyPath()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OKAnimation"));
		return Plugin ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("kk_cf_j_rest.json")) : FString();
	}
}

UOKAnimSequence* UOKAnimationLibrary::ConvertKKClip(AKKPreviewActor* Character, const FString& ClipKey, float Fps, UObject* Outer, FName Name, FString& Error)
{
	if (!IsValid(Character) || !Character->Session.IsOpen()) { Error = TEXT("character not loaded"); return nullptr; }
	TArray<uint8> Bytes;
	if (!FOkAnimBridge::FromClip(Character->Session, ClipKey, Fps > 0.f ? Fps : 30.f, Bytes)) { Error = FOkAnimBridge::LastError(); return nullptr; }
	UOKAnimSequence* Seq = NewAsset<UOKAnimSequence>(Outer, Name);
	return Seq->SetData(Bytes, Error) ? Seq : nullptr;
}

UOKGroupAnimSequence* UOKAnimationLibrary::ConvertKKPair(AKKPairedPlayback* Pair, float Fps, UObject* Outer, FName Name, FString& Error)
{
	if (!IsValid(Pair) || !IsValid(Pair->FemaleActor) || !IsValid(Pair->MaleActor)) { Error = TEXT("no paired playback"); return nullptr; }
	TArray<uint8> Package;
	if (!FOkAnimBridge::GroupFromPair(Pair->FemaleActor->Session, Pair->MaleActor->Session, Pair->GetState(), Pair->GetFemaleMotion(), Pair->GetMaleMotion(),
		Fps > 0.f ? Fps : 30.f, Package))
	{
		Error = FOkAnimBridge::LastError();
		return nullptr;
	}
	UOKGroupAnimSequence* Group = NewAsset<UOKGroupAnimSequence>(Outer, Name);
	return Group->SetFromPackage(Package, Outer, Error) ? Group : nullptr;
}

UOKAnimSequence* UOKAnimationLibrary::ConvertUnrealAnimation(UAnimSequence* Sequence, UOKRetargetProfile* Profile, UOKSkeletonFamily* Family, float Fps, UObject* Outer, FName Name, FString& Error)
{
	if (!Sequence || !Sequence->GetSkeleton()) { Error = TEXT("no animation / skeleton"); return nullptr; }
	if (!Family) Family = Profile && Profile->Family ? Profile->Family.Get() : GetDefaultFamily();
	FOKUnrealSkeleton Binding;
	if (!Binding.Build(Sequence->GetSkeleton()->GetReferenceSkeleton(), Profile, Family, Error)) return nullptr;
	TArray<uint8> Bytes;
	if (!Binding.ConvertAnimation(Sequence, Fps > 0.f ? Fps : 30.f, Bytes, Error)) return nullptr;
	UOKAnimSequence* Seq = NewAsset<UOKAnimSequence>(Outer, Name.IsNone() ? FName(*(Sequence->GetName() + TEXT("_OK"))) : Name);
	return Seq->SetData(Bytes, Error) ? Seq : nullptr;
}

UOKSkeletonFamily* UOKAnimationLibrary::MakeFamilyFromCharacter(AKKPreviewActor* Character, UObject* Outer, FName Name, FString& Error)
{
	if (!IsValid(Character) || !Character->Session.IsOpen()) { Error = TEXT("character not loaded"); return nullptr; }
	FString Json;
	if (!FOkAnimBridge::SessionRestHeader(Character->Session, Json)) { Error = FOkAnimBridge::LastError(); return nullptr; }
	UOKSkeletonFamily* Family = NewAsset<UOKSkeletonFamily>(Outer, Name);
	Family->FamilyId = TEXT("kk.cf_j");
	Family->RestHeaderJson = Json;
	return Family;
}

UOKAnimSequence* UOKAnimationLibrary::MakeRestAnimation(UOKSkeletonFamily* Family, int32 Frames, float Fps, UObject* Outer, FName Name, FString& Error)
{
	if (!Family) Family = GetDefaultFamily();
	TSharedPtr<FJsonObject> Header;
	const auto Reader = TJsonReaderFactory<>::Create(Family ? Family->RestHeaderJson : FString());
	if (!Family || !FJsonSerializer::Deserialize(Reader, Header) || !Header) { Error = TEXT("no family rest (run OKAnim.SaveFamily in the game)"); return nullptr; }
	Frames = FMath::Max(2, Frames);
	Fps = Fps > 0.f ? Fps : 30.f;
	const FString HipsName = Header->HasField(TEXT("hips")) ? Header->GetStringField(TEXT("hips")) : FString(TEXT("cf_j_hips"));
	TArray<float> Rest, Rotations, Hips;
	FVector HipsPos = FVector::ZeroVector;
	for (const TSharedPtr<FJsonValue>& V : Header->GetArrayField(TEXT("bones")))
	{
		const TSharedPtr<FJsonObject> B = V->AsObject();
		for (const TSharedPtr<FJsonValue>& C : B->GetArrayField(TEXT("rest_local"))) Rest.Add(float(C->AsNumber()));
		if (B->GetStringField(TEXT("name")) == HipsName)
		{
			const TArray<TSharedPtr<FJsonValue>>& P = B->GetArrayField(TEXT("rest_pos"));
			HipsPos = FVector(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber());
		}
	}
	for (int32 F = 0; F < Frames; ++F) { Rotations.Append(Rest); Hips.Add(float(HipsPos.X)); Hips.Add(float(HipsPos.Y)); Hips.Add(float(HipsPos.Z)); }
	Header->SetNumberField(TEXT("frames"), Frames);
	Header->SetNumberField(TEXT("fps"), Fps);
	Header->SetBoolField(TEXT("looping"), true);
	TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
	Meta->SetStringField(TEXT("source"), TEXT("keyed"));
	Header->SetObjectField(TEXT("meta"), Meta);
	FString Text;
	const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	FJsonSerializer::Serialize(Header.ToSharedRef(), Writer);
	TArray<uint8> Bytes;
	if (!FOkAnimBridge::FromLocals(Text, Rotations, Hips, Bytes)) { Error = FOkAnimBridge::LastError(); return nullptr; }
	UOKAnimSequence* Seq = NewAsset<UOKAnimSequence>(Outer, Name.IsNone() ? FName(TEXT("NewOKAnimation")) : Name);
	return Seq->SetData(Bytes, Error) ? Seq : nullptr;
}

UOKSkeletonFamily* UOKAnimationLibrary::GetDefaultFamily()
{
	if (UOKSkeletonFamily* Family = GetDefault<UOKAnimationSettings>()->DefaultFamily.LoadSynchronous()) return Family;
	static TStrongObjectPtr<UOKSkeletonFamily> BuiltIn;
	if (!BuiltIn)
	{
		FString Json;
		const FString Path = BuiltInFamilyPath();
		if (Path.IsEmpty() || !FFileHelper::LoadFileToString(Json, *Path)) return nullptr;
		BuiltIn.Reset(NewObject<UOKSkeletonFamily>(GetTransientPackage(), TEXT("OKBuiltInKKFamily")));
		BuiltIn->RestHeaderJson = Json;
		FString Male;
		if (FFileHelper::LoadFileToString(Male, *Path.Replace(TEXT("kk_cf_j_rest.json"), TEXT("kk_cf_j_rest_male.json")))) BuiltIn->RestVariants.Add(TEXT("male"), Male);
	}
	return BuiltIn.Get();
}

bool UOKAnimationLibrary::PlayOnCharacter(AKKPreviewActor* Character, UOKAnimationAsset* Animation)
{
	if (!IsValid(Character) || !Animation) return false;
	UOKAnimationComponent* Component = Character->FindComponentByClass<UOKAnimationComponent>();
	if (!Component)
	{
		Component = NewObject<UOKAnimationComponent>(Character, TEXT("OKAnimation"));
		Component->bAutoPlay = false;
		Component->RegisterComponent();
	}
	return Component->Play(Animation);
}

AOKGroupPlayer* UOKAnimationLibrary::PlayGroup(UObject* WorldContext, UOKAnimationAsset* Animation, const TArray<AKKPreviewActor*>& Characters, FString& Error)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) { Error = TEXT("no world"); return nullptr; }
	AOKGroupPlayer* Player = World->SpawnActor<AOKGroupPlayer>();
	if (!Player) { Error = TEXT("spawn failed"); return nullptr; }
	if (!Player->Play(Animation, Characters)) { Error = Player->LastError; Player->Destroy(); return nullptr; }
	return Player;
}

bool UOKAnimationLibrary::SaveOkAnimFile(UOKAnimSequence* Animation, const FString& Path)
{
	return Animation && !Animation->Data.IsEmpty() && FFileHelper::SaveArrayToFile(Animation->Data, *Path);
}

FString UOKAnimationLibrary::LiveFolder() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OKAnim"), TEXT("Live"))); }

UOKAnimSequence* UOKAnimationLibrary::ImportBvhFile(const FString& Path, UObject* Outer, FName Name, FString& Error)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path)) { Error = FString::Printf(TEXT("cannot read %s"), *Path); return nullptr; }
	return ImportBvhText(Text, Path, Outer, Name.IsNone() ? FName(*FPaths::GetBaseFilename(Path)) : Name, Error);
}

UOKAnimSequence* UOKAnimationLibrary::ImportBvhText(const FString& Text, const FString& SourceName, UObject* Outer, FName Name, FString& Error)
{
	FOKBvh Bvh;
	if (!Bvh.Parse(Text, Error)) return nullptr;
	FOKUnrealSkeleton Binding;
	if (!Binding.Build(Bvh.Ref, nullptr, GetDefaultFamily(), Error)) return nullptr;
	const TMap<FString, FString> Meta = { { TEXT("source"), TEXT("bvh") }, { TEXT("source_key"), SourceName }, { TEXT("bvh_scale"), FString::SanitizeFloat(Bvh.Scale) } };
	TArray<uint8> Bytes;
	if (!Binding.ConvertLocals(Bvh.Frames, Bvh.Fps, false, FPaths::GetBaseFilename(SourceName), Meta, Bytes, Error)) return nullptr;
	UOKAnimSequence* Seq = NewAsset<UOKAnimSequence>(Outer, Name);
	return Seq->SetData(Bytes, Error) ? Seq : nullptr;
}

UOKGroupAnimSequence* UOKAnimationLibrary::LoadOkGroupFile(const FString& Path, UObject* Outer, FName Name, FString& Error)
{
	TArray<uint8> Package;
	if (!FFileHelper::LoadFileToArray(Package, *Path)) { Error = FString::Printf(TEXT("cannot read %s"), *Path); return nullptr; }
	UOKGroupAnimSequence* Group = NewAsset<UOKGroupAnimSequence>(Outer, Name.IsNone() ? FName(*FPaths::GetBaseFilename(Path)) : Name);
	return Group->SetFromPackage(Package, Outer, Error) ? Group : nullptr;
}

UOKAnimSequence* UOKAnimationLibrary::LoadOkAnimFile(const FString& Path, UObject* Outer, FName Name, FString& Error)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path)) { Error = FString::Printf(TEXT("cannot read %s"), *Path); return nullptr; }
	UOKAnimSequence* Seq = NewAsset<UOKAnimSequence>(Outer, Name.IsNone() ? FName(*FPaths::GetBaseFilename(Path)) : Name);
	return Seq->SetData(Bytes, Error) ? Seq : nullptr;
}
