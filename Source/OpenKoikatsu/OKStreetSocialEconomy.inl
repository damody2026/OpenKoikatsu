// Included by OKStreetSocialScene.cpp: plan §8 life economy — menus, ordering for her, who pays, gifts, loans,
// paydays and opening hours. Money changes go through CommitMoney so the save always matches.

void AOKStreetSocialScene::LoadCatalog(){
 auto& S=*State;
 FString Path=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_STREET_CATALOG"));
 // Staged builds cannot see the project Content folder; the launcher passes the personas file next to it.
 if(Path.IsEmpty()){const FString Personas=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_FREE_TALK_PERSONAS"));if(!Personas.IsEmpty())Path=FPaths::GetPath(Personas)/TEXT("catalog.json");}
 if(Path.IsEmpty()||!FPaths::FileExists(Path))Path=FPaths::ProjectContentDir()/TEXT("IndependentResources/StreetSocialData/catalog.json");
 S.Catalog=OKStreetLoadCatalog(Path);
 if(!S.Catalog.Error.IsEmpty()){UE_LOG(LogTemp,Warning,TEXT("StreetSocial catalog: %s"),*S.Catalog.Error);}
 else{UE_LOG(LogTemp,Display,TEXT("StreetSocial catalog: %d items, %d places from %s"),S.Catalog.Items.Num(),S.Catalog.Places.Num(),*Path);}
}

FDateTime AOKStreetSocialScene::StreetNow() const{
 const auto& S=*State;return OKStreetCalendarDateTime(S.Store.CalendarDay,OKStreetClockHours(S.ClockStartHours,FPlatformTime::Seconds()-S.ClockStartWall));
}

// The place the player stands in that sells something, or empty.
// The floor he is on in a building with elevators (1-based, by the nearest landing's height); 0 elsewhere.
int32 AOKStreetSocialScene::FloorHere(const FString& Place) const{
 const FOKStreetPlace* P=OKStreetFindPlace(Place);if(!P)return 0;const auto Lifts=OKStreetPlaceLifts(*P);if(Lifts.IsEmpty())return 0;
 int32 Floor=0;for(int32 F=0;F<Lifts.Num();++F)if(FMath::Abs(Lifts[F]->Position.Z-State->Male.Z)<FMath::Abs(Lifts[Floor]->Position.Z-State->Male.Z))Floor=F;
 return Floor+1;
}
// What this place sells where he stands: this month's, and in a building only this floor's (the 10F view restaurant's
// coffee is not on the 9F food court's menu).
// The menu id the model meant: its own id when it is one, else one of the shop's items it named another way
// ("cafe_lemon_tart" for cf_lemon_tart, or the dish's name). An unknown id threw away a whole order.
FString AOKStreetSocialScene::ResolveItemId(const FString& Id,const FString& Place) const{
 const auto& S=*State;if(S.Catalog.Find(Id)||Place.IsEmpty())return Id;
 FString Tail=Id;int32 Us=INDEX_NONE;if(Id.FindChar(TEXT('_'),Us))Tail=Id.Mid(Us+1);
 for(const auto* I:ItemsHere(Place)){FString ITail=I->Id;int32 U=INDEX_NONE;if(I->Id.FindChar(TEXT('_'),U))ITail=I->Id.Mid(U+1);
  if((!Tail.IsEmpty()&&ITail==Tail)||(!I->Name.IsEmpty()&&Id.Contains(I->Name))){UE_LOG(LogTemp,Display,TEXT("Street order: %s read as %s (%s)"),*Id,*I->Id,*I->Name);return I->Id;}}
 return Id;
}

TArray<const FOKStreetCatalogItem*> AOKStreetSocialScene::ItemsHere(const FString& Place) const{
 TArray<const FOKStreetCatalogItem*> Out=OKStreetItemsAt(State->Catalog,Place,StreetNow().GetMonth());
 const int32 Floor=FloorHere(Place);if(Floor>0)Out.RemoveAll([Floor](const FOKStreetCatalogItem* I){return I->Floor>0&&I->Floor!=Floor;});
 return Out;
}
bool AOKStreetSocialScene::SoldHere(const FOKStreetCatalogItem& Item,const FString& Place) const{
 if(Item.Place!=Place)return false;const int32 Floor=FloorHere(Place);return Floor<=0||Item.Floor<=0||Item.Floor==Floor;
}

// The shop he is walking her to (an outing to a place that sells things), for orders said on the way; empty otherwise.
FString AOKStreetSocialScene::OutingShop(int32 Girl) const{
 const auto& S=*State;if(S.OutingGirl!=Girl||Girl==INDEX_NONE)return FString();
 FString Place=S.OutingActivity.StartsWith(TEXT("go:"))?S.OutingActivity.Mid(3):FString();
 if(Place.IsEmpty())if(const FOKStreetActivity* A=OKStreetFindActivity(S.OutingActivity))Place=A->Place?A->Place:TEXT("");
 return !Place.IsEmpty()&&S.Catalog.Items.ContainsByPredicate([&](const FOKStreetCatalogItem& I){return I.Place==Place;})?Place:FString();
}

// The shop her orders go to: the one here, unless she is walking with him to somewhere else (passing the park on the
// way to lunch, "shall we buy fish food?" bought it at once); then none here, and OutingShop carries them there.
FString AOKStreetSocialScene::OrderShopHere(int32 Girl) const{
 const auto& S=*State;const FString Here=ShopPlaceHere();if(Here.IsEmpty()||S.OutingGirl!=Girl||Girl==INDEX_NONE)return Here;
 FString Dest=S.OutingActivity.StartsWith(TEXT("go:"))?S.OutingActivity.Mid(3):FString();
 if(Dest.IsEmpty())if(const FOKStreetActivity* A=OKStreetFindActivity(S.OutingActivity))Dest=A->Place?A->Place:TEXT("");
 return Dest.IsEmpty()||Dest==Here?Here:FString();
}

FString AOKStreetSocialScene::ShopPlaceHere() const{
 const auto& S=*State;
 // In a facility (the restaurant's private room) he is in its own room, away from the place's floor: that place's menu.
 if(const auto* F=S.Facility!=INDEX_NONE?OKStreetFacilityActivity(S.Facility):nullptr)
  if(S.Catalog.Items.ContainsByPredicate([&](const FOKStreetCatalogItem& I){return I.Place==F->Place;}))return F->Place;
 for(const auto& Place:OKStreetPlaces())if(OKStreetInsidePlace(Place,S.Male)&&S.Catalog.Items.ContainsByPredicate([&](const FOKStreetCatalogItem& I){return I.Place==Place.Id;}))return Place.Id;
 return FString();
}

bool AOKStreetSocialScene::PlaceClosedNow(const FString& PlaceId,FString& Why) const{return PlaceClosedAt(PlaceId,StreetNow(),Why);}
// Whether a place is closed at a time (a meeting booked for 8:00 at a shop opening at 10:00).
bool AOKStreetSocialScene::PlaceClosedAt(const FString& PlaceId,const FDateTime& Now,FString& Why) const{
 auto Request=OKRulesRequest(TEXT("place_open"));Request->SetStringField(TEXT("place"),PlaceId);
 Request->SetNumberField(TEXT("hours"),Now.GetHour()+Now.GetMinute()/60.0);Request->SetNumberField(TEXT("weekday"),int32(Now.GetDayOfWeek()));
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(*State,Request);if(!Answer||Answer->GetBoolField(TEXT("open")))return false;
 Why=Answer->GetStringField(TEXT("why"));return true;
}

// Writes a changed store; self-tests keep the file untouched.
bool AOKStreetSocialScene::CommitStore(FOKStreetStore&& Candidate){
 auto& S=*State;FString Error;
 S.Store=MoveTemp(Candidate);
 if(!S.SelfTest&&!S.Manifest.SavePath.IsEmpty())OKStreetAtomicSaveStoreAsync(S.Manifest.SavePath,S.Store); // serialized by the save task
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_FlushRulesEffects);FlushRulesEffects();}return true;
}

TSharedPtr<FJsonObject> AOKStreetSocialScene::ProfileOf(int32 Character) const{
 if(!State->Manifest.Characters.IsValidIndex(Character))return nullptr;
 return State->MindProfiles.FindRef(OKStreetStableId(State->Manifest.Characters[Character]));
}
// Satiety is how full she is, 0..100 of her own stomach; food brings the same to everyone, so it fills a bigger one by
// less of itself: a dish's satiety times 100 / SatietyMax (R3-269, needs.rs advance_with).
float AOKStreetSocialScene::SatietyMax(int32 Character) const{
 double Max=100;const TSharedPtr<FJsonObject> P=ProfileOf(Character);if(P.IsValid())P->TryGetNumberField(TEXT("satiety_max"),Max);
 return FMath::Clamp(float(Max),50.f,1000.f);
}
float AOKStreetSocialScene::MoodNow(int32 Character) const{
 if(!State->Manifest.Characters.IsValidIndex(Character))return 0.f;
 const FOKStreetRelation R=OKStreetViewRelation(State->Store,OKStreetStableId(State->Manifest.Characters[Character]),State->Manifest.TalkBudget);
 return R.MoodLevel*FMath::Pow(.5f,float(FMath::Max<int64>(0,OKStreetMindNow(*State)-R.MoodMinute))/360.f);
}

void AOKStreetSocialScene::AddToCart(const FString& ItemId,bool ForHer){
 auto& S=*State;const auto* Item=S.Catalog.Find(ItemId);if(!Item)return;
 FOKStreetCartLine Line;Line.ItemId=ItemId;Line.Size=Item->DefaultSize();Line.ForHer=ForHer;
 if(!Item->Options.IsEmpty()){Line.Sweetness=TEXT("全糖");Line.Ice=TEXT("正常");}
 S.Cart.Add(Line);
 // Every dish is told to her as he orders it (not once at the end), so she reacts to each: pleased, does not like it, does not
 // want it, wants to eat it together. She answers when she is free; the dishes queue in the order they were picked.
 {const FString Dish=Item->Options.IsEmpty()?OKMsg(TEXT("ui.accounts_app.quoted_name"), {{TEXT("name"), *Item->Name}}):OKMsg(TEXT("ui.economy.drink_order_name"), {{TEXT("sweetness"), OKT(*Line.Sweetness)}, {TEXT("ice"), OKT(*Line.Ice)}, {TEXT("item_name"), *Item->Name}});
  S.TellHer.Add(OKMsg(TEXT("ui.economy.placed_order_reaction_prompt"), {{TEXT("for_label"), ForHer?OKT(TEXT("ui.economy.ordered_for_you_note")):OKT(TEXT("ui.economy.he_eats_own_order_note"))}, {TEXT("dish_name"), *Dish}}));
  UE_LOG(LogTemp,Display,TEXT("Street dish ordered: %s for_her=%d"),*Dish,ForHer?1:0);}
 RefreshUI();
}

int32 AOKStreetSocialScene::CartTotal() const{
 const auto& S=*State;int32 Total=0;
 for(const auto& L:S.Cart)if(const auto* I=S.Catalog.Find(L.ItemId))Total+=I->PriceOf(L.Size);
 if(const auto* P=S.Catalog.Places.Find(S.ShopPlace))Total+=Total*P->Service/100;
 return Total;
}

