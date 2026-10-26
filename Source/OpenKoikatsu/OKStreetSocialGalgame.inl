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
static TAutoConsoleVariable<int32> CVarOKStreetGalgamePortraitOutline(TEXT("ok.Street.GalgamePortraitOutline"), 0,
	TEXT("The portrait's outermost line (her outline shells around the silhouette): 0 cut off (its coverage is captured without them, a view of its own), 1 kept (the coverage drawn with the main view)."));

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
	// A replayed line (history replay or one line from the history window) last sent, and whether the scene was opened
	// only to show the replay (no conversation going on).
	int64 ReplayShown = 0;
	bool bReplayScene = false;
	// A replay was on screen last frame: once it ends (or is stopped) the box shows the conversation's own last line again.
	bool bWasReplaying = false;
	// The words of the line on screen (a restore of the same line after a replay is not typed again).
	FString OnScreen;
	// Who speaks the line on screen (character slot) and whether a voice plays it.
	int32 Speaker = INDEX_NONE;
	bool bVoiced = false;
	// A group reply (several girls in turn): its lines, the next one to show, and the run on screen (speech line indices
	// [RunStart, RunEnd) of one speaker); shown as each girl's voice starts, or by reading time without voices.
	struct FGroupLine { int32 Slot = INDEX_NONE; FString Words; };
	TArray<FGroupLine> Group;
	int32 GroupNext = 0, RunStart = 0, RunEnd = 0;
	double GroupNextAt = 0.0;
	bool bGroupVoiced = false;
	// Passers-by kept out of the shot (hidden from the main view only while they stand in it).
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Hidden;
	// When a broken far-off shot was last written to the log (one line per two seconds).
	double FarShotLoggedAt = -1000.0;
	// Rollback: the lines of this conversation as shown (slot, words), the one the wheel scrolled back to (INDEX_NONE:
	// the newest), and a point to return to taken before each of his lines (GalgameCheckpoint).
	struct FLine { int32 Slot = INDEX_NONE; FString Words; };
	TArray<FLine> Lines;
	int32 ViewLine = INDEX_NONE;
	bool bNoRecord = false;
	struct FCheckpoint
	{
		int32 Lines = 0;
		FString Id;
		int64 LogSerial = 0;
		int64 HistorySerial = 0;
		TArray<FOKFreeTalkTurn> Memory;
		bool bRelation = false;
		FOKStreetRelation Relation;
		bool bMind = false;
		int64 MindMessages = 0, MindCompactions = 0, MindMemoryNext = 0;
	};
	TArray<FCheckpoint> Checkpoints;
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
	// The model's "⋯" (U+22EF, midline) is not in the box's font: it showed as two boxes. The usual "…" is.
	Text.ReplaceInline(TEXT("⋯"), TEXT("…"));
	// Its "〜" (U+301C, the wave dash of a drawn-out "好呀〜") is drawn as an arc there; the full-width tilde reads right.
	// (Checked: the rest of the usual marks, 『』《》〈〉【】‥︙―﹏♪☆♡❤※ and the like, are in the font.)
	Text.ReplaceInline(TEXT("〜"), TEXT("～"));
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

bool AOKStreetSocialScene::GalgameReplays() const
{
	return CVarOKStreetGalgame.GetValueOnGameThread() != 0 && Galgame.IsValid() && Galgame->Live.IsOpen();
}

