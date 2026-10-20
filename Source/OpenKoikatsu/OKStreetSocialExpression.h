#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "KKRuntimeRenderMesh.h"

struct FOKStreetExpression {
 FKKRuntimeFaceState Face;
 bool Blink=true,BlinkSpecified=false;
 // The source sheet's other cells: cheek blush 0..1, tears 0..3 (0 none), gaze (-1 the card's, 0 ahead, 1 at the camera,
 // 2..3 away).
 float Blush=0.f;int32 Tears=0,EyesLook=-1;
 FString Name,Source;
};
struct FOKStreetExpressions {
 TMap<FString,FOKStreetExpression> Items;
 FString Error;
 const FOKStreetExpression* Find(int32 Personality,const FString& Name)const{return Items.Find(FString::FromInt(Personality)+TEXT("\t")+Name);}
};
inline FOKStreetExpressions OKStreetParseExpressions(const FString& Body){
 FOKStreetExpressions Out;TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Profiles=nullptr;double Schema=0;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body),Root)||!Root.IsValid()||!Root->TryGetNumberField(TEXT("schema"),Schema)||Schema!=1||!Root->TryGetObjectField(TEXT("profiles"),Profiles)){Out.Error=TEXT("Expression catalogue schema invalid");return Out;}
 for(const auto& Profile:(*Profiles)->Values){if(!Profile.Value.IsValid()||Profile.Value->Type!=EJson::Object){Out.Error=TEXT("Expression profile must be an object");return Out;}const auto Table=Profile.Value->AsObject();if(!Table.IsValid()){Out.Error=TEXT("Expression profile invalid");return Out;}
  for(const auto& Entry:Table->Values){if(!Entry.Value.IsValid()||Entry.Value->Type!=EJson::Object){Out.Error=TEXT("Expression entry must be an object");return Out;}const auto Object=Entry.Value->AsObject();const TSharedPtr<FJsonObject>* Values=nullptr;if(!Object.IsValid()||!Object->TryGetObjectField(TEXT("values"),Values)){Out.Error=TEXT("Expression values invalid");return Out;}
   FOKStreetExpression Expression;Expression.Name=Entry.Key;FString PatternError;
   auto Pattern=[&](const TCHAR* Key,int32& Target,bool& Blend){const TSharedPtr<FJsonObject>* P=nullptr;if(!(*Values)->HasField(Key))return true;double Number=0;if(!(*Values)->TryGetObjectField(Key,P)||!(*P)->TryGetNumberField(TEXT("pattern"),Number)||!FMath::IsFinite(Number)||Number<0||Number>255||Number!=FMath::FloorToDouble(Number))return false;if((*P)->HasField(TEXT("blend"))){if(!(*P)->HasTypedField<EJson::Boolean>(TEXT("blend"))||!(*P)->TryGetBoolField(TEXT("blend"),Blend)){PatternError=TEXT("Expression blend invalid");return false;}}else Blend=true;Target=int32(Number);return true;};
   auto Opening=[&](const TCHAR* Key,float& Target){if(!(*Values)->HasField(Key))return true;double Number=0;if(!(*Values)->TryGetNumberField(Key,Number)||!FMath::IsFinite(Number)||Number<0||Number>1)return false;Target=float(Number);return true;};
   if(!Pattern(TEXT("brow"),Expression.Face.BrowPattern,Expression.Face.BrowBlend)||!Pattern(TEXT("eyes"),Expression.Face.EyePattern,Expression.Face.EyeBlend)||!Pattern(TEXT("mouth"),Expression.Face.MouthPattern,Expression.Face.MouthBlend)||!Opening(TEXT("brow_open"),Expression.Face.BrowOpening)||!Opening(TEXT("eyes_open"),Expression.Face.EyeOpening)||!Opening(TEXT("mouth_open"),Expression.Face.MouthOpening)){Out.Error=PatternError.IsEmpty()?TEXT("Expression pattern or opening invalid"):PatternError;return Out;}
   {double N=0;if((*Values)->TryGetNumberField(TEXT("blush"),N)&&FMath::IsFinite(N))Expression.Blush=FMath::Clamp(float(N),0.f,1.f);
    if((*Values)->TryGetNumberField(TEXT("tears"),N)&&FMath::IsFinite(N))Expression.Tears=FMath::Clamp(int32(N),0,3);
    if((*Values)->TryGetNumberField(TEXT("eyes_look"),N)&&FMath::IsFinite(N))Expression.EyesLook=FMath::Clamp(int32(N),-1,3);}
   if((*Values)->HasField(TEXT("blink"))){if(!(*Values)->TryGetBoolField(TEXT("blink"),Expression.Blink)){Out.Error=TEXT("Expression blink invalid");return Out;}Expression.BlinkSpecified=true;}
   const TSharedPtr<FJsonObject>* Source=nullptr;if(Object->TryGetObjectField(TEXT("source"),Source))FJsonSerializer::Serialize((*Source).ToSharedRef(),TJsonWriterFactory<>::Create(&Expression.Source));
   Out.Items.Add(FString(Profile.Key)+TEXT("\t")+Expression.Name,MoveTemp(Expression));
  }
 }
 return Out;
}
