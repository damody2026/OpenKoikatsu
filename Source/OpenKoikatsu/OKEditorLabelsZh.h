#pragma once
#include "CoreMinimal.h"

// Display labels only. Callers keep the original source parameter as the key.
inline FString OKAnimationParameterLabel(const FString& Key,int32 Index)
{
 static const TMap<FString,FString> Names={
  {TEXT("height"),TEXT("身高校正")},{TEXT("breast"),TEXT("胸部校正")},
  {TEXT("speed"),TEXT("播放速度")},{TEXT("motion"),TEXT("動作混合")},
  {TEXT("option"),TEXT("動作選項")},{TEXT("blend"),TEXT("混合比例")},
  {TEXT("weight"),TEXT("混合權重")},{TEXT("size"),TEXT("尺寸校正")},
  {TEXT("angle"),TEXT("角度")},{TEXT("yaw"),TEXT("水平轉向")},
  {TEXT("pitch"),TEXT("上下俯仰")},{TEXT("roll"),TEXT("左右傾斜")}};
 FString Name=Key.ToLower(),Suffix;
 while(!Name.IsEmpty()&&FChar::IsDigit(Name[Name.Len()-1])){Suffix=Name.Right(1)+Suffix;Name.LeftChopInline(1);}
 if(const FString* Label=Names.Find(Name))return *Label+(Suffix.IsEmpty()?FString():TEXT(" ")+Suffix);
 for(const auto& Pair:{TPair<FString,FString>(TEXT("OK.LayerWeight"),TEXT("動畫層權重")),
  TPair<FString,FString>(TEXT("OK.LayerStartTime"),TEXT("動畫層開始時間")),
  TPair<FString,FString>(TEXT("OK.LayerStartPhase"),TEXT("動畫層起始進度"))}){
  if(Key==Pair.Key)return Pair.Value;
  if(Key.StartsWith(Pair.Key+TEXT(".")))return Pair.Value+TEXT(" ")+Key.Mid(Pair.Key.Len()+1);
 }
 // Custom controllers may declare arbitrary identifiers without descriptions.
 // Keep every control available and distinguish it by its stable source order.
 return FString::Printf(TEXT("自訂動畫參數 %d"),Index+1);
}
