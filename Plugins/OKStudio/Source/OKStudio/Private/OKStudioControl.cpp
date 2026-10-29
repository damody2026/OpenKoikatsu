// OK Studio control channel (-Control): POST http://127.0.0.1:<StudioControlPort, 47890>/studio/<op> with a JSON body;
// the reply is {"ok": true, ...} or {"ok": false, "error": ...}. Tools/ok-game-mcp's studio_* tools speak it
// (Docs/OK_STUDIO_MCP.md). Edits wait while the user drags, a scene loads, the timeline plays or exports ("busy").
#include "SOKStudioPanel.h"
#include "OKStudioGameInternal.h"
#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"

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
		TArray<TSharedPtr<FJsonValue>> Objects;
		for (const FOKSceneObject& X : G.Document->Objects)
		{
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetNumberField(TEXT("id"), X.Id);
			J->SetNumberField(TEXT("parent"), X.Parent);
			J->SetStringField(TEXT("name"), X.Name);
			J->SetStringField(TEXT("kind"), KindName(X.Kind));
			J->SetBoolField(TEXT("visible"), X.bVisible);
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
		return O;
	}

	/** Runs one op on the studio (game thread). */
	TSharedRef<FJsonObject> Run(const FString& Op, const TSharedRef<FJsonObject>& J)
	{
		UOKStudioGame* G = UOKStudioGame::Get();
		SOKStudioPanel* P = G ? G->GetPanel() : nullptr;
		if (!G || !P) return Result(false, TEXT("no studio running"));
		static const TSet<FString> ReadOnly = { TEXT("state"), TEXT("inspect"), TEXT("catalog_search") };
		if (!ReadOnly.Contains(Op))
		{
			const FString Busy = G->BusyReason();
			// The timeline op itself may stop playback.
			if (!Busy.IsEmpty() && !(Op == TEXT("timeline") && Busy == TEXT("playing")))
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
					X->Character.Ik.Add({ E, Pos });
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
			const TSharedPtr<FJsonObject>* Sub = nullptr;
			if (J->TryGetObjectField(TEXT("expression"), Sub))
			{
				double D;
				if ((*Sub)->TryGetNumberField(TEXT("eyebrow"), D)) X->Character.EyebrowPattern = int32(D);
				if ((*Sub)->TryGetNumberField(TEXT("eyes"), D)) X->Character.EyesPattern = int32(D);
				if ((*Sub)->TryGetNumberField(TEXT("mouth"), D)) X->Character.MouthPattern = int32(D);
				if ((*Sub)->TryGetNumberField(TEXT("mouth_open"), D)) X->Character.MouthOpen = float(D);
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
					static const TMap<FString, EOKSceneLook> Names = { { TEXT("animation"), EOKSceneLook::Animation }, { TEXT("camera"), EOKSceneLook::Camera }, { TEXT("point"), EOKSceneLook::Point }, { TEXT("fixed"), EOKSceneLook::Fixed } };
					if (V->Type == EJson::Number) Out = EOKSceneLook(FMath::Clamp(int32(V->AsNumber()), 0, 3));
					else if (const EOKSceneLook* L = Names.Find(V->AsString().ToLower())) Out = *L;
				};
				Look((*Sub)->TryGetField(TEXT("eyes")), X->Character.EyesLook);
				Look((*Sub)->TryGetField(TEXT("neck")), X->Character.NeckLook);
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
			TEXT("pose"), TEXT("camera"), TEXT("capture"), TEXT("save"), TEXT("load"), TEXT("undo"), TEXT("redo"), TEXT("cmd"), TEXT("timeline") };
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