// The line on screen is skipped (a click off her, Enter, Space): its text is shown whole anyway, so skipping means its
// voice ends now and the conversation goes on. A replayed line: the next one of the replay. Her free-talk reply: the
// rest of it in the same voice (the segments of this speaker in a row), as when he talks over her. A scripted line:
// it ends and counts as said. His own voiced line: it stops.
// Her side is still speaking: a line playing (in a group, any girl's voice), lines still coming, a replay, or the group's
// lines not all shown yet. Nothing new is asked of them meanwhile (a till note, a silence nudge): its answer would
// replace what they have not finished saying.
bool AOKStreetSocialScene::FreeTalkHerVoiceBusy() const
{
	if (!State) return false;
	const auto& S = *State;
	if (S.FreeTalkSpeechPlayed < S.FreeTalkSpeechLines.Num() || !S.FreeTalkReplay.IsEmpty() || !S.FreeTalkReplayParts.IsEmpty()) return true;
	const int32 Partner = FreeTalkPartner();
	AOKStreetSocialScene* Self = const_cast<AOKStreetSocialScene*>(this); // the speech lookup only reads
	if (Partner != INDEX_NONE && OKCrowdRuntimeSpeechActive(Self, Partner)) return true;
	for (const int32 Who : S.FreeTalkSpeechSpeakers)
		if (Who > 0 && Who != Partner && OKCrowdRuntimeSpeechActive(Self, Who)) return true;
	return Galgame.IsValid() && Galgame->GroupNext < Galgame->Group.Num();
}

void AOKStreetSocialScene::GalgameDropGroupLines()
{
	if (Galgame.IsValid()) Galgame->GroupNext = Galgame->Group.Num();
}

void AOKStreetSocialScene::SkipGalgameLine()
{
	if (!GalgameShowsDialogue() || !State) return;
	auto& S = *State;
	FString Ignore;
	if (!S.FreeTalkReplay.IsEmpty() || !S.FreeTalkReplayParts.IsEmpty() || SceneTime() < S.FreeTalkReplayHold ||
		(S.FreeTalkReplaySpeaker != INDEX_NONE && OKCrowdRuntimeSpeechActive(this, S.FreeTalkReplaySpeaker)))
	{
		if (S.FreeTalkReplaySpeaker != INDEX_NONE) OKStopCrowdRuntimeSpeech(this, S.FreeTalkReplaySpeaker, Ignore);
		S.FreeTalkReplayParts.Reset();
		S.FreeTalkReplayHold = 0;
		UE_LOG(LogTemp, Display, TEXT("Street galgame: skipped a replayed line (%d left)"), S.FreeTalkReplay.Num());
		return;
	}
	const int32 Speaker = Galgame->Speaker;
	if (Speaker == 0 && (S.MalePhase == 1 || S.MalePhase == 2))
	{
		OKStopCrowdRuntimeSpeech(this, 0, Ignore);
		S.MalePhase = 3;
		UE_LOG(LogTemp, Display, TEXT("Street galgame: skipped his line"));
		return;
	}
	if (Speaker == INDEX_NONE || Speaker == 0) return;
	if (SongOwnsVoice(Speaker)) return; // her song is a performance, not a line
	Galgame->GroupNextAt = 0.0; // a group reply without voices: the next girl's lines come up now
	OKStopCrowdRuntimeSpeech(this, Speaker, Ignore);
	int32 Dropped = 0;
	while (S.FreeTalkSpeechPlayed < S.FreeTalkSpeechLines.Num())
	{
		const int32 Who = S.FreeTalkSpeechSpeakers.IsValidIndex(S.FreeTalkSpeechPlayed) ? S.FreeTalkSpeechSpeakers[S.FreeTalkSpeechPlayed] : S.FreeTalkSpeechCharacter;
		if (Who != Speaker) break;
		if (S.FreeTalkSpeechStreams.IsValidIndex(S.FreeTalkSpeechPlayed) && S.FreeTalkSpeechStreams[S.FreeTalkSpeechPlayed]) S.FreeTalkSpeechStreams[S.FreeTalkSpeechPlayed]->Finish();
		++S.FreeTalkSpeechPlayed;
		++Dropped;
	}
	if (S.Node != INDEX_NONE) S.LineInterrupted = true; // a scripted line ends here and counts as said (FinishLine)
	UE_LOG(LogTemp, Display, TEXT("Street galgame: skipped the line of %d (%d voice segments dropped)"), Speaker, Dropped);
}