// Checkout (plan §8.3): alone or only his items, he just pays; with her items it becomes a choice she takes part in.
// Tells the heroine what he ordered. Two seconds after the last change to the cart (he is often still picking), the
// order goes to her as his turn (not shown as his words) so she reacts to it: pleased, surprised, wanting another,
// ordering herself. Waits while she is still answering; the same order is told once.
void AOKStreetSocialScene::TickOrderNotice(){
 auto& S=*State;
 // Nothing of the game's is said for her while a replay he asked for plays (it would stop the replay).
 if(!S.FreeTalkReplay.IsEmpty()||!S.FreeTalkReplayParts.IsEmpty())return;
 // What the walk is like (rain, umbrella, the empty street at night): told while they are still on the way and she is free
 // (TickUmbrella drops it when the walk ends: "only the two of us on the street" once seated in the restaurant is wrong).
 if(!S.WalkFact.IsEmpty()&&FreeTalkPartner()!=INDEX_NONE&&S.FreeTalkOpen&&!S.FreeTalkBusy&&!FreeTalkHerVoiceBusy()&&!S.FarewellPending){
  S.SystemNote=true;S.FreeTalkDraft=S.WalkFact;S.WalkFact.Reset();UE_LOG(LogTemp,Display,TEXT("Street walk note sent: %s"),*S.FreeTalkDraft.Left(120));SendFreeTalk();return;}
 // Things that happened at the till (checkout opened, paid, advanced) wait here until she is free to answer.
 if(!S.TellHer.IsEmpty()){
  // Nobody to tell, or they are saying goodbye (her "good night" said, the talk ends after it): a note now would start
  // the talk again ("all eaten — so what shall we do next?" after the goodbye).
  if(FreeTalkPartner()==INDEX_NONE||S.FarewellPending){if(S.FarewellPending)UE_LOG(LogTemp,Display,TEXT("Street till notice dropped (%d): they are saying goodbye"),S.TellHer.Num());S.TellHer.Reset();}
  else if(S.FreeTalkOpen&&!S.FreeTalkBusy&&!FreeTalkHerVoiceBusy()){S.SystemNote=true;S.FreeTalkDraft=S.TellHer[0];S.TellHer.RemoveAt(0);UE_LOG(LogTemp,Display,TEXT("Street till notice sent: %s"),*S.FreeTalkDraft.Left(120));SendFreeTalk();return;}
 }
 if(S.OrderNoticeDue<=0)return;
 if(S.Cart.IsEmpty()){S.OrderNoticeDue=0;S.OrderNoticeSig.Reset();return;}
 const int32 Her=FreeTalkPartner();
 if(Her==INDEX_NONE){S.OrderNoticeDue=0;return;} // nobody to tell
 if(FPlatformTime::Seconds()<S.OrderNoticeDue||S.FreeTalkBusy)return;
 TArray<FString> Mine,Hers;
 for(const auto& L:S.Cart){
  const auto* Item=S.Catalog.Find(L.ItemId);if(!Item)continue;
  const FString Name=Item->Options.IsEmpty()?OKMsg(TEXT("ui.accounts_app.quoted_name"), {{TEXT("name"), *Item->Name}}):OKMsg(TEXT("ui.economy.drink_order_name"), {{TEXT("sweetness"), OKT(*L.Sweetness)}, {TEXT("ice"), OKT(*L.Ice)}, {TEXT("item_name"), *Item->Name}});
  (L.ForHer?Hers:Mine).Add(Name);
 }
 FString Order;
 if(!Mine.IsEmpty())Order+=OKT(TEXT("ui.economy.he_ordered_himself"))+FString::Join(Mine,OKT(TEXT("ui.bank.list_separator")));
 if(!Hers.IsEmpty())Order+=FString(Order.IsEmpty()?TEXT(""):OKT(TEXT("ui.economy.semicolon_mark")))+OKT(TEXT("ui.economy.ordered_for_you_done"))+FString::Join(Hers,OKT(TEXT("ui.bank.list_separator")));
 S.OrderNoticeDue=0;
 if(Order.IsEmpty()||Order==S.OrderNoticeSig)return;
 S.OrderNoticeSig=Order;
 S.SystemNote=true;S.FreeTalkDraft=OKMsg(TEXT("ui.economy.placed_order_total_prompt"), {{TEXT("order_summary"), *Order}, {TEXT("cart_total"), CartTotal()}});
 UE_LOG(LogTemp,Display,TEXT("Street order notice: actor=%d %s"),Her,*Order);
 SendFreeTalk();
}

// The kitchen is done: the dishes reach the table. Her share fills her needs now, the player sees it, and she is told
// (as his turn, not shown as his words) so she reacts to the food. Told once she is free to answer.
void AOKStreetSocialScene::TickServes(){
 auto& S=*State;
 // Her reply's order waiting for the judge's payer (R4-3): no verdict within 10 s (a reply with no judge after it), billed as it is.
 for(auto It=S.ChatOrderAwaitsJudge.CreateIterator();It;++It)if(FPlatformTime::Seconds()-It.Value()>10.0){const int32 Girl=It.Key();It.RemoveCurrent();
  UE_LOG(LogTemp,Display,TEXT("Street chat order: actor=%d no judge in 10 s, billed now"),Girl);if(!S.Cart.IsEmpty())PlaceChatOrder(Girl,FString());}
 // A gift he just bought for her goes to her once she can take it (the talk is not busy, the gift choice not open).
 if(!S.PendingGift.IsEmpty()&&!S.FreeTalkBusy&&FreeTalkPartner()!=INDEX_NONE&&!S.Choices.ContainsByPredicate([](const FOKStreetPendingChoice& C){return C.Kind==TEXT("gift");})){
  const FString Gift=S.PendingGift;S.PendingGift.Reset();if(S.Store.Inventory.Contains(Gift))StartGift(Gift);}if(S.Serves.IsEmpty())return;const double Now=SceneTime();
 for(int32 K=S.Serves.Num()-1;K>=0;--K){
  auto& P=S.Serves[K];if(Now<P.At)continue;
  // He walked off before it came (the latte ordered at the cafe was "served" at the cafeteria): it waits at the counter
  // where it was ordered and is served when he is back there; after three minutes away it is left there, paid for.
  if(!P.Applied&&!P.PlaceId.IsEmpty()&&ShopPlaceHere()!=P.PlaceId){
   if(Now-P.At<180.0)continue;
   TArray<FString> Left;for(const auto& L:P.Lines)if(const auto* I=S.Catalog.Find(L.ItemId))Left.Add(I->Name);
   const int32 Batch=P.Batch;S.Table.RemoveAll([Batch](const FOKStreetDish& D){return D.Batch==Batch&&!D.Served;});
   const FOKStreetPlace* Where=OKStreetFindPlace(P.PlaceId);
   S.Message=OKMsg(TEXT("ui.economy.order_left_behind"),{{TEXT("place"),Where?FString(Where->Name):P.Place},{TEXT("items"),FString::Join(Left,OKT(TEXT("ui.bank.list_separator")))}});RefreshUI();
   UE_LOG(LogTemp,Display,TEXT("Street order left behind at %s: %s"),*P.Place,*FString::Join(Left,TEXT("、")));S.Serves.RemoveAt(K);continue;}
  TArray<FString> Names;
  if(!P.Applied){
   P.Applied=true;
   for(const auto& L:P.Lines)if(const auto* I=S.Catalog.Find(L.ItemId))Names.Add(S.Manifest.Characters.IsValidIndex(L.Girl)?S.Manifest.Characters[L.Girl].Name+OKT(TEXT("ui.economy.possessive_de"))+I->Name:L.ForHer?FString(OKT(TEXT("ui.economy.her_label")))+I->Name:I->Name);
   // Served: the dishes can be eaten now; her needs fill as she eats them (TickMeal).
   for(auto& D:S.Table)if(D.Batch==P.Batch)D.Served=true;
   S.Message=OKMsg(TEXT("ui.economy.dishes_served_line"), {{TEXT("dish_names"), *FString::Join(Names,OKT(TEXT("ui.bank.list_separator")))}});
   S.FreeTalkNotice=S.Message;RefreshUI();
   UE_LOG(LogTemp,Display,TEXT("Street order served: %s"),*FString::Join(Names,TEXT("、")));
   // Served at a place where people sit down to eat, while the two of them just stand there (they walked into the
   // restaurant and ordered on the way in: Hu Tao ate her salad standing by the door): they sit down at its table, the
   // place's own seated meal on his floor (the department store has one on several floors).
   // Not while she is at work or in class: Jeanne Alter, on her shift at the cafe ("I'm working, no time to sit with
   // you"), was sat down with him the moment her latte came (R3-231).
   const FOKStreetPlanBlock* HerBlock=P.Her!=INDEX_NONE?BlockNow(P.Her,int32(OKStreetMindNow(S)%1440)):nullptr;
   const bool OnDuty=HerBlock&&(HerBlock->Kind==TEXT("work")||HerBlock->Kind==TEXT("class"));
   if(OnDuty&&P.Her!=INDEX_NONE)UE_LOG(LogTemp,Display,TEXT("Street order served: actor %d is on %s; they do not sit down"),P.Her,*HerBlock->Kind);
   if(const int32 G=P.Her;!OnDuty&&G!=INDEX_NONE&&FreeTalkPartner()==G&&S.Pair.Partner==INDEX_NONE&&S.Facility==INDEX_NONE&&S.SoloGirl==INDEX_NONE&&S.OutingGirl==INDEX_NONE&&!P.PlaceId.IsEmpty()&&ShopPlaceHere()==P.PlaceId)
    if(const FOKStreetPlace* Here=OKStreetFindPlace(P.PlaceId)){
     const FOKStreetActivity* Best=nullptr;float BestZ=0;
     for(const auto& A:OKStreetActivities()){
      if(A.Kind!=EOKStreetActivityKind::Pair||!(A.Code==5||A.Code==1)||!A.Place||FCString::Strcmp(A.Place,*P.PlaceId)!=0)continue;
      const FOKStreetModelAnchor* Spot=A.Anchor?OKStreetPlaceAnchor(*Here,A.Anchor):nullptr;const float Dz=Spot?FMath::Abs(Spot->Position.Z-S.Male.Z):0.f;
      if(!Best||Dz<BestZ){Best=&A;BestZ=Dz;}}
     FString Why;if(Best&&BestZ<300.f&&BeginOuting(G,Best->Id,Why)){UE_LOG(LogTemp,Display,TEXT("Street order served: they sit down to eat it (%s)"),Best->Id);
      // What she said while it was being made waits until they have eaten it: as soon as it came, the walk she had
      // agreed to on the way in pulled them off their cake and out of the cafe (R3-207).
      if(!S.RetryAction.IsEmpty()&&S.RetryGirl==G){if(S.ThenAction.IsEmpty()){S.ThenAction=S.RetryAction;S.ThenGirl=G;S.ThenUntil=OKStreetMindNow(S)+120;S.ThenReadyAt=0;S.ThenStayMinutes=0;}
       UE_LOG(LogTemp,Display,TEXT("Street order served: %s waits until they have eaten"),*S.RetryAction);S.RetryAction.Reset();}}
     else if(Best){UE_LOG(LogTemp,Display,TEXT("Street order served: no seat taken (%s: %s)"),Best->Id,*Why);}}
  }
  // She reacts once she is free; if she is no longer his partner there is nobody to tell.
  if(P.Her==INDEX_NONE||FreeTalkPartner()!=P.Her){S.Serves.RemoveAt(K);continue;}
  // A replay he asked for plays out first: "the tea is here" sent in the middle of it stopped it after two lines.
  if(S.FreeTalkBusy||!S.FreeTalkReplay.IsEmpty()||!S.FreeTalkReplayParts.IsEmpty())continue;
  TArray<FString> Dishes;for(const auto& L:P.Lines)if(const auto* I=S.Catalog.Find(L.ItemId))Dishes.Add(OKMsg(TEXT("ui.economy.item_owner_label"), {{TEXT("owner_label"), L.ForHer?OKT(TEXT("ui.economy.your_label")):OKT(TEXT("ui.economy.his_label"))}, {TEXT("item_name"), *I->Name}}));
  S.SystemNote=true;S.FreeTalkDraft=OKMsg(TEXT("ui.economy.dishes_served_prompt"), {{TEXT("dish_names"), *FString::Join(Dishes,OKT(TEXT("ui.bank.list_separator")))}});
  S.Serves.RemoveAt(K);SendFreeTalk();return;
 }
}

