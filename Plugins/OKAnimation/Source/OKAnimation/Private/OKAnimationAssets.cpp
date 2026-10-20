#include "OKAnimationAssets.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/IConsoleManager.h"
#include "OKAnimationLibrary.h"
#include "OKUnrealSkeleton.h"
#include "KKOkAnimation.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/AssetRegistryTagsContext.h"

namespace
{
	TArray<TSharedPtr<FJsonValue>> Numbers(std::initializer_list<double> Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (double V : Values) Out.Add(MakeShared<FJsonValueNumber>(V));
		return Out;
	}
	FString ToString(const TSharedRef<FJsonObject>& Json)
	{
		FString Out;
		const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Json, Writer);
		return Out;
	}
	TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		TSharedPtr<FJsonObject> Out;
		const auto Reader = TJsonReaderFactory<>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, Out) ? Out : nullptr;
	}
	TArray<TSharedPtr<FJsonValue>> ContactsJson(const TArray<FOKContact>& Contacts)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FOKContact& C : Contacts) Out.Add(MakeShared<FJsonValueObject>(C.ToJson()));
		return Out;
	}
}

// ------------------------------------------------------------------------------------------------ contacts

TSharedRef<FJsonObject> FOKContact::ToJson() const
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("role"), FMath::Max(0, Role));
	J->SetStringField(TEXT("effector"), Effector.ToString());
	J->SetNumberField(TEXT("target_role"), TargetRole);
	J->SetStringField(TEXT("target_bone"), TargetBone.IsNone() ? FString() : TargetBone.ToString());
	J->SetArrayField(TEXT("offset"), Numbers({ Offset.X, Offset.Y, Offset.Z }));
	J->SetNumberField(TEXT("start"), Start);
	J->SetNumberField(TEXT("end"), FMath::Max(Start, End));
	J->SetNumberField(TEXT("blend"), Blend);
	J->SetNumberField(TEXT("weight"), Weight);
	J->SetNumberField(TEXT("priority"), Priority);
	J->SetBoolField(TEXT("ik"), bIK);
	J->SetNumberField(TEXT("target_moves"), TargetMoves);
	TArray<TSharedPtr<FJsonValue>> TrackJson;
	for (const FVector& V : Track) TrackJson.Add(MakeShared<FJsonValueArray>(Numbers({ V.X, V.Y, V.Z })));
	J->SetArrayField(TEXT("track"), TrackJson);
	return J;
}

FOKContact FOKContact::FromJson(const FJsonObject& J)
{
	FOKContact C;
	C.Role = int32(J.GetNumberField(TEXT("role")));
	C.Effector = FName(J.GetStringField(TEXT("effector")));
	C.TargetRole = int32(J.GetNumberField(TEXT("target_role")));
	const FString Target = J.GetStringField(TEXT("target_bone"));
	C.TargetBone = Target.IsEmpty() ? NAME_None : FName(Target);
	const TArray<TSharedPtr<FJsonValue>>* Offset = nullptr;
	if (J.TryGetArrayField(TEXT("offset"), Offset) && Offset->Num() == 3)
		C.Offset = FVector((*Offset)[0]->AsNumber(), (*Offset)[1]->AsNumber(), (*Offset)[2]->AsNumber());
	C.Start = float(J.GetNumberField(TEXT("start")));
	C.End = float(J.GetNumberField(TEXT("end")));
	C.Blend = float(J.GetNumberField(TEXT("blend")));
	C.Weight = float(J.GetNumberField(TEXT("weight")));
	C.Priority = float(J.GetNumberField(TEXT("priority")));
	C.bIK = J.GetBoolField(TEXT("ik"));
	C.TargetMoves = float(J.GetNumberField(TEXT("target_moves")));
	const TArray<TSharedPtr<FJsonValue>>* Track = nullptr;
	if (J.TryGetArrayField(TEXT("track"), Track))
		for (const TSharedPtr<FJsonValue>& V : *Track)
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			if (A.Num() == 3) C.Track.Add(FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()));
		}
	return C;
}

// ------------------------------------------------------------------------------------------------ spec builder

int32 FOKSpecBuilder::AddBlob(const UObject* Owner, const TArray<uint8>& Data)
{
	if (const int32* Found = BlobIndex.Find(Owner)) return *Found;
	const int32 Index = Blobs.Add(Data);
	BlobIndex.Add(Owner, Index);
	return Index;
}

TSharedPtr<FJsonObject> FOKSpecBuilder::Build(const UOKAnimationAsset* Asset)
{
	if (!Asset) { Error = TEXT("missing OK animation asset"); return nullptr; }
	if (Stack.Contains(Asset)) { Error = FString::Printf(TEXT("%s references itself"), *Asset->GetPathName()); return nullptr; }
	Stack.Push(Asset);
	TSharedPtr<FJsonObject> Node = Asset->BuildSpecNode(*this);
	Stack.Pop();
	if (!Node && Error.IsEmpty()) Error = FString::Printf(TEXT("%s: invalid"), *Asset->GetPathName());
	return Node;
}

TSharedPtr<FJsonObject> FOKSpecBuilder::BuildCrossfade(const UOKAnimationAsset* From, float FromTime, const UOKAnimationAsset* To, float Duration)
{
	const TSharedPtr<FJsonObject> A = Build(From);
	const TSharedPtr<FJsonObject> B = A ? Build(To) : nullptr;
	if (!B) return nullptr;
	TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("type"), TEXT("crossfade"));
	J->SetObjectField(TEXT("from"), A);
	J->SetObjectField(TEXT("to"), B);
	J->SetNumberField(TEXT("from_time"), FMath::Max(0.f, FromTime));
	J->SetNumberField(TEXT("duration"), FMath::Max(0.f, Duration));
	return J;
}

