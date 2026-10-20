#pragma once

#include "CoreMinimal.h"

enum class EKKMsgKind : uint8
{
	Nil,
	Bool,
	Int,
	Float,
	String,
	Bin,
	Array,
	Map,
	Other
};

struct FKKMsgValue
{
	EKKMsgKind Kind = EKKMsgKind::Nil;
	bool b = false;
	int64 i = 0;
	double f = 0.0;
	FString s;
	TArray<uint8> Bin;
	TArray<TSharedPtr<FKKMsgValue>> Array;
	TMap<FString, TSharedPtr<FKKMsgValue>> Map;
	TArray<FString> MapKeys;

	const FKKMsgValue* Find(const TCHAR* Key) const
	{
		if (const TSharedPtr<FKKMsgValue>* Found = Map.Find(Key))
		{
			return Found->Get();
		}
		return nullptr;
	}

	int32 AsInt(int32 Default = 0) const
	{
		if (Kind == EKKMsgKind::Int)
		{
			return static_cast<int32>(i);
		}
		if (Kind == EKKMsgKind::Float)
		{
			return static_cast<int32>(f);
		}
		return Default;
	}

	float AsFloat(float Default = 0.0f) const
	{
		if (Kind == EKKMsgKind::Float)
		{
			return static_cast<float>(f);
		}
		if (Kind == EKKMsgKind::Int)
		{
			return static_cast<float>(i);
		}
		return Default;
	}
};

bool UnpackMsgpack(const uint8* Data, int32 Size, FKKMsgValue& Out, FString& OutError);
bool UnpackMsgpackExact(const uint8* Data, int32 Size, FKKMsgValue& Out, FString& OutError);
// Validate and traverse a whole map, allocating values only for requested keys.
bool UnpackMsgpackSelectedMap(const uint8* Data,int32 Size,TConstArrayView<FString> Keys,FKKMsgValue& Out,FString& OutError);
bool UnpackMapish(const uint8* Data, int32 Size, FKKMsgValue& Out, FString& OutError);
void SplitLenPrefixedMaps(const TArray<uint8>& Raw, TArray<FKKMsgValue>& OutParts);
bool PackLenPrefixedMaps(const TArray<FKKMsgValue>& Parts, TArray<uint8>& Out, FString& OutError);
bool PackMsgpack(const FKKMsgValue& Value, TArray<uint8>& Out, FString& OutError);
void MsgSetString(FKKMsgValue& MapValue, const TCHAR* Key, const FString& StringValue);
void MsgSetFloatArray(FKKMsgValue& MapValue, const TCHAR* Key, const TArray<float>& Values);
