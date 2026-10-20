#include "OKSceneDocument.h"
#include "Engine/Scene.h"
#include "OKStudioCatalog.h"
#include "OKStudioPost.h"
#include "OKStudioBloom.h"
#include "ok_extract.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

int32 UOKSceneDocument::AddObject(EOKSceneObjectKind Kind, const FString& Name, int32 Parent)
{
	if (Parent >= 0 && !Find(Parent)) Parent = -1;
	FOKSceneObject& Object = Objects.AddDefaulted_GetRef();
	Object.Id = NextId++;
	Object.Parent = Parent;
	Object.Kind = Kind;
	Object.Name = Name.IsEmpty() ? FString::Printf(TEXT("%s %d"), *StaticEnum<EOKSceneObjectKind>()->GetNameStringByValue(int64(Kind)), Object.Id) : Name;
	const int32 Id = Object.Id;
	NotifyChanged();
	return Id;
}

bool UOKSceneDocument::RemoveObject(int32 Id)
{
	if (!Find(Id)) return false;
	TArray<int32> Doomed = { Id };
	for (int32 I = 0; I < Doomed.Num(); ++I) Doomed.Append(Children(Doomed[I]));
	Objects.RemoveAll([&](const FOKSceneObject& O) { return Doomed.Contains(O.Id); });
	if (Doomed.Contains(ActiveCamera)) ActiveCamera = -1;
	NotifyChanged();
	return true;
}

bool UOKSceneDocument::Reparent(int32 Id, int32 NewParent)
{
	FOKSceneObject* Object = Find(Id);
	if (!Object || (NewParent >= 0 && !Find(NewParent))) return false;
	for (int32 P = NewParent; P >= 0; P = Find(P) ? Find(P)->Parent : -1) if (P == Id) return false;
	const FTransform World = WorldTransform(Id);
	const FTransform ParentWorld = ChildFrame(NewParent);
	Object = Find(Id);
	Object->Parent = NewParent;
	Object->AttachBone.Reset();
	Object->Transform = World.GetRelativeTransform(ParentWorld);
	NotifyChanged();
	return true;
}

int32 UOKSceneDocument::Duplicate(int32 Id)
{
	if (!Find(Id)) return -1;
	TMap<int32, int32> IdMap;
	TArray<int32> Order = { Id };
	for (int32 I = 0; I < Order.Num(); ++I) Order.Append(Children(Order[I]));
	for (int32 Old : Order)
	{
		FOKSceneObject Copy = *Find(Old);
		Copy.Id = NextId++;
		Copy.Parent = Old == Id ? Copy.Parent : IdMap.FindChecked(Copy.Parent);
		if (Old == Id) Copy.Name += TEXT(" copy");
		IdMap.Add(Old, Copy.Id);
		Objects.Add(Copy);
	}
	NotifyChanged();
	return IdMap[Id];
}

FOKSceneObject* UOKSceneDocument::Find(int32 Id)
{
	return Objects.FindByPredicate([Id](const FOKSceneObject& O) { return O.Id == Id; });
}

const FOKSceneObject* UOKSceneDocument::Find(int32 Id) const
{
	return Objects.FindByPredicate([Id](const FOKSceneObject& O) { return O.Id == Id; });
}

TArray<int32> UOKSceneDocument::Children(int32 Parent) const
{
	TArray<int32> Out;
	for (const FOKSceneObject& O : Objects) if (O.Parent == Parent) Out.Add(O.Id);
	return Out;
}

FTransform UOKSceneDocument::WorldTransform(int32 Id) const
{
	FTransform World = FTransform::Identity;
	// Bounded walk: a corrupt file with a parent cycle must not hang.
	for (int32 Steps = 0, Cur = Id; Cur >= 0 && Steps < 256; ++Steps)
	{
		const FOKSceneObject* O = Find(Cur);
		if (!O) break;
		World = World * O->Transform;
		const FOKSceneObject* P = Find(O->Parent);
		if (P && P->Kind == EOKSceneObjectKind::Item) World = World * FOKStudioCatalog::Get().AnimatedItemChildPoint(P->Item.Group, P->Item.Category, P->Item.No, P->Item.AnimePattern, P->Item.AnimeTime, ItemAnimationSeconds, P->Item.AnimeSpeed);
		FTransform Bone;
		if (P && P->Kind == EOKSceneObjectKind::Character && !O->AttachBone.IsEmpty() && BoneFrame && BoneFrame(P->Id, O->AttachBone, Bone)) World = World * Bone;
		Cur = O->Parent;
	}
	return World;
}

