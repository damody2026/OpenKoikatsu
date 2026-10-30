#include "OKHScene.h"
#include "HAL/IConsoleManager.h"
#include "OKCharacterController.h"
#include "OKStreetSocialText.h"
#include "OKMotionLabelZh.h"
#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
#include "KKRustExtract.h"
#include "KKSpeech.h"
#include "Engine/StaticMeshActor.h"
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
    PendingAibu=Index;Queue.Reset();Wanted.Reset();TouchArea=INDEX_NONE;BuildList();
    const int32 Generation=++AibuGeneration;const FAibuPose Pose=AibuPoses[Index];const FString Root=FOkSession::CommonPairRoot();
    TWeakObjectPtr<UOKHScenePanel> Weak(this);
    Note(OKT(TEXT("ui.hscene.loading_caress")),false);
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
            UI->SoloKeys=MoveTemp(Keys);UI->ActiveAibu=UI->PendingAibu;UI->PendingAibu=INDEX_NONE;UI->ActivePose=INDEX_NONE;UI->Mode=0;UI->SoloState.Reset();
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
        auto* L=WidgetTree->ConstructWidget<UTextBlock>();L->SetText(FText::FromString(Label));L->SetAutoWrapText(true);B->SetContent(L);List->AddChild(B);
    };
    if(Tab==TabClothes){
        static const TCHAR* Parts[]={TEXT("ui.hscene.cloth_top"),TEXT("ui.hscene.cloth_bottom"),TEXT("ui.hscene.cloth_bra"),TEXT("ui.hscene.cloth_shorts"),TEXT("ui.hscene.cloth_gloves"),TEXT("ui.hscene.cloth_pantyhose"),TEXT("ui.hscene.cloth_socks"),TEXT("ui.hscene.cloth_inner_shoes"),TEXT("ui.hscene.cloth_shoes")};
        static const TCHAR* States[]={TEXT("ui.hscene.cloth_on"),TEXT("ui.hscene.cloth_half"),TEXT("ui.hscene.cloth_half2"),TEXT("ui.hscene.cloth_off")};
        // Each piece she is wearing comes off or goes back on by itself (visibility only: a reload would end the scene).
        Add(ToggleMale,OKT(bMaleHidden?TEXT("ui.hscene.male_show"):TEXT("ui.hscene.male_hide")),Mode!=0,bMaleHidden);
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
    const FString K=Kind;
    if(Mode==1){
        ++ServiceFinishes;++HisFinishes;His=0.f;if(K==TEXT("inside"))++HisMouth;else ++HisOutside;
        if(K==TEXT("inside")){Chain({TEXT("IN_Start"),TEXT("IN_Loop"),TEXT("Oral_Idle_IN"),TEXT("Oral_Idle")});OralIdleSince=0.;const int32 Kind=Poses.IsValidIndex(ActivePose)?Poses[ActivePose].KindHoushi:1;Note(OKT(Kind==1?TEXT("ui.hscene.event_service_inside"):Kind==2?TEXT("ui.hscene.event_service_chest"):TEXT("ui.hscene.event_service_hand")));}
        else{Chain({TEXT("M_OUT_Start"),TEXT("M_OUT_Loop"),TEXT("OUT_A")});Note(OKT(TEXT("ui.hscene.event_service_outside")));}
        return;
    }
    const bool Org=K.StartsWith(TEXT("org")),Same=K.StartsWith(TEXT("same"));
    if(Org||Same){++HerOrgasms;++(bAnalPlay?AnalClimaxes:CrotchClimaxes);Her=0.f;}
    if(Same||K==TEXT("inside")||K==TEXT("outside")){++HisFinishes;His=0.f;++(K==TEXT("outside")?HisOutside:HisInside);}
    if(Org||Same)ReleaseHeld();
    bInsideFinished=Same||K==TEXT("inside");
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
    if(Kind==ToggleMale){bMaleHidden=!bMaleHidden;BuildList();return;}
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
        SpeedValue=FMath::Min(1.f,SpeedValue+.15f);if(Speed)Speed->SetValue(SpeedValue);
        if(Mode==1&&(Base==TEXT("Idle")||Base==TEXT("Stop_Idle")||Base.EndsWith(TEXT("_A"))))Want(TEXT("WLoop"));
        else if(Mode==2&&Base==TEXT("InsertIdle"))Want(P(TEXT("WLoop")));
        break;
    case Slower:
        SpeedValue=FMath::Max(0.f,SpeedValue-.15f);if(Speed)Speed->SetValue(SpeedValue);
        if(Mode==2&&SpeedValue<=0.f&&HIsLoop(Base))Want(P(TEXT("InsertIdle")));
        break;
    case Toggle:if(HIsLoop(Base)){bStrong=Base==TEXT("WLoop");Want(P(bStrong?TEXT("SLoop"):TEXT("WLoop")));}break;
    case Stop:Want(Mode==1?(Has(TEXT("Stop_Idle"))?TEXT("Stop_Idle"):TEXT("Idle")):P(TEXT("InsertIdle")));break;
    case Inside:Finish(Mode==2&&Her>=70.f?(bStrong?TEXT("sameS"):TEXT("sameW")):TEXT("inside"));break;
    case Outside:Finish(TEXT("outside"));break;
    case Drink:++Swallowed;Chain({TEXT("Drink_IN"),TEXT("Drink"),TEXT("Drink_A")});Note(OKT(TEXT("ui.hscene.event_drink")));break;
    case Vomit:Chain({TEXT("Vomit_IN"),TEXT("Vomit"),TEXT("Vomit_A")});Note(OKT(TEXT("ui.hscene.event_vomit")));break;
    case Again:Want(TEXT("WLoop"));bStrong=false;break;
    case Condom:bCondom=!bCondom;Note(OKT(bCondom?TEXT("ui.hscene.condom_on"):TEXT("ui.hscene.condom_off")),false);break;
    case Insert:
        // Raw only when she lets him (NamaInsertCheck): otherwise she has him put one on (the original's voice 302).
        if(!bCondom&&!(X.IsValid()&&X->GetBoolField(TEXT("raw_ok")))){
            if(!bRawAsked){bRawAsked=true;Note(OKT(TEXT("ui.hscene.wants_condom")));break;}
            bRawPressed=true;Note(OKT(TEXT("ui.hscene.raw_after_asking")));}
        bAnalPlay=false;Chain({TEXT("Insert"),TEXT("InsertIdle")});Note(OKT(bCondom?TEXT("ui.hscene.event_insert"):TEXT("ui.hscene.event_insert_raw")),false);break;
    case InsertAnal:{
        // Asked once a scene: her anal experience is the chance she agrees (isAnalInsertOK), more for a lover or a lewd girl.
        if(!bAnalAsked){bAnalAsked=true;float Chance=X.IsValid()?float(X->GetNumberField(TEXT("anal_chance"))):0.f;if(Level()>=3)Chance=1.f;if(Gallery->HHeroineStage==TEXT("lover"))Chance+=.3f;bAnalRefused=FMath::FRand()>=FMath::Max(Chance,.1f);}
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

void UOKHScenePanel::BuildActions(){
    if(!Actions||!WidgetTree)return;
    const FString Cur=Current(),Base=Mode==0?Cur:HBase(Cur);
    const bool Busy=!Queue.IsEmpty()&&!(Queue.Num()==1&&Cur==Queue[0]);
    const FString Key=FString::Printf(TEXT("%d|%s|%d|%d|%d|%d|%d|%d|%d"),Mode,*Cur,Busy,TouchArea,ActiveAibu+100*PendingAibu,His>=70.f,int32(bCondom)+2*int32(bAnalRefused)+4*int32(bLockHer)+8*int32(bLockHis),ActivePose,PendingPose);
    if(Key==ActionsKey)return;ActionsKey=Key;Actions->ClearChildren();Offered.Reset();
    auto Add=[this](int32 Kind,const TCHAR* Label){Offered.Add(Kind);auto* B=WidgetTree->ConstructWidget<UOKHSceneButton>();B->Kind=Kind;B->Owner=this;B->OnClicked.AddDynamic(B,&UOKHSceneButton::HandleClick);auto* L=WidgetTree->ConstructWidget<UTextBlock>();L->SetText(FText::FromString(OKT(Label)));B->SetContent(L);Actions->AddChildToWrapBox(B);};
    if(Mode==0&&ActiveAibu!=INDEX_NONE&&PendingAibu==INDEX_NONE&&!Busy){
        if(!Base.StartsWith(TEXT("Orgasm"))&&!Base.EndsWith(TEXT("Dislikes"))){
            Add(Kiss,TEXT("ui.hscene.act_kiss"));Add(TouchChest,TEXT("ui.hscene.act_touch_chest"));Add(TouchNipple,TEXT("ui.hscene.act_touch_nipple"));Add(TouchCrotch,TEXT("ui.hscene.act_touch_crotch"));Add(TouchButt,TEXT("ui.hscene.act_touch_butt"));Add(TouchAnal,TEXT("ui.hscene.act_touch_anal"));
            if(TouchArea!=INDEX_NONE)Add(Release,TEXT("ui.hscene.act_release"));
            Add(LockHer,bLockHer?TEXT("ui.hscene.act_unlock_her"):TEXT("ui.hscene.act_lock_her"));}
        return;
    }
    if(Mode==0||ActivePose==INDEX_NONE||PendingPose!=INDEX_NONE||Busy||Cur.IsEmpty())return;
    const FOKHPose& Pose=Poses[ActivePose];
    Add(LockHer,bLockHer?TEXT("ui.hscene.act_unlock_her"):TEXT("ui.hscene.act_lock_her"));Add(LockHis,bLockHis?TEXT("ui.hscene.act_unlock_his"):TEXT("ui.hscene.act_lock_his"));
    if(Mode==1){
        if(Base==TEXT("Idle")||Base==TEXT("Stop_Idle"))Add(Faster,TEXT("ui.hscene.act_start"));
        else if(HIsLoop(Base)){Add(Faster,TEXT("ui.hscene.act_faster"));Add(Slower,TEXT("ui.hscene.act_slower"));Add(Toggle,TEXT("ui.hscene.act_toggle"));Add(Stop,TEXT("ui.hscene.act_stop"));}
        else if(Base==TEXT("OLoop")){Add(Inside,Pose.KindHoushi==1?TEXT("ui.hscene.act_in_mouth"):Pose.KindHoushi==2?TEXT("ui.hscene.act_on_chest"):TEXT("ui.hscene.act_in_hand"));Add(Outside,TEXT("ui.hscene.act_outside"));}
        else if(Base==TEXT("Oral_Idle")){Add(Drink,TEXT("ui.hscene.act_drink"));Add(Vomit,TEXT("ui.hscene.act_vomit"));}
        else if(Base.EndsWith(TEXT("_A"))){Add(Again,TEXT("ui.hscene.act_again"));Add(Stop,TEXT("ui.hscene.act_rest"));}
        return;
    }
    const bool Out=Base==TEXT("Idle")||Base==TEXT("OUT_A");
    if(Out){Add(Insert,TEXT("ui.hscene.act_insert"));if(Pose.Anal&&!bAnalRefused)Add(InsertAnal,TEXT("ui.hscene.act_insert_anal"));Add(Condom,bCondom?TEXT("ui.hscene.act_condom_on"):TEXT("ui.hscene.act_condom_off"));}
    else if(Base==TEXT("InsertIdle")){Add(Faster,TEXT("ui.hscene.act_move"));Add(Pull,TEXT("ui.hscene.act_pull"));}
    else if(HIsLoop(Base)){Add(Faster,TEXT("ui.hscene.act_faster"));Add(Slower,TEXT("ui.hscene.act_slower"));Add(Toggle,TEXT("ui.hscene.act_toggle"));Add(Stop,TEXT("ui.hscene.act_stop"));
        if(His>=70.f){Add(Inside,TEXT("ui.hscene.act_inside"));Add(Outside,TEXT("ui.hscene.act_outside"));}}
    else if(Base.EndsWith(TEXT("_A"))){Add(Continue,TEXT("ui.hscene.act_continue"));Add(Pull,TEXT("ui.hscene.act_pull"));}
}

void UOKHScenePanel::NativeTick(const FGeometry& Geometry,float Delta){
    Super::NativeTick(Geometry,Delta);
    if(!Gallery)return;
    if(Poses.IsEmpty()&&!Gallery->NativePairs.IsEmpty()){LoadPoses();BuildList();}
    UpdateFace();FrameCamera();
    {const int32 Kind=Mode==0?(AibuPoses.IsValidIndex(ActiveAibu)?AibuPoses[ActiveAibu].Prop:0):(Poses.IsValidIndex(ActivePose)?Poses[ActivePose].Prop:0);
     const FString Key=FString::Printf(TEXT("%d|%d|%d"),Mode,ActivePose,ActiveAibu);const double T=FPlatformTime::Seconds();
     if(Key!=PropKey){PropKey=Key;ClearProps();PropAt=Kind>0?T+.8:0.;}
     else if(PropAt>0&&T>=PropAt){PropAt=0;PlaceProps(Kind);}}
    // A pose counts as done once something happened in it (a gauge rose, he entered her, a touch), not when it only opened:
    // a pair opens on a moving state for a few seconds before its Idle.
    {const FString Cur=Current(),Base=Mode==0?Cur:HBase(Cur);
     if(Mode!=0&&Poses.IsValidIndex(ActivePose)&&(Her>3.f||His>3.f||Base.StartsWith(TEXT("Insert"))))PosesPlayed.AddUnique(Poses[ActivePose].Name);
     if(Mode==0&&AibuPoses.IsValidIndex(ActiveAibu)&&TouchArea!=INDEX_NONE)PosesPlayed.AddUnique(AibuPoses[ActiveAibu].Name);}
    if(Mode!=0&&Gallery->PartnerActor&&Gallery->PartnerActor->IsHidden()!=bMaleHidden)Gallery->PartnerActor->SetActorHiddenInGame(bMaleHidden);
    const double Now=FPlatformTime::Seconds();
    auto* Playback=Gallery->PairedPlayback.Get();
    // The pose actually playing: the one he picked once it starts, or the one the scene opened with.
    const int32 Playing=Gallery->SelectedPairCatalogueIndex();
    if((Mode!=0||PendingPose!=INDEX_NONE)&&Playback&&!Gallery->bPairPreparing&&Gallery->PairStateCombo&&Gallery->PairStateCombo->GetOptionCount()>1){
        const int32 Found=Poses.IndexOfByPredicate([Playing](const FOKHPose& Pose){return Pose.Pair==Playing;});
        if(Found!=INDEX_NONE&&(Found!=ActivePose||PendingPose==Found)){
            ActivePose=Found;PendingPose=INDEX_NONE;Mode=Poses[Found].Mode;Queue.Reset();Wanted.Reset();bAnalPlay=false;bInsideFinished=false;bStrong=false;
            if(Has(TEXT("Idle")))Want(TEXT("Idle"));
            Tab=Mode==2?TabSonyu:TabHand+Poses[Found].KindHoushi;BuildList();
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
            if(First)React(OKT(Virgin?(Cur.StartsWith(TEXT("A_"))?TEXT("ui.hscene.react_first_anal"):TEXT("ui.hscene.react_virgin")):Cur.StartsWith(TEXT("A_"))?TEXT("ui.hscene.event_insert_anal"):bCondom?TEXT("ui.hscene.event_insert"):TEXT("ui.hscene.event_insert_raw")));
        }
        if(HIsLoop(Base)){bPlayed=true;if(Mode==1)bService=true;}
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
        else if(Her>=100.f){++HerOrgasms;Her=0.f;TouchArea=INDEX_NONE;ReleaseHeld();Chain({TEXT("Orgasm_Start"),TEXT("Orgasm_Loop"),TEXT("Orgasm_A"),TEXT("Idle")});Note(OKT(TEXT("ui.hscene.event_her_orgasm")));}
    }else if(Queue.IsEmpty()&&HIsLoop(Base)){
        if(Mode==1){His+=Delta*(2.5f+8.f*Rate)*Strong;if(bLockHis)His=FMath::Min(His,69.f);Her=FMath::Min(69.f,Her+Delta*.8f);if(His>=70.f&&Wanted.IsEmpty())Want(TEXT("OLoop"));}
        else{His+=Delta*(1.2f+3.5f*Rate)*Strong;Her+=Delta*(1.5f+6.f*Rate)*Strong*HerByLevel[FMath::Clamp(Level(),0,3)];
            if(bLockHer)Her=FMath::Min(Her,95.f);if(bLockHis)His=FMath::Min(His,95.f);
            if(Her>=100.f)Finish(His>=70.f?(Strong>1.f?TEXT("sameS"):TEXT("sameW")):(Strong>1.f?TEXT("orgS"):TEXT("orgW")));
            else if(His>=100.f)Finish(!bCondom?TEXT("outside"):Her>=70.f?(Strong>1.f?TEXT("sameS"):TEXT("sameW")):TEXT("inside"));}
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
    Text=Outbox[0].Text;System=Outbox[0].System;Out=Context();if(!Outbox[0].Note.IsEmpty())Out+=TEXT("\n")+Outbox[0].Note;Outbox.RemoveAt(0);
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
    Out=Context();if(Caption&&Gallery)Caption->SetText(FText::FromString(Gallery->HPlayerName+TEXT("：")+Text));return true;
}
void UOKHScenePanel::ShowLine(const FString& Text){if(Caption)Caption->SetText(FText::FromString(Text));}
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
    else if(L>=2){Eye=4;Brow=0;Open=.9f;}
    const FString Key=FString::Printf(TEXT("%d/%d/%.2f"),Eye,Brow,Open);if(Key==FaceKey)return;FaceKey=Key;
    Girl->SetFaceControl(1,Eye,Open,true);Girl->SetFaceControl(2,Brow,1.f,true);
}
void UOKHScenePanel::FrameCamera(){
    auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;if(!IsValid(Girl)||!Girl->StudioCam)return;
    const FString Key=FString::Printf(TEXT("%d|%d|%d"),Mode,ActivePose,ActiveAibu);const double Now=FPlatformTime::Seconds();
    if(Key!=FramedKey){FramedKey=Key;FrameAt=Now+.8;return;} // after the pose has placed them
    if(FrameAt<=0||Now<FrameAt)return;FrameAt=0;
    const FBox HerBox=Girl->GetComponentsBoundingBox(true);if(!HerBox.IsValid)return;
    FVector Facing=Girl->GetActorRotation().RotateVector(FVector(0,1,0));Facing.Z=0;Facing=Facing.GetSafeNormal();
    FVector Focus=HerBox.GetCenter(),Dir=Facing;float Size=HerBox.GetExtent().Size();
    auto* Him=Gallery->PartnerActor.Get();
    if(Mode!=0&&IsValid(Him)&&!Him->IsHidden()){
        const FBox HisBox=Him->GetComponentsBoundingBox(true);
        if(HisBox.IsValid){
            FVector Line=HisBox.GetCenter()-HerBox.GetCenter();Line.Z=0;
            if(Line.Size()>5.f){Line.Normalize();FVector Side(-Line.Y,Line.X,0);if((Side|Facing)<0)Side=-Side;Dir=(Side+.45f*Facing-.25f*Line).GetSafeNormal();}
            Focus=FMath::Lerp(HerBox.GetCenter(),HisBox.GetCenter(),.3f);Size=FMath::Max(Size,(HerBox+HisBox).GetExtent().Size()*.8f);
        }
    }else{Dir=(Facing+.5f*FVector(Facing.Y,-Facing.X,0)).GetSafeNormal();Size*=.95f;Focus.Z+=HerBox.GetExtent().Z*.25f;} // head to knees, the head never cut
    if(Dir.IsNearlyZero())return;
    Girl->OrbitFocus=Focus;Girl->OrbitYaw=FMath::RadiansToDegrees(FMath::Atan2(Dir.X,Dir.Y));Girl->OrbitPitch=18.f;Girl->OrbitDistance=FMath::Clamp(Size*2.1f,120.f,600.f);
    Girl->ApplyOrbitCamera();
}
void UOKHScenePanel::ClearProps(){for(AStaticMeshActor* A:Props)if(IsValid(A))A->Destroy();Props.Reset();}
void UOKHScenePanel::NativeDestruct(){ClearProps();Super::NativeDestruct();}
void UOKHScenePanel::PlaceProps(int32 Kind){
    ClearProps();auto* Girl=Gallery?Gallery->PreviewActor.Get():nullptr;if(!IsValid(Girl)||!Girl->BodySkel||!GetWorld())return;
    auto* Him=Mode!=0&&Gallery->PartnerActor&&!bMaleHidden?Gallery->PartnerActor.Get():nullptr;
    // Their bones in the world (the male skeleton may name its bones like hers).
    auto Bone=[](AKKPreviewActor* A,const TCHAR* Name,FVector& Out){
        if(!IsValid(A)||!A->BodySkel)return false;
        for(const TCHAR* P:{TEXT("cf_"),TEXT("cm_")}){const FName N(FString(P)+Name);if(A->BodySkel->GetBoneIndex(N)!=INDEX_NONE){Out=A->BodySkel->GetBoneLocation(N);return true;}}
        return false;};
    TArray<FVector> Hers;FVector HerHips,HisHips,P;
    if(!Bone(Girl,TEXT("j_hips"),HerHips))return;
    for(const TCHAR* N:{TEXT("j_hips"),TEXT("j_spine02"),TEXT("j_head"),TEXT("j_hand_L"),TEXT("j_hand_R"),TEXT("j_foot_L"),TEXT("j_foot_R")})if(Bone(Girl,N,P))Hers.Add(P);
    const bool HasHim=Bone(Him,TEXT("j_hips"),HisHips);
    const float Floor=Girl->GetActorLocation().Z;
    static UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));if(!Cube)return;
    auto Box=[&](const FVector& Center,const FVector& Size,const FRotator& Rot){
        auto* A=GetWorld()->SpawnActor<AStaticMeshActor>(Center,Rot);if(!A)return;
        A->SetMobility(EComponentMobility::Movable);A->GetStaticMeshComponent()->SetStaticMesh(Cube);A->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        A->SetActorScale3D(Size/100.f);Props.Add(A);};
    FString What;
    if(Kind==1){
        // Under the lower hips of the two (he sits and she is on his lap, or she sits alone).
        const bool HeSits=HasHim&&HisHips.Z<HerHips.Z;const FVector Sit=HeSits?HisHips:HerHips;
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
        FBox2D Area(ForceInit);for(const FVector& V:Rest)if(V.Z<Top+30.f)Area+=FVector2D(V.X,V.Y);Area=Area.ExpandBy(18.f);
        FVector2D Size=Area.GetSize();Size.X=FMath::Max(Size.X,55.);Size.Y=FMath::Max(Size.Y,55.);const FVector2D Mid=Area.GetCenter();
        Box(FVector(Mid.X,Mid.Y,(Floor+Top)/2),FVector(Size.X,Size.Y,Top-Floor),FRotator::ZeroRotator);What=TEXT("table");
    }else if(Kind==3){
        // Beyond her on the side away from him (he is behind her: her hands are on it; he faces her: her back is), or the
        // way her hands reach when she is alone.
        FVector Dir=FVector::ZeroVector;FVector L,R;
        if(HasHim)Dir=HerHips-HisHips;
        if(Dir.Size2D()<5.f&&Bone(Girl,TEXT("j_hand_L"),L)&&Bone(Girl,TEXT("j_hand_R"),R))Dir=(L+R)/2-HerHips;
        Dir.Z=0;if(!Dir.Normalize())return;
        float Far=0;for(const FVector& V:Hers)Far=FMath::Max(Far,float((V-HerHips)|Dir));
        const FVector At=HerHips+Dir*(Far+4.f);
        Box(FVector(At.X,At.Y,Floor+110.f),FVector(6,160,220),Dir.Rotation());What=TEXT("wall");
    }
    UE_LOG(LogTemp,Display,TEXT("H scene: %s placed for the pose (%d)"),*What,Props.Num());
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
    return OKMsg(TEXT("ui.hscene.talk_context"),{{TEXT("pose"),PoseName},{TEXT("state"),StateWords(Base)},{TEXT("speed"),FString::Printf(TEXT("%.1f"),Mode==0||HIsLoop(Base)?PlaybackSpeed():0.f)},
        {TEXT("her"),FMath::RoundToInt(Her)},{TEXT("his"),FMath::RoundToInt(His)},{TEXT("orgasms"),HerOrgasms},{TEXT("finishes"),HisFinishes},{TEXT("condom"),Mode==2&&(bCrotch||bAnal)?(bCondom?TEXT("yes"):TEXT("no")):TEXT("none")}});
}
void UOKHScenePanel::OnInputCommitted(const FText& Text,ETextCommit::Type Commit){
    if(Commit!=ETextCommit::OnEnter)return;Say(Text.ToString());if(Input)Input->SetText(FText::GetEmpty());
}
TSharedRef<FJsonObject> UOKHScenePanel::Describe() const{
    auto O=MakeShared<FJsonObject>();
    O->SetStringField(TEXT("pose"),Mode==0&&AibuPoses.IsValidIndex(ActiveAibu)?AibuPoses[ActiveAibu].Name:Poses.IsValidIndex(ActivePose)?Poses[ActivePose].Name:FString());O->SetStringField(TEXT("pending"),Poses.IsValidIndex(PendingPose)?Poses[PendingPose].Name:FString());
    O->SetStringField(TEXT("state"),Current());O->SetStringField(TEXT("wanted"),Wanted);O->SetNumberField(TEXT("mode"),Mode);
    TArray<TSharedPtr<FJsonValue>> Q;for(const FString& S:Queue)Q.Add(MakeShared<FJsonValueString>(S));O->SetArrayField(TEXT("queue"),Q);
    O->SetNumberField(TEXT("playback_speed"),PlaybackSpeed());O->SetNumberField(TEXT("held"),Held.Num());O->SetStringField(TEXT("caption"),Caption?Caption->GetText().ToString():FString());
    O->SetNumberField(TEXT("her"),Her);O->SetNumberField(TEXT("his"),His);O->SetNumberField(TEXT("speed"),SpeedValue);O->SetBoolField(TEXT("condom"),bCondom);O->SetBoolField(TEXT("anal_play"),bAnalPlay);
    O->SetStringField(TEXT("title"),Title?Title->GetText().ToString():FString());O->SetStringField(TEXT("hint"),Hint?Hint->GetText().ToString():FString());
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
    Counts->SetBoolField(TEXT("played"),bPlayed);Counts->SetBoolField(TEXT("service"),bService);Counts->SetNumberField(TEXT("service_finishes"),ServiceFinishes);
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