// An order made in conversation goes straight to the kitchen. Who pays (shop.rs chat_order_payer): he does when he
// offered to in what he just said; otherwise her own inclination (split, she pays, he pays), and a strong wish to pay
// herself opens the usual checkout choice, as for the buttons.
// Who pays as they settled it, for this girl and three game hours (the AI's reading: her reply's payer, the judge's).
void AOKStreetSocialScene::RecordPayAgreed(int32 Girl,const FString& How,const FString& GoingNow){
 auto& S=*State;if(How!=TEXT("him")&&How!=TEXT("her")&&How!=TEXT("split"))return;
 // Where a place is: "go:<place>", else the activity's place.
 auto DestOf=[](const FString& Going){FString Dest=Going.StartsWith(TEXT("go:"))?Going.Mid(3):FString();
  if(Dest.IsEmpty()&&!Going.IsEmpty())if(const FOKStreetActivity* A=OKStreetFindActivity(Going);A&&A->Place&&*A->Place)Dest=A->Place;return Dest;};
 // Settled while setting off with these very words ("let's eat at the restaurant, my treat", said in the bookshop or at
 // her dorm's vending machine): for where they are going (the outing itself starts after this).
 const FString Now=DestOf(GoingNow);const FString Here=ShopPlaceHere();
 // Said again in the same shop (the judge reads what they settled so far, every turn) and not on the way anywhere with
 // it: it stays this shop's. The dinner treat moved on to her dorm's vending machine when "after we eat I'll walk you
 // home" came with it.
 if(Now.IsEmpty()&&S.PayAgreedWith==Girl&&S.PayAgreedHow==How&&!Here.IsEmpty()&&S.PayAgreedShop==Here&&OKStreetMindNow(S)-S.PayAgreedAt<=180)return;
 S.PayAgreedWith=Girl;S.PayAgreedHow=How;S.PayAgreedAt=OKStreetMindNow(S);
 // Settled inside a shop: for that shop at once (she paid her own book: not her own soup at the restaurant after).
 // Settled outside ("I will treat you to a drink" on the way): for the shop where it is first used (PayAgreed).
 // On the way somewhere, or for what they do next ("after the fish, lunch at the restaurant, my treat": the judge's
 // then): for there.
 const FString Later=DestOf(S.OutingGirl==Girl&&!S.OutingActivity.IsEmpty()?S.OutingActivity:S.ThenGirl==Girl?S.ThenAction:FString());
 S.PayAgreedShop=!Now.IsEmpty()?Now:!Later.IsEmpty()?Later:Here;
 UE_LOG(LogTemp,Display,TEXT("Street pay agreed: actor=%d %s (for %s)"),Girl,*How,S.PayAgreedShop.IsEmpty()?TEXT("the next shop"):*S.PayAgreedShop);
 // "My treat" just after she paid her own bill here (she ordered on the way in): he pays her back what she paid, and
 // she is told. Only later bills followed the agreement; she thanked him for a treat that never moved any money.
 if(How==TEXT("him")&&Girl==S.HerPaidGirl&&S.HerPaidAmount>0&&!S.HerPaidShop.IsEmpty()&&S.HerPaidShop==ShopPlaceHere()&&OKStreetMindNow(S)-S.HerPaidAt<=15&&S.Store.PlayerCash>=S.HerPaidAmount){
  const int64 Back=S.HerPaidAmount;S.HerPaidAmount=0;GiveMoneyNow(Girl,Back);
  S.TellHer.Add(OKMsg(TEXT("ui.scene.game_fact_note"),{{TEXT("fact"),OKMsg(TEXT("ui.economy.treat_paid_back"),{{TEXT("amount"),Back}})}}));
  UE_LOG(LogTemp,Display,TEXT("Street pay: actor=%d had paid %lld here; his treat pays it back"),Girl,Back);
 }
}
FString AOKStreetSocialScene::PayAgreed(int32 Girl) const{
 const auto& S=*State;
 // For the place where it was first used: "my treat" at dinner does not pay for her own drink at the dorm later.
 if(!S.PayAgreedShop.IsEmpty()&&!S.ShopPlace.IsEmpty()&&S.PayAgreedShop!=S.ShopPlace)return FString();
 return S.PayAgreedWith==Girl&&Girl!=INDEX_NONE&&OKStreetMindNow(S)-S.PayAgreedAt<=180?S.PayAgreedHow:FString();
}

void AOKStreetSocialScene::PlaceChatOrder(int32 Her,const FString& HerSaid){
 auto& S=*State;if(S.Cart.IsEmpty()||!S.Manifest.Characters.IsValidIndex(Her))return;
 // She promised to treat him: it is hers to pay (what she cannot cover he fronts and she owes him).
 // She promised to treat him: hers to pay, unless he insists now (chat_order_payer, promise).
 FString Lean;float Insist=0;HeroineCheckoutLean(Her,CartTotal(),Lean,Insist);
 auto Request=OKRulesRequest(TEXT("chat_order_payer"));Request->SetStringField(TEXT("lean"),Lean);Request->SetStringField(TEXT("agreed"),PayAgreed(Her));Request->SetBoolField(TEXT("has_hers"),S.Cart.ContainsByPredicate([](const FOKStreetCartLine& L){return L.ForHer;}));Request->SetNumberField(TEXT("insist"),Insist);Request->SetBoolField(TEXT("promise"),S.HerTreatPromise.Contains(Her));
 // Everything a thing he buys to give her (for her, not her own pick, nothing to eat or drink): his to pay (R3-255b).
 Request->SetBoolField(TEXT("his_gift"),S.Cart.Num()>0&&!S.Cart.ContainsByPredicate([&S](const FOKStreetCartLine& L){const auto* I=S.Catalog.Find(L.ItemId);return !L.ForHer||L.HerOwn||!I||I->Consumable;}));
 const TSharedPtr<FJsonObject> Payer=OKRulesEval(S,Request);const FString Option=Payer.IsValid()?Payer->GetStringField(TEXT("option")):Lean;
 if(!PayAgreed(Her).IsEmpty()&&S.PayAgreedShop.IsEmpty())S.PayAgreedShop=S.ShopPlace; // used here: this shop's agreement from now on
 UE_LOG(LogTemp,Display,TEXT("Street chat order: actor=%d lines=%d lean=%s insist=%.2f agreed=%s promise=%d -> %s"),Her,S.Cart.Num(),*Lean,Insist,*PayAgreed(Her),S.HerTreatPromise.Contains(Her),*Option);
 if(Option==TEXT("choice"))StartCheckout(Her);else FinishCheckout(Option,Her);
}

// Whether Who (0: him, else a girl) already has one of these in front of them: the same kind (catalog category: a meal,
// a dessert, a drink...; a steak and a pudding are both fine), on the table and not finished, or already in the cart. A second one is not ordered for them (the model may
// list the same order again on every turn of a talk).
bool AOKStreetSocialScene::HasDishLike(int32 Who,const FOKStreetCatalogItem& Item) const{
 const auto& S=*State;
 // A dish for two (the couple set, the moon-festival platter) is one for both of them: not a second one for him
 // ("包厢双人套餐" twice, ¥540).
 if(Item.Tags.Contains(TEXT("for_two"))){
  if(S.Table.ContainsByPredicate([&](const FOKStreetDish& D){return D.ItemId==Item.Id&&D.Percent>0;})||S.Cart.ContainsByPredicate([&](const FOKStreetCartLine& L){return L.ItemId==Item.Id;}))return true;
  for(const auto& Carried:S.CarriedOrders)for(const auto& L:Carried.Value)if(L.ItemId==Item.Id)return true;}
 auto Same=[&](const FString& Id){const auto* Had=S.Catalog.Find(Id);return Had&&(Had->Category.IsEmpty()||Item.Category.IsEmpty()?Had->Options.IsEmpty()==Item.Options.IsEmpty():Had->Category==Item.Category);};
 for(const auto& D:S.Table){const int32 Eater=D.ForHer?D.Her:0;if(Eater==Who&&D.Percent>0&&Same(D.ItemId))return true;}
 for(const auto& L:S.Cart){const int32 Eater=L.Girl!=INDEX_NONE?L.Girl:L.ForHer?FreeTalkPartner():0;if(Eater==Who&&Same(L.ItemId))return true;}
 // Ordered on the way and carried to the shop (placed when they arrive): already ordered, not a second cocoa at the door.
 for(const auto& Carried:S.CarriedOrders)for(const auto& L:Carried.Value){const int32 Eater=L.Girl!=INDEX_NONE?L.Girl:L.ForHer?Carried.Key:0;if(Eater==Who&&Same(L.ItemId))return true;}
 return false;
}

// A group's order: each girl's dishes are settled as hers, the same rule as one on one (shop.rs chat_order_payer): he
// pays when he offered to, everyone pays her own when he asked to split, otherwise each by her own inclination (a strong
// wish to pay herself just pays: no choice dialog at a group table). His own dishes go on the bill of the girl he talks to.
void AOKStreetSocialScene::SettleGroupOrder(int32 Partner){
 auto& S=*State;if(S.Cart.IsEmpty())return;const TArray<FOKStreetCartLine> All=S.Cart;
 TArray<int32> Girls{Partner};for(const auto& L:All)if(L.Girl!=INDEX_NONE)Girls.AddUnique(L.Girl);
 TArray<FString> Notices;
 for(const int32 G:Girls){
  S.Cart.Reset();for(const auto& L:All)if(G==Partner?L.Girl==INDEX_NONE:L.Girl==G)S.Cart.Add(L);
  if(S.Cart.IsEmpty()||!S.Manifest.Characters.IsValidIndex(G))continue;
  FString Option=TEXT("she_pays");
  if(!S.HerTreatPromise.Contains(G)){
   FString Lean;float Insist=0;HeroineCheckoutLean(G,CartTotal(),Lean,Insist);
   auto Request=OKRulesRequest(TEXT("chat_order_payer"));Request->SetStringField(TEXT("lean"),Lean);Request->SetStringField(TEXT("agreed"),PayAgreed(Partner));Request->SetNumberField(TEXT("insist"),Insist);
   const TSharedPtr<FJsonObject> Payer=OKRulesEval(S,Request);Option=Payer.IsValid()?Payer->GetStringField(TEXT("option")):Lean;
   if(!PayAgreed(Partner).IsEmpty()&&S.PayAgreedShop.IsEmpty())S.PayAgreedShop=S.ShopPlace;
   if(Option==TEXT("choice"))Option=TEXT("she_pays");
  }
  UE_LOG(LogTemp,Display,TEXT("Street group order: actor=%d lines=%d -> %s"),G,S.Cart.Num(),*Option);
  FinishCheckout(Option,G,G==Partner?FString():OKStreetStableId(S.Manifest.Characters[G]));
  Notices.Add(S.Manifest.Characters[G].Name+OKT(TEXT("ui.economy.colon_mark"))+S.FreeTalkNotice);
 }
 S.Cart.Reset();S.FreeTalkNotice=FString::Join(Notices,TEXT("\n"));RefreshUI();
}