// Before each of his lines to her: the point a rollback returns to (the lines shown so far, her free-talk log and
// memory, her history, her relation, her mind's context and memory as they are now).
void AOKStreetSocialScene::GalgameCheckpoint(int32 Character, const FString& Id)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_GalgameCheckpoint);
	if (!Galgame.IsValid() || !Galgame->bActive || !State) return;
	auto& S = *State;
	FOKStreetGalgame::FCheckpoint P;
	P.Lines = Galgame->Lines.Num();
	P.Id = Id;
	P.LogSerial = S.FreeTalkSerial;
	{ const auto& List = HistoryOf(Id); P.HistorySerial = List.IsEmpty() ? 0 : List.Last().Serial; }
	if (const auto* Memory = S.FreeTalkMemory.Find(Id)) P.Memory = *Memory;
	if (const auto* R = S.Store.Relations.FindByPredicate([&Id](const FOKStreetRelation& X) { return X.Id == Id; })) { P.bRelation = true; P.Relation = *R; }
	if (S.Mind)
	{
		FString Out, Error;
		TSharedPtr<FJsonObject> J;
		if (S.Mind->Query(FString::Printf(TEXT("{\"kind\":\"checkpoint\",\"heroine\":\"%s\"}"), *Id.ReplaceCharWithEscapedChar()), Out, Error) &&
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Out), J) && J.IsValid())
		{
			P.bMind = true;
			P.MindMessages = int64(J->GetNumberField(TEXT("messages")));
			P.MindCompactions = int64(J->GetNumberField(TEXT("compactions")));
			P.MindMemoryNext = int64(J->GetNumberField(TEXT("memory_next")));
		}
	}
	Galgame->Checkpoints.Add(MoveTemp(P));
}

