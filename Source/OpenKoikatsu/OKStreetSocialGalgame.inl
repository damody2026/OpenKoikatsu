// Street conversations as galgame scenes (Docs/GALGAME_STREET.md): the bottom dialog box with the line typed out as
// its voice plays, the speaker's live portrait at the bottom left, and a shot per line. The ok-story live presenter
// (Tools/ok-story/src/live.rs) decides all of it; this only tells it what the street says and shows what it answers.
// Included by OKStreetSocialScene.cpp. ok.Street.Galgame 0 brings the old dialogue box back.
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/SceneCaptureComponent2D.h"
#include "OKStoryCapture.h"
#include "OKStoryLive.h"
#include "SOKStoryView.h"

static TAutoConsoleVariable<int32> CVarOKStreetGalgame(TEXT("ok.Street.Galgame"), 1,
	TEXT("Street conversations as galgame scenes (bottom dialog box, live portrait, a shot per line): 1 on, 0 the old dialogue box."));

struct FOKStreetGalgame
{
	FOKStoryLive Live;
	FOKStoryPortraitCapture Portrait;
	FOKStoryProbes Probes;
	TSharedPtr<SOKStoryView> View;
	TSharedPtr<SWidget> ViewHost;
	TSharedPtr<FJsonObject> Frame;
	bool bTried = false;
	bool bActive = false;
	int32 Partner = INDEX_NONE;
	// What was last sent, so only changes become lines.
	FString LastHer, LastPlayer, LastScripted, LastPlayerLine;
	int32 LastNode = INDEX_NONE;
	bool bHerPending = false;
	// When his scripted line went up: hers waits a moment so his can be read (his voice may be skipped).
	double PlayerSentAt = -1.0;
	// Who speaks the line on screen (character slot) and whether a voice plays it.
	int32 Speaker = INDEX_NONE;
	bool bVoiced = false;
	// Passers-by kept out of the shot (hidden from the main view only while they stand in it).
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Hidden;
};

// The main view shows everyone again.
static void OKStreetGalgameUnhide(UWorld* World, FOKStreetGalgame& G)
{
	if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
		PC->HiddenPrimitiveComponents.RemoveAll([&G](const TWeakObjectPtr<UPrimitiveComponent>& P) { return G.Hidden.Contains(P); });
	G.Hidden.Reset();
}

// The street's right panel (400 wide, 16 on each side): the scene's dialog box and framing leave its column free.
static constexpr float OKStreetGalgameRightColumn = 432.f;
// The scene's dialog box height, a share of the view's height (ok-story tuning `dialog_height`).
static constexpr float OKStreetGalgameDialogShare = .26f;

static FString OKStreetGalgameId(int32 Slot)
{
	return FString::Printf(TEXT("c%d"), Slot);
}

static FString OKStreetGalgameColor(int32 Slot)
{
	return TEXT("#") + OKStreetSpeakerColor(Slot).ToFColor(true).ToHex().Left(6);
}