// Walking into a shop or restaurant with her: she is told the menu is in front of them so she orders what she wants
// herself (and may ask what he wants). Once per visit, while free talk is open and she is free to answer.
void AOKStreetSocialScene::TickShopArrival(){
 auto& S=*State;const FString Place=ShopPlaceHere();
 // Who came in with him: walking in together (talking, beside him, or on their way here) is a visit together; meeting her
 // when she is already inside and he starts talking to her is not "we just walked in" (no menu prompt for a stranger).
 if(Place!=S.ShopSeenPlace){S.ShopSeenPlace=Place;S.ShopEnteredWith=!Place.IsEmpty()&&(FreeTalkPartner()!=INDEX_NONE||S.Companion!=INDEX_NONE||S.OutingGirl!=INDEX_NONE);}
 if(Place.IsEmpty()){if(SceneTime()-S.ShopArrivalAt>60.0)S.ShopArrivalPlace.Reset();return;} // out through the door and back in: not a new visit
 if(Place==S.ShopArrivalPlace||!S.FreeTalkOpen||S.FreeTalkBusy)return;
 const int32 Her=FreeTalkPartner();if(Her==INDEX_NONE)return;
 if(!S.ShopEnteredWith){S.ShopArrivalPlace=Place;UE_LOG(LogTemp,Display,TEXT("Street shop arrival: actor=%d was already in %s; no arrival prompt"),Her,*Place);return;}
 // On their way through a shop to an activity in the same building (the food court on 9F of the department store):
 // that activity is the visit. The "we just walked in, nothing to eat here" at the 1F door came back on 9F, at the
 // table: "this is the first floor" (R3-245). A restaurant's own menu still comes (the meal is ordered from it).
 const bool SellsFood=ItemsHere(Place).ContainsByPredicate([](const FOKStreetCatalogItem* I){return I&&I->Consumable;});
 if(!SellsFood&&S.OutingGirl!=INDEX_NONE&&!S.OutingActivity.StartsWith(TEXT("go:")))if(const auto* Act=OKStreetFindActivity(S.OutingActivity);Act&&Act->Place==Place){
  S.ShopArrivalPlace=Place;UE_LOG(LogTemp,Display,TEXT("Street shop arrival: on the way to %s inside %s; no arrival prompt"),*S.OutingActivity,*Place);return;}
 FString Why;if(PlaceClosedNow(Place,Why)||ItemsHere(Place).IsEmpty()){S.ShopArrivalPlace=Place;return;}
 // Her home (walking her back to the dorm at night): she is home, not out shopping ("I'm full, I don't need to buy anything").
 if(Place==OKStreetProfileString(S.MindProfiles.FindRef(OKStreetStableId(S.Manifest.Characters[Her])),nullptr,TEXT("home_place_id"))){
  S.ShopArrivalPlace=Place;UE_LOG(LogTemp,Display,TEXT("Street shop arrival: actor=%d is home at %s; no menu prompt"),Her,*Place);return;}
 // Closing within twenty minutes: no "look at the menu and order" (they ordered at 21:59 and it closed on their dishes);
 // the closing notice (TickPlaceClosing) tells her when it comes.
 if(PlaceClosedAt(Place,StreetNow()+FTimespan::FromMinutes(20),Why)){S.ShopArrivalPlace=Place;UE_LOG(LogTemp,Display,TEXT("Street shop arrival: %s closes within 20 minutes; no menu prompt"),*Place);return;}
 S.ShopArrivalPlace=Place;S.ShopArrivalAt=SceneTime();
 const auto* Where=OKStreetFindPlace(Place);
 // A bookshop or a pet shop: what is on sale, not "order what you feel like eating" (she said "nothing to eat here, don't order").
 const bool Food=ItemsHere(Place).ContainsByPredicate([](const FOKStreetCatalogItem* I){return I&&I->Consumable;});
 if(S.Party.ContainsByPredicate([Her](int32 G){return G!=Her;})){
  S.SystemNote=true;S.FreeTalkDraft=OKMsg(Food?TEXT("ui.economy.group_entered_menu_prompt"):TEXT("ui.economy.group_entered_shop_prompt"), {{TEXT("place_name"), Where?Where->Name:*Place}});
  UE_LOG(LogTemp,Display,TEXT("Street shop arrival: party of %d at %s"),S.Party.Num()+1,*Place);SendFreeTalk();return;
 }
 S.SystemNote=true;S.FreeTalkDraft=OKMsg(Food?TEXT("ui.economy.entered_place_prompt"):TEXT("ui.economy.entered_shop_prompt"), {{TEXT("place_name"), Where?Where->Name:*Place}});
 UE_LOG(LogTemp,Display,TEXT("Street shop arrival: actor=%d place=%s"),Her,*Place);
 SendFreeTalk();
}

// A shop that closes while they are in it: the meal or drink ends, and she is told so she can suggest somewhere else.
void AOKStreetSocialScene::TickPlaceClosing(){
 auto& S=*State;const double Wall=FPlatformTime::Seconds();if(Wall<S.PlaceClosingAt)return;S.PlaceClosingAt=Wall+1.0;
 const FString Place=PlaceHere(S.Male);
 if(Place.IsEmpty()){S.ClosedNoticePlace.Reset();return;}
 FString Why;if(!PlaceClosedNow(Place,Why)){S.ClosedNoticePlace.Reset();return;}
 // Staff early for a shift here (within the hour): he waits inside for the doors to open, not shown out as a customer.
 {const int64 NowMinute=OKStreetMindNow(S);if(S.Store.Appointments.ContainsByPredicate([&](const FOKStreetAppointment& A){return A.Kind==TEXT("shift")&&A.Place==Place&&A.Start>=NowMinute&&A.Start-NowMinute<=60;}))return;}
 // Not open yet but opening within a quarter of an hour (they came for breakfast at 6:56): they wait, nothing closes.
 if(!PlaceClosedAt(Place,StreetNow()+FTimespan::FromMinutes(15),Why))return;
 const auto* Where=OKStreetFindPlace(Place);const FString Name=Where?FString(Where->Name):Place;
 if(S.Pair.Partner!=INDEX_NONE||S.Facility!=INDEX_NONE){
  UE_LOG(LogTemp,Display,TEXT("Street place closed: %s closed during an activity (pair=%d facility=%d); it ends"),*Place,S.Pair.Partner,S.Facility);
  S.ActCutShort=true; // cut short by the closing: billed and timed for what really went by (ActivityAftermath)
  if(S.Pair.Partner!=INDEX_NONE)FinishPhoto();
  if(S.Facility!=INDEX_NONE)FinishFacility();
  S.Message=Name+OKT(TEXT("ui.economy.closed_event_over"));RefreshUI();
 }
 if(S.ClosedNoticePlace==Place)return;
 const int32 Her=FreeTalkPartner();
 if(Her==INDEX_NONE){
  // On his own: the staff say it once, the menu closes and what is left on the table is cleared.
  S.ClosedNoticePlace=Place;S.ShopOpen=false;if(!S.Table.IsEmpty()){S.Table.Reset();S.Serves.Reset();}
  S.Message=Name+OKT(TEXT("ui.economy.closing_time_leave"));UE_LOG(LogTemp,Display,TEXT("Street place closed: %s closed while he was alone there"),*Place);RefreshUI();return;
 }
 if(!S.FreeTalkOpen||S.FreeTalkBusy)return;
 S.ClosedNoticePlace=Place;
 S.SystemNote=true;S.FreeTalkDraft=OKMsg(TEXT("ui.economy.closed_place_prompt"), {{TEXT("place_name"), *Name}});
 UE_LOG(LogTemp,Display,TEXT("Street place closed: actor=%d told that %s closed"),Her,*Place);
 SendFreeTalk();
}

