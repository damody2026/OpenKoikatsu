#include "OKInteractionPairGameMode.h"
#include "GameFramework/HUD.h"
#include "OKInteractionMode.h"
#include "OKCharacterController.h"
#include "KKPreviewActor.h"
#include "Components/ComboBoxString.h"
#include "OKMotionLabelZh.h"
#include "OKInteractionScene.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "OpenKoikatsuPlayerController.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "OKPhysXSecondary.h"
#include "KKPairedPlayback.h"
#include "EngineUtils.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/SkinnedMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/StringOutputDevice.h"

DEFINE_LOG_CATEGORY_STATIC(LogOKInteractionPair, Log, All);

AOKInteractionPairGameMode::AOKInteractionPairGameMode()
{
	// AOKHUD builds the card-reader gallery (card folders, save card, 3D preview) over the scene; this mode does not use it.
	HUDClass = AHUD::StaticClass();
	PlayerControllerClass = AOpenKoikatsuPlayerController::StaticClass();
	// The interaction scene owns the camera; no pawn is needed.
	DefaultPawnClass = nullptr;
}

void AOKInteractionPairGameMode::BeginPlay()
{
	Super::BeginPlay();
	const FString CardRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()) / TEXT("CharacterCards/Demo");
	FemaleCard = CardRoot / TEXT("C050.png");
	MaleCard = CardRoot / TEXT("Male01.png");
	FParse::Value(FCommandLine::Get(), TEXT("PairFemale="), FemaleCard);
	FParse::Value(FCommandLine::Get(), TEXT("PairMale="), MaleCard);
	FemaleCard = FPaths::ConvertRelativePathToFull(FemaleCard);
	MaleCard = FPaths::ConvertRelativePathToFull(MaleCard);
	FPaths::NormalizeFilename(FemaleCard);
	FPaths::NormalizeFilename(MaleCard);
	// Give the player controller and viewport one frame to exist before the stage replaces the view.
	GetWorldTimerManager().SetTimer(StartTimer, this, &AOKInteractionPairGameMode::StartInteraction, 0.5f, false);
}