TSharedPtr<FOkAnimPlayer> UOKAnimationAsset::CreateCrossfadePlayer(const UOKAnimationAsset* From, float FromTime, float BlendTime, FString& OutError) const
{
	FOKSpecBuilder Builder;
	const TSharedPtr<FJsonObject> Root = From ? Builder.BuildCrossfade(From, FromTime, this, BlendTime) : Builder.Build(this);
	if (!Root) { OutError = Builder.Error; return nullptr; }
	TSharedPtr<FOkAnimPlayer> Player = MakeShared<FOkAnimPlayer>();
	if (!Player->Create(ToString(Root.ToSharedRef()), Builder.Blobs)) { OutError = FOkAnimBridge::LastError(); return nullptr; }
	ApplyFamilyAliases(*Player);
	return Player;
}

bool UOKAnimationAsset::BuildSpec(FString& OutSpecJson, TArray<TArray<uint8>>& OutBlobs, FString& OutError) const
{
	FOKSpecBuilder Builder;
	const TSharedPtr<FJsonObject> Root = Builder.Build(this);
	if (!Root) { OutError = Builder.Error; return false; }
	OutSpecJson = ToString(Root.ToSharedRef());
	OutBlobs = MoveTemp(Builder.Blobs);
	return true;
}

TSharedPtr<FOkAnimPlayer> UOKAnimationAsset::CreatePlayer(FString& OutError) const
{
	FString Spec;
	TArray<TArray<uint8>> Blobs;
	if (!BuildSpec(Spec, Blobs, OutError)) return nullptr;
	TSharedPtr<FOkAnimPlayer> Player = MakeShared<FOkAnimPlayer>();
	if (!Player->Create(Spec, Blobs)) { OutError = FOkAnimBridge::LastError(); return nullptr; }
	ApplyFamilyAliases(*Player);
	return Player;
}

static TAutoConsoleVariable<FString> CVarWalkClip(TEXT("ok.Anim.WalkClip"), TEXT(""), TEXT("Walk cycle (.okanim) mixed into the legs of group roles the placement moves fast; empty = the first looping walk of Saved/OKAnim/KK."));

const TArray<uint8>& UOKAnimationAsset::DefaultWalk()
{
	static TArray<uint8> Bytes;
	static FString Loaded = TEXT("?");
	FString Path = CVarWalkClip.GetValueOnGameThread();
	if (Path == Loaded) return Bytes;
	Loaded = Path;
	Bytes.Reset();
	if (Path.IsEmpty())
	{
		// The library's catalog: a looping clip named like a walk.
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("OKAnim/KK/female");
		FString Text;
		TSharedPtr<FJsonObject> J;
		if (FFileHelper::LoadFileToString(Text, *(Dir / TEXT("catalog.json"))) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) && J)
			for (const TSharedPtr<FJsonValue>& V : J->GetArrayField(TEXT("clips")))
			{
				const TSharedPtr<FJsonObject> C = V->AsObject();
				FString Key = C->GetStringField(TEXT("key"));
				int32 Dot;
				if (Key.FindLastChar(TEXT('.'), Dot)) Key = Key.Mid(Dot + 1);
				if (Key.Contains(TEXT("walk"), ESearchCase::IgnoreCase) && C->HasField(TEXT("looping")) && C->GetBoolField(TEXT("looping"))) { Path = Dir / C->GetStringField(TEXT("file")); break; }
			}
	}
	if (!Path.IsEmpty()) FFileHelper::LoadFileToArray(Bytes, *Path);
	if (Bytes.Num()) UE_LOG(LogTemp, Display, TEXT("OK walk cycle: %s"), *Path);
	return Bytes;
}

void UOKAnimationAsset::ApplyFamilyAliases(FOkAnimPlayer& Player)
{
	if (Player.Roles() > 1) Player.SetWalk(DefaultWalk());
	const UOKSkeletonFamily* Family = GetDefault<UOKAnimationSettings>()->DefaultFamily.LoadSynchronous();
	if (!Family || Family->Aliases.IsEmpty()) return;
	TMap<FName, FName> FamilyToCharacter;
	for (const auto& P : Family->Aliases) FamilyToCharacter.Add(P.Value, P.Key);
	Player.SetAliases(FamilyToCharacter);
}

float UOKAnimationAsset::GetPlayLength() const
{
	FString Error;
	const TSharedPtr<FOkAnimPlayer> Player = CreatePlayer(Error);
	return Player ? Player->Duration() : 0.f;
}

TArray<FName> UOKAnimationAsset::GetParameterNames() const { return {}; }

// ------------------------------------------------------------------------------------------------ sequence

bool UOKAnimSequence::SetData(const TArray<uint8>& Bytes, FString& OutError)
{
	const TArray<uint8> Old = Data;
	Data = Bytes;
	if (!ReadInfo(OutError, true)) { Data = Old; return false; }
	SourceData.Reset();
	KeyLayer.Reset();
	return true;
}

TSharedPtr<const FOKAnimTracks> UOKAnimSequence::GetBaseTracks(FString& OutError) const
{
	const TArray<uint8>& Base = GetBaseData();
	const uint32 Crc = FCrc::MemCrc32(Base.GetData(), Base.Num());
	if (BaseTracks && Crc == BaseTracksCrc && Base.Num() == BaseTracksSize) return BaseTracks;
	TSharedPtr<FOKAnimTracks> Tracks = MakeShared<FOKAnimTracks>();
	if (!Tracks->Load(Base, OutError)) return nullptr;
	BaseTracks = Tracks;
	BaseTracksCrc = Crc;
	BaseTracksSize = Base.Num();
	return BaseTracks;
}

bool UOKAnimSequence::SetFrameCount(int32 NewFrames, FString& OutError)
{
	const TSharedPtr<const FOKAnimTracks> Base = GetBaseTracks(OutError);
	if (!Base) return false;
	FOKAnimTracks Tracks = *Base;
	Tracks.SetFrameCount(NewFrames);
	TArray<uint8> Out;
	if (!Tracks.Save(GetBaseData(), Out, OutError)) return false;
	(SourceData.IsEmpty() ? Data : SourceData) = MoveTemp(Out);
	const float End = (Tracks.Frames - 1) / FMath::Max(1.f, Tracks.Fps) + 1.e-4f;
	for (FOKBoneKeys& K : KeyLayer)
		for (int32 C = 0; C < FOKBoneKeys::NumChannels; ++C)
		{
			FRichCurve& Curve = K.Channel(C);
			TArray<FKeyHandle> Past;
			for (auto It = Curve.GetKeyHandleIterator(); It; ++It) if (Curve.GetKeyTime(*It) > End) Past.Add(*It);
			for (const FKeyHandle H : Past) Curve.DeleteKey(H);
		}
	if (!Rebake(OutError)) return false;
	return ReadInfo(OutError, false);
}

