#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
// Included by OKStreetSocialScene.cpp: photos (Docs/STREET_NEW_FEATURES_PLAN.md 0-3). Every picture is filed by the rules
// (photos.rs: who took it, where, when, who is in it, whose album); here the picture is taken, shown and sent.

// A photo's picture for Slate, loaded once from its PNG (kept for the run: the album and the threads show the same ones).
const FSlateBrush* OKStreetPhotoBrush(const FString& File){
 static TMap<FString,TSharedPtr<FSlateDynamicImageBrush>> Brushes;
 if(const TSharedPtr<FSlateDynamicImageBrush>* Have=Brushes.Find(File))return Have->Get();
 TSharedPtr<FSlateDynamicImageBrush> Brush=MakeShared<FSlateDynamicImageBrush>(FName(*File),FVector2D(320,180));
 Brushes.Add(File,Brush);return Brush.Get();
}

// The names photos.rs puts in a caption: places by id, heroines by stable id.
static void OKStreetPhotoNames(const FOKStreetSceneState& S,const TSharedRef<FJsonObject>& Request){
 auto Places=MakeShared<FJsonObject>();for(const auto& P:OKStreetPlaces())Places->SetStringField(P.Id,P.Name);Request->SetObjectField(TEXT("place_names"),Places);
 auto Names=MakeShared<FJsonObject>();for(int32 I=1;I<S.Manifest.Characters.Num();++I)Names->SetStringField(OKStreetStableId(S.Manifest.Characters[I]),S.Manifest.Characters[I].Name);Request->SetObjectField(TEXT("names"),Names);
}

// Her drawing as a drawing: the view she drew turned into a pencil-and-wash sketch (fewer tones, paper colour, the
// edges drawn in). Only the look; what it is and whose is the rules'.
static bool OKStreetSketchify(const FString& Path){
 TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Path))return false;
 IImageWrapperModule& Images=FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
 TSharedPtr<IImageWrapper> In=Images.CreateImageWrapper(EImageFormat::PNG);TArray<uint8> Raw;
 if(!In.IsValid()||!In->SetCompressed(Bytes.GetData(),Bytes.Num())||!In->GetRaw(ERGBFormat::BGRA,8,Raw))return false;
 const int32 W=In->GetWidth(),H=In->GetHeight();if(W<3||H<3)return false;
 TArray<float> Lum;Lum.SetNumUninitialized(W*H);
 for(int32 I=0;I<W*H;++I)Lum[I]=(.114f*Raw[I*4]+.587f*Raw[I*4+1]+.299f*Raw[I*4+2])/255.f;
 TArray<uint8> Out=Raw;const FLinearColor Paper(.96f,.93f,.86f);
 for(int32 Y=0;Y<H;++Y)for(int32 X=0;X<W;++X){const int32 I=Y*W+X;
  const int32 X0=FMath::Max(X-1,0),X1=FMath::Min(X+1,W-1),Y0=FMath::Max(Y-1,0),Y1=FMath::Min(Y+1,H-1);
  const float Gx=Lum[Y*W+X1]-Lum[Y*W+X0],Gy=Lum[Y1*W+X]-Lum[Y0*W+X];const float Edge=FMath::Clamp(FMath::Sqrt(Gx*Gx+Gy*Gy)*4.f,0.f,1.f);
  for(int32 C=0;C<3;++C){float V=Raw[I*4+C]/255.f;V=FMath::RoundToFloat(V*4.f)/4.f; // a few washes of colour
   const float P=C==0?Paper.B:C==1?Paper.G:Paper.R;V=FMath::Lerp(V,P,.4f)*(1.f-.75f*Edge); // on paper, with pencil lines
   Out[I*4+C]=uint8(FMath::Clamp(V,0.f,1.f)*255.f);}
  Out[I*4+3]=255;}
 TSharedPtr<IImageWrapper> Png=Images.CreateImageWrapper(EImageFormat::PNG);
 if(!Png.IsValid()||!Png->SetRaw(Out.GetData(),Out.Num(),W,H,ERGBFormat::BGRA,8))return false;
 const TArray64<uint8>& Data=Png->GetCompressed();return FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(Data.GetData(),Data.Num()),*Path);
}

