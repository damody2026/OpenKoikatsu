// Included by OKStreetSocialScene.cpp: a development UI probe for the plan §15 apps. With OK_STREET_UI_PROBE=1 the game
// finds buttons by their label in the live widget tree and presses them through FSlateApplication (the same hit
// testing, mouse capture and rebuild timing as a real click), checks what each press did, and writes
// <play folder>/ui_probe.json. OS-level synthetic input is not reliable from the tool environment, so this is the check.

static void OKProbeCollect(const TSharedRef<SWidget>& W,TSharedPtr<SWidget> Button,TArray<TPair<FString,TSharedPtr<SWidget>>>& Out){
 if(W->GetType()==FName(TEXT("SButton")))Button=W;
 if(W->GetType()==FName(TEXT("STextBlock"))&&Button.IsValid())Out.Add(TPair<FString,TSharedPtr<SWidget>>(StaticCastSharedRef<STextBlock>(W)->GetText().ToString(),Button));
 FChildren* C=W->GetChildren();if(!C)return;
 for(int32 I=0;I<C->Num();++I)OKProbeCollect(C->GetChildAt(I),Button,Out);
}

// Not painted yet only because its scroll box shows other rows (the menu past its ninth dish). A collapsed or hidden
// button has no size either, but no shown scroll box holds it.
static bool OKProbeScrolledOut(const TSharedPtr<SWidget>& W){
 if(!W->GetCachedGeometry().GetLocalSize().IsNearlyZero())return false;
 for(TSharedPtr<SWidget> Up=W->GetParentWidget();Up.IsValid();Up=Up->GetParentWidget()){
  if(Up->GetVisibility()==EVisibility::Collapsed||Up->GetVisibility()==EVisibility::Hidden)return false;
  // A scroll box inside another one (the shop menu inside the side panel) has no size either while the outer one shows
  // other rows: the outer one decides (the whole bookstore menu was missing during a conversation).
  if(Up->GetType()==FName(TEXT("SScrollBox"))&&!Up->GetCachedGeometry().GetLocalSize().IsNearlyZero())return true;}
 return false;
}

// Development actions a probe step can name instead of a button ("@..."): bring the nearest heroine in front of him,
// walk together, the lover touches, her watermelon swing, and a screenshot into the play folder.
static int32 OKProbeNearestGirl(const FOKStreetSceneState& S){
 int32 Best=INDEX_NONE;float D=1e12f;
 for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(S.Agents.IsValidIndex(I-1)){const float X=FVector::Dist2D(S.Agents[I-1].Position,S.Male);if(X<D){D=X;Best=I;}}
 return Best;
}