FOKBoneKeys* UOKAnimSequence::FindKeys(FName Bone) { return KeyLayer.FindByPredicate([Bone](const FOKBoneKeys& K) { return K.Bone == Bone; }); }

FOKBoneKeys& UOKAnimSequence::FindOrAddKeys(FName Bone)
{
	if (FOKBoneKeys* K = FindKeys(Bone)) return *K;
	FOKBoneKeys& K = KeyLayer.AddDefaulted_GetRef();
	K.Bone = Bone;
	return K;
}

bool UOKAnimSequence::Rebake(FString& OutError)
{
	KeyLayer.RemoveAll([](const FOKBoneKeys& K) { return K.IsEmpty(); });
	if (KeyLayer.IsEmpty())
	{
		if (!SourceData.IsEmpty()) { Data = MoveTemp(SourceData); SourceData.Reset(); }
		return ReadInfo(OutError, false);
	}
	if (SourceData.IsEmpty()) SourceData = Data;
	const TSharedPtr<const FOKAnimTracks> Base = GetBaseTracks(OutError);
	if (!Base) return false;
	FOKAnimTracks Tracks = *Base;
	Tracks.ApplyLayer(KeyLayer);
	TArray<uint8> Out;
	if (!Tracks.Save(SourceData, Out, OutError)) return false;
	Data = MoveTemp(Out);
	return ReadInfo(OutError, false);
}

bool UOKAnimSequence::CollapseLayer(FString& OutError)
{
	if (!Rebake(OutError)) return false;
	SourceData.Reset();
	KeyLayer.Reset();
	return true;
}

bool UOKAnimSequence::ReadInfo(FString& OutError, bool bParameters)
{
	FString Text;
	if (!FOkAnimBridge::Info(Data, Text)) { OutError = FOkAnimBridge::LastError(); return false; }
	const TSharedPtr<FJsonObject> J = Parse(Text);
	if (!J) { OutError = TEXT("OK animation info unreadable"); return false; }
	Family = FName(J->GetStringField(TEXT("family")));
	Fps = float(J->GetNumberField(TEXT("fps")));
	Frames = int32(J->GetNumberField(TEXT("frames")));
	Duration = float(J->GetNumberField(TEXT("duration")));
	Bones.Reset();
	BoneParents.Reset();
	for (const TSharedPtr<FJsonValue>& T : J->GetArrayField(TEXT("tracks")))
	{
		const TSharedPtr<FJsonObject> O = T->AsObject();
		const FName Bone(O->GetStringField(TEXT("bone")));
		Bones.Add(Bone);
		const FString Parent = O->GetStringField(TEXT("parent"));
		if (!Parent.IsEmpty()) BoneParents.Add(Bone, FName(Parent));
	}
	if (!bParameters) return true;
	bLooping = J->GetBoolField(TEXT("looping"));
	Scene = EOKSourceScene(FMath::Clamp(int32(J->GetNumberField(TEXT("scene"))), 0, 4));
	Contacts.Reset();
	for (const TSharedPtr<FJsonValue>& C : J->GetArrayField(TEXT("contacts"))) Contacts.Add(FOKContact::FromJson(*C->AsObject()));
	Curves.Reset();
	for (const TSharedPtr<FJsonValue>& C : J->GetArrayField(TEXT("curve_values")))
	{
		const TSharedPtr<FJsonObject> O = C->AsObject();
		FOKCurveTrack& Track = Curves.AddDefaulted_GetRef();
		Track.Name = FName(O->GetStringField(TEXT("name")));
		const TArray<TSharedPtr<FJsonValue>>& Values = O->GetArrayField(TEXT("values"));
		FRichCurve& Curve = *Track.Curve.GetRichCurve();
		for (int32 F = 0; F < Values.Num(); ++F)
		{
			// Drop keys that a straight line through their neighbours already gives.
			const float V = float(Values[F]->AsNumber());
			if (F > 0 && F + 1 < Values.Num())
			{
				const float Prev = float(Values[F - 1]->AsNumber()), Next = float(Values[F + 1]->AsNumber());
				if (FMath::IsNearlyEqual(V, 0.5f * (Prev + Next), 1.e-4f)) continue;
			}
			const FKeyHandle Key = Curve.AddKey(F / FMath::Max(Fps, 1.f), V);
			Curve.SetKeyInterpMode(Key, RCIM_Linear);
		}
	}
	Meta.Reset();
	const TSharedPtr<FJsonObject>* MetaJson = nullptr;
	if (J->TryGetObjectField(TEXT("meta"), MetaJson))
		for (const auto& Pair : (*MetaJson)->Values) Meta.Add(FString(Pair.Key), Pair.Value->AsString());
	// Fixed keys -> the structured parameters.
	Info = FOKAnimationMeta();
	auto Get = [&](const TCHAR* Key) { const FString* V = Meta.Find(Key); return V ? *V : FString(); };
	Info.Category = Get(TEXT("category"));
	Get(TEXT("tags")).ParseIntoArray(Info.Tags, TEXT(","));
	Info.Source = Get(TEXT("source"));
	Info.License = Get(TEXT("license"));
	if (!Get(TEXT("fade_in")).IsEmpty()) Info.FadeIn = FCString::Atof(*Get(TEXT("fade_in")));
	if (!Get(TEXT("fade_out")).IsEmpty()) Info.FadeOut = FCString::Atof(*Get(TEXT("fade_out")));
	const FString Root = Get(TEXT("root_motion"));
	Info.RootMotion = Root == TEXT("in_place") ? EOKRootMotion::InPlace : Root == TEXT("root_motion") ? EOKRootMotion::RootMotion : EOKRootMotion::AsAuthored;
	if (!Get(TEXT("ik_hands")).IsEmpty()) Info.HandIK = FCString::Atof(*Get(TEXT("ik_hands")));
	if (!Get(TEXT("ik_feet")).IsEmpty()) Info.FootIK = FCString::Atof(*Get(TEXT("ik_feet")));
	Info.bFootLock = Get(TEXT("foot_lock")) != TEXT("0");
	return true;
}