// Eating at the table: once a dish is served she (and he, his own) eats it down over OKStreetEatSeconds, one dish at a time,
// and what she eats fills her needs in proportion. When she is full she stops and says so; stop_eating stops her earlier.
void AOKStreetSocialScene::TickMeal(float Dt){
 auto& S=*State;if(S.Table.IsEmpty())return;const double Wall=FPlatformTime::Seconds();
 // The meal is with the girl the dishes were ordered with (each dish keeps her): talking to another one at the table
 // (the drink one girl bought him, then he turns to her friend) does not make it the new one's meal.
 const int32 Talking=FreeTalkPartner();
 // A dish for "her" with nobody named (bought before she was his partner again) is the partner's, as DishOwner reads it.
 const int32 Her=Talking!=INDEX_NONE&&S.Table.ContainsByPredicate([Talking,&S](const FOKStreetDish& D){return D.Her==Talking||(D.ForHer&&!S.Manifest.Characters.IsValidIndex(D.Her));})?Talking:INDEX_NONE;
 // Still at the meal: in the place, or in one of its rooms (a private room is a facility away from the floor).
 const bool There=PlaceHere(S.Male)==S.MealPlace||S.Facility!=INDEX_NONE;
 // Leaving the place (or her) ends the meal.
 if(Her==INDEX_NONE&&There){
  // Alone at the table: he eats his own dishes; hers (if any) wait. The table stays while he is in the place.
  S.MealAwayAt=0;
  const float SoloStep=float(100.0/OKStreetEatSeconds)*FMath::Clamp(Dt,0.f,.1f);
  // One dish and one drink at a time, side by side (tea with the tart, not the tea first and then the tart).
  bool Ate=false,Sipped=false;
  for(auto& D:S.Table)if(!D.ForHer&&D.Served&&D.Percent>0.f){const auto* I=S.Catalog.Find(D.ItemId);bool& Busy=I&&I->IsDrink()?Sipped:Ate;if(Busy)continue;Busy=true;D.Percent=FMath::Max(0.f,D.Percent-SoloStep);}
  bool Any=false;for(const auto& D:S.Table)if(D.Percent>0.f||!D.Served)Any=true;
  if(!Any){S.Message=OKT(TEXT("ui.economy.dishes_all_eaten"));S.Table.Reset();UE_LOG(LogTemp,Display,TEXT("Street meal: everything eaten (alone)"));RefreshUI();}
  return;
 }
 if(Her==INDEX_NONE||!There){
  if(S.MealAwayAt==0)S.MealAwayAt=Wall;
  else if(Wall-S.MealAwayAt>5){
   // Untouched ones go with him, as a takeaway would (R3-259): a ¥14 bento served just as he walked her home was thrown away.
   auto Candidate=S.Store;TArray<FString> Packed;
   for(const auto& D:S.Table)if(D.Served&&D.Percent>=99.f)if(const auto* I=S.Catalog.Find(D.ItemId);I&&I->Consumable){Candidate.Inventory.Add(D.ItemId);Packed.Add(I->Name);}
   if(!Packed.IsEmpty()){CommitStore(MoveTemp(Candidate));S.Message=OKMsg(TEXT("ui.economy.untouched_packed"),{{TEXT("items"),FString::Join(Packed,TEXT("、"))}});RefreshUI();}
   UE_LOG(LogTemp,Display,TEXT("Street meal: they left, the table is cleared (%d dishes, %d packed)"),S.Table.Num(),Packed.Num());S.Table.Reset();return;}
  return;
 }
 S.MealAwayAt=0;
 // Everyone eating: the partner and the girls with them (each her own dishes); a dish ordered for "her" with nobody named is the partner's.
 auto DishOwner=[&](const FOKStreetDish& D){return S.Manifest.Characters.IsValidIndex(D.Her)?D.Her:Her;};
 // The meal ends: each girl who ate remembers it (with its time): where, with whom, what she had and what he had.
 auto RememberMeal=[&](const TArray<int32>& Who){
  TArray<FString> His;for(const auto& D:S.Table)if(!D.ForHer)His.AddUnique(D.Name);
  for(const int32 G:Who){TArray<FString> Mine,Others;for(const auto& D:S.Table)if(D.ForHer&&DishOwner(D)==G)Mine.AddUnique(D.Name);
   for(const int32 O:Who)if(O!=G)Others.Add(S.Manifest.Characters[O].Name);
   const FString Sep=OKT(TEXT("ui.memory.list_separator"));
   MindObserve(G,TEXT("meal"),OKMsg(TEXT("ui.memory.meal_together"),{{TEXT("place"),[&]{const FOKStreetPlace* P=OKStreetFindPlace(PlaceHere(S.Male));return P?FString(P->Name):FString(OKT(TEXT("ui.etiquette.street_location")));}()},{TEXT("others"),FString::Join(Others,*Sep)},{TEXT("with_others"),Others.Num()?TEXT("yes"):TEXT("no")},
    {TEXT("mine"),FString::Join(Mine,*Sep)},{TEXT("his"),FString::Join(His,*Sep)},{TEXT("he_ate"),His.Num()?TEXT("yes"):TEXT("no")}}),PlaceHere(S.Male),0.45f);}
 };
 TArray<int32> Eaters{Her};for(const int32 G:S.Party)if(G!=Her&&S.Agents.IsValidIndex(G-1)&&FVector::Dist2D(S.Agents[G-1].Position,S.Male)<600.f)Eaters.Add(G);
 TMap<int32,float> Full;
 for(const int32 G:Eaters)Full.Add(G,OKStreetViewRelation(S.Store,OKStreetStableId(S.Manifest.Characters[G]),S.Manifest.TalkBudget).Satiety);
 float Satiety=Full[Her];
 const float Step=float(100.0/OKStreetEatSeconds)*FMath::Clamp(Dt,0.f,.1f);
 // Full stops food, not drinks: a full girl still sips her drink.
 auto Drink=[&](const FOKStreetDish& D){const auto* I=S.Catalog.Find(D.ItemId);return I&&I->IsDrink();};
 // She goes on with a dish while she is not full, or when the rest of it still fits (OKStreetOverfullSatiety).
 auto CanEat=[&](int32 G,const FOKStreetDish& D){if(Drink(D)||!Full.Contains(G)||Full[G]<OKStreetFullSatiety)return true;const auto* I=S.Catalog.Find(D.ItemId);return I&&Full[G]+OKStreetItemFill(*I)*D.Percent/100.f*100.f/SatietyMax(G)<=OKStreetOverfullSatiety;};
 // Each eats one dish and sips one drink at a time, side by side (tea with the tart, not the tea first and then the tart).
 auto EatSome=[&](TFunctionRef<bool(const FOKStreetDish&)> Mine){bool Ate=false,Sipped=false;
  for(auto& D:S.Table)if(Mine(D)){bool& Busy=Drink(D)?Sipped:Ate;if(Busy)continue;Busy=true;D.Percent=FMath::Max(0.f,D.Percent-Step);}
  return Ate||Sipped;};
 // Who is eating now (the pair's clips follow it: eating, or sitting with nothing to eat).
 for(const int32 G:Eaters)if(G!=Her||!S.StopEating)if(EatSome([&](const FOKStreetDish& D){return D.ForHer&&DishOwner(D)==G&&D.Served&&D.Percent>0.f&&CanEat(G,D);}))S.MealAteAt.Add(G,Wall);
 if(EatSome([](const FOKStreetDish& D){return !D.ForHer&&D.Served&&D.Percent>0.f;}))S.MealAteAt.Add(0,Wall);
 if(Wall<S.MealBookAt)return;
 S.MealBookAt=Wall+2.0;
 // What each has eaten since the last booking fills her needs by that share of each dish's effects.
 {
  auto Candidate=S.Store;bool Changed=false;
  for(const int32 G:Eaters){
   const int32 Index=OKStreetRelationIndex(Candidate,OKStreetStableId(S.Manifest.Characters[G]),S.Manifest.TalkBudget,true);
   if(!Candidate.Relations.IsValidIndex(Index))continue;
   auto& R=Candidate.Relations[Index];
   for(auto& D:S.Table)if(D.ForHer&&DishOwner(D)==G&&D.Served){
    const float Eaten=100.f-D.Percent,Delta=Eaten-D.Booked;if(Delta<1.f)continue;
    if(const auto* I=S.Catalog.Find(D.ItemId))for(const auto& E:I->Effects){float* V=E.Key==TEXT("satiety")?&R.Satiety:E.Key==TEXT("energy")?&R.Energy:E.Key==TEXT("fun")?&R.Fun:E.Key==TEXT("hygiene")?&R.Hygiene:nullptr;
     if(V)*V=FMath::Clamp(*V+E.Value*Delta/100.f*(V==&R.Satiety?100.f/SatietyMax(G):1.f),0.f,100.f);} // her stomach's share (R3-269)
    D.Booked=Eaten;Changed=true;
   }
   if(G==Her)Satiety=R.Satiety;
  }
  if(Changed)CommitStore(MoveTemp(Candidate));
 }
 // Full: the rest of her dishes stay on the table; she is told so she says it.
 bool HerLeft=false,AnyLeft=false;for(const auto& D:S.Table){if(D.Percent>0.f)AnyLeft=true;if(D.ForHer&&DishOwner(D)==Her&&D.Served&&D.Percent>0.f&&(!CanEat(Her,D)||S.StopEating))HerLeft=true;}
 if(!S.FullTold&&HerLeft){
  S.FullTold=true;S.FreeTalkNotice=OKT(TEXT("ui.economy.she_full_now"));
  S.TellHer.Add(OKT(TEXT("ui.economy.too_full_prompt")));
  UE_LOG(LogTemp,Display,TEXT("Street meal: she is full (satiety %.0f) with dishes left"),Satiety);RefreshUI();
 }
 // She said she cannot eat any more (stop_eating) at 99: her dish is not waiting for her either. Counted as waiting,
 // the meal never ended and "walk me home after" never came (R3-247).
 {bool Waiting=false;for(const auto& D:S.Table)if(D.Percent>0.f&&(!D.ForHer||!D.Served||(CanEat(DishOwner(D),D)&&!S.StopEating)))Waiting=true;
  // Nobody at the table can eat any more (she alone, full with her soup half left): the eating is over for what they
  // do next (TickThenAction), the dishes stay.
  if(!Waiting&&AnyLeft)S.TableClearedAt=SceneTime();
  if(!Waiting&&AnyLeft&&Eaters.Num()>1){S.Message=OKT(TEXT("ui.economy.everyone_full"));RememberMeal(Eaters);S.Table.Reset();S.TableClearedAt=SceneTime();UE_LOG(LogTemp,Display,TEXT("Street meal: everyone is full, the rest is left"));RefreshUI();return;}}
 if(!AnyLeft){
  S.Message=OKT(TEXT("ui.economy.all_dishes_eaten"));RememberMeal(Eaters);S.Table.Reset();S.TableClearedAt=SceneTime();
  S.TellHer.Add(OKT(TEXT("ui.economy.all_eaten_prompt")));
  UE_LOG(LogTemp,Display,TEXT("Street meal: everything eaten"));RefreshUI();
 }
}

// One line per dish: whose it is, how much is left (100% while it is still being prepared), and when it comes.
FString AOKStreetSocialScene::DishTableText() const{
 const auto& S=*State;if(S.Table.IsEmpty())return FString();
 const double Now=SceneTime();TArray<FString> Lines;
 for(const auto& D:S.Table){
  const int32 Filled=FMath::Clamp(FMath::RoundToInt(D.Percent/10.f),0,10);
  FString Bar;for(int32 K=0;K<10;++K)Bar+=K<Filled?TEXT("■"):TEXT("□");
  FString Note;
  if(!D.Served){double Wait=0;for(const auto& P:S.Serves)if(P.Batch==D.Batch)Wait=P.At-Now;Note=OKMsg(TEXT("ui.economy.kitchen_wait_note"), {{TEXT("seconds"), FMath::Max(0,int32(FMath::CeilToDouble(Wait)))}});}
  else if(D.Percent<=0.f)Note=OKT(TEXT("ui.economy.finished_eating_suffix"));
  Lines.Add(OKMsg(TEXT("ui.economy.item_remaining_line"), {{TEXT("item_name"), *D.Name}, {TEXT("owner_label"), !D.ForHer?OKT(TEXT("ui.economy.his_label")):S.Manifest.Characters.IsValidIndex(D.Her)?*SpokenName(D.Her):OKT(TEXT("ui.economy.her_label"))}, {TEXT("bar"), *Bar}, {TEXT("percent"), FMath::RoundToInt(D.Percent)}, {TEXT("note"), *Note}}));
 }
 return FString::Join(Lines,TEXT("\n"));
}

// Eating before the food is served (an act word from her or the menu): not yet; she is told why.
bool AOKStreetSocialScene::EatActBlocked(int32 Girl,const FString& Act){
 auto& S=*State;
 if(Act!=TEXT("eat")&&Act!=TEXT("share_food")&&Act!=TEXT("feed"))return false;
 if(S.Table.IsEmpty())return false;
 for(const auto& D:S.Table)if(D.Served)return false;
 const double Now=SceneTime();double Wait=0;for(const auto& P:S.Serves)Wait=FMath::Max(Wait,P.At-Now);
 S.FreeTalkNotice=OKT(TEXT("ui.economy.not_served_yet"));
 S.TellHer.Add(OKMsg(TEXT("ui.economy.not_served_wait_prompt"), {{TEXT("seconds"), FMath::Max(0,int32(FMath::CeilToDouble(Wait)))}}));
 UE_LOG(LogTemp,Display,TEXT("Street meal: %s refused, the food is not served yet"),*Act);RefreshUI();
 return true;
}

void AOKStreetSocialScene::StartCheckout(int32 ForHer){
 auto& S=*State;if(S.Cart.IsEmpty())return;
 // Her order placed as they walk in is hers even before the talk has her as partner again: without her it read as
 // his own purchase and he paid for her braised pork rice and tea without being asked.
 const int32 Total=CartTotal();const int32 Her=ForHer!=INDEX_NONE?ForHer:FreeTalkPartner();
 const bool Shared=Her!=INDEX_NONE&&S.Cart.ContainsByPredicate([](const FOKStreetCartLine& L){return L.ForHer;});
 // She promised to treat him: it is hers to pay, whether he picked it with the buttons or by talking, and even when the
 // cart holds only his own dish (he asked her to treat him to a drink and chose one for himself).
 if(Her!=INDEX_NONE&&S.HerTreatPromise.Contains(Her)){UE_LOG(LogTemp,Display,TEXT("Street checkout: actor=%d promised to treat -> she pays"),Her);FinishCheckout(TEXT("she_pays"),Her);return;}
 if(!Shared){FinishCheckout(TEXT("player_pays"),INDEX_NONE);return;}
 if(S.Choices.ContainsByPredicate([](const FOKStreetPendingChoice& C){return C.Kind==TEXT("checkout");}))return;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Her]);
 const FOKStreetRelation R=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
 FString Lean;float Insist=0;HeroineCheckoutLean(Her,Total,Lean,Insist);
 TArray<FString> Names;for(const auto& L:S.Cart)if(const auto* I=S.Catalog.Find(L.ItemId))Names.Add(I->Name);
 const FString Label=OKMsg(TEXT("ui.economy.checkout_with_names_button"), {{TEXT("total"), Total}, {TEXT("names"), *FString::Join(Names,OKT(TEXT("ui.bank.list_separator")))}});
 TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 OpenChoice(Her,TEXT("checkout"),Label,{TEXT("player_pays"),TEXT("split"),TEXT("she_pays"),TEXT("let_her_decide")},Lean,Insist,20,[Weak,Her](const FString& Option){if(Weak.IsValid())Weak->FinishCheckout(Option,Her);});
 S.FreeTalkNotice=Label+OKT(TEXT("ui.economy.pay_hint_text"));
 {const FOKStreetRelation Mine=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);
  S.TellHer.Add(OKMsg(TEXT("ui.economy.checkout_decision_prompt"), {{TEXT("total"), Total}, {TEXT("names"), *FString::Join(Names,OKT(TEXT("ui.bank.list_separator")))}, {TEXT("wallet_cash"), FMath::Max<int64>(0,Mine.Cash)}, {TEXT("affordable"), Mine.Cash>=Total?TEXT("yes"):TEXT("no")}}));} // what she has, a fact: "not enough" said for 2 out of 235
 if(S.HerTreatPromise.Contains(Her)&&Insist>=1.f)S.FreeTalkNotice+=OKT(TEXT("ui.economy.she_treats_this_time"));
 else if(Lean==TEXT("she_pays")&&Insist>0.6f)S.FreeTalkNotice+=OKT(TEXT("ui.economy.she_wants_to_pay_suffix"));
 RefreshUI();
}