bool AOKStreetSocialScene::ProbeAction(const FString& Label,FString& Why){
 auto& S=*State;const int32 Girl=OKProbeNearestGirl(S);
 if(Label.StartsWith(TEXT("@shot:"))){FScreenshotRequest::RequestScreenshot(FPaths::GetPath(S.Manifest.SavePath)/(TEXT("probe_")+Label.Mid(6)+TEXT(".png")),true,false);return true;}
 if(Girl==INDEX_NONE){Why=TEXT("no heroine");return false;}
 // His lover (the first heroine whose stage is lover) in front of him, so she is the nearest from now on.
 if(Label==TEXT("@summonlover")||Label==TEXT("@summonstranger")){const FVector Front=FRotator(0,S.Rotations[0].Yaw+90.f,0).Vector();const FString Want=Label==TEXT("@summonlover")?TEXT("lover"):TEXT("stranger");
  for(int32 I=1;I<S.Manifest.Characters.Num();++I)if(S.Agents.IsValidIndex(I-1)&&OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[I]),S.Manifest.TalkBudget).Stage==Want){
   for(auto& A:S.Agents)if(A.Slot!=I&&FVector::Dist2D(A.Position,S.Male)<300.f)A.Position+=FVector(600,0,0); // she alone is the nearest
   S.Agents[I-1].Position=FVector(S.Male.X,S.Male.Y,S.Male.Z)+Front*120.f;UE_LOG(LogTemp,Display,TEXT("Street probe summon: %s actor=%d"),*Want,I);return true;}
  Why=TEXT("no lover");return false;}
 if(Label==TEXT("@summon")){const FVector Front=FRotator(0,S.Rotations[0].Yaw+90.f,0).Vector();S.Agents[Girl-1].Position=FVector(S.Male.X,S.Male.Y,S.Male.Z)+Front*120.f;
  FOKRuntimeCharacterStatus St;const bool Ok=OKGetCrowdRuntimeCharacterStatus(this,Girl,St);
  UE_LOG(LogTemp,Display,TEXT("Street probe summon: actor=%d status=%d visible=%d error=%s yaw=%.0f"),Girl,Ok,Ok&&St.Visible,Ok?*St.Error:TEXT(""),S.Rotations[0].Yaw);return true;}
 // Camera from his front-left (camera yaw is the direction it looks; his forward is facing yaw + 90).
 if(Label==TEXT("@cam")){S.Yaw=S.Rotations[0].Yaw+90.f+180.f+35.f;S.Pitch=-8.f;S.Distance=320.f;return true;}
 if(Label==TEXT("@log")){const FVector Rel=S.Agents[Girl-1].Position-S.Male;const FVector Fwd=FRotator(0,S.Rotations[0].Yaw+90.f,0).Vector();
  UE_LOG(LogTemp,Display,TEXT("Street probe pose: him yaw=%.0f her yaw=%.0f her offset=(%.0f,%.0f) dist=%.0f ahead=%.0f react=%s/%s"),S.Rotations[0].Yaw,S.Rotations[Girl].Yaw,Rel.X,Rel.Y,Rel.Size2D(),FVector::DotProduct(Rel,Fwd),*S.LoverReact.Clip,*S.LoverReact.MaleClip);return true;}
 if(Label==TEXT("@farewell")){FarewellCompanion();return true;}
 // Both of them from the side (camera yaw looks across the line between them).
 if(Label==TEXT("@camboth")){S.Yaw=(S.Agents[Girl-1].Position-S.Male).Rotation().Yaw+90.f;S.Pitch=-6.f;S.Distance=520.f;return true;}
 if(Label==TEXT("@endpair")){FinishPhoto();return true;}
 if(Label==TEXT("@companion")){BeginCompanion(Girl);return true;}
 // @do:<act> his, @hers:<act> hers: through the heroine module (Interact), the result in the log.
 if(Label.StartsWith(TEXT("@do:"))||Label.StartsWith(TEXT("@hers:"))){FOKHeroineArgs A;A.Hers=Label.StartsWith(TEXT("@hers:"));const FString Act=Label.Mid(A.Hers?6:4);
  const FOKHeroineResult R=Interact(Girl,Act,A);
  UE_LOG(LogTemp,Display,TEXT("UI probe heroine: actor=%d %s %s known=%d done=%d refused=%d reaction=%d why=%s stage=%s"),Girl,A.Hers?TEXT("hers"):TEXT("his"),*Act,int32(R.Known),int32(R.Done),int32(R.Refused),R.Reaction,*R.Why,
   *OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Girl]),S.Manifest.TalkBudget).Stage);
  return R.Known;}
 // @lean / @promise: who pays a ¥120 checkout with her, before and after she says this one is on her.
 // @chaste:<n> sets her chastity (the panel's setter); @alone asks her to be alone with him (no scene starts).
 if(Label.StartsWith(TEXT("@stat:"))){FString F,V;Label.Mid(6).Split(TEXT(":"),&F,&V);SetCharacterStat(Girl,FCString::Atoi(*F),FCString::Atof(*V));return true;}
 if(Label==TEXT("@stage")){UE_LOG(LogTemp,Display,TEXT("UI probe chastity: actor=%d stage=%s favor=%.0f cap=%d"),Girl,*OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Girl]),S.Manifest.TalkBudget).Stage,
   OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Girl]),S.Manifest.TalkBudget).Favor,OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Girl]),S.Manifest.TalkBudget).StrangerCap);return true;}
 if(Label.StartsWith(TEXT("@chaste:"))){SetCharacterStat(Girl,5,FCString::Atof(*Label.Mid(8)));return true;}
 if(Label==TEXT("@dbg")){UE_LOG(LogTemp,Display,TEXT("UI probe chastity: panel show=%d sliders=%d target=%d selection=%d"),int32(S.ShowDebug),S.DebugSliders,S.Target,S.ContactSelection);return S.ShowDebug&&S.DebugSliders==OKHeroineStats().Num();}
 if(Label==TEXT("@alone")){const bool Yes=AloneConsent(Girl,TEXT("rest"));
  UE_LOG(LogTemp,Display,TEXT("UI probe chastity: actor=%d stage=%s chastity=%.0f charm=%.0f alone=%d"),Girl,*OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[Girl]),S.Manifest.TalkBudget).Stage,ChastityOf(Girl),PlayerLook(),int32(Yes));return true;}
 if(Label==TEXT("@talk")){BeginTalk(Girl);return S.Target==Girl;}
 if(Label==TEXT("@promise")){S.HerTreatPromise.Add(Girl,OKStreetMindNow(S)+360);return true;}
 if(Label==TEXT("@lean")){FString Lean;float Insist=0;HeroineCheckoutLean(Girl,120,Lean,Insist);
  UE_LOG(LogTemp,Display,TEXT("UI probe heroine: actor=%d lean=%s insist=%.2f promise=%d"),Girl,*Lean,Insist,int32(S.HerTreatPromise.Contains(Girl)));return true;}
 if(Label==TEXT("@giftnote")){const int32 Before=S.Store.Rumors.Num();HeroineNote(Girl,TEXT("gift"),TEXT("他送了我測試花束（¥1）"),FString(),0.5f,TEXT("測試花束"));
  UE_LOG(LogTemp,Display,TEXT("UI probe heroine: actor=%d gift rumours %d -> %d last=%s"),Girl,Before,S.Store.Rumors.Num(),S.Store.Rumors.Num()?*S.Store.Rumors.Last().Text:TEXT(""));return true;}
 // A booking made with her, missed: the place blacklists him all the same (P3).
 if(Label==TEXT("@missbooking")){AddAppointment(TEXT("reservation"),Girl,TEXT("lottery"),OKStreetMindNow(S)-300,10,TEXT("測試：帶她訂位"));S.AppointmentTickMinute=-1;TickAppointments();
  UE_LOG(LogTemp,Display,TEXT("UI probe heroine: actor=%d missed booking blacklisted=%d"),Girl,int32(Blacklisted(TEXT("lottery"),nullptr)));return true;}
 if(Label==TEXT("@pat")){PlayLoverAct(Girl,TEXT("head_pat"),0);return true;}
 if(Label==TEXT("@hand")){PlayLoverAct(Girl,TEXT("hold_hands"),0);return true;}
 if(Label==TEXT("@melon")){
  FOKRuntimeCharacterStatus M,N;const bool Mk=OKGetCrowdRuntimeCharacterStatus(this,0,M),Nk=OKGetCrowdRuntimeCharacterStatus(this,Girl,N);
  UE_LOG(LogTemp,Display,TEXT("Street probe melon: pair=%d companion=%d facility=%d private=%d node=%d collected=%d pending=%d touch=%d react=%d dist=%.0f male=%d/%d npc=%d/%d"),
   S.Pair.Partner,S.Companion,S.Facility,S.PrivateGirl,S.Node,S.CollectedActive,S.RelationshipPending,S.TouchActive,S.LoverReact.Girl,FVector::Dist2D(S.Male,S.Agents[Girl-1].Position),Mk&&M.Visible,Mk&&M.Error.IsEmpty(),Nk&&N.Visible,Nk&&N.Error.IsEmpty());
  BeginPair(Girl,7);return true;}
 Why=TEXT("unknown probe action");return false;
}

