#include "OKHScene.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Components/ButtonSlot.h"
#include "HAL/IConsoleManager.h"
#include "OKCharacterController.h"
#include "OKStreetSocialText.h"
#include "OKMotionLabelZh.h"
#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
#include "KKRustExtract.h"
#include "ok_extract.h"
#include "KKSpeech.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Async/Async.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/EditableTextBox.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

void UOKHSceneButton::HandleClick(){if(Owner)Owner->Click(Kind);}

// The authored metadata of every pose, by "AnimationInfo_XX:id" (ok-hposes from the original h/list).
static const TMap<FString,TSharedPtr<FJsonObject>>& HPoseMeta(){
    static TMap<FString,TSharedPtr<FJsonObject>> Meta;static bool Loaded=false;
    if(Loaded)return Meta;Loaded=true;
    FString Json;TArray<TSharedPtr<FJsonValue>> Rows;
    if(FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("IndependentResources/StreetSocialData/h_poses.json")))&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Rows))
        for(const auto& V:Rows)if(const auto O=V->AsObject())Meta.Add(FString::Printf(TEXT("%s:%d"),*O->GetStringField(TEXT("category")),int32(O->GetNumberField(TEXT("id")))),O);
    UE_LOG(LogTemp,Display,TEXT("H scene: %d authored poses"),Meta.Num());
    return Meta;
}

// A state without the anal prefix: the flow is the same for both.
static FString HBase(const FString& State){return State.StartsWith(TEXT("A_"))?State.Mid(2):State;}
static bool HIsLoop(const FString& Base){return Base==TEXT("WLoop")||Base==TEXT("SLoop");}

int32 UOKHScenePanel::Level() const{return Gallery&&Gallery->HExperience.IsValid()?int32(Gallery->HExperience->GetNumberField(TEXT("level"))):0;}
FString UOKHScenePanel::Current() const{if(Mode==0)return SoloState;return Gallery&&Gallery->PairedPlayback?Gallery->PairedPlayback->GetState():FString();}
bool UOKHScenePanel::Has(const FString& State) const{if(Mode==0)return SoloKeys.Contains(State);return Gallery&&Gallery->PairStateCombo&&Gallery->PairStateCombo->FindOptionIndex(State)>0;}
float UOKHScenePanel::AreaExperience(int32 Area) const{
    static const TCHAR* Keys[]={TEXT("kiss"),TEXT("chest"),TEXT("crotch"),TEXT("anal"),TEXT("butt"),TEXT("nipple")};
    const TSharedPtr<FJsonObject>* Areas=nullptr;double V=0;
    if(Gallery&&Gallery->HExperience.IsValid()&&Gallery->HExperience->TryGetObjectField(TEXT("areas"),Areas)&&Area>=0&&Area<6)(*Areas)->TryGetNumberField(Keys[Area],V);
    return float(V);
}
// The private room is a bare floor: a pose fits it when the original lets it play on the floor, a bed or standing
// (H point categories 0, 1, 9). Chair, desk, wall, pool, fence, net and the rest need their prop and floated there (he
// sat on air in 椅子対面, she lay in mid-air in 机寝位).
static TAutoConsoleVariable<int32> CVarHAllPoses(TEXT("ok.HScene.AllPoses"),0,TEXT("1: list every authored pose in the private room, props or not (testing prop placement)"));
// The room's prop for a pose: 0 none (it plays on the floor, a bed or standing: categories 0, 1, 9), 1 a seat (chair
// 2-5, bench 12, prank chair 1003, straddled chair 1200, sofa 1201), 2 a table (desk 6-7, box 1009, table tennis 1301,
// crouch at a desk 1305), 3 a wall (8, bookshelf 1002, fence 1006, net 1300, wall 1302, pressed 1304); -1 none fits
// (pool, banana boat, the rest): left out of the room.
static int32 HPropKind(const FString& Category,int32 Id){
    const TSharedPtr<FJsonObject>* M=HPoseMeta().Find(FString::Printf(TEXT("%s:%d"),*Category,Id));if(!M)return 0;
    const TArray<TSharedPtr<FJsonValue>>* Places=nullptr;if(!(*M)->TryGetArrayField(TEXT("categories"),Places)||Places->IsEmpty())return 0;
    int32 Kind=-1;
    for(const auto& V:*Places){const int32 C=int32(V->AsNumber());
        if(C==0||C==1||C==9)return 0;
        const int32 K=(C>=2&&C<=5)||C==12||C==1003||C==1200||C==1201?1:(C==6||C==7||C==1009||C==1301||C==1305)?2:(C==8||C==1002||C==1006||C==1300||C==1302||C==1304)?3:-1;
        if(K>0&&Kind<0)Kind=K;}
    return Kind;
}
static bool HFitsRoom(const FString& Category,int32 Id){return CVarHAllPoses.GetValueOnGameThread()||HPropKind(Category,Id)>=0;}
void UOKHScenePanel::LoadAibuPoses(){
    bAibuLoaded=true;AibuPoses.Reset();TArray<TSharedPtr<FJsonValue>> Rows;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FOkSession::CommonInteractionPoses()),Rows))return;
    for(const auto& V:Rows){const auto Row=V->AsObject();if(!Row||Row->GetIntegerField(TEXT("participants"))!=1||Row->GetStringField(TEXT("category"))!=TEXT("AnimationInfo_00"))continue;
        if(!HFitsRoom(TEXT("AnimationInfo_00"),Row->GetIntegerField(TEXT("id"))))continue;
        FAibuPose& Pose=AibuPoses.AddDefaulted_GetRef();Pose.Source=Row->GetStringField(TEXT("female_source"));Pose.Layer=Row->GetIntegerField(TEXT("female_layer"));Pose.Id=Row->GetIntegerField(TEXT("id"));Pose.Prop=FMath::Max(0,HPropKind(TEXT("AnimationInfo_00"),Pose.Id));
        const FString Raw=Row->GetStringField(TEXT("name"));Pose.Name=Raw.IsEmpty()?FString::Printf(TEXT("#%d"),Row->GetIntegerField(TEXT("id"))):OKMotionLabelDisplay(Raw);}
    UE_LOG(LogTemp,Display,TEXT("H scene: %d caress poses"),AibuPoses.Num());
}
void UOKHScenePanel::SelectAibu(int32 Index){
    if(!Gallery||!AibuPoses.IsValidIndex(Index)||!Gallery->PreviewActor)return;
    if(RefuseWhenEnough())return;
    PendingAibu=Index;Queue.Reset();Wanted.Reset();TouchArea=INDEX_NONE;BuildList();
    const int32 Generation=++AibuGeneration;const FAibuPose Pose=AibuPoses[Index];const FString Root=FOkSession::CommonPairRoot();
    TWeakObjectPtr<UOKHScenePanel> Weak(this);
    if(Hint)Hint->SetText(FText::FromString(OKT(TEXT("ui.hscene.loading_caress")))); // a hint, not something they did (not in her memory)
    Async(EAsyncExecution::Thread,[Weak,Generation,Pose,Root](){
        TMap<FString,FString> Keys;const FString Prefix=TEXT(" / [Controller layer 0] Base Layer.");
        FOkSession::BeginAnimationPreparation();
        for(const FString& Label:FOkSession::AnimationSourceNames(Root,Pose.Source)){const int32 At=Label.Find(Prefix);if(At==INDEX_NONE)continue;
            const FString State=Label.Mid(At+Prefix.Len());if(State.Contains(TEXT(".")))continue;
            const FString Key=FOkSession::PairClipForRoot(Root,Pose.Source+TEXT("|")+Label,Pose.Layer);
            if(!Key.IsEmpty()&&FOkSession::PrepareAnimationClip(Root,Key))Keys.Add(State,Key);}
        FOkSession::EndAnimationPreparation();
        AsyncTask(ENamedThreads::GameThread,[Weak,Generation,Keys=MoveTemp(Keys)]() mutable {
            auto* UI=Weak.Get();if(!UI||UI->AibuGeneration!=Generation||!UI->Gallery||!UI->Gallery->PreviewActor)return;
            if(!Keys.Contains(TEXT("Idle"))){UI->PendingAibu=INDEX_NONE;UI->Note(OKT(TEXT("ui.hscene.pose_missing")),false);UI->BuildList();return;}
            UOKCharacterController* G=UI->Gallery;
            // Him out of the picture as in the original caress; his pose stops.
            // Only the pair playback stops: OnStopPair also destroys him, and no pose could be chosen after the caress.
            G->CancelPairPreparation();G->bPairBreathArmed=false;G->PreviewActor->StopHBreath();
            if(G->PairedPlayback){G->PairedPlayback->Stop();G->PairedPlayback->Destroy();G->PairedPlayback=nullptr;}
            if(G->PartnerActor){G->PartnerActor->StopHBreath();G->PartnerActor->SetActorHiddenInGame(true);}
            G->PreviewActor->SetActorTickEnabled(true);G->PreviewActor->SetGameSceneMode(3,false);G->SelectedGameSceneMode=3;
            UI->SoloKeys=MoveTemp(Keys);UI->ActiveAibu=UI->PendingAibu;UI->PendingAibu=INDEX_NONE;UI->ClearLine();UI->ActivePose=INDEX_NONE;UI->Mode=0;UI->SoloState.Reset();
            G->NoteAction(TEXT("pair"),UI->AibuPoses[UI->ActiveAibu].Name);
            UI->PlaySolo(TEXT("Idle"));UI->BuildList();if(UI->Hint)UI->Hint->SetText(FText::FromString(UI->AibuPoses[UI->ActiveAibu].Name)); // loaded: the hint no longer says loading
        });
    });
}
void UOKHScenePanel::PlaySolo(const FString& State){
    if(!Gallery||!Gallery->PreviewActor)return;const FString* Key=SoloKeys.Find(State);if(!Key)return;
    if(Gallery->PreviewActor->SetGameAnimation(*Key)){SoloState=State;SoloStarted=FPlatformTime::Seconds();Gallery->SyncGameAnimationSelection(*Key);CaressBreath();}
    else UE_LOG(LogTemp,Warning,TEXT("H scene: caress state %s did not play"),*State);
}
void UOKHScenePanel::Touch(int32 Area){
    const TSharedPtr<FJsonObject> X=Gallery?Gallery->HExperience:nullptr;
    if(Area==0){
        // A kiss she has not allowed yet (not his lover, never kissed) is refused (HandCtrl: voice 103).
        if(!(X.IsValid()&&X->GetBoolField(TEXT("kiss_ok")))){Note(OKT(TEXT("ui.hscene.kiss_refused")));Chain({TEXT("Front_Dislikes"),TEXT("Idle")});TouchArea=INDEX_NONE;return;}
        TouchArea=0;Chain({TEXT("K_Touch"),TEXT("K_Loop")});Note(OKT(TEXT("ui.hscene.event_kiss")));return;
    }
    // Her anal untouched and him not her lover: she pulls away (anal_dislikes).
    if(Area==3&&AreaExperience(3)<=0.f&&Gallery->HHeroineStage!=TEXT("lover")){Note(OKT(TEXT("ui.hscene.anal_touch_refused")));Chain({TEXT("Back_Dislikes"),TEXT("Idle")});TouchArea=INDEX_NONE;return;}
    static const TCHAR* Prefix[]={TEXT(""),TEXT("M_"),TEXT("A_"),TEXT("S_"),TEXT("S_"),TEXT("M_")};
    static const TCHAR* TouchEvents[]={TEXT(""),TEXT("ui.hscene.event_touch_chest"),TEXT("ui.hscene.event_touch_crotch"),TEXT("ui.hscene.event_touch_anal"),TEXT("ui.hscene.event_touch_butt"),TEXT("ui.hscene.event_touch_nipple")};
    if(TouchArea!=Area)Note(OKT(TouchEvents[Area]));
    TouchArea=Area;Chain({FString(Prefix[Area])+TEXT("Touch"),FString(Prefix[Area])+TEXT("Idle")});
}
void UOKHScenePanel::Note(const FString& Text,bool bReact){Events.Add(Text);if(Gallery)Gallery->NoteAction(TEXT("h"),Text);if(bReact)React(Text);if(Hint)Hint->SetText(FText::FromString(Text));UE_LOG(LogTemp,Display,TEXT("H scene: %s"),*Text);}

TSharedRef<SWidget> UOKHScenePanel::RebuildWidget(){
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    UHorizontalBox* Root=WidgetTree->ConstructWidget<UHorizontalBox>();WidgetTree->RootWidget=Root;
    USizeBox* Space=WidgetTree->ConstructWidget<USizeBox>();Space->SetVisibility(ESlateVisibility::HitTestInvisible);Root->AddChildToHorizontalBox(Space)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    USizeBox* Width=WidgetTree->ConstructWidget<USizeBox>();Width->SetWidthOverride(340.f);
    if(auto* S=Root->AddChildToHorizontalBox(Width)){S->SetPadding(FMargin(0,72,16,16));S->SetVerticalAlignment(VAlign_Fill);}
    UBorder* Back=WidgetTree->ConstructWidget<UBorder>();Back->SetBrushColor(FLinearColor(.06f,.02f,.05f,.82f));Back->SetPadding(FMargin(12));Width->AddChild(Back);
    UVerticalBox* Box=WidgetTree->ConstructWidget<UVerticalBox>();Back->AddChild(Box);
    auto Text=[&](float Size,const FLinearColor& Color){auto* T=WidgetTree->ConstructWidget<UTextBlock>();FSlateFontInfo F=T->GetFont();F.Size=Size;T->SetFont(F);T->SetColorAndOpacity(FSlateColor(Color));T->SetAutoWrapText(true);Box->AddChildToVerticalBox(T)->SetPadding(FMargin(0,2));return T;};
    auto Gauge=[&](const TCHAR* Label,const FLinearColor& Color){
        UHorizontalBox* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* L=WidgetTree->ConstructWidget<UTextBlock>();L->SetText(FText::FromString(OKT(Label)));L->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        if(auto* S=Row->AddChildToHorizontalBox(L)){S->SetPadding(FMargin(0,0,8,0));S->SetVerticalAlignment(VAlign_Center);}
        auto* Bar=WidgetTree->ConstructWidget<UProgressBar>();Bar->SetFillColorAndOpacity(Color);Bar->SetPercent(0.f);
        if(auto* S=Row->AddChildToHorizontalBox(Bar)){S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));S->SetVerticalAlignment(VAlign_Center);}
        Box->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,3));return Bar;
    };
    Title=Text(15.f,FLinearColor(1.f,.8f,.9f));
    Status=Text(12.f,FLinearColor::White);
    HerGauge=Gauge(TEXT("ui.hscene.her_gauge"),FLinearColor(1.f,.35f,.6f));
    HisGauge=Gauge(TEXT("ui.hscene.his_gauge"),FLinearColor(.35f,.6f,1.f));
    {
        UHorizontalBox* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
        auto* L=WidgetTree->ConstructWidget<UTextBlock>();L->SetText(FText::FromString(OKT(TEXT("ui.hscene.speed"))));L->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        if(auto* S=Row->AddChildToHorizontalBox(L)){S->SetPadding(FMargin(0,0,8,0));S->SetVerticalAlignment(VAlign_Center);}
        Speed=WidgetTree->ConstructWidget<USlider>();Speed->SetMinValue(0.f);Speed->SetMaxValue(1.f);Speed->SetValue(SpeedValue);
        if(auto* S=Row->AddChildToHorizontalBox(Speed))S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Box->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,3));
    }
    Actions=WidgetTree->ConstructWidget<UWrapBox>();Actions->SetInnerSlotPadding(FVector2D(6,6));Box->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0,8));
    Hint=Text(12.f,FLinearColor(1.f,.9f,.6f));
    Caption=Text(13.f,FLinearColor(1.f,1.f,1.f));
    Input=WidgetTree->ConstructWidget<UEditableTextBox>();Input->SetHintText(FText::FromString(OKT(TEXT("ui.hscene.say_hint"))));Input->OnTextCommitted.AddDynamic(this,&UOKHScenePanel::OnInputCommitted);
    Box->AddChildToVerticalBox(Input)->SetPadding(FMargin(0,4));
    Tabs=WidgetTree->ConstructWidget<UWrapBox>();Tabs->SetInnerSlotPadding(FVector2D(4,4));Box->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0,8,0,4));
    List=WidgetTree->ConstructWidget<UScrollBox>();
    if(auto* S=Box->AddChildToVerticalBox(List))S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto Tab_=[this](int32 Kind,const TCHAR* Key){auto* B=WidgetTree->ConstructWidget<UOKHSceneButton>();B->Kind=Kind;B->Owner=this;B->OnClicked.AddDynamic(B,&UOKHSceneButton::HandleClick);auto* L=WidgetTree->ConstructWidget<UTextBlock>();L->SetText(FText::FromString(OKT(Key)));B->SetContent(L);Tabs->AddChildToWrapBox(B);};
    Tab_(TabAibu,TEXT("ui.hscene.tab_aibu"));Tab_(TabHand,TEXT("ui.hscene.tab_hand"));Tab_(TabMouth,TEXT("ui.hscene.tab_mouth"));Tab_(TabBreast,TEXT("ui.hscene.tab_breast"));Tab_(TabSonyu,TEXT("ui.hscene.tab_sonyu"));Tab_(TabClothes,TEXT("ui.hscene.tab_clothes"));
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    return Super::RebuildWidget();
}