FTransform UOKSceneDocument::ChildFrame(int32 Parent) const
{
	const FOKSceneObject* P = Find(Parent);
	if (!P) return FTransform::Identity;
	const FTransform World = WorldTransform(Parent);
	return P->Kind == EOKSceneObjectKind::Item ? FOKStudioCatalog::Get().AnimatedItemChildPoint(P->Item.Group, P->Item.Category, P->Item.No, P->Item.AnimePattern, P->Item.AnimeTime, ItemAnimationSeconds, P->Item.AnimeSpeed) * World : World;
}

FTransform UOKSceneDocument::ParentFrame(int32 Id) const
{
	const FOKSceneObject* O = Find(Id);
	if (!O) return FTransform::Identity;
	const FTransform Frame = ChildFrame(O->Parent);
	const FOKSceneObject* P = Find(O->Parent);
	FTransform Bone;
	if (P && P->Kind == EOKSceneObjectKind::Character && !O->AttachBone.IsEmpty() && BoneFrame && BoneFrame(P->Id, O->AttachBone, Bone)) return Bone * Frame;
	return Frame;
}

bool UOKSceneDocument::IsShown(int32 Id) const
{
	for (int32 Steps = 0, Cur = Id; Cur >= 0 && Steps < 256; ++Steps)
	{
		const FOKSceneObject* O = Find(Cur);
		if (!O) break;
		if (!O->bVisible) return false;
		Cur = O->Parent;
	}
	return true;
}

FString UOKSceneDocument::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("format"), TEXT("okscene"));
	Root->SetNumberField(TEXT("version"), FormatVersion);
	Root->SetNumberField(TEXT("active_camera"), ActiveCamera);
	Root->SetNumberField(TEXT("next_id"), NextId);
	Root->SetNumberField(TEXT("map"), Map);
	if (!MapTransform.Equals(FTransform::Identity)) Root->SetStringField(TEXT("map_transform"), MapTransform.ToString());
	Root->SetNumberField(TEXT("duration"), Duration);
	if (TSharedPtr<FJsonObject> PostJson = FJsonObjectConverter::UStructToJsonObject(Post)) Root->SetObjectField(TEXT("post"), PostJson);
	TArray<TSharedPtr<FJsonValue>> TrackJson;
	for (const FOKSceneTrack& T : Tracks) if (TSharedPtr<FJsonObject> J = FJsonObjectConverter::UStructToJsonObject(T)) TrackJson.Add(MakeShared<FJsonValueObject>(J));
	Root->SetArrayField(TEXT("tracks"), TrackJson);
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FOKSceneObject& O : Objects)
	{
		TSharedPtr<FJsonObject> J = FJsonObjectConverter::UStructToJsonObject(O);
		if (!J) continue;
		// Only the payload of the object's kind is written.
		if (O.Kind != EOKSceneObjectKind::Character) J->RemoveField(TEXT("character"));
		if (O.Kind != EOKSceneObjectKind::Item) J->RemoveField(TEXT("item"));
		if (O.Kind != EOKSceneObjectKind::Light) J->RemoveField(TEXT("light"));
		if (O.Kind != EOKSceneObjectKind::Camera) J->RemoveField(TEXT("camera"));
		if (O.Kind != EOKSceneObjectKind::Effect) J->RemoveField(TEXT("effect"));
		Items.Add(MakeShared<FJsonValueObject>(J));
	}
	Root->SetArrayField(TEXT("objects"), Items);
	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

