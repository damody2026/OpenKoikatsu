// OK Studio control channel (-Control): POST http://127.0.0.1:<StudioControlPort, 47890>/studio/<op> with a JSON body;
// the reply is {"ok": true, ...} or {"ok": false, "error": ...}. Tools/ok-game-mcp's studio_* tools speak it
// (Docs/OK_STUDIO_MCP.md). Edits wait while the user drags, a scene loads, the timeline plays or exports ("busy").
#include "SOKStudioPanel.h"
#include "OKStudioGameInternal.h"
#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "OKStudioSky.h"
#include "OKStudioPost.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "KKPreviewActor.h"
#include "Materials/Material.h"
#include "Engine/Texture.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Input/SEditableText.h"

namespace
{
	TSharedPtr<IHttpRouter> GRouter;
	TArray<FHttpRouteHandle> GRoutes;

	TSharedRef<FJsonObject> Result(bool bOk, const FString& Error = FString())
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), bOk);
		if (!Error.IsEmpty()) O->SetStringField(TEXT("error"), Error);
		return O;
	}

	void Reply(const FHttpResultCallback& Done, const TSharedRef<FJsonObject>& Body)
	{
		FString Text;
		FJsonSerializer::Serialize(Body, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		Done(FHttpServerResponse::Create(Text, TEXT("application/json; charset=utf-8")));
	}

	TSharedRef<FJsonObject> Body(const FHttpServerRequest& Req)
	{
		const FUTF8ToTCHAR Utf8(reinterpret_cast<const ANSICHAR*>(Req.Body.GetData()), Req.Body.Num());
		const FString Text(Utf8.Length(), Utf8.Get());
		TSharedPtr<FJsonObject> Root;
		if (Text.IsEmpty() || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) Root = MakeShared<FJsonObject>();
		return Root.ToSharedRef();
	}

	TArray<TSharedPtr<FJsonValue>> Vec(const FVector& V) { return { MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) }; }
	bool ReadVec(const TSharedRef<FJsonObject>& J, const TCHAR* Name, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!J->TryGetArrayField(Name, A) || A->Num() != 3) return false;
		Out = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
		return true;
	}
	int32 ReadId(const TSharedRef<FJsonObject>& J, const TCHAR* Name, int32 Default = -1)
	{
		double D;
		if (J->TryGetNumberField(Name, D)) return int32(D);
		FString S;
		if (J->TryGetStringField(Name, S) && S.IsNumeric()) return FCString::Atoi(*S);
		return Default;
	}
	TArray<int32> ReadIds(const TSharedRef<FJsonObject>& J, const TCHAR* Name)
	{
		TArray<int32> Ids;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (J->TryGetArrayField(Name, A)) for (const TSharedPtr<FJsonValue>& V : *A) Ids.Add(V->Type == EJson::String ? FCString::Atoi(*V->AsString()) : int32(V->AsNumber()));
		return Ids;
	}

	const TCHAR* KindName(EOKSceneObjectKind K)
	{
		switch (K)
		{
		case EOKSceneObjectKind::Character: return TEXT("character");
		case EOKSceneObjectKind::Item: return TEXT("item");
		case EOKSceneObjectKind::Light: return TEXT("light");
		case EOKSceneObjectKind::Camera: return TEXT("camera");
		case EOKSceneObjectKind::Effect: return TEXT("effect");
		default: return TEXT("folder");
		}
	}

	TSharedRef<FJsonObject> StateOf(UOKStudioGame& G)
	{
		TSharedRef<FJsonObject> O = Result(true);
		O->SetStringField(TEXT("scene_file"), G.File);
		O->SetBoolField(TEXT("dirty"), G.bDirty);
		TArray<TSharedPtr<FJsonValue>> Sel;
		for (int32 Id : G.Selection) Sel.Add(MakeShared<FJsonValueNumber>(Id));
		O->SetArrayField(TEXT("selection"), Sel);
		O->SetNumberField(TEXT("primary"), G.SelectedId);
		O->SetBoolField(TEXT("grid"), G.bShowGrid);
		O->SetBoolField(TEXT("hide_helpers"), G.bHideHelpers);
		O->SetStringField(TEXT("background"), G.Document->Post.Background);
		O->SetStringField(TEXT("frame_picture"), G.Document->Post.Frame);
		// The sound menu: the scene's settings, the files they resolve to and whether each is audible now.
		{
			const FOKSceneSound& S = G.Document->Sound;
			TSharedRef<FJsonObject> Sound = MakeShared<FJsonObject>();
			Sound->SetNumberField(TEXT("bgm"), S.Bgm);
			Sound->SetBoolField(TEXT("bgm_play"), S.bBgmPlay);
			Sound->SetBoolField(TEXT("bgm_repeat"), S.bBgmRepeat);
			Sound->SetStringField(TEXT("bgm_file"), G.BgmFile());
			Sound->SetBoolField(TEXT("bgm_playing"), G.IsBgmPlaying());
			Sound->SetStringField(TEXT("outside"), S.Outside);
			Sound->SetBoolField(TEXT("outside_play"), S.bOutsidePlay);
			Sound->SetBoolField(TEXT("outside_repeat"), S.bOutsideRepeat);
			Sound->SetStringField(TEXT("outside_file"), G.OutsideSoundFile());
			Sound->SetBoolField(TEXT("outside_playing"), G.IsOutsideSoundPlaying());
			O->SetObjectField(TEXT("sound"), Sound);
		}
		O->SetObjectField(TEXT("free_view"), FJsonObjectConverter::UStructToJsonObject(G.Document->FreeView));
		// The scene post passes' shaders and how many blendables the view's camera draws (sun shafts only once ready).
		O->SetStringField(TEXT("post_opaque"), OKStudioScenePostMaterialState(0));
		O->SetStringField(TEXT("post_shafts"), OKStudioScenePostMaterialState(1));
		O->SetBoolField(TEXT("sun_shafts"), G.Document->Post.bSunShafts);
		if (G.Viewer) O->SetNumberField(TEXT("post_blendables"), G.Viewer->GetCameraComponent()->PostProcessSettings.WeightedBlendables.Array.Num());
		TArray<TSharedPtr<FJsonValue>> Objects;
		for (const FOKSceneObject& X : G.Document->Objects)
		{
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetNumberField(TEXT("id"), X.Id);
			J->SetNumberField(TEXT("parent"), X.Parent);
			J->SetStringField(TEXT("name"), X.Name);
			J->SetStringField(TEXT("kind"), KindName(X.Kind));
			J->SetBoolField(TEXT("visible"), X.bVisible);
			J->SetBoolField(TEXT("locked"), X.bLocked);
			if (X.Kind == EOKSceneObjectKind::Camera) { J->SetBoolField(TEXT("ortho"), X.Camera.bOrtho); J->SetNumberField(TEXT("ortho_width"), X.Camera.OrthoWidth); }
			if (X.Kind == EOKSceneObjectKind::Character) { J->SetNumberField(TEXT("speed"), X.Character.Speed); J->SetNumberField(TEXT("anime_no"), X.Character.StudioAnimeNo); J->SetBoolField(TEXT("force_loop"), X.Character.bForceLoop); }
			// The Studio controls the shown H motion blends along, with the values its player has (the controller's sliders).
			if (X.Kind == EOKSceneObjectKind::Character && G.SceneActor)
			{
				TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
				for (const TPair<FString, float>& P : G.SceneActor->BlendControlsOf(X.Id)) F->SetNumberField(P.Key, P.Value);
				J->SetObjectField(TEXT("anim_controls"), F);
				// The voice list: its length, settings and the entry being said now (-1: silent).
				J->SetNumberField(TEXT("voices"), X.Character.Voices.Num());
				J->SetBoolField(TEXT("voice_play"), X.Character.bVoicePlay);
				J->SetNumberField(TEXT("voice_repeat"), X.Character.VoiceRepeat);
				J->SetNumberField(TEXT("voice_playing"), G.VoicePlayingIndex(X.Id));
			}
			if (X.Kind == EOKSceneObjectKind::Light)
			{
				static const TCHAR* LightTypes[] = { TEXT("directional"), TEXT("point"), TEXT("spot"), TEXT("area") };
				J->SetStringField(TEXT("light_type"), LightTypes[FMath::Min(int32(X.Light.Type), 3)]);
			}
			J->SetStringField(TEXT("attach_bone"), X.AttachBone);
			J->SetArrayField(TEXT("location"), Vec(X.Transform.GetLocation()));
			const FRotator R = X.Transform.Rotator();
			J->SetArrayField(TEXT("rotation"), Vec(FVector(R.Pitch, R.Yaw, R.Roll)));
			J->SetArrayField(TEXT("scale"), Vec(X.Transform.GetScale3D()));
			if (const FString* E = G.SceneActor ? G.SceneActor->Errors.Find(X.Id) : nullptr) J->SetStringField(TEXT("error"), *E);
			Objects.Add(MakeShared<FJsonValueObject>(J));
		}
		O->SetArrayField(TEXT("objects"), Objects);
		TSharedPtr<FJsonObject> Ui;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(G.UiStateJson()), Ui);
		if (Ui) O->SetObjectField(TEXT("ui"), Ui);
		O->SetStringField(TEXT("busy"), G.BusyReason());
		TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
		T->SetNumberField(TEXT("time"), G.TimelineTime);
		T->SetNumberField(TEXT("duration"), G.Document->Duration);
		T->SetBoolField(TEXT("playing"), G.bTimelinePlaying);
		O->SetObjectField(TEXT("timeline"), T);
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetBoolField(TEXT("running"), G.IsExporting());
		E->SetNumberField(TEXT("frame"), G.ExportFrameIndex());
		E->SetNumberField(TEXT("frames"), G.ExportFrameCount());
		E->SetStringField(TEXT("folder"), G.ExportFolder());
		E->SetStringField(TEXT("last_movie"), G.LastMovie);
		E->SetStringField(TEXT("error"), G.LastMovieError);
		O->SetObjectField(TEXT("export"), E);
		return O;
	}

	/** Merges a JSON object into another: objects merge field by field, everything else (arrays too) replaces. */
	void DeepMerge(const TSharedPtr<FJsonObject>& Into, const TSharedPtr<FJsonObject>& From)
	{
		for (const auto& P : From->Values)
		{
			const FString Key(*P.Key);
			const TSharedPtr<FJsonObject>* Have = nullptr;
			if (P.Value.IsValid() && P.Value->Type == EJson::Object && Into->TryGetObjectField(Key, Have)) DeepMerge(*Have, P.Value->AsObject());
			else Into->SetField(Key, P.Value);
		}
	}

	TSharedRef<FJsonObject> TransformJson(const FTransform& T)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetArrayField(TEXT("location"), Vec(T.GetLocation()));
		const FRotator R = T.Rotator();
		O->SetArrayField(TEXT("rotation"), Vec(FVector(R.Pitch, R.Yaw, R.Roll)));
		O->SetArrayField(TEXT("scale"), Vec(T.GetScale3D()));
		return O;
	}

	TSharedRef<FJsonObject> SceneJson(UOKStudioGame& G)
	{
		UOKSceneDocument* D = G.Document;
		TSharedRef<FJsonObject> O = Result(true);
		if (TSharedPtr<FJsonObject> Post = FJsonObjectConverter::UStructToJsonObject(D->Post)) O->SetObjectField(TEXT("post"), Post);
		O->SetNumberField(TEXT("map"), D->Map);
		O->SetObjectField(TEXT("map_transform"), TransformJson(D->MapTransform));
		O->SetNumberField(TEXT("duration"), D->Duration);
		O->SetNumberField(TEXT("active_camera"), D->ActiveCamera);
		TArray<TSharedPtr<FJsonValue>> Marks;
		for (int32 I = 0; I < D->Bookmarks.Num(); ++I)
			if (D->Bookmarks[I].bSet)
			{
				TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
				B->SetNumberField(TEXT("slot"), I);
				B->SetArrayField(TEXT("focus"), Vec(D->Bookmarks[I].Focus));
				B->SetNumberField(TEXT("yaw"), D->Bookmarks[I].Yaw);
				B->SetNumberField(TEXT("pitch"), D->Bookmarks[I].Pitch);
				B->SetNumberField(TEXT("distance"), D->Bookmarks[I].Distance);
				B->SetNumberField(TEXT("camera"), D->Bookmarks[I].Camera);
				Marks.Add(MakeShared<FJsonValueObject>(B));
			}
		O->SetArrayField(TEXT("bookmarks"), Marks);
		return O;
	}

	TSharedRef<FJsonObject> KeyJson(const FOKSceneKey& K)
	{
		static const TCHAR* Eases[] = { TEXT("linear"), TEXT("ease_in_out"), TEXT("ease_in"), TEXT("ease_out"), TEXT("step") };
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("time"), K.Time);
		O->SetStringField(TEXT("ease"), Eases[FMath::Clamp(int32(K.Ease), 0, 4)]);
		O->SetObjectField(TEXT("transform"), TransformJson(K.Transform));
		O->SetNumberField(TEXT("value"), K.Value);
		O->SetNumberField(TEXT("fk_bones"), K.Fk.Num());
		O->SetBoolField(TEXT("character_state"), K.bCharacterState);
		if (K.AnimTime >= 0.f) O->SetNumberField(TEXT("anim_time"), K.AnimTime);
		if (K.Color.A >= 0.f) O->SetStringField(TEXT("color"), TEXT("#") + K.Color.ToFColor(false).ToHex());
		O->SetNumberField(TEXT("exposure"), K.Exposure);
		O->SetNumberField(TEXT("focal_distance"), K.FocalDistance);
		return O;
	}

	/** The ops an AI uses to build a scene in detail (patch, scene look, bones, bounds, catalogue, keys, ...). */
	TSharedPtr<FJsonObject> RunMore(UOKStudioGame& G, SOKStudioPanel& P, const FString& Op, const TSharedRef<FJsonObject>& J)
	{
		UOKSceneDocument* Doc = G.Document;
		if (Op == TEXT("patch"))
		{
			FOKSceneObject* X = Doc->Find(ReadId(J, TEXT("id")));
			const TSharedPtr<FJsonObject>* Fields = nullptr;
			if (!X) return Result(false, TEXT("no such object"));
			if (!J->TryGetObjectField(TEXT("fields"), Fields)) return Result(false, TEXT("fields (object) required"));
			TSharedPtr<FJsonObject> Now = FJsonObjectConverter::UStructToJsonObject(*X);
			TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>(**Fields);
			for (const TCHAR* Fixed : { TEXT("id"), TEXT("kind"), TEXT("parent") }) Change->RemoveField(Fixed);
			DeepMerge(Now, Change);
			FOKSceneObject Patched;
			if (!FJsonObjectConverter::JsonObjectToUStruct(Now.ToSharedRef(), &Patched)) return Result(false, TEXT("fields do not fit the object (see studio_inspect)"));
			Patched.Id = X->Id; Patched.Kind = X->Kind; Patched.Parent = X->Parent;
			*X = Patched;
			Doc->NotifyChanged();
			TSharedRef<FJsonObject> O = Result(true);
			if (TSharedPtr<FJsonObject> Obj = FJsonObjectConverter::UStructToJsonObject(*X)) O->SetObjectField(TEXT("object"), Obj);
			return O;
		}
		if (Op == TEXT("scene"))
		{
			const TSharedPtr<FJsonObject>* Post = nullptr;
			if (J->TryGetObjectField(TEXT("post"), Post))
			{
				TSharedPtr<FJsonObject> Now = FJsonObjectConverter::UStructToJsonObject(Doc->Post);
				DeepMerge(Now, *Post);
				FOKScenePost Patched;
				if (!FJsonObjectConverter::JsonObjectToUStruct(Now.ToSharedRef(), &Patched)) return Result(false, TEXT("post fields do not fit (see studio_scene)"));
				Doc->Post = Patched;
			}
			// The sound menu ({"sound": {"Bgm": 3, "bBgmPlay": true, ...}}, FOKSceneSound's fields).
			const TSharedPtr<FJsonObject>* SoundPatch = nullptr;
			if (J->TryGetObjectField(TEXT("sound"), SoundPatch))
			{
				TSharedPtr<FJsonObject> Now = FJsonObjectConverter::UStructToJsonObject(Doc->Sound);
				DeepMerge(Now, *SoundPatch);
				FOKSceneSound Patched;
				if (!FJsonObjectConverter::JsonObjectToUStruct(Now.ToSharedRef(), &Patched)) return Result(false, TEXT("sound fields do not fit"));
				Doc->Sound = Patched;
			}
			// The free view's lens ({"free_view": {"fov": 30, "bOrtho": true, ...}}, FOKSceneCamera's fields).
			const TSharedPtr<FJsonObject>* LensPatch = nullptr;
			if (J->TryGetObjectField(TEXT("free_view"), LensPatch))
			{
				TSharedPtr<FJsonObject> Now = FJsonObjectConverter::UStructToJsonObject(Doc->FreeView);
				DeepMerge(Now, *LensPatch);
				FOKSceneCamera Patched;
				if (!FJsonObjectConverter::JsonObjectToUStruct(Now.ToSharedRef(), &Patched)) return Result(false, TEXT("free_view fields do not fit"));
				Doc->FreeView = Patched;
			}
			double D;
			if (J->TryGetNumberField(TEXT("map"), D)) Doc->Map = int32(D);
			const TSharedPtr<FJsonObject>* MapT = nullptr;
			if (J->TryGetObjectField(TEXT("map_transform"), MapT))
			{
				FVector V = FVector::ZeroVector;
				const TSharedRef<FJsonObject> M = MapT->ToSharedRef();
				if (ReadVec(M, TEXT("location"), V)) Doc->MapTransform.SetLocation(V);
				if (ReadVec(M, TEXT("rotation"), V)) Doc->MapTransform.SetRotation(FRotator(V.X, V.Y, V.Z).Quaternion());
				if (ReadVec(M, TEXT("scale"), V)) Doc->MapTransform.SetScale3D(V);
			}
			if (J->TryGetNumberField(TEXT("duration"), D)) Doc->Duration = FMath::Max(float(D), 0.1f);
			const int32 Camera = ReadId(J, TEXT("active_camera"), -2);
			if (Camera != -2) Doc->ActiveCamera = Doc->Find(Camera) ? Camera : -1;
			if (J->Values.Num()) Doc->NotifyChanged();
			return SceneJson(G);
		}
		if (Op == TEXT("bones"))
		{
			const FOKSceneObject* X = Doc->Find(ReadId(J, TEXT("id")));
			AKKPreviewActor* A = X && G.SceneActor ? G.SceneActor->PreviewOf(X->Id) : nullptr;
			if (!X || X->Kind != EOKSceneObjectKind::Character) return Result(false, TEXT("not a character"));
			if (!A) return Result(false, TEXT("the character is not shown (studio_report says why)"));
			static const TCHAR* Parts[] = { TEXT("body"), TEXT("chest"), TEXT("hair"), TEXT("neck"), TEXT("left_hand"), TEXT("right_hand"), TEXT("skirt") };
			TArray<TSharedPtr<FJsonValue>> Fk, Ik, Points;
			for (const FOKStudioFkBone& B : FOKStudioCatalog::Get().FkBones)
			{
				const FOKSceneFkBone* Set = X->Character.Fk.FindByPredicate([&B](const FOKSceneFkBone& F) { return F.Bone == B.Bone; });
				FVector Euler = FVector::ZeroVector;
				if (Set) Euler = Set->Euler; else A->Session.BoneEuler(B.Bone, Euler);
				TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("bone"), B.Bone);
				O->SetStringField(TEXT("name"), B.Name);
				O->SetStringField(TEXT("part"), Parts[FMath::Clamp(SOKStudioPanel::FkPartOf(B.Bone), 0, 6)]);
				O->SetArrayField(TEXT("euler"), Vec(Euler));
				O->SetBoolField(TEXT("set"), Set != nullptr);
				FTransform W;
				if (OKStudioGameInternal::BoneWorld(A, B.Bone, W)) O->SetArrayField(TEXT("world"), Vec(W.GetLocation()));
				Fk.Add(MakeShared<FJsonValueObject>(O));
			}
			static const TCHAR* Effectors[13] = { TEXT("body"), TEXT("left_shoulder"), TEXT("right_shoulder"), TEXT("left_thigh"), TEXT("right_thigh"), TEXT("left_hand"), TEXT("right_hand"),
				TEXT("left_foot"), TEXT("right_foot"), TEXT("left_elbow"), TEXT("right_elbow"), TEXT("left_knee"), TEXT("right_knee") };
			FString Error;
			const bool bIk = A->bStudioIkReady || A->Session.PrepareStudioIk(Error);
			if (bIk) A->bStudioIkReady = true;
			for (int32 E = 0; E < 13; ++E)
			{
				const FOKSceneIkTarget* T = X->Character.Ik.FindByPredicate([E](const FOKSceneIkTarget& I) { return I.Effector == E; });
				FVector Local = FVector::ZeroVector, Unity;
				if (T) Local = T->Position; else if (bIk && A->Session.IkEffectorPosition(E, Unity)) Local = AKKPreviewActor::UnityToActorLocal(Unity);
				TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("effector"), E);
				O->SetStringField(TEXT("name"), Effectors[E]);
				O->SetBoolField(TEXT("enabled"), T != nullptr);
				O->SetNumberField(TEXT("weight"), T ? T->Weight : 1.f);
				O->SetArrayField(TEXT("position"), Vec(Local));
				O->SetArrayField(TEXT("world"), Vec(A->GetActorTransform().TransformPosition(Local)));
				Ik.Add(MakeShared<FJsonValueObject>(O));
			}
			for (const FOKStudioAccessoryPoint& Pt : FOKStudioCatalog::Get().AccessoryPoints)
			{
				TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("key"), Pt.Key);
				O->SetStringField(TEXT("name"), Pt.Name);
				O->SetNumberField(TEXT("group"), Pt.Group);
				FTransform W;
				if (OKStudioGameInternal::BoneWorld(A, Pt.Bone, W)) O->SetArrayField(TEXT("world"), Vec(W.GetLocation()));
				Points.Add(MakeShared<FJsonValueObject>(O));
			}
			TSharedRef<FJsonObject> O = Result(true);
			O->SetArrayField(TEXT("fk"), Fk);
			O->SetArrayField(TEXT("ik"), Ik);
			O->SetNumberField(TEXT("fk_parts_off"), X->Character.FkPartsOff);
			O->SetNumberField(TEXT("ik_parts_off"), X->Character.IkPartsOff);
			O->SetNumberField(TEXT("joint_correction"), X->Character.JointCorrection);
			O->SetArrayField(TEXT("look_point"), Vec(X->Character.LookPoint));
			// What the character shows now (the actor's own state, not the document's): checks for the face and look controls.
			O->SetNumberField(TEXT("face_eyes_open"), A->FaceOpen01[0]);
			O->SetNumberField(TEXT("face_eyes_pattern"), A->FacePattern[0]);
			O->SetNumberField(TEXT("eyes_look"), int32(X->Character.EyesLook));
			O->SetNumberField(TEXT("neck_look"), int32(X->Character.NeckLook));
			O->SetNumberField(TEXT("neck_follow"), X->Character.NeckFollow);
			if (const FOKSceneCharacter* C = &X->Character) { TArray<TSharedPtr<FJsonValue>> Cl; for (uint8 S : C->ClothesStates) Cl.Add(MakeShared<FJsonValueNumber>(S)); O->SetArrayField(TEXT("clothes_states"), Cl); }
			O->SetNumberField(TEXT("hand_left"), X->Character.HandLeft);
			O->SetNumberField(TEXT("hand_right"), X->Character.HandRight);
			O->SetArrayField(TEXT("accessory_points"), Points);
			O->SetStringField(TEXT("note"), TEXT("euler: Unity local degrees (as KK Studio FK); ik position: cm in the character's space (+Y ahead, +Z up); world: cm. A character with rotation [0,0,0] faces +Y (the front view looks at its face); yaw -90 faces +X, yaw 90 faces -X, yaw 180 faces away."));
			return O;
		}
		if (Op == TEXT("bounds"))
		{
			TArray<int32> Ids = ReadIds(J, TEXT("ids"));
			if (Ids.IsEmpty()) for (const FOKSceneObject& X : Doc->Objects) Ids.Add(X.Id);
			TArray<TSharedPtr<FJsonValue>> Out;
			for (int32 Id : Ids)
			{
				FBox B;
				if (!G.WorldBounds(Id, B)) continue;
				TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("id"), Id);
				O->SetArrayField(TEXT("min"), Vec(B.Min));
				O->SetArrayField(TEXT("max"), Vec(B.Max));
				O->SetArrayField(TEXT("center"), Vec(B.GetCenter()));
				O->SetArrayField(TEXT("size"), Vec(B.GetSize()));
				// Not built yet (still loading) or not shown: the box is only a stand-in around its place.
				const FOKSceneObject* X = Doc->Find(Id);
				const bool bShown = X && G.SceneActor && !G.SceneActor->Errors.Contains(Id)
					&& (X->Kind == EOKSceneObjectKind::Character ? G.SceneActor->PreviewOf(Id) != nullptr : X->Kind != EOKSceneObjectKind::Item || G.SceneActor->PreviewOf(Id) != nullptr);
				O->SetBoolField(TEXT("built"), bShown && !G.SceneActor->HasPendingBuilds());
				Out.Add(MakeShared<FJsonValueObject>(O));
			}
			TSharedRef<FJsonObject> O = Result(true);
			O->SetArrayField(TEXT("objects"), Out);
			return O;
		}
		if (Op == TEXT("catalog"))
		{
			const FOKStudioCatalog& C = FOKStudioCatalog::Get();
			FString Kind;
			J->TryGetStringField(TEXT("kind"), Kind);
			const int32 Group = ReadId(J, TEXT("group"), INDEX_NONE), Category = ReadId(J, TEXT("category"), INDEX_NONE), AnimKind = ReadId(J, TEXT("anim_kind"), 0);
			TArray<TSharedPtr<FJsonValue>> Groups, Categories, Entries;
			auto Named = [](int32 Id, const FString& Name) { TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(); O->SetNumberField(TEXT("id"), Id); O->SetStringField(TEXT("name"), Name); return MakeShared<FJsonValueObject>(O); };
			if (Kind == TEXT("item"))
			{
				for (const FOKStudioCatalogGroup& Gr : C.Groups) Groups.Add(Named(Gr.Id, Gr.Name));
				if (Group != INDEX_NONE) for (const FOKStudioCatalogGroup* Ca : C.CategoriesOf(Group)) Categories.Add(Named(Ca->Id, Ca->Name));
				if (Group != INDEX_NONE && Category != INDEX_NONE)
					for (const FOKStudioCatalogItem* I : C.ItemsIn(Group, Category))
					{
						TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
						O->SetStringField(TEXT("item"), FString::Printf(TEXT("%d/%d/%d"), I->Group, I->Category, I->No));
						O->SetStringField(TEXT("name"), I->Name);
						O->SetBoolField(TEXT("colors"), I->Colors[0] || I->Colors[1] || I->Colors[2]);
						O->SetBoolField(TEXT("patterns"), I->Patterns[0] || I->Patterns[1] || I->Patterns[2]);
						O->SetBoolField(TEXT("animated"), I->bAnime);
						Entries.Add(MakeShared<FJsonValueObject>(O));
					}
			}
			else if (Kind == TEXT("map"))
			{
				for (const FOKStudioCatalogGroup& Ca : C.MapCategories) Categories.Add(Named(Ca.Id, Ca.Name));
				for (const FOKStudioCatalogMap& M : C.Maps) if (Category == INDEX_NONE || M.Category == Category) { TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(); O->SetNumberField(TEXT("map"), M.No); O->SetStringField(TEXT("name"), M.Name); O->SetNumberField(TEXT("category"), M.Category); Entries.Add(MakeShared<FJsonValueObject>(O)); }
			}
			else if (Kind == TEXT("effect"))
			{
				for (const FString& Ca : C.EffectCategories) Categories.Add(MakeShared<FJsonValueString>(Ca));
				for (const FOKStudioEffect& E : C.Effects) { TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("effect"), E.Id); O->SetStringField(TEXT("name"), E.Name); O->SetStringField(TEXT("category"), E.Category); Entries.Add(MakeShared<FJsonValueObject>(O)); }
			}
			else if (Kind == TEXT("anim"))
			{
				TSet<int32> Used;
				for (const FOKStudioAnim& A : C.Anims) if (A.Kind == AnimKind && !A.File.IsEmpty()) Used.Add(A.Group);
				for (const FOKStudioCatalogGroup& Gr : C.AnimGroups) if (Used.Contains(Gr.Id)) Groups.Add(Named(Gr.Id, Gr.Name));
				if (Group != INDEX_NONE) for (const FOKStudioCatalogGroup& Ca : C.AnimCategories) if (Ca.Group == Group) Categories.Add(Named(Ca.Id, Ca.Name));
				if (Group != INDEX_NONE && Category != INDEX_NONE)
					for (const FOKStudioAnim& A : C.Anims)
						if (A.Kind == AnimKind && A.Group == Group && A.Category == Category && !A.File.IsEmpty())
						{
							TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
							O->SetStringField(TEXT("anim"), FString::Printf(TEXT("%d/%d/%d/%d"), A.Kind, A.Group, A.Category, A.No));
							O->SetStringField(TEXT("name"), A.Name);
							O->SetNumberField(TEXT("duration"), A.Duration);
							O->SetBoolField(TEXT("looping"), A.bLooping);
							if (!A.Variants.IsEmpty()) { TArray<TSharedPtr<FJsonValue>> Controls; for (const FString& Control : A.Controls) Controls.Add(MakeShared<FJsonValueString>(Control)); O->SetArrayField(TEXT("controls"), Controls); }
							Entries.Add(MakeShared<FJsonValueObject>(O));
						}
			}
			else if (Kind == TEXT("video"))
			{
				// Video motion pairs (pose video_pair) and the .okanim files a character can play (pose anim_file).
				for (const SOKStudioPanel::FVideoPair& V : SOKStudioPanel::VideoPairs()) { TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("video_pair"), V.Name); O->SetStringField(TEXT("female"), V.Female); O->SetStringField(TEXT("male"), V.Male); Entries.Add(MakeShared<FJsonValueObject>(O)); }
				for (const FString& F : SOKStudioPanel::AnimFiles()) { TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("anim_file"), F); Entries.Add(MakeShared<FJsonValueObject>(O)); }
			}
			else if (Kind == TEXT("filter")) { for (const FOKStudioFilter& F : C.Filters) Entries.Add(Named(F.No, F.Name)); }
			else if (Kind == TEXT("sky")) { for (const FOKStudioSkyPattern& S : OKStudioSkyPatterns()) Entries.Add(Named(S.No, S.Name)); }
			else if (Kind == TEXT("accessory_point"))
			{
				for (const FOKStudioAccessoryPoint& Pt : C.AccessoryPoints) { TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("key"), Pt.Key); O->SetStringField(TEXT("name"), Pt.Name); O->SetNumberField(TEXT("group"), Pt.Group); Entries.Add(MakeShared<FJsonValueObject>(O)); }
			}
			else return Result(false, TEXT("kind: item, map, effect, anim, video, filter, sky or accessory_point"));
			TSharedRef<FJsonObject> O = Result(true);
			if (Groups.Num()) O->SetArrayField(TEXT("groups"), Groups);
			if (Categories.Num()) O->SetArrayField(TEXT("categories"), Categories);
			O->SetArrayField(TEXT("entries"), Entries);
			return O;
		}
		if (Op == TEXT("keys"))
		{
			const int32 Id = ReadId(J, TEXT("id"), INDEX_NONE);
			if (Id != UOKSceneDocument::SceneTrackId && !Doc->Find(Id)) return Result(false, TEXT("no such object (-2: the scene's track)"));
			FString Action;
			J->TryGetStringField(TEXT("action"), Action);
			double Time = G.TimelineTime, To = 0.;
			J->TryGetNumberField(TEXT("time"), Time);
			if (Action == TEXT("set")) Doc->SetKey(Id, float(Time));
			else if (Action == TEXT("remove") && !Doc->RemoveKey(Id, float(Time))) return Result(false, TEXT("no key at that time"));
			else if (Action == TEXT("move"))
			{
				FOKSceneTrack* T = Doc->TrackOf(Id);
				const int32 Index = T ? T->Keys.IndexOfByPredicate([Time](const FOKSceneKey& K) { return FMath::Abs(K.Time - Time) < 1.f / 60.f; }) : INDEX_NONE;
				if (Index == INDEX_NONE || !J->TryGetNumberField(TEXT("to"), To)) return Result(false, TEXT("move needs a key at time and to"));
				Doc->MoveKey(Id, Index, float(To));
			}
			else if (Action == TEXT("ease"))
			{
				static const TMap<FString, EOKSceneEase> Eases = { { TEXT("linear"), EOKSceneEase::Linear }, { TEXT("ease_in_out"), EOKSceneEase::EaseInOut }, { TEXT("ease_in"), EOKSceneEase::EaseIn }, { TEXT("ease_out"), EOKSceneEase::EaseOut }, { TEXT("step"), EOKSceneEase::Step } };
				FString Name;
				J->TryGetStringField(TEXT("ease"), Name);
				FOKSceneKey* K = Doc->KeyAt(Id, float(Time));
				const EOKSceneEase* E = Eases.Find(Name);
				if (!K || !E) return Result(false, TEXT("ease needs a key at time and ease (linear, ease_in_out, ease_in, ease_out, step)"));
				K->Ease = *E;
				Doc->NotifyChanged();
			}
			else if (Action == TEXT("clear")) { Doc->Tracks.RemoveAll([Id](const FOKSceneTrack& T) { return T.ObjectId == Id; }); Doc->NotifyChanged(); }
			else if (Action == TEXT("smooth"))
			{
				bool bSmooth = true;
				J->TryGetBoolField(TEXT("smooth"), bSmooth);
				FOKSceneTrack* T = Doc->TrackOf(Id);
				if (!T) return Result(false, TEXT("no keys yet"));
				T->bSmoothPath = bSmooth;
				Doc->NotifyChanged();
			}
			else if (Action != TEXT("list")) return Result(false, TEXT("action: list, set, remove, move, ease, clear or smooth"));
			TArray<TSharedPtr<FJsonValue>> Keys;
			if (const FOKSceneTrack* T = Doc->TrackOf(Id)) for (const FOKSceneKey& K : T->Keys) Keys.Add(MakeShared<FJsonValueObject>(KeyJson(K)));
			TSharedRef<FJsonObject> O = Result(true);
			O->SetArrayField(TEXT("keys"), Keys);
			O->SetNumberField(TEXT("duration"), Doc->Duration);
			if (const FOKSceneTrack* T = Doc->TrackOf(Id)) O->SetBoolField(TEXT("smooth"), T->bSmoothPath);
			return O;
		}
		if (Op == TEXT("duplicate"))
		{
			TArray<TSharedPtr<FJsonValue>> New;
			TArray<int32> Ids;
			for (int32 Id : ReadIds(J, TEXT("ids"))) { const int32 Copy = Doc->Duplicate(Id); if (Copy >= 0) { New.Add(MakeShared<FJsonValueNumber>(Copy)); Ids.Add(Copy); } }
			if (Ids.Num()) G.SelectMany(Ids, Ids.Last());
			TSharedRef<FJsonObject> O = Result(Ids.Num() > 0, Ids.Num() ? FString() : TEXT("nothing duplicated"));
			O->SetArrayField(TEXT("ids"), New);
			return O;
		}
		if (Op == TEXT("export"))
		{
			FString Kind = TEXT("png"), Folder;
			J->TryGetStringField(TEXT("kind"), Kind);
			J->TryGetStringField(TEXT("folder"), Folder);
			double Fps = 30., W = 1920., H = 1080., From = 0., To = -1.;
			J->TryGetNumberField(TEXT("fps"), Fps); J->TryGetNumberField(TEXT("width"), W); J->TryGetNumberField(TEXT("height"), H);
			J->TryGetNumberField(TEXT("from"), From); J->TryGetNumberField(TEXT("to"), To);
			TSharedRef<FJsonObject> O = Result(true);
			if (Kind == TEXT("okanim"))
			{
				FString Error;
				const FString File = G.ExportOkAnim(ReadId(J, TEXT("id")), float(Fps), Error);
				if (File.IsEmpty()) return Result(false, Error);
				O->SetStringField(TEXT("file"), File);
				return O;
			}
			if (Kind != TEXT("png") && Kind != TEXT("mp4")) return Result(false, TEXT("kind: png, mp4 or okanim"));
			if (Kind == TEXT("mp4") && !FPaths::FileExists(UOKStudioGame::FfmpegPath())) return Result(false, TEXT("no ffmpeg at ") + UOKStudioGame::FfmpegPath());
			O->SetStringField(TEXT("folder"), G.ExportFrames(float(Fps), int32(W), int32(H), Kind == TEXT("mp4"), float(From), float(To), Folder));
			return O;
		}
		if (Op == TEXT("scenes"))
		{
			FString Folder;
			J->TryGetStringField(TEXT("folder"), Folder);
			if (Folder.IsEmpty()) Folder = UOKStudioGame::SceneDirectory();
			TArray<FString> Names, More;
			IFileManager::Get().FindFiles(Names, *(Folder / TEXT("*.png")), true, false);
			IFileManager::Get().FindFiles(More, *(Folder / TEXT("*.okscene")), true, false);
			Names.Append(More);
			TArray<TSharedPtr<FJsonValue>> Files;
			for (const FString& N : Names)
			{
				const FString Path = Folder / N;
				const FOKSceneCard::EKind K = N.EndsWith(TEXT(".okscene")) ? FOKSceneCard::EKind::Json : FOKSceneCard::Kind(Path);
				if (K == FOKSceneCard::EKind::None) continue;
				TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("path"), Path);
				O->SetStringField(TEXT("name"), FPaths::GetBaseFilename(N));
				O->SetStringField(TEXT("kind"), K == FOKSceneCard::EKind::KStudio ? TEXT("kstudio") : (N.EndsWith(TEXT(".okscene")) ? TEXT("okscene") : TEXT("ok_scene_card")));
				O->SetStringField(TEXT("modified"), IFileManager::Get().GetTimeStamp(*Path).ToIso8601());
				Files.Add(MakeShared<FJsonValueObject>(O));
			}
			TSharedRef<FJsonObject> O = Result(true);
			O->SetStringField(TEXT("folder"), Folder);
			O->SetArrayField(TEXT("files"), Files);
			return O;
		}
		if (Op == TEXT("report"))
		{
			TArray<TSharedPtr<FJsonValue>> Load, Missing;
			for (const FString& L : G.LoadReport()) Load.Add(MakeShared<FJsonValueString>(L));
			const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			for (const FOKSceneObject& X : Doc->Objects)
			{
				FString Why;
				if (X.Kind == EOKSceneObjectKind::Character && !FPaths::FileExists(FPaths::IsRelative(X.Character.Card) ? Project / X.Character.Card : X.Character.Card)) Why = TEXT("card missing: ") + X.Character.Card;
				if (X.Kind == EOKSceneObjectKind::Item) { FString Model, Prefab, Error; if (!AOKSceneActor::ItemModel(X.Item, Model, Prefab, Error)) Why = Error; }
				if (X.Kind == EOKSceneObjectKind::Effect && !FOKStudioCatalog::Get().FindEffect(X.Effect.Effect)) Why = TEXT("effect missing: ") + X.Effect.Effect;
				if (!Why.IsEmpty()) Missing.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("#%d %s: %s"), X.Id, *X.Name, *Why)));
			}
			TSharedRef<FJsonObject> O = Result(true);
			O->SetArrayField(TEXT("load"), Load);
			O->SetArrayField(TEXT("missing"), Missing);
			return O;
		}
		// What does not fit the window at its current size (U28): {"open": "<float>"} / {"close": ...} opens a floating
		// window, {"page": n, "bones": k} a detail page (EPage, the bones window's sub page); without them it reports, for
		// every shown part of the studio canvas (floats, the detail window, the shell groups), the widgets reaching out of
		// it, text cut short and parts outside the view. Lists and scroll boxes run on by design and are not checked inside.
		if (Op == TEXT("ui_fit"))
		{
			FString Id;
			if (J->TryGetStringField(TEXT("open"), Id)) { P.OpenFloat(Id); return Result(P.Floats.Contains(Id), P.Floats.Contains(Id) ? FString() : TEXT("no such window")); }
			if (J->TryGetStringField(TEXT("close"), Id)) { P.CloseFloat(Id); return Result(true); }
			int32 PageNo = -1;
			if (J->TryGetNumberField(TEXT("page"), PageNo))
			{
				J->TryGetNumberField(TEXT("bones"), P.BonePage);
				P.SetPage(SOKStudioPanel::EPage(FMath::Clamp(PageNo, 0, int32(SOKStudioPanel::EPage::Camera))));
				return Result(true);
			}
			if (!P.Canvas) return Result(false, TEXT("no canvas"));
			TArray<TSharedPtr<FJsonValue>> Out, Shown;
			auto Note = [&Out](const FString& Region, const FString& What, const FString& Widget, const FSlateRect& R, const FSlateRect& Bound, float Scale)
			{
				TSharedRef<FJsonObject> X = MakeShared<FJsonObject>();
				X->SetStringField(TEXT("region"), Region);
				X->SetStringField(TEXT("what"), What);
				X->SetStringField(TEXT("widget"), Widget);
				// How far it reaches out, in canvas units (left, top, right, bottom; 0 where it fits).
				X->SetStringField(TEXT("over"), FString::Printf(TEXT("%.0f %.0f %.0f %.0f"), FMath::Max(0.f, Bound.Left - R.Left) / Scale, FMath::Max(0.f, Bound.Top - R.Top) / Scale, FMath::Max(0.f, R.Right - Bound.Right) / Scale, FMath::Max(0.f, R.Bottom - Bound.Bottom) / Scale));
				Out.Add(MakeShared<FJsonValueObject>(X));
			};
			auto Describe = [](const SWidget& W)
			{
				FString Text;
				if (W.GetType() == TEXT("STextBlock")) Text = static_cast<const STextBlock&>(W).GetText().ToString().Left(40);
				return Text.IsEmpty() ? W.GetType().ToString() : W.GetType().ToString() + TEXT(" ") + Text;
			};
			TFunction<void(const FArrangedWidget&, const FString&, const FSlateRect&, float)> Walk;
			Walk = [&](const FArrangedWidget& A, const FString& Region, const FSlateRect& Bound, float Scale)
			{
				const SWidget& W = A.Widget.Get();
				const FSlateRect R = A.Geometry.GetLayoutBoundingRect();
				if (R.Left < Bound.Left - 1.f || R.Top < Bound.Top - 1.f || R.Right > Bound.Right + 1.f || R.Bottom > Bound.Bottom + 1.f)
				{
					Note(Region, TEXT("outside"), Describe(W), R, Bound, Scale);
					return;
				}
				// Text shortened with an ellipsis on purpose carries the tag "Ellipsis".
				const TSharedPtr<FTagMetaData> TextTag = W.GetMetaData<FTagMetaData>();
				if (W.GetType() == TEXT("STextBlock") && !(TextTag && TextTag->Tag == TEXT("Ellipsis")) && W.GetDesiredSize().X > A.Geometry.GetLocalSize().X + 1.f)
					Note(Region, TEXT("text cut"), Describe(W), R, FSlateRect(R.Left, R.Top, R.Left + A.Geometry.GetLocalSize().X * Scale, R.Bottom), Scale);
				// Inside a scroll box or list only the width counts (rows below the fold are scrolled to), within the box.
				const FString Type = W.GetType().ToString();
				FSlateRect Inner = Bound;
				if (Type == TEXT("SScrollBox") || Type.StartsWith(TEXT("SListView")) || Type.StartsWith(TEXT("STreeView")) || Type.StartsWith(TEXT("STileView")))
					Inner = FSlateRect(FMath::Max(Bound.Left, R.Left), -1e9f, FMath::Min(Bound.Right, R.Right), 1e9f);
				FArrangedChildren Children(EVisibility::Visible);
				W.ArrangeChildren(A.Geometry, Children);
				for (int32 I = 0; I < Children.Num(); ++I) Walk(Children[I], Region, Inner, Scale);
			};
			const FGeometry& CanvasGeometry = P.Canvas->GetCachedGeometry();
			const FSlateRect View = CanvasGeometry.GetLayoutBoundingRect();
			const float Scale = CanvasGeometry.Scale > 0.f ? CanvasGeometry.Scale : 1.f;
			FArrangedChildren Parts(EVisibility::Visible);
			P.Canvas->ArrangeChildren(CanvasGeometry, Parts);
			for (int32 I = 0; I < Parts.Num(); ++I)
			{
				const FArrangedWidget& Part = Parts[I];
				const TSharedPtr<FTagMetaData> Tag = Part.Widget->GetMetaData<FTagMetaData>();
				const FString Region = Tag ? Tag->Tag.ToString() : Part.Widget->GetType().ToString();
				const FSlateRect R = Part.Geometry.GetLayoutBoundingRect();
				Shown.Add(MakeShared<FJsonValueString>(Region));
				if (R.Left < View.Left - 1.f || R.Top < View.Top - 1.f || R.Right > View.Right + 1.f || R.Bottom > View.Bottom + 1.f) Note(Region, TEXT("outside view"), Region, R, View, Scale);
				FArrangedChildren Inner(EVisibility::Visible);
				Part.Widget->ArrangeChildren(Part.Geometry, Inner);
				for (int32 C = 0; C < Inner.Num(); ++C) Walk(Inner[C], Region, R, Scale);
			}
			TSharedRef<FJsonObject> O = Result(true);
			O->SetStringField(TEXT("view"), FString::Printf(TEXT("%.0fx%.0f px, canvas %.0fx%.0f"), View.GetSize().X, View.GetSize().Y, CanvasGeometry.GetLocalSize().X, CanvasGeometry.GetLocalSize().Y));
			O->SetArrayField(TEXT("shown"), Shown);
			O->SetArrayField(TEXT("problems"), Out);
			return O;
		}
		if (Op == TEXT("ui_shot"))
		{
			FString Name;
			J->TryGetStringField(TEXT("name"), Name);
			const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OKStudio/mcp_ui")) / FPaths::MakeValidFileName(Name.IsEmpty() ? FDateTime::Now().ToString(TEXT("ui_%Y%m%d_%H%M%S")) : Name) + TEXT(".png");
			IFileManager::Get().Delete(*File, false, true, true);
			FScreenshotRequest::RequestScreenshot(File, true, false);
			TSharedRef<FJsonObject> O = Result(true);
			O->SetStringField(TEXT("file"), File);
			return O;
		}
		// Leaves the studio (as its close button: the mode that started it comes back, its UI restored). The channel
		// closes with the studio; with "name", a whole-window screenshot (UI included) is taken a second later to
		// Saved/OKStudio/mcp_ui/<name>.png, showing what the mode looks like afterwards.
		// Clicks a shown button by its text, as a person would, a list row too ({"text": "全裸", "index": 0}; "contains": true matches part of
		// the text): the button's own click handler runs, so a test exercises the UI and not a shortcut around it. The
		// matches (in window order, top to bottom) are listed when "index" is out of range. "tag": true matches the widget's
		// tag (FTagMetaData) instead, for picture buttons (thumbnails) that show no text.
		if (Op == TEXT("click"))
		{
			FString Text;
			J->TryGetStringField(TEXT("text"), Text);
			int32 Index = 0;
			J->TryGetNumberField(TEXT("index"), Index);
			bool bContains = false;
			J->TryGetBoolField(TEXT("contains"), bContains);
			bool bTag = false; // match the widget's tag instead (picture buttons have no text)
			J->TryGetBoolField(TEXT("tag"), bTag);
			bool bTip = false; // match a button's tooltip (icon buttons: play, first, key...)
			J->TryGetBoolField(TEXT("tip"), bTip);
			if (Text.IsEmpty() || !FSlateApplication::IsInitialized()) return Result(false, TEXT("text required"));
			// Buttons and check boxes (a check box's label is a text block inside it; clicking it toggles).
			TArray<TSharedRef<SWidget>> Found;
			TArray<FString> Labels;
			TFunction<void(const TSharedRef<SWidget>&)> Walk = [&](const TSharedRef<SWidget>& W)
			{
				if (!W->GetVisibility().IsVisible()) return;
				if (bTip)
				{
					// Icon buttons carry their tooltip as a "tip:" tag (OKStudioStyle::IconButton). Tooltips themselves are
					// not read: many are lambdas over objects that may be gone (ERROR_LOG: reading them crashed the game).
					const TSharedPtr<FTagMetaData> Tag = W->GetType() == TEXT("SButton") ? W->GetMetaData<FTagMetaData>() : nullptr;
					const FString T = Tag.IsValid() ? Tag->Tag.ToString() : FString();
					if (T.StartsWith(TEXT("tip:")) && (bContains ? T.Mid(4).Contains(Text) : T.Mid(4) == Text)) { Found.Add(W); Labels.Add(T.Mid(4)); }
				}
				else if (bTag)
				{
					const TSharedPtr<FTagMetaData> Tag = W->GetMetaData<FTagMetaData>();
					const FString T = Tag.IsValid() ? Tag->Tag.ToString() : FString();
					if (!T.IsEmpty() && (bContains ? T.Contains(Text) : T == Text))
					{
						// The button is the widget itself, inside it (a tag on the box round it) or round it.
						TSharedPtr<SWidget> Hit;
						TFunction<void(const TSharedRef<SWidget>&)> Down = [&](const TSharedRef<SWidget>& C)
						{
							if (Hit) return;
							if (C->GetType() == TEXT("SButton") || C->GetType() == TEXT("SCheckBox")) { Hit = C; return; }
							FChildren* Ch = C->GetChildren();
							for (int32 I = 0; Ch && I < Ch->Num(); ++I) Down(Ch->GetChildAt(I));
						};
						Down(W);
						for (TSharedPtr<SWidget> P = W->GetParentWidget(); !Hit && P; P = P->GetParentWidget())
							if (P->GetType() == TEXT("SButton") || P->GetType() == TEXT("SCheckBox")) Hit = P;
						if (Hit) { Found.Add(Hit.ToSharedRef()); Labels.Add(T); }
					}
				}
				else if (W->GetType() == TEXT("STextBlock"))
				{
					const FString T = StaticCastSharedRef<STextBlock>(W)->GetText().ToString();
					if (bContains ? T.Contains(Text) : T == Text)
						for (TSharedPtr<SWidget> P = W->GetParentWidget(); P; P = P->GetParentWidget())
							if (P->GetType() == TEXT("SButton") || P->GetType() == TEXT("SCheckBox") || P->GetType().ToString().StartsWith(TEXT("STableRow"))) { Found.Add(P.ToSharedRef()); Labels.Add(T); break; }
				}
				FChildren* Children = W->GetChildren();
				for (int32 I = 0; Children && I < Children->Num(); ++I) Walk(Children->GetChildAt(I));
			};
			for (const TSharedRef<SWindow>& Window : FSlateApplication::Get().GetInteractiveTopLevelWindows()) Walk(Window);
			if (!Found.IsValidIndex(Index)) return Result(false, FString::Printf(TEXT("%d buttons match: %s"), Found.Num(), *FString::Join(Labels, TEXT(" | "))));
			if (!Found[Index]->IsEnabled()) return Result(false, TEXT("the button is disabled"));
			if (Found[Index]->GetType() == TEXT("SCheckBox")) StaticCastSharedRef<SCheckBox>(Found[Index])->ToggleCheckedState();
			else if (Found[Index]->GetType().ToString().StartsWith(TEXT("STableRow")))
			{
				// A list row has no click handler of its own: a left click at its middle, through Slate as the mouse would.
				const FVector2D At = Found[Index]->GetTickSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5, 0.5));
				FSlateApplication& App = FSlateApplication::Get();
				// The row reports a new selection on release only (its highlight changes on press), and a capture left over
				// from an earlier press would take the release: start from no capture, as a fresh press does. (A tree click
				// once only moved the highlight, 2026-11-01; not seen again, the log says whether a capture was released.)
				if (App.HasAnyMouseCaptor()) { UE_LOG(LogTemp, Display, TEXT("OK studio control: click released a stale mouse capture")); App.ReleaseAllPointerCapture(); }
				App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, 0, At, At, TSet<FKey>{ EKeys::LeftMouseButton }, EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
				App.ProcessMouseButtonUpEvent(FPointerEvent(0, 0, At, At, TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
			}
			else StaticCastSharedRef<SButton>(Found[Index])->SimulateClick();
			TSharedRef<FJsonObject> O = Result(true);
			O->SetNumberField(TEXT("matches"), Found.Num());
			O->SetStringField(TEXT("clicked"), Labels[Index]);
			return O;
		}
		// Types into a text field as the keyboard would: the field whose hint (or text) contains "field" takes keyboard
		// focus, Ctrl+A and Backspace clear it, each character is a key press and "enter" commits (its own handlers run).
		if (Op == TEXT("type"))
		{
			FString Field, Text;
			J->TryGetStringField(TEXT("field"), Field);
			J->TryGetStringField(TEXT("text"), Text);
			int32 Index = 0;
			J->TryGetNumberField(TEXT("index"), Index);
			bool bEnter = false;
			J->TryGetBoolField(TEXT("enter"), bEnter);
			if (Field.IsEmpty() || !FSlateApplication::IsInitialized()) return Result(false, TEXT("field required"));
			TArray<TSharedRef<SEditableText>> Found;
			TFunction<void(const TSharedRef<SWidget>&)> Walk = [&](const TSharedRef<SWidget>& W)
			{
				if (!W->GetVisibility().IsVisible()) return;
				if (W->GetType() == TEXT("SEditableText"))
				{
					const TSharedRef<SEditableText> E = StaticCastSharedRef<SEditableText>(W);
					if (E->GetHintText().ToString().Contains(Field) || E->GetText().ToString().Contains(Field)) Found.Add(E);
				}
				FChildren* Children = W->GetChildren();
				for (int32 I = 0; Children && I < Children->Num(); ++I) Walk(Children->GetChildAt(I));
			};
			for (const TSharedRef<SWindow>& Window : FSlateApplication::Get().GetInteractiveTopLevelWindows()) Walk(Window);
			if (!Found.IsValidIndex(Index)) return Result(false, FString::Printf(TEXT("%d fields match"), Found.Num()));
			FSlateApplication& App = FSlateApplication::Get();
			App.SetKeyboardFocus(Found[Index], EFocusCause::Mouse);
			auto Key = [&App](const FKey& K, bool bCtrl)
			{
				const FModifierKeysState Mods(false, false, bCtrl, false, false, false, false, false, false);
				App.ProcessKeyDownEvent(FKeyEvent(K, Mods, 0, false, 0, 0));
				App.ProcessKeyUpEvent(FKeyEvent(K, Mods, 0, false, 0, 0));
			};
			Key(EKeys::A, true);
			Key(EKeys::BackSpace, false);
			for (const TCHAR Ch : Text) App.ProcessKeyCharEvent(FCharacterEvent(Ch, FModifierKeysState(), 0, false));
			if (bEnter) Key(EKeys::Enter, false);
			TSharedRef<FJsonObject> O = Result(true);
			O->SetNumberField(TEXT("matches"), Found.Num());
			O->SetStringField(TEXT("text"), Found[Index]->GetText().ToString());
			return O;
		}
		// A console command on the game world (cvars, show flags, stat), for diagnosing what the studio draws.
		if (Op == TEXT("exec"))
		{
			FString Command;
			J->TryGetStringField(TEXT("command"), Command);
			UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
			if (Command.IsEmpty() || !World) return Result(false, TEXT("command and a game world required"));
			return Result(GEngine->Exec(World, *Command), FString());
		}
		// The drawn parts (mesh components of the scene's preview actors: map, items, characters) whose name contains
		// "match": listed with their owner and bounds, and hidden or shown again with "hide" (diagnostics).
		if (Op == TEXT("parts"))
		{
			FString Match;
			J->TryGetStringField(TEXT("match"), Match);
			bool bHide = false;
			const bool bSet = J->TryGetBoolField(TEXT("hide"), bHide);
			// "has_param": only parts whose first material has that parameter; "params": true lists the material's
			// parameter values (scalars, vectors, textures).
			FString HasParam;
			J->TryGetStringField(TEXT("has_param"), HasParam);
			bool bParams = false;
			J->TryGetBoolField(TEXT("params"), bParams);
			// "owner": only the parts of the preview actor with that name (one item of several sharing part names).
			FString Owner;
			J->TryGetStringField(TEXT("owner"), Owner);
			UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
			if (!World) return Result(false, TEXT("no game world"));
			TArray<TSharedPtr<FJsonValue>> Out;
			for (TActorIterator<AKKPreviewActor> It(World); It; ++It)
			{
				if (!Owner.IsEmpty() && It->GetName() != Owner) continue;
				TInlineComponentArray<UMeshComponent*> Meshes(*It);
				for (UMeshComponent* Mesh : Meshes)
				{
					if (!Match.IsEmpty() && !Mesh->GetName().Contains(Match)) continue;
					UMaterialInterface* Material = Mesh->GetNumMaterials() ? Mesh->GetMaterial(0) : nullptr;
					TArray<FMaterialParameterInfo> Scalars, Vectors, Textures;
					TArray<FGuid> Ids;
					if (Material)
					{
						Material->GetAllScalarParameterInfo(Scalars, Ids);
						Material->GetAllVectorParameterInfo(Vectors, Ids);
						Material->GetAllTextureParameterInfo(Textures, Ids);
					}
					if (!HasParam.IsEmpty())
					{
						const FName Wanted(*HasParam);
						const auto Named = [&Wanted](const FMaterialParameterInfo& I) { return I.Name == Wanted; };
						if (!Scalars.ContainsByPredicate(Named) && !Vectors.ContainsByPredicate(Named) && !Textures.ContainsByPredicate(Named)) continue;
					}
					if (bSet) Mesh->SetVisibility(!bHide);
					if (Out.Num() >= 400) continue;
					TSharedRef<FJsonObject> X = MakeShared<FJsonObject>();
					X->SetStringField(TEXT("name"), Mesh->GetName());
					X->SetStringField(TEXT("owner"), It->GetName());
					const FBoxSphereBounds B = Mesh->Bounds;
					X->SetStringField(TEXT("center"), B.Origin.ToString());
					X->SetNumberField(TEXT("radius"), B.SphereRadius);
					X->SetBoolField(TEXT("visible"), Mesh->IsVisible());
					if (bParams && Material)
					{
						X->SetStringField(TEXT("material"), Material->GetPathName());
						X->SetStringField(TEXT("base"), Material->GetBaseMaterial() ? Material->GetBaseMaterial()->GetPathName() : FString());
						TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
						for (const FMaterialParameterInfo& I : Scalars) { float V = 0.f; if (Material->GetScalarParameterValue(I, V)) P->SetNumberField(I.Name.ToString(), V); }
						for (const FMaterialParameterInfo& I : Vectors) { FLinearColor V; if (Material->GetVectorParameterValue(I, V)) P->SetStringField(I.Name.ToString(), V.ToString()); }
						for (const FMaterialParameterInfo& I : Textures)
						{
							UTexture* T = nullptr;
							if (Material->GetTextureParameterValue(I, T)) P->SetStringField(I.Name.ToString(), T ? FString::Printf(TEXT("%s %dx%d"), *T->GetName(), int32(T->GetSurfaceWidth()), int32(T->GetSurfaceHeight())) : TEXT("null"));
						}
						X->SetObjectField(TEXT("params"), P);
					}
					Out.Add(MakeShared<FJsonValueObject>(X));
				}
			}
			TSharedRef<FJsonObject> O = Result(true);
			O->SetArrayField(TEXT("parts"), Out);
			return O;
		}
		if (Op == TEXT("leave"))
		{
			FString Name;
			J->TryGetStringField(TEXT("name"), Name);
			if (UOKStudioGame* G = UOKStudioGame::Get()) G->RequestStop();
			TSharedRef<FJsonObject> O = Result(true);
			if (!Name.IsEmpty())
			{
				const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OKStudio/mcp_ui")) / FPaths::MakeValidFileName(Name) + TEXT(".png");
				IFileManager::Get().Delete(*File, false, true, true);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([File](float) { FScreenshotRequest::RequestScreenshot(File, true, false); return false; }), 1.f);
				O->SetStringField(TEXT("file"), File);
			}
			return O;
		}
		return nullptr;
	}

	/** Ops that only read (always answered, also while the studio is busy). */
	bool ReadsOnly(const FString& Op, const TSharedRef<FJsonObject>& J)
	{
		static const TSet<FString> Read = { TEXT("state"), TEXT("inspect"), TEXT("catalog_search"), TEXT("bones"), TEXT("bounds"), TEXT("catalog"), TEXT("scenes"), TEXT("report"), TEXT("ui_shot") };
		if (Read.Contains(Op)) return true;
		if (Op == TEXT("scene")) return J->Values.Num() == 0;
		if (Op == TEXT("keys")) { FString A; J->TryGetStringField(TEXT("action"), A); return A.IsEmpty() || A == TEXT("list"); }
		return false;
	}

	/** Runs one op on the studio (game thread). */
	TSharedRef<FJsonObject> Run(const FString& Op, const TSharedRef<FJsonObject>& J)
	{
		UOKStudioGame* G = UOKStudioGame::Get();
		SOKStudioPanel* P = G ? G->GetPanel() : nullptr;
		if (!G || !P) return Result(false, TEXT("no studio running"));
		if (!ReadsOnly(Op, J))
		{
			const FString Busy = G->BusyReason();
			// The timeline op itself may stop playback; buttons and fields work during playback as they do for the user
			// (the stop button among them), and a capture only looks.
			static const TSet<FString> WhilePlaying = { TEXT("timeline"), TEXT("click"), TEXT("type"), TEXT("capture") };
			if (!Busy.IsEmpty() && !(Busy == TEXT("playing") && WhilePlaying.Contains(Op)))
			{
				TSharedRef<FJsonObject> O = Result(false, TEXT("busy: ") + Busy);
				O->SetStringField(TEXT("busy"), Busy);
				return O;
			}
		}
		UOKSceneDocument* Doc = G->Document;
		if (Op == TEXT("state")) return StateOf(*G);
		if (Op == TEXT("inspect"))
		{
			const FOKSceneObject* X = Doc->Find(ReadId(J, TEXT("id")));
			if (!X) return Result(false, TEXT("no such object"));
			TSharedRef<FJsonObject> O = Result(true);
			if (TSharedPtr<FJsonObject> Obj = FJsonObjectConverter::UStructToJsonObject(*X)) O->SetObjectField(TEXT("object"), Obj);
			if (const FString* E = G->SceneActor ? G->SceneActor->Errors.Find(X->Id) : nullptr) O->SetStringField(TEXT("error"), *E);
			return O;
		}
		if (Op == TEXT("catalog_search"))
		{
			FString Query;
			J->TryGetStringField(TEXT("query"), Query);
			TArray<FString> Kinds;
			J->TryGetStringArrayField(TEXT("kinds"), Kinds);
			const int32 Limit = ReadId(J, TEXT("limit"), 50);
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const SOKStudioPanel::FHit& H : P->SearchCatalog(Query, Kinds, Limit))
			{
				TSharedRef<FJsonObject> X = MakeShared<FJsonObject>();
				X->SetStringField(TEXT("kind"), H.Kind); X->SetStringField(TEXT("key"), H.Key); X->SetStringField(TEXT("name"), H.Name);
				if (!H.Path.IsEmpty()) X->SetStringField(TEXT("path"), H.Path);
				Out.Add(MakeShared<FJsonValueObject>(X));
			}
			TSharedRef<FJsonObject> O = Result(true);
			O->SetArrayField(TEXT("results"), Out);
			return O;
		}
		if (Op == TEXT("spawn"))
		{
			FString Kind, Name, Value;
			J->TryGetStringField(TEXT("kind"), Kind);
			const int32 Parent = ReadId(J, TEXT("parent"));
			if (Parent >= 0) G->Select(Parent); else G->Select(-1);
			const int32 Before = Doc->NextId;
			bool bOk = false;
			if (Kind == TEXT("character")) { J->TryGetStringField(TEXT("card"), Value); bOk = !Value.IsEmpty() && P->AddOrReplaceCard(Value, false); }
			else if (Kind == TEXT("item")) { J->TryGetStringField(TEXT("item"), Value); bOk = P->RunCommand(TEXT("add.item"), Value); }
			else if (Kind == TEXT("light")) { J->TryGetStringField(TEXT("light_type"), Value); bOk = P->RunCommand(TEXT("add.light"), Value); }
			else if (Kind == TEXT("camera")) bOk = P->RunCommand(TEXT("add.camera"));
			else if (Kind == TEXT("effect")) { J->TryGetStringField(TEXT("effect"), Value); bOk = !Value.IsEmpty() && P->RunCommand(TEXT("add.effect"), Value); }
			else if (Kind == TEXT("folder")) bOk = P->RunCommand(TEXT("add.folder"));
			else return Result(false, TEXT("kind must be character, item, light, camera, effect or folder"));
			FOKSceneObject* X = Doc->NextId > Before ? Doc->Find(Doc->NextId - 1) : nullptr;
			if (!bOk || !X) return Result(false, TEXT("could not add ") + Kind + (Value.IsEmpty() ? FString() : TEXT(" ") + Value));
			if (Parent >= 0 && X->Parent != Parent && Doc->Find(Parent)) Doc->Reparent(X->Id, Parent);
			X = Doc->Find(X->Id);
			FString Bone;
			if (J->TryGetStringField(TEXT("bone"), Bone) && !Bone.IsEmpty()) { X->AttachBone = Bone; X->Transform = FTransform::Identity; }
			if (J->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty()) X->Name = Name;
			FVector V = FVector::ZeroVector;
			if (ReadVec(J, TEXT("location"), V)) X->Transform.SetLocation(V);
			if (ReadVec(J, TEXT("rotation"), V)) X->Transform.SetRotation(FRotator(V.X, V.Y, V.Z).Quaternion());
			if (ReadVec(J, TEXT("scale"), V)) X->Transform.SetScale3D(V);
			Doc->NotifyChanged();
			G->Select(X->Id);
			TSharedRef<FJsonObject> O = Result(true);
			O->SetNumberField(TEXT("id"), X->Id);
			return O;
		}
		if (Op == TEXT("transform"))
		{
			FOKSceneObject* X = Doc->Find(ReadId(J, TEXT("id")));
			if (!X) return Result(false, TEXT("no such object"));
			bool bRelative = false;
			J->TryGetBoolField(TEXT("relative"), bRelative);
			FVector V;
			if (ReadVec(J, TEXT("location"), V)) X->Transform.SetLocation(bRelative ? X->Transform.GetLocation() + V : V);
			if (ReadVec(J, TEXT("rotation"), V)) { const FQuat Q = FRotator(V.X, V.Y, V.Z).Quaternion(); X->Transform.SetRotation(bRelative ? Q * X->Transform.GetRotation() : Q); }
			if (ReadVec(J, TEXT("scale"), V)) X->Transform.SetScale3D(bRelative ? X->Transform.GetScale3D() * V : V);
			Doc->NotifyChanged();
			return Result(true);
		}
		if (Op == TEXT("reparent"))
		{
			const int32 Id = ReadId(J, TEXT("id")), Parent = ReadId(J, TEXT("parent"), -1);
			if (!Doc->Find(Id) || !Doc->Reparent(Id, Parent)) return Result(false, TEXT("cannot move it there (missing object or a cycle)"));
			FString Bone;
			if (J->TryGetStringField(TEXT("bone"), Bone) && !Bone.IsEmpty()) if (FOKSceneObject* X = Doc->Find(Id)) { X->AttachBone = Bone; X->Transform = FTransform::Identity; Doc->NotifyChanged(); }
			return Result(true);
		}
		if (Op == TEXT("delete")) { for (int32 Id : ReadIds(J, TEXT("ids"))) Doc->RemoveObject(Id); G->Select(-1); return Result(true); }
		if (Op == TEXT("select")) { const TArray<int32> Ids = ReadIds(J, TEXT("ids")); G->SelectMany(Ids, Ids.Num() ? Ids.Last() : -1); return Result(true); }
		if (Op == TEXT("pose"))
		{
			FOKSceneObject* X = Doc->Find(ReadId(J, TEXT("id")));
			if (!X || X->Kind != EOKSceneObjectKind::Character) return Result(false, TEXT("not a character"));
			G->Select(X->Id);
			bool bFlag = false;
			if (J->TryGetBoolField(TEXT("clear_fk"), bFlag) && bFlag) X->Character.Fk.Reset();
			if (J->TryGetBoolField(TEXT("clear_ik"), bFlag) && bFlag) X->Character.Ik.Reset();
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (J->TryGetArrayField(TEXT("fk"), List))
				for (const TSharedPtr<FJsonValue>& V : *List)
				{
					const TSharedPtr<FJsonObject> B = V->AsObject();
					FVector E = FVector::ZeroVector;
					if (B && ReadVec(B.ToSharedRef(), TEXT("euler"), E)) SOKStudioPanel::SetFk(*X, B->GetStringField(TEXT("bone")), E);
				}
			if (J->TryGetArrayField(TEXT("ik"), List))
				for (const TSharedPtr<FJsonValue>& V : *List)
				{
					const TSharedPtr<FJsonObject> T = V->AsObject();
					FVector Pos = FVector::ZeroVector;
					if (!T || !ReadVec(T.ToSharedRef(), TEXT("position"), Pos)) continue;
					const int32 E = int32(T->GetNumberField(TEXT("effector")));
					X->Character.Ik.RemoveAll([E](const FOKSceneIkTarget& I) { return I.Effector == E; });
					FOKSceneIkTarget& Added = X->Character.Ik.Add_GetRef({ E, Pos });
					double W = 1.;
					if (T->TryGetNumberField(TEXT("weight"), W)) Added.Weight = FMath::Clamp(float(W), 0.f, 1.f);
				}
			FString S;
			if (J->TryGetStringField(TEXT("pose_file"), S) && !S.IsEmpty() && !P->LoadPose(S)) return Result(false, TEXT("pose file not loaded"));
			if (J->TryGetStringField(TEXT("anim"), S) && !S.IsEmpty())
			{
				TArray<FString> K;
				S.ParseIntoArray(K, TEXT("/"));
				const FOKStudioCatalog& C = FOKStudioCatalog::Get();
				const int32 Index = K.Num() == 4 ? C.Anims.IndexOfByPredicate([&K](const FOKStudioAnim& A) { return A.Kind == FCString::Atoi(*K[0]) && A.Group == FCString::Atoi(*K[1]) && A.Category == FCString::Atoi(*K[2]) && A.No == FCString::Atoi(*K[3]); }) : INDEX_NONE;
				if (Index == INDEX_NONE || !P->PickAnim(Index)) return Result(false, TEXT("no such animation (kind/group/category/no)"));
				X = Doc->Find(X->Id);
			}
			if (J->TryGetStringField(TEXT("anim_file"), S) && !S.IsEmpty()) { if (!P->PickAnimFile(*X, S)) return Result(false, TEXT("anim_file not set")); X = Doc->Find(X->Id); }
			if (J->TryGetStringField(TEXT("video_pair"), S) && !S.IsEmpty()) { if (!P->PickVideoPair(S)) return Result(false, TEXT("no such video pair (studio_catalog kind video)")); X = Doc->Find(X->Id); }
			const TSharedPtr<FJsonObject>* Sub = nullptr;
			if (J->TryGetObjectField(TEXT("expression"), Sub))
			{
				double D;
				if ((*Sub)->TryGetNumberField(TEXT("eyebrow"), D)) X->Character.EyebrowPattern = int32(D);
				if ((*Sub)->TryGetNumberField(TEXT("eyes"), D)) X->Character.EyesPattern = int32(D);
				if ((*Sub)->TryGetNumberField(TEXT("mouth"), D)) X->Character.MouthPattern = int32(D);
				if ((*Sub)->TryGetNumberField(TEXT("mouth_open"), D)) X->Character.MouthOpen = float(D);
				if ((*Sub)->TryGetNumberField(TEXT("eyes_open"), D)) X->Character.EyesOpen = float(D);
			}
			if (J->TryGetObjectField(TEXT("hands"), Sub))
			{
				double D;
				if ((*Sub)->TryGetNumberField(TEXT("left"), D)) X->Character.HandLeft = int32(D);
				if ((*Sub)->TryGetNumberField(TEXT("right"), D)) X->Character.HandRight = int32(D);
			}
			if (J->TryGetObjectField(TEXT("look"), Sub))
			{
				auto Look = [](const TSharedPtr<FJsonValue>& V, EOKSceneLook& Out)
				{
					if (!V) return;
					static const TMap<FString, EOKSceneLook> Names = { { TEXT("animation"), EOKSceneLook::Animation }, { TEXT("camera"), EOKSceneLook::Camera }, { TEXT("point"), EOKSceneLook::Point }, { TEXT("fixed"), EOKSceneLook::Fixed },
						{ TEXT("front"), EOKSceneLook::Front }, { TEXT("away"), EOKSceneLook::Away } };
					if (V->Type == EJson::Number) Out = EOKSceneLook(FMath::Clamp(int32(V->AsNumber()), 0, int32(EOKSceneLook::Away)));
					else if (const EOKSceneLook* L = Names.Find(V->AsString().ToLower())) Out = *L;
				};
				Look((*Sub)->TryGetField(TEXT("eyes")), X->Character.EyesLook);
				Look((*Sub)->TryGetField(TEXT("neck")), X->Character.NeckLook);
				double D;
				if ((*Sub)->TryGetNumberField(TEXT("neck_follow"), D)) X->Character.NeckFollow = FMath::Clamp(float(D), 0.f, 1.f);
				FVector Point;
				if (ReadVec(Sub->ToSharedRef(), TEXT("point"), Point)) X->Character.LookPoint = Point;
			}
			// Studio controls (option1, option2, pattern) by name; an empty object gives them all back to the animation.
			if (J->TryGetObjectField(TEXT("anim_controls"), Sub))
			{
				if ((*Sub)->Values.IsEmpty()) X->Character.AnimFloats.Reset();
				for (const auto& Pair : (*Sub)->Values)
				{
					const FString Key(*Pair.Key);
					FOKSceneAnimFloat* F = X->Character.AnimFloats.FindByPredicate([&Key](const FOKSceneAnimFloat& A) { return A.Name == Key; });
					if (!F) { F = &X->Character.AnimFloats.AddDefaulted_GetRef(); F->Name = Key; }
					F->Value = float(Pair.Value->AsNumber());
				}
			}
			Doc->NotifyChanged();
			if (J->TryGetStringField(TEXT("mirror"), S) && !S.IsEmpty())
				if (FOKSceneObject* Y = Doc->Find(X->Id)) P->MirrorPose(*Y, S == TEXT("left_to_right") ? 1 : S == TEXT("right_to_left") ? 2 : 0);
			return Result(true);
		}
		if (Op == TEXT("camera"))
		{
			FVector V;
			double D;
			FString Preset;
			bool bFlag = false;
			if (ReadVec(J, TEXT("focus"), V)) { G->Focus = V; Doc->ActiveCamera = -1; }
			if (J->TryGetNumberField(TEXT("yaw"), D)) { G->Yaw = float(D); Doc->ActiveCamera = -1; }
			if (J->TryGetNumberField(TEXT("pitch"), D)) { G->Pitch = FMath::Clamp(float(D), -89.f, 89.f); Doc->ActiveCamera = -1; }
			if (J->TryGetNumberField(TEXT("distance"), D)) { G->Distance = FMath::Clamp(float(D), 20.f, 5000.f); Doc->ActiveCamera = -1; }
			if (J->TryGetStringField(TEXT("preset"), Preset))
			{
				static const TMap<FString, int32> Presets = { { TEXT("front"), 0 }, { TEXT("side"), 1 }, { TEXT("top"), 2 }, { TEXT("back"), 3 }, { TEXT("left"), 4 }, { TEXT("reset"), 5 } };
				if (const int32* N = Presets.Find(Preset)) G->ViewPreset(*N); else return Result(false, TEXT("preset: front, side, top, back, left or reset"));
			}
			if (J->TryGetBoolField(TEXT("focus_selection"), bFlag) && bFlag) G->FocusSelection();
			const int32 Use = ReadId(J, TEXT("use_camera"), -2);
			if (Use != -2) Doc->ActiveCamera = Doc->Find(Use) ? Use : -1;
			const int32 SaveSlot = ReadId(J, TEXT("bookmark_save"), -1), LoadSlot = ReadId(J, TEXT("bookmark_load"), -1);
			if (SaveSlot >= 0) G->SaveBookmark(SaveSlot);
			if (LoadSlot >= 0 && !G->LoadBookmark(LoadSlot)) return Result(false, TEXT("that bookmark is not set"));
			TSharedRef<FJsonObject> O = Result(true);
			O->SetArrayField(TEXT("focus"), Vec(G->Focus));
			O->SetNumberField(TEXT("yaw"), G->Yaw);
			O->SetNumberField(TEXT("pitch"), G->Pitch);
			O->SetNumberField(TEXT("distance"), G->Distance);
			return O;
		}
		if (Op == TEXT("capture"))
		{
			int32 W = ReadId(J, TEXT("width"), 1920), H = ReadId(J, TEXT("height"), 1080);
			bool bAlpha = false;
			J->TryGetBoolField(TEXT("transparent"), bAlpha);
			FString Path;
			J->TryGetStringField(TEXT("path"), Path);
			if (Path.IsEmpty()) Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UserData/Studio/screenshot")) / FDateTime::Now().ToString(TEXT("okstudio_mcp_%Y%m%d_%H%M%S.png"));
			if (FPaths::FileExists(Path)) return Result(false, TEXT("file exists (never overwritten): ") + Path);
			FString Error;
			if (!G->CaptureToPng(Path, W, H, Error, bAlpha)) return Result(false, Error);
			TSharedRef<FJsonObject> O = Result(true);
			O->SetStringField(TEXT("file"), Path);
			// The scene's post passes still compiling: the picture lacks them (fog / occlusion, sun shafts).
			const bool bOpaquePost = G->Document->Post.bFog || G->Document->Post.bAmbientOcclusion;
			if (bOpaquePost && OKStudioScenePostMaterialState(0) != TEXT("ready"))
				O->SetStringField(TEXT("warning_post_opaque"), TEXT("fog / occlusion pass not ready: ") + OKStudioScenePostMaterialState(0));
			if (G->Document->Post.bSunShafts && OKStudioScenePostMaterialState(1) != TEXT("ready"))
				O->SetStringField(TEXT("warning_post_shafts"), TEXT("sun shafts pass not ready: ") + OKStudioScenePostMaterialState(1));
			return O;
		}
		if (Op == TEXT("save"))
		{
			FString Name;
			J->TryGetStringField(TEXT("name"), Name);
			const FString Clean = FPaths::MakeValidFileName(Name.TrimStartAndEnd());
			if (Clean.IsEmpty()) return Result(false, TEXT("name required"));
			const FString Path = UOKStudioGame::SceneDirectory() / Clean + TEXT(".png");
			if (FPaths::FileExists(Path)) return Result(false, TEXT("file exists (never overwritten): ") + Path);
			if (!P->SaveNamed(Clean)) return Result(false, TEXT("not saved"));
			TSharedRef<FJsonObject> O = Result(true);
			O->SetStringField(TEXT("file"), Path);
			return O;
		}
		if (Op == TEXT("load"))
		{
			FString Path;
			J->TryGetStringField(TEXT("path"), Path);
			bool bMerge = false;
			J->TryGetBoolField(TEXT("merge"), bMerge);
			if (Path.IsEmpty() || !FPaths::FileExists(Path)) return Result(false, TEXT("no such file"));
			const bool bOk = bMerge ? P->OpenScene(Path, true) : (FOKSceneCard::Kind(Path) == FOKSceneCard::EKind::KStudio ? P->ImportKStudio(Path) : P->LoadFile(Path));
			TSharedRef<FJsonObject> O = Result(bOk, bOk ? FString() : TEXT("not opened"));
			TArray<TSharedPtr<FJsonValue>> Report;
			for (const FString& L : G->LoadReport()) Report.Add(MakeShared<FJsonValueString>(L));
			O->SetArrayField(TEXT("report"), Report);
			return O;
		}
		if (Op == TEXT("undo")) { G->Undo(); return Result(true); }
		if (Op == TEXT("redo")) { G->Redo(); return Result(true); }
		if (Op == TEXT("cmd"))
		{
			FString Id, Arg;
			J->TryGetStringField(TEXT("id"), Id);
			J->TryGetStringField(TEXT("arg"), Arg);
			return P->RunCommand(Id, Arg) ? Result(true) : Result(false, TEXT("command failed or unknown: ") + Id);
		}
		if (Op == TEXT("timeline"))
		{
			bool bPlay = false;
			double D;
			if (J->TryGetBoolField(TEXT("play"), bPlay)) G->bTimelinePlaying = bPlay;
			if (J->TryGetNumberField(TEXT("time"), D)) P->ScrubTo(float(D));
			const TSharedPtr<FJsonObject>* Key = nullptr;
			if (J->TryGetObjectField(TEXT("key"), Key)) for (int32 Id : ReadIds((*Key).ToSharedRef(), TEXT("ids"))) Doc->SetKey(Id, G->TimelineTime);
			TSharedRef<FJsonObject> O = Result(true);
			O->SetNumberField(TEXT("time"), G->TimelineTime);
			O->SetNumberField(TEXT("duration"), Doc->Duration);
			O->SetBoolField(TEXT("playing"), G->bTimelinePlaying);
			return O;
		}
		if (TSharedPtr<FJsonObject> More = RunMore(*G, *P, Op, J)) return More.ToSharedRef();
		return Result(false, TEXT("unknown op: ") + Op);
	}
}