// He says something while the box shows an earlier line (the wheel scrolled back): everything after that line is
// undone, as if the conversation had stopped there. His own line on screen is undone too (he says it again, differently).
void AOKStreetSocialScene::GalgameRollBack(int32 Character, const FString& Id)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_GalgameRollBack);
	if (!Galgame.IsValid() || !State) return;
	auto& G = *Galgame;
	if (G.ViewLine == INDEX_NONE) return;
	auto& S = *State;
	const int32 Keep = G.ViewLine + 1;
	G.ViewLine = INDEX_NONE;
	int32 Pick = INDEX_NONE;
	for (int32 I = 0; I < G.Checkpoints.Num(); ++I) if (G.Checkpoints[I].Lines <= Keep && G.Checkpoints[I].Id == Id) Pick = I;
	if (Pick == INDEX_NONE) { UE_LOG(LogTemp, Display, TEXT("Street galgame rollback: nothing to undo before line %d"), Keep); return; }
	const FOKStreetGalgame::FCheckpoint P = G.Checkpoints[Pick];
	const int32 Log = S.FreeTalkLog.RemoveAll([&P](const FOKFreeTalkLine& L) { return L.Serial > P.LogSerial; });
	if (Log) S.FreeTalkLogDirty = true;
	S.FreeTalkMemory.FindOrAdd(Id) = P.Memory;
	FreeTalkSaveMemory();
	auto& List = HistoryOf(Id);
	const int32 History = List.RemoveAll([&P](const FOKStreetHistoryEntry& E) { return E.Serial > P.HistorySerial; });
	if (History)
	{
		const FString Path = HistoryPath(Id);
		OKStreetFlushSaves(); // appends still queued would land after the rewrite
		FString Out;
		for (const auto& E : List) Out += OKStreetHistoryLine(E) + TEXT("\n");
		if (!Path.IsEmpty()) FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	if (P.bRelation)
	{
		FOKStreetStore Next = S.Store;
		if (auto* R = Next.Relations.FindByPredicate([&Id](const FOKStreetRelation& X) { return X.Id == Id; })) *R = P.Relation;
		CommitStore(MoveTemp(Next));
	}
	FString MindResult;
	if (P.bMind && S.Mind)
	{
		FString Error;
		S.Mind->Query(FString::Printf(TEXT("{\"kind\":\"rollback\",\"heroine\":\"%s\",\"messages\":%lld,\"compactions\":%lld,\"memory_next\":%lld}"),
			*Id.ReplaceCharWithEscapedChar(), P.MindMessages, P.MindCompactions, P.MindMemoryNext), MindResult, Error);
		if (MindResult.IsEmpty()) MindResult = Error;
	}
	G.Lines.SetNum(FMath::Min(G.Lines.Num(), P.Lines));
	G.Checkpoints.SetNum(Pick);
	UE_LOG(LogTemp, Display, TEXT("Street galgame rollback: back to line %d (kept %d lines): log -%d, history -%d, favor %.1f, mind %s"),
		Keep, P.Lines, Log, History, P.bRelation ? P.Relation.Favor : -1.f, *MindResult);
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
	// The one-on-one scene is a world of its own: the street's dialog box (its last line) stayed over it.
	if (G.ViewHost) G.ViewHost->SetVisibility(S.PrivateGirl != INDEX_NONE ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	const TArray<FString> Names = OKStreetCharacterNames(S);
	auto NameOf = [&](int32 Slot) { return Names.IsValidIndex(Slot) ? Names[Slot] : FString(); };
	auto SendLine = [&](int32 Slot, const FString& Text, bool bVoiced)
	{
		TSharedRef<FJsonObject> L = MakeShared<FJsonObject>();
		L->SetStringField(TEXT("type"), TEXT("line"));
		L->SetStringField(TEXT("who"), OKStreetGalgameId(Slot));
		// In the game's script (a card named in Simplified showed 「贞德」 over a Traditional line).
		L->SetStringField(TEXT("name"), OKStreetScriptedName(NameOf(Slot)));
		L->SetStringField(TEXT("color"), OKStreetGalgameColor(Slot));
		// The box draws its own quotes.
		L->SetStringField(TEXT("text"), OKStreetGalgameWords(Text, FString()));
		L->SetBoolField(TEXT("voiced"), bVoiced);
		G.Live.Input(OKStreetGalgameJson(L));
		UE_LOG(LogTemp, Display, TEXT("Street galgame line c%d%s: %s"), Slot, bVoiced ? TEXT(" (voiced)") : TEXT(""), *Text);
		G.Speaker = Slot;
		G.bVoiced = bVoiced;
		G.OnScreen = L->GetStringField(TEXT("text"));
		// A line of the conversation itself (not a replay, not one scrolled back to): kept for the rollback, and the box
		// is at the newest line again.
		if (!G.bNoRecord && (G.Lines.IsEmpty() || G.Lines.Last().Slot != Slot || G.Lines.Last().Words != Text))
		{
			G.Lines.Add({ Slot, Text });
			G.ViewLine = INDEX_NONE;
		}
	};

	// The conversation: who he talks with (free talk follows the walk companion, the date, the pair partner too).
	const int32 Partner = FreeTalkPartner();
	// Not while she does a one-person activity beside him (watching the fish, her explosion spell): that is watched with
	// its own camera, and speaking to her ends it anyway. The dialog shot held the whole spell off screen (R3-119).
	const bool bTalking = Partner != INDEX_NONE && (S.Target != INDEX_NONE || S.FreeTalkOpen) && S.SoloGirl == INDEX_NONE;
	// History lines played back (the replay button, a line's play button in the history window) are shown as scene
	// lines too, with her portrait; without a conversation the scene opens for the replay alone and closes after it.
	const bool bReplaying = !S.FreeTalkReplay.IsEmpty() || !S.FreeTalkReplayParts.IsEmpty() ||
		(S.FreeTalkReplaySpeaker != INDEX_NONE && OKCrowdRuntimeSpeechActive(this, S.FreeTalkReplaySpeaker)) || SceneTime() < S.FreeTalkReplayHold;
	const int32 ReplaySlot = S.FreeTalkReplaySceneSlot;
	const bool bReplayScene = !bTalking && bReplaying && ReplaySlot != INDEX_NONE && S.SoloGirl == INDEX_NONE;
	const int32 SceneWith = bTalking ? Partner : bReplayScene ? ReplaySlot : INDEX_NONE;
	if (SceneWith != INDEX_NONE && (!G.bActive || SceneWith != G.Partner))
	{
		G.bReplayScene = !bTalking;
		TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("type"), TEXT("conversation"));
		C->SetStringField(TEXT("player"), OKStreetGalgameId(0));
		C->SetArrayField(TEXT("people"), { MakeShared<FJsonValueString>(OKStreetGalgameId(SceneWith)) });
		G.Live.Input(OKStreetGalgameJson(C));
		G.bActive = true;
		G.Partner = SceneWith;
		G.LastHer = S.FreeTalkCaptionHer;
		G.LastPlayer = S.FreeTalkCaptionPlayer;
		G.LastScripted.Reset();
		G.LastPlayerLine.Reset();
		G.LastNode = INDEX_NONE;
		G.bHerPending = false;
		G.Lines.Reset();
		G.Checkpoints.Reset();
		G.ViewLine = INDEX_NONE;
		G.Group.Reset();G.GroupNext = G.RunStart = G.RunEnd = 0;
	}
	else if (SceneWith == INDEX_NONE && G.bActive)
	{
		G.bReplayScene = false;
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
		// A history line played back: her words as a scene line while her voice replays.
		if (S.FreeTalkReplayShown != G.ReplayShown)
		{
			G.ReplayShown = S.FreeTalkReplayShown;
			if (bReplaying && S.FreeTalkReplayShownSlot != INDEX_NONE)
			{
				const int32 Slot = S.FreeTalkReplayShownSlot;
				const FString Words = OKStreetGalgameWords(S.FreeTalkReplayShownText, NameOf(Slot));
				G.bNoRecord = true;
				if (!OKStreetGalgamePlaceholder(Words)) SendLine(Slot, Words, S.FreeTalkReplayShownVoiced);
				G.bNoRecord = false;
			}
		}
		// Rollback: the wheel over the scene goes back through this conversation's lines and forward to the newest. The
		// voice of the line on screen stops; what she remembers goes back only once he says something there.
		if (!bReplaying && G.Lines.Num() > 1)
		{
			APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
			const int32 Step = PC ? (OKKeyPressed(PC, EKeys::MouseScrollUp) ? -1 : OKKeyPressed(PC, EKeys::MouseScrollDown) ? 1 : 0) : 0;
			const int32 From = G.ViewLine == INDEX_NONE ? G.Lines.Num() - 1 : G.ViewLine;
			const int32 To = FMath::Clamp(From + Step, 0, G.Lines.Num() - 1);
			if (Step != 0 && To != From)
			{
				SkipGalgameLine();
				G.ViewLine = To == G.Lines.Num() - 1 ? INDEX_NONE : To;
				G.bNoRecord = true;
				SendLine(G.Lines[To].Slot, G.Lines[To].Words, false);
				G.bNoRecord = false;
				UE_LOG(LogTemp, Display, TEXT("Street galgame rollback: line %d of %d"), To + 1, G.Lines.Num());
			}
		}
		// The replay is over while they still talk: back to her latest line of this conversation.
		if (G.bWasReplaying && !bReplaying && !G.bReplayScene)
		{
			TArray<FString> Lines;
			S.FreeTalkCaptionHer.ParseIntoArrayLines(Lines);
			if (Lines.Num())
			{
				int32 Slot = OKStreetLineSpeaker(Lines.Last(), Names);
				if (Slot == INDEX_NONE) Slot = G.Partner;
				const FString Words = OKStreetGalgameWords(Lines.Last(), NameOf(Slot));
				if (!OKStreetGalgamePlaceholder(Words) && OKStreetGalgameWords(Words, FString()) != G.OnScreen) SendLine(Slot, Words, false);
			}
		}
		// Free talk: his words, then her (newest) line as it arrives.
		if (S.FreeTalkCaptionPlayer != G.LastPlayer)
		{
			G.LastPlayer = S.FreeTalkCaptionPlayer;
			const FString Words = OKStreetGalgameWords(S.FreeTalkCaptionPlayer, NameOf(0));
			if (!Words.IsEmpty()) SendLine(0, Words, false);
		}
		// Her line comes up once he has said his (his voice in TTS mode).
		if (S.FreeTalkCaptionHer != G.LastHer && !FreeTalkHisVoiceBusy())
		{
			G.LastHer = S.FreeTalkCaptionHer;
			TArray<FString> Lines;
			S.FreeTalkCaptionHer.ParseIntoArrayLines(Lines);
			// Several girls answering in turn (a group talk): not the last line at once (Megumin's last word over Yuuka's
			// voice, the lines between never shown) but each girl's lines in order as she speaks them (below).
			G.Group.Reset();G.GroupNext = 0;G.RunStart = G.RunEnd = 0;G.GroupNextAt = FPlatformTime::Seconds();
			{
				TSet<int32> Who;
				for (const FString& Line : Lines)
				{
					int32 Slot = OKStreetLineSpeaker(Line, Names);
					if (Slot == INDEX_NONE) Slot = Partner;
					const FString Words = OKStreetGalgameWords(Line, NameOf(Slot));
					if (OKStreetGalgamePlaceholder(Words)) continue;
					G.Group.Add({ Slot, Words });Who.Add(Slot);
				}
				if (Who.Num() < 2) G.Group.Reset();
				// Voiced line by line when the speech queue holds the same lines, one speaker each.
				G.bGroupVoiced = G.Group.Num() && S.TtsEnabled && S.FreeTalkSpeechSpeakers.Num() == G.Group.Num() && S.FreeTalkSpeechLines.Num() == G.Group.Num();
			}
			if (Lines.Num() && G.Group.IsEmpty())
			{
				const FString& Last = Lines.Last();
				int32 Slot = OKStreetLineSpeaker(Last, Names);
				if (Slot == INDEX_NONE) Slot = Partner;
				const FString Words = OKStreetGalgameWords(Last, NameOf(Slot));
				if (!OKStreetGalgamePlaceholder(Words)) SendLine(Slot, Words, S.TtsEnabled && Slot != 0);
			}
		}
		// The group reply, one girl at a time: her run of lines (the same speaker back to back) comes up when her voice
		// for its first line starts, and not before whoever spoke before her is done; without voices, by reading time.
		if (G.GroupNext < G.Group.Num() && !FreeTalkHisVoiceBusy())
		{
			// A voice that never starts (the voice server gone: a failed line still counts as played) holds the rest only so long;
			// a slow server (60 s from reply to voice on a busy GPU) is waited for.
			const bool bDue = G.bGroupVoiced ? S.FreeTalkSpeechPlayed > G.GroupNext || FPlatformTime::Seconds() >= G.GroupNextAt + 120.0 : FPlatformTime::Seconds() >= G.GroupNextAt;
			if (bDue)
			{
				const int32 Slot = G.Group[G.GroupNext].Slot;
				int32 End = G.GroupNext;FString Words;
				while (End < G.Group.Num() && G.Group[End].Slot == Slot) { Words += G.Group[End].Words; ++End; }
				G.RunStart = G.GroupNext;G.RunEnd = End;G.GroupNext = End;
				G.GroupNextAt = FPlatformTime::Seconds() + FMath::Max(1.5, 0.12 * Words.Len());
				SendLine(Slot, Words, G.bGroupVoiced && Slot != 0);
			}
		}
	}

	G.bWasReplaying = bReplaying;

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
	// Another girl speaking in a group talk: her too, so the first-person view turns to her.
	if (G.bActive && G.Speaker != INDEX_NONE && !People.Contains(G.Speaker)) People.Add(G.Speaker);
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
		// A replayed line of several clips: all of them count, and the short pause between clips is still the line.
		const bool bReplayClips = S.FreeTalkReplaySpeaker == G.Speaker && S.FreeTalkReplayClips > 1 && G.Speaker != INDEX_NONE;
		if (bReplayClips)
		{
			const bool bMore = !S.FreeTalkReplayParts.IsEmpty();
			const double Clip = bPlaying && Total > 0 ? FMath::Clamp(Played / Total, 0.0, 1.0) : 0.0;
			Progress = FMath::Clamp((FMath::Clamp(S.FreeTalkReplayClip, 0, S.FreeTalkReplayClips - 1) + Clip) / S.FreeTalkReplayClips, 0.0, 1.0);
			if (!bPlaying && !bMore) Progress = 1.0;
		}
		TSharedRef<FJsonObject> V = MakeShared<FJsonObject>();
		V->SetNumberField(TEXT("id"), 0);
		// Her free-talk reply is several TTS segments (a sentence or two each) under one line on screen: the typewriter
		// follows all of hers, and the gap while the next segment loads is still her line.
		bool bMoreSegments = false;
		if (!bScripted && !bReplayClips && G.Speaker != 0 && S.FreeTalkSpeechLines.Num() > 1 && S.FreeTalkSpeechPlayed > 0)
		{
			int32 Segments = 0, Started = 0;
			// A group reply: only the run on screen (her lines back to back), not all of hers in the reply.
			if (G.bGroupVoiced && G.RunEnd > G.RunStart) { Segments = G.RunEnd - G.RunStart; Started = FMath::Clamp(S.FreeTalkSpeechPlayed - G.RunStart, 0, Segments); }
			else for (int32 I = 0; I < S.FreeTalkSpeechLines.Num(); ++I)
			{
				const int32 Who = S.FreeTalkSpeechSpeakers.IsValidIndex(I) ? S.FreeTalkSpeechSpeakers[I] : S.FreeTalkSpeechCharacter;
				if (Who != G.Speaker) continue;
				++Segments;
				if (I < S.FreeTalkSpeechPlayed) ++Started;
			}
			if (Segments > 1 && Started > 0)
			{
				bMoreSegments = Started < Segments;
				const double Clip = bPlaying && Total > 0 ? FMath::Clamp(Played / Total, 0.0, 1.0) : 1.0;
				Progress = FMath::Clamp((Started - 1 + Clip) / Segments, 0.0, 1.0);
			}
		}
		V->SetBoolField(TEXT("playing"), bPlaying || (bReplayClips && !S.FreeTalkReplayParts.IsEmpty()) || bMoreSegments);
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
			FOKRuntimeCharacterStatus A, B;
			const bool bBoth = OKGetCrowdRuntimeCharacterStatus(this, 0, A) && OKGetCrowdRuntimeCharacterStatus(this, G.Partner, B);
			// A shot metres away from both of them is a broken one (the camera went off somewhere with the two far in
			// the picture): the view stays where it is, and what the shot was made from goes to the log.
			const bool bFar = bBoth && FVector::Dist(Pos, A.Transform.GetLocation()) > 600.f && FVector::Dist(Pos, B.Transform.GetLocation()) > 600.f;
			if (bFar)
			{
				if (SceneTime() >= G.FarShotLoggedAt + 2.0)
				{
					G.FarShotLoggedAt = SceneTime();
					FTransform HeadA, HeadB;
					const bool bHeadA = OKGetCrowdRuntimeBoneTransform(this, 0, TEXT("cm_j_head"), HeadA) || OKGetCrowdRuntimeBoneTransform(this, 0, TEXT("cf_j_head"), HeadA);
					const bool bHeadB = OKGetCrowdRuntimeBoneTransform(this, G.Partner, TEXT("cf_j_head"), HeadB) || OKGetCrowdRuntimeBoneTransform(this, G.Partner, TEXT("cm_j_head"), HeadB);
					UE_LOG(LogTemp, Warning, TEXT("Street galgame: far shot dropped: camera %s; he root %s head %s; partner %d root %s head %s"),
						*Pos.ToCompactString(), *A.Transform.GetLocation().ToCompactString(), bHeadA ? *HeadA.GetLocation().ToCompactString() : TEXT("-"),
						G.Partner, *B.Transform.GetLocation().ToCompactString(), bHeadB ? *HeadB.GetLocation().ToCompactString() : TEXT("-"));
				}
			}
			else
			{
				// Several metres from both of them (not broken, yet the two small in the picture): logged to see when it happens.
				if (bBoth && FVector::Dist(Pos, A.Transform.GetLocation()) > 250.f && FVector::Dist(Pos, B.Transform.GetLocation()) > 250.f && SceneTime() >= G.FarShotLoggedAt + 1.0)
				{
					G.FarShotLoggedAt = SceneTime();
					bool bMoving = false;
					(*Camera)->TryGetBoolField(TEXT("moving"), bMoving);
					UE_LOG(LogTemp, Display, TEXT("Street galgame: distant shot: camera %s %.0f/%.0f cm from him/her, moving=%d, street view %s"),
						*Pos.ToCompactString(), FVector::Dist(Pos, A.Transform.GetLocation()), FVector::Dist(Pos, B.Transform.GetLocation()), bMoving ? 1 : 0,
						*S.Camera->GetActorLocation().ToCompactString());
				}
				S.Camera->SetActorLocationAndRotation(Pos, Rot);
				S.Camera->GetCameraComponent()->SetFieldOfView(float((*Camera)->GetNumberField(TEXT("fov"))));
			}
			// Someone walking between the lens and the two of them is left out of the picture until they are past.
			if (bBoth && !bFar)
			{
				const FVector Target = (A.Transform.GetLocation() + B.Transform.GetLocation()) * .5 + FVector(0, 0, 120);
				TArray<UPrimitiveComponent*> Block;
				// Seen through his eyes (the first-person shot): his own head and body stay out of his view.
				FTransform PlayerHead;
				if ((OKGetCrowdRuntimeBoneTransform(this, 0, TEXT("cm_j_head"), PlayerHead) || OKGetCrowdRuntimeBoneTransform(this, 0, TEXT("cf_j_head"), PlayerHead))
					&& FVector::Dist(PlayerHead.GetLocation(), Pos) < 45.f * FMath::Max(A.Transform.GetScale3D().Z, .1))
					OKGetCrowdRuntimeCharacterComponents(this, 0, Block);
				// Her face is what the shot is about: the line to her head counts as well as the one to the middle of the two.
				const FVector HerHead = B.Transform.GetLocation() + FVector(0, 0, 140);
				const FVector Forward = Rot.GetForwardVector();
				const float HalfFov = FMath::DegreesToRadians(float((*Camera)->GetNumberField(TEXT("fov"))) * .5f + 10.f);
				for (int32 Slot = 1; Slot < S.Manifest.Characters.Num(); ++Slot)
				{
					FOKRuntimeCharacterStatus O;
					if (Slot == G.Partner || !OKGetCrowdRuntimeCharacterStatus(this, Slot, O)) continue;
					const FVector Body = O.Transform.GetLocation() + FVector(0, 0, 90);
					bool bInWay = false;
					for (const FVector& To : {Target, HerHead})
					{
						const FVector Closest = FMath::ClosestPointOnSegment(Body, Pos, To);
						bInWay |= FVector::DistXY(Body, Closest) < 80.f && FVector::Dist(Pos, Closest) < FVector::Dist(Pos, To) - 30.f;
					}
					// Standing right at the lens (a girl beside the table at the cafeteria): off the line, yet her hair and
					// back fill the picture.
					for (const float Z : {30.f, 90.f, 150.f})
					{
						const FVector Rel = O.Transform.GetLocation() + FVector(0, 0, Z) - Pos;
						const float Depth = float(Rel | Forward);
						bInWay |= Depth > 0.f && Depth < 150.f && FMath::Acos(FMath::Clamp(Depth / FMath::Max(float(Rel.Size()), 1.f), -1.f, 1.f)) < HalfFov;
					}
					if (bInWay) OKGetCrowdRuntimeCharacterComponents(this, Slot, Block);
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
			}, Post, CVarOKStreetGalgamePortraitOutline.GetValueOnGameThread() ? TFunction<void(bool)>() : TFunction<void(bool)>([this, Slot](bool bCoverage) { OKSetCrowdRuntimeOutlinesShown(this, Slot, !bCoverage); }));
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