bool UOKAnimSequence::CheckQuality(TArray<FQualityIssue>& OutIssues, FString& OutError) const
{
	OutIssues.Reset();
	const UOKSkeletonFamily* Fam = UOKAnimationLibrary::GetDefaultFamily();
	if (Data.IsEmpty() || !Fam) { OutError = TEXT("no animation data / family"); return false; }
	FString Json;
	if (!FOkAnimBridge::Quality(Data, Fam->RestHeaderJson, Json)) { OutError = FOkAnimBridge::LastError(); return false; }
	TArray<TSharedPtr<FJsonValue>> Values;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Values)) { OutError = TEXT("quality json"); return false; }
	for (const TSharedPtr<FJsonValue>& V : Values)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O) continue;
		FQualityIssue& I = OutIssues.AddDefaulted_GetRef();
		I.Kind = O->GetStringField(TEXT("kind"));
		I.Bone = FName(O->GetStringField(TEXT("bone")));
		I.Start = O->GetIntegerField(TEXT("start"));
		I.End = O->GetIntegerField(TEXT("end"));
		I.Value = float(O->GetNumberField(TEXT("value")));
	}
	return true;
}

bool UOKAnimSequence::ApplyModifier(const FString& OpJson, FString& OutError)
{
	if (Data.IsEmpty()) { OutError = TEXT("no animation data"); return false; }
	if (KeyLayer.Num() && !CollapseLayer(OutError)) return false;
	// Positions for the foot detection: the default family's rest (the KK family every OK animation is made for).
	const UOKSkeletonFamily* Fam = GetDefault<UOKAnimationSettings>()->DefaultFamily.LoadSynchronous();
	if (!Fam) Fam = UOKAnimationLibrary::GetDefaultFamily();
	TArray<uint8> Out;
	if (!FOkAnimBridge::Modify(GetBaseData(), OpJson, Fam ? Fam->RestHeaderJson : FString(), Out)) { OutError = FOkAnimBridge::LastError(); return false; }
	SourceData.Reset();
	return SetData(Out, OutError);
}

bool UOKAnimSequence::ApplyEdits(FString& OutError)
{
	if (Data.IsEmpty()) { OutError = TEXT("no animation data"); return false; }
	TSharedRef<FJsonObject> Patch = MakeShared<FJsonObject>();
	Patch->SetBoolField(TEXT("looping"), bLooping);
	Patch->SetNumberField(TEXT("scene"), int32(Scene));
	Patch->SetArrayField(TEXT("contacts"), ContactsJson(Contacts));
	TArray<TSharedPtr<FJsonValue>> CurveJson;
	for (const FOKCurveTrack& Track : Curves)
	{
		if (Track.Name.IsNone()) continue;
		TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("name"), Track.Name.ToString());
		TArray<TSharedPtr<FJsonValue>> Values;
		for (int32 F = 0; F < Frames; ++F) Values.Add(MakeShared<FJsonValueNumber>(Track.Curve.GetRichCurveConst()->Eval(F / FMath::Max(Fps, 1.f))));
		C->SetArrayField(TEXT("values"), Values);
		CurveJson.Add(MakeShared<FJsonValueObject>(C));
	}
	Patch->SetArrayField(TEXT("curves"), CurveJson);
	// The structured parameters go into the meta under fixed keys.
	auto Set = [&](const TCHAR* Key, const FString& Value) { if (Value.IsEmpty()) Meta.Remove(Key); else Meta.Add(Key, Value); };
	Set(TEXT("category"), Info.Category);
	Set(TEXT("tags"), FString::Join(Info.Tags, TEXT(",")));
	Set(TEXT("source"), Info.Source.IsEmpty() ? Meta.FindRef(TEXT("source")) : Info.Source);
	Set(TEXT("license"), Info.License);
	Set(TEXT("fade_in"), FString::SanitizeFloat(Info.FadeIn));
	Set(TEXT("fade_out"), FString::SanitizeFloat(Info.FadeOut));
	Set(TEXT("root_motion"), Info.RootMotion == EOKRootMotion::InPlace ? FString(TEXT("in_place")) : Info.RootMotion == EOKRootMotion::RootMotion ? FString(TEXT("root_motion")) : FString());
	Set(TEXT("ik_hands"), FString::SanitizeFloat(Info.HandIK));
	Set(TEXT("ik_feet"), FString::SanitizeFloat(Info.FootIK));
	Set(TEXT("foot_lock"), Info.bFootLock ? FString() : FString(TEXT("0")));
	TSharedRef<FJsonObject> MetaJson = MakeShared<FJsonObject>();
	for (const auto& Pair : Meta) MetaJson->SetStringField(Pair.Key, Pair.Value);
	Patch->SetObjectField(TEXT("meta"), MetaJson);
	// Parameters live in the base; the layer is baked on top again.
	TArray<uint8> Out;
	if (!FOkAnimBridge::Edit(GetBaseData(), ToString(Patch), Out)) { OutError = FOkAnimBridge::LastError(); return false; }
	(SourceData.IsEmpty() ? Data : SourceData) = MoveTemp(Out);
	return Rebake(OutError);
}

bool UOKAnimSequence::SampleOnto(const FString& TargetJson, float Time, TArray<float>& Out) const
{
	return !Data.IsEmpty() && FOkAnimBridge::SampleOnto(Data, TargetJson, Time, Out);
}

