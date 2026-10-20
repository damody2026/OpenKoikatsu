#pragma once

// Pure mapping tables for StreetSocial completion data.
// Not included by the scene or the rules runtime. The integrating change can call these
// without treating an unknown speak id as true or inventing a gesture/audio match.
// Sources:
//   Info.ConditionDeterminationSpeak
//   Info.CreateBranchADV(BranchInfo, ChangeValueInfo)
//   Info.GetIntroductionADV / GetEndConversationADV / GetEventADV
//   TalkScene.CommandFunc and TalkEnd
//   GameAssist.GetPoseInfo and Content/IndependentResources/RuntimeAnimations/actions.manifest.json

#include "CoreMinimal.h"

enum class EOKStreetSupport : uint8
{
	Supported,
	Described,
	Unsupported
};

enum class EOKStreetSelectKind : uint8
{
	AttributeWeights,
	PreferenceCompare,
	TalkTemper,
	FixedIndex,
	RandomWeights,
	Unresolved
};

struct FOKStreetSpeakRule
{
	int32 Id = -1;
	EOKStreetSupport Support = EOKStreetSupport::Unsupported;
	const TCHAR* GameFilter = TEXT("original_default_true_not_accepted");
};

struct FOKStreetGestureClip
{
	const TCHAR* State = nullptr;
	const TCHAR* File = nullptr;
};

struct FOKStreetWeightSet
{
	int32 Count = 0;
	int32 Values[4] = {0, 0, 0, 0};
};

inline FOKStreetSpeakRule OKStreetSpeakRule(int32 ConditionId)
{
	if (ConditionId == -1)
	{
		return {-1, EOKStreetSupport::Supported, TEXT("always")};
	}
	if (ConditionId == 0)
	{
		return {0, EOKStreetSupport::Supported, TEXT("never")};
	}
	if (ConditionId == 40)
	{
		return {40, EOKStreetSupport::Unsupported, TEXT("nickname_not_normal_and_not_event")};
	}
	if (ConditionId == 41)
	{
		return {41, EOKStreetSupport::Unsupported, TEXT("nickname_not_normal_and_event_54")};
	}
	return {ConditionId, EOKStreetSupport::Unsupported, TEXT("original_default_true_not_accepted")};
}

// Indexer order is success parts, then failure, then noInterested. This does not pick one.
inline bool OKStreetTalkRandomWeights(int32 ConditionId, int32 SuccessCount, bool bHasNoInterested, FOKStreetWeightSet& OutWeights)
{
	OutWeights = {};
	if (ConditionId == 1 || ConditionId == 2 || ConditionId == 3 || ConditionId == 4 ||
		(ConditionId >= 7 && ConditionId <= 12) || ConditionId == 41 ||
		(ConditionId >= 1 && ConditionId <= 39))
	{
		return false;
	}
	const int32* Source = nullptr;
	int32 Count = 0;
	static const int32 Two[] = {30, 70};
	static const int32 Interested[] = {30, 30, 40};
	static const int32 Pair[] = {20, 40, 40};
	if (SuccessCount == 1 && bHasNoInterested)
	{
		Source = Interested;
		Count = 3;
	}
	else if (SuccessCount == 1)
	{
		Source = Two;
		Count = 2;
	}
	else if (SuccessCount == 2)
	{
		Source = Pair;
		Count = 3;
	}
	else
	{
		return false;
	}
	OutWeights.Count = Count;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		OutWeights.Values[Index] = Source[Index];
	}
	return true;
}

inline EOKStreetSelectKind OKStreetTalkSelectionKind(int32 ConditionId, int32 SuccessCount, bool bHasNoInterested)
{
	if (ConditionId >= 1 && ConditionId <= 4)
	{
		return EOKStreetSelectKind::AttributeWeights;
	}
	if (ConditionId >= 7 && ConditionId <= 12)
	{
		return EOKStreetSelectKind::PreferenceCompare;
	}
	if (ConditionId == 41)
	{
		return EOKStreetSelectKind::FixedIndex;
	}
	if (ConditionId >= 1 && ConditionId <= 39)
	{
		return EOKStreetSelectKind::TalkTemper;
	}
	FOKStreetWeightSet Weights;
	if (OKStreetTalkRandomWeights(ConditionId, SuccessCount, bHasNoInterested, Weights))
	{
		return EOKStreetSelectKind::RandomWeights;
	}
	return EOKStreetSelectKind::Unresolved;
}

inline int32 OKStreetTemperDomainMax(int32 ConditionId)
{
	// Heroine.Initialize stores 0..1 in slots 8..13 and 0..2 elsewhere. Slot = condition - 1.
	const int32 Slot = ConditionId - 1;
	if (Slot >= 8 && Slot <= 13)
	{
		return 1;
	}
	return 2;
}

inline const FOKStreetGestureClip* OKStreetCommonGestures(int32& OutCount)
{
	static const FOKStreetGestureClip Clips[] = {
		{TEXT("f_talk_00_00_00"), TEXT("talk_gesture_00.okanim")},
		{TEXT("f_talk_00_03_00"), TEXT("talk_gesture_03.okanim")},
		{TEXT("f_talk_00_07_00"), TEXT("talk_gesture_07.okanim")},
		{TEXT("f_talk_00_12_00"), TEXT("talk_gesture_12.okanim")},
	};
	OutCount = UE_ARRAY_COUNT(Clips);
	return Clips;
}

// Exact animator state only. Pose names such as Stand_* are not remapped by suffix.
inline const TCHAR* OKStreetGestureForState(const TCHAR* State)
{
	if (!State || !*State)
	{
		return nullptr;
	}
	int32 Count = 0;
	const FOKStreetGestureClip* Clips = OKStreetCommonGestures(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (FCString::Strcmp(Clips[Index].State, State) == 0)
		{
			return Clips[Index].File;
		}
	}
	return nullptr;
}

// Content-relative ogg ref. Drive letters, KKS roots, and parent segments are rejected.
inline bool OKStreetIsPortableAudioRef(const TCHAR* Ref)
{
	if (!Ref || !*Ref)
	{
		return false;
	}
	if (Ref[0] == TEXT('/') || Ref[0] == TEXT('\\'))
	{
		return false;
	}
	const FString Text(Ref);
	if (Text.Contains(TEXT(":")) || Text.Contains(TEXT("..")) || Text.Contains(TEXT("KoikatuSunshine")) || Text.Contains(TEXT("voice_extract")))
	{
		return false;
	}
	return (Text.StartsWith(TEXT("zh_ogg/")) || Text.StartsWith(TEXT("ja_ogg/"))) && Text.EndsWith(TEXT(".ogg"));
}

// TalkScene buttonEventContents[6] -> CommandFunc(11) -> GetEventADV command 6.
inline constexpr int32 OKStreetConfessionButtonIndex = 6;
inline constexpr int32 OKStreetConfessionCommandFunc = 11;
inline constexpr int32 OKStreetConfessionEventCommand = 6;

// Friendship is EndADV.RankUp from farewell condition 10, not a talk command.
inline constexpr int32 OKStreetFriendshipFarewellCondition = 10;