void UOKHScenePanel::LoadPoses(){
    Poses.Reset();if(!Gallery)return;
    const auto& Meta=HPoseMeta();
    for(int32 I=0;I<Gallery->NativePairs.Num();++I){
        const auto& Row=Gallery->NativePairs[I];if(!Row)continue;
        const FString Category=Row->GetStringField(TEXT("category"));const int32 Id=int32(Row->GetNumberField(TEXT("id")));
        FOKHPose Pose;Pose.Pair=I;Pose.Mode=Category==TEXT("AnimationInfo_01")?1:Category==TEXT("AnimationInfo_02")?2:0;if(!Pose.Mode||!HFitsRoom(Category,Id))continue;
        Pose.Name=Row->GetStringField(TEXT("name"));
        if(const TSharedPtr<FJsonObject>* M=Meta.Find(FString::Printf(TEXT("%s:%d"),*Category,Id))){
            Pose.KindHoushi=int32((*M)->GetNumberField(TEXT("kind_houshi")));Pose.Anal=(*M)->GetBoolField(TEXT("anal"));
            Pose.IsExperience=int32((*M)->GetNumberField(TEXT("is_experience")));Pose.StateRestriction=int32((*M)->GetNumberField(TEXT("state_restriction")));
            const FString Named=(*M)->GetStringField(TEXT("name"));if(!Named.IsEmpty())Pose.Name=Named;
        }
        Pose.Prop=FMath::Max(0,HPropKind(Category,Id));
        if(Pose.Name.IsEmpty())Pose.Name=FString::Printf(TEXT("#%d"),Id);
        Pose.Raw=Pose.Name;Pose.Name=OKMotionLabelDisplay(Pose.Raw);
        Poses.Add(Pose);
    }
}

void UOKHScenePanel::BuildList(){
    if(!List||!WidgetTree)return;List->ClearChildren();
    auto Add=[this](int32 Kind,const FString& Label,bool Enabled,bool Current){
        auto* B=WidgetTree->ConstructWidget<UOKHSceneButton>();B->Kind=Kind;B->Owner=this;B->OnClicked.AddDynamic(B,&UOKHSceneButton::HandleClick);B->SetIsEnabled(Enabled);
        if(Current)B->SetBackgroundColor(FLinearColor(1.f,.45f,.7f));
        auto* L=WidgetTree->ConstructWidget<UTextBlock>();L->SetText(FText::FromString(Label));L->SetAutoWrapText(true);L->SetJustification(ETextJustify::Center);
        // The label fills the button: centred in it, it wrapped at its own last width ("單手／交" on two lines).
        if(auto* S=Cast<UButtonSlot>(B->SetContent(L)))S->SetHorizontalAlignment(HAlign_Fill);List->AddChild(B);
    };
    if(Tab==TabClothes){
        static const TCHAR* Parts[]={TEXT("ui.hscene.cloth_top"),TEXT("ui.hscene.cloth_bottom"),TEXT("ui.hscene.cloth_bra"),TEXT("ui.hscene.cloth_shorts"),TEXT("ui.hscene.cloth_gloves"),TEXT("ui.hscene.cloth_pantyhose"),TEXT("ui.hscene.cloth_socks"),TEXT("ui.hscene.cloth_inner_shoes"),TEXT("ui.hscene.cloth_shoes")};
        static const TCHAR* States[]={TEXT("ui.hscene.cloth_on"),TEXT("ui.hscene.cloth_half"),TEXT("ui.hscene.cloth_half2"),TEXT("ui.hscene.cloth_off")};
        // Each piece she is wearing comes off or goes back on by itself (visibility only: a reload would end the scene).
        static const TCHAR* Views[]={TEXT("ui.hscene.male_view_shown"),TEXT("ui.hscene.male_view_ghost"),TEXT("ui.hscene.male_view_hidden")};
        Add(ToggleMale,OKT(Views[FMath::Clamp(MaleView,0,2)]),Mode!=0,MaleView!=0);
        Add(MaleBottom,OKT(bMaleBottomOff?TEXT("ui.hscene.male_bottom_off"):TEXT("ui.hscene.male_bottom_on")),Mode!=0,bMaleBottomOff);
        Add(RoomBase,OKT(TEXT("ui.hscene.room_floor")),true,Room==0);Add(RoomBase+1,OKT(TEXT("ui.hscene.room_bed")),true,Room==1);
        Add(ClothesBase+100,OKT(TEXT("ui.hscene.cloth_all_on")),true,false);Add(ClothesBase+102,OKT(TEXT("ui.hscene.cloth_all_off")),true,false);
        auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;
        for(int32 K=0;K<9;++K)if(Girl&&Girl->HasClothingKind(K)&&Girl->DisplayClothesStates[K]!=3)
            Add(ClothesBase+K,FString::Printf(TEXT("%s：%s"),OKT(Parts[K]),OKT(States[FMath::Clamp<int32>(Gallery->ClothesStates[K],0,3)])),true,Gallery->ClothesStates[K]==3);
        return;
    }
    if(Tab==TabAibu){if(!bAibuLoaded)LoadAibuPoses();for(int32 I=0;I<AibuPoses.Num();++I)Add(AibuBase+I,AibuPoses[I].Name,true,(I==ActiveAibu&&Mode==0)||I==PendingAibu);return;}
    const bool Used=Gallery&&Gallery->HExperience.IsValid()&&Gallery->HExperience->GetBoolField(TEXT("used"));
    for(int32 I=0;I<Poses.Num();++I){const FOKHPose& Pose=Poses[I];
        const bool Shown=Tab==TabSonyu?Pose.Mode==2:Pose.Mode==1&&Pose.KindHoushi==Tab-TabHand;if(!Shown)continue;
        const bool Locked=Pose.IsExperience==1&&!Used;
        FString Label=Pose.Name;if(Pose.Anal)Label+=OKT(TEXT("ui.hscene.anal_mark"));if(Locked)Label+=OKT(TEXT("ui.hscene.locked_mark"));
        Add(PoseBase+I,Label,!Locked,(Mode!=0&&I==ActivePose)||I==PendingPose);
    }
}

void UOKHScenePanel::SelectPose(int32 Index){
    if(!Gallery||!Poses.IsValidIndex(Index)||!Gallery->PairCombo)return;
    if(RefuseWhenEnough())return;
    int32 Option=Gallery->PairVisibleIndices.Find(Poses[Index].Pair);
    if(Option==INDEX_NONE&&Gallery->PairCategoryCombo){Gallery->PairCategoryCombo->SetSelectedIndex(0);Option=Gallery->PairVisibleIndices.Find(Poses[Index].Pair);}
    if(Option==INDEX_NONE){Note(OKT(TEXT("ui.hscene.pose_missing")),false);return;}
    if(Gallery->PartnerActor)Gallery->PartnerActor->SetActorHiddenInGame(false);
    ++AibuGeneration;PendingAibu=INDEX_NONE;TouchArea=INDEX_NONE;
    PendingPose=Index;Queue.Reset();Wanted.Reset();bAnalPlay=false;bInsideFinished=false;bStrong=false;
    Gallery->PairCombo->SetSelectedIndex(Option+1);
    Gallery->NoteAction(TEXT("pair"),Poses[Index].Name);
    BuildList();
}

void UOKHScenePanel::Want(const FString& State){
    if(!Has(State)){UE_LOG(LogTemp,Display,TEXT("H scene: pose has no state %s"),*State);return;}
    Wanted=State;LastRequest=0.;
}
void UOKHScenePanel::Chain(const TArray<FString>& States){
    Queue.Reset();for(const FString& S:States)if(Has(S))Queue.Add(S);
    if(!Queue.IsEmpty())Want(Queue[0]);
}

void UOKHScenePanel::ApplySpeed(){
    if(!Gallery||!Gallery->PairedPlayback)return;
    const float Target=HIsLoop(HBase(Current()))?PlaybackSpeed():1.f;
    if(!FMath::IsNearlyEqual(Gallery->PairedPlayback->GetSpeed(),Target,.01f))Gallery->OnPairSpeed(Target/4.f);
}

void UOKHScenePanel::Finish(const TCHAR* Kind){
    const FString K=Kind;++LineEpoch;
    if(Mode==1){
        ++ServiceFinishes;++HisFinishes;His=0.f;
        // In her mouth only for a mouth pose: on her chest or in her hand is outside her (not swallowed or spat).
        if(K==TEXT("inside")&&(!Poses.IsValidIndex(ActivePose)||Poses[ActivePose].KindHoushi==1))++HisMouth;else ++HisOutside;
        if(K==TEXT("inside")){const int32 Kind=Poses.IsValidIndex(ActivePose)?Poses[ActivePose].KindHoushi:1;
            // In her mouth: she holds it, then swallows or spits (Oral_Idle). On her chest or in her hand there is nothing to
            // swallow: the finish, then the after state (IN_A, or OUT_A where the pose has none).
            if(Kind==1){Chain({TEXT("IN_Start"),TEXT("IN_Loop"),TEXT("Oral_Idle_IN"),TEXT("Oral_Idle")});OralIdleSince=0.;}
            else Chain({TEXT("IN_Start"),TEXT("IN_Loop"),Has(TEXT("IN_A"))?TEXT("IN_A"):TEXT("OUT_A")});
            if(Kind==2)AddSiru(1); // on her chest
            Note(OKT(Kind==1?TEXT("ui.hscene.event_service_inside"):Kind==2?TEXT("ui.hscene.event_service_chest"):TEXT("ui.hscene.event_service_hand")));}
        else{const bool Mouth=Poses.IsValidIndex(ActivePose)&&Poses[ActivePose].KindHoushi==1;Chain({TEXT("M_OUT_Start"),TEXT("M_OUT_Loop"),TEXT("OUT_A")});AddSiru(Mouth?0:1);
            // Out of her mouth it lands on her face (she said "my mouth is full" to a plain "outside").
            Note(OKT(Mouth?TEXT("ui.hscene.event_service_face"):TEXT("ui.hscene.event_service_outside")));}
        return;
    }
    const bool Org=K.StartsWith(TEXT("org")),Same=K.StartsWith(TEXT("same"));
    if(Org||Same){++(bAnalPlay?AnalClimaxes:CrotchClimaxes);Her=0.f;}
    if(Same||K==TEXT("inside")||K==TEXT("outside")){++HisFinishes;His=0.f;++(K==TEXT("outside")?HisOutside:HisInside);}
    if(Org||Same){ReleaseHeld();CountHerOrgasm();}
    bInsideFinished=Same||K==TEXT("inside");
    if(K==TEXT("outside"))AddSiru(SiruSide()==0?1:3); // over the side of her he is on
    else if((Same||K==TEXT("inside"))&&!bCondom)AddSiru(bAnalPlay?4:2);
    TArray<FString> States={P(TEXT("OLoop"))};
    if(K==TEXT("outside"))States.Append({P(TEXT("M_OUT_Start")),P(TEXT("M_OUT_Loop")),P(TEXT("OUT_A"))});
    else if(K==TEXT("inside"))States.Append({P(TEXT("M_IN_Start")),P(TEXT("M_IN_Loop")),P(TEXT("IN_A"))});
    else if(K==TEXT("orgW"))States.Append({P(TEXT("WF_IN_Start")),P(TEXT("WF_IN_Loop")),P(TEXT("WS_IN_A"))});
    else if(K==TEXT("sameW"))States.Append({P(TEXT("WS_IN_Start")),P(TEXT("WS_IN_Loop")),P(TEXT("WS_IN_A"))});
    else if(K==TEXT("orgS"))States.Append({P(TEXT("SF_IN_Start")),P(TEXT("SF_IN_Loop")),P(TEXT("SS_IN_A"))});
    else States.Append({P(TEXT("SS_IN_Start")),P(TEXT("SS_IN_Loop")),P(TEXT("SS_IN_A"))});
    Chain(States);
    Note(OKT(Org?TEXT("ui.hscene.event_her_orgasm"):Same?TEXT("ui.hscene.event_together"):K==TEXT("inside")?(bCondom?TEXT("ui.hscene.event_inside_condom"):TEXT("ui.hscene.event_inside")):TEXT("ui.hscene.event_outside")));
}