// A picture was saved: filed (photo_taken), and sent to her phone thread when SendTo is a girl (she took it for him).
void AOKStreetSocialScene::PhotoFiled(const FString& File,const FString& Taker,const TArray<FString>& People,const FString& Topic,int32 SendTo,const FString& Album){
 auto& S=*State;auto Request=OKRulesRequest(TEXT("photo_taken"));OKRulesPutExt(S,Request);
 const int64 Now=OKStreetMindNow(S);Request->SetNumberField(TEXT("minute"),double(Now%1440));Request->SetStringField(TEXT("file"),File);Request->SetStringField(TEXT("place"),PlaceHere(S.Male));
 Request->SetStringField(TEXT("taker"),Taker);Request->SetStringField(TEXT("topic"),Topic);Request->SetStringField(TEXT("album"),Album);
 TArray<TSharedPtr<FJsonValue>> In;for(const FString& P:People)In.Add(MakeShared<FJsonValueString>(P));Request->SetArrayField(TEXT("people"),In);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return;
 auto Candidate=S.Store;OKRulesTakeExt(Candidate,*Out);if(!CommitStore(MoveTemp(Candidate)))return;
 UE_LOG(LogTemp,Display,TEXT("Street photo: filed #%d by %s (%s) %s"),int32(Out->GetNumberField(TEXT("id"))),*Taker,*Topic,*File);
 if(SendTo!=INDEX_NONE)SendPhoto(Taker,SendTo,MakeShared<FJsonValueNumber>(Out->GetNumberField(TEXT("id"))));
}

// She takes a photo (the AI said so): the view as it is, without the game's panels; filed once it is on disk.
void AOKStreetSocialScene::HerPhoto(int32 Girl,const FString& Topic,const FString& Subject,bool bSend,bool bDrawing){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl)||FScreenshotRequest::IsScreenshotRequested())return;
 const FString Directory=(S.Manifest.SavePath.IsEmpty()?FPaths::ProjectSavedDir()/TEXT("StreetSocial"):FPaths::GetPath(S.Manifest.SavePath))/TEXT("Photos");IFileManager::Get().MakeDirectory(*Directory,true);
 FOKStreetSceneState::FHerPhoto P;P.Girl=Girl;P.Topic=Topic;P.Subject=Subject;P.Send=bSend;P.Drawing=bDrawing;P.At=SceneTime();
 P.Path=Directory/(TEXT("Her-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png"));
 S.HerPhotos.Add(P); // taken next frame from her view (TickPhotos)
 if(!bDrawing){S.FreeTalkNotice=OKMsg(TEXT("ui.photos.she_takes_photo"),{{TEXT("name"),S.Manifest.Characters[Girl].Name}});RefreshUI();}
}

// Where the phone is held: at her eyes looking at him (subject him), at arm's length facing both (both), along her
// gaze a little upward (the view: the sky, the moon, the street); on a photo walk or a drawing, from him to her or her view.
static void OKStreetPhotoView(AOKStreetSocialScene* Scene,const FOKStreetSceneState& S,const FOKStreetSceneState::FHerPhoto& P,FVector& Loc,FRotator& Rot){
 FVector Her=S.Agents[P.Girl-1].Position+FVector(0,0,150),Him=S.Male+FVector(0,0,155);
 OKGetCrowdRuntimeBonePosition(Scene,P.Girl,TEXT("cf_j_head"),Her);{FVector H;if(OKGetCrowdRuntimeBonePosition(Scene,0,TEXT("cm_j_head"),H)||OKGetCrowdRuntimeBonePosition(Scene,0,TEXT("cf_j_head"),H))Him=H;}
 FVector Gaze=FRotator(0,S.Rotations[P.Girl].Yaw+90.f,0).Vector(); // her forward is facing yaw + 90
 // The view (the moon, what she drew) is not him: when she is turned to him (they were talking), it is the other way.
 if(P.Subject==TEXT("scene")&&FVector::DotProduct(Gaze,(Him-Her).GetSafeNormal2D())>.5f)Gaze=(Her-Him).GetSafeNormal2D();
 // A photo of a person: from about 2.3 m, aimed at the chest so the upper body is in it (from 30 cm it was her face).
 // From 40 degrees aside: straight from the photographer's side the other one stood in the way (his back filled it).
 auto Portrait=[&](const FVector& From,const FVector& Of){const FVector Chest=Of-FVector(0,0,35);Loc=Chest+FRotator(0,40,0).RotateVector((From-Of).GetSafeNormal2D())*230.f+FVector(0,0,15);Rot=(Chest-Loc).Rotation();};
 if(P.ByHim){Portrait(Him,Her);return;}
 if(P.Subject==TEXT("him")){Portrait(Her,Him);return;}
 if(P.Subject==TEXT("both")){const FVector Mid=(Her+Him)*.5f;Loc=Mid+Gaze*90.f+FVector(0,0,15);Rot=(Mid-Loc).Rotation();return;}
 Loc=Her+Gaze*30.f;Rot=FRotator(P.Drawing?-5.f:15.f,Gaze.Rotation().Yaw,0);
}