bool AOKStreetSocialScene::ProbePress(const FString& Label,FString& Why,int32 Nth){
 auto& S=*State;if(Label.StartsWith(TEXT("@")))return ProbeAction(Label,Why);
 if(!S.Overlay.IsValid()){Why=TEXT("no overlay");return false;}
 TArray<TPair<FString,TSharedPtr<SWidget>>> Buttons;OKProbeCollect(S.Overlay.ToSharedRef(),nullptr,Buttons);
 if(S.PrivateLeave.IsValid())OKProbeCollect(S.PrivateLeave.ToSharedRef(),nullptr,Buttons); // the one-on-one scene's leave button
 // Nth: which of several buttons with the same label (the menu has one "我要" per dish), counted from the top of the list.
 const TPair<FString,TSharedPtr<SWidget>>* Hit=nullptr;
 {int32 Seen=0;for(const auto& B:Buttons)if(B.Key==Label&&B.Value->IsEnabled()&&(!B.Value->GetCachedGeometry().GetLocalSize().IsNearlyZero()||OKProbeScrolledOut(B.Value))&&Seen++==Nth){Hit=&B;break;}}
 if(!Hit&&Nth==0)Hit=Buttons.FindByPredicate([&](const TPair<FString,TSharedPtr<SWidget>>& B){return B.Key==Label;});
 if(!Hit&&Nth==0)Hit=Buttons.FindByPredicate([&](const TPair<FString,TSharedPtr<SWidget>>& B){return B.Key.StartsWith(Label);});
 if(!Hit){Why=TEXT("button not found");return false;}
 const FGeometry G=Hit->Value->GetCachedGeometry();
#if !UE_BUILD_SHIPPING
 // Scrolled out of its list: pressed as a button all the same (no hit test needed), and brought into view for the eye.
 if(OKProbeScrolledOut(Hit->Value)&&Hit->Value->IsEnabled()&&Hit->Value->GetType()==FName(TEXT("SButton"))){
  for(TSharedPtr<SWidget> Up=Hit->Value->GetParentWidget();Up.IsValid();Up=Up->GetParentWidget())if(Up->GetType()==FName(TEXT("SScrollBox")))StaticCastSharedPtr<SScrollBox>(Up)->ScrollDescendantIntoView(Hit->Value,false,EDescendantScrollDestination::Center,0.f); // every enclosing list (the menu, the side panel)
  UE_LOG(LogTemp,Display,TEXT("Street control press: %s (scrolled out, clicked)"),*Label);StaticCastSharedPtr<SButton>(Hit->Value)->SimulateClick();return true;}
#endif
 if(G.GetLocalSize().IsNearlyZero()){
  for(TSharedPtr<SWidget> Up=Hit->Value->GetParentWidget();Up.IsValid();Up=Up->GetParentWidget())
   if(Up->GetType()==FName(TEXT("SScrollBox"))){StaticCastSharedPtr<SScrollBox>(Up)->ScrollDescendantIntoView(Hit->Value,false,EDescendantScrollDestination::Center,0.f);Why=TEXT("scrolled it into view; press again");return false;}
  Why=TEXT("button has no size (not laid out / hidden)");return false;
 }
 if(!Hit->Value->IsEnabled()){Why=TEXT("button disabled");return false;}
#if !UE_BUILD_SHIPPING
 // A button is clicked as a button: a synthetic mouse press hit-tests the last painted layout, which is stale right after
 // the panel was rebuilt or scrolled (presses that did nothing, the clock pause among them).
 if(Hit->Value->GetType()==FName(TEXT("SButton"))){UE_LOG(LogTemp,Display,TEXT("Street control press: %s (clicked)"),*Label);StaticCastSharedPtr<SButton>(Hit->Value)->SimulateClick();return true;}
#endif
 // Laid out but outside what its scroll box shows (clipped): the click would land on nothing. Scroll it in first.
 for(TSharedPtr<SWidget> Up=Hit->Value->GetParentWidget();Up.IsValid();Up=Up->GetParentWidget())
  if(Up->GetType()==FName(TEXT("SScrollBox"))){
   const FGeometry SG=Up->GetCachedGeometry();const FVector2D C=G.GetAbsolutePositionAtCoordinates(FVector2D(.5f,.5f));
   if(!SG.GetLayoutBoundingRect().ContainsPoint(C)){StaticCastSharedPtr<SScrollBox>(Up)->ScrollDescendantIntoView(Hit->Value,false,EDescendantScrollDestination::Center,0.f);Why=TEXT("scrolled it into view; press again");return false;}
   break;
  }
 const FVector2D P=G.GetAbsolutePositionAtCoordinates(FVector2D(.5f,.5f));
 FSlateApplication& App=FSlateApplication::Get();
 UE_LOG(LogTemp,Display,TEXT("Street control press: %s at (%.0f,%.0f) enabled=%d"),*Label,P.X,P.Y,Hit->Value->IsEnabled()?1:0);
 App.SetCursorPos(P);
 TSet<FKey> Pressed;Pressed.Add(EKeys::LeftMouseButton);
 FPointerEvent Move(App.CursorPointerIndex,P,P,TSet<FKey>(),FKey(),0,FModifierKeysState());App.ProcessMouseMoveEvent(Move);
 FPointerEvent Down(App.CursorPointerIndex,P,P,Pressed,EKeys::LeftMouseButton,0,FModifierKeysState());App.ProcessMouseButtonDownEvent(nullptr,Down);
 FPointerEvent Up(App.CursorPointerIndex,P,P,TSet<FKey>(),EKeys::LeftMouseButton,0,FModifierKeysState());App.ProcessMouseButtonUpEvent(Up);
 return true;
}