static FString OKStreetGalgameJson(const TSharedRef<FJsonObject>& O)
{
	FString Json;
	FJsonSerializer::Serialize(O, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
	return Json;
}

// "Name：text" / "Name「text」" without its name and quotes.
static FString OKStreetGalgameWords(const FString& Line, const FString& Name)
{
	FString Text = Line;
	if (!Name.IsEmpty() && Text.StartsWith(Name)) Text = Text.Mid(Name.Len());
	Text.TrimStartInline();
	for (const TCHAR* Mark : { TEXT("："), TEXT(":") })
	{
		if (Text.StartsWith(Mark)) { Text = Text.Mid(FCString::Strlen(Mark)).TrimStart(); break; }
	}
	for (const TCHAR* Mark : { TEXT("「"), TEXT("“"), TEXT("\"") })
	{
		if (Text.StartsWith(Mark)) { Text = Text.Mid(FCString::Strlen(Mark)); break; }
	}
	for (const TCHAR* Mark : { TEXT("」"), TEXT("”"), TEXT("\"") })
	{
		if (Text.EndsWith(Mark)) { Text = Text.LeftChop(FCString::Strlen(Mark)); break; }
	}
	return Text.TrimStartAndEnd();
}

// "……" while she thinks of her answer: not a line (his words stay on screen until hers come).
static bool OKStreetGalgamePlaceholder(const FString& Words)
{
	for (const TCHAR C : Words)
	{
		if (C != TEXT('…') && C != TEXT('.') && C != TEXT('。') && C != TEXT('・') && !FChar::IsWhitespace(C)) return false;
	}
	return true;
}

bool AOKStreetSocialScene::GalgameShowsDialogue() const
{
	return Galgame.IsValid() && Galgame->bActive && CVarOKStreetGalgame.GetValueOnGameThread() != 0;
}

bool AOKStreetSocialScene::GalgameHidesDialogueBox() const
{
	if (!GalgameShowsDialogue()) return false;
	const auto& S = *State;
	// What the old box still shows during a scene: the dishes on the table and the words the microphone heard.
	// Heard words stay only until they are sent (then they are his line in the scene's box).
	const bool bHeard = S.FreeTalkOpen && S.FreeTalkLive && !S.FreeTalkHeard.IsEmpty() && !S.FreeTalkCaptionPlayer.Contains(S.FreeTalkHeard);
	return S.Table.IsEmpty() && !bHeard;
}

float AOKStreetSocialScene::GalgameDialogueLift() const
{
	if (!GalgameShowsDialogue() || !GEngine || !GEngine->GameViewport) return 0.f;
	FVector2D Size;
	GEngine->GameViewport->GetViewportSize(Size);
	const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(GEngine->GameViewport), .01f);
	// Above the scene's box (its slot already keeps 24 from the bottom).
	return FMath::Max(0.f, float(Size.Y) / Scale * OKStreetGalgameDialogShare - 16.f);
}

void AOKStreetSocialScene::EndGalgame()
{
	if (!Galgame) return;
	OKStreetGalgameUnhide(GetWorld(), *Galgame);
	if (Galgame->ViewHost && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Galgame->ViewHost.ToSharedRef());
	Galgame->Portrait.Release();
	Galgame->Probes.Release();
	Galgame->Live.Close();
	Galgame.Reset();
}