void AOKStreetSocialScene::TickPhotos(){
 auto& S=*State;const double Now=SceneTime();
 // Point the camera for the next photo, take it the frame after (the camera moves in this frame's tick).
 for(auto& P:S.HerPhotos)if(!P.Requested&&S.Agents.IsValidIndex(P.Girl-1)&&!FScreenshotRequest::IsScreenshotRequested()){
  if(S.PhotoViewUntil<Now){OKStreetPhotoView(this,S,P,S.PhotoViewLoc,S.PhotoViewRot);S.PhotoViewUntil=Now+.6;break;}
  if(Now-(S.PhotoViewUntil-.6)>.1){FScreenshotRequest::RequestScreenshot(P.Path,false,false);P.Requested=true;P.At=Now;}
  break;}
 for(int32 K=S.HerPhotos.Num()-1;K>=0;--K){const auto P=S.HerPhotos[K];if(!P.Requested)continue;
  if(IFileManager::Get().FileSize(*P.Path)>1000&&Now-P.At>.5){S.HerPhotos.RemoveAt(K);
   const FString HerId=OKStreetStableId(S.Manifest.Characters[P.Girl]);
   // Her drawing (sketching ended): the view she drew, hers to keep or give (photos.rs drawing_made).
   if(P.Drawing){if(!OKStreetSketchify(P.Path))UE_LOG(LogTemp,Warning,TEXT("Street photo: could not make %s a sketch"),*P.Path);const FString Place=PlaceHere(S.Male);const auto* Where=OKStreetFindPlace(Place);
    auto Request=OKRulesRequest(TEXT("drawing_made"));OKRulesPutExt(S,Request);Request->SetStringField(TEXT("girl"),HerId);Request->SetStringField(TEXT("girl_name"),S.Manifest.Characters[P.Girl].Name);
    Request->SetNumberField(TEXT("minute"),double(OKStreetMindNow(S)%1440));Request->SetStringField(TEXT("place"),Place);Request->SetStringField(TEXT("place_name"),Where?FString(Where->Name):FString());Request->SetStringField(TEXT("file"),P.Path);
    if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request)){auto Candidate=S.Store;OKRulesTakeExt(Candidate,*Out);
     if(CommitStore(MoveTemp(Candidate))){S.FreeTalkNotice=OKMsg(TEXT("ui.photos.she_finished_drawing"),{{TEXT("name"),S.Manifest.Characters[P.Girl].Name}});S.TellHer.Add(S.FreeTalkNotice);RefreshUI();}
     UE_LOG(LogTemp,Display,TEXT("Street photo: actor=%d drew %s (%s)"),P.Girl,*Out->GetStringField(TEXT("item")),*P.Path);}
    continue;}
   // Who is in it: him, the two of them, or neither (the view itself).
   // His, on a photo walk: of her at the stop.
   if(P.ByHim){PhotoFiled(P.Path,TEXT("him"),{HerId},P.Topic,INDEX_NONE,P.Album);continue;}
   TArray<FString> People;if(P.Subject!=TEXT("scene"))People.Add(TEXT("him"));if(P.Subject==TEXT("both"))People.Add(HerId);
   PhotoFiled(P.Path,HerId,People,P.Topic,P.Send?P.Girl:INDEX_NONE);}
  else if(Now-P.At>5){S.HerPhotos.RemoveAt(K);UE_LOG(LogTemp,Warning,TEXT("Street photo: her picture was not saved (%s)"),*P.Path);}
 }
}