// Paying the cart: who pays what, her shortfall, her tastes and what she remembers are rules (shop.rs checkout); here the
// money goes through the books (a card when his wallet is short) and the dishes go to the kitchen.
void AOKStreetSocialScene::FinishCheckout(const FString& Option,int32 Her,const FString& AsGirl){
 auto& S=*State;if(S.Cart.IsEmpty())return;
 const bool WithHer=Her!=INDEX_NONE&&S.Manifest.Characters.IsValidIndex(Her);const FString Id=WithHer?OKStreetStableId(S.Manifest.Characters[Her]):FString();
 const FString PlaceName=OKStreetFindPlace(S.ShopPlace)?FString(OKStreetFindPlace(S.ShopPlace)->Name):S.ShopPlace;
 auto Request=WithHer?OKRulesHeroineRequest(S,TEXT("checkout"),Id):OKRulesRequest(TEXT("checkout"));
 Request->SetStringField(TEXT("option"),Option);Request->SetNumberField(TEXT("service"),S.Catalog.Places.Contains(S.ShopPlace)?S.Catalog.Places[S.ShopPlace].Service:0);
 Request->SetNumberField(TEXT("player_cash"),double(S.Store.PlayerCash));Request->SetStringField(TEXT("place"),S.ShopPlace);Request->SetStringField(TEXT("place_name"),PlaceName);
 if(!AsGirl.IsEmpty())Request->SetStringField(TEXT("as_girl"),AsGirl);
 Request->SetNumberField(TEXT("seed"),FMath::Rand()); // what a fortune slip says (shop.rs draw_fortunes)
 TArray<TSharedPtr<FJsonValue>> Cart;
 for(const auto& L:S.Cart){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("item"),L.ItemId);O->SetStringField(TEXT("size"),L.Size);O->SetStringField(TEXT("sweetness"),L.Sweetness);O->SetStringField(TEXT("ice"),L.Ice);
  O->SetBoolField(TEXT("for_her"),L.ForHer);O->SetBoolField(TEXT("her_own"),L.HerOwn);O->SetStringField(TEXT("girl"),L.Girl!=INDEX_NONE&&S.Manifest.Characters.IsValidIndex(L.Girl)?OKStreetStableId(S.Manifest.Characters[L.Girl]):FString());Cart.Add(MakeShared<FJsonValueObject>(O));}
 Request->SetArrayField(TEXT("cart"),Cart);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 const int32 Total=int32(Answer->GetNumberField(TEXT("total")));const int64 PlayerPays=int64(Answer->GetNumberField(TEXT("player_pays"))),HerPays=int64(Answer->GetNumberField(TEXT("her_pays"))),Advanced=int64(Answer->GetNumberField(TEXT("advanced")));
 auto Candidate=S.Store;const FString Why=OKMsg(TEXT("ui.economy.checkout_at_place"), {{TEXT("place_name"), *PlaceName}});
 if(Answer->GetBoolField(TEXT("card"))){if(!PayByCard(Candidate,PlayerPays,S.ShopPlace,Why,TEXT("food"))){S.FreeTalkNotice=OKMsg(TEXT("ui.economy.not_enough_breakdown"), {{TEXT("amount_needed"), PlayerPays}, {TEXT("your_cash"), Candidate.PlayerCash}});
  // Not silently: the order stays in the basket, and she hears it could not be paid (so they change it, not wait for food).
  UE_LOG(LogTemp,Display,TEXT("Street checkout: not paid, he would have to pay %lld and has %lld (total %d, option %s)"),PlayerPays,Candidate.PlayerCash,Total,*Option);S.OrderDroppedMinute=OKStreetMindNow(S);
  if(WithHer)S.TellHer.Add(OKMsg(TEXT("ui.economy.checkout_failed_note"), {{TEXT("total"), Total}}));RefreshUI();return;}}
 else OKCash(Candidate,-PlayerPays,TEXT("food"),Why);
 // Something to eat or drink paid for here: what a meal's or a drink's end is not billed again for (Life.inl
 // bought_at_counter). Not the fish food: ¥2 of it in the park made the drinks at the park's machine free (R4-17).
 const auto Edible=[&S](const FOKStreetCartLine& L){const auto* I=S.Catalog.Find(L.ItemId);return I&&I->Consumable&&(I->IsDrink()||I->Effects.Contains(TEXT("satiety")));};
 if(S.Cart.ContainsByPredicate(Edible))S.LastCheckoutMinute=OKStreetMindNow(S);
 ApplyRulesEffects(Candidate,*Answer);
 // What is not eaten (a plush, a keyring, a notebook) is his to carry: into the backpack. Bought for her and giftable:
 // he gives it to her next, the gift as the gift rules take it (StartGift), not a dish served.
 TArray<FString> GiveHer;
 // Whose each thing is (items.rs items_checkout): what she picked for herself is hers to keep (the postcard she chose at
 // the bookstore came back as "his gift"), the rest his to carry, one bought for her given next.
 {auto Things=OKRulesRequest(TEXT("items_checkout"));OKRulesPutExt(S,Things);if(WithHer)Things->SetStringField(TEXT("girl"),Id);
  TArray<TSharedPtr<FJsonValue>> Lines;for(const auto& L:S.Cart){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("item"),L.ItemId);O->SetBoolField(TEXT("for_her"),L.ForHer);O->SetBoolField(TEXT("her_own"),L.HerOwn);Lines.Add(MakeShared<FJsonValueObject>(O));}
  Things->SetArrayField(TEXT("lines"),Lines);
  if(const TSharedPtr<FJsonObject> Whose=OKRulesEval(S,Things)){OKRulesTakeExt(Candidate,*Whose);const FString Give=Whose->GetStringField(TEXT("give_her"));if(!Give.IsEmpty())GiveHer.Add(Give);}}
 if(!CommitStore(MoveTemp(Candidate)))return;
 FlushRulesEffects();
 if(GiveHer.Num()){S.PendingGift=GiveHer[0];UE_LOG(LogTemp,Display,TEXT("Street checkout: %s bought for her, given next"),*GiveHer[0]);}
 if(WithHer&&Answer->GetBoolField(TEXT("treat_kept"))){S.HerTreatPromise.Remove(Her);S.HerTreatKept.Add(Her,OKStreetMindNow(S));}
 const double ServeIn=S.Catalog.Places.Contains(S.ShopPlace)?S.Catalog.Places[S.ShopPlace].ServeSeconds:OKStreetServeSeconds;
 {const bool Food=S.Cart.ContainsByPredicate([&S](const FOKStreetCartLine& L){const auto* I=S.Catalog.Find(L.ItemId);return I&&I->Consumable;});
  S.FreeTalkNotice=Answer->GetStringField(TEXT("notice"))+(Food&&ServeIn>=10?OKMsg(TEXT("ui.economy.dish_serve_eta_suffix"), {{TEXT("seconds"), int32(ServeIn)}}):FString());}
 {
  // Only food and drink go to the kitchen and onto the table (a notebook or a gift is simply bought).
  FOKStreetPendingServe Serve;Serve.Batch=++S.ServeBatch;Serve.At=SceneTime()+ServeIn;Serve.Her=Her;Serve.Place=PlaceName;Serve.PlaceId=S.ShopPlace;
  for(const auto& L:S.Cart)if(const auto* I=S.Catalog.Find(L.ItemId))if(I->Consumable){
   Serve.Lines.Add(L);
   // The dishes are on the table at 100% but cannot be eaten until they are served.
   FOKStreetDish D;D.ItemId=L.ItemId;D.Name=I->Name;D.ForHer=L.ForHer||L.Girl!=INDEX_NONE;D.Batch=Serve.Batch;D.Her=L.Girl!=INDEX_NONE?L.Girl:Her;S.Table.Add(D);
  }
  if(!Serve.Lines.IsEmpty()){S.Serves.Add(Serve);S.MealPlace=S.ShopPlace;if(Serve.Lines.ContainsByPredicate(Edible))S.OrderedAtPlace.Add(S.ShopPlace,OKStreetMindNow(S));S.StopEating=false;S.FullTold=false;S.MealAwayAt=0;}
 }
 UE_LOG(LogTemp,Display,TEXT("Street order placed: %d lines, served in %.0f s"),S.Cart.Num(),ServeIn);
 UE_LOG(LogTemp,Display,TEXT("Street checkout: total=%d player=%lld her=%lld advanced=%lld option=%s"),Total,PlayerPays,HerPays,Advanced,*Option);
 // What she just paid all by herself, here (RecordPayAgreed: "my treat" right after hands it back to her).
 // Not a bill she paid as she had promised or agreed to (her treat this time): "my treat" after it is for what comes
 // next, and paying her back went against her own promise ("keep the money, I said it was mine").
 bool TreatKept=false;Answer->TryGetBoolField(TEXT("treat_kept"),TreatKept);
 if(HerPays>0&&PlayerPays==0&&!TreatKept&&PayAgreed(Her)!=TEXT("her")){S.HerPaidGirl=Her;S.HerPaidAmount=HerPays;S.HerPaidShop=S.ShopPlace;S.HerPaidAt=OKStreetMindNow(S);}
 {FString Tell=Answer->GetStringField(TEXT("tell_her"));if(!Tell.IsEmpty()&&ServeIn>=10){const FString Natural=OKT(TEXT("ui.economy.please_naturally"));Tell=Tell.Contains(Natural)?Tell.Replace(*Natural,*OKMsg(TEXT("ui.economy.dish_eta_natural_hint"), {{TEXT("seconds"), int32(ServeIn)}})):Tell+OKMsg(TEXT("ui.economy.dish_eta_note"), {{TEXT("seconds"), int32(ServeIn)}});} // the payment notes too: paid is not served ("the hamburg steak is here!" while it was being made)
 if(!Tell.IsEmpty())S.TellHer.Add(Tell);} // handed over at once: no "it comes in 3 seconds" for her to say
 S.Cart.Reset();S.OrderNoticeSig.Reset();S.OrderNoticeDue=0;RefreshUI();
}

// Buying something to give later goes into the backpack (plan §8.4).
void AOKStreetSocialScene::BuyForGift(const FString& ItemId){
 auto& S=*State;const auto* Item=S.Catalog.Find(ItemId);if(!Item)return;
 const int32 Price=Item->PriceOf(Item->DefaultSize());
 auto Candidate=S.Store;
 if(Candidate.PlayerCash>=Price)OKCash(Candidate,-Price,TEXT("shopping"),OKT(TEXT("ui.bank.buy_label"))+Item->Name+OKT(TEXT("ui.economy.gift_label")));
 else if(!PayByCard(Candidate,Price,S.ShopPlace,OKT(TEXT("ui.bank.buy_label"))+Item->Name)){S.FreeTalkNotice=OKT(TEXT("ui.economy.not_enough_money"));RefreshUI();return;}
 Candidate.Inventory.Add(ItemId);
 if(CommitStore(MoveTemp(Candidate))){S.FreeTalkNotice=OKMsg(TEXT("ui.economy.bought_item_line"), {{TEXT("item_name"), *Item->Name}, {TEXT("price"), Price}});RefreshUI();}
}