TSharedPtr<FJsonObject> UOKAnimSequence::BuildSpecNode(FOKSpecBuilder& Builder) const
{
	if (Data.IsEmpty()) { Builder.Error = FString::Printf(TEXT("%s has no animation data"), *GetPathName()); return nullptr; }
	TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("type"), TEXT("anim"));
	J->SetNumberField(TEXT("anim"), Builder.AddBlob(this, Data));
	J->SetNumberField(TEXT("speed"), FMath::Max(0.01f, RateScale));
	return J;
}

#if WITH_EDITOR
void UOKAnimSequence::PostEditUndo()
{
	Super::PostEditUndo();
	FString Error;
	if (!Data.IsEmpty() && !Rebake(Error)) UE_LOG(LogTemp, Error, TEXT("OK animation %s: %s"), *GetName(), *Error);
}

void UOKAnimSequence::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	const FName Name = Event.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UOKAnimSequence, RateScale) || Name == GET_MEMBER_NAME_CHECKED(UOKAnimSequence, EditProfile) || Name == GET_MEMBER_NAME_CHECKED(UOKAnimSequence, EditProxy)) return;
	FString Error;
	if (!Data.IsEmpty() && !ApplyEdits(Error)) UE_LOG(LogTemp, Error, TEXT("OK animation %s: %s"), *GetName(), *Error);
}
#endif

void UOKAnimSequence::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);
	Context.AddTag(FAssetRegistryTag(TEXT("Family"), Family.ToString(), FAssetRegistryTag::TT_Alphabetical));
	Context.AddTag(FAssetRegistryTag(TEXT("Duration"), FString::SanitizeFloat(Duration), FAssetRegistryTag::TT_Numerical));
	Context.AddTag(FAssetRegistryTag(TEXT("Frames"), FString::FromInt(Frames), FAssetRegistryTag::TT_Numerical));
	Context.AddTag(FAssetRegistryTag(TEXT("Bones"), FString::FromInt(Bones.Num()), FAssetRegistryTag::TT_Numerical));
	if (const FString* Source = Meta.Find(TEXT("source"))) Context.AddTag(FAssetRegistryTag(TEXT("Source"), *Source, FAssetRegistryTag::TT_Alphabetical));
}

// ------------------------------------------------------------------------------------------------ group

bool UOKGroupAnimSequence::SetFromPackage(const TArray<uint8>& Package, UObject* SequenceOuter, FString& OutError)
{
	FString SpecText;
	TArray<TArray<uint8>> Anims;
	if (!FOkAnimBridge::ParseGroupPackage(Package, SpecText, Anims)) { OutError = TEXT("not an OKGROUP1 package"); return false; }
	const TSharedPtr<FJsonObject> Spec = Parse(SpecText);
	if (!Spec || Spec->GetStringField(TEXT("type")) != TEXT("group")) { OutError = TEXT("package spec is not a group"); return false; }
	TArray<UOKAnimSequence*> Sequences;
	for (int32 I = 0; I < Anims.Num(); ++I)
	{
		UOKAnimSequence* Seq = NewObject<UOKAnimSequence>(SequenceOuter ? SequenceOuter : GetOuter(),
			MakeUniqueObjectName(SequenceOuter ? SequenceOuter : GetOuter(), UOKAnimSequence::StaticClass(), FName(*FString::Printf(TEXT("%s_Role%d"), *GetName(), I))),
			GetFlags() & (RF_Public | RF_Standalone | RF_Transactional | RF_Transient));
		if (!Seq->SetData(Anims[I], OutError)) return false;
		Sequences.Add(Seq);
	}
	Roles.Reset();
	for (const TSharedPtr<FJsonValue>& R : Spec->GetArrayField(TEXT("roles")))
	{
		const TSharedPtr<FJsonObject> O = R->AsObject();
		FOKGroupRole& Role = Roles.AddDefaulted_GetRef();
		Role.Name = FName(O->GetStringField(TEXT("name")));
		const int32 Anim = int32(O->GetObjectField(TEXT("spec"))->GetNumberField(TEXT("anim")));
		if (!Sequences.IsValidIndex(Anim)) { OutError = TEXT("role animation missing"); return false; }
		Role.Animation = Sequences[Anim];
	}
	Contacts.Reset();
	for (const TSharedPtr<FJsonValue>& C : Spec->GetArrayField(TEXT("contacts"))) Contacts.Add(FOKContact::FromJson(*C->AsObject()));
	return true;
}

TSharedPtr<FJsonObject> UOKGroupAnimSequence::BuildSpecNode(FOKSpecBuilder& Builder) const
{
	if (Roles.IsEmpty()) { Builder.Error = FString::Printf(TEXT("%s has no roles"), *GetPathName()); return nullptr; }
	TArray<TSharedPtr<FJsonValue>> RoleJson;
	for (const FOKGroupRole& Role : Roles)
	{
		if (Role.Animation && Role.Animation->GetRoleCount() != 1)
		{
			Builder.Error = FString::Printf(TEXT("%s: role %s plays a %d-role asset"), *GetPathName(), *Role.Name.ToString(), Role.Animation->GetRoleCount());
			return nullptr;
		}
		const TSharedPtr<FJsonObject> Spec = Builder.Build(Role.Animation);
		if (!Spec) return nullptr;
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("name"), Role.Name.ToString());
		R->SetObjectField(TEXT("spec"), Spec);
		R->SetArrayField(TEXT("offset"), Numbers({ Role.Offset.X, Role.Offset.Y, Role.Offset.Z }));
		R->SetNumberField(TEXT("yaw"), Role.Yaw);
		RoleJson.Add(MakeShared<FJsonValueObject>(R));
	}
	for (const FOKContact& C : Contacts)
	{
		if (C.Role >= Roles.Num() || C.TargetRole >= Roles.Num())
		{
			Builder.Error = FString::Printf(TEXT("%s: contact %s -> %s names a missing role"), *GetPathName(), *C.Effector.ToString(), *C.TargetBone.ToString());
			return nullptr;
		}
	}
	TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("type"), TEXT("group"));
	J->SetArrayField(TEXT("roles"), RoleJson);
	J->SetArrayField(TEXT("contacts"), ContactsJson(Contacts));
	return J;
}

