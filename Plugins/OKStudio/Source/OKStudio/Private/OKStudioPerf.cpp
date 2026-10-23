#include "OKStudioGame.h"

#include "OKSceneActor.h"
#include "OKSceneDocument.h"
#include "KKPreviewActor.h"
#include "KKOkAnimation.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RenderCore.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/** Percentile of sorted values (0..1). */
	double Percentile(const TArray<double>& Sorted, double P)
	{
		if (Sorted.IsEmpty()) return 0.;
		return Sorted[FMath::Clamp(int32(P * (Sorted.Num() - 1) + .5), 0, Sorted.Num() - 1)];
	}

	double Mean(const TArray<double>& Values)
	{
		double Sum = 0.;
		for (double V : Values) Sum += V;
		return Values.IsEmpty() ? 0. : Sum / Values.Num();
	}
}

void UOKStudioGame::RunKStudioPerf(const TArray<FString>& Scenes, int32 SettleFrames, float Seconds, bool bExit)
{
	// Each scene loaded as a player opens it (animations playing, panel shown), then once its runtime materials are
	// compiled and `SettleFrames` more frames have passed, `Seconds` of frames measured with no frame cap and no vsync:
	// the frame time, the game / render thread and GPU times (what `stat unit` shows), and the scene's draw counts.
	// Report: Saved/OKStudio/perf/<-OKStudioPerfTag= or time>.json and a log line per scene.
	struct FFrame { double Frame, Game, Render, Gpu; int32 Draws, Prims; int32 Ab = -1; };
	struct FState
	{
		int32 Index = -1, Frame = 0;
		bool bLoaded = false, bMeasuring = false;
		double LoadSeconds = 0., MeasureStart = 0.;
		int32 AbFrames = 0, AbValue = 0, AbSince = 0, AbOriginal = 0;
		TArray<FFrame> Frames;
		TArray<TSharedPtr<FJsonValue>> Rows;
	};
	TSharedRef<FState> State = MakeShared<FState>();
	for (const TCHAR* Command : { TEXT("t.MaxFPS 0"), TEXT("r.VSync 0") }) if (GEngine) GEngine->Exec(GetTickableGameObjectWorld(), Command);
	FString Tag;
	if (!FParse::Value(FCommandLine::Get(), TEXT("OKStudioPerfTag="), Tag) || Tag.IsEmpty()) Tag = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString Out = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OKStudio/perf") / (Tag + TEXT(".json")));
	TWeakObjectPtr<UOKStudioGame> Self(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, State, Scenes, SettleFrames, Seconds, bExit, Out](float)
	{
		if (!Self.IsValid()) { if (bExit) FPlatformMisc::RequestExit(false); return false; }
		if (State->bLoaded && !State->bMeasuring)
		{
			++State->Frame;
			// Runtime materials still compiling are not the steady state a player sees (at most 20 x the frames asked).
			if (State->Frame < SettleFrames || (Self->MaterialsPending() && State->Frame < SettleFrames * 20)) return true;
			State->bMeasuring = true;
			State->MeasureStart = FPlatformTime::Seconds();
			// OK_STUDIO_PERF_DUMP=<frames>: the engine's hierarchical stat averages over that many frames into the log.
			const int32 Dump = FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_PERF_DUMP")));
			if (Dump > 0 && GEngine) GEngine->Exec(Self->GetTickableGameObjectWorld(), *FString::Printf(TEXT("stat dumpave -num=%d -ms=0.1"), Dump));
			// OK_STUDIO_PERF_SHOT=1: a screenshot of the game viewport itself (what a player sees, unlike CompareKStudio's
			// own scene capture) into Saved/OKStudio/perf/<tag>_<scene>.png.
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_PERF_SHOT")) == TEXT("1"))
			{
				FString Tag = TEXT("perf");
				FParse::Value(FCommandLine::Get(), TEXT("OKStudioPerfTag="), Tag);
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("OKStudio/perf") / FString::Printf(TEXT("%s_%s.png"), *Tag, *FPaths::GetBaseFilename(Scenes[State->Index])), false, false);
			}
			State->Frames.Reset();
			FOkAnimBridge::Profile(true); // OK playback's native phase timings from here
			return true;
		}
		if (State->bMeasuring)
		{
			// The engine's last completed frame (FStatUnitData's sources).
			FFrame F;
			F.Frame = FApp::GetDeltaTime() * 1000.;
			F.Game = FPlatformTime::ToMilliseconds(GGameThreadTime);
			F.Render = FPlatformTime::ToMilliseconds(GRenderThreadTime);
			F.Gpu = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
			F.Draws = GNumDrawCallsRHI[0];
			F.Prims = GNumPrimitivesDrawnRHI[0];
			// OK_STUDIO_PERF_AB=<console variable>: that variable flips between 0 and 1 every 20 frames while measuring and
			// the frames are kept apart by its value (each frame measures the one before it, so the first frames after a
			// flip are left out): an A/B comparison under the same outside load.
			static const FString AbName = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STUDIO_PERF_AB"));
			IConsoleVariable* Ab = AbName.IsEmpty() ? nullptr : IConsoleManager::Get().FindConsoleVariable(*AbName);
			if (Ab)
			{
				if (State->AbFrames++ == 0) { State->AbOriginal = Ab->GetInt(); State->AbValue = 0; State->AbSince = 0; Ab->Set(0, ECVF_SetByCode); }
				else if (++State->AbSince >= 20) { State->AbValue ^= 1; State->AbSince = 0; Ab->Set(State->AbValue, ECVF_SetByCode); }
				F.Ab = State->AbSince >= 3 ? State->AbValue : -2;
			}
			State->Frames.Add(F);
			if (FPlatformTime::Seconds() - State->MeasureStart < Seconds) return true;
			if (Ab)
			{
				Ab->Set(State->AbOriginal, ECVF_SetByCode);
				State->AbFrames = 0;
				double Sum[2][4] = {};
				int32 Count[2] = {};
				for (const FFrame& X : State->Frames)
					if (X.Ab == 0 || X.Ab == 1) { Sum[X.Ab][0] += X.Frame; Sum[X.Ab][1] += X.Game; Sum[X.Ab][2] += X.Render; Sum[X.Ab][3] += X.Gpu; ++Count[X.Ab]; }
				for (int32 V = 0; V < 2; ++V)
					if (Count[V])
						UE_LOG(LogTemp, Display, TEXT("OK studio perf: AB %s=%d %s fps=%.1f frame=%.2f game=%.2f render=%.2f gpu=%.2f ms (%d frames)"), *AbName, V,
							*FPaths::GetCleanFilename(Scenes[State->Index]), 1000. * Count[V] / Sum[V][0], Sum[V][0] / Count[V], Sum[V][1] / Count[V],
							Sum[V][2] / Count[V], Sum[V][3] / Count[V], Count[V]);
			}
			// Skip the first frames of the window (the switch from settling).
			TArray<double> Frame, Game, Render, Gpu;
			int64 Draws = 0, Prims = 0;
			for (int32 I = FMath::Min(5, State->Frames.Num() / 4); I < State->Frames.Num(); ++I)
			{
				const FFrame& X = State->Frames[I];
				Frame.Add(X.Frame); Game.Add(X.Game); Render.Add(X.Render); Gpu.Add(X.Gpu);
				Draws += X.Draws; Prims += X.Prims;
			}
			const int32 N = FMath::Max(1, Frame.Num());
			for (TArray<double>* A : { &Frame, &Game, &Render, &Gpu }) A->Sort();
			int32 Previews = 0, Skinned = 0, Static = 0, Objects = Self->Document ? Self->Document->Objects.Num() : 0;
			if (Self->SceneActor && Self->Document)
				for (const FOKSceneObject& O : Self->Document->Objects)
					if (const AKKPreviewActor* P = Self->SceneActor->PreviewOf(O.Id))
					{
						++Previews;
						TInlineComponentArray<UPrimitiveComponent*> Prims2(P);
						for (const UPrimitiveComponent* C : Prims2)
						{
							if (!C->IsVisible()) continue;
							if (C->IsA<USkinnedMeshComponent>()) ++Skinned;
							else if (C->IsA<UStaticMeshComponent>()) ++Static;
						}
					}
			// Every preview actor of the world, the scene's or not, and which of them tick on their own.
			int32 WorldPreviews = 0, Ticking = 0;
			FString Outside;
			TSet<const AActor*> SceneOwned;
			if (Self->SceneActor) for (AActor* A : Self->SceneActor->ObjectPreviews()) SceneOwned.Add(A);
			if (UWorld* World = Self->GetTickableGameObjectWorld())
				for (TActorIterator<AKKPreviewActor> It(World); It; ++It)
				{
					++WorldPreviews;
					if (It->IsActorTickEnabled()) ++Ticking;
					if (!SceneOwned.Contains(*It)) Outside += FString::Printf(TEXT("%s(tick=%d hidden=%d) "), *It->GetName(), It->IsActorTickEnabled() ? 1 : 0, It->IsHidden() ? 1 : 0);
				}
			UE_LOG(LogTemp, Display, TEXT("OK studio perf: world previews=%d ticking=%d outside the scene: %s"), WorldPreviews, Ticking, *Outside);
			// OK playback's native phases over the measured frames: {phase: [total microseconds, calls]}.
			UE_LOG(LogTemp, Display, TEXT("OK studio perf: native phases %s"), *FOkAnimBridge::Profile(true));
			// Each character's native solve (SolveScenePose) since it was loaded, and its parts.
			if (Self->SceneActor && Self->Document)
				for (const FOKSceneObject& O : Self->Document->Objects)
					if (AKKPreviewActor* P = O.Kind == EOKSceneObjectKind::Character ? Self->SceneActor->PreviewOf(O.Id) : nullptr)
						UE_LOG(LogTemp, Display, TEXT("OK studio perf: character %s solve=%.2f ms over %d frames, parts=%d pose_mode=%d"), *O.Name,
							P->SceneSolveSamples ? P->SceneSolveTotalMs / P->SceneSolveSamples : 0., P->SceneSolveSamples, P->Session.PartCount(), P->PoseMode);
			const FString Name = FPaths::GetCleanFilename(Scenes[State->Index]);
			auto Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("scene"), Name);
			Row->SetNumberField(TEXT("load_seconds"), State->LoadSeconds);
			Row->SetNumberField(TEXT("frames"), N);
			Row->SetNumberField(TEXT("fps"), 1000. / FMath::Max(.001, Mean(Frame)));
			auto Stat = [&Row](const TCHAR* Key, const TArray<double>& V)
			{
				Row->SetNumberField(FString(Key) + TEXT("_mean_ms"), Mean(V));
				Row->SetNumberField(FString(Key) + TEXT("_p95_ms"), Percentile(V, .95));
			};
			Stat(TEXT("frame"), Frame); Stat(TEXT("game"), Game); Stat(TEXT("render"), Render); Stat(TEXT("gpu"), Gpu);
			Row->SetNumberField(TEXT("draws"), double(Draws) / N);
			Row->SetNumberField(TEXT("primitives"), double(Prims) / N);
			Row->SetNumberField(TEXT("objects"), Objects);
			Row->SetNumberField(TEXT("previews"), Previews);
			Row->SetNumberField(TEXT("skinned_components"), Skinned);
			Row->SetNumberField(TEXT("static_components"), Static);
			State->Rows.Add(MakeShared<FJsonValueObject>(Row));
			UE_LOG(LogTemp, Display, TEXT("OK studio perf: %s fps=%.1f frame=%.2f/%.2f game=%.2f render=%.2f gpu=%.2f ms (mean/p95, mean) draws=%.0f prims=%.0f objects=%d previews=%d skinned=%d static=%d load=%.1fs"),
				*Name, 1000. / FMath::Max(.001, Mean(Frame)), Mean(Frame), Percentile(Frame, .95), Mean(Game), Mean(Render), Mean(Gpu), double(Draws) / N, double(Prims) / N,
				Objects, Previews, Skinned, Static, State->LoadSeconds);
			State->bMeasuring = false;
			State->bLoaded = false;
		}
		if (++State->Index >= Scenes.Num())
		{
			FString Json;
			FJsonSerializer::Serialize(State->Rows, TJsonWriterFactory<>::Create(&Json));
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Out), true);
			FFileHelper::SaveStringToFile(Json, *Out, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogTemp, Display, TEXT("OK studio perf: RESULT %d / %d scenes measured into %s"), State->Rows.Num(), Scenes.Num(), *Out);
			if (bExit) { Self->ReleaseSceneResources(); FPlatformMisc::RequestExit(false); }
			return false;
		}
		FString Error;
		const double Start = FPlatformTime::Seconds();
		State->bLoaded = Self->Load(Scenes[State->Index], Error);
		State->LoadSeconds = FPlatformTime::Seconds() - Start;
		State->Frame = 0;
		if (!State->bLoaded) UE_LOG(LogTemp, Warning, TEXT("OK studio perf: %s not opened: %s"), *Scenes[State->Index], *Error);
		return true;
	}));
}