bool UOKSceneDocument::FromJson(const FString& Json, FString& OutError)
{
    ItemAnimationSeconds = 0.f;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { OutError = TEXT("not JSON"); return false; }
	if (Root->GetStringField(TEXT("format")) != TEXT("okscene")) { OutError = TEXT("not an .okscene file"); return false; }
	const int32 Version = int32(Root->GetNumberField(TEXT("version")));
	if (Version < 1 || Version > FormatVersion) { OutError = FString::Printf(TEXT("unsupported .okscene version %d"), Version); return false; }
	TArray<FOKSceneObject> Loaded;
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (Root->TryGetArrayField(TEXT("objects"), Items))
	{
		for (const TSharedPtr<FJsonValue>& V : *Items)
		{
			FOKSceneObject O;
			if (!V->AsObject() || !FJsonObjectConverter::JsonObjectToUStruct(V->AsObject().ToSharedRef(), &O)) { OutError = TEXT("bad scene object"); return false; }
			Loaded.Add(O);
		}
	}
	TSet<int32> Ids;
	int32 MaxId = 0;
	for (const FOKSceneObject& O : Loaded)
	{
		if (O.Id <= 0 || Ids.Contains(O.Id)) { OutError = FString::Printf(TEXT("duplicate or invalid object id %d"), O.Id); return false; }
		Ids.Add(O.Id);
		MaxId = FMath::Max(MaxId, O.Id);
	}
	for (FOKSceneObject& O : Loaded) if (O.Parent >= 0 && !Ids.Contains(O.Parent)) O.Parent = -1;
	Objects = MoveTemp(Loaded);
	ActiveCamera = Root->HasField(TEXT("active_camera")) ? int32(Root->GetNumberField(TEXT("active_camera"))) : -1;
	Map = Root->HasField(TEXT("map")) ? int32(Root->GetNumberField(TEXT("map"))) : -1;
	MapTransform = FTransform::Identity;
	FString MapTransformText;
	if (Root->TryGetStringField(TEXT("map_transform"), MapTransformText)) MapTransform.InitFromString(MapTransformText);
	Duration = Root->HasField(TEXT("duration")) ? float(Root->GetNumberField(TEXT("duration"))) : 10.f;
	Post = FOKScenePost();
	const TSharedPtr<FJsonObject>* PostJson = nullptr;
	if (Root->TryGetObjectField(TEXT("post"), PostJson)) FJsonObjectConverter::JsonObjectToUStruct(PostJson->ToSharedRef(), &Post);
	Tracks.Reset();
	const TArray<TSharedPtr<FJsonValue>>* TrackJson = nullptr;
	if (Root->TryGetArrayField(TEXT("tracks"), TrackJson))
		for (const TSharedPtr<FJsonValue>& V : *TrackJson)
		{
			FOKSceneTrack T;
			if (V->AsObject() && FJsonObjectConverter::JsonObjectToUStruct(V->AsObject().ToSharedRef(), &T) && Ids.Contains(T.ObjectId)) Tracks.Add(MoveTemp(T));
		}
	NextId = FMath::Max(MaxId + 1, Root->HasField(TEXT("next_id")) ? int32(Root->GetNumberField(TEXT("next_id"))) : 1);
	NotifyChanged();
	return true;
}

bool UOKSceneDocument::SaveToFile(const FString& Path, FString& OutError) const
{
	if (!FFileHelper::SaveStringToFile(ToJson(), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { OutError = TEXT("cannot write ") + Path; return false; }
	return true;
}

bool UOKSceneDocument::LoadFromFile(const FString& Path, FString& OutError)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path)) { OutError = TEXT("cannot read ") + Path; return false; }
	return FromJson(Json, OutError);
}

namespace
{
	/** The keyed value of an object: a light's intensity or a camera's field of view. */
	float KeyValue(const FOKSceneObject& O)
	{
		return O.Kind == EOKSceneObjectKind::Light ? O.Light.Intensity : O.Kind == EOKSceneObjectKind::Camera ? O.Camera.Fov : O.Kind == EOKSceneObjectKind::Effect ? O.Effect.Intensity : 0.f;
	}
	void SetKeyValue(FOKSceneObject& O, float V)
	{
		if (O.Kind == EOKSceneObjectKind::Light) O.Light.Intensity = V;
		else if (O.Kind == EOKSceneObjectKind::Camera) O.Camera.Fov = V;
		else if (O.Kind == EOKSceneObjectKind::Effect) O.Effect.Intensity = V;
	}
	constexpr float KeyTolerance = 1.f / 60.f;
}

const FOKSceneTrack* UOKSceneDocument::TrackOf(int32 Id) const
{
	return Tracks.FindByPredicate([Id](const FOKSceneTrack& T) { return T.ObjectId == Id; });
}

