#include "OKSceneDocument.h"
#include "Engine/Scene.h"
#include "OKStudioCatalog.h"
#include "OKStudioPost.h"
#include "OKStudioBloom.h"
#include "ok_extract.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
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
	// World scale is the object's own (WorldTransform): moving it under another parent keeps its size.
	Object->Transform.SetScale3D(World.GetScale3D());
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
	if (FindScopes > 0) return const_cast<FOKSceneObject*>(FindIndexed(Id));
	return Objects.FindByPredicate([Id](const FOKSceneObject& O) { return O.Id == Id; });
}

const FOKSceneObject* UOKSceneDocument::Find(int32 Id) const
{
	if (FindScopes > 0) return FindIndexed(Id);
	return Objects.FindByPredicate([Id](const FOKSceneObject& O) { return O.Id == Id; });
}

const FOKSceneObject* UOKSceneDocument::FindIndexed(int32 Id) const
{
	// The index is built when a scope starts and belongs to that array: a resized or moved array (an object added or
	// removed inside the scope) or an entry whose object no longer carries its id is indexed again. An id absent from a
	// fresh index is absent (a root's parent, -1, is looked up for every object).
	auto Rebuild = [this]
	{
		FindIndex.Reset();
		FindIndex.Reserve(Objects.Num());
		for (int32 I = 0; I < Objects.Num(); ++I) FindIndex.Add(Objects[I].Id, I);
		FindIndexData = Objects.GetData();
		FindIndexNum = Objects.Num();
	};
	if (FindIndexData != Objects.GetData() || FindIndexNum != Objects.Num()) Rebuild();
	const int32* At = FindIndex.Find(Id);
	if (At && (!Objects.IsValidIndex(*At) || Objects[*At].Id != Id)) { Rebuild(); At = FindIndex.Find(Id); }
	return At && Objects.IsValidIndex(*At) && Objects[*At].Id == Id ? &Objects[*At] : nullptr;
}

UOKSceneDocument::FFindScope::FFindScope(const UOKSceneDocument* InDocument) : Document(InDocument)
{
	// OK_STUDIO_FIND_INDEX=0 (diagnostic): scan as before, to measure what the index saves.
	static const bool bOff = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_FIND_INDEX")) == TEXT("0");
	if (!Document || bOff) { Document = nullptr; return; }
	if (Document->FindScopes++ == 0) Document->FindIndexNum = -1;
}

UOKSceneDocument::FFindScope::~FFindScope()
{
	if (Document) --Document->FindScopes;
}

TArray<int32> UOKSceneDocument::Children(int32 Parent) const
{
	TArray<int32> Out;
	for (const FOKSceneObject& O : Objects) if (O.Parent == Parent) Out.Add(O.Id);
	return Out;
}