void AOKStreetSocialScene::StartGift(const FString& ItemId){
 auto& S=*State;const int32 Her=FreeTalkPartner();const auto* Item=S.Catalog.Find(ItemId);
 if(Her==INDEX_NONE||!Item||!S.Store.Inventory.Contains(ItemId))return;
 // How she leans is a rule (shop.rs gift_offer; her birthday counts for more).
 auto Request=OKRulesHeroineRequest(S,TEXT("gift_offer"),OKStreetStableId(S.Manifest.Characters[Her]));Request->SetStringField(TEXT("item"),ItemId);Request->SetBoolField(TEXT("birthday"),BirthdayOf(Her,S.Store.CalendarDay));
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 const float Score=float(Answer->GetNumberField(TEXT("score")));
 TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 // The same gift already held out to her (he said "this is for you" and her answer opened it, then he pressed the button
 // too): one offer, not a second one that gave her the same hand cream twice.
 {const FString Label=OKMsg(TEXT("ui.economy.gift_her_button"),{{TEXT("item_name"),*Item->Name}});
  if(S.Choices.ContainsByPredicate([&](const FOKStreetPendingChoice& C){return C.Kind==TEXT("gift")&&C.Heroine==Her&&C.Label==Label;})){UE_LOG(LogTemp,Display,TEXT("Street gift: %s already offered to actor %d"),*ItemId,Her);return;}}
 const FString ChoiceId=OpenChoice(Her,TEXT("gift"),OKMsg(TEXT("ui.economy.gift_her_button"), {{TEXT("item_name"), *Item->Name}}),{TEXT("accept"),TEXT("decline")},Answer->GetStringField(TEXT("lean")),float(Answer->GetNumberField(TEXT("insist"))),15,
  [Weak,Her,ItemId,Score](const FString& Option){if(Weak.IsValid())Weak->FinishGift(Her,ItemId,Option,Score);});
 // She is told he holds it out to her, and answers it in her next turn: as one open choice among the prompt's lines she
 // talked on about dango, the gift was taken on its deadline, and she asked whose cake that was.
 S.TellHer.Add(OKMsg(TEXT("ui.economy.gift_held_out_note"),{{TEXT("item_name"),*Item->Name},{TEXT("choice_id"),*ChoiceId}}));
 S.FreeTalkNotice=OKMsg(TEXT("ui.economy.you_pulled_out_item"), {{TEXT("item_name"), *Item->Name}});RefreshUI();
}

void AOKStreetSocialScene::FinishGift(int32 Her,const FString& ItemId,const FString& Option,float Score){
 auto& S=*State;if(!S.Catalog.Find(ItemId))return;
 if(!S.Store.Inventory.Contains(ItemId)){UE_LOG(LogTemp,Display,TEXT("Street gift: %s is no longer in his bag; nothing to give"),*ItemId);return;}
 auto Request=OKRulesHeroineRequest(S,TEXT("gift_settle"),OKStreetStableId(S.Manifest.Characters[Her]));Request->SetStringField(TEXT("item"),ItemId);Request->SetStringField(TEXT("option"),Option);Request->SetNumberField(TEXT("score"),Score);
 const TSharedPtr<FJsonObject> Answer=OKRulesEval(S,Request);if(!Answer)return;
 auto Candidate=S.Store;
 // Taken: it is hers now (items.rs), she can keep, lend or give it.
 if(Answer->GetBoolField(TEXT("accepted"))){Candidate.Inventory.RemoveSingle(ItemId);
  auto Got=OKRulesRequest(TEXT("items_received"));OKRulesPutExt(S,Got);Got->SetStringField(TEXT("girl"),OKStreetStableId(S.Manifest.Characters[Her]));Got->SetStringField(TEXT("item"),ItemId);
  if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Got))OKRulesTakeExt(Candidate,*Out);}
 ApplyRulesEffects(Candidate,*Answer);
 if(CommitStore(MoveTemp(Candidate))){FlushRulesEffects();S.FreeTalkNotice=Answer->GetStringField(TEXT("notice"));}
 RefreshUI();
}

// Lending her money (plan §8.5): she accepts when she needs it, pride may refuse.
void AOKStreetSocialScene::StartLoan(int32 Amount){
 auto& S=*State;const int32 Her=FreeTalkPartner();if(Her==INDEX_NONE)return;
 if(S.Store.PlayerCash<Amount){S.FreeTalkNotice=OKT(TEXT("ui.economy.not_enough_cash_you"));RefreshUI();return;}
 // Whether she leans to take it is a rule (shop.rs loan_offer); taking it moves the money (loan_settle).
 auto Request=OKRulesHeroineRequest(S,TEXT("loan_offer"),OKStreetStableId(S.Manifest.Characters[Her]));Request->SetNumberField(TEXT("amount"),Amount);
 const TSharedPtr<FJsonObject> Offer=OKRulesEval(S,Request);if(!Offer)return;
 // A loan to her already on offer (his words and the button for the same money): one, not two (as StartGift).
 if(S.Choices.ContainsByPredicate([Her](const FOKStreetPendingChoice& C){return C.Kind==TEXT("loan")&&C.Heroine==Her;})){UE_LOG(LogTemp,Display,TEXT("Street loan: already offered to actor %d"),Her);return;}
 TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 OpenChoice(Her,TEXT("loan"),OKMsg(TEXT("ui.economy.loan_her_button"), {{TEXT("amount"), Amount}}),{TEXT("accept"),TEXT("decline")},Offer->GetStringField(TEXT("lean")),float(Offer->GetNumberField(TEXT("insist"))),15,
  [Weak,Her,Amount](const FString& Option){if(!Weak.IsValid())return;auto& L=*Weak->State;
   if(Option!=TEXT("accept")){L.FreeTalkNotice=OKT(TEXT("ui.economy.she_declined"));Weak->RefreshUI();return;}
   if(L.Store.PlayerCash<Amount)return;
   auto Settle=OKRulesHeroineRequest(L,TEXT("loan_settle"),OKStreetStableId(L.Manifest.Characters[Her]));Settle->SetNumberField(TEXT("amount"),Amount);Settle->SetStringField(TEXT("name"),L.Manifest.Characters[Her].Name);
   const TSharedPtr<FJsonObject> Answer=OKRulesEval(L,Settle);if(!Answer)return;
   auto Candidate=L.Store;Weak->ApplyRulesEffects(Candidate,*Answer);
   if(Weak->CommitStore(MoveTemp(Candidate))){Weak->FlushRulesEffects();L.FreeTalkNotice=OKT(TEXT("ui.economy.she_accepted_loan_promise"));}
   Weak->RefreshUI();});
 S.FreeTalkNotice=OKMsg(TEXT("ui.economy.loan_ask_pending"), {{TEXT("amount"), Amount}});
 RefreshUI();
}

// A new game day's money (his allowance, her income and bills, debts, paying him back) is the rules' (day.rs day_people).

// Her wallet starts from her profile the first time it is known.
void AOKStreetSocialScene::InitWalletFromProfile(const FString& Id,const TSharedPtr<FJsonObject>& Profile){
 auto& S=*State;const FOKStreetRelation View=OKStreetViewRelation(S.Store,Id,S.Manifest.TalkBudget);if(View.Cash>=0)return;
 auto Candidate=S.Store;const int32 I=OKStreetRelationIndex(Candidate,Id,S.Manifest.TalkBudget,true);if(!Candidate.Relations.IsValidIndex(I))return;
 Candidate.Relations[I].Cash=Local(TEXT("cash"),OKStreetProfileInt(Profile,TEXT("money"),TEXT("cash"),600));Candidate.Relations[I].Bank=Local(TEXT("cash"),OKStreetProfileInt(Profile,TEXT("money"),TEXT("bank"),0));
 CommitStore(MoveTemp(Candidate));
}