// One sends a photo to the other (photo_send): it goes into her phone thread as a picture with its caption. From her:
// he sees it. From him: she reads its caption as his message and answers it.
void AOKStreetSocialScene::SendPhoto(const FString& From,int32 Girl,const TSharedPtr<FJsonValue>& Which){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl)||Girl<=0)return;
 const FString HerId=OKStreetStableId(S.Manifest.Characters[Girl]);const bool FromHim=From==TEXT("him");
 auto Request=OKRulesRequest(TEXT("photo_send"));OKRulesPutExt(S,Request);OKStreetPhotoNames(S,Request);
 Request->SetField(TEXT("which"),Which.IsValid()?Which:MakeShared<FJsonValueString>(TEXT("latest")));Request->SetStringField(TEXT("from"),From);Request->SetStringField(TEXT("to"),FromHim?HerId:FString(TEXT("him")));
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);
 if(!Out.IsValid()||!Out->GetBoolField(TEXT("ok"))){UE_LOG(LogTemp,Display,TEXT("Street photo: %s has no photo to send"),*From);return;}
 auto Candidate=S.Store;OKRulesTakeExt(Candidate,*Out);if(!CommitStore(MoveTemp(Candidate)))return;
 const FString Caption=Out->GetStringField(TEXT("caption"));
 HistoryAdd(Girl,TEXT("photo"),FromHim?TEXT("him"):TEXT("her"),FString::Printf(TEXT("%d|%s|%s"),int32(Out->GetNumberField(TEXT("id"))),*Out->GetStringField(TEXT("file")),*Caption),FromHim?FString():PlaceHere(S.Agents.IsValidIndex(Girl-1)?S.Agents[Girl-1].Position:S.Male));
 UE_LOG(LogTemp,Display,TEXT("Street photo: %s sends #%d to %s: %s"),*From,int32(Out->GetNumberField(TEXT("id"))),FromHim?*HerId:TEXT("him"),*Caption);
 if(FromHim){S.PhoneThread=Girl;S.PhoneDraft=OKMsg(TEXT("ui.photos.sent_photo_text"),{{TEXT("caption"),Caption}});PhoneSend();}
 else{S.FreeTalkNotice=OKMsg(TEXT("ui.photos.she_sent_photo"),{{TEXT("name"),S.Manifest.Characters[Girl].Name},{TEXT("caption"),Caption}});RefreshUI();}
}

// For her words and the judge: the photos she took lately and how many of the two of them there are.
FString AOKStreetSocialScene::PhotoFacts(int32 Character) const{
 const auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Character)||Character<=0)return FString();
 auto Request=OKRulesRequest(TEXT("photos_facts"));OKRulesPutExt(S,Request);Request->SetStringField(TEXT("girl"),OKStreetStableId(S.Manifest.Characters[Character]));
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);return Out.IsValid()?Out->GetStringField(TEXT("text")):FString();
}

