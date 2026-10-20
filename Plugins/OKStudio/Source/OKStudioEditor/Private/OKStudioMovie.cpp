#include "OKStudioMovie.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "LevelSequence.h"
#include "Misc/Paths.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelinePIEExecutor.h"
#include "MoviePipelinePrimaryConfig.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineQueueSubsystem.h"
#include "OKStudioGame.h"

namespace
{
	UMoviePipelineQueueSubsystem* Queues() { return GEditor ? GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>() : nullptr; }
}

bool FOKStudioMovie::IsRendering()
{
	const UMoviePipelineQueueSubsystem* Q = Queues();
	return Q && Q->IsRendering();
}

bool FOKStudioMovie::HasGpuSkinCache()
{
	const IConsoleVariable* Compile = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkinCache.CompileShaders"));
	const IConsoleVariable* Mode = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkinCache.Mode"));
	return Compile && Compile->GetInt() != 0 && Mode && Mode->GetInt() != 0;
}

bool FOKStudioMovie::Render(ULevelSequence* Sequence, int32 Width, int32 Height, int32 TemporalSamples, TFunction<void(const FString& Movie, const FString& Error)> Done, FString& OutError)
{
	// Without the GPU skin cache, OK characters' morph targets are double buffered and the engine stops on a morph
	// buffer check during the render (SkeletalRenderGPUSkin.cpp: check(Index == 1)). Docs/OK_STUDIO_PLAN.md R3.
	if (!HasGpuSkinCache())
	{
		OutError = TEXT("Movie Render Queue needs the GPU skin cache (r.SkinCache.CompileShaders=True, r.SkinCache.Mode=1 in Config/DefaultEngine.ini; see Docs/OK_STUDIO_PLAN.md R3). Use the game's mp4 export meanwhile.");
		return false;
	}
	UMoviePipelineQueueSubsystem* Q = Queues();
	if (!Q || !Sequence) { OutError = TEXT("no Movie Render Queue / sequence"); return false; }
	if (Q->IsRendering()) { OutError = TEXT("Movie Render Queue is already rendering"); return false; }
	if (!FPaths::FileExists(UOKStudioGame::FfmpegPath())) { OutError = TEXT("Tools/ffmpeg/ffmpeg.exe not found (third-party tool, not versioned)"); return false; }

	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UserData/Studio/movie") / (Sequence->GetName() + FDateTime::Now().ToString(TEXT("_mrq_%Y%m%d_%H%M%S"))));
	UMoviePipelineQueue* Queue = Q->GetQueue();
	Queue->DeleteAllJobs();
	UMoviePipelineExecutorJob* Job = Queue->AllocateNewJob(UMoviePipelineExecutorJob::StaticClass());
	Job->SetSequence(FSoftObjectPath(Sequence));
	// An empty level: the sequence's scene spawnable brings the map, characters, items, lights and effects.
	Job->Map = FSoftObjectPath(TEXT("/Engine/Maps/Entry.Entry"));
	UMoviePipelinePrimaryConfig* Config = Job->GetConfiguration();
	UMoviePipelineOutputSetting* Output = Cast<UMoviePipelineOutputSetting>(Config->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass()));
	Output->OutputDirectory.Path = Dir;
	Output->OutputResolution = FIntPoint(Width + (Width & 1), Height + (Height & 1));
	Output->FileNameFormat = TEXT("frame_{frame_number}");
	Output->ZeroPadFrameNumbers = 5;
	Config->FindOrAddSettingByClass(UMoviePipelineDeferredPassBase::StaticClass());
	Config->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_PNG::StaticClass());
	UMoviePipelineAntiAliasingSetting* AntiAliasing = Cast<UMoviePipelineAntiAliasingSetting>(Config->FindOrAddSettingByClass(UMoviePipelineAntiAliasingSetting::StaticClass()));
	AntiAliasing->SpatialSampleCount = 1;
	AntiAliasing->TemporalSampleCount = FMath::Max(1, TemporalSamples);
	// A few rendered warm-up frames before the first one (temporal history, effects).
	AntiAliasing->bRenderWarmUpFrames = true;
	AntiAliasing->RenderWarmUpCount = 4;

	UMoviePipelineExecutorBase* Executor = Q->RenderQueueWithExecutor(UMoviePipelinePIEExecutor::StaticClass());
	if (!Executor) { OutError = TEXT("Movie Render Queue did not start"); return false; }
	Executor->OnExecutorFinished().AddLambda([Dir, Done](UMoviePipelineExecutorBase*, bool bSuccess)
	{
		TArray<FString> Frames;
		IFileManager::Get().FindFiles(Frames, *(Dir / TEXT("frame_*.png")), true, false);
		FString Movie = Dir + TEXT(".mp4"), Encoder, Error;
		if (!bSuccess || Frames.IsEmpty()) { Done(FString(), FString::Printf(TEXT("render failed (%d frames in %s)"), Frames.Num(), *Dir)); return; }
		if (!UOKStudioGame::EncodeMp4(Dir, 30.f, Movie, Encoder, Error)) { Done(FString(), Error); return; }
		IFileManager::Get().DeleteDirectory(*Dir, false, true);
		UE_LOG(LogTemp, Display, TEXT("OK studio: Movie Render Queue %d frames -> %s (%s)"), Frames.Num(), *Movie, *Encoder);
		Done(Movie, FString());
	});
	return true;
}