namespace OKStudioControl
{
	void Start()
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("Control")) || GRouter.IsValid()) return;
		int32 Port = 47890;
		FParse::Value(FCommandLine::Get(), TEXT("StudioControlPort="), Port);
		GRouter = FHttpServerModule::Get().GetHttpRouter(Port, true);
		if (!GRouter.IsValid()) { UE_LOG(LogTemp, Warning, TEXT("OK studio control: could not open port %d"), Port); return; }
		static const TCHAR* Ops[] = { TEXT("state"), TEXT("inspect"), TEXT("catalog_search"), TEXT("spawn"), TEXT("transform"), TEXT("reparent"), TEXT("delete"), TEXT("select"),
			TEXT("pose"), TEXT("camera"), TEXT("capture"), TEXT("save"), TEXT("load"), TEXT("undo"), TEXT("redo"), TEXT("cmd"), TEXT("timeline"),
			TEXT("patch"), TEXT("scene"), TEXT("bones"), TEXT("bounds"), TEXT("catalog"), TEXT("keys"), TEXT("duplicate"), TEXT("export"), TEXT("scenes"), TEXT("report"), TEXT("ui_shot"), TEXT("ui_fit"), TEXT("click"), TEXT("type"), TEXT("leave"), TEXT("exec"), TEXT("parts") };
		for (const TCHAR* Op : Ops)
		{
			const FString Name = Op;
			GRoutes.Add(GRouter->BindRoute(FHttpPath(TEXT("/studio/") + Name), EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_GET,
				FHttpRequestHandler::CreateLambda([Name](const FHttpServerRequest& Req, const FHttpResultCallback& Done)
				{
					Reply(Done, Run(Name, Body(Req)));
					return true;
				})));
		}
		FHttpServerModule::Get().StartAllListeners();
		UE_LOG(LogTemp, Display, TEXT("OK studio control: listening on 127.0.0.1:%d"), Port);
	}

	void Stop()
	{
		if (GRouter.IsValid()) for (const FHttpRouteHandle& Route : GRoutes) GRouter->UnbindRoute(Route);
		GRoutes.Reset();
		GRouter.Reset();
	}
}
