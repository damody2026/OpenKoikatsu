// What runs out from between her legs on the street (user, 2026-11-01). Her desire for him past 100 (relation
// sex_desire, up to 200): her cheeks stay flushed, more the higher it is, and while he is within 10 m clear juice
// drips, more the higher it is. After a scene with him: his semen (white) and her blood (red) for the hours the rules
// gave (hexp.rs discharge, white_until / red_until in game minutes), wherever she is; both by turns when both run. Her
// desire past 150 with a bust past 40 (her card's shapeBody 4): milk from her nipples, more the higher her desire and
// the bigger her bust. The drops and the marks they leave are OKArousalDrops.h: a drop is gone in ten seconds, where
// it fell stays wet until she sleeps or bathes. How wet she is goes to her mind each turn (OKStreetBodyWetStatus).

namespace OKStreetArousal{constexpr float DripRange=1000.f,SeenRange=3000.f;}

// Her card, read once: bust size and KK_SkinEffects' settings (陰部脆弱, 處女膜再生, stretched hymen).
static const FOKStreetHerCard& OKStreetHerCard(FOKStreetSceneState& S,int32 Girl){
 static const FOKStreetHerCard None;if(Girl<0||Girl>=OKStreetCastCount)return None;
 FOKStreetHerCard& C=S.HerCard[Girl];if(C.Read)return C;C.Read=true;
 const FString Path=OKStreetCardFor(S,Girl);FKKCharacterFile File;FString Error;
 if(Path.IsEmpty()||!UKKCardReader::LoadFromFile(Path,File,Error)){UE_LOG(LogTemp,Display,TEXT("Street her card: actor=%d not read (%s)"),Girl,*Error);return C;}
 C.Bust=File.Body.ShapeValueBody.IsValidIndex(4)?File.Body.ShapeValueBody[4]:0.f;C.HymenRegen=File.bHymenRegen;C.HymenStretched=File.bStretchedHymen;C.Fragile=File.bFragileVag;
 UE_LOG(LogTemp,Display,TEXT("Street her card: actor=%d bust %.0f, hymen regen %d, stretched %d, fragile %d"),Girl,C.Bust*100.f,C.HymenRegen,C.HymenStretched,C.Fragile);
 return C;
}
// Onto her profile, which the rules read (traits.rs): the libido she gained in one-on-ones (ext hexp.<id>.libido_gain, a
// point a climax past ten in a day) as libido_gain, and her card's hymen / 陰部脆弱 settings as traits (a profile's own
// vagina_fragile stays). The profile comes from the mind each run; the gain lives in the save, the settings in her card.
static void OKStreetApplyHerExtras(FOKStreetSceneState& S,const FString& Id){
 const TSharedPtr<FJsonObject> Profile=S.MindProfiles.FindRef(Id);if(!Profile.IsValid())return;
 TSharedPtr<FJsonObject> E;const TSharedPtr<FJsonObject>* H=nullptr;const TSharedPtr<FJsonObject>* Her=nullptr;double Gain=0;
 if(!S.Store.Ext.IsEmpty()&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S.Store.Ext),E)&&E.IsValid()
  &&E->TryGetObjectField(TEXT("hexp"),H)&&(*H)->TryGetObjectField(Id,Her))(*Her)->TryGetNumberField(TEXT("libido_gain"),Gain);
 Profile->SetNumberField(TEXT("libido_gain"),Gain);
 const int32 Girl=S.Manifest.Characters.IndexOfByPredicate([&](const FOKStreetCharacterMeta& C){return OKStreetStableId(C)==Id;});if(Girl<=0)return;
 const FOKStreetHerCard& Card=OKStreetHerCard(S,Girl);
 const TSharedPtr<FJsonObject>* Found=nullptr;TSharedPtr<FJsonObject> Traits=Profile->TryGetObjectField(TEXT("traits"),Found)?*Found:MakeShared<FJsonObject>();
 bool Own=false;Traits->TryGetBoolField(TEXT("vagina_fragile"),Own);
 Traits->SetBoolField(TEXT("vagina_fragile"),Own||Card.Fragile);Traits->SetBoolField(TEXT("hymen_regen"),Card.HymenRegen);Traits->SetBoolField(TEXT("hymen_stretched"),Card.HymenStretched);
 Profile->SetObjectField(TEXT("traits"),Traits);
}