void UOKHScenePanel::Click(int32 Kind){
    if(!Gallery)return;
    const FString Base=Mode==0?Current():HBase(Current());
    if(Kind>=TabHand&&Kind<=TabAibu){Tab=Kind;BuildList();return;}
    if(Kind>=PoseBase&&Kind<PoseBase+1000){SelectPose(Kind-PoseBase);return;}
    if(Kind>=AibuBase&&Kind<PoseBase){SelectAibu(Kind-AibuBase);return;}
    if(Kind>=RoomBase&&Kind<=RoomBase+1){Room=Kind-RoomBase;PropKey.Reset();Note(OKT(Room==1?TEXT("ui.hscene.event_room_bed"):TEXT("ui.hscene.event_room_floor")),false);BuildList();return;}
    if(Kind>=TouchChest&&Kind<=Kiss&&Mode==0){static const int32 Areas[]={1,2,3,4,5,0};Touch(Areas[Kind-TouchChest]);ActionsKey.Reset();return;}
    if(Mode==0&&Kind>=Faster&&Kind<=Continue)return; // pair actions while caressing (a stale button or the control channel)
    // An action counts only when the panel offers it now (finishing outside at an empty gauge, inserting during a
    // service): the same rule for a click, a stale button and the control channel.
    if((Kind>=Faster&&Kind<=Continue)||(Kind>=TouchChest&&Kind<=Release)){BuildActions();if(!Offered.Contains(Kind)){UE_LOG(LogTemp,Display,TEXT("H scene: action %d not offered now"),Kind);return;}}
    // Insertion and the condom belong to intercourse poses, anal to the ones that allow it (a stale button or the channel).
    if((Kind==Insert||Kind==InsertAnal||Kind==Condom||Kind==Pull)&&Mode!=2)return;
    if(Kind==InsertAnal&&!(Poses.IsValidIndex(ActivePose)&&Poses[ActivePose].Anal))return;
    if(Kind==LockHer){bLockHer=!bLockHer;ActionsKey.Reset();return;}
    if(Kind==LockHis){bLockHis=!bLockHis;ActionsKey.Reset();return;}
    if((Kind==ToggleMale||Kind==MaleBottom)&&Mode==0)return; // he is not there in a caress
    if(Kind==ToggleMale){MaleView=(MaleView+1)%3;bMaleHidden=MaleView==2;ApplyMaleLook();BuildList();return;}
    if(Kind==MaleBottom){bMaleBottomOff=!bMaleBottomOff;ApplyMaleLook();BuildList();return;}
    if(Kind==Release&&Mode==0){TouchArea=INDEX_NONE;Chain({TEXT("Idle")});ActionsKey.Reset();return;}
    if(Kind>=ClothesBase){
        auto* Girl=Gallery->PreviewActor.Get();const int32 C=Kind-ClothesBase;if(!IsValid(Girl))return;
        static const TCHAR* Parts[]={TEXT("ui.hscene.cloth_top"),TEXT("ui.hscene.cloth_bottom"),TEXT("ui.hscene.cloth_bra"),TEXT("ui.hscene.cloth_shorts"),TEXT("ui.hscene.cloth_gloves"),TEXT("ui.hscene.cloth_pantyhose"),TEXT("ui.hscene.cloth_socks"),TEXT("ui.hscene.cloth_inner_shoes"),TEXT("ui.hscene.cloth_shoes")};
        uint8* S=Gallery->ClothesStates;FString Event;
        if(C==100){for(int32 K=0;K<9;++K)S[K]=Girl->DisplayClothesStates[K];Event=OKT(TEXT("ui.hscene.event_dressed"));}
        else if(C==102){for(int32 K=0;K<9;++K)S[K]=3;Event=OKT(TEXT("ui.hscene.event_undressed"));}
        else if(C>=0&&C<9){const bool Off=S[C]!=3;S[C]=Off?3:Girl->DisplayClothesStates[C];Event=OKMsg(Off?TEXT("ui.hscene.event_part_off"):TEXT("ui.hscene.event_part_on"),{{TEXT("part"),OKT(Parts[C])}});}
        else return;
        Girl->SetClothingPartsHidden(S);Gallery->RefreshClothesStateLabels();Note(Event);BuildList();return;
    }
    const auto& X=Gallery->HExperience;
    switch(Kind){
    case Faster:
        // From a standstill the button only starts them moving (start, move); faster only once moving.
        if(HIsLoop(Base)){SpeedValue=FMath::Min(1.f,SpeedValue+.15f);if(Speed)Speed->SetValue(SpeedValue);}
        if(Mode==1&&(Base==TEXT("Idle")||Base==TEXT("Stop_Idle")||Base.EndsWith(TEXT("_A"))))Want(TEXT("WLoop"));
        else if(Mode==2&&Base==TEXT("InsertIdle"))Want(P(TEXT("WLoop")));
        break;
    case Slower:
        SpeedValue=FMath::Max(0.f,SpeedValue-.15f);if(Speed)Speed->SetValue(SpeedValue);
        if(Mode==2&&SpeedValue<=0.f&&HIsLoop(Base))Want(P(TEXT("InsertIdle")));
        break;
    case Toggle:if(HIsLoop(Base)){bStrong=Base==TEXT("WLoop");Want(P(bStrong?TEXT("SLoop"):TEXT("WLoop")));}break;
    case Stop:Want(Mode==1?(Has(TEXT("Stop_Idle"))?TEXT("Stop_Idle"):TEXT("Idle")):P(TEXT("InsertIdle")));break;
    case Inside:Finish(Mode==2&&Her>=70.f&&!bLockHer?(bStrong?TEXT("sameS"):TEXT("sameW")):TEXT("inside"));break; // with her gauge locked she does not come with him
    case Outside:Finish(TEXT("outside"));break;
    case Drink:++Swallowed;Chain({TEXT("Drink_IN"),TEXT("Drink"),TEXT("Drink_A")});Note(OKT(TEXT("ui.hscene.event_drink")));break;
    case Vomit:Chain({TEXT("Vomit_IN"),TEXT("Vomit"),TEXT("Vomit_A")});AddSiru(0);Note(OKT(TEXT("ui.hscene.event_vomit")));break; // spat out: on her mouth and chin
    case Again:
        // Once more only while she is willing: past her service_rounds for the scene she says no (and the panel stops offering it).
        if(ServiceFinishes+(X.IsValid()&&X->HasField(TEXT("today_service"))?int32(X->GetNumberField(TEXT("today_service"))):0)>=FMath::Max(1,X.IsValid()?int32(X->GetNumberField(TEXT("service_rounds"))):2)){bServiceRefused=true;Note(OKT(TEXT("ui.hscene.again_refused")));BuildList();break;}
        Want(TEXT("WLoop"));bStrong=false;break;
    case Condom:bCondom=!bCondom;Note(OKT(bCondom?TEXT("ui.hscene.condom_on"):TEXT("ui.hscene.condom_off")),false);break;
    case Insert:
        // Raw only when she lets him (NamaInsertCheck): otherwise she has him put one on (the original's voice 302).
        if(!bCondom&&!(X.IsValid()&&X->GetBoolField(TEXT("raw_ok")))){
            if(!bRawAsked){bRawAsked=true;Note(OKT(TEXT("ui.hscene.wants_condom")));break;}
            bRawPressed=true;Note(OKT(TEXT("ui.hscene.raw_after_asking")));}
        bAnalPlay=false;Chain({TEXT("Insert"),TEXT("InsertIdle")});Note(OKT(bCondom?TEXT("ui.hscene.event_insert"):TEXT("ui.hscene.event_insert_raw")),false);break;
    case InsertAnal:{
        // Asked once a scene: her anal experience is the chance she agrees (isAnalInsertOK), more for a lover or a lewd girl.
        // Already said no today (another scene): still no, not asked again.
        if(!bAnalAsked&&X.IsValid()&&X->HasField(TEXT("anal_refused_today"))&&X->GetBoolField(TEXT("anal_refused_today"))){bAnalAsked=true;bAnalRefused=true;}
        if(!bAnalAsked){bAnalAsked=true;float Chance=X.IsValid()?float(X->GetNumberField(TEXT("anal_chance"))):0.f;if(Level()>=3)Chance=1.f;if(Gallery->HHeroineStage==TEXT("lover"))Chance+=.3f;
            const bool Resentful=X.IsValid()&&X->HasField(TEXT("resentful"))&&X->GetBoolField(TEXT("resentful")); // she resents him: no
            bAnalRefused=Resentful||FMath::FRand()>=FMath::Max(Chance,.1f);}
        if(bAnalRefused){Note(OKT(TEXT("ui.hscene.anal_refused")));break;}
        bAnalPlay=true;Chain({TEXT("A_Insert"),TEXT("A_InsertIdle")});Note(OKT(TEXT("ui.hscene.event_insert_anal")),false);break;}
    case Pull:
        if(bInsideFinished)Chain({P(TEXT("Pull")),P(TEXT("Drop")),P(TEXT("OUT_A"))});else Chain({P(TEXT("Pull")),P(TEXT("Idle"))});
        if(Queue.IsEmpty())Want(P(TEXT("Idle")));
        bInsideFinished=false;break;
    case Continue:Want(P(TEXT("InsertIdle")));break;
    default:break;
    }
    ActionsKey.Reset();
}

// Breast service is done bare: through her top and bra his penis went in and out of the cloth. Off with them, as she
// would (only those two; a part already off stays as it is).
void UOKHScenePanel::BareChest(){
    auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;if(!IsValid(Girl))return;
    uint8* S=Gallery->ClothesStates;bool Changed=false;
    for(const int32 C:{0,2})if(S[C]!=3){S[C]=3;Changed=true;}
    if(!Changed)return;
    Girl->SetClothingPartsHidden(S);Gallery->RefreshClothesStateLabels();Note(OKT(TEXT("ui.hscene.event_chest_bared")),false);
}
// Once she has had enough every action is gone; another pose or caress only stood them in it with nothing to press (a
// dead end that did not say leaving is all that is left). The first pose of a scene is still chosen.
bool UOKHScenePanel::RefuseWhenEnough(){
    if(!HadEnough()||(ActivePose==INDEX_NONE&&ActiveAibu==INDEX_NONE))return false;
    if(Hint)Hint->SetText(FText::FromString(OKT(TEXT("ui.hscene.enough_no_more_poses"))));return true;
}
bool UOKHScenePanel::HadEnough() const{
    const auto& X=Gallery?Gallery->HExperience:nullptr;
    const int32 Today=X.IsValid()&&X->HasField(TEXT("today_orgasms"))?int32(X->GetNumberField(TEXT("today_orgasms"))):0; // earlier scenes today
    return Today+HerOrgasms>=FMath::Max(1,X.IsValid()&&X->HasField(TEXT("climax_limit"))?int32(X->GetNumberField(TEXT("climax_limit"))):4);
}
void UOKHScenePanel::CountHerOrgasm(){
    ++LineEpoch;
    ++HerOrgasms;
    if(HadEnough()&&!bSaidEnough){bSaidEnough=true;Note(OKT(TEXT("ui.hscene.she_had_enough")));ActionsKey.Reset();}
}
void UOKHScenePanel::BuildActions(){
    if(!Actions||!WidgetTree)return;
    const FString Cur=Current(),Base=Mode==0?Cur:HBase(Cur);
    const bool Busy=!Queue.IsEmpty()&&!(Queue.Num()==1&&Cur==Queue[0]);
    const FString Key=FString::Printf(TEXT("%d|%s|%d|%d|%d|%d|%d|%d|%d"),Mode,*Cur,Busy,TouchArea,ActiveAibu+100*PendingAibu,His>=70.f,int32(bCondom)+2*int32(bAnalRefused)+4*int32(bLockHer)+8*int32(bLockHis)+16*int32(bServiceRefused)+32*int32(HadEnough()),ActivePose,PendingPose);
    if(Key==ActionsKey)return;ActionsKey=Key;Actions->ClearChildren();Offered.Reset();
    auto Add=[this](int32 Kind,const TCHAR* Label){Offered.Add(Kind);auto* B=WidgetTree->ConstructWidget<UOKHSceneButton>();B->Kind=Kind;B->Owner=this;B->OnClicked.AddDynamic(B,&UOKHSceneButton::HandleClick);auto* L=WidgetTree->ConstructWidget<UTextBlock>();L->SetText(FText::FromString(OKT(Label)));B->SetContent(L);Actions->AddChildToWrapBox(B);};
    if(Mode==0&&ActiveAibu!=INDEX_NONE&&PendingAibu==INDEX_NONE&&!Busy){
        if(!Base.StartsWith(TEXT("Orgasm"))&&!Base.EndsWith(TEXT("Dislikes"))){
            if(!HadEnough()){Add(Kiss,TEXT("ui.hscene.act_kiss"));Add(TouchChest,TEXT("ui.hscene.act_touch_chest"));Add(TouchNipple,TEXT("ui.hscene.act_touch_nipple"));Add(TouchCrotch,TEXT("ui.hscene.act_touch_crotch"));Add(TouchButt,TEXT("ui.hscene.act_touch_butt"));Add(TouchAnal,TEXT("ui.hscene.act_touch_anal"));
            }
            if(TouchArea!=INDEX_NONE)Add(Release,TEXT("ui.hscene.act_release"));
            Add(LockHer,bLockHer?TEXT("ui.hscene.act_unlock_her"):TEXT("ui.hscene.act_lock_her"));}
        return;
    }
    if(Mode==0||ActivePose==INDEX_NONE||PendingPose!=INDEX_NONE||Busy||Cur.IsEmpty())return;
    const FOKHPose& Pose=Poses[ActivePose];
    Add(LockHer,bLockHer?TEXT("ui.hscene.act_unlock_her"):TEXT("ui.hscene.act_lock_her"));Add(LockHis,bLockHis?TEXT("ui.hscene.act_unlock_his"):TEXT("ui.hscene.act_lock_his"));
    if(Mode==1){
        if((Base==TEXT("Idle")||Base==TEXT("Stop_Idle"))&&!bServiceRefused&&!HadEnough())Add(Faster,TEXT("ui.hscene.act_start"));
        else if(HIsLoop(Base)){Add(Faster,TEXT("ui.hscene.act_faster"));Add(Slower,TEXT("ui.hscene.act_slower"));Add(Toggle,TEXT("ui.hscene.act_toggle"));Add(Stop,TEXT("ui.hscene.act_stop"));}
        else if(Base==TEXT("OLoop")){Add(Inside,Pose.KindHoushi==1?TEXT("ui.hscene.act_in_mouth"):Pose.KindHoushi==2?TEXT("ui.hscene.act_on_chest"):TEXT("ui.hscene.act_in_hand"));Add(Outside,TEXT("ui.hscene.act_outside"));}
        else if(Base==TEXT("Oral_Idle")){Add(Drink,TEXT("ui.hscene.act_drink"));Add(Vomit,TEXT("ui.hscene.act_vomit"));}
        else if(Base.EndsWith(TEXT("_A"))){if(!bServiceRefused&&!HadEnough())Add(Again,TEXT("ui.hscene.act_again"));Add(Stop,TEXT("ui.hscene.act_rest"));}
        return;
    }
    const bool Out=Base==TEXT("Idle")||Base==TEXT("OUT_A");
    if(Out&&!HadEnough()){Add(Insert,TEXT("ui.hscene.act_insert"));if(Pose.Anal&&!bAnalRefused)Add(InsertAnal,TEXT("ui.hscene.act_insert_anal"));Add(Condom,bCondom?TEXT("ui.hscene.act_condom_on"):TEXT("ui.hscene.act_condom_off"));}
    else if(Base==TEXT("InsertIdle")){if(!HadEnough())Add(Faster,TEXT("ui.hscene.act_move"));Add(Pull,TEXT("ui.hscene.act_pull"));}
    else if(HIsLoop(Base)){Add(Faster,TEXT("ui.hscene.act_faster"));Add(Slower,TEXT("ui.hscene.act_slower"));Add(Toggle,TEXT("ui.hscene.act_toggle"));Add(Stop,TEXT("ui.hscene.act_stop"));
        if(His>=70.f){Add(Inside,TEXT("ui.hscene.act_inside"));Add(Outside,TEXT("ui.hscene.act_outside"));}}
    else if(Base.EndsWith(TEXT("_A"))){if(!HadEnough())Add(Continue,TEXT("ui.hscene.act_continue"));Add(Pull,TEXT("ui.hscene.act_pull"));}
}