FTransform UOKSceneDocument::WorldTransform(int32 Id) const
{
	// The chain from the scene root down (bounded: a corrupt file with a parent cycle must not hang).
	TArray<const FOKSceneObject*, TInlineAllocator<16>> Chain;
	for (int32 Steps = 0, Cur = Id; Cur >= 0 && Steps < UOKSceneDocument::MaxDepth; ++Steps)
	{
		const FOKSceneObject* O = Find(Cur);
		if (!O) break;
		Chain.Add(O);
		Cur = O->Parent;
	}
	FTransform World = FTransform::Identity;
	for (int32 K = Chain.Num() - 1; K >= 0; --K)
	{
		const FOKSceneObject* O = Chain[K];
		const FOKSceneObject* P = Chain.IsValidIndex(K + 1) ? Chain[K + 1] : nullptr;
		FTransform Frame = World;
		if (P && P->Kind == EOKSceneObjectKind::Item) Frame = FOKStudioCatalog::Get().AnimatedItemChildPoint(P->Item.Group, P->Item.Category, P->Item.No, P->Item.AnimePattern, P->Item.AnimeTime, ItemAnimationSeconds, P->Item.AnimeSpeed) * Frame;
		FTransform Bone;
		if (P && P->Kind == EOKSceneObjectKind::Character && !O->AttachBone.IsEmpty() && BoneFrame && BoneFrame(P->Id, O->AttachBone, Bone)) Frame = Bone * Frame;
		World = O->Transform * Frame;
		// Studio's GuideObject keeps every object's world scale at its own (localScale = localScale / lossyScale *
		// changeAmount.scale each frame): a parent's scale moves its children (their positions scale with it) but does
		// not size them. A child under a 57x stretched cylinder keeps its own size.
		World.SetScale3D(O->Transform.GetScale3D());
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
	for (int32 Steps = 0, Cur = Id; Cur >= 0 && Steps < UOKSceneDocument::MaxDepth; ++Steps)
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
	if (!SoundFile.IsEmpty()) Root->SetStringField(TEXT("sound_file"), SoundFile);
	if (!FMath::IsNearlyZero(SoundDelay)) Root->SetNumberField(TEXT("sound_delay"), SoundDelay);
	if (TSharedPtr<FJsonObject> PostJson = FJsonObjectConverter::UStructToJsonObject(Post)) Root->SetObjectField(TEXT("post"), PostJson);
	TArray<TSharedPtr<FJsonValue>> TrackJson;
	for (const FOKSceneTrack& T : Tracks) if (TSharedPtr<FJsonObject> J = FJsonObjectConverter::UStructToJsonObject(T)) TrackJson.Add(MakeShared<FJsonValueObject>(J));
	Root->SetArrayField(TEXT("tracks"), TrackJson);
	if (Bookmarks.ContainsByPredicate([](const FOKSceneBookmark& B) { return B.bSet; }))
	{
		TArray<TSharedPtr<FJsonValue>> MarkJson;
		for (const FOKSceneBookmark& B : Bookmarks) if (TSharedPtr<FJsonObject> J = FJsonObjectConverter::UStructToJsonObject(B)) MarkJson.Add(MakeShared<FJsonValueObject>(J));
		Root->SetArrayField(TEXT("bookmarks"), MarkJson);
	}
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
	SoundFile = Root->HasField(TEXT("sound_file")) ? Root->GetStringField(TEXT("sound_file")) : FString();
	SoundDelay = Root->HasField(TEXT("sound_delay")) ? float(Root->GetNumberField(TEXT("sound_delay"))) : 0.f;
	Post = FOKScenePost();
	const TSharedPtr<FJsonObject>* PostJson = nullptr;
	if (Root->TryGetObjectField(TEXT("post"), PostJson)) FJsonObjectConverter::JsonObjectToUStruct(PostJson->ToSharedRef(), &Post);
	Tracks.Reset();
	const TArray<TSharedPtr<FJsonValue>>* TrackJson = nullptr;
	if (Root->TryGetArrayField(TEXT("tracks"), TrackJson))
		for (const TSharedPtr<FJsonValue>& V : *TrackJson)
		{
			FOKSceneTrack T;
			if (V->AsObject() && FJsonObjectConverter::JsonObjectToUStruct(V->AsObject().ToSharedRef(), &T) && (Ids.Contains(T.ObjectId) || T.ObjectId == SceneTrackId)) Tracks.Add(MoveTemp(T));
		}
	Bookmarks.Reset();
	const TArray<TSharedPtr<FJsonValue>>* MarkJson = nullptr;
	if (Root->TryGetArrayField(TEXT("bookmarks"), MarkJson))
		for (const TSharedPtr<FJsonValue>& V : *MarkJson)
		{
			FOKSceneBookmark B;
			if (V->AsObject()) FJsonObjectConverter::JsonObjectToUStruct(V->AsObject().ToSharedRef(), &B);
			if (Bookmarks.Num() < 10) Bookmarks.Add(B);
		}
	NextId = FMath::Max(MaxId + 1, Root->HasField(TEXT("next_id")) ? int32(Root->GetNumberField(TEXT("next_id"))) : 1);
	NotifyChanged();
	return true;
}

bool UOKSceneDocument::ReplaceFile(const FString& Temp, const FString& Path, FString& OutError)
{
	if (IFileManager::Get().Move(*Path, *Temp, true, true, false, true)) return true;
	IFileManager::Get().Delete(*Temp, false, true, true);
	OutError = TEXT("cannot replace ") + Path;
	return false;
}

bool UOKSceneDocument::SaveToFile(const FString& Path, FString& OutError) const
{
	// The old file stays whole until the new one is written in full.
	const FString Temp = Path + TEXT(".saving");
	if (!FFileHelper::SaveStringToFile(ToJson(), *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { IFileManager::Get().Delete(*Temp, false, true, true); OutError = TEXT("cannot write ") + Path; return false; }
	return ReplaceFile(Temp, Path, OutError);
}

namespace
{
	/** Objects of a fragment or scene added to a document under new ids (parents inside the set follow; the set's top
	 *  objects go under Parent). Tracks of the objects come along. */
	TArray<int32> AddObjectsRemapped(UOKSceneDocument& Doc, TArray<FOKSceneObject>&& In, const TArray<FOKSceneTrack>& InTracks, int32 Parent)
	{
		TMap<int32, int32> IdMap;
		for (FOKSceneObject& O : In) IdMap.Add(O.Id, Doc.NextId++);
		TArray<int32> Tops;
		for (FOKSceneObject& O : In)
		{
			const bool bTop = !IdMap.Contains(O.Parent);
			O.Id = IdMap[O.Id];
			O.Parent = bTop ? (Doc.Find(Parent) ? Parent : -1) : IdMap[O.Parent];
			if (bTop) { Tops.Add(O.Id); if (O.Parent < 0 || (Doc.Find(O.Parent) && Doc.Find(O.Parent)->Kind != EOKSceneObjectKind::Character)) O.AttachBone.Reset(); }
			Doc.Objects.Add(MoveTemp(O));
		}
		for (const FOKSceneTrack& T : InTracks)
			if (const int32* To = IdMap.Find(T.ObjectId)) { FOKSceneTrack Copy = T; Copy.ObjectId = *To; Doc.Tracks.Add(MoveTemp(Copy)); }
		return Tops;
	}

	bool ParseObjects(const TSharedPtr<FJsonObject>& Root, TArray<FOKSceneObject>& Out, TArray<FOKSceneTrack>& OutTracks, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!Root || !Root->TryGetArrayField(TEXT("objects"), Items)) { OutError = TEXT("no objects"); return false; }
		TSet<int32> Ids;
		for (const TSharedPtr<FJsonValue>& V : *Items)
		{
			FOKSceneObject O;
			if (!V->AsObject() || !FJsonObjectConverter::JsonObjectToUStruct(V->AsObject().ToSharedRef(), &O) || Ids.Contains(O.Id)) { OutError = TEXT("bad scene object"); return false; }
			Ids.Add(O.Id);
			Out.Add(MoveTemp(O));
		}
		const TArray<TSharedPtr<FJsonValue>>* TrackJson = nullptr;
		if (Root->TryGetArrayField(TEXT("tracks"), TrackJson))
			for (const TSharedPtr<FJsonValue>& V : *TrackJson)
			{
				FOKSceneTrack T;
				if (V->AsObject() && FJsonObjectConverter::JsonObjectToUStruct(V->AsObject().ToSharedRef(), &T) && Ids.Contains(T.ObjectId)) OutTracks.Add(MoveTemp(T));
			}
		return true;
	}
}

FString UOKSceneDocument::ExportFragment(const TArray<int32>& Ids) const
{
	// Every chosen object with its subtree, once, in document order (parents before children as the document has them).
	TSet<int32> Wanted;
	TArray<int32> Queue = Ids;
	for (int32 I = 0; I < Queue.Num(); ++I) if (Find(Queue[I]) && !Wanted.Contains(Queue[I])) { Wanted.Add(Queue[I]); Queue.Append(Children(Queue[I])); }
	TArray<TSharedPtr<FJsonValue>> Items, TrackJson;
	for (const FOKSceneObject& O : Objects)
		if (Wanted.Contains(O.Id))
			if (TSharedPtr<FJsonObject> J = FJsonObjectConverter::UStructToJsonObject(O)) Items.Add(MakeShared<FJsonValueObject>(J));
	for (const FOKSceneTrack& T : Tracks)
		if (Wanted.Contains(T.ObjectId))
			if (TSharedPtr<FJsonObject> J = FJsonObjectConverter::UStructToJsonObject(T)) TrackJson.Add(MakeShared<FJsonValueObject>(J));
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("format"), TEXT("okscene-fragment"));
	Root->SetNumberField(TEXT("version"), FormatVersion);
	Root->SetArrayField(TEXT("objects"), Items);
	Root->SetArrayField(TEXT("tracks"), TrackJson);
	FString Out;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Out));
	return Out;
}