// ------------------------------------------------------------------------------------------------ blend space

TSharedPtr<FJsonObject> UOKBlendSpace::BuildSpecNode(FOKSpecBuilder& Builder) const
{
	if (Axes.IsEmpty() || Axes.Num() > 2) { Builder.Error = FString::Printf(TEXT("%s: a blend space has 1 or 2 axes"), *GetPathName()); return nullptr; }
	if (Samples.IsEmpty()) { Builder.Error = FString::Printf(TEXT("%s has no samples"), *GetPathName()); return nullptr; }
	TArray<TSharedPtr<FJsonValue>> AxisJson, SampleJson;
	for (const FOKBlendAxis& A : Axes)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), A.Name.ToString());
		O->SetNumberField(TEXT("min"), A.Min);
		O->SetNumberField(TEXT("max"), A.Max);
		AxisJson.Add(MakeShared<FJsonValueObject>(O));
	}
	for (const FOKBlendSample& S : Samples)
	{
		const TSharedPtr<FJsonObject> Spec = Builder.Build(S.Animation);
		if (!Spec) return nullptr;
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetArrayField(TEXT("at"), Axes.Num() == 1 ? Numbers({ S.Position.X }) : Numbers({ S.Position.X, S.Position.Y }));
		O->SetObjectField(TEXT("spec"), Spec);
		SampleJson.Add(MakeShared<FJsonValueObject>(O));
	}
	TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("type"), TEXT("blend"));
	J->SetArrayField(TEXT("axes"), AxisJson);
	J->SetArrayField(TEXT("samples"), SampleJson);
	J->SetBoolField(TEXT("sync"), bSyncPhase);
	return J;
}

int32 UOKBlendSpace::GetRoleCount() const
{
	for (const FOKBlendSample& S : Samples) if (S.Animation && S.Animation != this) return S.Animation->GetRoleCount();
	return 1;
}

TArray<FName> UOKBlendSpace::GetParameterNames() const
{
	TArray<FName> Out;
	for (const FOKBlendAxis& A : Axes) Out.Add(A.Name);
	return Out;
}

// ------------------------------------------------------------------------------------------------ montage

float UOKAnimMontage::GetSectionStart(FName Section) const
{
	float End = 0.f;
	for (const FOKMontageSection& S : Sections)
	{
		const float Length = S.Length > 0.f ? S.Length : (S.Animation ? FMath::Max(0.f, S.Animation->GetPlayLength() - S.From) / FMath::Max(0.01f, S.Rate) : 0.f);
		const float Start = S.Start >= 0.f ? S.Start : FMath::Max(0.f, End - S.BlendIn);
		if (S.Name == Section) return Start;
		End = Start + Length;
	}
	return -1.f;
}

TArray<TPair<float, float>> UOKAnimMontage::GetSectionRanges() const
{
	TArray<TPair<float, float>> Out;
	float End = 0.f;
	for (const FOKMontageSection& S : Sections)
	{
		const float Length = S.Length > 0.f ? S.Length : (S.Animation ? FMath::Max(0.f, S.Animation->GetPlayLength() - S.From) / FMath::Max(0.01f, S.Rate) : 0.f);
		const float Start = S.Start >= 0.f ? S.Start : FMath::Max(0.f, End - S.BlendIn);
		Out.Emplace(Start, Start + Length);
		End = Start + Length;
	}
	return Out;
}

TSharedPtr<FJsonObject> UOKAnimMontage::BuildSpecNode(FOKSpecBuilder& Builder) const
{
	if (Sections.IsEmpty()) { Builder.Error = FString::Printf(TEXT("%s has no sections"), *GetPathName()); return nullptr; }
	TArray<TSharedPtr<FJsonValue>> SectionJson;
	float End = 0.f;
	for (const FOKMontageSection& S : Sections)
	{
		const TSharedPtr<FJsonObject> Spec = Builder.Build(S.Animation);
		if (!Spec) return nullptr;
		const float Rate = FMath::Max(0.01f, S.Rate);
		const float Length = S.Length > 0.f ? S.Length : FMath::Max(0.f, S.Animation->GetPlayLength() - S.From) / Rate;
		if (Length <= 0.f) { Builder.Error = FString::Printf(TEXT("%s: section %s is empty"), *GetPathName(), *S.Name.ToString()); return nullptr; }
		const float Start = S.Start >= 0.f ? S.Start : FMath::Max(0.f, End - S.BlendIn);
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetObjectField(TEXT("spec"), Spec);
		O->SetStringField(TEXT("name"), S.Name.ToString());
		O->SetNumberField(TEXT("start"), Start);
		O->SetNumberField(TEXT("length"), Length);
		O->SetNumberField(TEXT("from"), S.From);
		O->SetNumberField(TEXT("rate"), Rate);
		O->SetNumberField(TEXT("blend_in"), S.BlendIn);
		SectionJson.Add(MakeShared<FJsonValueObject>(O));
		End = Start + Length;
	}
	TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("type"), TEXT("sequence"));
	J->SetArrayField(TEXT("sections"), SectionJson);
	J->SetBoolField(TEXT("looping"), bLooping);
	return J;
}

int32 UOKAnimMontage::GetRoleCount() const
{
	for (const FOKMontageSection& S : Sections) if (S.Animation && S.Animation != this) return S.Animation->GetRoleCount();
	return 1;
}

TArray<FName> UOKAnimMontage::GetParameterNames() const
{
	for (const FOKMontageSection& S : Sections) if (S.Animation && S.Animation != this) { const TArray<FName> N = S.Animation->GetParameterNames(); if (!N.IsEmpty()) return N; }
	return {};
}

// ------------------------------------------------------------------------------------------------ family / profile

TArray<FName> UOKSkeletonFamily::GetBoneNames() const
{
	TArray<FName> Out;
	if (const TSharedPtr<FJsonObject> J = Parse(RestHeaderJson))
	{
		const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
		if (J->TryGetArrayField(TEXT("bones"), Bones))
			for (const TSharedPtr<FJsonValue>& B : *Bones) Out.Add(FName(B->AsObject()->GetStringField(TEXT("name"))));
	}
	return Out;
}