void UOKSceneDocument::SetKey(int32 Id, float Time)
{
	const FOKSceneObject* O = Find(Id);
	if (!O) return;
	FOKSceneTrack* T = Tracks.FindByPredicate([Id](const FOKSceneTrack& X) { return X.ObjectId == Id; });
	if (!T) { T = &Tracks.AddDefaulted_GetRef(); T->ObjectId = Id; }
	// Re-keying keeps the key's curve.
	const FOKSceneKey* Old = T->Keys.FindByPredicate([Time](const FOKSceneKey& K) { return FMath::Abs(K.Time - Time) < KeyTolerance; });
	const EOKSceneEase Ease = Old ? Old->Ease : EOKSceneEase::Linear;
	T->Keys.RemoveAll([Time](const FOKSceneKey& K) { return FMath::Abs(K.Time - Time) < KeyTolerance; });
	FOKSceneKey K;
	K.Time = FMath::Max(Time, 0.f);
	K.Ease = Ease;
	K.Transform = O->Transform;
	K.Value = KeyValue(*O);
	if (O->Kind == EOKSceneObjectKind::Character)
	{
		const FOKSceneCharacter& C = O->Character;
		K.Fk = C.Fk;
		K.bCharacterState = true;
		K.EyebrowPattern = C.EyebrowPattern; K.EyesPattern = C.EyesPattern; K.MouthPattern = C.MouthPattern; K.MouthOpen = C.MouthOpen;
		K.HandLeft = C.HandLeft; K.HandRight = C.HandRight; K.ClothesStates = C.ClothesStates;
	}
	T->Keys.Add(K);
	T->Keys.Sort([](const FOKSceneKey& A, const FOKSceneKey& B) { return A.Time < B.Time; });
	Duration = FMath::Max(Duration, K.Time);
	NotifyChanged();
}

FOKSceneKey* UOKSceneDocument::KeyAt(int32 Id, float Time)
{
	FOKSceneTrack* T = Tracks.FindByPredicate([Id](const FOKSceneTrack& X) { return X.ObjectId == Id; });
	return T ? T->Keys.FindByPredicate([Time](const FOKSceneKey& K) { return FMath::Abs(K.Time - Time) < KeyTolerance; }) : nullptr;
}

float UOKSceneDocument::EaseAlpha(EOKSceneEase Ease, float U)
{
	U = FMath::Clamp(U, 0.f, 1.f);
	switch (Ease)
	{
	case EOKSceneEase::EaseInOut: return U < 0.5f ? 2.f * U * U : 1.f - 2.f * (1.f - U) * (1.f - U);
	case EOKSceneEase::EaseIn: return U * U;
	case EOKSceneEase::EaseOut: return 1.f - (1.f - U) * (1.f - U);
	case EOKSceneEase::Step: return 0.f;
	default: return U;
	}
}

bool UOKSceneDocument::RemoveKey(int32 Id, float Time)
{
	FOKSceneTrack* T = Tracks.FindByPredicate([Id](const FOKSceneTrack& X) { return X.ObjectId == Id; });
	if (!T || !T->Keys.RemoveAll([Time](const FOKSceneKey& K) { return FMath::Abs(K.Time - Time) < KeyTolerance; })) return false;
	if (T->Keys.IsEmpty()) Tracks.RemoveAll([Id](const FOKSceneTrack& X) { return X.ObjectId == Id; });
	NotifyChanged();
	return true;
}

namespace
{
	/** A key's face, hands and clothes onto a character (the mouth opening given separately, it blends). */
	void ApplyCharacterKey(FOKSceneCharacter& C, const FOKSceneKey& K, float MouthOpen)
	{
		if (!K.bCharacterState) return;
		C.EyebrowPattern = K.EyebrowPattern; C.EyesPattern = K.EyesPattern; C.MouthPattern = K.MouthPattern; C.MouthOpen = MouthOpen;
		C.HandLeft = K.HandLeft; C.HandRight = K.HandRight;
		// Clothes only change when they differ (a change rebuilds the character).
		if (K.ClothesStates.Num() == C.ClothesStates.Num() && K.ClothesStates != C.ClothesStates) C.ClothesStates = K.ClothesStates;
	}
}