namespace
{
	FAutoConsoleCommand PerfKStudioCommand(TEXT("OKStudio.PerfKStudio"),
		TEXT("OKStudio.PerfKStudio <folder|samples> <count> [settle frames] [seconds] [exit]: frame, game, render and GPU times of an evenly spread sample of KStudio scenes once loaded (Saved/OKStudio/perf; -OKStudioPerfTag=<name>)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2) { UE_LOG(LogTemp, Error, TEXT("OKStudio.PerfKStudio: folder and count required")); return; }
			const bool bExit = Args.Contains(TEXT("exit"));
			const FString Folder = Args[0].Equals(TEXT("samples"), ESearchCase::IgnoreCase) ? AOKSceneActor::LibraryDirectory() / TEXT("studio/samples") : Args[0];
			TArray<FString> Names, More;
			IFileManager::Get().FindFiles(Names, *(Folder / TEXT("*.png")), true, false);
			IFileManager::Get().FindFiles(More, *(Folder / TEXT("*.okscene")), true, false);
			Names.Append(More);
			Names.Sort();
			const int32 Count = FMath::Clamp(FCString::Atoi(*Args[1]), 1, FMath::Max(1, Names.Num()));
			TArray<FString> Scenes;
			for (int32 I = 0; I < Count && Names.Num(); ++I) Scenes.Add(Folder / Names[int64(I) * Names.Num() / Count]);
			const int32 Settle = Args.Num() > 2 && Args[2].IsNumeric() ? FMath::Max(1, FCString::Atoi(*Args[2])) : 120;
			const float Seconds = Args.Num() > 3 && Args[3].IsNumeric() ? FMath::Max(1.f, FCString::Atof(*Args[3])) : 5.f;
			TSharedRef<int32> Frames = MakeShared<int32>(0);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bExit, Frames, Scenes, Settle, Seconds](float)
			{
				if (++*Frames < 30) return true;
				UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
				FString Error;
				// The studio game mode (SceneEditor.cmd) runs its own studio and quits when it stops: measure in that one.
				UOKStudioGame* Game = UOKStudioGame::Get();
				if (!Game) Game = UOKStudioGame::Start(World, FString(), Error);
				if (Game) Game->RunKStudioPerf(Scenes, Settle, Seconds, bExit);
				else { UE_LOG(LogTemp, Error, TEXT("OK studio perf: %s"), *Error); if (bExit) FPlatformMisc::RequestExit(false); }
				return false;
			}), 0.1f);
		}));
}