// Whether she runs now and how wet she is, for her mind (ui.mind.body_wet_status): crotch none / damp (marks only) /
// dripping / flooding (past 150), what else runs (white, red, both), her nipples none / wet / leaking.
static FString OKStreetBodyWetStatus(const FOKStreetSceneState& S,int32 Girl,int64 Minute){
 if(Girl<=0||Girl>=OKStreetCastCount)return FString();
 const float Desire=S.ArousalDesire[Girl];const bool Near=S.Agents.IsValidIndex(Girl-1)&&FVector::Dist(S.Agents[Girl-1].Position,S.Male)<=OKStreetArousal::DripRange;
 const bool White=S.ArousalWhiteUntil[Girl]>Minute,Red=S.ArousalRedUntil[Girl]>Minute,Clear=Desire>OKArousal::DesireFlush&&Near;
 const TCHAR* Crotch=Clear?(Desire>OKArousal::DesireMilk?TEXT("flooding"):TEXT("dripping")):(White||Red)?TEXT("dripping"):S.ArousalWetCrotch[Girl]>0?TEXT("damp"):TEXT("none");
 const bool Leak=Desire>OKArousal::DesireMilk&&S.HerCard[Girl].Bust>OKArousal::BustMilk;
 return OKMsg(TEXT("ui.mind.body_wet_status"),{{TEXT("crotch"),Crotch},{TEXT("discharge"),White&&Red?TEXT("both"):White?TEXT("white"):Red?TEXT("red"):TEXT("none")},
  {TEXT("milk"),Leak?TEXT("leaking"):S.ArousalWetMilk[Girl]>0?TEXT("wet"):TEXT("none")},
  {TEXT("any"),FCString::Strcmp(Crotch,TEXT("none"))||White||Red||Leak||S.ArousalWetMilk[Girl]>0?TEXT("yes"):TEXT("no")}});
}