namespace
{
	struct FPresetTable
	{
		TMap<FName, TMap<FName, FName>> Presets;
		FPresetTable()
		{
			auto Sides = [](TMap<FName, FName>& M, const TCHAR* Source, const TCHAR* Target)
			{
				// "{s}" / "{S}": side letter (l / L, r / R), "{side}": Left / Right.
				for (int32 Side = 0; Side < 2; ++Side)
				{
					FString S = Source, T = Target;
					S.ReplaceInline(TEXT("{s}"), Side ? TEXT("r") : TEXT("l"), ESearchCase::CaseSensitive);
					S.ReplaceInline(TEXT("{S}"), Side ? TEXT("R") : TEXT("L"), ESearchCase::CaseSensitive);
					S.ReplaceInline(TEXT("{side}"), Side ? TEXT("Right") : TEXT("Left"), ESearchCase::CaseSensitive);
					T.ReplaceInline(TEXT("{S}"), Side ? TEXT("R") : TEXT("L"), ESearchCase::CaseSensitive);
					M.Add(FName(S), FName(T));
				}
			};
			auto Fingers = [&](TMap<FName, FName>& M, const TCHAR* Pattern, std::initializer_list<const TCHAR*> SourceNames)
			{
				// Pattern: source with {f} (finger name) and {n} (1..3).
				static const TCHAR* Kk[] = { TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("little") };
				int32 F = 0;
				for (const TCHAR* Finger : SourceNames)
				{
					for (int32 N = 1; N <= 3; ++N)
					{
						FString S = Pattern;
						S.ReplaceInline(TEXT("{f}"), Finger, ESearchCase::CaseSensitive);
						S.ReplaceInline(TEXT("{n}"), *FString::FromInt(N), ESearchCase::CaseSensitive);
						S.ReplaceInline(TEXT("{n0}"), *FString::FromInt(N - 1), ESearchCase::CaseSensitive);
						Sides(M, *S, *FString::Printf(TEXT("cf_j_%s0%d_{S}"), Kk[F], N));
					}
					++F;
				}
			};
			auto Limbs = [&](TMap<FName, FName>& M, const TCHAR* Clavicle, const TCHAR* Upper, const TCHAR* Lower, const TCHAR* Hand,
				const TCHAR* Thigh, const TCHAR* Calf, const TCHAR* Foot, const TCHAR* Toe)
			{
				Sides(M, Clavicle, TEXT("cf_j_shoulder_{S}"));
				Sides(M, Upper, TEXT("cf_j_arm00_{S}"));
				Sides(M, Lower, TEXT("cf_j_forearm01_{S}"));
				Sides(M, Hand, TEXT("cf_j_hand_{S}"));
				Sides(M, Thigh, TEXT("cf_j_thigh00_{S}"));
				Sides(M, Calf, TEXT("cf_j_leg01_{S}"));
				Sides(M, Foot, TEXT("cf_j_leg03_{S}"));
				Sides(M, Toe, TEXT("cf_j_toes_{S}"));
			};
			{
				TMap<FName, FName>& M = Presets.Add(TEXT("UE5Mannequin"));
				M.Add(TEXT("pelvis"), TEXT("cf_j_hips"));
				M.Add(TEXT("spine_01"), TEXT("cf_j_spine01"));
				M.Add(TEXT("spine_03"), TEXT("cf_j_spine02"));
				M.Add(TEXT("spine_05"), TEXT("cf_j_spine03"));
				M.Add(TEXT("neck_01"), TEXT("cf_j_neck"));
				M.Add(TEXT("head"), TEXT("cf_j_head"));
				Limbs(M, TEXT("clavicle_{s}"), TEXT("upperarm_{s}"), TEXT("lowerarm_{s}"), TEXT("hand_{s}"), TEXT("thigh_{s}"), TEXT("calf_{s}"), TEXT("foot_{s}"), TEXT("ball_{s}"));
				Fingers(M, TEXT("{f}_0{n}_{s}"), { TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") });
			}
			{
				TMap<FName, FName>& M = Presets.Add(TEXT("UE4Mannequin"));
				M.Add(TEXT("pelvis"), TEXT("cf_j_hips"));
				M.Add(TEXT("spine_01"), TEXT("cf_j_spine01"));
				M.Add(TEXT("spine_02"), TEXT("cf_j_spine02"));
				M.Add(TEXT("spine_03"), TEXT("cf_j_spine03"));
				M.Add(TEXT("neck_01"), TEXT("cf_j_neck"));
				M.Add(TEXT("head"), TEXT("cf_j_head"));
				Limbs(M, TEXT("clavicle_{s}"), TEXT("upperarm_{s}"), TEXT("lowerarm_{s}"), TEXT("hand_{s}"), TEXT("thigh_{s}"), TEXT("calf_{s}"), TEXT("foot_{s}"), TEXT("ball_{s}"));
				Fingers(M, TEXT("{f}_0{n}_{s}"), { TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") });
			}
			{
				// 3ds Max Biped ("Bip001 L UpperArm"; the prefix and separators are normalised away).
				TMap<FName, FName>& M = Presets.Add(TEXT("Biped"));
				M.Add(TEXT("Pelvis"), TEXT("cf_j_hips"));
				M.Add(TEXT("Spine"), TEXT("cf_j_spine01"));
				M.Add(TEXT("Spine1"), TEXT("cf_j_spine02"));
				M.Add(TEXT("Spine2"), TEXT("cf_j_spine03"));
				M.Add(TEXT("Neck"), TEXT("cf_j_neck"));
				M.Add(TEXT("Head"), TEXT("cf_j_head"));
				Limbs(M, TEXT("{S} Clavicle"), TEXT("{S} UpperArm"), TEXT("{S} Forearm"), TEXT("{S} Hand"), TEXT("{S} Thigh"), TEXT("{S} Calf"), TEXT("{S} Foot"), TEXT("{S} Toe0"));
				// Finger0 (thumb), Finger01, Finger02; Finger1, Finger11, Finger12...
				static const TCHAR* Kk[] = { TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("little") };
				for (int32 F = 0; F < 5; ++F)
				{
					Sides(M, *FString::Printf(TEXT("{S} Finger%d"), F), *FString::Printf(TEXT("cf_j_%s01_{S}"), Kk[F]));
					Sides(M, *FString::Printf(TEXT("{S} Finger%d1"), F), *FString::Printf(TEXT("cf_j_%s02_{S}"), Kk[F]));
					Sides(M, *FString::Printf(TEXT("{S} Finger%d2"), F), *FString::Printf(TEXT("cf_j_%s03_{S}"), Kk[F]));
				}
			}
			{
				TMap<FName, FName>& M = Presets.Add(TEXT("Mixamo"));
				M.Add(TEXT("Hips"), TEXT("cf_j_hips"));
				M.Add(TEXT("Spine"), TEXT("cf_j_spine01"));
				M.Add(TEXT("Spine1"), TEXT("cf_j_spine02"));
				M.Add(TEXT("Spine2"), TEXT("cf_j_spine03"));
				M.Add(TEXT("Neck"), TEXT("cf_j_neck"));
				M.Add(TEXT("Head"), TEXT("cf_j_head"));
				Limbs(M, TEXT("{side}Shoulder"), TEXT("{side}Arm"), TEXT("{side}ForeArm"), TEXT("{side}Hand"), TEXT("{side}UpLeg"), TEXT("{side}Leg"), TEXT("{side}Foot"), TEXT("{side}ToeBase"));
				Fingers(M, TEXT("{side}Hand{f}{n}"), { TEXT("Thumb"), TEXT("Index"), TEXT("Middle"), TEXT("Ring"), TEXT("Pinky") });
			}
			{
				// CMU motion capture BVH (Hips, LowerBack, Spine, Spine1, Neck, Neck1, Head; LeftUpLeg ... LeftToeBase).
				TMap<FName, FName>& M = Presets.Add(TEXT("CMU"));
				M.Add(TEXT("Hips"), TEXT("cf_j_hips"));
				M.Add(TEXT("LowerBack"), TEXT("cf_j_spine01"));
				M.Add(TEXT("Spine"), TEXT("cf_j_spine02"));
				M.Add(TEXT("Spine1"), TEXT("cf_j_spine03"));
				M.Add(TEXT("Neck"), TEXT("cf_j_neck"));
				M.Add(TEXT("Head"), TEXT("cf_j_head"));
				Limbs(M, TEXT("{side}Shoulder"), TEXT("{side}Arm"), TEXT("{side}ForeArm"), TEXT("{side}Hand"), TEXT("{side}UpLeg"), TEXT("{side}Leg"), TEXT("{side}Foot"), TEXT("{side}ToeBase"));
			}
			Presets.Add(TEXT("KK"));
		}
	};
	const FPresetTable& PresetTable() { static FPresetTable Table; return Table; }
}

const TMap<FName, FName>* UOKRetargetProfile::GetPreset(FName Preset) { return PresetTable().Presets.Find(Preset); }

TArray<FName> UOKRetargetProfile::GetPresetNames()
{
	TArray<FName> Out;
	PresetTable().Presets.GetKeys(Out);
	return Out;
}

bool UOKRetargetProfile::ApplyPreset(FName Preset)
{
	const TMap<FName, FName>* Map = GetPreset(Preset);
	if (!Map) return false;
	BoneMap = *Map;
	return true;
}

FName UOKRetargetProfile::DetectPreset(const TArray<FName>& BoneNames)
{
	TSet<FString> Names;
	int32 Kk = 0;
	for (const FName N : BoneNames)
	{
		Names.Add(FOKUnrealSkeleton::NormalizeName(N.ToString()));
		Kk += N.ToString().StartsWith(TEXT("cf_j_")) ? 1 : 0;
	}
	if (Kk >= 20) return TEXT("KK");
	FName Best = NAME_None;
	int32 BestHits = 8;
	for (const auto& Pair : PresetTable().Presets)
	{
		int32 Hits = 0;
		for (const auto& Bone : Pair.Value) Hits += Names.Contains(FOKUnrealSkeleton::NormalizeName(Bone.Key.ToString())) ? 1 : 0;
		// UE5 and UE4 mannequins share most names: the finer mapping wins ties through spine_05.
		if (Pair.Key == FName(TEXT("UE5Mannequin")) && Names.Contains(TEXT("spine05"))) Hits += 2;
		// CMU and Mixamo share the limb names: CMU has a LowerBack and no Spine2.
		if (Pair.Key == FName(TEXT("CMU")) && Names.Contains(TEXT("lowerback")) && !Names.Contains(TEXT("spine2"))) Hits += 2;
		if (Hits > BestHits) { BestHits = Hits; Best = Pair.Key; }
	}
	return Best;
}


FName UOKSkeletonFamily::MirrorOf(FName Bone) const
{
	if (const FName* P = MirrorPairs.Find(Bone)) return *P;
	for (const TPair<FName, FName>& P : MirrorPairs) if (P.Value == Bone) return P.Key;
	const FString S = Bone.ToString();
	FString Other;
	if (S.EndsWith(TEXT("_L"))) Other = S.LeftChop(2) + TEXT("_R");
	else if (S.EndsWith(TEXT("_R"))) Other = S.LeftChop(2) + TEXT("_L");
	return Other.IsEmpty() ? NAME_None : FName(*Other);
}

int32 UOKSkeletonFamily::AutoMirrorPairs()
{
	const TArray<FName> Bones = GetBoneNames();
	const TSet<FName> Known(Bones);
	int32 Added = 0;
	for (const FName B : Bones)
	{
		const FString S = B.ToString();
		if (!S.EndsWith(TEXT("_L"))) continue;
		const FName R(*(S.LeftChop(2) + TEXT("_R")));
		if (!Known.Contains(R) || MirrorPairs.Contains(B)) continue;
		MirrorPairs.Add(B, R);
		++Added;
	}
	return Added;
}