void AOKStreetSocialScene::TickGalgame(float Dt)
{
	auto& S = *State;
	if (CVarOKStreetGalgame.GetValueOnGameThread() == 0)
	{
		if (Galgame && Galgame->bActive) { Galgame->Live.Input(TEXT("{\"type\":\"end\"}")); Galgame->bActive = false; }
		if (Galgame && Galgame->View) Galgame->View->SetVisibility(EVisibility::Collapsed);
		return;
	}
	if (!Galgame) Galgame = MakeShared<FOKStreetGalgame>();
	FOKStreetGalgame& G = *Galgame;
	if (!G.Live.IsOpen())
	{
		if (G.bTried) return;
		G.bTried = true;
		FString Error;
		if (!G.Live.Open(Error)) { UE_LOG(LogTemp, Warning, TEXT("Street galgame presenter unavailable, the old dialogue box stays: %s"), *Error); return; }
		UE_LOG(LogTemp, Display, TEXT("Street galgame presenter: %s"), *G.Live.DllPath());
	}
	if (!G.View && GEngine && GEngine->GameViewport)
	{
		FOKStoryViewSource Source;
		const TWeakPtr<FOKStreetGalgame> Weak(Galgame);
		Source.Frame = [Weak] { const auto P = Weak.Pin(); return P ? P->Frame : nullptr; };
		Source.Portrait = [Weak] { const auto P = Weak.Pin(); return P ? P->Portrait.Material() : nullptr; };
		G.View = SNew(SOKStoryView).Source(Source);
		// It only shows: clicks and keys stay the street's. The right panel keeps its column.
		G.View->SetVisibility(EVisibility::HitTestInvisible);
		G.ViewHost = SNew(SBox).Padding(FMargin(0.f, 0.f, OKStreetGalgameRightColumn, 0.f)).Visibility(EVisibility::HitTestInvisible)[G.View.ToSharedRef()];
		GEngine->GameViewport->AddViewportWidgetContent(G.ViewHost.ToSharedRef(), 9);
	}
	const TArray<FString> Names = OKStreetCharacterNames(S);
	auto NameOf = [&](int32 Slot) { return Names.IsValidIndex(Slot) ? Names[Slot] : FString(); };
	auto SendLine = [&](int32 Slot, const FString& Text, bool bVoiced)
	{
		TSharedRef<FJsonObject> L = MakeShared<FJsonObject>();
		L->SetStringField(TEXT("type"), TEXT("line"));
		L->SetStringField(TEXT("who"), OKStreetGalgameId(Slot));
		L->SetStringField(TEXT("name"), NameOf(Slot));
		L->SetStringField(TEXT("color"), OKStreetGalgameColor(Slot));
		// The box draws its own quotes.
		L->SetStringField(TEXT("text"), OKStreetGalgameWords(Text, FString()));
		L->SetBoolField(TEXT("voiced"), bVoiced);
		G.Live.Input(OKStreetGalgameJson(L));
		UE_LOG(LogTemp, Display, TEXT("Street galgame line c%d%s: %s"), Slot, bVoiced ? TEXT(" (voiced)") : TEXT(""), *Text);
		G.Speaker = Slot;
		G.bVoiced = bVoiced;
	};

	// The conversation: who he talks with (free talk follows the walk companion, the date, the pair partner too).
	const int32 Partner = FreeTalkPartner();
	const bool bTalking = Partner != INDEX_NONE && (S.Target != INDEX_NONE || S.FreeTalkOpen);
	if (bTalking && (!G.bActive || Partner != G.Partner))
	{
		TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("type"), TEXT("conversation"));
		C->SetStringField(TEXT("player"), OKStreetGalgameId(0));
		C->SetArrayField(TEXT("people"), { MakeShared<FJsonValueString>(OKStreetGalgameId(Partner)) });
		G.Live.Input(OKStreetGalgameJson(C));
		G.bActive = true;
		G.Partner = Partner;
		G.LastHer = S.FreeTalkCaptionHer;
		G.LastPlayer = S.FreeTalkCaptionPlayer;
		G.LastScripted.Reset();
		G.LastPlayerLine.Reset();
		G.LastNode = INDEX_NONE;
		G.bHerPending = false;
	}
	else if (!bTalking && G.bActive)
	{
		G.Live.Input(TEXT("{\"type\":\"end\"}"));
		G.bActive = false;
		G.Partner = INDEX_NONE;
	}

	if (G.bActive)
	{
		// A scripted line: his line first (while his voice plays), hers once her voice starts (or at once without one).
		FString Said, Hint;
		OKStreetSplitMessageHint(S.Message, Said, Hint);
		if (S.Node != INDEX_NONE && (S.Node != G.LastNode || Said != G.LastScripted))
		{
			G.LastNode = S.Node;
			G.LastScripted = Said;
			G.bHerPending = !Said.IsEmpty();
			if (!S.PlayerLine.IsEmpty() && S.PlayerLine != G.LastPlayerLine)
			{
				G.LastPlayerLine = S.PlayerLine;
				SendLine(0, S.PlayerLine, S.MalePhase == 1 || S.MalePhase == 2);
				G.PlayerSentAt = FPlatformTime::Seconds();
			}
		}
		if (G.bHerPending && FPlatformTime::Seconds() - G.PlayerSentAt >= 1.2 && (S.ClipStarted || S.VoiceIds.IsEmpty() || S.PlayerLine.IsEmpty() || S.MalePhase == 3))
		{
			G.bHerPending = false;
			SendLine(S.Target != INDEX_NONE ? S.Target : Partner, G.LastScripted, !S.VoiceIds.IsEmpty());
		}
		if (S.Node == INDEX_NONE) G.LastNode = INDEX_NONE;
		// Free talk: his words, then her (newest) line as it arrives.
		if (S.FreeTalkCaptionPlayer != G.LastPlayer)
		{
			G.LastPlayer = S.FreeTalkCaptionPlayer;
			const FString Words = OKStreetGalgameWords(S.FreeTalkCaptionPlayer, NameOf(0));
			if (!Words.IsEmpty()) SendLine(0, Words, false);
		}
		if (S.FreeTalkCaptionHer != G.LastHer)
		{
			G.LastHer = S.FreeTalkCaptionHer;
			TArray<FString> Lines;
			S.FreeTalkCaptionHer.ParseIntoArrayLines(Lines);
			if (Lines.Num())
			{
				const FString& Last = Lines.Last();
				int32 Slot = OKStreetLineSpeaker(Last, Names);
				if (Slot == INDEX_NONE) Slot = Partner;
				const FString Words = OKStreetGalgameWords(Last, NameOf(Slot));
				if (!OKStreetGalgamePlaceholder(Words)) SendLine(Slot, Words, S.TtsEnabled && Slot != 0);
			}
		}
	}

	// Facts: the people of the conversation (root and bones), the view, the speaker's voice, probe answers.
	TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
	F->SetNumberField(TEXT("dt"), Dt);
	FVector2D Size(1920, 1080);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Size);
	// Where the dialog box and portrait are drawn (the right panel keeps its column), and the whole picture the
	// street camera's angle spans (the shots are framed for it).
	const float UiScale = GEngine && GEngine->GameViewport ? UWidgetLayoutLibrary::GetViewportScale(GEngine->GameViewport) : 1.f;
	F->SetArrayField(TEXT("viewport"), { MakeShared<FJsonValueNumber>(FMath::Max(1.0, Size.X - OKStreetGalgameRightColumn * UiScale)), MakeShared<FJsonValueNumber>(Size.Y) });
	if (Size.Y > 0) F->SetNumberField(TEXT("view_aspect"), Size.X / Size.Y);
	auto Vec = [](const FVector& V) { return TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) }; };
	auto Quat = [](const FQuat& Q) { return TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(Q.X), MakeShared<FJsonValueNumber>(Q.Y), MakeShared<FJsonValueNumber>(Q.Z), MakeShared<FJsonValueNumber>(Q.W) }; };
	if (S.Camera.IsValid())
	{
		TSharedRef<FJsonObject> V = MakeShared<FJsonObject>();
		V->SetArrayField(TEXT("pos"), Vec(S.Camera->GetActorLocation()));
		V->SetArrayField(TEXT("rot"), Quat(S.Camera->GetActorQuat()));
		F->SetObjectField(TEXT("view"), V);
		F->SetNumberField(TEXT("view_fov"), S.Camera->GetCameraComponent()->FieldOfView);
	}
	TSharedRef<FJsonObject> Actors = MakeShared<FJsonObject>();
	TArray<int32> People;
	if (G.bActive) { People.Add(0); People.Add(G.Partner); }
	for (const int32 Slot : People)
	{
		FOKRuntimeCharacterStatus Status;
		if (!OKGetCrowdRuntimeCharacterStatus(this, Slot, Status)) continue;
		TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetBoolField(TEXT("ready"), Status.Ready && Status.Visible);
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetArrayField(TEXT("pos"), Vec(Status.Transform.GetLocation()));
		R->SetArrayField(TEXT("rot"), Quat(Status.Transform.GetRotation()));
		R->SetNumberField(TEXT("scale"), Status.Transform.GetScale3D().Z);
		A->SetObjectField(TEXT("root"), R);
		TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
		static const TCHAR* Bones[][2] = { { TEXT("head"), TEXT("j_head") }, { TEXT("spine03"), TEXT("j_spine03") }, { TEXT("spine02"), TEXT("j_spine02") }, { TEXT("hips"), TEXT("j_hips") } };
		for (const auto& Bone : Bones)
		{
			FTransform W;
			// Her bones are cf_*, his cm_*.
			if (!OKGetCrowdRuntimeBoneTransform(this, Slot, FName(FString(TEXT("cf_")) + Bone[1]), W) && !OKGetCrowdRuntimeBoneTransform(this, Slot, FName(FString(TEXT("cm_")) + Bone[1]), W)) continue;
			TSharedRef<FJsonObject> X = MakeShared<FJsonObject>();
			X->SetArrayField(TEXT("pos"), Vec(W.GetLocation()));
			X->SetArrayField(TEXT("rot"), Quat(W.GetRotation()));
			B->SetObjectField(Bone[0], X);
		}
		A->SetObjectField(TEXT("bones"), B);
		Actors->SetObjectField(OKStreetGalgameId(Slot), A);
	}
	F->SetObjectField(TEXT("actors"), Actors);
	if (G.bVoiced && G.Speaker != INDEX_NONE)
	{
		double Played = 0, Total = -1;
		const bool bPlaying = OKGetCrowdRuntimeSpeechPlayback(this, G.Speaker, Played, Total);
		// A scripted line of several clips: the clips played so far count too.
		const bool bScripted = G.Speaker != 0 && S.Node != INDEX_NONE && S.VoiceIds.Num() > 1;
		double Progress = bPlaying && Total > 0 ? FMath::Clamp(Played / Total, 0.0, 1.0) : -1.0;
		if (bScripted && Progress >= 0) Progress = (FMath::Clamp(S.Voice, 0, S.VoiceIds.Num()) + Progress) / S.VoiceIds.Num();
		TSharedRef<FJsonObject> V = MakeShared<FJsonObject>();
		V->SetNumberField(TEXT("id"), 0);
		V->SetBoolField(TEXT("playing"), bPlaying);
		V->SetNumberField(TEXT("progress"), Progress);
		V->SetBoolField(TEXT("done"), false);
		F->SetObjectField(TEXT("voice"), V);
	}
	F->SetArrayField(TEXT("probes"), G.Probes.TakeAnswers());

	FString Error;
	G.Frame = G.Live.Tick(OKStreetGalgameJson(F), Error);
	if (!G.Frame) { UE_LOG(LogTemp, Warning, TEXT("Street galgame presenter tick failed: %s"), *Error); return; }
	if (G.View) G.View->SetVisibility(G.bActive ? EVisibility::HitTestInvisible : EVisibility::Collapsed);

	// The shot: only while they stand talking (walking together keeps the street camera), never over a photo, and not
	// while she sings (SongCameraShot has the camera then).
	const TSharedPtr<FJsonObject>* Camera = nullptr;
	if (G.bActive && S.Target != INDEX_NONE && !SongOwnsVoice(S.Target) && S.Camera.IsValid() && SceneTime() >= S.PhotoViewUntil && G.Frame->TryGetObjectField(TEXT("camera"), Camera))
	{
		const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* R = nullptr;
		if ((*Camera)->TryGetArrayField(TEXT("pos"), P) && P->Num() == 3 && (*Camera)->TryGetArrayField(TEXT("rot"), R) && R->Num() == 4)
		{
			const FVector Pos((*P)[0]->AsNumber(), (*P)[1]->AsNumber(), (*P)[2]->AsNumber());
			const FQuat Rot = FQuat((*R)[0]->AsNumber(), (*R)[1]->AsNumber(), (*R)[2]->AsNumber(), (*R)[3]->AsNumber()).GetNormalized();
			S.Camera->SetActorLocationAndRotation(Pos, Rot);
			S.Camera->GetCameraComponent()->SetFieldOfView(float((*Camera)->GetNumberField(TEXT("fov"))));
			// Someone walking between the lens and the two of them is left out of the picture until they are past.
			FOKRuntimeCharacterStatus A, B;
			if (OKGetCrowdRuntimeCharacterStatus(this, 0, A) && OKGetCrowdRuntimeCharacterStatus(this, G.Partner, B))
			{
				const FVector Target = (A.Transform.GetLocation() + B.Transform.GetLocation()) * .5 + FVector(0, 0, 120);
				TArray<UPrimitiveComponent*> Block;
				for (int32 Slot = 1; Slot < S.Manifest.Characters.Num(); ++Slot)
				{
					FOKRuntimeCharacterStatus O;
					if (Slot == G.Partner || !OKGetCrowdRuntimeCharacterStatus(this, Slot, O)) continue;
					const FVector Body = O.Transform.GetLocation() + FVector(0, 0, 90);
					const FVector Closest = FMath::ClosestPointOnSegment(Body, Pos, Target);
					if (FVector::DistXY(Body, Closest) < 80.f && FVector::Dist(Pos, Closest) < FVector::Dist(Pos, Target) - 30.f)
						OKGetCrowdRuntimeCharacterComponents(this, Slot, Block);
				}
				OKStreetGalgameUnhide(GetWorld(), G);
				if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
					for (UPrimitiveComponent* Prim : Block) { PC->HiddenPrimitiveComponents.AddUnique(Prim); G.Hidden.Add(Prim); }
			}
		}
	}
	else if (G.Hidden.Num()) OKStreetGalgameUnhide(GetWorld(), G);
	else if (S.Camera.IsValid() && S.Camera->GetCameraComponent()->FieldOfView != 65.f && !G.bActive)
	{
		// Back to the street's own lens.
		S.Camera->GetCameraComponent()->SetFieldOfView(65.f);
	}

	// The portrait: her (or his) own components only.
	const TSharedPtr<FJsonObject>* Portrait = nullptr;
	if (G.bActive && G.Frame->TryGetObjectField(TEXT("portrait"), Portrait) && (*Portrait)->GetBoolField(TEXT("visible")))
	{
		FString Who;
		(*Portrait)->TryGetStringField(TEXT("who"), Who);
		const int32 Slot = Who.StartsWith(TEXT("c")) ? FCString::Atoi(*Who.Mid(1)) : INDEX_NONE;
		TArray<UPrimitiveComponent*> Shown;
		OKGetCrowdRuntimeCharacterComponents(this, Slot, Shown);
		if (Shown.Num())
		{
			const FPostProcessSettings* Post = S.Camera.IsValid() ? &S.Camera->GetCameraComponent()->PostProcessSettings : nullptr;
			G.Portrait.Update(this, GetWorld(), *Portrait, [&Shown](USceneCaptureComponent2D* C)
			{
				C->ShowOnlyComponents.Reset();
				for (UPrimitiveComponent* P : Shown) C->ShowOnlyComponents.Add(P);
			}, Post);
		}
	}

	// Lines of sight for the shots: the two of them never block it, the street does.
	// Every frame, with no new probes too: queued ones render a few a frame and their answers arrive later.
	const TArray<TSharedPtr<FJsonValue>>* Probes = nullptr;
	const TArray<TSharedPtr<FJsonValue>> NoProbes;
	{
		TArray<UPrimitiveComponent*> Hidden;
		for (const int32 Slot : People) OKGetCrowdRuntimeCharacterComponents(this, Slot, Hidden);
		G.Probes.Run(this, GetWorld(), G.Frame->TryGetArrayField(TEXT("probes"), Probes) ? *Probes : NoProbes, [&Hidden](USceneCaptureComponent2D* C)
		{
			for (UPrimitiveComponent* P : Hidden) C->HiddenComponents.Add(P);
		});
	}
}