void AOKStreetSocialScene::TickUiProbe(){
 auto& S=*State;
 if(S.ProbeStep<0){if(FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STREET_UI_PROBE")).IsEmpty()||S.SelfTest||!S.AllReady)return;S.ProbeStep=0;S.ProbeAt=FPlatformTime::Seconds()+2;if(!S.MouseUI){S.MouseUI=true;ApplyMouseMode(true);RefreshUI();}}
 const double Now=FPlatformTime::Seconds();if(Now<S.ProbeAt||S.ProbeStep>=1000)return;S.ProbeAt=Now+1.2;
 // Each step: press a label, then check the state one step later.
 struct FStep{const TCHAR* Press;TFunction<bool(FOKStreetSceneState&)> Check;};
 const TArray<FStep> Steps={
  // Into opening hours first (the shop opens 09–23).
  {TEXT("+1小時"),[](FOKStreetSceneState&){return true;}},
  {TEXT("+1小時"),[](FOKStreetSceneState&){return true;}},
  {TEXT("+1小時"),[](FOKStreetSceneState&){return true;}},
  // Phone view of bought tickets: no buying tabs.
  {TEXT("理財"),[](FOKStreetSceneState& L){return L.FinMenuOpen;}},
  {TEXT("手機：我的彩票"),[](FOKStreetSceneState& L){return L.App==TEXT("lottery")&&L.AppReadOnly&&L.LotTab==TEXT("mine");}},
  {TEXT("開獎結果"),[](FOKStreetSceneState& L){return L.LotTab==TEXT("board");}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
  {TEXT("星運彩券行櫃台"),[](FOKStreetSceneState& L){return L.App==TEXT("lottery")&&L.LotTab==TEXT("sports");}},
  {TEXT("星光 6/49"),[](FOKStreetSceneState& L){return L.LotTab==TEXT("star649");}},
  {TEXT("電腦選號 1 注"),[](FOKStreetSceneState& L){return L.FinNotice.Contains(TEXT("出票"));}},
  {TEXT("01"),[](FOKStreetSceneState& L){return L.LotPicks.Contains(1);}},
  {TEXT("幸運刮刮卡"),[](FOKStreetSceneState& L){return L.LotTab==TEXT("scratch");}},
  {TEXT("「好運連連」"),[](FOKStreetSceneState& L){return L.App==TEXT("scratch");}},
  {TEXT("全部刮開"),[](FOKStreetSceneState& L){return L.Scratch.IsValid()&&L.Scratch->Revealed&&L.Scratch->Cells.Num()==9;}},
  {TEXT("回櫃台"),[](FOKStreetSceneState& L){return L.App==TEXT("lottery");}},
  {TEXT("競技彩"),[](FOKStreetSceneState& L){return L.LotTab==TEXT("sports");}},
  {TEXT("我的彩券・兌獎"),[](FOKStreetSceneState& L){return L.LotTab==TEXT("mine");}},
  {TEXT("開獎看板"),[](FOKStreetSceneState& L){return L.LotTab==TEXT("board");}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
  {TEXT("手機：星運彩官網"),[](FOKStreetSceneState& L){return L.App==TEXT("web");}},
  {TEXT("開獎公告"),[](FOKStreetSceneState& L){return L.WebTab==TEXT("draws");}},
  {TEXT("走勢圖"),[](FOKStreetSceneState& L){return L.WebTab==TEXT("trend");}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
  {TEXT("手機：筆記本"),[](FOKStreetSceneState& L){return L.App==TEXT("notebook")&&L.FinNotebook.IsValid();}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
  {TEXT("手機：看盤"),[](FOKStreetSceneState& L){return L.App==TEXT("cn")||L.App==TEXT("us");}},
  {TEXT("美股：浮標 Buoy"),[](FOKStreetSceneState& L){return L.App==TEXT("us")&&L.FinQuote.IsValid();}},
  {TEXT("1M"),[](FOKStreetSceneState& L){return L.FinPeriod==TEXT("1M")&&L.FinChart.IsValid()&&L.FinChart->C.Num()>0;}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
  {TEXT("手機：網銀"),[](FOKStreetSceneState& L){return L.App==TEXT("atm");}},
  {TEXT("存入 200"),[](FOKStreetSceneState& L){return L.Store.PlayerBank>=200;}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
  {TEXT("手機：我的公司"),[](FOKStreetSceneState& L){return L.App==TEXT("venture");}},
  {TEXT("成立公司（資本額"),[](FOKStreetSceneState& L){return !L.FinNotice.IsEmpty();}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
 };
 // OK_STREET_UI_PROBE=pc: the trading terminals at a computer (start the player in boys_dorm).
 const TArray<FStep> PcSteps={
  {TEXT("電腦：浮標 Buoy（美股）"),[](FOKStreetSceneState& L){return L.App==TEXT("us")&&!L.AppReadOnly;}},
  {TEXT("入金（錢包→帳戶）"),[](FOKStreetSceneState& L){return L.FinNotice.Contains(TEXT("入金"))&&L.FinAccount.IsValid();}},
  {TEXT("限價"),[](FOKStreetSceneState& L){return L.FinOrderKind==TEXT("limit");}},
  {TEXT("市價"),[](FOKStreetSceneState& L){return L.FinOrderKind==TEXT("market");}},
  {TEXT("下單"),[](FOKStreetSceneState& L){return !L.FinNotice.IsEmpty();}},
  {TEXT("A 股：乘風行情"),[](FOKStreetSceneState& L){return L.App==TEXT("cn");}},
  {TEXT("日K"),[](FOKStreetSceneState& L){return L.FinPeriod==TEXT("d");}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
 };
 // OK_STREET_UI_PROBE=away: inside the shop but away from the counter, the counter menu must not be offered.
 const TArray<FStep> AwaySteps={
  {TEXT("+1小時"),[](FOKStreetSceneState&){return true;}},
  {TEXT("+1小時"),[](FOKStreetSceneState&){return true;}},
  {TEXT("+1小時"),[](FOKStreetSceneState&){return true;}},
  {TEXT("星運彩券行櫃台"),[](FOKStreetSceneState& L){return L.App.IsEmpty()&&L.ProbeWhy==TEXT("button not found");}},
  {TEXT("手機：我的彩票"),[](FOKStreetSceneState& L){return L.App==TEXT("lottery")&&L.AppReadOnly;}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
 };
 // OK_STREET_UI_PROBE=campus: at the CSIE office counter (OK_STREET_START_PLACE=campus_ee@office_csie).
 const TArray<FStep> CampusSteps={
  {TEXT("資訊工程系 辦公室"),[](FOKStreetSceneState& L){return L.App==TEXT("dept")&&L.Store.PlayerAcademic.Dept==TEXT("csie");}},
  {TEXT("申請成績單"),[](FOKStreetSceneState& L){return L.ShowTranscript;}},
  {TEXT("辦理休學"),[](FOKStreetSceneState& L){return L.Store.PlayerAcademic.Status==TEXT("leave");}},
  {TEXT("辦理復學"),[](FOKStreetSceneState& L){return L.Store.PlayerAcademic.Status==TEXT("enrolled");}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
  {TEXT("讀書"),[](FOKStreetSceneState& L){return L.Studying;}},
  {TEXT("+1小時"),[](FOKStreetSceneState& L){return L.Studying;}},
  {TEXT("停止讀書"),[](FOKStreetSceneState& L){return !L.Studying&&L.Message.Contains(TEXT("智力"));}},
  {TEXT("手機：我的課表"),[](FOKStreetSceneState& L){return L.App==TEXT("timetable");}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
 };
 // OK_STREET_UI_PROBE=stairs: on the ground-floor landing (OK_STREET_START_PLACE=campus_ee@stair1).
 const TArray<FStep> StairSteps={
  {TEXT("樓梯：上樓到 2F"),[](FOKStreetSceneState& L){return L.Male.Z>200&&L.Message.Contains(TEXT("2F"));}},
  {TEXT("樓梯：上樓到 3F"),[](FOKStreetSceneState& L){return L.Male.Z>500;}},
  {TEXT("樓梯：下樓到 2F"),[](FOKStreetSceneState& L){return L.Male.Z>200&&L.Male.Z<500;}},
  {TEXT("樓梯：下樓到 1F"),[](FOKStreetSceneState& L){return L.Male.Z<200;}},
 };
 // OK_STREET_UI_PROBE=term with OK_STREET_START_DAY / OK_STREET_START_CLOCK: a few hours of a class day or an exam
 // afternoon; something must be counted (attendance, a midterm or a final grade). Writes inspect.json beside the save.
 auto Counted=[](FOKStreetSceneState& L){auto Any=[](const FOKStreetAcademic& A){for(const auto& C:A.Courses)if(C.Held>0||C.Midterm>=0||C.Grade>=0)return true;return false;};
  if(Any(L.Store.PlayerAcademic))return true;for(const auto& P:L.Store.Academics)if(Any(P.Value))return true;return false;};
 const TArray<FStep> TermSteps={
  {TEXT("+1小時"),[](FOKStreetSceneState&){return true;}},
  {TEXT("+1小時"),[](FOKStreetSceneState&){return true;}},
  {TEXT("+1小時"),[](FOKStreetSceneState&){return true;}},
  {TEXT("+1小時"),Counted},
 };
 // OK_STREET_UI_PROBE=admin: the administration building's 教務處 (OK_STREET_START_PLACE=campus_ad@office_aa).
 const TArray<FStep> AdminSteps={
  {TEXT("教務處（轉系）"),[](FOKStreetSceneState& L){return L.App==TEXT("dept")&&L.DeptOffice==TEXT("aa");}},
  {TEXT("轉入電機工程系"),[](FOKStreetSceneState& L){return L.FinNotice.Contains(TEXT("轉系"));}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
 };
 // OK_STREET_UI_PROBE=bank: the bank app's tabs (deposit, card, fees, net worth, alerts) and an alert on a stock.
 const TArray<FStep> BankSteps={
  {TEXT("手機：網銀"),[](FOKStreetSceneState& L){return L.App==TEXT("atm");}},
  {TEXT("存入 1000"),[](FOKStreetSceneState& L){return L.Store.PlayerBank>=1000;}},
  {TEXT("定存"),[](FOKStreetSceneState& L){return L.AtmTab==TEXT("deposit");}},
  {TEXT("存 12 個月"),[](FOKStreetSceneState& L){return L.Store.Banking.Deposits.Num()==1&&L.Store.Banking.Deposits[0].Amount==1000;}},
  {TEXT("提前解約"),[](FOKStreetSceneState& L){return L.Store.Banking.Deposits.Num()==0;}},
  {TEXT("信用卡"),[](FOKStreetSceneState& L){return L.AtmTab==TEXT("card");}},
  {TEXT("申辦學生信用卡"),[](FOKStreetSceneState& L){return L.Store.Banking.Card.Issued;}},
  {TEXT("繳費・貸款"),[](FOKStreetSceneState& L){return L.AtmTab==TEXT("fees");}},
  {TEXT("總資產"),[](FOKStreetSceneState& L){return L.AtmTab==TEXT("worth")&&L.WorthChart.IsValid()&&L.WorthChart->C.Num()>=1;}},
  {TEXT("到價提醒"),[](FOKStreetSceneState& L){return L.AtmTab==TEXT("alerts");}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
  {TEXT("手機：看盤"),[](FOKStreetSceneState& L){return L.App==TEXT("cn")||L.App==TEXT("us");}},
  {TEXT("漲跌超過 %"),[](FOKStreetSceneState& L){return L.FinNotice==TEXT("已設定提醒。");}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
 };
 // OK_STREET_UI_PROBE=bankday with OK_STREET_START_DAY=2010-09-27 OK_STREET_START_CLOCK=03:58: the clock runs past 04:00
 // (the +1小時 buttons do not roll the day) — net worth is recorded and, the term's fees unpaid past the deadline, he is put on leave.
 auto Wait=[](FOKStreetSceneState&){return true;};
 const TArray<FStep> BankDaySteps={
  {TEXT("手機：我的課表"),Wait},{TEXT("關閉 (Esc)"),Wait},{TEXT("手機：我的課表"),Wait},{TEXT("關閉 (Esc)"),Wait},{TEXT("手機：我的課表"),Wait},{TEXT("關閉 (Esc)"),Wait},
  {TEXT("手機：我的課表"),Wait},{TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.Store.Banking.NetWorth.Num()>=1&&L.Store.PlayerAcademic.Status==TEXT("leave");}},
 };
 // OK_STREET_UI_PROBE=invest with OK_STREET_INVESTORS=1 and a trading morning: the heroines take their turns.
 const TArray<FStep> InvestSteps={
  {TEXT("+1小時"),[](FOKStreetSceneState&){return true;}},{TEXT("+1小時"),[](FOKStreetSceneState&){return true;}},
  {TEXT("+1小時"),[](FOKStreetSceneState&){return true;}},{TEXT("+1小時"),[](FOKStreetSceneState& L){return L.HerInvestTotal.Num()>0;}},
 };
 // OK_STREET_UI_PROBE=father with OK_STREET_TEST_DEBT=<more than he has>: act 1, the bankruptcy scene with nothing paid yet;
 // act 2, as soon as it is closed, the unknown number's text and transfer.
 const TArray<FStep> FatherSteps={
  {TEXT("（等待）"),[](FOKStreetSceneState& L){return L.App==TEXT("bankrupt")&&L.Store.Banking.Bankrupt&&L.Store.Banking.FatherGifts==0&&L.Store.PlayerBank==0&&L.Store.Banking.Card.Stopped
   &&L.BankruptLines.ContainsByPredicate([](const FString& X){return X.Contains(TEXT("知道了這件事"));});}}, // the save has a lover
  {TEXT("關閉 (Esc)"),Wait},{TEXT("（等待）"),Wait},
  {TEXT("（等待）"),[](FOKStreetSceneState& L){return L.Store.Banking.FatherGifts==1&&!L.Store.Banking.Bankrupt&&L.Store.Banking.FatherTotal%100000==0&&L.Store.PlayerBank>=100000;}},
  {TEXT("手機（1 則未讀）"),[](FOKStreetSceneState& L){return L.App==TEXT("phone")&&L.PhoneThread==OKStreetFatherThread&&L.Store.Banking.FatherSeen==1;}},
  {TEXT("關閉 (Esc)"),[](FOKStreetSceneState& L){return L.App.IsEmpty();}},
 };
 // OK_STREET_UI_PROBE=heroine: his and her acts through Interact on the nearest heroine (the save's lover), recorded.
 const TArray<FStep> HeroineSteps={
  {TEXT("@summon"),Wait},{TEXT("（等待）"),Wait},{TEXT("@do:kiss"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},
  {TEXT("@summonlover"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},
  {TEXT("@do:hold_hands"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},
  {TEXT("@do:kiss"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},
  {TEXT("@hers:hug"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},
  {TEXT("@lean"),Wait},{TEXT("@promise"),Wait},{TEXT("@lean"),[](FOKStreetSceneState& L){return L.HerTreatPromise.Num()==1;}},{TEXT("@giftnote"),Wait},{TEXT("（等待）"),Wait},
  {TEXT("@talk"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("膝枕"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@shot:lap"),Wait},
  {TEXT("@missbooking"),[](FOKStreetSceneState& L){const int32* U=L.Store.Blacklist.Find(TEXT("lottery"));return U&&*U>L.Store.CalendarDay;}},
 };
 // OK_STREET_UI_PROBE=panel: his lover in front of him, talking, and every button of her panel pressed for real (the
 // labels on screen), the state after each in the log ("Street UI probe panel:").
 auto Panel=[](FOKStreetSceneState& L){UE_LOG(LogTemp,Display,TEXT("Street UI probe panel: target=%d partner-notice=%s message=%s react=%s male=%s choices=%d outing=%d pending=%d"),L.Target,*L.FreeTalkNotice,*L.Message,*L.LoverReact.Clip,*L.LoverReact.MaleClip,L.Choices.Num(),L.OutingGirl,L.PendingIntimacy);return true;};
 const TArray<FStep> PanelSteps={
  {TEXT("理財"),[](FOKStreetSceneState& L){return L.FinMenuOpen;}},{TEXT("收起"),[](FOKStreetSceneState& L){return !L.FinMenuOpen;}},
  {TEXT("@summonlover"),Wait},{TEXT("（等待）"),Wait},{TEXT("@talk"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Panel},
  {TEXT("借她 ¥200"),Panel},{TEXT("（等待）"),Panel},{TEXT("（等待）"),Panel},{TEXT("一起喝酒"),Panel},{TEXT("（等待）"),Panel},{TEXT("更親密"),Panel},{TEXT("（等待）"),Panel},{TEXT("（等待）"),Panel},{TEXT("（等待）"),Panel},
  {TEXT("@talk"),Wait},{TEXT("（等待）"),Wait},{TEXT("牽手"),Panel},{TEXT("（等待）"),Panel},{TEXT("摸頭"),Panel},{TEXT("（等待）"),Panel},{TEXT("擁抱"),Panel},{TEXT("（等待）"),Panel},
  {TEXT("親吻"),Panel},{TEXT("（等待）"),Panel},{TEXT("膝枕"),Panel},{TEXT("（等待）"),Panel},{TEXT("一起躺下"),Panel},{TEXT("（等待）"),Panel},
  {TEXT("一起喝酒"),Panel},{TEXT("（等待）"),Panel},{TEXT("@endpair"),Wait},{TEXT("@talk"),Wait},{TEXT("（等待）"),Wait},{TEXT("借她 ¥200"),Panel},{TEXT("（等待）"),Panel},{TEXT("（等待）"),Panel},
  {TEXT("@endpair"),Wait},{TEXT("@talk"),Wait},{TEXT("（等待）"),Wait},{TEXT("更親密"),Panel},{TEXT("（等待）"),Panel},{TEXT("（等待）"),Panel},
 };
 // OK_STREET_UI_PROBE=chastity: the stats panel from its button, and being alone with a stranger by chastity.
 const TArray<FStep> ChastitySteps={
  {TEXT("@summonstranger"),Wait},{TEXT("（等待）"),Wait},{TEXT("@talk"),Wait},{TEXT("（等待）"),Wait},
  {TEXT("女主數值（查看／修改）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@dbg"),Wait},{TEXT("@shot:stats"),Wait},
  {TEXT("@chaste:5"),Wait},{TEXT("@alone"),Wait},{TEXT("@alone"),Wait},{TEXT("@alone"),Wait},
  {TEXT("@chaste:100"),Wait},{TEXT("@alone"),Wait},{TEXT("@alone"),Wait},{TEXT("（等待）"),Wait},{TEXT("@shot:stats2"),Wait},
  {TEXT("@stage"),Wait},{TEXT("@stat:6:20"),Wait},{TEXT("@stat:0:19"),Wait},{TEXT("（等待）"),Wait},{TEXT("@stage"),Wait},{TEXT("@stat:0:25"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@stage"),Wait},
  {TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@shot:glad"),Wait},
 };
 // OK_STREET_UI_PROBE=lover: the goodbye wave, the head pat and holding hands (contact poses), her watermelon swings.
 auto ReactIs=[](const TCHAR* Clip){return [Clip](FOKStreetSceneState& L){return L.LoverReact.Clip==Clip;};};
 const TArray<FStep> LoverSteps={
  {TEXT("@summon"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@companion"),[](FOKStreetSceneState& L){return L.Companion!=INDEX_NONE;}},{TEXT("（等待）"),Wait},
  {TEXT("@farewell"),ReactIs(TEXT("lover_wave.okanim"))},{TEXT("@cam"),Wait},{TEXT("@shot:wave"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},
  {TEXT("@summon"),Wait},{TEXT("（等待）"),Wait},{TEXT("@pat"),[](FOKStreetSceneState& L){return !L.LoverReact.Clip.IsEmpty()&&L.LoverReact.MaleClip.IsEmpty();}},{TEXT("@log"),Wait},{TEXT("@cam"),Wait},{TEXT("@log"),Wait},{TEXT("（等待）"),Wait},{TEXT("@shot:pat1"),Wait},{TEXT("@shot:pat2"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},
  {TEXT("@summon"),Wait},{TEXT("（等待）"),Wait},{TEXT("@hand"),ReactIs(TEXT("lover_hand_shy.okanim"))},{TEXT("@log"),Wait},{TEXT("@cam"),Wait},{TEXT("@log"),Wait},{TEXT("（等待）"),Wait},{TEXT("@shot:hand1"),Wait},{TEXT("@shot:hand2"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},
  {TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@summon"),Wait},{TEXT("（等待）"),Wait},{TEXT("@melon"),[](FOKStreetSceneState& L){return L.Pair.Partner!=INDEX_NONE&&L.Pair.FemaleStrikes&&L.Pair.FemaleClip==TEXT("watermelon_strike.okanim");}},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@shot:melon1"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@shot:melon2"),Wait},
  // The second swing is the other take.
  {TEXT("@endpair"),[](FOKStreetSceneState& L){return L.Pair.Partner==INDEX_NONE;}},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@summon"),Wait},{TEXT("（等待）"),Wait},
  {TEXT("@melon"),[](FOKStreetSceneState& L){return L.Pair.Partner!=INDEX_NONE&&L.Pair.FemaleClip==TEXT("watermelon_strike.okanim");}},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@shot:melon3"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@shot:melon4"),Wait},
 };
// OK_STREET_UI_PROBE=melon: her two swings (the original strike, then the other take), filmed from the side.
 const TArray<FStep> MelonSteps={
  {TEXT("@summon"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@melon"),[](FOKStreetSceneState& L){return L.Pair.Partner!=INDEX_NONE&&L.Pair.FemaleClip==TEXT("watermelon_strike.okanim");}},{TEXT("@camboth"),Wait},
  {TEXT("@shot:m1a"),Wait},{TEXT("@shot:m1b"),Wait},{TEXT("@shot:m1c"),Wait},{TEXT("@shot:m1d"),Wait},
  {TEXT("@endpair"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@summon"),Wait},{TEXT("（等待）"),Wait},{TEXT("@melon"),[](FOKStreetSceneState& L){return L.Pair.Partner!=INDEX_NONE&&L.Pair.FemaleClip==TEXT("watermelon_strike.okanim");}},{TEXT("@camboth"),Wait},
  {TEXT("@shot:m2a"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@shot:m2b"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("（等待）"),Wait},{TEXT("@shot:m2c"),Wait},{TEXT("@shot:m2d"),Wait},
 };
 const FString Mode=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STREET_UI_PROBE"));
 const TArray<FStep>& Run=Mode==TEXT("pc")?PcSteps:Mode==TEXT("away")?AwaySteps:Mode==TEXT("campus")?CampusSteps:Mode==TEXT("stairs")?StairSteps:Mode==TEXT("term")?TermSteps:Mode==TEXT("admin")?AdminSteps:Mode==TEXT("bank")?BankSteps:Mode==TEXT("bankday")?BankDaySteps:Mode==TEXT("invest")?InvestSteps:Mode==TEXT("father")?FatherSteps:Mode==TEXT("lover")?LoverSteps:Mode==TEXT("melon")?MelonSteps:Mode==TEXT("heroine")?HeroineSteps:Mode==TEXT("chastity")?ChastitySteps:Mode==TEXT("panel")?PanelSteps:Steps;
 // Check the previous press.
 if(S.ProbeStep>0){
  const int32 Prev=S.ProbeStep-1;const bool Ok=Run[Prev].Check(S);
  auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("press"),Run[Prev].Press);Row->SetBoolField(TEXT("ok"),Ok);Row->SetStringField(TEXT("why"),S.ProbeWhy);
  Row->SetStringField(TEXT("app"),S.App);Row->SetStringField(TEXT("notice"),S.FinNotice);S.ProbeRows.Add(MakeShared<FJsonValueObject>(Row));
  UE_LOG(LogTemp,Display,TEXT("Street UI probe %s: %s (%s) app=%s notice=%s"),Ok?TEXT("PASS"):TEXT("FAIL"),Run[Prev].Press,*S.ProbeWhy,*S.App,*S.FinNotice);
 }
 if(S.ProbeStep>=Run.Num()){
  int32 Passed=0;for(const auto& R:S.ProbeRows)Passed+=R->AsObject()->GetBoolField(TEXT("ok"));
  auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("passed"),Passed);Root->SetNumberField(TEXT("steps"),S.ProbeRows.Num());Root->SetArrayField(TEXT("rows"),S.ProbeRows);
  FString Text;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));
  FFileHelper::SaveStringToFile(Text,*(FPaths::GetPath(S.Manifest.SavePath)/TEXT("ui_probe.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  if(Mode==TEXT("term"))FFileHelper::SaveStringToFile(InspectJson(FString()),*(FPaths::GetPath(S.Manifest.SavePath)/TEXT("inspect.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  UE_LOG(LogTemp,Display,TEXT("Street UI probe done: %d/%d"),Passed,S.ProbeRows.Num());S.ProbeStep=1000;return;
 }
 // An app closing restores the mouse mode from before it opened (maybe before the probe took the cursor): take it again.
 if(!S.MouseUI){S.MouseUI=true;ApplyMouseMode(true);RefreshUI();}
 S.ProbeWhy.Reset();ProbePress(Run[S.ProbeStep].Press,S.ProbeWhy);++S.ProbeStep;
}