void AOKInteractionPairGameMode::StartInteraction()
{
	APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Player)
	{
		UE_LOG(LogOKInteractionPair, Error, TEXT("No player controller; cannot start the interaction scene"));
		return;
	}
	UE_LOG(LogOKInteractionPair, Display, TEXT("Female=%s Male=%s"), *FemaleCard, *MaleCard);
	AOKInteractionMode* Mode = AOKInteractionMode::EnterCards(Player, FemaleCard, MaleCard);
	if (!Mode)
	{
		UE_LOG(LogOKInteractionPair, Error, TEXT("EnterCards refused (missing card file or invalid controller)"));
		return;
	}
	ActiveMode = Mode;
	HeadLookAttempts = 0;
	// The audit drives the head itself (OKPhysXSecondary.DebugHeadTilt); looking at the camera would fight it.
	// Plain stage: gray background, no floor (-PairBackground=<0..1> -PairFloor=1 to change). Applied once the
	// interaction scene has spawned its showcase meshes.
	FParse::Value(FCommandLine::Get(), TEXT("PairBackground="), BackgroundGray);
	FParse::Bool(FCommandLine::Get(), TEXT("PairFloor="), bFloorVisible);
	GetWorldTimerManager().SetTimer(StageTimer, this, &AOKInteractionPairGameMode::ApplyStage, 0.5f, true);
	bool bFaceCamera = true;
	FParse::Bool(FCommandLine::Get(), TEXT("PairFaceCamera="), bFaceCamera);
	if (!FCString::Strifind(FCommandLine::Get(), TEXT("-PairAudit")) && bFaceCamera)
	{
		GetWorldTimerManager().SetTimer(HeadLookTimer, this, &AOKInteractionPairGameMode::EnableHeadLook, 0.5f, true);
	}
	Mode->ReturnLabel = TEXT("結束");
	// Dev only: dump every solo motion to Saved/MotionCatalogue.tsv (input of `ok-extract motion-catalogue`).
	// Reading all sources blocks the game thread for about 15 s, so it never runs unless asked for.
	// Dev only: select a few motions from the merged list and log whether each one starts playing, then quit.
	if (FParse::Param(FCommandLine::Get(), TEXT("PairSelfTest")))
	{
		GetWorldTimerManager().SetTimer(StartTimer, this, &AOKInteractionPairGameMode::TickSelfTest, 1.0f, true);
	}
	// Matches both -PairAudit and -PairAudit=first,last (FParse::Param only matches the bare switch).
	if (FCString::Strifind(FCommandLine::Get(), TEXT("-PairAudit")))
	{
		FString Range;
		PairAuditIndex = 1;
		PairAuditLast = MAX_int32;
		// FParse::Value stops at commas: the range is -PairAuditFirst=N -PairAuditLast=M.
		int32 First = 1, LastPose = MAX_int32;
		FParse::Value(FCommandLine::Get(), TEXT("PairAuditFirst="), First);
		FParse::Value(FCommandLine::Get(), TEXT("PairAuditLast="), LastPose);
		PairAuditIndex = FMath::Max(1, First);
		PairAuditLast = LastPose;
		// -PairAuditPerf measures frame time only: the clipping/jitter audit stays off as in normal play.
		IConsoleVariable* Audit = FCString::Strifind(FCommandLine::Get(), TEXT("-PairAuditPerf")) ? nullptr : IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.Audit"));
		if (Audit)
		{
			Audit->Set(1, ECVF_SetByCode); // clipping/jitter measurement is off in normal play
		}
		GetWorldTimerManager().SetTimer(StartTimer, this, &AOKInteractionPairGameMode::TickPairAudit, 0.5f, true);
	}
	if (FCString::Strifind(FCommandLine::Get(), TEXT("-PairControl")))
	{
		int32 Port = 47871;
		FParse::Value(FCommandLine::Get(), TEXT("PairControl="), Port);
		StartControl(Port);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("DumpMotionCatalogue")))
	{
		GetWorldTimerManager().SetTimer(StartTimer, this, &AOKInteractionPairGameMode::ReportCatalogue, 2.0f, true);
	}
	TWeakObjectPtr<UWorld> WeakWorld(GetWorld());
	Mode->Returned = [WeakWorld, WeakPlayer = TWeakObjectPtr<APlayerController>(Player)](TSharedRef<FJsonObject>)
	{
		if (UWorld* World = WeakWorld.Get())
		{
			UKismetSystemLibrary::QuitGame(World, WeakPlayer.Get(), EQuitPreference::Quit, false);
		}
	};
}
// Logs how many motions the interaction scene exposes once it is ready (pairs, categories, solo sources).
void AOKInteractionPairGameMode::ReportCatalogue()
{
	AOKInteractionMode* Mode = ActiveMode.Get();
	if (!Mode || Mode->IsFinished())
	{
		GetWorldTimerManager().ClearTimer(StartTimer);
		return;
	}
	UOKCharacterController* C = Mode->Controller;
	if (!Mode->IsReady() || !C || !C->PairCombo || !C->PairCategoryCombo || !C->GameSourceCombo || !C->PreviewActor)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(StartTimer);
	int32 SoloClips = 0;
	FString Dump;
	for (const TPair<FString, FString>& Source : C->InteractionMotionSources)
	{
		const int32 Count = C->PreviewActor->Session.ClipNamesForSource(Source.Value).Num();
		SoloClips += Count;
		for (const FString& Name : C->PreviewActor->Session.ClipNamesForSource(Source.Value))
		{
			Dump += FString::Printf(TEXT("solo\t%s\t%s\t%s\n"), *Source.Value, *Name, *OKMotionLabelDisplay(Name));
		}
		UE_LOG(LogOKInteractionPair, Display, TEXT("Solo source %s (%s): %d clips"), *Source.Key, *Source.Value, Count);
	}
	for (const TSharedPtr<FJsonObject>& Row : C->NativePairs)
	{
		FString Json;
		FJsonSerializer::Serialize(Row.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
		Dump += TEXT("pair\t") + Json + TEXT("\t") + OKMotionLabelDisplay(Row->GetStringField(TEXT("name"))) + TEXT("\n");
	}
	FFileHelper::SaveStringToFile(Dump, *(FPaths::ProjectSavedDir() / TEXT("MotionCatalogue.tsv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogOKInteractionPair, Display, TEXT("Catalogue: pair motions=%d categories=%d solo sources=%d solo clips(imported)=%d"),
		C->NativePairs.Num(), C->PairCategoryCombo->GetOptionCount() - 1, C->InteractionMotionSources.Num(), SoloClips);
}


// Two-person clipping audit. Walks the paired poses (PairCombo, both characters animated together), plays the most
// violent looping state of each for 10 s once it is running, and logs how far hair/cloth ended up inside any body (own
// or partner; see OKPhysXSecondary::FStats). -PairAudit[=first,last] limits the range (1-based PairCombo rows),
// -PairAuditFilter=<text> keeps rows whose label contains the text, -PairAuditShots saves one screenshot each.
static TAutoConsoleVariable<int32> CVarPairBatch(TEXT("ok.Interaction.PairBatch"), 0,
	TEXT("1: the /pose walk saves every state of every pose as an OK group (OKAnim.SavePair) instead of measuring physics."));

void AOKInteractionPairGameMode::TickPairAudit()
{
	AOKInteractionMode* Mode = ActiveMode.Get();
	UOKCharacterController* C = Mode ? Mode->Controller.Get() : nullptr;
	const double Now = FPlatformTime::Seconds();
	if (!Mode || !Mode->IsReady() || !C || !C->PairCombo || !C->PairStateCombo || !C->PreviewActor || !C->PartnerActor || C->PairCombo->GetOptionCount() < 2)
	{
		static double LastNote = 0.;
		if (Now - LastNote > 20.)
		{
			LastNote = Now;
			UE_LOG(LogOKInteractionPair, Display, TEXT("PairAudit waiting: mode=%d ready=%d controller=%d pairs=%d partner=%d"), Mode != nullptr, Mode && Mode->IsReady(),
				C != nullptr, C && C->PairCombo ? C->PairCombo->GetOptionCount() - 1 : -1, C && C->PartnerActor);
		}
		return;
	}
	if (PairAuditKeys.IsEmpty())
	{
		if (PairAuditFilter.IsEmpty()) FParse::Value(FCommandLine::Get(), TEXT("PairAuditFilter="), PairAuditFilter); // /pose sets it at runtime
		for (int32 Row = 1; Row < C->PairCombo->GetOptionCount(); ++Row)
		{
			if (PairAuditFilter.IsEmpty() || PairAuditFilter == TEXT("*") || C->PairCombo->GetOptionAtIndex(Row).Contains(PairAuditFilter)) // * = every pose
			{
				PairAuditKeys.Add(Row);
			}
		}
		UE_LOG(LogOKInteractionPair, Display, TEXT("PairAudit poses=%d (of %d paired poses)"), PairAuditKeys.Num(), C->PairCombo->GetOptionCount() - 1);
		if (PairAuditKeys.IsEmpty())
		{
			GetWorldTimerManager().ClearTimer(StartTimer);
			return;
		}
	}
	int32 Last = FMath::Min(PairAuditLast, PairAuditKeys.Num());
	const int32 Row = PairAuditKeys.IsValidIndex(PairAuditIndex - 1) ? PairAuditKeys[PairAuditIndex - 1] : INDEX_NONE;
	// ok.Interaction.PairBatch 1: walk every state of every pose and save each as an OK group (OKAnim.SavePair) instead of measuring.
	static IConsoleVariable* PairBatchVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Interaction.PairBatch"));
	const bool bPairBatch = PairBatchVar && PairBatchVar->GetInt() != 0;
	if (bPairBatch) Last = PairAuditKeys.Num();
	const bool bAllStates = bPairBatch || FCString::Strifind(FCommandLine::Get(), TEXT("-PairAuditAllStates")) != nullptr;
	auto Finish = [&](const TCHAR* Result)
	{
		const OKPhysXSecondary::FStats Stats = OKPhysXSecondary::GetStats();
		UE_LOG(LogOKInteractionPair, Display, TEXT("PairAudit %d/%d %s · %s maxPenetration=%.3f violations=%lld maxProjection=%.3f resets=%lld steps=%lld %s byBody=%s softTissue=%.3f/%lld pops=%lld jitter=%lld maxJerk=%.3f swing=%.4f hard=%lld (%s)"),
			PairAuditIndex, Last, *C->PairCombo->GetOptionAtIndex(Row), *C->PairStateCombo->GetSelectedOption(), Stats.MaxPenetration, Stats.Violations,
			Stats.MaxProjection, Stats.Resets, Stats.Steps, Result, *Stats.ViolationsByBody, Stats.BodyPenetration, Stats.BodyViolations, Stats.Pops, Stats.Jitter, Stats.MaxJerk, Stats.MeanSwing, Stats.HardFixes, *Stats.HardSplit);
		if (bAllStates && PairAuditState > 0 && PairAuditState + 1 < C->PairStateCombo->GetOptionCount())
		{
			++PairAuditState; // next variant of the same pose
			PairAuditPhase = 1;
			PairAuditAt = FPlatformTime::Seconds();
			return;
		}
		PairAuditState = 0;
		++PairAuditIndex;
		PairAuditPhase = 0;
	};
	switch (PairAuditPhase)
	{
	case 0:
		if (PairAuditIndex > Last || Row == INDEX_NONE)
		{
			UE_LOG(LogOKInteractionPair, Display, TEXT("PairAudit done"));
			GetWorldTimerManager().ClearTimer(StartTimer);
			if (FParse::Param(FCommandLine::Get(), TEXT("PairAuditQuit")))
			{
				FPlatformMisc::RequestExit(false);
			}
			return;
		}
		C->PairCombo->SetSelectedIndex(Row);
		PairAuditAt = Now;
		PairAuditPhase = 1;
		return;
	case 1:
	{
		// Wait for the pose to be prepared and playing (its states are listed), then pick the most violent loop:
		// strong loops move head and hips to their extremes.
		// The new pose is playing once the status reads "<pose> · <state>" (the previous pose's states are still listed
		// until then, so checking the state list alone would measure the old pose).
		const FString Pose = C->PairCombo->GetOptionAtIndex(Row);
		const FString PairText = C->PairStatus ? C->PairStatus->GetText().ToString() : FString();
		if (!C->PairedPlayback || C->PairStateCombo->GetOptionCount() < 2 || !PairText.StartsWith(Pose + TEXT(" · ")))
		{
			if (Now - PairAuditAt > 240.) // some poses take ~90 s to prepare
			{
				Finish(TEXT("result=not-playing"));
			}
			return;
		}
		int32 Best = INDEX_NONE, BestRank = MAX_int32;
		if (bAllStates || PairAuditState > 0) // every state in turn, or the one /pose asked for
		{
			// Every variant of the pose (weak/strong loops, anal versions, climaxes ...) in turn.
			if (PairAuditState == 0) PairAuditState = 1;
			Best = PairAuditState;
			BestRank = -1;
		}
		for (int32 Option = 1; Option < C->PairStateCombo->GetOptionCount(); ++Option)
		{
			const FString State = C->PairStateCombo->GetOptionAtIndex(Option);
			const int32 Rank = State.Contains(TEXT("SLoop")) || State.Contains(TEXT("S_Loop")) ? 0 : State.Contains(TEXT("Loop")) ? 1 : 2;
			if (!bAllStates && Rank < BestRank) { Best = Option; BestRank = Rank; }
			if (!PairAuditStateName.IsEmpty() && State == PairAuditStateName) { Best = Option; BestRank = -2; break; }
		}
		if (Best != INDEX_NONE && C->PairStateCombo->GetSelectedIndex() != Best)
		{
			C->PairStateCombo->SetSelectedIndex(Best);
		}
		PairAuditAt = Now;
		PairAuditPhase = 2;
		return;
	}
	case 2:
	{
		const FString Expected = C->PairCombo->GetOptionAtIndex(Row) + TEXT(" · ") + C->PairStateCombo->GetSelectedOption();
		const FString PairText = C->PairStatus ? C->PairStatus->GetText().ToString() : FString();
		// Measuring lets the state settle for 4 s; saving only needs the state to be the one playing.
		if ((!PairText.StartsWith(Expected) && Now - PairAuditAt < 30.) || Now - PairAuditAt < (bPairBatch ? 0.5 : 4.))
		{
			return;
		}
	}
		if (bPairBatch)
		{
			GEngine->Exec(GetWorld(), *FString::Printf(TEXT("OKAnim.SavePair %s"), *C->PairCombo->GetOptionAtIndex(Row)));
			Finish(TEXT("result=saved"));
			return;
		}
		{
			// -PairAuditSpeed=<0..4>: faster playback covers more motion cycles (and harder accelerations) per run.
			float Speed = 1.f;
			if (FParse::Value(FCommandLine::Get(), TEXT("PairAuditSpeed="), Speed) && C->PairedPlayback) C->PairedPlayback->SetSpeed(Speed);
		}
		OKPhysXSecondary::ResetStats();
		if (C->InteractionWidget && C->InteractionWidget->Scene)
		{
			C->InteractionWidget->Scene->ResetPerformance();
		}
		PairAuditAt = Now;
		MeasureSince = Now;
		if (bControl) UE_LOG(LogOKInteractionPair, Display, TEXT("PairControl measuring: %s · %s"), *C->PairCombo->GetOptionAtIndex(Row), *C->PairStateCombo->GetSelectedOption());
		PairAuditPhase = 3;
		return;
	default:
		if (PairAuditPhase == 3 && Now - PairAuditAt >= 5. && FParse::Param(FCommandLine::Get(), TEXT("PairAuditShots")))
		{
			PairAuditPhase = 4;
			GEngine->Exec(GetWorld(), *FString::Printf(TEXT("OKPhysXSecondary.Shot 0 PairAudit_%03d_%02d"), PairAuditIndex, PairAuditState));
		}
		double Seconds = 10.;
		FParse::Value(FCommandLine::Get(), TEXT("PairAuditSeconds="), Seconds);
		if (bControl)
		{
			return; // measured on request (/stats) instead of after a fixed time
		}
		if (Now - PairAuditAt < Seconds)
		{
			return;
		}
		if (C->InteractionWidget && C->InteractionWidget->Scene)
		{
			const OKPhysXSecondary::FStats Perf = OKPhysXSecondary::GetStats();
			UE_LOG(LogOKInteractionPair, Display, TEXT("PairPerf %d physxSolveMsPerSecond=%.3f (prepare %.1f simulate %.1f collide %.1f finish %.1f) frames: %s"), PairAuditIndex, Perf.SolveSeconds * 1000. / FMath::Max(Now - PairAuditAt, 1e-3),
				Perf.PhaseSeconds[0] * 1000. / FMath::Max(Now - PairAuditAt, 1e-3), Perf.PhaseSeconds[1] * 1000. / FMath::Max(Now - PairAuditAt, 1e-3), Perf.PhaseSeconds[2] * 1000. / FMath::Max(Now - PairAuditAt, 1e-3), Perf.PhaseSeconds[3] * 1000. / FMath::Max(Now - PairAuditAt, 1e-3),
				*C->InteractionWidget->Scene->PerformanceSummary().Replace(TEXT("\n"), TEXT(" | ")));
		}
		Finish(TEXT("result=measured"));
		return;
	}
}

void AOKInteractionPairGameMode::TickSelfTest()
{
	AOKInteractionMode* Mode = ActiveMode.Get();
	UOKCharacterController* C = Mode ? Mode->Controller.Get() : nullptr;
	UOKInteractionWidget* UI = C ? C->InteractionWidget.Get() : nullptr;
	if (!Mode || !Mode->IsReady() || !UI || !UI->ReadyMotionCombo || !UI->MotionPreparation || UI->ReadyMotionKeys.IsEmpty())
	{
		return;
	}
	static const int32 Picks[] = {1, 2500, 5000, 9000, 12000};
	const FString Status = UI->MotionPreparation->GetText().ToString();
	const double Now = FPlatformTime::Seconds();
	if (SelfTestAt > 0. && !Status.StartsWith(TEXT("正在播放：")) && !Status.Contains(TEXT("失敗")) && !Status.Contains(TEXT("無法套用")) && Now - SelfTestAt < 40.)
	{
		return;
	}
	if (SelfTestAt > 0.)
	{
		UE_LOG(LogOKInteractionPair, Display, TEXT("SelfTest pick #%d: %s"), Picks[SelfTestStep - 1], *Status);
	}
	if (SelfTestStep >= UE_ARRAY_COUNT(Picks))
	{
		UE_LOG(LogOKInteractionPair, Display, TEXT("SelfTest done"));
		GetWorldTimerManager().ClearTimer(StartTimer);
		return;
	}
	const int32 Pick = FMath::Min(Picks[SelfTestStep], UI->ReadyMotionKeys.Num());
	UE_LOG(LogOKInteractionPair, Display, TEXT("SelfTest select #%d key=%s label=%s"), Pick, *UI->ReadyMotionKeys[Pick - 1], *OKMotionLabelDisplay(UI->ReadyMotionKeys[Pick - 1].RightChop(UI->ReadyMotionKeys[Pick - 1].Find(TEXT("|")) + 1)));
	UI->MotionPreparation->SetText(FText::FromString(TEXT("測試中")));
	UI->ReadyMotionCombo->SetSelectedIndex(Pick);
	++SelfTestStep;
	SelfTestAt = Now;
}

// Both characters turn their heads to the camera by default (the paired playback updates the target every frame).
// Retried until the stage and both rigs are ready; the panel checkbox stays available to turn it off afterwards.
void AOKInteractionPairGameMode::EnableHeadLook()
{
	AOKInteractionMode* Mode = ActiveMode.Get();
	UOKCharacterController* C = Mode ? Mode->Controller.Get() : nullptr;
	if (!Mode || Mode->IsFinished() || ++HeadLookAttempts > 240)
	{
		GetWorldTimerManager().ClearTimer(HeadLookTimer);
		return;
	}
	AKKPreviewActor* Female = C ? C->PreviewActor.Get() : nullptr;
	AKKPreviewActor* Male = C ? C->PartnerActor.Get() : nullptr;
	if (!Mode->IsReady() || !Female || !Male)
	{
		return;
	}
	if (!Male->StudioCam)
	{
		Male->StudioCam = Female->StudioCam;
	}
	const bool FemaleOn = Female->bHeadLookFollowCamera || Female->FollowHeadLookCamera(true);
	const bool MaleOn = Male->bHeadLookFollowCamera || Male->FollowHeadLookCamera(true);
	if (C->InteractionWidget && C->InteractionWidget->HeadCamera)
	{
		C->InteractionWidget->HeadCamera->SetIsChecked(FemaleOn);
	}
	if (FemaleOn && MaleOn)
	{
		UE_LOG(LogOKInteractionPair, Display, TEXT("Head look at camera: on for both characters"));
		GetWorldTimerManager().ClearTimer(HeadLookTimer);
	}
}

void AOKInteractionPairGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ControlRouter)
	{
		for (const FHttpRouteHandle& Handle : ControlRoutes)
		{
			ControlRouter->UnbindRoute(Handle);
		}
	}
	ControlRoutes.Reset();
	ControlRouter.Reset();
	Super::EndPlay(Reason);
}

FString AOKInteractionPairGameMode::MeasurementLines(double Since) const
{
	AOKInteractionMode* Mode = ActiveMode.Get();
	UOKCharacterController* C = Mode ? Mode->Controller.Get() : nullptr;
	const double Seconds = FMath::Max(FPlatformTime::Seconds() - Since, 1e-3);
	const OKPhysXSecondary::FStats Stats = OKPhysXSecondary::GetStats();
	IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.Enabled"));
	IConsoleVariable* Backend = IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.Backend"));
	// Backend 3 (Bullet) is not shipped and runs as PhysX (OKPhysXSecondary falls back to 0).
	static const TCHAR* const Names[] = {TEXT("PhysX"), TEXT("Kawaii"), TEXT("Chaos"), TEXT("PhysX"), TEXT("Jolt"), TEXT("MuJoCo"), TEXT("DynamicBone")};
	const FString Solver = (!Enabled || !Enabled->GetBool()) ? FString(TEXT("Verlet")) : (Backend && Backend->GetInt() < 0 ? FString(TEXT("PerComponent")) : FString(Names[FMath::Clamp(Backend ? Backend->GetInt() : 0, 0, 6)]));
	FString Out = FString::Printf(TEXT("solver=%s seconds=%.1f pose=%s · %s\n"), *Solver, Seconds,
		C && C->PairCombo ? *C->PairCombo->GetSelectedOption() : TEXT("-"), C && C->PairStateCombo ? *C->PairStateCombo->GetSelectedOption() : TEXT("-"));
	Out += FString::Printf(TEXT("solveMsPerSecond=%.1f (prepare %.1f simulate %.1f collide %.1f finish %.1f)\n"), Stats.SolveSeconds * 1000. / Seconds,
		Stats.PhaseSeconds[0] * 1000. / Seconds, Stats.PhaseSeconds[1] * 1000. / Seconds, Stats.PhaseSeconds[2] * 1000. / Seconds, Stats.PhaseSeconds[3] * 1000. / Seconds);
	if (C && C->InteractionWidget && C->InteractionWidget->Scene)
	{
		Out += C->InteractionWidget->Scene->PerformanceSummary().Replace(TEXT("\n"), TEXT(" | ")) + TEXT("\n");
	}
	if (C && C->PreviewActor && C->PartnerActor)
	{
		// Face camera check: residual = face vs its limited goal (should be ~0), excess = goal vs camera (outside the limits).
		auto Look = [](AKKPreviewActor* A) { float R = -1.f, E = -1.f; return A->Session.HeadLookError(R, E) ? FString::Printf(TEXT("%.1f/%.1f"), R, E) : FString(TEXT("off")); };
		Out += FString::Printf(TEXT("look female=%s male=%s\n"), *Look(C->PreviewActor), *Look(C->PartnerActor));
	}
	Out += FString::Printf(TEXT("maxPenetration=%.3f violations=%lld maxProjection=%.3f resets=%lld steps=%lld byBody=%s softTissue=%.3f/%lld pops=%lld jitter=%lld maxJerk=%.3f swing=%.4f drop=%.4f hard=%lld (%s) true=%.3f/%lld stretch=%.2f maxSwing=%.3f@%llu/%llu\n"),
		Stats.MaxPenetration, Stats.Violations, Stats.MaxProjection, Stats.Resets, Stats.Steps, *Stats.ViolationsByBody, Stats.BodyPenetration, Stats.BodyViolations,
		Stats.Pops, Stats.Jitter, Stats.MaxJerk, Stats.MeanSwing, Stats.MeanDrop, Stats.HardFixes, *Stats.HardSplit, Stats.TrueDepth, Stats.TrueViolations, Stats.MaxStretch, Stats.MaxSwing, Stats.MaxSwingWhere >> 16, Stats.MaxSwingWhere & 0xffff);
	if (!Stats.BySolver.IsEmpty()) Out += FString::Printf(TEXT("bySolver=%s\n"), *Stats.BySolver);
	return Out;
}

void AOKInteractionPairGameMode::StartControl(int32 Port)
{
	ControlRouter = FHttpServerModule::Get().GetHttpRouter(Port, /*bFailOnBindFailure*/ true);
	if (!ControlRouter)
	{
		UE_LOG(LogOKInteractionPair, Error, TEXT("PairControl: cannot listen on port %d"), Port);
		return;
	}
	bControl = true;
	TWeakObjectPtr<AOKInteractionPairGameMode> Weak(this);
	auto Reply = [](const FHttpResultCallback& Done, const FString& Text)
	{
		Done(FHttpServerResponse::Create(Text, TEXT("text/plain; charset=utf-8")));
		return true;
	};
	auto Param = [](const FHttpServerRequest& Request, const TCHAR* Key)
	{
		const FString* Value = Request.QueryParams.Find(Key);
		return Value ? FGenericPlatformHttp::UrlDecode(*Value) : FString();
	};
	ControlRoutes.Add(ControlRouter->BindRoute(FHttpPath(TEXT("/exec")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([Weak, Reply, Param](const FHttpServerRequest& Request, const FHttpResultCallback& Done)
		{
			AOKInteractionPairGameMode* Self = Weak.Get();
			if (!Self) return Reply(Done, TEXT("no game\n"));
			FString Text;
			TArray<FString> Commands;
			Param(Request, TEXT("cmd")).ParseIntoArray(Commands, TEXT(";"));
			for (FString Command : Commands)
			{
				Command.TrimStartAndEndInline();
				if (Command.IsEmpty()) continue;
				FStringOutputDevice Output;
				const bool bOk = GEngine->Exec(Self->GetWorld(), *Command, Output);
				UE_LOG(LogOKInteractionPair, Display, TEXT("PairControl exec %s -> %d"), *Command, bOk);
				Text += FString::Printf(TEXT("%s %s %s\n"), bOk ? TEXT("ok") : TEXT("??"), *Command, *FString(Output).TrimStartAndEnd());
			}
			return Reply(Done, Text.IsEmpty() ? FString(TEXT("nothing to run\n")) : Text);
		})));
	ControlRoutes.Add(ControlRouter->BindRoute(FHttpPath(TEXT("/reset")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([Weak, Reply](const FHttpServerRequest&, const FHttpResultCallback& Done)
		{
			AOKInteractionPairGameMode* Self = Weak.Get();
			if (!Self) return Reply(Done, TEXT("no game\n"));
			OKPhysXSecondary::ResetStats();
			AOKInteractionMode* Mode = Self->ActiveMode.Get();
			if (UOKCharacterController* C = Mode ? Mode->Controller.Get() : nullptr)
			{
				if (C->InteractionWidget && C->InteractionWidget->Scene) C->InteractionWidget->Scene->ResetPerformance();
			}
			Self->MeasureSince = FPlatformTime::Seconds();
			return Reply(Done, TEXT("reset\n"));
		})));
	ControlRoutes.Add(ControlRouter->BindRoute(FHttpPath(TEXT("/stats")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([Weak, Reply](const FHttpServerRequest&, const FHttpResultCallback& Done)
		{
			AOKInteractionPairGameMode* Self = Weak.Get();
			if (!Self) return Reply(Done, TEXT("no game\n"));
			const FString Lines = Self->MeasurementLines(Self->MeasureSince);
			UE_LOG(LogOKInteractionPair, Display, TEXT("PairControl stats %s"), *Lines.Replace(TEXT("\n"), TEXT(" || ")));
			return Reply(Done, Lines);
		})));
	ControlRoutes.Add(ControlRouter->BindRoute(FHttpPath(TEXT("/pose")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda([Weak, Reply, Param](const FHttpServerRequest& Request, const FHttpResultCallback& Done)
		{
			AOKInteractionPairGameMode* Self = Weak.Get();
			if (!Self) return Reply(Done, TEXT("no game\n"));
			// Restart the audit walk on the matching poses; it picks the strongest loop (or state=<n>) and keeps
			// measuring until /stats.
			Self->PairAuditFilter = Param(Request, TEXT("filter"));
			Self->PairAuditKeys.Reset();
			Self->PairAuditIndex = 1;
			Self->PairAuditLast = 1;
			Self->PairAuditPhase = 0;
			const FString State = Param(Request, TEXT("state"));
			Self->PairAuditState = State.IsNumeric() ? FCString::Atoi(*State) : 0;
			Self->PairAuditStateName = State.IsNumeric() ? FString() : State;
			if (!Self->GetWorldTimerManager().IsTimerActive(Self->StartTimer))
			{
				Self->GetWorldTimerManager().SetTimer(Self->StartTimer, Self, &AOKInteractionPairGameMode::TickPairAudit, 0.5f, true);
			}
			return Reply(Done, FString::Printf(TEXT("switching to the first pose containing '%s'\n"), *Self->PairAuditFilter));
		})));
	FHttpServerModule::Get().StartAllListeners();
	UE_LOG(LogOKInteractionPair, Display, TEXT("PairControl listening on http://127.0.0.1:%d (/exec /reset /stats /pose)"), Port);
}

void AOKInteractionPairGameMode::SetFaceCamera(bool bOn)
{
	GetWorldTimerManager().ClearTimer(HeadLookTimer); // an explicit choice ends the startup default
	AOKInteractionMode* Mode = ActiveMode.Get();
	UOKCharacterController* C = Mode ? Mode->Controller.Get() : nullptr;
	if (!C || !C->PreviewActor)
	{
		UE_LOG(LogOKInteractionPair, Warning, TEXT("FaceCamera: scene not ready"));
		return;
	}
	if (C->InteractionWidget)
	{
		C->InteractionWidget->OnHeadCamera(bOn); // same path as the panel checkbox (heroine + partner, checkbox state)
	}
	UE_LOG(LogOKInteractionPair, Display, TEXT("FaceCamera %s"), bOn ? TEXT("on") : TEXT("off"));
}

static FAutoConsoleCommandWithWorldAndArgs GOKInteractionPairFaceCameraCommand(
	TEXT("ok.Interaction.FaceCamera"),
	TEXT("ok.Interaction.FaceCamera 0|1: both characters turn their heads to the camera, or not."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AOKInteractionPairGameMode* Mode = World ? World->GetAuthGameMode<AOKInteractionPairGameMode>() : nullptr;
		if (!Mode) return;
		Mode->SetFaceCamera(Args.Num() == 0 || FCString::ToBool(*Args[0]));
	}));

void AOKInteractionPairGameMode::ApplyStage()
{
	AOKInteractionMode* Mode = ActiveMode.Get();
	if (!Mode || !Mode->IsReady())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(StageTimer);
	SetBackground(BackgroundGray);
	SetFloorVisible(bFloorVisible);
}

void AOKInteractionPairGameMode::SetBackground(float Gray)
{
	BackgroundGray = FMath::Clamp(Gray, 0.f, 4.f);
	// Height fog that only starts far behind the characters: everything that is not geometry (the empty sky) gets
	// its colour, the characters and their hair are untouched. No material or shader compile needed.
	AExponentialHeightFog* Fog = BackgroundFog.Get();
	if (!Fog)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Fog = GetWorld()->SpawnActor<AExponentialHeightFog>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		BackgroundFog = Fog;
	}
	if (!Fog) return;
	UExponentialHeightFogComponent* C = Fog->GetComponent();
	C->SetFogDensity(0.05f);
	C->SetFogHeightFalloff(0.001f);
	C->SetStartDistance(3000.f); // 30 m: the pair and the camera stay well inside
	C->SetFogMaxOpacity(1.f);
	C->SetFogInscatteringColor(FLinearColor(BackgroundGray, BackgroundGray, BackgroundGray));
	C->SetDirectionalInscatteringExponent(1.f);
	C->SetDirectionalInscatteringColor(FLinearColor::Black);
	C->SetVolumetricFog(false);
	C->SetVisibility(BackgroundGray > 0.f);
	UE_LOG(LogOKInteractionPair, Display, TEXT("Background gray %.2f"), BackgroundGray);
}

void AOKInteractionPairGameMode::SetFloorVisible(bool bVisible)
{
	bFloorVisible = bVisible;
	int32 Changed = 0;
	for (TActorIterator<AOKInteractionScene> It(GetWorld()); It; ++It)
	{
		if (It->Floor) { It->Floor->SetVisibility(bVisible); ++Changed; }
		if (It->Backdrop) { It->Backdrop->SetVisibility(bVisible); ++Changed; }
	}
	// The template map's own floor slab.
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		if (It->GetName().Contains(TEXT("Floor")))
		{
			It->GetStaticMeshComponent()->SetVisibility(bVisible);
			++Changed;
		}
	}
	UE_LOG(LogOKInteractionPair, Display, TEXT("Floor %s (%d meshes)"), bVisible ? TEXT("visible") : TEXT("hidden"), Changed);
}

static FAutoConsoleCommandWithWorldAndArgs GOKInteractionPairBackgroundCommand(
	TEXT("ok.Interaction.Background"),
	TEXT("ok.Interaction.Background <gray>: background brightness (linear, 0 = black, 0.5 default)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (AOKInteractionPairGameMode* Mode = World ? World->GetAuthGameMode<AOKInteractionPairGameMode>() : nullptr) Mode->SetBackground(Args.Num() ? FCString::Atof(*Args[0]) : 0.5f);
	}));
static FAutoConsoleCommandWithWorldAndArgs GOKInteractionPairFloorCommand(
	TEXT("ok.Interaction.Floor"),
	TEXT("ok.Interaction.Floor 0|1: show the floor and backdrop meshes (hidden by default)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (AOKInteractionPairGameMode* Mode = World ? World->GetAuthGameMode<AOKInteractionPairGameMode>() : nullptr) Mode->SetFloorVisible(Args.Num() && FCString::ToBool(*Args[0]));
	}));