// The shop panel: menu of the place he stands in, his cart, checkout buttons, gifts in the backpack, lending.
void AOKStreetSocialScene::BuildShopPanel(const TSharedRef<SVerticalBox>& Box){
 auto& S=*State;
 S.ShopPlace=ShopPlaceHere();
 const int32 Her=FreeTalkPartner();
 auto Panel=SNew(SVerticalBox);
 TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 auto Button=[&](const FString& Text,TFunction<void()> Do,bool Enabled=true){return SNew(SButton).ButtonStyle(OKStreetGlassButton()).IsEnabled(Enabled).Text(FText::FromString(Text)).OnClicked_Lambda([Weak,Do](){if(Weak.IsValid())Do();return FReply::Handled();});};
 Panel->AddSlot().AutoHeight().Padding(2)[SNew(STextBlock).Text(FText::FromString(OKMsg(TEXT("ui.economy.wallet_line"), {{TEXT("cash"), S.Store.PlayerCash}, {TEXT("bag_part"), S.Store.Inventory.IsEmpty()?TEXT(""):*OKMsg(TEXT("ui.economy.bag_count_suffix"), {{TEXT("item_count"), S.Store.Inventory.Num()}})}})))];
 // His books: read one for an hour (reading.rs), alone; with her it is the reading activities.
 {TSet<FString> Seen;auto Books=SNew(SWrapBox).UseAllottedSize(true);
  for(const FString& Id:S.Store.Inventory)if(const auto* I=S.Catalog.Find(Id);I&&I->Tags.Contains(TEXT("book"))&&!Seen.Contains(Id)){Seen.Add(Id);
   Books->AddSlot().Padding(1)[Button(OKMsg(TEXT("ui.reading.read_hour_button"),{{TEXT("book"),I->Name}}),[Weak,Id](){Weak->ReadBooks(INDEX_NONE,60,Id);},Her==INDEX_NONE)];}
  if(Seen.Num())Panel->AddSlot().AutoHeight().Padding(2)[Books];}
 if(!S.ShopPlace.IsEmpty()){
  FString Why;int32 BanDays=0;const bool Banned=Blacklisted(S.ShopPlace,&BanDays);if(Banned)Why=OKMsg(TEXT("ui.economy.shop_banned_notice"), {{TEXT("ban_days"), BanDays}});
  bool Closed=Banned;{QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_ShopClosed);Closed=Closed||PlaceClosedNow(S.ShopPlace,Why);}
  const auto* Place=OKStreetFindPlace(S.ShopPlace);
  Panel->AddSlot().AutoHeight().Padding(2)[Button(FString::Printf(TEXT("%s %s"),Place?Place->Name:*S.ShopPlace,Closed?*Why:S.ShopOpen?OKT(TEXT("ui.economy.hide_menu_button")):OKT(TEXT("ui.economy.view_menu_button"))),[Weak](){Weak->State->ShopOpen=!Weak->State->ShopOpen;Weak->RefreshUI();},!Closed)];
  // The menu itself opens as a card in the middle of the screen, not as rows in this panel.
  if(S.UiMenuHolder.IsValid()){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_ShopMenu);
   S.UiMenuHolder->SetContent(S.ShopOpen&&!Closed?BuildMenuCard(Place?Place->Name:S.ShopPlace,Her):SNullWidget::NullWidget);}
 }else{
  // He walked out: the card closes with the shop.
  S.ShopOpen=false;if(S.UiMenuHolder.IsValid())S.UiMenuHolder->SetContent(SNullWidget::NullWidget);
 }
 {QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_ShopJobs);BuildJobRows(Panel);}
 if(!S.Cart.IsEmpty()){
  for(int32 Index=0;Index<S.Cart.Num();++Index){const auto& L=S.Cart[Index];const auto* I=S.Catalog.Find(L.ItemId);if(!I)continue;
   auto Row=SNew(SHorizontalBox);
   Row->AddSlot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s %s%s%s"),L.ForHer?OKT(TEXT("ui.economy.she_speaker_label")):OKT(TEXT("ui.economy.you_speaker_label")),*I->Name,L.Sweetness.IsEmpty()?TEXT(""):*(TEXT(" ")+FString(OKT(*L.Sweetness))),L.Ice.IsEmpty()?TEXT(""):*(TEXT(" ")+FString(OKT(*L.Ice))))))];
   if(!I->Options.IsEmpty()){
    Row->AddSlot().AutoWidth()[Button(OKT(TEXT("ui.economy.sweetness_label")),[Weak,Index](){static const TArray<FString> V={TEXT("全糖"),TEXT("少糖"),TEXT("半糖"),TEXT("微糖"),TEXT("無糖")};auto& Line=Weak->State->Cart[Index];Line.Sweetness=V[(V.IndexOfByKey(Line.Sweetness)+1)%V.Num()];Weak->RefreshUI();})];
    Row->AddSlot().AutoWidth()[Button(OKT(TEXT("ui.economy.ice_label")),[Weak,Index](){static const TArray<FString> V={TEXT("正常"),TEXT("少冰"),TEXT("微冰"),TEXT("去冰"),TEXT("溫"),TEXT("熱")};auto& Line=Weak->State->Cart[Index];Line.Ice=V[(V.IndexOfByKey(Line.Ice)+1)%V.Num()];Weak->RefreshUI();})];
   }
   Row->AddSlot().AutoWidth()[Button(OKT(TEXT("ui.economy.remove_button")),[Weak,Index](){auto& L=*Weak->State;if(L.Cart.IsValidIndex(Index)){if(const auto* Gone=L.Catalog.Find(L.Cart[Index].ItemId))L.TellHer.Add(OKMsg(TEXT("ui.economy.cancelled_order_prompt"), {{TEXT("item_name"), *Gone->Name}}));L.Cart.RemoveAt(Index);L.OrderDroppedMinute=OKStreetMindNow(L);}Weak->RefreshUI();})];
   Panel->AddSlot().AutoHeight()[Row];
  }
  const FOKStreetPendingChoice* Checkout=S.Choices.FindByPredicate([](const FOKStreetPendingChoice& C){return C.Kind==TEXT("checkout");});
  if(!Checkout)Panel->AddSlot().AutoHeight().Padding(2)[Button(OKMsg(TEXT("ui.economy.checkout_total_button"), {{TEXT("cart_total"), CartTotal()}}),[Weak](){Weak->StartCheckout();})];
  else{
   const FString ChoiceId=Checkout->Id;auto Row=SNew(SHorizontalBox);
   for(const auto& Pair:TArray<TPair<FString,FString>>{{TEXT("player_pays"),OKT(TEXT("ui.economy.i_treat_button"))},{TEXT("split"),OKT(TEXT("ui.economy.split_bill_button"))},{TEXT("she_pays"),OKT(TEXT("ui.economy.let_her_treat_button"))},{TEXT("let_her_decide"),OKT(TEXT("ui.economy.let_her_decide"))}}){const FString Option=Pair.Key;
    Row->AddSlot().AutoWidth()[Button(Pair.Value,[Weak,ChoiceId,Option](){Weak->ResolveChoice(ChoiceId,Option,0.6f);})];}
   Panel->AddSlot().AutoHeight()[Row];
  }
 }
 if(Her!=INDEX_NONE){QUICK_SCOPE_CYCLE_COUNTER(STAT_Street_ShopHeroinePanel);BuildHeroinePanel(Panel,Her);} // the heroine module (OKStreetSocialHeroine.inl)
 Box->AddSlot().AutoHeight().Padding(4)[Panel];
}
TSharedRef<SWidget> AOKStreetSocialScene::BuildMenuCard(const FString& Title,int32 Her){
 auto& S=*State;
 TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 const FLinearColor Ink(.24f,.14f,.07f),Accent(.55f,.13f,.08f),Paper(.97f,.93f,.84f,.98f),Wood(.20f,.11f,.05f,.97f);
 const FSlateBrush* Fill=FCoreStyle::Get().GetBrush("GenericWhiteBox");
 auto Button=[&Weak](const FString& Text,TFunction<void()> Do){return SNew(SButton).ButtonStyle(OKStreetGlassButton()).ContentPadding(FMargin(8,2)).Text(FText::FromString(Text)).OnClicked_Lambda([Weak,Do](){if(Weak.IsValid())Do();return FReply::Handled();});};
 auto Line=[&](float Height){return SNew(SBox).HeightOverride(Height)[SNew(SBorder).BorderImage(Fill).BorderBackgroundColor(Accent.CopyWithNewOpacity(.55f))];};
 // Sections in the order a menu reads: meals first, drinks after, things to take away last.
 static const TCHAR* Order[]={TEXT("meal"),TEXT("light"),TEXT("dessert"),TEXT("fruit"),TEXT("coffee"),TEXT("drink"),TEXT("alcohol"),TEXT("stay"),TEXT("spa"),TEXT("bath"),TEXT("leisure"),TEXT("game"),TEXT("service"),TEXT("goods"),TEXT("gift")};
 auto Rank=[](const FString& Category){for(int32 I=0;I<UE_ARRAY_COUNT(Order);++I)if(Category==Order[I])return I;return int32(UE_ARRAY_COUNT(Order));};
 TArray<const FOKStreetCatalogItem*> Items=ItemsHere(S.ShopPlace);
 Items.StableSort([&Rank](const FOKStreetCatalogItem& A,const FOKStreetCatalogItem& B){return Rank(A.Category)<Rank(B.Category);});
 auto List=SNew(SVerticalBox);FString Section;
 for(const auto* Item:Items){
  if(Item->Category!=Section||List->NumSlots()==0){Section=Item->Category;
   const FString Key=TEXT("ui.economy.menu_section_")+Section;const FString Name=OKT(*Key);
   List->AddSlot().AutoHeight().Padding(0,List->NumSlots()?14:2,0,4)[SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",17)).ColorAndOpacity(Accent).Text(FText::FromString(Name==Key?Section:Name))]
    +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[Line(1.f)]];}
  const FString Id=Item->Id;auto Row=SNew(SHorizontalBox);
  Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",15)).ColorAndOpacity(Ink).Text(FText::FromString(Item->Name))];
  // The dotted leader from the dish to its price.
  Row->AddSlot().FillWidth(1).VAlign(VAlign_Center).Padding(6,0)[SNew(STextBlock).Clipping(EWidgetClipping::ClipToBounds).ColorAndOpacity(Ink.CopyWithNewOpacity(.45f)).Text(FText::FromString(FString::ChrN(120,TEXT('.'))))];
  Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",15)).ColorAndOpacity(Ink).Text(FText::FromString(Money(Item->PriceOf(Item->DefaultSize()))))];
  if(Item->Consumable||Item->Category==TEXT("stay")||Item->Category==TEXT("leisure")||Item->Category==TEXT("spa")||Item->Category==TEXT("bath")||Item->Category==TEXT("game")||Item->Category==TEXT("service"))
   Row->AddSlot().AutoWidth().Padding(2,0)[Button(OKT(TEXT("ui.economy.i_want_button")),[Weak,Id](){Weak->AddToCart(Id,false);})];
  if(Her!=INDEX_NONE&&Item->Consumable)Row->AddSlot().AutoWidth().Padding(2,0)[Button(OKT(TEXT("ui.economy.order_for_her_button")),[Weak,Id](){Weak->AddToCart(Id,true);})];
  if(Item->Giftable)Row->AddSlot().AutoWidth().Padding(2,0)[Button(OKT(TEXT("ui.economy.buy_as_gift_label")),[Weak,Id](){Weak->BuyForGift(Id);})];
  else if(Item->Consumable)Row->AddSlot().AutoWidth().Padding(2,0)[Button(OKT(TEXT("ui.economy.takeaway_label")),[Weak,Id](){Weak->BuyForGift(Id);})];
  // Goods for himself (an umbrella, a comic, a notebook): bought into his bag.
  else if(Item->Category==TEXT("goods"))Row->AddSlot().AutoWidth().Padding(2,0)[Button(OKT(TEXT("ui.economy.buy_for_self_button")),[Weak,Id](){Weak->BuyForGift(Id);})];
  List->AddSlot().AutoHeight().Padding(0,3)[Row];
 }
 // As tall as the screen allows; a long menu scrolls inside the card.
 float ListHeight=360.f;if(GEngine&&GEngine->GameViewport){FVector2D Size;GEngine->GameViewport->GetViewportSize(Size);const float Scale=FMath::Max(.5f,GEngine->GameViewport->GetDPIScale());ListHeight=FMath::Max(ListHeight,Size.Y/Scale*.42f);}
 return SNew(SBox).WidthOverride(660)[SNew(SBorder).BorderImage(Fill).BorderBackgroundColor(Wood).Padding(6)[SNew(SBorder).BorderImage(Fill).BorderBackgroundColor(Paper).Padding(FMargin(32,20,32,16))[SNew(SVerticalBox)
  +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",28)).ColorAndOpacity(Ink).Text(FText::FromString(Title))]
  +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,2,0,8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).ColorAndOpacity(Accent).Text(FText::FromString(OKT(TEXT("ui.economy.menu_card_subtitle"))))]
  +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)[Line(2.f)]
  +SVerticalBox::Slot().MaxHeight(ListHeight)[SNew(SScrollBox)+SScrollBox::Slot()[List]]
  +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[Line(2.f)]
  +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,10,0,0)[Button(OKT(TEXT("ui.economy.hide_menu_button")),[Weak](){Weak->State->ShopOpen=false;Weak->RefreshUI();})]]]];
}
// Admission as he walks in (the aquarium, the onsen): how much and who pays are the rules' (shop.rs entry_fee); she is
// the girl walking with him. He cannot pay: back out of the door.
// Walking with him, on the way to something with him, or talking with him face to face in the same place: 惠惠 walked
// into the aquarium beside him, talking, and only his ticket was bought (R3-267).
int32 AOKStreetSocialScene::EntryCompanion(const FString& Place) const{
 const auto& S=*State;if(S.Companion!=INDEX_NONE)return S.Companion;if(S.OutingGirl!=INDEX_NONE)return S.OutingGirl;
 return S.RemoteMode==0&&S.Target>0&&S.Agents.IsValidIndex(S.Target-1)&&PlaceHere(S.Agents[S.Target-1].Position)==Place?S.Target:INDEX_NONE;
}
bool AOKStreetSocialScene::PayEntry(const FString& Place,int32 MinutesInside){
 auto& S=*State;const int32 Her=EntryCompanion(Place);const bool With=S.Manifest.Characters.IsValidIndex(Her)&&Her>0;
 const FString HerId=With?OKStreetStableId(S.Manifest.Characters[Her]):FString();
 auto Request=With?OKRulesHeroineRequest(S,TEXT("entry_fee"),HerId):OKRulesRequest(TEXT("entry_fee"));OKRulesPutExt(S,Request);
 Request->SetStringField(TEXT("place"),Place);Request->SetNumberField(TEXT("player_cash"),double(S.Store.PlayerCash));Request->SetNumberField(TEXT("minutes_inside"),MinutesInside);
 Request->SetStringField(TEXT("him_dept"),S.Store.PlayerAcademic.Dept);Request->SetStringField(TEXT("him_status"),S.Store.PlayerAcademic.Status);
 if(With){const auto* A=S.Store.Academics.Find(HerId);Request->SetStringField(TEXT("her_status"),A?A->Status:FString());Request->SetStringField(TEXT("payer"),PayAgreed(Her));Request->SetNumberField(TEXT("seed"),FMath::Rand());} // nobody settled: her checkout lean (shop.rs entry_fee)
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return true;
 bool bWait=false;if(Out->TryGetBoolField(TEXT("wait"),bWait)&&bWait)return false;if(Out->GetNumberField(TEXT("fee"))<=0)return true;
 const FString Notice=Out->GetStringField(TEXT("notice"));
 if(!Out->GetBoolField(TEXT("ok"))){
  if(const FOKStreetPlace* P=OKStreetFindPlace(Place)){S.Male=P->Door;S.EntryPlace.Reset();}
  S.FreeTalkNotice=Notice;S.Message=Notice;UE_LOG(LogTemp,Display,TEXT("Street entry: %s refused (%s)"),*Place,*Notice);RefreshUI();return true;}
 auto Candidate=S.Store;OKRulesTakeExt(Candidate,*Out);ApplyRulesEffects(Candidate,*Out);if(!CommitStore(MoveTemp(Candidate)))return true;FlushRulesEffects();
 if(With)S.TellHer.Add(OKMsg(TEXT("ui.scene.game_fact_note"),{{TEXT("fact"),Notice}}));
 S.FreeTalkNotice=Notice;UE_LOG(LogTemp,Display,TEXT("Street entry: %s %s (day %d, actor %d, %d minutes in)"),*Place,*Notice,S.Store.CalendarDay,With?Her:-1,MinutesInside);RefreshUI();return true;
}