void AOKStreetSocialScene::TickArousal(float Dt){
 auto& S=*State;const int32 Cast=FMath::Min(S.Manifest.Characters.Num(),OKStreetCastCount);
 const double Wall=FPlatformTime::Seconds();const int64 Minute=OKStreetMindNow(S);
 // Twice a second: her flush follows her desire (the mind's own face look keeps it as a floor, StreetMind face), and
 // until when she runs white / red (read again only when the save's ext changed); asleep or in the bath her wet marks
 // and wetness go (a new day too: she slept through it).
 if(Wall>=S.ArousalCheckAt){S.ArousalCheckAt=Wall+.5;
  const uint32 ExtHash=GetTypeHash(S.Store.Ext);
  if(ExtHash!=S.ArousalExtHash){S.ArousalExtHash=ExtHash;TSharedPtr<FJsonObject> E;const TSharedPtr<FJsonObject>* H=nullptr;
   const bool Read=!S.Store.Ext.IsEmpty()&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S.Store.Ext),E)&&E.IsValid()&&E->TryGetObjectField(TEXT("hexp"),H);
   for(int32 Girl=1;Girl<Cast;++Girl){const TSharedPtr<FJsonObject>* Her=nullptr;double White=-1,Red=-1;
    if(Read&&(*H)->TryGetObjectField(OKStreetStableId(S.Manifest.Characters[Girl]),Her)){(*Her)->TryGetNumberField(TEXT("white_until"),White);(*Her)->TryGetNumberField(TEXT("red_until"),Red);}
    S.ArousalWhiteUntil[Girl]=int64(White);S.ArousalRedUntil[Girl]=int64(Red);}}
  const bool NewDay=S.ArousalDay!=S.Store.CalendarDay;S.ArousalDay=S.Store.CalendarDay;
  for(int32 Girl=1;Girl<Cast;++Girl){
   const FOKStreetPlanBlock* B=LifeBlock(Girl,int32(Minute%1440));
   const bool Washing=(B&&(B->Kind==TEXT("sleep")||B->Kind==TEXT("bath")))||S.SleepGirl==Girl||S.SleepGirl2==Girl||(S.Pair.Partner==Girl&&S.Pair.Bath);
   if((Washing&&!S.ArousalWashing[Girl])||NewDay){S.ArousalDrops.ClearMarks(Girl);
    if(S.ArousalWetCrotch[Girl]||S.ArousalWetMilk[Girl])UE_LOG(LogTemp,Display,TEXT("Street arousal: actor=%d %s, dry again"),Girl,NewDay?TEXT("a new day"):Washing&&B?*B->Kind:TEXT("asleep or bathing"));
    S.ArousalWetCrotch[Girl]=S.ArousalWetMilk[Girl]=0;}
   S.ArousalWashing[Girl]=Washing;
   const float Desire=OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Girl]),S.Manifest.TalkBudget).SexDesire;
   S.ArousalDesire[Girl]=Desire;const float Bl=OKArousal::Blush(Desire);
   if(FMath::Abs(Bl-S.ArousalBlush[Girl])<.05f)continue;
   FOKRuntimeFaceLookNow Was;FOKRuntimeFaceLook Look;Look.Blush=Bl;if(OKGetCrowdRuntimeFaceLook(this,Girl,Was))Look.Gaze=Was.Gaze; // her gaze stays
   FString Error;if(OKSetCrowdRuntimeFaceLook(this,Girl,Look,Error)){S.ArousalBlush[Girl]=Bl;UE_LOG(LogTemp,Display,TEXT("Street arousal: actor=%d desire %.0f, flushed %.2f"),Girl,Desire,Bl);}
  }
 }
 auto Spawn=[&](int32 Girl,FName Bone,uint8 Colour){FVector At;
  return OKGetCrowdRuntimeBonePosition(this,Girl,Bone,At)&&S.ArousalDrops.Spawn(this,Girl,Bone,At,S.Agents[Girl-1].Position.Z+.3f,Colour);};
 for(int32 Girl=1;Girl<Cast;++Girl){
  if(!S.Agents.IsValidIndex(Girl-1)||S.PrivateGirl==Girl)continue; // in the room with him: the room's own drops (OKHScene)
  const float Far=FVector::Dist(S.Agents[Girl-1].Position,S.Male),Desire=S.ArousalDesire[Girl];if(Far>OKStreetArousal::SeenRange)continue;
  // Milk from her nipples (desire past 150, bust past 40), one side then the other.
  const float Bust=OKStreetHerCard(S,Girl).Bust;
  if(Desire>OKArousal::DesireMilk&&Bust>OKArousal::BustMilk&&Wall>=S.ArousalNippleAt[Girl]){
   S.ArousalNippleAt[Girl]=Wall+OKArousal::NippleInterval(Desire,Bust)*.5f*FMath::FRandRange(.8f,1.2f); // two nipples by turns
   if(Spawn(Girl,++S.ArousalTurn[Girl]%2?FName(TEXT("cf_j_bnip02_L")):FName(TEXT("cf_j_bnip02_R")),OKArousal::White))++S.ArousalWetMilk[Girl];}
  // A new drop at her crotch: what she runs now (white and red by turns, else clear while he is near).
  if(Wall<S.ArousalDripAt[Girl])continue;
  const bool White=S.ArousalWhiteUntil[Girl]>Minute,Red=S.ArousalRedUntil[Girl]>Minute,Clear=Desire>OKArousal::DesireFlush&&Far<=OKStreetArousal::DripRange;
  if(!(White||Red||Clear))continue;
  S.ArousalDripAt[Girl]=Wall+FMath::Min((White||Red)?1.f:99.f,Clear?OKArousal::CrotchInterval(Desire):99.f)*FMath::FRandRange(.8f,1.2f);
  const uint8 Colour=White&&Red?(++S.ArousalTurn[Girl]%2?OKArousal::White:OKArousal::Red):White?OKArousal::White:Red?OKArousal::Red:OKArousal::Clear;
  if(Spawn(Girl,TEXT("cf_J_Kokan"),Colour))++S.ArousalWetCrotch[Girl];
 }
 S.ArousalDrops.Tick(Dt,[this](int32 Girl,FName Bone,FVector& At){return OKGetCrowdRuntimeBonePosition(this,Girl,Bone,At);});
}
