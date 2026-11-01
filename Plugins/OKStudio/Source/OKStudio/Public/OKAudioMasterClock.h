#pragma once

#include "CoreMinimal.h"

/**
 * Hitch-proof, phase-locked Audio Master Clock for OK Studio MMDD timeline playback.
 *
 * - Treats hardware audio sample progression as the single authoritative master clock while scene audio is active.
 * - Bridges discrete ~50-100Hz AudioMixer block callbacks with 60/120/144Hz render frames via wall-clock anchor
 *   extrapolation + a bounded monotonic proportional phase-locked loop (PLL) when |Drift| < HardSnapThreshold.
 * - Immediately hard-snaps TimelineTime on GameThread hitches or Win32 modal window-drag freezes (|Drift| >= threshold),
 *   never rewinding audio to match a lagged DeltaTime accumulator.
 * - Guards against stale in-flight audio callbacks across seeks, scrubs, pauses, and loop wraps via SeekGeneration.
 */
struct OKSTUDIO_API FOKAudioMasterClock
{
	/** Wall-clock desync threshold (seconds at 1.0x speed) above which TimelineTime hard-snaps immediately. */
	static constexpr float HardSnapThresholdSeconds = 0.025f;
	/** Proportional PLL convergence rate (1/s) for sub-threshold jitter smoothing (~125ms time constant). */
	static constexpr float PllGainPerSecond = 8.0f;
	/** Supported UE AudioMixer pitch multiplier bounds. */
	static constexpr float MinAudioSpeed = 0.1f;
	static constexpr float MaxAudioSpeed = 4.0f;

	struct FStepInput
	{
		float CurrentTimelineTime = 0.f;
		float RenderDeltaSeconds = 0.f;
		double NowWallSeconds = 0.0;
		float TimelineSpeed = 1.f;
		float SoundDelay = 0.f;
		float LoopBegin = 0.f;
		float LoopEnd = 10.f;
		/** True on the exact tick SceneAudio was freshly spawned so pre-spawn DeltaTime does not skip initial audio. */
		bool bJustStartedThisTick = false;
	};

	struct FStepResult
	{
		float NextTimelineTime = 0.f;
		bool bHardSnapped = false;
		bool bLooped = false;
		bool bCrossedSoundDelay = false;
		bool bAudioReachedEnd = false;
	};

	uint64 SeekGeneration = 0;
	bool bAudioActive = false;
	bool bHasHardwareCallback = false;
	bool bHasDirectGeneratorAnchor = false;
	bool bGeneratorFinished = false;
	double AnchorAudioSeconds = 0.0;
	double AnchorWallSeconds = 0.0;
	float AudioSpeed = 1.f;
	float AudioDuration = 0.f;
	uint32 LastMeterSeq = 0;

	static float ClampAudioSpeed(float InTimelineSpeed)
	{
		return FMath::Clamp(InTimelineSpeed, MinAudioSpeed, MaxAudioSpeed);
	}

	uint64 BeginPlayback(float StartAudioSeconds, double NowWallSeconds, float InTimelineSpeed, float InAudioDuration)
	{
		++SeekGeneration;
		bAudioActive = true;
		bHasHardwareCallback = false;
		bHasDirectGeneratorAnchor = false;
		bGeneratorFinished = false;
		AnchorAudioSeconds = FMath::Max(0.0, double(StartAudioSeconds));
		AnchorWallSeconds = NowWallSeconds;
		AudioSpeed = ClampAudioSpeed(InTimelineSpeed);
		AudioDuration = FMath::Max(0.f, InAudioDuration);
		LastMeterSeq = 0;
		return SeekGeneration;
	}

	uint64 StopPlayback()
	{
		++SeekGeneration;
		bAudioActive = false;
		bHasHardwareCallback = false;
		bHasDirectGeneratorAnchor = false;
		bGeneratorFinished = false;
		LastMeterSeq = 0;
		return SeekGeneration;
	}

	/** Accepts a real-time anchor from FSpeechGenerator::OnGenerateAudio (or fallback OnAudioPlaybackPercentNative).
	 *  Returns false and ignores the callback if SeekGeneration no longer matches (stale pre-seek/pre-loop callback). */
	bool OnHardwareAnchor(uint64 CallbackGeneration, double ReportedAudioSeconds, double ReportedWallSeconds, bool bInFinished = false, bool bDirectGenerator = true)
	{
		if (!bAudioActive || CallbackGeneration != SeekGeneration)
		{
			return false;
		}
		if (!bDirectGenerator && bHasDirectGeneratorAnchor)
		{
			// Direct OS Audio Render Thread anchors carry exact simultaneous wall timestamps; ignore queued GT percent tasks.
			return false;
		}
		if (!FMath::IsFinite(ReportedAudioSeconds) || !FMath::IsFinite(ReportedWallSeconds))
		{
			return false;
		}
		AnchorAudioSeconds = FMath::Clamp(ReportedAudioSeconds, 0.0, double(FMath::Max(AudioDuration, 0.f)));
		AnchorWallSeconds = ReportedWallSeconds;
		bHasHardwareCallback = true;
		if (bDirectGenerator)
		{
			bHasDirectGeneratorAnchor = true;
		}
		if (bInFinished)
		{
			bGeneratorFinished = true;
		}
		return true;
	}

