#pragma once
#include "CoreMinimal.h"
#include "KKFaceAnimation.h"

// FBSBase.CalculateBlendShape: cast Clamp(FixedRate * 100, 0, 100)
// to int before accumulating close/open channels. Manual mouth controls
// specify the final fixed rate, independent of voice/min/max controllers.
inline int32 OKFbsOpenPercent(float FixedRate)
{
	return FMath::IsFinite(FixedRate)
		? FMath::TruncToInt(FMath::Clamp(FixedRate * 100.f, 0.f, 100.f)) : 0;
}

inline void OKFbsWriteTransition(TMap<FName,float>& Weights,
	const TArray<TPair<FName,FName>>& Patterns, const FOKFbsTransition& State, float Rate)
{
	if(!Patterns.IsValidIndex(State.Now) || !Patterns.IsValidIndex(State.Back)) return;
	for(const auto& Pair:Patterns)
	{
		if(!Pair.Key.IsNone()) Weights.Add(Pair.Key,0.f);
		if(!Pair.Value.IsNone()) Weights.Add(Pair.Value,0.f);
	}
	const int32 Open=OKFbsOpenPercent(Rate);
	const float Alpha=State.Alpha();
	auto Add=[&](int32 Pattern,float Blend)
	{
		const auto& Pair=Patterns[Pattern];
		if(!Pair.Key.IsNone()) Weights.FindOrAdd(Pair.Key)+=Blend*(100-Open);
		if(!Pair.Value.IsNone()) Weights.FindOrAdd(Pair.Value)+=Blend*Open;
	};
	if(Alpha!=1.f) Add(State.Back,1.f-Alpha);
	Add(State.Now,Alpha);
}

// Each controller clears its entire target channel set before accumulating
// the selected close/open pair. Call in the game's Brow -> Eyes -> Mouth order.
inline void OKFbsWriteController(TMap<FName, float>& Weights,
	const TArray<TPair<FName, FName>>& Patterns, int32 Pattern, float FixedRate)
{
	if (!Patterns.IsValidIndex(Pattern)) return;
	for (const auto& Pair : Patterns)
	{
		if (!Pair.Key.IsNone()) Weights.Add(Pair.Key, 0.f);
		if (!Pair.Value.IsNone()) Weights.Add(Pair.Value, 0.f);
	}
	const float Open = OKFbsOpenPercent(FixedRate);
	const auto& Pair = Patterns[Pattern];
	if (!Pair.Key.IsNone()) Weights.FindOrAdd(Pair.Key) += 100.f - Open;
	if (!Pair.Value.IsNone()) Weights.FindOrAdd(Pair.Value) += Open;
}