void UOKSceneDocument::EvaluateTimeline(float Time)
{
	bool bAny = false;
	for (const FOKSceneTrack& T : Tracks)
	{
		FOKSceneObject* O = Find(T.ObjectId);
		if (!O || T.Keys.IsEmpty()) continue;
		int32 Next = T.Keys.IndexOfByPredicate([Time](const FOKSceneKey& K) { return K.Time > Time; });
		FTransform X;
		float V;
		const bool bCharacter = O->Kind == EOKSceneObjectKind::Character;
		if (Next == INDEX_NONE) { const FOKSceneKey& K = T.Keys.Last(); X = K.Transform; V = K.Value; if (bCharacter) { O->Character.Fk = K.Fk; ApplyCharacterKey(O->Character, K, K.MouthOpen); } }
		else if (Next == 0) { const FOKSceneKey& K = T.Keys[0]; X = K.Transform; V = K.Value; if (bCharacter) { O->Character.Fk = K.Fk; ApplyCharacterKey(O->Character, K, K.MouthOpen); } }
		else
		{
			const FOKSceneKey& A = T.Keys[Next - 1];
			const FOKSceneKey& B = T.Keys[Next];
			const float U = EaseAlpha(A.Ease, (Time - A.Time) / FMath::Max(B.Time - A.Time, 1e-4f));
			if (T.bSmoothPath)
			{
				// Catmull-Rom through the keys (the ends repeat their key), so the path has no corners at keys.
				const FVector P0 = T.Keys[FMath::Max(Next - 2, 0)].Transform.GetLocation(), P1 = A.Transform.GetLocation(), P2 = B.Transform.GetLocation();
				const FVector P3 = T.Keys[FMath::Min(Next + 1, T.Keys.Num() - 1)].Transform.GetLocation();
				const float U2 = U * U, U3 = U2 * U;
				X.SetLocation(0.5f * (2.f * P1 + (P2 - P0) * U + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * U2 + (3.f * P1 - P0 - 3.f * P2 + P3) * U3));
			}
			else X.SetLocation(FMath::Lerp(A.Transform.GetLocation(), B.Transform.GetLocation(), U));
			X.SetRotation(FQuat::Slerp(A.Transform.GetRotation(), B.Transform.GetRotation(), U));
			X.SetScale3D(FMath::Lerp(A.Transform.GetScale3D(), B.Transform.GetScale3D(), U));
			V = FMath::Lerp(A.Value, B.Value, U);
			if (bCharacter)
			{
				// Each FK bone turns from its rotation at A to its rotation at B; a bone keyed on one side only holds.
				TArray<FOKSceneFkBone> Fk = A.Fk;
				for (FOKSceneFkBone& Bone : Fk)
				{
					const FOKSceneFkBone* To = B.Fk.FindByPredicate([&](const FOKSceneFkBone& X2) { return X2.Bone == Bone.Bone; });
					if (!To) continue;
					const float Ea[3] = { float(Bone.Euler.X), float(Bone.Euler.Y), float(Bone.Euler.Z) };
					const float Eb[3] = { float(To->Euler.X), float(To->Euler.Y), float(To->Euler.Z) };
					float E[3] = {};
					if (ok_unity_euler_slerp(Ea, Eb, U, E) == 0) Bone.Euler = FVector(E[0], E[1], E[2]);
				}
				for (const FOKSceneFkBone& Bone : B.Fk) if (!Fk.ContainsByPredicate([&](const FOKSceneFkBone& X2) { return X2.Bone == Bone.Bone; })) Fk.Add(Bone);
				O->Character.Fk = MoveTemp(Fk);
				// Patterns hold the earlier key's; the mouth opening blends when both keys set it.
				const float Mouth = A.MouthOpen >= 0.f && B.MouthOpen >= 0.f ? FMath::Lerp(A.MouthOpen, B.MouthOpen, U) : A.MouthOpen;
				ApplyCharacterKey(O->Character, A, Mouth);
			}
		}
		O->Transform = X;
		SetKeyValue(*O, V);
		bAny = true;
	}
	if (bAny) NotifyChanged();
}

TMap<FName, FLinearColor> FOKScenePost::ShaderGlobals() const
{
	TMap<FName, FLinearColor> Out;
	if (AmbientShadowG >= 0.f) Out.Add(TEXT("_ambientshadowG"), FLinearColor(AmbientShadow.R, AmbientShadow.G, AmbientShadow.B, 1.f - FMath::Clamp(AmbientShadowG, 0.f, 1.f)));
	if (LineColorG >= 0.f) Out.Add(TEXT("_LineColorG"), FLinearColor(0.5f, 0.5f, 0.5f, 1.f - FMath::Clamp(LineColorG, 0.f, 1.f)));
	if (LineWidthG >= 0.f) Out.Add(TEXT("_linewidthG"), FLinearColor(LineWidthG, 0.f, 0.f, 0.f));
	return Out;
}