// His album on the phone: newest first, the ones she sent him marked; each can be sent to the girl whose thread is open.
TSharedRef<SWidget> AOKStreetSocialScene::BuildPhotoAlbum(int32 Girl){
 auto& S=*State;TWeakObjectPtr<AOKStreetSocialScene> Weak(this);
 auto Request=OKRulesRequest(TEXT("photos_view"));OKRulesPutExt(S,Request);OKStreetPhotoNames(S,Request);Request->SetStringField(TEXT("owner"),TEXT("him"));
 auto Grid=SNew(SWrapBox).UseAllottedSize(true);int32 Shown=0;
 if(const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request))for(const auto& V:Out->GetArrayField(TEXT("photos"))){const auto O=V->AsObject();if(++Shown>60)break;
  const int32 PhotoId=int32(O->GetNumberField(TEXT("id")));const FString File=O->GetStringField(TEXT("file"));const bool Sent=O->GetBoolField(TEXT("sent"));
  Grid->AddSlot().Padding(4)[SNew(SBorder).BorderImage(OKFinSolid()).BorderBackgroundColor(Sent?FLinearColor(.3f,.22f,.3f):FLinearColor(.22f,.22f,.28f)).Padding(4)[SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[SNew(SBox).WidthOverride(240).HeightOverride(135)[SNew(SImage).Image(OKStreetPhotoBrush(File))]]
   +SVerticalBox::Slot().AutoHeight().Padding(0,3)[SNew(SBox).WidthOverride(240)[OKFinText(O->GetStringField(TEXT("caption")),FLinearColor(.85f,.85f,.9f),9,false,true)]]
   +SVerticalBox::Slot().AutoHeight()[OKFinButton(Weak,OKMsg(TEXT("ui.photos.send_to_her"),{{TEXT("name"),S.Manifest.Characters[Girl].Name}}),[Weak,Girl,PhotoId](){if(Weak.IsValid())Weak->SendPhoto(TEXT("him"),Girl,MakeShared<FJsonValueNumber>(PhotoId));},!Sent)]]];}
 if(Shown==0)Grid->AddSlot().Padding(6)[OKFinText(OKT(TEXT("ui.photos.album_empty")),FLinearColor(.7f,.7f,.7f),11)];
 return Grid;
}

// Reading (reading.rs): he reads a book of his for a while (alone: the clock moves on by it), or they read together
// (Girl set: his current book and hers from the things she owns).
void AOKStreetSocialScene::ReadBooks(int32 Girl,int32 Minutes,const FString& OnlyBook){
 auto& S=*State;auto Request=OKRulesRequest(TEXT("reading_read"));OKRulesPutExt(S,Request);Request->SetNumberField(TEXT("minutes"),Minutes);
 TArray<TSharedPtr<FJsonValue>> Readers;
 {auto Him=MakeShared<FJsonObject>();Him->SetStringField(TEXT("who"),TEXT("him"));TArray<TSharedPtr<FJsonValue>> Books;
  if(!OnlyBook.IsEmpty())Books.Add(MakeShared<FJsonValueString>(OnlyBook));else for(const FString& I:S.Store.Inventory)Books.Add(MakeShared<FJsonValueString>(I));
  Him->SetArrayField(TEXT("books"),Books);Readers.Add(MakeShared<FJsonValueObject>(Him));}
 if(S.Manifest.Characters.IsValidIndex(Girl)&&Girl>0){auto Her=MakeShared<FJsonObject>();Her->SetStringField(TEXT("who"),OKStreetStableId(S.Manifest.Characters[Girl]));Readers.Add(MakeShared<FJsonValueObject>(Her));}
 Request->SetArrayField(TEXT("readers"),Readers);
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return;
 auto Candidate=S.Store;OKRulesTakeExt(Candidate,*Out);if(!CommitStore(MoveTemp(Candidate)))return;
 // Alone, the hour goes by (never past the day's end); together the activity already took its time.
 if(Girl==INDEX_NONE&&!S.ClockPaused){const double Hours=OKStreetClockHours(S.ClockStartHours,FPlatformTime::Seconds()-S.ClockStartWall);const double Add=FMath::Min(Minutes/60.0,FMath::Max(0.0,23.75-Hours));if(Add>0){S.ClockStartHours+=Add;S.NextSunUpdate=0;}}
 const FString Notice=Out->GetStringField(TEXT("notice"));if(!Notice.IsEmpty())S.FreeTalkNotice=Notice;
 UE_LOG(LogTemp,Display,TEXT("Street reading: %d min, girl=%d: %d readers moved on (%s)"),Minutes,Girl,Out->GetArrayField(TEXT("read")).Num(),*Notice);RefreshUI();
}

FString AOKStreetSocialScene::ReadingFacts(int32 Character) const{
 const auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Character)||Character<=0)return FString();
 auto Request=OKRulesRequest(TEXT("reading_facts"));OKRulesPutExt(S,Request);Request->SetStringField(TEXT("girl"),OKStreetStableId(S.Manifest.Characters[Character]));
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);return Out.IsValid()?Out->GetStringField(TEXT("text")):FString();
}

