#pragma once
#include "OKStreetSocialRules.h"

struct FOKStreetCollectedTopic {int32 No=INDEX_NONE,Category=0,Rarity=0;FString Name,ZhName;}; // ZhName: the data's own (Traditional), Name: in the player's language
struct FOKStreetTopicDrop {int32 No=INDEX_NONE,Probability=0;TArray<FString> States;};
struct FOKStreetTopicPart {FString MaleText;FOKStreetNode Line;int32 Node=INDEX_NONE;};
struct FOKStreetTopicListen {int32 Topic=INDEX_NONE;TArray<FOKStreetTopicPart> Intro,Success,Normal,Failure;};
struct FOKStreetTouch {int32 Stage=0,Part=0,Repeat=0,SourceRow=0;bool See=false,Available=false;FOKStreetEffect Effect;TArray<FOKStreetTopicPart> Parts;};
struct FOKStreetTopicProfile {int32 Personality=INDEX_NONE;TArray<FOKStreetTopicListen> Listen;TMap<int32,TArray<TArray<FOKStreetTopicPart>>> Offers;TArray<FOKStreetTouch> Touch;};
// TalkScene.TouchFunc shares left/right hand and chest counters.
inline int32 OKStreetTouchPart(int32 Region,bool See){if(Region<0||Region>5)return INDEX_NONE;if(See)return Region>=2&&Region<=3?INDEX_NONE:Region>=4?1:0;return Region==0?2:Region==1?3:Region>=4?1:0;}
inline int32 OKStreetNextTouchCount(int32 Count){return FMath::Clamp(Count,0,2)+1;}
inline int32 OKStreetTouchRepeat(int32 Count){return Count==1?0:Count==2?2:1;}
inline const FOKStreetTouch* OKStreetFindTouch(const FOKStreetTopicProfile& Profile,int32 Stage,int32 Part,bool See,int32 Repeat){for(const auto& T:Profile.Touch)if(T.Stage==Stage&&T.Part==Part&&T.See==See&&T.Repeat==Repeat)return &T;return nullptr;}
struct FOKStreetTopics {
 TArray<FOKStreetCollectedTopic> Topics;
 TArray<FOKStreetTopicDrop> Drops;
 TArray<int32> TalkFavor,TalkPoint,ListenCommon,ListenRare,ListenPoint,InterestFavor,InterestLewd,InterestPoint;
 TArray<FOKStreetTopicProfile> Profiles;
 const FOKStreetCollectedTopic* Find(int32 No)const{for(const auto& T:Topics)if(T.No==No)return &T;return nullptr;}
 const FOKStreetTopicProfile* Profile(int32 Personality)const{for(const auto& P:Profiles)if(P.Personality==Personality)return &P;return nullptr;}
};
// What answers are worth, pickups and dialogue drops are rules (Tools/ok-rules talk.rs).
inline bool OKStreetReadTopicParts(const TArray<TSharedPtr<FJsonValue>>& Values,int32 Personality,TArray<FOKStreetTopicPart>& Out,FString& Error){
 FString Male;
 for(const auto& V:Values){const auto Obj=V->AsObject();if(!Obj){Error=TEXT("topic_part_invalid");return false;}FString Speaker,Text,TextZh;Obj->TryGetStringField(TEXT("speaker"),Speaker);Obj->TryGetStringField(TEXT("text_jp"),Text);Obj->TryGetStringField(TEXT("text_zh"),TextZh);
  if(Speaker==TEXT("male")){Male=OKStreetLineLabel(TextZh,Text,TEXT(""));continue;}
  FOKStreetTopicPart Part;Part.MaleText=MoveTemp(Male);Part.Line.TextJp=Text;Part.Line.TextZh=TextZh;Part.Line.PersonalityId=Personality;Part.Line.Category=TEXT("daily");Part.Line.VoiceSequenceComplete=true;Obj->TryGetStringField(TEXT("face"),Part.Line.FacialExpression);
  if(Speaker!=TEXT("female")||!OKStreetReadStringArray(Obj,TEXT("voices"),Part.Line.VoiceFiles)||Part.Line.VoiceFiles.IsEmpty()){Error=TEXT("topic_voice_incomplete");return false;}
  for(const auto& Voice:Part.Line.VoiceFiles)if(!FPaths::IsRelative(Voice)&&!IFileManager::Get().FileExists(*Voice)){Error=TEXT("topic_voice_missing");return false;}
  Out.Add(MoveTemp(Part));
 }
 if(!Male.IsEmpty()){FOKStreetTopicPart Trailing;Trailing.MaleText=MoveTemp(Male);Out.Add(MoveTemp(Trailing));}
 if(Out.IsEmpty()){Error=TEXT("topic_sequence_empty");return false;}return true;
}
inline bool OKStreetReadTopics(const FString& Json,FOKStreetTopics& Out,FString& Error){
 TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root){Error=TEXT("topic_json_invalid");return false;}
 const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
 if(!Root->TryGetArrayField(TEXT("topics"),Values)||Values->IsEmpty()){Error=TEXT("topics_missing");return false;}
 for(const auto& V:*Values){const auto Obj=V->AsObject();FOKStreetCollectedTopic T;if(!Obj||!OKStreetJsonInt(Obj->TryGetField(TEXT("No")),T.No)||!OKStreetJsonInt(Obj->TryGetField(TEXT("Category")),T.Category)||!OKStreetJsonInt(Obj->TryGetField(TEXT("Rarity")),T.Rarity)||T.No<0||T.Category<0||T.Category>4||T.Rarity<0||T.Rarity>4||!Obj->TryGetStringField(TEXT("Name"),T.Name)||Out.Find(T.No)){Error=TEXT("topic_metadata_invalid");return false;}Out.Topics.Add(T);}
 if(Root->TryGetArrayField(TEXT("drops"),Values))for(const auto& V:*Values){const auto O=V->AsObject();FOKStreetTopicDrop D;if(!O||!OKStreetJsonInt(O->TryGetField(TEXT("no")),D.No)||!OKStreetJsonInt(O->TryGetField(TEXT("probability")),D.Probability)||!Out.Find(D.No)||D.Probability<0||D.Probability>100||!OKStreetReadStringArray(O,TEXT("states"),D.States)||Out.Drops.ContainsByPredicate([&D](const auto& Other){return Other.No==D.No;})){Error=TEXT("topic_drop_invalid");return false;}Out.Drops.Add(MoveTemp(D));}
 for(const auto& Kind:{FString(TEXT("talk_points")),FString(TEXT("listen_points")),FString(TEXT("interest"))}){
  if(!Root->TryGetArrayField(Kind,Values)||Values->Num()!=(Kind==TEXT("listen_points")?3:5)){Error=TEXT("topic_points_invalid");return false;}
  for(const auto& V:*Values){const auto Obj=V->AsObject();int32 A=0,B=0,C=0;if(!Obj||!OKStreetJsonInt(Obj->TryGetField(Kind==TEXT("listen_points")?TEXT("common"):TEXT("favor")),A)||!OKStreetJsonInt(Obj->TryGetField(Kind==TEXT("listen_points")?TEXT("rare"):Kind==TEXT("interest")?TEXT("lewdness"):TEXT("point")),B)||!OKStreetJsonInt(Obj->TryGetField(TEXT("point")),C)){Error=TEXT("topic_points_invalid");return false;}
   if(Kind==TEXT("talk_points")){Out.TalkFavor.Add(A);Out.TalkPoint.Add(B);}else if(Kind==TEXT("listen_points")){Out.ListenCommon.Add(A);Out.ListenRare.Add(B);Out.ListenPoint.Add(C);}else{Out.InterestFavor.Add(A);Out.InterestLewd.Add(B);Out.InterestPoint.Add(C);}
  }
 }
 if(!Root->TryGetArrayField(TEXT("profiles"),Values)){Error=TEXT("topic_profiles_missing");return false;}
 for(const auto& V:*Values){const auto Obj=V->AsObject();FOKStreetTopicProfile P;const TArray<TSharedPtr<FJsonValue>>* Listen=nullptr;const TSharedPtr<FJsonObject>* Offers=nullptr;
  if(!Obj||!OKStreetJsonInt(Obj->TryGetField(TEXT("personality")),P.Personality)||!Obj->TryGetArrayField(TEXT("listen"),Listen)||!Obj->TryGetObjectField(TEXT("offer"),Offers)){Error=TEXT("topic_profile_invalid");return false;}
  for(const auto& L:*Listen){const auto O=L->AsObject();FOKStreetTopicListen Row;if(!O||!OKStreetJsonInt(O->TryGetField(TEXT("topic")),Row.Topic)||!Out.Find(Row.Topic)){Error=TEXT("listen_topic_invalid");return false;}
   for(const auto& Pair:{TPair<FString,TArray<FOKStreetTopicPart>*>(TEXT("intro"),&Row.Intro),TPair<FString,TArray<FOKStreetTopicPart>*>(TEXT("success"),&Row.Success),TPair<FString,TArray<FOKStreetTopicPart>*>(TEXT("normal"),&Row.Normal),TPair<FString,TArray<FOKStreetTopicPart>*>(TEXT("failure"),&Row.Failure)}){const TArray<TSharedPtr<FJsonValue>>* Parts=nullptr;if(!O->TryGetArrayField(Pair.Key,Parts)||!OKStreetReadTopicParts(*Parts,P.Personality,*Pair.Value,Error))return false;}P.Listen.Add(MoveTemp(Row));
  }
  for(const auto& Offer:(*Offers)->Values){int32 No;if(!LexTryParseString(No,*Offer.Key)||!Out.Find(No)){Error=TEXT("offer_topic_invalid");return false;}const TArray<TSharedPtr<FJsonValue>>* Choices=nullptr;if(!Offer.Value->TryGetArray(Choices)){Error=TEXT("offer_choices_invalid");return false;}for(const auto& Choice:*Choices){const TArray<TSharedPtr<FJsonValue>>* Parts=nullptr;TArray<FOKStreetTopicPart> Seq;if(!Choice->TryGetArray(Parts)||!OKStreetReadTopicParts(*Parts,P.Personality,Seq,Error))return false;P.Offers.FindOrAdd(No).Add(MoveTemp(Seq));}}
  const TArray<TSharedPtr<FJsonValue>>* Touch=nullptr;
  if(Obj->TryGetArrayField(TEXT("touch"),Touch))for(const auto& T:*Touch){const auto O=T->AsObject();FOKStreetTouch Row;FString Mode;bool Available=false;const TArray<TSharedPtr<FJsonValue>>* Parts=nullptr;int32 Favor=0,Lewd=0;
   if(!O||!OKStreetJsonInt(O->TryGetField(TEXT("stage")),Row.Stage)||!OKStreetJsonInt(O->TryGetField(TEXT("part")),Row.Part)||!OKStreetJsonInt(O->TryGetField(TEXT("repeat")),Row.Repeat)||!OKStreetJsonInt(O->TryGetField(TEXT("source_row")),Row.SourceRow)||!OKStreetJsonInt(O->TryGetField(TEXT("favor")),Favor)||!OKStreetJsonInt(O->TryGetField(TEXT("lewdness")),Lewd)||!O->TryGetStringField(TEXT("mode"),Mode)||!O->TryGetBoolField(TEXT("available"),Available)||Row.Stage<0||Row.Stage>2||Row.Repeat<0||Row.Repeat>2||(Mode!=TEXT("Touch")&&Mode!=TEXT("See"))){Error=TEXT("touch_row_invalid");return false;}
   Row.See=Mode==TEXT("See");if(Row.Part<0||Row.Part>=(Row.See?2:4)){Error=TEXT("touch_part_invalid");return false;}
   Row.Available=Available;
   if(!Available){if(OKStreetFindTouch(P,Row.Stage,Row.Part,Row.See,Row.Repeat)||!O->TryGetArrayField(TEXT("parts"),Parts)||!Parts->IsEmpty()){Error=TEXT("touch_unavailable_invalid");return false;}P.Touch.Add(MoveTemp(Row));continue;}
   if(OKStreetFindTouch(P,Row.Stage,Row.Part,Row.See,Row.Repeat)||!O->TryGetArrayField(TEXT("parts"),Parts)||!OKStreetReadTopicParts(*Parts,P.Personality,Row.Parts,Error)){Error=TEXT("touch_sequence_invalid");return false;}
   Row.Effect.Favor=Favor;Row.Effect.Lewdness=Lewd;Row.Effect.TalkCost=1;P.Touch.Add(MoveTemp(Row));
  }
  Out.Profiles.Add(MoveTemp(P));
 }
 return true;
}