void FOKScenePost::ApplyTo(FPostProcessSettings& S) const
{
	// Studio's shaders write their colour straight to the display (as the card reader shows it): no film curve or
	// gamut expansion, a fixed exposure (the bias below is the only change). The screen filter LUT then sees the
	// colours the game's Amplify Color sees.
	S.bOverride_ToneCurveAmount = true;
	S.ToneCurveAmount = 0.f;
	S.bOverride_ExpandGamut = true;
	S.ExpandGamut = 0.f;
	S.bOverride_BlueCorrection = true;
	S.BlueCorrection = 0.f;
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = AEM_Manual;
	S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	S.AutoExposureApplyPhysicalCameraExposure = false;
	S.bOverride_AutoExposureMinBrightness = true;
	S.AutoExposureMinBrightness = 1.f;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMaxBrightness = 1.f;
	// Legacy Gaussian fallback / diagnostic mode only. The installed HDR source does not use a luminance gate
	// or 1/(1-threshold) normalization. OKStudioApplySourceBloom below replaces this approximation with the
	// quarter-resolution, per-channel, nine-tap source chain and disables UE bloom once its parents are ready.
	const float Threshold = BloomThreshold >= 0.f ? FMath::Clamp(BloomThreshold, 0.f, 0.95f) : 0.f;
	const float Spread = 4.f * FMath::Clamp(BloomSize, 0.1f, 8.f);
	const float SigmaOfWidth = 0.00498f * Spread * 9.f / 16.f;
	S.bOverride_BloomMethod = true;
	S.BloomMethod = BM_SOG;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = bBloom ? BloomIntensity * 2.f / (1.f - Threshold) : 0.f;
	S.bOverride_BloomThreshold = true;
	S.BloomThreshold = BloomThreshold >= 0.f ? Threshold : -1.f;
	S.bOverride_BloomSizeScale = true;
	S.BloomSizeScale = 1.f;
	S.bOverride_BloomGaussianIntensity = true;
	S.BloomGaussianIntensity = 1.f;
	S.bOverride_Bloom1Tint = S.bOverride_Bloom2Tint = S.bOverride_Bloom3Tint = S.bOverride_Bloom4Tint = S.bOverride_Bloom5Tint = S.bOverride_Bloom6Tint = true;
	S.Bloom1Tint = S.Bloom2Tint = S.Bloom4Tint = S.Bloom5Tint = S.Bloom6Tint = FLinearColor::Black;
	S.Bloom3Tint = FLinearColor(6.f, 6.f, 6.f, 1.f);
	S.bOverride_Bloom3Size = true;
	S.Bloom3Size = SigmaOfWidth * 5.78f * 2.f * 100.f;
	// Always written: a focal distance of 0 turns depth of field off (an unset override let the previous scene's stay).
	S.bOverride_DepthOfFieldFocalDistance = true;
	S.bOverride_DepthOfFieldFstop = true;
	S.DepthOfFieldFocalDistance = bDepthOfField ? FMath::Max(FocalDistance, 1.f) : 0.f;
	S.DepthOfFieldFstop = FMath::Clamp(Aperture, 0.7f, 32.f);
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = bVignette ? VignetteIntensity : 0.f;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = Exposure;
	UTexture* Lut = ColorFilter >= 0 ? FOKStudioCatalog::Get().FilterLut(ColorFilter) : nullptr;
	S.bOverride_ColorGradingLUT = true;
	S.ColorGradingLUT = Lut;
	S.bOverride_ColorGradingIntensity = true;
	S.ColorGradingIntensity = Lut ? FMath::Clamp(ColorFilterIntensity, 0.f, 1.f) : 0.f;
	// Fog, ambient occlusion and sun shafts: Studio's own formulas in one post-process material.
	OKStudioApplyScenePostMaterial(*this, S);
	if (OKStudioApplySourceBloom(*this, S)) S.BloomIntensity = 0.f;
}