// He helped her with her own work (help_her_work): how it went is counted by the rules from the crowd and the weather
// (work.rs); she hears the counts and says what she thinks.
void AOKStreetSocialScene::HelpedHerWork(int32 Girl,const FString& Place,int32 Minutes){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl)||Girl<=0)return;
 const FString Id=OKStreetStableId(S.Manifest.Characters[Girl]);const TSharedPtr<FJsonObject> Profile=S.MindProfiles.FindRef(Id);
 FString Task;if(Profile.IsValid()){const TSharedPtr<FJsonObject>* Occ=nullptr;if(Profile->TryGetObjectField(TEXT("occupation"),Occ))(*Occ)->TryGetStringField(TEXT("side_task"),Task);}
 const FDateTime Now=StreetNow();const auto* Where=OKStreetFindPlace(Place);
 auto Request=OKRulesRequest(TEXT("help_work_end"));OKRulesPutExt(S,Request);Request->SetStringField(TEXT("girl"),Id);Request->SetStringField(TEXT("task"),Task);
 Request->SetStringField(TEXT("place_name"),Where?FString(Where->Name):Place);Request->SetNumberField(TEXT("minutes"),Minutes);
 Request->SetNumberField(TEXT("crowd"),OKRulesCrowdAt(S,Place,Now.GetHour()+Now.GetMinute()/60.0));Request->SetBoolField(TEXT("raining"),RainingAt(S.Store.CalendarDay,Now.GetHour()*60+Now.GetMinute()));Request->SetNumberField(TEXT("seed"),FMath::Rand());
 const TSharedPtr<FJsonObject> Out=OKRulesEval(S,Request);if(!Out.IsValid())return;
 auto Candidate=S.Store;OKRulesTakeExt(Candidate,*Out);if(!CommitStore(MoveTemp(Candidate)))return;
 const FString Fact=Out->GetStringField(TEXT("fact"));S.TellHer.Add(Fact);S.FreeTalkNotice=Fact;RefreshUI();
 MindRemember(Girl,TEXT("activity"),Fact,Where?FString(Where->Name):Place,0.6f);
 UE_LOG(LogTemp,Display,TEXT("Street work: actor=%d helped (%s) %d min: handed %d, stopped %d, total %d"),Girl,*Task,Minutes,int32(Out->GetNumberField(TEXT("handed"))),int32(Out->GetNumberField(TEXT("stopped"))),int32(Out->GetNumberField(TEXT("progress"))));
}

// A stop on a photo walk: they stand a moment and he takes one of her where they are (the place, or the street), filed
// in the walk's album (photos.rs).
void AOKStreetSocialScene::PhotoWalkStop(int32 Girl){
 auto& S=*State;if(!S.Manifest.Characters.IsValidIndex(Girl)||FScreenshotRequest::IsScreenshotRequested())return;
 const FString Directory=(S.Manifest.SavePath.IsEmpty()?FPaths::ProjectSavedDir()/TEXT("StreetSocial"):FPaths::GetPath(S.Manifest.SavePath))/TEXT("Photos");IFileManager::Get().MakeDirectory(*Directory,true);
 FOKStreetSceneState::FHerPhoto P;P.Girl=Girl;P.ByHim=true;P.Album=S.PhotoWalkAlbum;P.At=SceneTime();
 const auto* Where=OKStreetFindPlace(PlaceHere(S.Male));P.Topic=Where?FString(Where->Name):FString(OKT(TEXT("ui.etiquette.street_location")));
 P.Path=Directory/(TEXT("Walk-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png"));
 // Her face to the camera for it: she turns to him.
 if(S.Agents.IsValidIndex(Girl-1))S.Rotations[Girl]=FRotator(0,(S.Male-S.Agents[Girl-1].Position).Rotation().Yaw-90.f,0);
 S.HerPhotos.Add(P);++S.PhotoWalkStops; // taken next frame from his view of her (TickPhotos)
 S.FreeTalkNotice=OKMsg(TEXT("ui.photos.walk_stop"),{{TEXT("n"),S.PhotoWalkStops}});RefreshUI();
 UE_LOG(LogTemp,Display,TEXT("Street photo walk: stop %d (%s)"),S.PhotoWalkStops,*P.Topic);
}