TArray<int32> UOKSceneDocument::PasteFragment(const FString& Json, int32 Parent, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root || Root->GetStringField(TEXT("format")) != TEXT("okscene-fragment")) { OutError = TEXT("not an OK Studio clipboard fragment"); return {}; }
	TArray<FOKSceneObject> In;
	TArray<FOKSceneTrack> InTracks;
	if (!ParseObjects(Root, In, InTracks, OutError)) return {};
	TArray<int32> Tops = AddObjectsRemapped(*this, MoveTemp(In), InTracks, Parent);
	NotifyChanged();
	return Tops;
}

TArray<int32> UOKSceneDocument::Merge(const FString& SceneJson, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SceneJson), Root) || !Root || Root->GetStringField(TEXT("format")) != TEXT("okscene")) { OutError = TEXT("not an .okscene"); return {}; }
	TArray<FOKSceneObject> In;
	TArray<FOKSceneTrack> InTracks;
	if (!ParseObjects(Root, In, InTracks, OutError)) return {};
	TSet<int32> Ids;
	for (const FOKSceneObject& O : In) Ids.Add(O.Id);
	for (FOKSceneObject& O : In) if (!Ids.Contains(O.Parent)) O.Parent = -1;
	TArray<int32> Tops = AddObjectsRemapped(*this, MoveTemp(In), InTracks, -1);
	if (Root->HasField(TEXT("duration"))) Duration = FMath::Max(Duration, float(Root->GetNumberField(TEXT("duration"))));
	NotifyChanged();
	return Tops;
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