void UOKHScenePanel::NativeTick(const FGeometry& Geometry,float Delta){
    Super::NativeTick(Geometry,Delta);
    if(!Gallery)return;
    if(Poses.IsEmpty()&&!Gallery->NativePairs.IsEmpty()){LoadPoses();BuildList();}
    // She came in already spent today: nothing will be offered, say so at once rather than on the first click.
    if(!bToldSpent&&(ActivePose!=INDEX_NONE||ActiveAibu!=INDEX_NONE)){bToldSpent=true;if(HadEnough()&&Hint)Hint->SetText(FText::FromString(OKT(TEXT("ui.hscene.enough_no_more_poses"))));}
    UpdateFace();UpdateBlush(Delta);FrameCamera();
    // The original writes the liquid every frame; her materials are remade when her clothes or pose parts change.
    if(Siru[0]+Siru[1]+Siru[2]+Siru[3]+Siru[4]>0&&(SiruRefresh-=Delta)<=0.f){SiruRefresh=.5f;ApplySiru();}
    {const int32 Kind=Mode==0?(AibuPoses.IsValidIndex(ActiveAibu)?AibuPoses[ActiveAibu].Prop:0):(Poses.IsValidIndex(ActivePose)?Poses[ActivePose].Prop:0);
     const FString Key=FString::Printf(TEXT("%d|%d|%d"),Mode,ActivePose,ActiveAibu);const double T=FPlatformTime::Seconds();
     // A place change (bed, floor) resets the key too: the same pose, only the props change.
     if(Key!=PropKey){PropKey=Key;ClearProps();PropAt=Kind>0||Room!=0?T+.8:0.;bPlaceOnly=Key==LastPoseKey;LastPoseKey=Key;if(bEntered)Cover(true,T);bEntered=true;}
     else{const bool bPlays=NewPosePlays(T);
         // Props are set on the bones of the new pose: not before it has replaced the old one.
         if(PropAt>0&&T>=PropAt&&bPlays){PropAt=0;PlaceProps(Kind);}
         if(bCovered&&bPlays&&T>=CoverAt+.45&&PropAt==0)Cover(false,T);}
     auto* Playback=Gallery->PairedPlayback.Get();SeenPlayback=Playback;
     SeenClip=Playback?Playback->GetFemaleClip()+Playback->GetFemaleMotion():FString();SeenElapsed=Playback?Playback->GetElapsed():0.f;}
    // A pose counts as done once something happened in it (a gauge rose, he entered her, a touch), not when it only opened:
    // a pair opens on a moving state for a few seconds before its Idle.
    {const FString Cur=Current(),Base=Mode==0?Cur:HBase(Cur);
     if(Mode!=0&&Poses.IsValidIndex(ActivePose)&&(Her>3.f||His>3.f||Base.StartsWith(TEXT("Insert")))){PosesPlayed.AddUnique(Poses[ActivePose].Name);
         // The session counts (her experience, h_session_end) only then too: opening a room and leaving is not an H.
         bPlayed=true;if(Mode==1)bService=true;}
     if(Mode==0&&AibuPoses.IsValidIndex(ActiveAibu)&&TouchArea!=INDEX_NONE)PosesPlayed.AddUnique(AibuPoses[ActiveAibu].Name);}
    if(Mode!=0&&Gallery->PartnerActor&&Gallery->PartnerActor->IsHidden()!=bMaleHidden)Gallery->PartnerActor->SetActorHiddenInGame(bMaleHidden);
    // Only once the pair plays: while his parts and materials are still being prepared, hiding his clothes or swapping his
    // materials keeps that preparation from ever finishing (the room timed out).
    const bool PairPlaying=Gallery->PairedPlayback&&Gallery->PairedPlayback->GetElapsed()>0.f;
    if(Mode!=0&&PairPlaying&&Gallery->PartnerActor&&MaleLookFor!=Gallery->PartnerActor.Get())ApplyMaleLook(); // he was (re)made for the pair
    AlignHim(Delta); // measures while the pair plays; a new pose or mode puts them back
    const double Now=FPlatformTime::Seconds();
    auto* Playback=Gallery->PairedPlayback.Get();
    // The pose actually playing: the one he picked once it starts, or the one the scene opened with.
    const int32 Playing=Gallery->SelectedPairCatalogueIndex();
    if((Mode!=0||PendingPose!=INDEX_NONE)&&Playback&&!Gallery->bPairPreparing&&Gallery->PairStateCombo&&Gallery->PairStateCombo->GetOptionCount()>1){
        const int32 Found=Poses.IndexOfByPredicate([Playing](const FOKHPose& Pose){return Pose.Pair==Playing;});
        if(Found!=INDEX_NONE&&(Found!=ActivePose||PendingPose==Found)){
            ActivePose=Found;PendingPose=INDEX_NONE;Mode=Poses[Found].Mode;ClearLine();Queue.Reset();Wanted.Reset();bAnalPlay=false;bInsideFinished=false;bStrong=false;
            if(Has(TEXT("Idle")))Want(TEXT("Idle"));
            if(Mode==1&&Poses[Found].KindHoushi==2)BareChest();
            Tab=Mode==2?TabSonyu:TabHand+Poses[Found].KindHoushi;BuildList();if(Hint)Hint->SetText(FText::FromString(Mode==1&&bServiceRefused?OKT(TEXT("ui.hscene.again_refused")):Poses[Found].Name)); // the new pose (or why she will not start it), not the last one's event
        }
    }
    const FString Cur=Current(),Base=Mode==0?Cur:HBase(Cur);
    if(Cur!=LastState){
        const FString Before=HBase(LastState);
        LastState=Cur;StateSeen=Now;
        // Inserted once the insertion has played (SetInsertKokan / SetInsertAnal): her first time is then.
        if(Base==TEXT("InsertIdle")&&Before==TEXT("Insert")){
            const bool Virgin=Gallery->HExperience.IsValid()&&Gallery->HExperience->GetBoolField(Cur.StartsWith(TEXT("A_"))?TEXT("anal_virgin"):TEXT("virgin"));
            const bool First=Cur.StartsWith(TEXT("A_"))?!bAnal:!bCrotch;
            if(Cur.StartsWith(TEXT("A_")))bAnal=true;else{bCrotch=true;if(!bCondom)bRaw=true;}
            // What she agreed to goes with it (the reaction keeps only the latest note): she said yes to his asking.
            if(First){FString Event=OKT(Virgin?(Cur.StartsWith(TEXT("A_"))?TEXT("ui.hscene.react_first_anal"):TEXT("ui.hscene.react_virgin")):Cur.StartsWith(TEXT("A_"))?TEXT("ui.hscene.event_insert_anal"):bCondom?TEXT("ui.hscene.event_insert"):TEXT("ui.hscene.event_insert_raw"));
                if(Cur.StartsWith(TEXT("A_")))Event+=OKT(TEXT("ui.hscene.react_agreed_anal"));else if(bRawPressed&&!bCondom)Event+=OKT(TEXT("ui.hscene.react_agreed_raw"));
                else if(!bCondom)Event+=OKT(TEXT("ui.hscene.react_raw_fine")); // raw_ok: she lets him without being asked twice
                React(Event);}
        }
        if(Base==TEXT("Oral_Idle"))OralIdleSince=Now;
    }
    // The queued flow: a one-shot state moves on when it has played once; a finish's own loop after two rounds.
    if(!Queue.IsEmpty()&&(Playback||Mode==0)){
        if(Cur==Queue[0]){
            if(Queue.Num()==1)Queue.Reset();
            else{
                const float Elapsed=Mode==0?float(Now-SoloStarted):Playback->GetElapsed(),Length=FMath::Max(.2f,Gallery->PreviewActor?Gallery->PreviewActor->AnimDuration:1.f);
                const bool LoopLike=Base.EndsWith(TEXT("_Loop"))||Base==TEXT("OLoop")||Base==TEXT("Oral_Idle_IN");
                if(Elapsed>=(LoopLike?FMath::Max(Length*2.f,2.5f):Length*.98f)){Queue.RemoveAt(0);Want(Queue[0]);}
            }
        }else if(Wanted!=Queue[0])Want(Queue[0]);
    }
    if(!Wanted.IsEmpty()&&Cur==Wanted)Wanted.Reset();
    if(Mode==0&&!Wanted.IsEmpty()){PlaySolo(Wanted);Wanted.Reset();}
    if(!Wanted.IsEmpty()&&Playback&&!Gallery->bPairPreparing&&Now-LastRequest>.75&&Gallery->PairStateCombo){
        LastRequest=Now;
        if(Gallery->PairStateCombo->GetSelectedOption()!=Wanted)Gallery->PairStateCombo->SetSelectedOption(Wanted);
        else Gallery->OnPairStateSelected(Wanted,ESelectInfo::Direct);
    }
    // Gauges (HFlag gaugeFemale / gaugeMale): they rise with the movement, faster when strong; hers by her experience.
    if(Speed)SpeedValue=Speed->GetValue();
    const float Rate=SpeedValue,Strong=Base==TEXT("SLoop")?1.25f:1.f;
    static const float HerByLevel[]={.55f,.8f,1.f,1.35f};
    // Caress (HAibu): resting on a place she is being touched; her gauge by how used to it she is, and too hard on a
    // place she has never been touched there makes her pull away (JudgeDislikesis70).
    if(Mode==0&&Queue.IsEmpty()&&TouchArea!=INDEX_NONE&&(Base.EndsWith(TEXT("_Idle"))||Base==TEXT("K_Loop"))){
        bPlayed=true;AreaSeconds[TouchArea]+=Delta;
        const float Used=FMath::Clamp(AreaExperience(TouchArea)/100.f,0.f,1.f);
        Her+=Delta*(1.5f+5.f*Rate)*(.6f+.6f*Used)*HerByLevel[FMath::Clamp(Level(),0,3)];if(bLockHer)Her=FMath::Min(Her,95.f);
        if(Rate>=.7f&&Used<=0.f&&TouchArea!=0){Note(OKT(TEXT("ui.hscene.too_hard")));Chain({TouchArea>=3&&TouchArea<=4?TEXT("Back_Dislikes"):TEXT("Front_Dislikes"),TEXT("Idle")});TouchArea=INDEX_NONE;}
        else if(Her>=100.f){Her=0.f;TouchArea=INDEX_NONE;ReleaseHeld();Chain({TEXT("Orgasm_Start"),TEXT("Orgasm_Loop"),TEXT("Orgasm_A"),TEXT("Idle")});Note(OKT(TEXT("ui.hscene.event_her_orgasm")));CountHerOrgasm();}
    }else if(Queue.IsEmpty()&&HIsLoop(Base)){
        if(Mode==1){His+=Delta*(2.5f+8.f*Rate)*Strong;if(bLockHis)His=FMath::Min(His,69.f);Her=FMath::Min(69.f,Her+Delta*.8f);if(His>=70.f&&Wanted.IsEmpty())Want(TEXT("OLoop"));}
        else{His+=Delta*(1.2f+3.5f*Rate)*Strong;Her+=Delta*(1.5f+6.f*Rate)*Strong*HerByLevel[FMath::Clamp(Level(),0,3)];
            if(bLockHer)Her=FMath::Min(Her,95.f);if(bLockHis)His=FMath::Min(His,95.f);
            if(Her>=100.f)Finish(His>=70.f?(Strong>1.f?TEXT("sameS"):TEXT("sameW")):(Strong>1.f?TEXT("orgS"):TEXT("orgW")));
            else if(His>=100.f)Finish(!bCondom?TEXT("outside"):Her>=70.f&&!bLockHer?(Strong>1.f?TEXT("sameS"):TEXT("sameW")):TEXT("inside"));}
    }else if(Queue.IsEmpty()&&Base==TEXT("OLoop")&&Mode==1){His=FMath::Min(bLockHis?95.f:100.f,His+Delta*3.f);if(His>=100.f)Finish(Poses.IsValidIndex(ActivePose)&&Poses[ActivePose].KindHoushi==1?TEXT("inside"):TEXT("outside"));}
    else if(Queue.IsEmpty()){Her=FMath::Max(0.f,Her-Delta*3.f);His=FMath::Max(0.f,His-Delta*3.f);}
    Her=FMath::Clamp(Her,0.f,100.f);His=FMath::Clamp(His,0.f,100.f);
    // In her mouth she decides herself if he waits (HHoushi Oral_Idle: drink or spit after a while).
    if(Queue.IsEmpty()&&Base==TEXT("Oral_Idle")&&OralIdleSince>0.&&Now-OralIdleSince>8.){OralIdleSince=0.;static const float DrinkChance[]={.2f,.35f,.6f,.85f};Click(FMath::FRand()<DrinkChance[FMath::Clamp(Level(),0,3)]?Drink:Vomit);} // her own choice: the more used to it, the likelier she swallows
    ApplySpeed();
    // Her spoken answer is over: her breathing goes on (the line stopped it).
    if(bTalkSpoken&&Gallery->PreviewActor&&!Gallery->PreviewActor->IsSpeaking()){bTalkSpoken=false;if(Mode!=0&&Gallery->PairedPlayback)Gallery->StartPairBreath();}
    if(HerGauge)HerGauge->SetPercent(Her/100.f);if(HisGauge)HisGauge->SetPercent(His/100.f);
    if(Title){FString Name=Gallery->HHeroineName;const FString LevelName=Gallery->HExperience.IsValid()?Gallery->HExperience->GetStringField(TEXT("level_name")):FString();
        Title->SetText(FText::FromString(LevelName.IsEmpty()?Name:FString::Printf(TEXT("%s · %s"),*Name,*LevelName)));}
    if(Status){
        const FString PoseName=Mode==0&&AibuPoses.IsValidIndex(ActiveAibu)?AibuPoses[ActiveAibu].Name:Poses.IsValidIndex(PendingPose)?Poses[PendingPose].Name+OKT(TEXT("ui.hscene.loading")):Poses.IsValidIndex(ActivePose)?Poses[ActivePose].Name:FString(OKT(TEXT("ui.hscene.pick_pose")));
        Status->SetText(FText::FromString(Cur.IsEmpty()?PoseName:FString::Printf(TEXT("%s · %s · ×%.1f%s"),*PoseName,*StateWords(Mode==0?Cur:HBase(Cur)),PlaybackSpeed(),Mode==2?(bCondom?OKT(TEXT("ui.hscene.condom_mark")):TEXT("")):TEXT(""))));
    }
    BuildActions();
}

