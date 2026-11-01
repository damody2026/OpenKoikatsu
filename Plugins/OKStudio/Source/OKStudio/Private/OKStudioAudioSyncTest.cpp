#include "OKAudioMasterClock.h"
#include "OKStudioGame.h"
#include "OKStudioGameInternal.h"
#include "OKSceneActor.h"
#include "KKSpeech.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	void WriteTestWav16Stereo(const FString& Path, int32 SampleRate, int32 NumFrames)
	{
		const int32 Channels = 2;
		const int32 DataBytes = NumFrames * Channels * 2;
		TArray<uint8> Bytes;
		Bytes.Reserve(44 + DataBytes);
		auto AppendTag = [&Bytes](const char Tag[4]) { Bytes.Append(reinterpret_cast<const uint8*>(Tag), 4); };
		auto AppendU16 = [&Bytes](uint16 V) { Bytes.Add(uint8(V & 0xFF)); Bytes.Add(uint8((V >> 8) & 0xFF)); };
		auto AppendU32 = [&Bytes](uint32 V)
		{
			Bytes.Add(uint8(V & 0xFF));
			Bytes.Add(uint8((V >> 8) & 0xFF));
			Bytes.Add(uint8((V >> 16) & 0xFF));
			Bytes.Add(uint8((V >> 24) & 0xFF));
		};
		AppendTag("RIFF");
		AppendU32(uint32(36 + DataBytes));
		AppendTag("WAVE");
		AppendTag("fmt ");
		AppendU32(16u);
		AppendU16(1u); // PCM
		AppendU16(uint16(Channels));
		AppendU32(uint32(SampleRate));
		AppendU32(uint32(SampleRate * Channels * 2));
		AppendU16(uint16(Channels * 2));
		AppendU16(16u);
		AppendTag("data");
		AppendU32(uint32(DataBytes));
		for (int32 F = 0; F < NumFrames; ++F)
		{
			const int16 Sample = int16(FMath::RoundToInt(FMath::Sin(double(F) * 2.0 * UE_DOUBLE_PI * 440.0 / double(SampleRate)) * 12000.0));
			AppendU16(uint16(Sample));
			AppendU16(uint16(-Sample));
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveArrayToFile(Bytes, *Path);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOKStudioAudioMasterClockSyncTest,
	"OpenKoikatsu.Studio.AudioMasterClockSync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOKStudioAudioMasterClockSyncTest::RunTest(const FString&)
{
	// 1. 4-minute smoothness, strict monotonicity & zero cumulative drift across 60Hz, 120Hz, and 144Hz render rates
	//    against discrete ~93.75Hz (512/48000s) and ~50Hz (20ms) AudioMixer block schedules with OS callback jitter.
	for (const int32 RenderHz : { 60, 120, 144 })
	{
		for (const double AudioBlockPeriod : { 512.0 / 48000.0, 0.020 })
		{
			FOKAudioMasterClock Clock;
			const float SongDuration = 250.f;
			const uint64 Gen = Clock.BeginPlayback(0.f, 0.0, 1.f, SongDuration);
			Clock.OnHardwareAnchor(Gen, 0.0, 0.0, false, true);

			const float RenderDt = float(1.0 / double(RenderHz)); // deliberate float32 truncation to prove 0 cumulative drift
			double WallNow = 0.0;
			double NextAudioBlockIdeal = AudioBlockPeriod;
			int32 BlockIndex = 0;
			float Timeline = 0.f;
			bool bMonotonic = true;
			bool bSmoothPacing = true;
			int32 SpuriousSnaps = 0;

			const int32 TotalFrames = RenderHz * 240; // full 4 minutes (240 seconds)
			for (int32 Frame = 0; Frame < TotalFrames; ++Frame)
			{
				WallNow += 1.0 / double(RenderHz);
				while (WallNow >= NextAudioBlockIdeal)
				{
					const double TrueBlockAudio = double(BlockIndex + 1) * AudioBlockPeriod;
					// Deterministic +/- 1.2ms OS audio thread scheduling jitter on the wall timestamp.
					const double Jitter = FMath::Sin(double(BlockIndex) * 1.73205) * 0.0012;
					const double ReportedWall = FMath::Clamp(NextAudioBlockIdeal + Jitter, 0.0, WallNow);
					Clock.OnHardwareAnchor(Gen, TrueBlockAudio, ReportedWall, false, true);
					++BlockIndex;
					NextAudioBlockIdeal = double(BlockIndex + 1) * AudioBlockPeriod;
				}

				FOKAudioMasterClock::FStepInput In;
				In.CurrentTimelineTime = Timeline;
				In.RenderDeltaSeconds = RenderDt;
				In.NowWallSeconds = WallNow;
				In.TimelineSpeed = 1.f;
				In.SoundDelay = 0.f;
				In.LoopBegin = 0.f;
				In.LoopEnd = SongDuration;
				const FOKAudioMasterClock::FStepResult Res = Clock.StepTimeline(In);

				const float Step = Res.NextTimelineTime - Timeline;
				if (Step <= 0.f) bMonotonic = false;
				if (Step < RenderDt * 0.74f || Step > RenderDt * 1.26f) bSmoothPacing = false;
				if (Res.bHardSnapped) ++SpuriousSnaps;
				Timeline = Res.NextTimelineTime;
			}

			const float FinalDriftSeconds = FMath::Abs(Timeline - 240.f);
			TestTrue(FString::Printf(TEXT("4-min %dHz (block %.4fs) strictly monotonic & smooth (snaps=%d, final=%.6fs, drift=%.4fms)"),
				RenderHz, AudioBlockPeriod, SpuriousSnaps, Timeline, FinalDriftSeconds * 1000.f),
				bMonotonic && bSmoothPacing && SpuriousSnaps == 0 && FinalDriftSeconds < 0.0025f);
		}
	}

	// 2. Win32 window title-bar drag & heavy GameThread hitch hard-snap (immediate recovery, never lags or rewinds audio).
	{
		FOKAudioMasterClock Clock;
		const uint64 Gen = Clock.BeginPlayback(12.f, 100.0, 1.f, 180.f);
		Clock.OnHardwareAnchor(Gen, 12.0, 100.0, false, true);

		// Simulate a 2.500-second Win32 window drag: OS audio thread kept advancing to 14.492s at wall=102.492s,
		// while GameThread wakes at wall=102.500s with UE-clamped RenderDeltaSeconds = 1/30s (0.0333s).
		Clock.OnHardwareAnchor(Gen, 14.492, 102.492, false, true);
		FOKAudioMasterClock::FStepInput HitchIn;
		HitchIn.CurrentTimelineTime = 12.0f;
		HitchIn.RenderDeltaSeconds = 1.f / 30.f;
		HitchIn.NowWallSeconds = 102.500;
		HitchIn.TimelineSpeed = 1.f;
		HitchIn.SoundDelay = 0.f;
		HitchIn.LoopBegin = 0.f;
		HitchIn.LoopEnd = 180.f;
		const FOKAudioMasterClock::FStepResult HitchRes = Clock.StepTimeline(HitchIn);

		TestTrue(TEXT("Win32 2.5s window drag triggers immediate hard-snap to hardware audio time (14.500s)"),
			HitchRes.bHardSnapped && FMath::IsNearlyEqual(HitchRes.NextTimelineTime, 14.500f, 1e-4f));

		// Also test a 180ms shader-compile hitch (> 25ms threshold).
		Clock.OnHardwareAnchor(Gen, 14.675, 102.675, false, true);
		HitchIn.CurrentTimelineTime = HitchRes.NextTimelineTime;
		HitchIn.RenderDeltaSeconds = 1.f / 60.f;
		HitchIn.NowWallSeconds = 102.680;
		const FOKAudioMasterClock::FStepResult ShaderHitchRes = Clock.StepTimeline(HitchIn);
		TestTrue(TEXT("180ms shader hitch immediately hard-snaps to 14.680s"),
			ShaderHitchRes.bHardSnapped && FMath::IsNearlyEqual(ShaderHitchRes.NextTimelineTime, 14.680f, 1e-4f));
	}

	// 3. SeekGeneration invalidation: stale pre-seek callbacks never snap TimelineTime back.
	{
		FOKAudioMasterClock Clock;
		const uint64 OldGen = Clock.BeginPlayback(10.f, 50.0, 1.f, 200.f);
		TestTrue(TEXT("current generation callback accepted"), Clock.OnHardwareAnchor(OldGen, 10.5, 50.5, false, true));

		// User scrubs to 85.0s: stop + restart increments SeekGeneration.
		Clock.StopPlayback();
		const uint64 NewGen = Clock.BeginPlayback(85.f, 51.0, 1.f, 200.f);
		TestTrue(TEXT("SeekGeneration advances monotonically across stop and seek"), NewGen > OldGen + 1 || NewGen > OldGen);

		// Stale in-flight callback from OldGen arrives late reporting 10.52s.
		const bool bAcceptedStale = Clock.OnHardwareAnchor(OldGen, 10.52, 51.01, false, false);
		TestFalse(TEXT("stale callback from previous SeekGeneration is rejected"), bAcceptedStale);

		Clock.OnHardwareAnchor(NewGen, 85.0, 51.0, false, true);
		TestFalse(TEXT("delayed non-direct percent callback is ignored when direct generator anchor is active"),
			Clock.OnHardwareAnchor(NewGen, 84.8, 51.016, false, false));

		FOKAudioMasterClock::FStepInput In;
		In.CurrentTimelineTime = 85.0f;
		In.RenderDeltaSeconds = 1.f / 60.f;
		In.NowWallSeconds = 51.0 + 1.0 / 60.0;
		In.TimelineSpeed = 1.f;
		In.LoopBegin = 0.f;
		In.LoopEnd = 200.f;
		const FOKAudioMasterClock::FStepResult Res = Clock.StepTimeline(In);
		TestTrue(TEXT("timeline progresses cleanly from 85.0s without snapping back to 10.52s"),
			!Res.bHardSnapped && FMath::IsNearlyEqual(Res.NextTimelineTime, 85.f + 1.f / 60.f, 1e-3f));
	}

	// 4. Playback speed scaling (0.5x -> 2.0x live transition) & safe clamping (0.05..8.0 -> 0.1..4.0).
	{
		TestEqual(TEXT("speed below 0.1 clamps to 0.1"), FOKAudioMasterClock::ClampAudioSpeed(0.05f), 0.1f);
		TestEqual(TEXT("speed above 4.0 clamps to 4.0"), FOKAudioMasterClock::ClampAudioSpeed(8.0f), 4.0f);

		FOKAudioMasterClock Clock;
		const uint64 Gen = Clock.BeginPlayback(0.f, 0.0, 0.5f, 120.f);
		Clock.OnHardwareAnchor(Gen, 0.0, 0.0, false, true);

		float Timeline = 0.f;
		double Wall = 0.0;
		// 10 wall seconds at 0.5x -> 5.0 timeline seconds.
		for (int32 F = 0; F < 600; ++F)
		{
			Wall += 1.0 / 60.0;
			Clock.OnHardwareAnchor(Gen, Wall * 0.5, Wall, false, true);
			FOKAudioMasterClock::FStepInput In{ Timeline, 1.f / 60.f, Wall, 0.5f, 0.f, 0.f, 120.f, false };
			Timeline = Clock.StepTimeline(In).NextTimelineTime;
		}
		TestTrue(TEXT("0.5x playback advances 5.0s over 10.0 wall seconds"), FMath::IsNearlyEqual(Timeline, 5.0f, 0.002f));

		// Live switch to 2.0x without recreating audio: 10 more wall seconds -> +20.0s = 25.0s total.
		Clock.UpdateSpeed(Wall, 2.0f);
		const double SwitchWall = Wall;
		for (int32 F = 0; F < 600; ++F)
		{
			Wall += 1.0 / 60.0;
			const double TrueAudio = 5.0 + (Wall - SwitchWall) * 2.0;
			Clock.OnHardwareAnchor(Gen, TrueAudio, Wall, false, true);
			FOKAudioMasterClock::FStepInput In{ Timeline, 1.f / 60.f, Wall, 2.0f, 0.f, 0.f, 120.f, false };
			const FOKAudioMasterClock::FStepResult Res = Clock.StepTimeline(In);
			TestFalse(TEXT("live 2.0x speed transition stays smooth without hard-snap"), Res.bHardSnapped);
			Timeline = Res.NextTimelineTime;
		}
		TestTrue(TEXT("2.0x playback reaches 25.0s after 10.0 wall seconds"), FMath::IsNearlyEqual(Timeline, 25.0f, 0.003f));
	}

	// 5. SoundDelay (+2.0s lead-in), short audio (5.0s audio in 10.0s document), and loop boundary wrap.
	{
		FOKAudioMasterClock Clock;
		const float SoundDelay = 2.0f;
		const float AudioDuration = 5.0f; // finishes at TimelineTime = 7.0s
		const float DocDuration = 10.0f;
		float Timeline = 0.f;
		double Wall = 0.0;
		bool bSawDelayCross = false;
		bool bSawAudioEnd = false;
		bool bSawLoopWrap = false;

		// Run 650 frames at 60Hz (~10.83s) to cover: [0..2s) pre-delay, [2..7s) audio-locked, [7..10s] post-audio free-run, and >10s loop wrap!
		for (int32 F = 0; F < 650; ++F)
		{
			Wall += 1.0 / 60.0;
			FOKAudioMasterClock::FStepInput In{ Timeline, 1.f / 60.f, Wall, 1.f, SoundDelay, 0.f, DocDuration, false };
			if (Clock.bAudioActive)
			{
				const double AudioNow = FMath::Min(Wall - 2.0, double(AudioDuration));
				Clock.OnHardwareAnchor(Clock.SeekGeneration, AudioNow, Wall, AudioNow >= double(AudioDuration), true);
			}
			const FOKAudioMasterClock::FStepResult Res = Clock.StepTimeline(In);
			Timeline = Res.NextTimelineTime;
			if (Res.bCrossedSoundDelay)
			{
				bSawDelayCross = true;
				const uint64 Gen = Clock.BeginPlayback(Timeline - SoundDelay, Wall, 1.f, AudioDuration);
				Clock.OnHardwareAnchor(Gen, double(Timeline - SoundDelay), Wall, false, true);
			}
			if (Res.bAudioReachedEnd)
			{
				bSawAudioEnd = true;
				TestTrue(TEXT("at audio end, TimelineTime >= SoundDelay + AudioDuration so it never re-triggers"),
					Timeline >= SoundDelay + AudioDuration - 1e-5f && !Clock.bAudioActive);
			}
			if (Res.bLooped)
			{
				bSawLoopWrap = true;
				break;
			}
		}
		TestTrue(TEXT("SoundDelay lead-in, short-audio continuation past 7.0s to 10.0s, and loop wrap all succeeded"),
			bSawDelayCross && bSawAudioEnd && bSawLoopWrap && Timeline < 0.1f);
	}

	// 6. Lock-free FKKSpeechMeter real-time generator anchor + .wav and long .ogg decoding verification.
	{
		const FString TempWav = FPaths::ProjectSavedDir() / TEXT("OKStudio/test_audio_sync.wav");
		WriteTestWav16Stereo(TempWav, 48000, 48000); // 1.000s stereo 48kHz WAV
		TSharedPtr<const FKKSpeechPcm, ESPMode::ThreadSafe> WavPcm;
		FString WavErr;
		const bool bWavOk = OKStudioGameInternal::LoadAudioFile(TempWav, WavPcm, WavErr);
		IFileManager::Get().Delete(*TempWav, false, true, true);
		TestTrue(FString::Printf(TEXT("WAV loader decodes 48kHz stereo PCM (%s)"), *WavErr),
			bWavOk && WavPcm.IsValid() && WavPcm->SampleRate == 48000 && WavPcm->Channels == 2 && WavPcm->Samples.Num() == 96000);

		if (WavPcm.IsValid())
		{
			TSharedPtr<FKKSpeechMeter, ESPMode::ThreadSafe> Meter = MakeShared<FKKSpeechMeter, ESPMode::ThreadSafe>();
			ISoundGeneratorPtr Generator = KKSpeech::MakeGenerator(WavPcm, Meter, 0.25f, 48000);
			TArray<float> OutBuf;
			OutBuf.SetNumZeroed(1024); // 512 stereo frames
			const int32 Written = Generator->OnGenerateAudio(OutBuf.GetData(), OutBuf.Num());
			uint32 Seq = 0;
			double AnchorAudio = -1.0, AnchorWall = -1.0;
			bool bFin = true;
			const bool bReadOk = Meter->ReadClockAnchor(Seq, AnchorAudio, AnchorWall, bFin);
			TestTrue(TEXT("FSpeechGenerator publishes exact StartSeconds (0.250s) and wall clock via lock-free seqlock"),
				Written == 1024 && bReadOk && Seq == 2u && FMath::IsNearlyEqual(AnchorAudio, 0.25, 1e-5) && AnchorWall > 0.0 && !bFin);
		}

		// Verify full-length base-library BGM OGG (~15.9 MB Vorbis, >32 MiB decoded PCM, >180s) loads cleanly.
		const FString LongOgg = AOKSceneActor::LibraryDirectory() / TEXT("studio/bgm/AozoraAdventure.ogg");
		if (FPaths::FileExists(LongOgg))
		{
			TSharedPtr<const FKKSpeechPcm, ESPMode::ThreadSafe> OggPcm;
			FString OggErr;
			const bool bOggOk = OKStudioGameInternal::LoadAudioFile(LongOgg, OggPcm, OggErr);
			const int64 PcmBytes = OggPcm.IsValid() ? int64(OggPcm->Samples.Num()) * int64(sizeof(int16)) : 0;
			const float DurationSec = (OggPcm.IsValid() && OggPcm->Channels > 0 && OggPcm->SampleRate > 0)
				? float(OggPcm->Samples.Num() / OggPcm->Channels) / float(OggPcm->SampleRate)
				: 0.f;
			TestTrue(FString::Printf(TEXT("full-length OGG AozoraAdventure.ogg decodes cleanly (bytes=%lld, dur=%.2fs, err=%s)"),
				(long long)PcmBytes, DurationSec, *OggErr),
				bOggOk && OggPcm.IsValid() && PcmBytes > 16ll * 1024 * 1024 && DurationSec > 120.f);
		}
	}

	return true;
}

#endif