	double EstimateUnclampedAudioSeconds(double NowWallSeconds) const
	{
		if (!bAudioActive) return AnchorAudioSeconds;
		const double ElapsedWall = FMath::Max(0.0, NowWallSeconds - AnchorWallSeconds);
		return AnchorAudioSeconds + ElapsedWall * double(AudioSpeed);
	}

	void UpdateSpeed(double NowWallSeconds, float NewTimelineSpeed)
	{
		const float Clamped = ClampAudioSpeed(NewTimelineSpeed);
		if (FMath::IsNearlyEqual(Clamped, AudioSpeed, 1e-4f)) return;
		if (bAudioActive && bHasHardwareCallback)
		{
			AnchorAudioSeconds = FMath::Clamp(EstimateUnclampedAudioSeconds(NowWallSeconds), 0.0, double(AudioDuration));
			AnchorWallSeconds = NowWallSeconds;
		}
		AudioSpeed = Clamped;
	}

	FStepResult StepTimeline(const FStepInput& In)
	{
		FStepResult Out;
		const float SafeDt = FMath::IsFinite(In.RenderDeltaSeconds) && In.RenderDeltaSeconds > 0.f ? In.RenderDeltaSeconds : 0.f;
		const float FreeSpeed = FMath::IsFinite(In.TimelineSpeed) ? FMath::Max(0.f, In.TimelineSpeed) : 1.f;
		const float Begin = In.LoopBegin;
		const float End = FMath::Max(In.LoopEnd, Begin + 0.01f);

		if (FreeSpeed <= 0.f)
		{
			if (bAudioActive)
			{
				StopPlayback();
			}
			Out.NextTimelineTime = In.CurrentTimelineTime;
			return Out;
		}

		float Next = In.CurrentTimelineTime;

		if (bAudioActive && AudioDuration > 0.f)
		{
			UpdateSpeed(In.NowWallSeconds, FreeSpeed);

			if (In.bJustStartedThisTick)
			{
				// Audio was freshly started at CurrentTimelineTime - SoundDelay on this tick; keep frame 0 aligned.
				Next = In.CurrentTimelineTime;
			}
			else if (!bHasHardwareCallback)
			{
				// Pre-first-callback or headless (-nosound): advance smoothly by RenderDeltaSeconds * AudioSpeed.
				const float Step = SafeDt * AudioSpeed;
				AnchorAudioSeconds += double(Step);
				AnchorWallSeconds = In.NowWallSeconds;
				if (AnchorAudioSeconds >= double(AudioDuration))
				{
					Out.bAudioReachedEnd = true;
					StopPlayback();
				}
				Next = In.CurrentTimelineTime + Step;
			}
			else
			{
				const double UnclampedAudio = EstimateUnclampedAudioSeconds(In.NowWallSeconds);
				if (UnclampedAudio >= double(AudioDuration))
				{
					Out.bAudioReachedEnd = true;
				}
				const float TargetTimelineTime = In.SoundDelay + float(FMath::Max(0.0, UnclampedAudio));
				const float NominalStep = SafeDt * AudioSpeed;
				const float PredictedNext = In.CurrentTimelineTime + NominalStep;
				const float Drift = TargetTimelineTime - PredictedNext;
				const float SnapThreshold = HardSnapThresholdSeconds * FMath::Max(AudioSpeed, MinAudioSpeed);

				if (FMath::Abs(Drift) >= SnapThreshold)
				{
					Next = TargetTimelineTime;
					Out.bHardSnapped = true;
				}
				else
				{
					const float Alpha = FMath::Clamp(SafeDt * PllGainPerSecond, 0.f, 0.25f);
					const float Step = FMath::Clamp(NominalStep + Drift * Alpha, NominalStep * 0.75f, NominalStep * 1.25f);
					Next = In.CurrentTimelineTime + Step;
				}

				if (Out.bAudioReachedEnd)
				{
					Next = FMath::Max(Next, In.SoundDelay + AudioDuration);
					StopPlayback();
				}
			}
		}
		else
		{
			Next = In.CurrentTimelineTime + SafeDt * FreeSpeed;
		}

		if (In.CurrentTimelineTime < In.SoundDelay && Next >= In.SoundDelay)
		{
			Out.bCrossedSoundDelay = true;
		}

		if (Next > End)
		{
			Next = Begin + FMath::Fmod(Next - Begin, FMath::Max(End - Begin, 0.01f));
			Out.bLooped = true;
			if (bAudioActive)
			{
				StopPlayback();
			}
		}

		Out.NextTimelineTime = Next;
		return Out;
	}
};