bool UOKHScenePanel::CannotAnswer() const{
    const FString Cur=Current(),Base=Mode==0?Cur:HBase(Cur);
    const bool Moving=Mode==0?TouchArea!=INDEX_NONE:HIsLoop(Base);
    return Moving&&Queue.IsEmpty()&&PlaybackSpeed()>2.f;
}
void UOKHScenePanel::Say(const FString& Raw){
    const FString Text=Raw.TrimStartAndEnd();if(Text.IsEmpty())return;
    if(Gallery)Gallery->NoteAction(TEXT("said"),Text);
    if(CannotAnswer()){Held.Add(Text);if(Hint)Hint->SetText(FText::FromString(OKT(TEXT("ui.hscene.cannot_answer"))));return;}
    Outbox.Add({Text,FString()});
}
void UOKHScenePanel::ReleaseHeld(){
    if(Held.IsEmpty())return;
    Outbox.Add({FString::Join(Held,TEXT(" ")),OKT(TEXT("ui.hscene.held_note"))});Held.Reset();
}
bool UOKHScenePanel::TakeLine(FString& Text,FString& Out,bool& System){
    if(Outbox.IsEmpty())return false;
    Text=Outbox[0].Text;System=Outbox[0].System;Out=Context();SentEpoch=LineEpoch;if(!Outbox[0].Note.IsEmpty())Out+=TEXT("\n")+Outbox[0].Note;Outbox.RemoveAt(0);
    if(!System&&Caption&&Gallery)Caption->SetText(FText::FromString(Gallery->HPlayerName+TEXT("：")+Text));
    return true;
}
void UOKHScenePanel::React(const FString& Event){
    if(Event.IsEmpty()||CannotAnswer())return;
    Outbox.RemoveAll([](const FHLine& L){return L.System;});
    Outbox.Add({OKMsg(TEXT("ui.hscene.react_note"),{{TEXT("event"),Event}}),FString(),true});
}
// Her original breathing for the caress (HVoiceCtrl breath, mode 0): moving while his hands are on her.
void UOKHScenePanel::CaressBreath(){
    if(!Gallery||!Gallery->PreviewActor||Mode!=0||!AibuPoses.IsValidIndex(ActiveAibu)||Gallery->PreviewActor->IsSpeaking()&&!Gallery->PreviewActor->bHBreath)return;
    TArray<FKKHBreathCue> Cues;FString Error;const int32 Personality=Gallery->Selected.Personality;
    if(KKSpeech::CollectHBreath(UOKCharacterController::DefaultVoiceRoot(),Personality,0,SoloState,AibuPoses[ActiveAibu].Id,TouchArea!=INDEX_NONE,Cues,Error)&&Cues.Num())
    {const bool Ok=Gallery->PreviewActor->BeginHBreath(Cues,FMath::Lerp(.94f,1.06f,Gallery->Selected.VoiceRate),Error);
        UE_LOG(LogTemp,Display,TEXT("H scene: caress breath %s cues=%d %s"),*SoloState,Cues.Num(),Ok?TEXT("playing"):*Error);}
    else UE_LOG(LogTemp,Display,TEXT("H scene: no caress breath for %s: %s"),*SoloState,*Error);
}
bool UOKHScenePanel::Hear(const FString& Raw,FString& Out){
    const FString Text=Raw.TrimStartAndEnd();if(Text.IsEmpty())return false;
    if(Gallery)Gallery->NoteAction(TEXT("said"),Text);
    if(CannotAnswer()){Held.Add(Text);if(Hint)Hint->SetText(FText::FromString(OKT(TEXT("ui.hscene.cannot_answer"))));return false;}
    Out=Context();SentEpoch=LineEpoch;if(Caption&&Gallery)Caption->SetText(FText::FromString(Gallery->HPlayerName+TEXT("：")+Text));return true;
}
void UOKHScenePanel::ShowLine(const FString& Text){
    bStaleLine=SentEpoch!=LineEpoch;if(bStaleLine){UE_LOG(LogTemp,Display,TEXT("H scene: her line from before the last climax / finish / pose not shown: %s"),*Text.Left(60));return;}
    if(Caption)Caption->SetText(FText::FromString(Text));
}
// What his words in the scene asked for (the street's H judge, ui.scene.h_judge_prompt): the pose and what is going on
// now, and every pose and caress by name.
FString UOKHScenePanel::JudgeFacts() const{
    const FString Cur=Current(),Base=Mode==0?Cur:HBase(Cur);
    const FString PoseName=Mode==0&&AibuPoses.IsValidIndex(ActiveAibu)?AibuPoses[ActiveAibu].Name:Poses.IsValidIndex(ActivePose)?Poses[ActivePose].Name:FString();
    TArray<FString> Names;for(const FOKHPose& P:Poses)Names.AddUnique(P.Name);for(const auto& A:AibuPoses)Names.AddUnique(A.Name);
    return OKMsg(TEXT("ui.hscene.judge_facts"),{{TEXT("pose"),PoseName},{TEXT("state"),Cur.IsEmpty()?FString():StateWords(Base)},{TEXT("poses"),FString::Join(Names,TEXT("、"))}});
}
// His words do what the buttons do, and only what they offer now: "stop" stops (whatever she would like), "pull out",
// "faster" / "slower", where he finishes; another pose only when she agreed in her reply. Ending the scene is the street's.
bool UOKHScenePanel::ApplyTalk(const TSharedPtr<FJsonObject>& V){
    if(!V.IsValid()||!Gallery)return false;
    bool bStop=false,bPull=false,bEnd=false,bAgrees=true;FString Pose,Pace,Where,Why;
    V->TryGetBoolField(TEXT("stop"),bStop);V->TryGetBoolField(TEXT("pull"),bPull);V->TryGetBoolField(TEXT("end"),bEnd);V->TryGetBoolField(TEXT("she_agrees"),bAgrees);
    V->TryGetStringField(TEXT("pose"),Pose);V->TryGetStringField(TEXT("speed"),Pace);V->TryGetStringField(TEXT("finish"),Where);V->TryGetStringField(TEXT("why"),Why);
    UE_LOG(LogTemp,Display,TEXT("H talk judge: stop=%d pull=%d end=%d pose=\"%s\" agrees=%d speed=\"%s\" finish=\"%s\"; %s"),bStop,bPull,bEnd,*Pose,bAgrees,*Pace,*Where,*Why);
    if(bEnd)return true;
    ActionsKey.Reset();BuildActions();
    if(bStop||bPull){
        if(Mode==0){if(TouchArea!=INDEX_NONE)Click(Release);}
        else{
            const FString Base=HBase(Current());
            if(Offered.Contains(Stop))Click(Stop);
            // Close to his climax nothing is offered but finishing: stopping is still his to do.
            else if(HIsLoop(Base)||Base==TEXT("OLoop")){Queue.Reset();Want(Mode==1?(Has(TEXT("Stop_Idle"))?TEXT("Stop_Idle"):TEXT("Idle")):P(TEXT("InsertIdle")));}
            // Pulling out while moving: stop, then out (the stop's InsertIdle has not played yet, so Pull is not offered).
            if(bPull){ActionsKey.Reset();BuildActions();if(Offered.Contains(Pull))Click(Pull);
                else if(Mode==2&&Has(P(TEXT("Pull")))){Chain({P(TEXT("InsertIdle")),P(TEXT("Pull")),P(TEXT("Idle"))});bInsideFinished=false;}}
        }
        return false;
    }
    if(!Pose.IsEmpty()&&bAgrees){
        const int32 A=AibuPoses.IndexOfByPredicate([&](const auto& X){return X.Name==Pose;});
        if(A!=INDEX_NONE){Tab=TabAibu;SelectAibu(A);}else if(!SelectPoseNamed(Pose)){UE_LOG(LogTemp,Display,TEXT("H talk judge: no pose named %s"),*Pose);}
        return false;
    }
    if(Pace==TEXT("faster")&&Offered.Contains(Faster))Click(Faster);
    else if(Pace==TEXT("slower")&&Offered.Contains(Slower))Click(Slower);
    if(Where==TEXT("inside")&&Offered.Contains(Inside))Click(Inside);
    else if(Where==TEXT("outside")&&Offered.Contains(Outside))Click(Outside);
    return false;
}
// A new pose or caress: what she said to the last one is no longer what is on the screen (her next line replaces nothing stale).
void UOKHScenePanel::ClearLine(){++LineEpoch;if(Caption)Caption->SetText(FText::GetEmpty());}
// Standard face patterns (eye_face.f00_*, mayuge.mayu00_*): eyes 1 closed, 4 smile, 7 setunai, 8 tere, 12 kurusi,
// 13 keno, 24 gyu (squeezed shut); brows 0 default, 1 oko, 2 koma, 11 huan.
void UOKHScenePanel::UpdateFace(){
    auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;if(!IsValid(Girl)||Girl->GetFacePatternCount(1)<25||Girl->GetFacePatternCount(2)<12)return;
    const FString Cur=Current(),Base=Mode==0?Cur:HBase(Cur);const int32 L=Level();
    int32 Eye=8,Brow=2;float Open=.85f;
    if(Cur.Contains(TEXT("Dislikes"))){Eye=13;Brow=1;Open=.9f;}
    else if(Mode==0&&(Base==TEXT("K_Loop")||Base==TEXT("K_Touch"))){Eye=1;Brow=2;Open=0.f;}
    else if(Base.StartsWith(TEXT("Orgasm"))||(Mode!=0&&!HIsLoop(Base)&&StateWords(Base)==OKT(TEXT("ui.hscene.state_finishing")))){Eye=24;Brow=2;Open=0.f;}
    else if(StateWords(Base)==OKT(TEXT("ui.hscene.state_after"))){Eye=7;Brow=2;Open=.45f;}
    else if(Base==TEXT("OLoop")||Her>=70.f){Eye=12;Brow=11;Open=.5f;}
    else if(Base==TEXT("SLoop")||Base==TEXT("WLoop")||(Mode==0&&TouchArea!=INDEX_NONE)){Eye=L>=3?8:7;Brow=L<=1?11:2;Open=Base==TEXT("SLoop")?.55f:.7f;}
    else if(Base==TEXT("Insert")||Base==TEXT("InsertIdle")){Eye=L<=1?12:8;Brow=L<=1?11:2;Open=.7f;}
    else if(HadEnough()){Eye=13;Brow=2;Open=.6f;} // she has had enough: tired of it, not smiling
    else if(L>=2){Eye=4;Brow=0;Open=.9f;}
    const FString Key=FString::Printf(TEXT("%d/%d/%.2f"),Eye,Brow,Open);if(Key==FaceKey)return;FaceKey=Key;
    Girl->SetFaceControl(1,Eye,Open,true);Girl->SetFaceControl(2,Brow,1.f,true);
}
void UOKHScenePanel::UpdateBlush(float Delta){
    auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;if(!IsValid(Girl))return;
    static const FName Alpha(TEXT("_overcolor2__A")),Color(TEXT("_overcolor2"));
    if(BlushFor!=Girl){RestoreBlush();BlushFor=Girl;Blush=BlushShown=-1.f;
        TInlineComponentArray<UMeshComponent*> Comps;Girl->GetComponents(Comps);
        for(UMeshComponent* Comp:Comps)for(const FName& Tag:Comp->ComponentTags){const FString T=Tag.ToString();if(!T.StartsWith(TEXT("KKSessionPart_")))continue;
            OkMeshView V{};if(!Girl->Session.PartAt(FCString::Atoi(*T.Mid(14)),V)||!V.name||FString(UTF8_TO_TCHAR(V.name)).ToLower()!=TEXT("cf_o_face"))break;
            for(int32 I=0;I<Comp->GetNumMaterials();++I)if(auto* M=Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(I))){float A=0.f;
                if(M->GetScalarParameterValue(FHashedMaterialParameterInfo(Alpha),A)){BlushMats.Add(M);BlushBase.Add(A);}}
            break;}
        if(BlushMats.IsEmpty()&&!bBlushLogged){bBlushLogged=true;UE_LOG(LogTemp,Warning,TEXT("H scene: her face has no live blush parameter (_overcolor2__A); her cheeks stay as her card has them"));}
    }
    if(BlushMats.IsEmpty())return;
    const FString Base=Mode==0?Current():HBase(Current());
    // Her gauge; flushed through the afterglow, and once she has come a little stays for the rest of the scene.
    float Target=Base.StartsWith(TEXT("Orgasm"))?1.f:FMath::Clamp(Her/100.f,0.f,1.f);
    if(Mode!=0&&StateWords(Base)==OKT(TEXT("ui.hscene.state_after")))Target=FMath::Max(Target,.7f);
    if(HerOrgasms>0)Target=FMath::Max(Target,.35f);
    // Up in a few seconds, down over twenty: the flush outlasts the moment.
    Blush=BlushTest>=0.f?BlushTest:Blush<0.f?Target:Target>Blush?FMath::Min(Target,Blush+Delta*.5f):FMath::Max(Target,Blush-Delta*.05f);
    const float Shown=FMath::RoundToFloat(Blush*40.f)/40.f;if(Shown==BlushShown)return;BlushShown=Shown;
    for(int32 I=0;I<BlushMats.Num();++I)if(IsValid(BlushMats[I])){const float A=FMath::Lerp(BlushBase[I],FMath::Max(BlushBase[I],.2f),Shown);
        BlushMats[I]->SetScalarParameterValue(Alpha,A);FLinearColor C;if(BlushMats[I]->GetVectorParameterValue(FHashedMaterialParameterInfo(Color),C)){C.A=A;BlushMats[I]->SetVectorParameterValue(Color,C);}}
}
void UOKHScenePanel::RestoreBlush(){
    for(int32 I=0;I<BlushMats.Num();++I)if(IsValid(BlushMats[I])){BlushMats[I]->SetScalarParameterValue(TEXT("_overcolor2__A"),BlushBase[I]);
        FLinearColor C;if(BlushMats[I]->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("_overcolor2")),C)){C.A=BlushBase[I];BlushMats[I]->SetVectorParameterValue(TEXT("_overcolor2"),C);}}
    BlushMats.Reset();BlushBase.Reset();BlushFor=nullptr;
}
bool UOKHScenePanel::NewPosePlays(double Now) const{
    if(Now>CoverAt+3.)return true; // never held black: a pose that does not start shows what there is
    if(Mode==0||bPlaceOnly)return Now>CoverAt+.2;
    auto* Playback=Gallery?Gallery->PairedPlayback.Get():nullptr;if(!Playback)return false;
    // The new pose is committed: another player, its clip or motion changed, or its clock started again. A place change
    // keeps the pose: its props are what changes.
    return Playback!=CoverPlayback.Get()||Playback->GetFemaleClip()+Playback->GetFemaleMotion()!=CoverClip||Playback->GetElapsed()<CoverElapsed-.01f;
}
void UOKHScenePanel::Cover(bool bOn,double Now){
    APlayerCameraManager* Camera=GetOwningPlayer()?GetOwningPlayer()->PlayerCameraManager.Get():nullptr;
    if(bOn){CoverAt=Now;CoverPlayback=SeenPlayback;CoverElapsed=SeenElapsed;CoverClip=SeenClip;}
    else if(bCovered)UE_LOG(LogTemp,Display,TEXT("H scene: new pose shown %.2fs after the change"),Now-CoverAt);
    if(!Camera){bCovered=false;return;}
    bCovered=bOn;
    if(bOn)Camera->StartCameraFade(1.f,1.f,0.f,FLinearColor::Black,false,true); // black this very frame: the jump is never seen
    else Camera->StartCameraFade(1.f,0.f,.35f,FLinearColor::Black,false,false);
}
static bool HBone(AKKPreviewActor* A,const TCHAR* Name,FVector& Out);
void UOKHScenePanel::AddSiru(int32 Area){
    if(Area<0||Area>4)return;Siru[Area]=FMath::Min(2,Siru[Area]+1);ApplySiru();
    UE_LOG(LogTemp,Display,TEXT("H scene: semen on her %d (levels %d %d %d %d %d)"),Area,Siru[0],Siru[1],Siru[2],Siru[3],Siru[4]);
}
void UOKHScenePanel::ApplySiru(){
    auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;if(!IsValid(Girl))return;
    static const FName Names[5]={TEXT("_liquidface"),TEXT("_liquidftop"),TEXT("_liquidfbot"),TEXT("_liquidbtop"),TEXT("_liquidbbot")};
    TInlineComponentArray<UMeshComponent*> Comps;Girl->GetComponents(Comps);
    for(UMeshComponent* Comp:Comps)for(int32 I=0;I<Comp->GetNumMaterials();++I)if(auto* M=Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(I)))
        for(int32 A=0;A<5;++A){float Was=0.f;if(M->GetScalarParameterValue(FHashedMaterialParameterInfo(Names[A]),Was)&&Was!=float(Siru[A]))M->SetScalarParameterValue(Names[A],float(Siru[A]));}
}
void UOKHScenePanel::SetSiruTest(const int32 (&Levels)[5]){for(int32 A=0;A<5;++A)Siru[A]=FMath::Clamp(Levels[A],0,2);ApplySiru();}
TArray<FString> UOKHScenePanel::LiquidReport() const{
    TArray<FString> Out;auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;if(!IsValid(Girl))return Out;
    TInlineComponentArray<UMeshComponent*> Comps;Girl->GetComponents(Comps);
    auto Name=[](UMaterialInstanceDynamic* M,const TCHAR* P){UTexture* T=nullptr;return M->GetTextureParameterValue(FHashedMaterialParameterInfo(P),T)&&T?FString::Printf(TEXT("%s(%dx%d)"),*T->GetName(),int32(T->GetSurfaceWidth()),int32(T->GetSurfaceHeight())):FString(TEXT("-"));};
    // Which of her session's parts carry the liquid textures at all (the renderer can only bind what the open made).
    for(int32 P=0;P<Girl->Session.PartCount();++P){OkMeshView V{};const uint8* Bytes=nullptr;int32 Length=0;
        if(Girl->Session.PartAt(P,V)&&Girl->Session.ShaderTexture(P,"_liquidmask",Bytes,Length))
            Out.Add(FString::Printf(TEXT("session part %d %s/%s liquidmask=%d"),P,UTF8_TO_TCHAR(V.role?V.role:""),UTF8_TO_TCHAR(V.name?V.name:""),Length));}
    for(UMeshComponent* Comp:Comps)for(int32 I=0;I<Comp->GetNumMaterials();++I)if(auto* M=Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(I))){
        float Top=0.f;if(!M->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("_liquidftop")),Top))continue;
        Out.Add(FString::Printf(TEXT("%s#%d %s ftop=%g mask=%s tex2=%s tex3=%s"),*Comp->GetName(),I,M->Parent?*M->Parent->GetName():TEXT("?"),Top,*Name(M,TEXT("_liquidmask")),*Name(M,TEXT("_Texture2")),*Name(M,TEXT("_Texture3"))));}
    return Out;
}
int32 UOKHScenePanel::SiruSide() const{
    // 0 when he is on her front (her face turns toward him), 1 when he is behind her.
    auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;auto* Him=Gallery?Gallery->PartnerActor.Get():nullptr;
    FVector Nose,Head,HerHips,HisHips;
    if(!HBone(Girl,TEXT("J_Nose_tip"),Nose)||!HBone(Girl,TEXT("j_head"),Head)||!HBone(Girl,TEXT("j_hips"),HerHips)||!HBone(Him,TEXT("j_hips"),HisHips))return 0;
    return ((Nose-Head)|(HisHips-HerHips))>=0.?0:1;
}
// A bone of theirs in the world (the male skeleton may name its bones like hers).
static bool HBone(AKKPreviewActor* A,const TCHAR* Name,FVector& Out);
void UOKHScenePanel::LookAtCrotch(float Distance,float Pitch,float Turn){
    // Where his penis is in her: her crotch from beside her body (across the line from her hips to her head), a little above.
    auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;FVector At,Head,Hips;
    if(!IsValid(Girl)||!Girl->StudioCam||!HBone(Girl,TEXT("J_Kokan"),At)||!HBone(Girl,TEXT("j_head"),Head)||!HBone(Girl,TEXT("j_hips"),Hips))return;
    FVector Body=Head-Hips;Body.Z=0;FVector Side=FVector::CrossProduct(Body.IsNearlyZero()?FVector::ForwardVector:Body.GetSafeNormal(),FVector::UpVector);if(Side.IsNearlyZero())Side=FVector::RightVector;
    Girl->OrbitFocus=At;Girl->OrbitYaw=FMath::RadiansToDegrees(FMath::Atan2(Side.X,Side.Y))+Turn;Girl->OrbitPitch=FMath::Clamp(Pitch,-80.f,85.f);
    Girl->OrbitDistance=FMath::Clamp(Distance,15.f,650.f);Girl->ApplyOrbitCamera();
}
void UOKHScenePanel::LookAtFace(float Distance){
    // Where her nose points from her head: the camera that way, at her face's height and tilt (down at him, up at the ceiling).
    auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;FVector Head,Nose;
    if(!IsValid(Girl)||!Girl->StudioCam||!HBone(Girl,TEXT("j_head"),Head)||!HBone(Girl,TEXT("J_Nose_tip"),Nose))return;
    FVector Dir=(Nose-Head).GetSafeNormal();if(Dir.IsNearlyZero())return;
    Girl->OrbitFocus=Nose;Girl->OrbitYaw=FMath::RadiansToDegrees(FMath::Atan2(Dir.X,Dir.Y));Girl->OrbitPitch=FMath::Clamp(FMath::RadiansToDegrees(FMath::Asin(Dir.Z)),-70.f,70.f);
    Girl->OrbitDistance=FMath::Clamp(Distance,15.f,650.f);Girl->ApplyOrbitCamera();
}
static bool HBone(AKKPreviewActor* A,const TCHAR* Name,FVector& Out){
    if(!IsValid(A)||!A->BodySkel)return false;
    for(const TCHAR* P:{TEXT("cf_"),TEXT("cm_")}){const FName N(FString(P)+Name);if(A->BodySkel->GetBoneIndex(N)!=INDEX_NONE){Out=A->BodySkel->GetBoneLocation(N);return true;}}
    return false;
}
// Which way her chest faces now, from the shoulders (left to right, crossed with up), while her upper body is upright:
// in a pair pose the actor's rotation is the pose's origin, not her body, and side-by-side poses were shot from behind.
static bool HChestForward(AKKPreviewActor* A,FVector& Out){
    FVector L,R,Head,Hips;
    if(!HBone(A,TEXT("j_shoulder_L"),L)||!HBone(A,TEXT("j_shoulder_R"),R)||!HBone(A,TEXT("j_head"),Head)||!HBone(A,TEXT("j_hips"),Hips))return false;
    const FVector Body=Head-Hips;if(FMath::Abs(float(Body.Z))<Body.Size2D()*1.2f)return false; // lying or on all fours: no upright chest
    // Her face says it best when the face rig is on this skeleton: head to the tip of her nose.
    FVector Nose;const FName NoseBone=A->BodySkel->GetBoneIndex(TEXT("cf_J_Nose_tip"))!=INDEX_NONE?FName(TEXT("cf_J_Nose_tip")):FName(TEXT("cm_J_Nose_tip"));
    if(A->BodySkel->GetBoneIndex(NoseBone)!=INDEX_NONE){Nose=A->BodySkel->GetBoneLocation(NoseBone);Out=Nose-Head;Out.Z=0;
        if(Out.Normalize()){UE_LOG(LogTemp,Display,TEXT("H camera: her front from her nose (%.2f,%.2f)"),Out.X,Out.Y);return true;}}
    // The converted skeleton is mirrored (its _L shoulder is on her right in UE): up × (R − L) is her front.
    FVector Right=R-L;Right.Z=0;Out=FVector::CrossProduct(FVector::UpVector,Right);Out.Z=0;
    UE_LOG(LogTemp,Display,TEXT("H camera: her front from her shoulders (%.2f,%.2f)"),Out.X,Out.Y);return Out.Normalize();
}
// Where their body is now, from the bones (the component bounds can still be the standing pose's after a sit or a lie).
static FBox HBodyBox(AKKPreviewActor* A){
    FBox Box(ForceInit);FVector P;
    for(const TCHAR* N:{TEXT("j_hips"),TEXT("j_spine02"),TEXT("j_head"),TEXT("j_hand_L"),TEXT("j_hand_R"),TEXT("j_foot_L"),TEXT("j_foot_R"),TEXT("j_leg01_L"),TEXT("j_leg01_R")})if(HBone(A,N,P))Box+=P;
    if(!Box.IsValid)return IsValid(A)?A->GetComponentsBoundingBox(true):Box;
    Box=Box.ExpandBy(15.f);Box.Max.Z+=12.f;return Box; // the flesh around the bones, the top of the head
}
void UOKHScenePanel::FrameCamera(){
    auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;if(!IsValid(Girl)||!Girl->StudioCam)return;
    const FString Key=FString::Printf(TEXT("%d|%d|%d"),Mode,ActivePose,ActiveAibu);const double Now=FPlatformTime::Seconds();
    if(Key!=FramedKey){FramedKey=Key;FrameAt=Now+.8;return;} // after the pose has placed them
    if(FrameAt<=0||Now<FrameAt)return;FrameAt=0;
    const FBox HerBox=HBodyBox(Girl);if(!HerBox.IsValid)return;
    FVector Facing=Girl->GetActorRotation().RotateVector(FVector(0,1,0));Facing.Z=0;Facing=Facing.GetSafeNormal();
    FVector Focus=HerBox.GetCenter(),Dir=Facing;float Size=HerBox.GetExtent().Size();
    auto* Him=Gallery->PartnerActor.Get();bool bOverlap=false; // one on top of the other: no side between them to shoot from
    if(Mode!=0&&IsValid(Him)&&!Him->IsHidden()){
        const FBox HisBox=HBodyBox(Him);
        if(HisBox.IsValid){
            FVector Line=HisBox.GetCenter()-HerBox.GetCenter();Line.Z=0;bOverlap=Line.Size()<=25.f;
            // Both of their fronts: the side that sees the two faces (beside him, she faces him and he faces ahead).
            // Face to face they cancel out: then her front alone.
            FVector Chest,HisChest;if(HChestForward(Girl,Chest)){Facing=Chest;
                if(HChestForward(Him,HisChest)&&(Chest+HisChest).Size2D()>.5f)Facing=(Chest+HisChest).GetSafeNormal2D();}
            if(Line.Size()>5.f){Line.Normalize();FVector Side(-Line.Y,Line.X,0);if((Side|Facing)<0)Side=-Side;const FVector Across=Facing-Line*float(Facing|Line); // her front across the line between them, not toward him
                Dir=(Side+.45f*Across-.25f*Line).GetSafeNormal();
                // From the side where his body is least in the way of her face (an arch, a pose seen across his back).
                FVector Face,B;TArray<FVector> HisBody;for(const TCHAR* N:{TEXT("j_head"),TEXT("j_spine02"),TEXT("j_hips")})if(HBone(Him,N,B))HisBody.Add(B);
                if(HBone(Girl,TEXT("j_head"),Face)&&HisBody.Num()){
                    const float Dist=FMath::Clamp(Size*2.5f,120.f,650.f);
                    auto Clear=[&](const FVector& D){const FVector Eye=Focus+D*Dist+FVector(0,0,Dist*.32f);float Near=TNumericLimits<float>::Max();
                        // Only what is between the eye and her face blocks it: his head beside hers (close to her end) does not.
                        const FVector Ray=Face-Eye;for(const FVector& V:HisBody){const float T=float((V-Eye)|Ray)/FMath::Max(1.f,float(Ray.SizeSquared()));
                            if(T>.05f&&T<.85f)Near=FMath::Min(Near,FMath::PointDistToSegment(V,Eye,Face));}return Near;};
                    const FVector Other=(-Side+.45f*Across-.25f*Line).GetSafeNormal();
                    const float Here=Clear(Dir);if(Here<20.f&&Clear(Other)>Here+12.f)Dir=Other;}} // only when this side is blocked
            else Dir=FVector(-Facing.Y,Facing.X,0); // one right against the other (held up, face to face): from beside her, not through him
            Focus=FMath::Lerp(HerBox.GetCenter(),HisBox.GetCenter(),.3f);Size=FMath::Max(Size,(HerBox+HisBox).GetExtent().Size()*.8f);
        }
    }else{Dir=(Facing+.5f*FVector(Facing.Y,-Facing.X,0)).GetSafeNormal();Focus.Z+=HerBox.GetExtent().Z*.15f;} // her body from the bones, a little above its middle so the head is never cut
    // Lying (on her back, on all fours): her body runs along the floor, not the way the actor faces. From beside her body,
    // toward her face, and from higher up, so neither her feet nor her back fill the view. One on top of the other (an
    // arch under him): the same, from beside her body, though it is not flat.
    float Pitch=18.f;
    {FVector Head,Hips;if((Mode==0||bOverlap)&&HBone(Girl,TEXT("j_head"),Head)&&HBone(Girl,TEXT("j_hips"),Hips)){
        FVector Body=Head-Hips;const float Up=FMath::Abs(float(Body.Z));Body.Z=0;
        const bool Flat=Body.Size()>Up*1.2f;
        if((Flat||bOverlap&&Body.Size()>10.f)&&Body.Normalize()){FVector Side(-Body.Y,Body.X,0);if((Side|Facing)<0)Side=-Side;
            if(Flat){Dir=(Side+.35f*Body).GetSafeNormal();Pitch=35.f;}else Dir=Side;}}} // held upright against him: straight from the side
    if(Dir.IsNearlyZero())return;
    float Distance=Size*(Mode==0&&Pitch<30.f?3.1f:2.5f); /* standing alone: head to feet in the shot (2.5 cut both) */ // the bone box is tighter than the mesh bounds
    // Tall shots (a standing pair: 立位 cut both heads at 2.5) need the height in the picture: far enough that the vertical
    // field of view holds their whole height, with a margin.
    {FBox Shot=HerBox;if(auto* Partner=Mode!=0&&Gallery->PartnerActor&&!bMaleHidden?Gallery->PartnerActor.Get():nullptr){const FBox PartnerBox=HBodyBox(Partner);if(PartnerBox.IsValid)Shot+=PartnerBox;}
     const float HFov=Girl->StudioCam&&Girl->StudioCam->GetCameraComponent()?Girl->StudioCam->GetCameraComponent()->FieldOfView:60.f;
     FVector2D View(16.,9.);if(GEngine&&GEngine->GameViewport)GEngine->GameViewport->GetViewportSize(View);
     const float Aspect=View.Y>1.?float(View.X/View.Y):16.f/9.f;
     const float HalfV=FMath::Atan(FMath::Tan(FMath::DegreesToRadians(HFov*.5f))/Aspect);
     const float Upper=float(Shot.Max.Z-Focus.Z),Lower=float(Focus.Z-Shot.Min.Z); // the focus need not be the middle
     Distance=FMath::Max(Distance,FMath::Max(Upper,Lower)*1.15f/FMath::Tan(HalfV));}
    Girl->OrbitFocus=Focus;Girl->OrbitYaw=FMath::RadiansToDegrees(FMath::Atan2(Dir.X,Dir.Y));Girl->OrbitPitch=Pitch;Girl->OrbitDistance=FMath::Clamp(Distance,120.f,650.f);
    Girl->ApplyOrbitCamera();
    UE_LOG(LogTemp,Display,TEXT("H camera: facing (%.2f,%.2f) shot from (%.2f,%.2f) pitch %.0f distance %.0f"),Facing.X,Facing.Y,Dir.X,Dir.Y,Pitch,Girl->OrbitDistance);
    HideWallsInTheWay();
}
void UOKHScenePanel::ApplyMaleLook(){
    auto* Him=Gallery?Gallery->PartnerActor.Get():nullptr;if(!IsValid(Him)||Mode==0||Him->IsHidden()||!Gallery->PairedPlayback)return;MaleLookFor=Him;
    // His bottom and briefs (the original takes them off in H): off shows the body under them, the penis with it.
    {uint8 S[9];for(int32 K=0;K<9;++K)S[K]=Him->DisplayClothesStates[K];if(bMaleBottomOff){S[1]=3;S[3]=3;}Him->SetClothingPartsHidden(S);}
    // Put his own materials back first, then (see-through) one colour on every part of him.
    int32 M=0;for(UMeshComponent* Comp:MaleGhostComps){if(!IsValid(Comp)){continue;}for(int32 I=0;I<Comp->GetNumMaterials()&&M<MaleGhostMats.Num();++I,++M)Comp->SetMaterial(I,MaleGhostMats[M]);}
    MaleGhostComps.Reset();MaleGhostMats.Reset();
    for(UMeshComponent* Comp:MaleGhostOutlines)if(IsValid(Comp))Comp->SetVisibility(true);MaleGhostOutlines.Reset();
    // The penis (hidden like the card preview's) shows when his bottom is off.
    Him->SetGenitalVisible(bMaleBottomOff);
    if(MaleView!=1)return;
    // Looked up each time, not kept in a static: once the scene that made his ghost material is gone, nothing holds the
    // parent and it is collected; the next scene read the freed one (crash on coming back into the room, R9).
    UMaterialInterface* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/IndependentResources/PreviewMaterials/M_PreviewTranslucent.M_PreviewTranslucent"));
    if(!Parent){UE_LOG(LogTemp,Warning,TEXT("H scene: no see-through material for him"));return;}
    if(!MaleGhost){MaleGhost=UMaterialInstanceDynamic::Create(Parent,this);MaleGhost->SetVectorParameterValue(TEXT("PreviewTint"),FLinearColor(.25f,.45f,1.f,.05f));}
    TInlineComponentArray<UMeshComponent*> Comps;Him->GetComponents(Comps);
    // Everything but the penis and the balls (o_dankon / cm_o_dan00, o_dan_f): those stay as they are, to see where he is in her.
    auto PartName=[Him](UMeshComponent* Comp){for(const FName& Tag:Comp->ComponentTags){const FString T=Tag.ToString();if(!T.StartsWith(TEXT("KKSessionPart_")))continue;
        OkMeshView V{};if(Him->Session.PartAt(FCString::Atoi(*T.Mid(14)),V)&&V.name)return FString(UTF8_TO_TCHAR(V.name)).ToLower();}return FString();};
    auto Genital=[&](UMeshComponent* Comp){const FString N=PartName(Comp);return N==TEXT("o_dankon")||N==TEXT("cm_o_dan00")||N==TEXT("o_dan_f")||N==TEXT("cm_o_dan_f");};
    for(UMeshComponent* Comp:Comps){if(!Comp->IsVisible()||Genital(Comp))continue;
        // One layer of colour: his body and face only. Hair cards, clothes, eyes and outline shells stacked on them made the
        // 5% look like half; they are hidden while he is see-through.
        {const UMaterialInterface* First=Comp->GetNumMaterials()>0?Comp->GetMaterial(0):nullptr;const FString N=PartName(Comp);
         if(!(N==TEXT("o_body_a")||N==TEXT("o_body_b")||N==TEXT("cf_o_face"))||(First&&First->GetName().Contains(TEXT("Outline")))){MaleGhostOutlines.Add(Comp);Comp->SetVisibility(false);continue;}}
        MaleGhostComps.Add(Comp);
        for(int32 I=0;I<Comp->GetNumMaterials();++I){MaleGhostMats.Add(Comp->GetMaterial(I));Comp->SetMaterial(I,MaleGhost);}}
    UE_LOG(LogTemp,Display,TEXT("H scene: he is drawn see-through (%d parts)"),MaleGhostComps.Num());
}
void UOKHScenePanel::MoveParts(AKKPreviewActor* A,const FVector& Want,FVector& Applied){
    const FVector Step=Want-Applied;if(!IsValid(A)||Step.IsNearlyZero(.01f))return;Applied=Want;
    if(USceneComponent* Root=A->GetRootComponent()){const FVector Local=Root->GetComponentTransform().InverseTransformVectorNoScale(Step);
        for(USceneComponent* Child:Root->GetAttachChildren())if(IsValid(Child))Child->AddRelativeLocation(Local);}
}
void UOKHScenePanel::SyncPair(){if(Gallery&&Gallery->PairedPlayback)Gallery->PairedPlayback->SyncAsync();}
void UOKHScenePanel::HoldFeet(AKKPreviewActor* A,bool bStands,const FVector& Offset,const FVector (&Feet)[2],FVector (&Kept)[2],bool& bIk){
    if(!IsValid(A)||!A->Session.IsOpen())return;
    // Lowered while standing: the feet stay where the clip put them (on the floor), so the knees bend.
    if(bStands&&Offset.Z<-1.f){
        const FVector Local=A->GetRootComponent()?A->GetRootComponent()->GetComponentTransform().InverseTransformVectorNoScale(Offset):Offset;
        const FVector Shift=AKKPreviewActor::ActorLocalToUnity(Local);
        const FVector L=Feet[0]-Shift,R=Feet[1]-Shift;
        if(bIk&&FVector::Dist(L,Kept[0])<.002&&FVector::Dist(R,Kept[1])<.002)return; // within 2 mm: leave the solver alone
        SyncPair();if(A->Session.SetStudioIk({7,8},{L,R})){bIk=true;Kept[0]=L;Kept[1]=R;}
    }else if(bIk){SyncPair();A->Session.SetStudioIk({},{});bIk=false;}
}
void UOKHScenePanel::RestoreAlign(){
    auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;auto* Him=Gallery?Gallery->PartnerActor.Get():nullptr;
    if(AlignedHer==Girl)MoveParts(Girl,FVector::ZeroVector,HerApplied);if(AlignedHim==Him)MoveParts(Him,FVector::ZeroVector,HimApplied);
    if(bHerIk||bHisIk)SyncPair();
    if(bHerIk&&IsValid(Girl)&&Girl->Session.IsOpen())Girl->Session.SetStudioIk({},{});if(bHisIk&&IsValid(Him)&&Him->Session.IsOpen())Him->Session.SetStudioIk({},{});
    bHerIk=bHisIk=false;HimOffset=HerOffset=FVector::ZeroVector;
}
void UOKHScenePanel::AlignHim(float Delta){
    auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;auto* Him=Gallery?Gallery->PartnerActor.Get():nullptr;
    if(!IsValid(Girl)||!IsValid(Him))return;
    if(AlignedHim!=Him){AlignedHim=Him;HimApplied=FVector::ZeroVector;bHisIk=false;} // a new him has his parts where they were made
    if(AlignedHer!=Girl){AlignedHer=Girl;HerApplied=FVector::ZeroVector;bHerIk=false;}
    const FString Key=FString::Printf(TEXT("%d|%d"),Mode,ActivePose);
    // A new pose places them on one origin again: start from no offset, no held feet.
    if(Key!=AlignedKey){AlignedKey=Key;HimOffset=HerOffset=FVector::ZeroVector;bAlignLogged=false;DeepAlong=1e9f;AlignPushed=0.f;AtWindow=0.f;AtMin=1e9f;AtMax=-1e9f;
        if(bHisIk||bHerIk)SyncPair();
        if(bHisIk){Him->Session.SetStudioIk({},{});bHisIk=false;}if(bHerIk){Girl->Session.SetStudioIk({},{});bHerIk=false;}
        FeetWait=0;bFeetRead=false;}
    AlignOff=-1.f;
    const FString Cur=Current(),Base=HBase(Cur);
    const bool In=Mode==2&&Gallery->PairedPlayback&&Gallery->PairedPlayback->GetElapsed()>0.f&&(Base==TEXT("Insert")||Base==TEXT("InsertIdle")||Base.EndsWith(TEXT("Loop"))&&!Base.Contains(TEXT("OUT"))||Base.Contains(TEXT("_IN_"))||Base==TEXT("IN_A"));
    // Where their feet are in the clip (no IK yet), and whether each stands on the floor: read a few frames into the pose.
    if(In&&!bFeetRead&&++FeetWait>3){
        const float Floor=float(Girl->GetActorLocation().Z);SyncPair();
        auto Read=[&](AKKPreviewActor* A,FVector (&Feet)[2],bool& bStands){FString Error;bStands=false;
            if(!A->bStudioIkReady){if(!A->Session.PrepareStudioIk(Error)){UE_LOG(LogTemp,Warning,TEXT("H align: no IK for %s: %s"),*A->GetName(),*Error);return;}A->bStudioIkReady=true;}
            FVector L,R,Hips;if(!A->Session.IkEffectorPosition(7,Feet[0])||!A->Session.IkEffectorPosition(8,Feet[1])||!HBone(A,TEXT("j_foot_L"),L)||!HBone(A,TEXT("j_foot_R"),R)||!HBone(A,TEXT("j_hips"),Hips))return;
            // On a foot (the other may be lifted) with the hips well over it: kneeling, sitting and lying have a foot near the
            // floor too, and lowering those sinks knees and backs into the floor.
            const float Low=float(FMath::Min(L.Z,R.Z));bStands=Low-Floor<18.f&&float(Hips.Z)-Low>60.f;};
        Read(Him,HisFeet,bHisStands);Read(Girl,HerFeet,bHerStands);bFeetRead=true;
        UE_LOG(LogTemp,Display,TEXT("H align: %s he %s, she %s"),*Cur,bHisStands?TEXT("stands"):TEXT("does not stand"),bHerStands?TEXT("stands"):TEXT("does not stand"));
    }
    FVector E,P0,P1;
    if(In&&HBone(Girl,Cur.StartsWith(TEXT("A_"))?TEXT("J_Ana"):TEXT("J_Kokan"),E)&&HBone(Him,TEXT("J_dan100_00"),P0)&&HBone(Him,TEXT("J_dan109_00"),P1)){
        const FVector Axis=(P1-P0).GetSafeNormal();const float Len=float((P1-P0).Size());
        if(Len>2.f){
            const FVector D=E-P0;const float Along=float(D|Axis);const FVector Fix=D-Axis*Along; // onto the line of the penis
            AlignOff=float(Fix.Size());AlignAt=100.f*Along/Len;AlignLen=Len;
            {FVector FL,FR;float Foot=1e9f;if(HBone(Him,TEXT("j_foot_L"),FL))Foot=float(FL.Z);if(HBone(Him,TEXT("j_foot_R"),FR))Foot=FMath::Min(Foot,float(FR.Z));AlignFoot=Foot-float(Girl->GetActorLocation().Z);}
            AtMin=FMath::Min(AtMin,AlignAt);AtMax=FMath::Max(AtMax,AlignAt);AtWindow+=Delta;
            {const float Floor=float(Girl->GetActorLocation().Z);AKKPreviewActor* Who[2]={Him,Girl};
             for(int32 W=0;W<2;++W){FVector FL,FR;if(HBone(Who[W],TEXT("j_foot_L"),FL)&&HBone(Who[W],TEXT("j_foot_R"),FR)){const float F=float(FMath::Min(FL.Z,FR.Z))-Floor;FeetMin[W]=FMath::Min(FeetMin[W],F);FeetMax[W]=FMath::Max(FeetMax[W],F);}}}
            if(AtWindow>=2.f){AtShownMin=AtMin;AtShownMax=AtMax;AtWindow=0.f;AtMin=1e9f;AtMax=-1e9f;
                for(int32 W=0;W<2;++W){FeetShown[W*2]=FeetMin[W];FeetShown[W*2+1]=FeetMax[W];FeetMin[W]=1e9f;FeetMax[W]=-1e9f;}}
            // Depth: only the deepest point of his thrusts is set (70% of him in her); per frame it would flatten the clip's
            // thrust. The deepest point eases back (a fifth of his length a second) so a shallower pose is followed too.
            DeepAlong=FMath::Min(DeepAlong+Delta*Len*.2f,Along);
            // The insertion itself starts with him outside her: its line is aligned, its depth is the clip's.
            float Deeper=0.f;if(Base==TEXT("Insert")){}else if(DeepAlong>Len*.3f&&DeepAlong<1e8f)Deeper=DeepAlong-Len*.3f;else if(DeepAlong<Len*.05f)Deeper=DeepAlong-Len*.05f;
            const float Rate=FMath::Min(1.f,Delta*10.f),Push=Deeper*FMath::Min(1.f,Delta*2.f);DeepAlong-=Push;AlignPushed+=Push;AlignDeep=100.f*DeepAlong/Len;
            if(!bAlignLogged){bAlignLogged=true;UE_LOG(LogTemp,Display,TEXT("H align: %s penis %.1f cm, her entrance %.1f cm off its line at %.0f%% of it"),*Cur,Len,AlignOff,AlignAt);}
            HimOffset+=Fix*Rate+Axis*Push;
            HimOffset=HimOffset.GetClampedToMaxSize(45.f); // a mismatch past that is a different pose, not a height
            // Both standing: the one who would have to go down bends, and nobody rises off the floor.
            if(bFeetRead&&bHisStands&&bHerStands){const double Net=HimOffset.Z-HerOffset.Z;
                if(Net>0.){HerOffset.Z=-Net;HimOffset.Z=0.;}else{HerOffset.Z=0.;HimOffset.Z=Net;}}
        }
    }
    MoveParts(Him,HimOffset,HimApplied);
    {const double Was=HerApplied.Z;MoveParts(Girl,HerOffset,HerApplied);const double Down=HerApplied.Z-Was;
     if(Down!=0.&&bPropsTable)for(AStaticMeshActor* Prop:Props)if(IsValid(Prop))Prop->AddActorWorldOffset(FVector(0.,0.,Down));}
    if(bFeetRead){HoldFeet(Him,bHisStands,HimOffset,HisFeet,HisHeld,bHisIk);HoldFeet(Girl,bHerStands,HerOffset,HerFeet,HerHeld,bHerIk);}
}
void UOKHScenePanel::HideWallsInTheWay(){
    auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;if(!IsValid(Girl)||!Girl->StudioCam||Props.IsEmpty())return;
    auto* Him=Gallery->PartnerActor.Get();const FVector Focus=Girl->OrbitFocus;
    const FVector Eye=Girl->StudioCam->GetActorLocation();
    // A wall (the only prop taller than a person) between the camera and them is hidden for this shot; seats and beds stay.
    // Lines to her head, her hips and his head, not only to the middle between them (a wall can hide her and miss that).
    TArray<FVector> Seen={Focus};FVector B;if(HBone(Girl,TEXT("j_head"),B))Seen.Add(B);if(HBone(Girl,TEXT("j_hips"),B))Seen.Add(B);
    if(Mode!=0&&IsValid(Him)&&!Him->IsHidden()&&HBone(Him,TEXT("j_head"),B))Seen.Add(B);
    for(AStaticMeshActor* A:Props)if(IsValid(A)){FVector O,E;A->GetActorBounds(false,O,E);bool Blocks=false;
        if(E.Z>90.f)for(const FVector& To:Seen)Blocks|=FMath::LineBoxIntersection(FBox(O-E,O+E),Eye,To,To-Eye);A->SetActorHiddenInGame(Blocks);}
}
void UOKHScenePanel::ClearProps(){for(AStaticMeshActor* A:Props)if(IsValid(A))A->Destroy();Props.Reset();}
void UOKHScenePanel::NativeDestruct(){if(Siru[0]+Siru[1]+Siru[2]+Siru[3]+Siru[4]>0){FMemory::Memzero(Siru);ApplySiru();}if(bCovered)Cover(false,FPlatformTime::Seconds());ClearProps();RestoreBlush();RestoreAlign();Super::NativeDestruct();}
void UOKHScenePanel::PlaceProps(int32 Kind){
    ClearProps();bPropsTable=false;auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;if(!IsValid(Girl)||!Girl->BodySkel||!GetWorld())return;
    auto* Him=Mode!=0&&Gallery->PartnerActor&&!bMaleHidden?Gallery->PartnerActor.Get():nullptr;
    auto Bone=HBone;
    TArray<FVector> Hers;FVector HerHips,HisHips,P;
    if(!Bone(Girl,TEXT("j_hips"),HerHips))return;
    for(const TCHAR* N:{TEXT("j_hips"),TEXT("j_spine02"),TEXT("j_head"),TEXT("j_hand_L"),TEXT("j_hand_R"),TEXT("j_foot_L"),TEXT("j_foot_R")})if(Bone(Girl,N,P))Hers.Add(P);
    const bool HasHim=Bone(Him,TEXT("j_hips"),HisHips);
    const float Floor=Girl->GetActorLocation().Z;
    static UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));if(!Cube)return;
    UMaterialInterface* Shape=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    FLinearColor Tint=FLinearColor(.55f,.55f,.55f); // set before a Box for its colour
    auto Box=[&](const FVector& Center,const FVector& Size,const FRotator& Rot){
        auto* A=GetWorld()->SpawnActor<AStaticMeshActor>(Center,Rot);if(!A)return;
        A->SetMobility(EComponentMobility::Movable);A->GetStaticMeshComponent()->SetStaticMesh(Cube);A->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if(Shape)if(auto* M=UMaterialInstanceDynamic::Create(Shape,A)){M->SetVectorParameterValue(TEXT("Color"),Tint);A->GetStaticMeshComponent()->SetMaterial(0,M);}
        A->SetActorScale3D(Size/100.f);Props.Add(A);};
    FString What;
    if(Kind==1){
        // Under whoever sits: knees off the floor and thighs about level with the hips (kneeling has the knees on the floor).
        // Both sitting (she is on his lap): the lower hips. Neither clearly: the lower hips too.
        auto Sitting=[&](AKKPreviewActor* A,const FVector& Hips){FVector KL,KR;if(!A||!Bone(A,TEXT("j_leg01_L"),KL)||!Bone(A,TEXT("j_leg01_R"),KR))return false;
            const float Knee=float(KL.Z+KR.Z)/2;UE_LOG(LogTemp,Display,TEXT("H scene: seat check knee %.0f above the floor, hips %.0f above the knees"),Knee-Floor,float(Hips.Z)-Knee);
            return Knee>Floor+25.f&&FMath::Abs(float(Hips.Z)-Knee)<25.f;}; // seated: 2–14 cm; bent over standing: 31
        const bool SheSits=Sitting(Girl,HerHips),HisSit=HasHim&&Sitting(Him,HisHips);
        // Nobody sits (she bends over it from behind): the seat is under her hands, nothing through her body.
        FVector HL,HR;
        if(!SheSits&&!HisSit&&Bone(Girl,TEXT("j_hand_L"),HL)&&Bone(Girl,TEXT("j_hand_R"),HR)&&FMath::Min(HL.Z,HR.Z)>Floor+25.f){
            const FVector Hands=(HL+HR)/2;const float Top=float(FMath::Min(HL.Z,HR.Z))-4.f;
            Box(FVector(Hands.X,Hands.Y,(Floor+Top)/2),FVector(48,48,Top-Floor),(Hands-HerHips).GetSafeNormal2D().Rotation());What=TEXT("seat under her hands");
            UE_LOG(LogTemp,Display,TEXT("H scene: %s placed for the pose (%d)"),*What,Props.Num());return;}
        const bool HeSits=HasHim&&(HisSit!=SheSits?HisSit:HisHips.Z<HerHips.Z);const FVector Sit=HeSits?HisHips:HerHips;
        const float Top=FMath::Max(Floor+20.f,Sit.Z-12.f);Box(FVector(Sit.X,Sit.Y,(Floor+Top)/2),FVector(48,48,Top-Floor),FRotator::ZeroRotator);What=TEXT("seat");
        // Leaning back (a sofa, a chair back): a slanted back from the hips along the spine to the head, behind it.
        FVector Head;if(Bone(HeSits?Him:Girl,TEXT("j_head"),Head)){
            const FVector Spine=Head-Sit;FVector Away=HasHim?(HeSits?HerHips-HisHips:HisHips-HerHips):FVector::ZeroVector;Away.Z=0;
            if(Away.Normalize()&&float(Spine|-Away)>20.f){
                const FVector Along=Spine.GetSafeNormal();const FVector Behind=-Away-Along*float(-Away|Along);
                Box((Sit+Head)/2+Behind.GetSafeNormal()*14.f,FVector(Spine.Size()+30.f,52,8),Along.Rotation());What=TEXT("seat with a back");}}
    }else if(Kind==2){
        // Under her body where it rests (hips, back, head, hands above the knees): a table just below the lowest of them.
        TArray<FVector> Rest;for(const FVector& V:Hers)if(V.Z>Floor+40.f)Rest.Add(V);if(Rest.IsEmpty())return;
        float Top=TNumericLimits<float>::Max();for(const FVector& V:Rest)Top=FMath::Min(Top,float(V.Z));Top-=6.f;
        // Laid along her body (hips to head), not square to the world: a square box around a turned body reached into them.
        FVector Head,Along=FVector::ZeroVector;if(Bone(Girl,TEXT("j_head"),Head))Along=Head-HerHips;Along.Z=0;if(!Along.Normalize())Along=FVector::ForwardVector;
        const FVector Across(-Along.Y,Along.X,0.);
        float A0=1e9f,A1=-1e9f,C0=1e9f,C1=-1e9f;
        for(const FVector& V:Rest)if(V.Z<Top+30.f){const float A=float((V-HerHips)|Along),C=float((V-HerHips)|Across);A0=FMath::Min(A0,A);A1=FMath::Max(A1,A);C0=FMath::Min(C0,C);C1=FMath::Max(C1,C);}
        A0-=15.f;A1+=15.f;C0-=18.f;C1+=18.f;
        // Its near edge clear of what stands beside it (his hips, knees and feet; her knees and feet hanging down): she bends
        // over the edge, he stands at it, nobody is inside the table.
        TArray<FVector> Beside;for(const TCHAR* N:{TEXT("j_leg01_L"),TEXT("j_leg01_R"),TEXT("j_foot_L"),TEXT("j_foot_R")})if(Bone(Girl,N,P)&&P.Z<Top+10.f)Beside.Add(P);
        if(HasHim)for(const TCHAR* N:{TEXT("j_hips"),TEXT("j_leg01_L"),TEXT("j_leg01_R"),TEXT("j_foot_L"),TEXT("j_foot_R")})if(Bone(Him,N,P)&&P.Z<Top+25.f)Beside.Add(P);
        const float Middle=(A0+A1)/2;
        for(const FVector& V:Beside){const float A=float((V-HerHips)|Along),C=float((V-HerHips)|Across);if(C>C0-10.f&&C<C1+10.f&&A<Middle)A0=FMath::Max(A0,A+12.f);}
        A1=FMath::Max(A1,A0+45.f);if(C1-C0<55.f){const float M=(C0+C1)/2;C0=M-27.5f;C1=M+27.5f;}
        const FVector Mid=HerHips+Along*((A0+A1)/2)+Across*((C0+C1)/2);
        Box(FVector(Mid.X,Mid.Y,(Floor+Top)/2),FVector(A1-A0,C1-C0,Top-Floor),Along.Rotation());What=TEXT("table");bPropsTable=true;
    }else if(Kind==3){
        // Beyond her on the side away from him (he is behind her: her hands are on it; he faces her: her back is), or the
        // way her hands reach when she is alone.
        FVector Dir=FVector::ZeroVector;FVector L,R;
        if(HasHim)Dir=HerHips-HisHips;
        if(Dir.Size2D()<5.f&&Bone(Girl,TEXT("j_hand_L"),L)&&Bone(Girl,TEXT("j_hand_R"),R))Dir=(L+R)/2-HerHips;
        Dir.Z=0;if(!Dir.Normalize())return;
        // Past both of them (he can be the one against it): never through anyone.
        TArray<FVector> Both=Hers;if(HasHim)for(const TCHAR* N:{TEXT("j_hips"),TEXT("j_spine02"),TEXT("j_head"),TEXT("j_hand_L"),TEXT("j_hand_R"),TEXT("j_foot_L"),TEXT("j_foot_R"),TEXT("j_leg01_L"),TEXT("j_leg01_R")})if(Bone(Him,N,P))Both.Add(P);
        float Far=0;for(const FVector& V:Both)Far=FMath::Max(Far,float((V-HerHips)|Dir));
        const FVector At=HerHips+Dir*(Far+4.f);
        Box(FVector(At.X,At.Y,Floor+110.f),FVector(6,160,220),Dir.Rotation());What=TEXT("wall");
    }else if(Room==1&&HerHips.Z<Floor+60.f){
        // A bed: its mattress top level with the floor under both of them (nobody moves), along her body, with a frame
        // and a headboard past her head. Not under a standing pose.
        // Along the body that lies flattest (him on his back when she rides him), the headboard past that head.
        FVector Along(1,0,0),Lying=HerHips;float Flat=-1.f;
        for(AKKPreviewActor* A:{Girl,HasHim?Him:nullptr}){FVector Hd,Hp;if(!A||!Bone(A,TEXT("j_head"),Hd)||!Bone(A,TEXT("j_hips"),Hp))continue;
            FVector D=Hd-Hp;D.Z=0;if(D.Size()>Flat){Flat=D.Size();Lying=Hp;if(D.Normalize())Along=D;}}
        const FVector Across(-Along.Y,Along.X,0);
        TArray<FVector> All=Hers;if(HasHim){for(const TCHAR* N:{TEXT("j_hips"),TEXT("j_head"),TEXT("j_foot_L"),TEXT("j_foot_R"),TEXT("j_leg01_L"),TEXT("j_leg01_R")})if(Bone(Him,N,P))All.Add(P);}
        float A0=TNumericLimits<float>::Max(),A1=-A0,C0=A0,C1=-A0;
        for(const FVector& V:All){const FVector D=V-Lying;A0=FMath::Min(A0,float(D|Along));A1=FMath::Max(A1,float(D|Along));C0=FMath::Min(C0,float(D|Across));C1=FMath::Max(C1,float(D|Across));}
        A0-=30.f;A1+=25.f;C0-=30.f;C1+=30.f;
        if(A1-A0<200.f){const float Grow=(200.f-(A1-A0))/2;A0-=Grow;A1+=Grow;}
        if(C1-C0<150.f){const float Grow=(150.f-(C1-C0))/2;C0-=Grow;C1+=Grow;}
        const FVector Mid=Lying+Along*((A0+A1)/2)+Across*((C0+C1)/2);const FRotator Rot=Along.Rotation();
        Tint=FLinearColor(.92f,.9f,.86f);Box(FVector(Mid.X,Mid.Y,Floor-18.f),FVector(A1-A0,C1-C0,40.f),Rot); // mattress, top 2 cm above the floor
        Tint=FLinearColor(.32f,.2f,.12f);Box(FVector(Mid.X,Mid.Y,Floor-32.f),FVector(A1-A0+12.f,C1-C0+12.f,30.f),Rot); // frame
        const FVector HeadAt=Lying+Along*(A1+8.f)+Across*((C0+C1)/2);Box(FVector(HeadAt.X,HeadAt.Y,Floor+20.f),FVector(8.f,C1-C0+12.f,80.f),Rot);What=TEXT("bed"); // a low headboard (below the camera)
    }
    UE_LOG(LogTemp,Display,TEXT("H scene: %s placed for the pose (%d)"),*What,Props.Num());
    HideWallsInTheWay(); // the camera may already be framed for this pose
}
FString UOKHScenePanel::StateWords(const FString& Base) const{
    if(Mode==0)return OKT(Base==TEXT("K_Loop")||Base==TEXT("K_Touch")?TEXT("ui.hscene.state_kiss"):Base.StartsWith(TEXT("Orgasm"))?TEXT("ui.hscene.state_after"):TouchArea!=INDEX_NONE?TEXT("ui.hscene.state_caress"):TEXT("ui.hscene.state_idle"));
    if(Base==TEXT("Idle")||Base==TEXT("Stop_Idle"))return OKT(TEXT("ui.hscene.state_idle"));
    if(Base==TEXT("Insert")||Base==TEXT("InsertIdle"))return OKT(TEXT("ui.hscene.state_inserted"));
    if(Base==TEXT("WLoop"))return OKT(TEXT("ui.hscene.state_slow"));
    if(Base==TEXT("SLoop"))return OKT(TEXT("ui.hscene.state_fast"));
    if(Base==TEXT("OLoop"))return OKT(TEXT("ui.hscene.state_near"));
    if(Base.StartsWith(TEXT("Oral")))return OKT(TEXT("ui.hscene.state_oral"));
    if(Base.EndsWith(TEXT("_A"))||Base==TEXT("Drop")||Base==TEXT("Pull"))return OKT(TEXT("ui.hscene.state_after"));
    return OKT(TEXT("ui.hscene.state_finishing"));
}
// What she is in the middle of, for her answer (the turn message, not the cached prefix).
FString UOKHScenePanel::Context() const{
    const FString Cur=Current(),Base=Mode==0?Cur:HBase(Cur);
    const FString PoseName=Mode==0&&AibuPoses.IsValidIndex(ActiveAibu)?AibuPoses[ActiveAibu].Name:Poses.IsValidIndex(ActivePose)?Poses[ActivePose].Name:FString();
    // What he has already taken off her: she agreed to this room, so undressing is part of it, not news to protest.
    static const TCHAR* Parts[]={TEXT("ui.hscene.cloth_top"),TEXT("ui.hscene.cloth_bottom"),TEXT("ui.hscene.cloth_bra"),TEXT("ui.hscene.cloth_shorts"),TEXT("ui.hscene.cloth_gloves"),TEXT("ui.hscene.cloth_pantyhose"),TEXT("ui.hscene.cloth_socks"),TEXT("ui.hscene.cloth_inner_shoes"),TEXT("ui.hscene.cloth_shoes")};
    TArray<FString> Bare;if(Gallery&&Gallery->PreviewActor)for(int32 K=0;K<9;++K)if(Gallery->ClothesStates[K]==3&&Gallery->PreviewActor->DisplayClothesStates[K]!=3)Bare.Add(OKT(Parts[K]));
    // How many more she can take today (HadEnough's numbers): her words about having had enough follow the same limit.
    const auto& X=Gallery?Gallery->HExperience:nullptr;
    const int32 Left=FMath::Max(0,FMath::Max(1,X.IsValid()&&X->HasField(TEXT("climax_limit"))?int32(X->GetNumberField(TEXT("climax_limit"))):4)-(X.IsValid()&&X->HasField(TEXT("today_orgasms"))?int32(X->GetNumberField(TEXT("today_orgasms"))):0)-HerOrgasms);
    // How many more times her desire for him wants (ok-rules hexp climax_wanted): a keen girl says she wants more.
    const int32 Want=FMath::Max(0,(X.IsValid()&&X->HasField(TEXT("climax_wanted"))?int32(X->GetNumberField(TEXT("climax_wanted"))):0)-(X.IsValid()&&X->HasField(TEXT("today_orgasms"))?int32(X->GetNumberField(TEXT("today_orgasms"))):0)-HerOrgasms);
    return OKMsg(TEXT("ui.hscene.talk_context"),{{TEXT("left"),Left},{TEXT("want"),FMath::Min(Want,Left)},{TEXT("bare"),Bare.IsEmpty()?OKT(TEXT("ui.hscene.bare_none")):FString::Join(Bare,TEXT("、"))},{TEXT("pose"),PoseName},{TEXT("state"),StateWords(Base)},{TEXT("speed"),FString::Printf(TEXT("%.1f"),Mode==0||HIsLoop(Base)?PlaybackSpeed():0.f)},
        {TEXT("her"),FMath::RoundToInt(Her)},{TEXT("his"),FMath::RoundToInt(His)},{TEXT("orgasms"),HerOrgasms},{TEXT("finishes"),HisFinishes},{TEXT("condom"),Mode==2&&(bCrotch||bAnal)?(bCondom?TEXT("yes"):TEXT("no")):TEXT("none")}});
}
void UOKHScenePanel::OnInputCommitted(const FText& Text,ETextCommit::Type Commit){
    if(Commit!=ETextCommit::OnEnter)return;Say(Text.ToString());if(Input)Input->SetText(FText::GetEmpty());
}
TSharedRef<FJsonObject> UOKHScenePanel::Describe() const{
    auto O=MakeShared<FJsonObject>();
    O->SetStringField(TEXT("pose"),Mode==0&&AibuPoses.IsValidIndex(ActiveAibu)?AibuPoses[ActiveAibu].Name:Poses.IsValidIndex(ActivePose)?Poses[ActivePose].Name:FString());O->SetStringField(TEXT("pending"),Poses.IsValidIndex(PendingPose)?Poses[PendingPose].Name:FString());
    if(AlignOff>=0.f){auto A=MakeShared<FJsonObject>();A->SetNumberField(TEXT("off"),AlignOff);A->SetNumberField(TEXT("at"),AlignAt);A->SetNumberField(TEXT("len"),AlignLen);A->SetNumberField(TEXT("moved"),HimOffset.Size());A->SetNumberField(TEXT("at_min"),AtShownMin);A->SetNumberField(TEXT("foot"),AlignFoot);A->SetNumberField(TEXT("mz"),HimOffset.Z);A->SetNumberField(TEXT("her_z"),HerOffset.Z);A->SetBoolField(TEXT("his_knees"),bHisIk);A->SetBoolField(TEXT("her_knees"),bHerIk);{TArray<TSharedPtr<FJsonValue>> F;for(float V:FeetShown)F.Add(MakeShared<FJsonValueNumber>(V));A->SetArrayField(TEXT("feet"),F);}A->SetNumberField(TEXT("pushed"),AlignPushed);A->SetNumberField(TEXT("deep"),AlignDeep);A->SetNumberField(TEXT("at_max"),AtShownMax);O->SetObjectField(TEXT("align"),A);}
    O->SetBoolField(TEXT("covered"),bCovered);{TArray<TSharedPtr<FJsonValue>> S;for(int32 V:Siru)S.Add(MakeShared<FJsonValueNumber>(V));O->SetArrayField(TEXT("siru"),S);}
    {TArray<TSharedPtr<FJsonValue>> L;for(const FString& R:LiquidReport())L.Add(MakeShared<FJsonValueString>(R));O->SetArrayField(TEXT("liquid"),L);}
    // Where their heads and feet are on the screen (0..1; -1 behind the camera): both whole in the shot or not.
    {TArray<TSharedPtr<FJsonValue>> Shot;APlayerController* PC=GetOwningPlayer();FVector2D View(1.,1.);if(GEngine&&GEngine->GameViewport)GEngine->GameViewport->GetViewportSize(View);
     for(AKKPreviewActor* A:{Gallery?Gallery->PreviewActor.Get():nullptr,bMaleHidden||!Gallery?nullptr:Gallery->PartnerActor.Get()})if(IsValid(A)&&PC&&View.X>1.)
      for(const TCHAR* N:{TEXT("j_head"),TEXT("j_foot_L"),TEXT("j_foot_R")}){FVector P;FVector2D S(-View.X,-View.Y);
       if(HBone(A,N,P))PC->ProjectWorldLocationToScreen(P+(N[2]==TEXT('h')?FVector(0,0,12):FVector::ZeroVector),S);
       Shot.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(S.X/View.X*100)/100));Shot.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(S.Y/View.Y*100)/100));}
     O->SetArrayField(TEXT("shot"),Shot);}
    O->SetNumberField(TEXT("blush"),BlushMats.IsEmpty()?-1.f:BlushShown);
    O->SetStringField(TEXT("state"),Current());O->SetStringField(TEXT("wanted"),Wanted);O->SetNumberField(TEXT("mode"),Mode);
    TArray<TSharedPtr<FJsonValue>> Q;for(const FString& S:Queue)Q.Add(MakeShared<FJsonValueString>(S));O->SetArrayField(TEXT("queue"),Q);
    O->SetNumberField(TEXT("playback_speed"),PlaybackSpeed());O->SetNumberField(TEXT("held"),Held.Num());O->SetStringField(TEXT("caption"),Caption?Caption->GetText().ToString():FString());
    O->SetNumberField(TEXT("her"),Her);O->SetNumberField(TEXT("his"),His);O->SetNumberField(TEXT("speed"),SpeedValue);O->SetBoolField(TEXT("condom"),bCondom);O->SetBoolField(TEXT("anal_play"),bAnalPlay);
    O->SetStringField(TEXT("title"),Title?Title->GetText().ToString():FString());O->SetStringField(TEXT("hint"),Hint?Hint->GetText().ToString():FString());O->SetStringField(TEXT("face"),FaceKey); /* eye/brow/openness now (UpdateFace) */
    if(Gallery&&Gallery->PreviewActor){O->SetBoolField(TEXT("breath"),Gallery->PreviewActor->bHBreath);O->SetBoolField(TEXT("speaking"),Gallery->PreviewActor->IsSpeaking());O->SetNumberField(TEXT("mouth"),Gallery->PreviewActor->EffectiveFaceRate(0));}
    if(Gallery&&Gallery->HExperience.IsValid())O->SetObjectField(TEXT("experience"),Gallery->HExperience); // what she allows (hexp.rs h_experience)
    auto Buttons=[](UPanelWidget* Panel){TArray<TSharedPtr<FJsonValue>> Out;if(Panel)for(int32 I=0;I<Panel->GetChildrenCount();++I)if(auto* B=Cast<UOKHSceneButton>(Panel->GetChildAt(I))){auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("kind"),B->Kind);R->SetBoolField(TEXT("enabled"),B->GetIsEnabled());
        if(auto* L=Cast<UTextBlock>(B->GetContent()))R->SetStringField(TEXT("label"),L->GetText().ToString());Out.Add(MakeShared<FJsonValueObject>(R));}return Out;};
    O->SetArrayField(TEXT("actions"),Buttons(Actions));O->SetArrayField(TEXT("list"),Buttons(List));O->SetNumberField(TEXT("tab"),Tab);
    O->SetObjectField(TEXT("result"),Result());
    return O;
}
bool UOKHScenePanel::SelectPoseNamed(const FString& Name){
    const int32 I=Poses.IndexOfByPredicate([&](const FOKHPose& P){return P.Name==Name||P.Raw==Name;});if(I==INDEX_NONE)return false;
    Tab=Poses[I].Mode==2?TabSonyu:TabHand+Poses[I].KindHoushi;SelectPose(I);return true;
}
void UOKHScenePanel::SetSpeed(float Value){SpeedValue=FMath::Clamp(Value,0.f,1.f);if(Speed)Speed->SetValue(SpeedValue);}
TSharedRef<FJsonObject> UOKHScenePanel::Result() const{
    auto Counts=MakeShared<FJsonObject>();
    Counts->SetBoolField(TEXT("anal_refused"),bAnalAsked&&bAnalRefused);Counts->SetBoolField(TEXT("played"),bPlayed);Counts->SetBoolField(TEXT("service"),bService);Counts->SetNumberField(TEXT("service_finishes"),ServiceFinishes);
    auto Part=[](bool Inserted,int32 Climaxes){auto O=MakeShared<FJsonObject>();O->SetBoolField(TEXT("inserted"),Inserted);O->SetNumberField(TEXT("climaxes"),Climaxes);return O;};
    Counts->SetObjectField(TEXT("crotch"),Part(bCrotch,CrotchClimaxes));Counts->SetObjectField(TEXT("anal"),Part(bAnal,AnalClimaxes));
    {static const TCHAR* Keys[]={TEXT("kiss"),TEXT("chest"),TEXT("crotch"),TEXT("anal"),TEXT("butt"),TEXT("nipple")};auto Areas=MakeShared<FJsonObject>();
     for(int32 I=0;I<6;++I)Areas->SetNumberField(Keys[I],FMath::RoundToDouble(AreaSeconds[I]));Counts->SetObjectField(TEXT("areas"),Areas);Counts->SetNumberField(TEXT("kiss"),FMath::RoundToDouble(AreaSeconds[0]));}
    Counts->SetBoolField(TEXT("raw"),bRaw);Counts->SetBoolField(TEXT("raw_pressed"),bRawPressed&&bRaw);
    Counts->SetNumberField(TEXT("his_inside"),HisInside);Counts->SetNumberField(TEXT("his_outside"),HisOutside);Counts->SetNumberField(TEXT("his_mouth"),HisMouth);Counts->SetNumberField(TEXT("swallowed"),Swallowed);Counts->SetNumberField(TEXT("her_orgasms"),HerOrgasms);Counts->SetNumberField(TEXT("his_finishes"),HisFinishes);
    auto Out=MakeShared<FJsonObject>();Out->SetObjectField(TEXT("counts"),Counts);
    TArray<TSharedPtr<FJsonValue>> P,E;for(const FString& S:PosesPlayed)P.Add(MakeShared<FJsonValueString>(S));for(const FString& S:Events)E.Add(MakeShared<FJsonValueString>(S));
    Out->SetArrayField(TEXT("poses"),P);Out->SetArrayField(TEXT("events"),E);
    return Out;
}