static FAutoConsoleCommandWithWorldAndArgs GOKInteractionPairCameraOrbitCommand(
	TEXT("ok.Interaction.CameraOrbit"),
	TEXT("ok.Interaction.CameraOrbit <dx> <dy>: orbit the camera like a left-button mouse drag of that many pixels (for remote checks)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AOKInteractionPairGameMode* Mode = World ? World->GetAuthGameMode<AOKInteractionPairGameMode>() : nullptr;
		if (Mode) Mode->OrbitCamera(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.f, Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.f);
	}));
static FAutoConsoleCommandWithWorldAndArgs GOKInteractionPairCameraMouseCommand(
	TEXT("ok.Interaction.CameraMouse"),
	TEXT("ok.Interaction.CameraMouse <buttons> <dx> <dy> [wheel]: a mouse drag with these buttons (1 left orbit, 2 right zoom/height, 4 middle pan, 8 frame the pair first, 16 look at her hips from <dx> cm, 32 at her head) and wheel notches (for remote checks)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AOKInteractionPairGameMode* Mode = World ? World->GetAuthGameMode<AOKInteractionPairGameMode>() : nullptr;
		if (Mode) Mode->CameraMouse(uint8(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0), Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.f,
			Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.f, Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.f);
	}));
void AOKInteractionPairGameMode::CameraMouse(uint8 Buttons, float DeltaX, float DeltaY, float WheelNotches)
{
	AOKInteractionMode* Mode = ActiveMode.Get();
	UOKCharacterController* C = Mode ? Mode->Controller.Get() : nullptr;
	if (!C || !C->PreviewActor) return;
	// Buttons 8: frame both characters first (the orbit then starts from them).
	if ((Buttons & 8) && C->PartnerActor) C->PreviewActor->FramePairedPose(C->PartnerActor);
	// Buttons 16: look at the heroine's hips from DeltaX cm (default 80), keeping the view direction; 32: at her head.
	if ((Buttons & (16 | 32)) && C->PreviewActor->StudioCam)
	{
		const FName Bone = (Buttons & 32) ? FName(TEXT("cf_j_head")) : FName(TEXT("cf_j_waist02"));
		TArray<USkinnedMeshComponent*> Meshes;
		C->PreviewActor->GetComponents(Meshes);
		for (USkinnedMeshComponent* Mesh : Meshes)
		{
			if (!Mesh || Mesh->GetBoneIndex(Bone) == INDEX_NONE) continue;
			const FVector Focus = Mesh->GetSocketLocation(Bone);
			const FVector Dir = (C->PreviewActor->StudioCam->GetActorLocation() - Focus).GetSafeNormal();
			C->PreviewActor->ResetOrbit(Focus, Focus + Dir * (DeltaX > 0.f ? DeltaX : 80.f));
			C->PreviewActor->ApplyOrbitCamera();
			UE_LOG(LogOKInteractionPair, Display, TEXT("CameraMouse hips focus %s"), *Focus.ToString());
			return;
		}
	}
	if (Buttons & 7) C->PreviewActor->OrbitMouse(Buttons & 7, DeltaX, DeltaY);
	if (WheelNotches != 0.f) C->PreviewActor->OrbitZoom(WheelNotches);
}
void AOKInteractionPairGameMode::OrbitCamera(float DeltaX, float DeltaY)
{
	AOKInteractionMode* Mode = ActiveMode.Get();
	UOKCharacterController* C = Mode ? Mode->Controller.Get() : nullptr;
	if (C && C->PreviewActor) C->PreviewActor->OrbitDrag(DeltaX, DeltaY);
}