int32 UOKSceneDocument::MoveKey(int32 Id, int32 Index, float NewTime)
{
	FOKSceneTrack* T = Tracks.FindByPredicate([Id](const FOKSceneTrack& X) { return X.ObjectId == Id; });
	if (!T || !T->Keys.IsValidIndex(Index)) return INDEX_NONE;
	FOKSceneKey K = T->Keys[Index];
	T->Keys.RemoveAt(Index);
	K.Time = FMath::Max(NewTime, 0.f);
	T->Keys.RemoveAll([&K](const FOKSceneKey& X) { return FMath::Abs(X.Time - K.Time) < KeyTolerance; });
	T->Keys.Add(K);
	T->Keys.Sort([](const FOKSceneKey& A, const FOKSceneKey& B) { return A.Time < B.Time; });
	Duration = FMath::Max(Duration, K.Time);
	NotifyChanged();
	return T->Keys.IndexOfByPredicate([&K](const FOKSceneKey& X) { return X.Time == K.Time; });
}

void UOKSceneDocument::SetKey(int32 Id, float Time)
{
	if (Id == SceneTrackId)
	{
		FOKSceneTrack* T = Tracks.FindByPredicate([](const FOKSceneTrack& X) { return X.ObjectId == SceneTrackId; });
		if (!T) { T = &Tracks.AddDefaulted_GetRef(); T->ObjectId = SceneTrackId; }
		const FOKSceneKey* Old = T->Keys.FindByPredicate([Time](const FOKSceneKey& K) { return FMath::Abs(K.Time - Time) < KeyTolerance; });
		FOKSceneKey K;
		K.Ease = Old ? Old->Ease : EOKSceneEase::Linear;
		T->Keys.RemoveAll([Time](const FOKSceneKey& X) { return FMath::Abs(X.Time - Time) < KeyTolerance; });
		K.Time = FMath::Max(Time, 0.f);
		K.Exposure = Post.Exposure;
		K.FocalDistance = Post.FocalDistance;
		T->Keys.Add(K);
		T->Keys.Sort([](const FOKSceneKey& A, const FOKSceneKey& B) { return A.Time < B.Time; });
		Duration = FMath::Max(Duration, K.Time);
		NotifyChanged();
		return;
	}
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
		// A stopped animation keys its frame (the timeline then scrubs it); a playing one runs on its own.
		K.AnimTime = C.Speed == 0.f ? C.Time : -1.f;
	}
	if (O->Kind == EOKSceneObjectKind::Light) K.Color = O->Light.Color;
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
	// The keys around Time and the eased fraction between them (B null: hold A).
	auto Around = [Time](const FOKSceneTrack& T, const FOKSceneKey*& A, const FOKSceneKey*& B, float& U)
	{
		const int32 Next = T.Keys.IndexOfByPredicate([Time](const FOKSceneKey& K) { return K.Time > Time; });
		if (Next == INDEX_NONE) { A = &T.Keys.Last(); B = nullptr; U = 0.f; return; }
		if (Next == 0) { A = &T.Keys[0]; B = nullptr; U = 0.f; return; }
		A = &T.Keys[Next - 1]; B = &T.Keys[Next];
		U = EaseAlpha(A->Ease, (Time - A->Time) / FMath::Max(B->Time - A->Time, 1e-4f));
	};
	for (const FOKSceneTrack& T : Tracks)
	{
		if (T.ObjectId != SceneTrackId || T.Keys.IsEmpty()) continue;
		const FOKSceneKey* A = nullptr; const FOKSceneKey* B = nullptr; float U = 0.f;
		Around(T, A, B, U);
		Post.Exposure = B ? FMath::Lerp(A->Exposure, B->Exposure, U) : A->Exposure;
		Post.FocalDistance = B ? FMath::Lerp(A->FocalDistance, B->FocalDistance, U) : A->FocalDistance;
		bAny = true;
	}
	for (const FOKSceneTrack& T : Tracks)
	{
		FOKSceneObject* O = Find(T.ObjectId);
		if (!O || T.Keys.IsEmpty()) continue;
		{
			// Light colour and a character's scrubbed animation time (keys that set them).
			const FOKSceneKey* A = nullptr; const FOKSceneKey* B = nullptr; float U = 0.f;
			Around(T, A, B, U);
			if (O->Kind == EOKSceneObjectKind::Light && A->Color.A >= 0.f) O->Light.Color = B && B->Color.A >= 0.f ? FMath::Lerp(A->Color, B->Color, U) : A->Color;
			if (O->Kind == EOKSceneObjectKind::Character && A->AnimTime >= 0.f)
			{
				O->Character.Speed = 0.f;
				O->Character.Time = B && B->AnimTime >= 0.f ? FMath::Lerp(A->AnimTime, B->AnimTime, U) : A->AnimTime;
			}
		}
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
	if (ShaderType >= 0) Out.Add(TEXT("_KanoVerShift"), FLinearColor(ShaderType == 0 ? 0.f : 1.f, 0.f, 0.f, 0.f));
	return Out;
}

void FOKScenePost::ApplyTo(FPostProcessSettings& S) const
{
	// Studio's shaders write their colour straight to the display (as the card reader shows it): no film curve or
	// gamut expansion, a fixed exposure (the bias below is the only change). The screen filter LUT then sees the
	// colours the game's Amplify Color sees.
	S.bOverride_ToneCurveAmount = true;
	S.ToneCurveAmount = 0.f;
	S.bOverride_MotionBlurAmount = true;
	S.MotionBlurAmount = 0.f;
	S.bOverride_MotionBlurMax = true;
	S.MotionBlurMax = 0.f;
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
	S.DepthOfFieldFstop = FMath::Clamp(Aperture, 0.3f, 32.f);
	// The lens opens as wide as the f-stop asks (UE's default limit, f/1.2, clamped wide Studio views' blur away).
	S.bOverride_DepthOfFieldMinFstop = true;
	S.DepthOfFieldMinFstop = 0.3f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = bVignette ? VignetteIntensity : 0.f;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = Exposure;
	UTexture* Lut = ColorFilter >= 0 ? FOKStudioCatalog::Get().FilterLut(ColorFilter) : nullptr;
	S.bOverride_ColorGradingLUT = true;
	const float FilterStrength = Lut ? FMath::Clamp(ColorFilterIntensity, 0.f, 1.f) : 0.f;
	S.ColorGradingLUT = FilterStrength > 0.f ? Lut : OKStudioIdentityGradeLut();
	S.bOverride_ColorGradingIntensity = true;
	S.ColorGradingIntensity = FilterStrength > 0.f ? FilterStrength : 1.f;
	if (OKStudioApplySourceGrade(*this, S))
	{
		// Exposure is applied to linear light inside the source grading pass.
		S.AutoExposureBias = 0.f;
		// A null/zero LUT does not clear a volume's earlier LUT in SceneView:
		// IF_PP rejects null and LerpTo(...,0) leaves the prior blend intact.
		S.ColorGradingLUT = OKStudioIdentityGradeLut();
		S.ColorGradingIntensity = 1.f;
		// The vignette is Studio's own formula inside the grading pass.
		S.VignetteIntensity = 0.f;
	}
	// Fog, ambient occlusion and sun shafts: Studio's own formulas in one post-process material.
	OKStudioApplyScenePostMaterial(*this, S);
	if (OKStudioApplySourceBloom(*this, S)) S.BloomIntensity = 0.f;
}
