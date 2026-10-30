#include "OKHScene.h"
#include "OKCharacterController.h"
#include "OKStreetSocialText.h"
#include "OKMotionLabelZh.h"
#include "KKPreviewActor.h"
#include "KKPairedPlayback.h"
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
FString UOKHScenePanel::Current() const{return Gallery&&Gallery->PairedPlayback?Gallery->PairedPlayback->GetState():FString();}
bool UOKHScenePanel::Has(const FString& State) const{return Gallery&&Gallery->PairStateCombo&&Gallery->PairStateCombo->FindOptionIndex(State)>0;}
void UOKHScenePanel::Note(const FString& Text){Events.Add(Text);if(Gallery)Gallery->NoteAction(TEXT("h"),Text);if(Hint)Hint->SetText(FText::FromString(Text));UE_LOG(LogTemp,Display,TEXT("H scene: %s"),*Text);}

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
    Tabs=WidgetTree->ConstructWidget<UWrapBox>();Tabs->SetInnerSlotPadding(FVector2D(4,4));Box->AddChildToVerticalBox(Tabs)->SetPadding(FMargin(0,8,0,4));
    List=WidgetTree->ConstructWidget<UScrollBox>();
    if(auto* S=Box->AddChildToVerticalBox(List))S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    auto Tab_=[this](int32 Kind,const TCHAR* Key){auto* B=WidgetTree->ConstructWidget<UOKHSceneButton>();B->Kind=Kind;B->Owner=this;B->OnClicked.AddDynamic(B,&UOKHSceneButton::HandleClick);auto* L=WidgetTree->ConstructWidget<UTextBlock>();L->SetText(FText::FromString(OKT(Key)));B->SetContent(L);Tabs->AddChildToWrapBox(B);};
    Tab_(TabHand,TEXT("ui.hscene.tab_hand"));Tab_(TabMouth,TEXT("ui.hscene.tab_mouth"));Tab_(TabBreast,TEXT("ui.hscene.tab_breast"));Tab_(TabSonyu,TEXT("ui.hscene.tab_sonyu"));Tab_(TabClothes,TEXT("ui.hscene.tab_clothes"));
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    return Super::RebuildWidget();
}

void UOKHScenePanel::LoadPoses(){
    Poses.Reset();if(!Gallery)return;
    const auto& Meta=HPoseMeta();
    for(int32 I=0;I<Gallery->NativePairs.Num();++I){
        const auto& Row=Gallery->NativePairs[I];if(!Row)continue;
        const FString Category=Row->GetStringField(TEXT("category"));const int32 Id=int32(Row->GetNumberField(TEXT("id")));
        FOKHPose Pose;Pose.Pair=I;Pose.Mode=Category==TEXT("AnimationInfo_01")?1:Category==TEXT("AnimationInfo_02")?2:0;if(!Pose.Mode)continue;
        Pose.Name=Row->GetStringField(TEXT("name"));
        if(const TSharedPtr<FJsonObject>* M=Meta.Find(FString::Printf(TEXT("%s:%d"),*Category,Id))){
            Pose.KindHoushi=int32((*M)->GetNumberField(TEXT("kind_houshi")));Pose.Anal=(*M)->GetBoolField(TEXT("anal"));
            Pose.IsExperience=int32((*M)->GetNumberField(TEXT("is_experience")));Pose.StateRestriction=int32((*M)->GetNumberField(TEXT("state_restriction")));
            const FString Named=(*M)->GetStringField(TEXT("name"));if(!Named.IsEmpty())Pose.Name=Named;
        }
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
        Add(ClothesBase+100,OKT(TEXT("ui.hscene.cloth_all_on")),true,false);Add(ClothesBase+101,OKT(TEXT("ui.hscene.cloth_all_half")),true,false);Add(ClothesBase+102,OKT(TEXT("ui.hscene.cloth_all_off")),true,false);
        for(int32 K=0;K<9;++K)Add(ClothesBase+K,FString::Printf(TEXT("%s：%s"),OKT(Parts[K]),OKT(States[FMath::Clamp<int32>(Gallery?Gallery->ClothesStates[K]:0,0,3)])),true,false);
        return;
    }
    const bool Used=Gallery&&Gallery->HExperience.IsValid()&&Gallery->HExperience->GetBoolField(TEXT("used"));
    for(int32 I=0;I<Poses.Num();++I){const FOKHPose& Pose=Poses[I];
        const bool Shown=Tab==TabSonyu?Pose.Mode==2:Pose.Mode==1&&Pose.KindHoushi==Tab-TabHand;if(!Shown)continue;
        const bool Locked=Pose.IsExperience==1&&!Used;
        FString Label=Pose.Name;if(Pose.Anal)Label+=OKT(TEXT("ui.hscene.anal_mark"));if(Locked)Label+=OKT(TEXT("ui.hscene.locked_mark"));
        Add(PoseBase+I,Label,!Locked,I==ActivePose||I==PendingPose);
    }
}

void UOKHScenePanel::SelectPose(int32 Index){
    if(!Gallery||!Poses.IsValidIndex(Index)||!Gallery->PairCombo)return;
    int32 Option=Gallery->PairVisibleIndices.Find(Poses[Index].Pair);
    if(Option==INDEX_NONE&&Gallery->PairCategoryCombo){Gallery->PairCategoryCombo->SetSelectedIndex(0);Option=Gallery->PairVisibleIndices.Find(Poses[Index].Pair);}
    if(Option==INDEX_NONE){Note(OKT(TEXT("ui.hscene.pose_missing")));return;}
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
    const float Target=HIsLoop(HBase(Current()))?.6f+1.4f*SpeedValue:1.f;
    if(!FMath::IsNearlyEqual(Gallery->PairedPlayback->GetSpeed(),Target,.01f))Gallery->OnPairSpeed(Target/4.f);
}

void UOKHScenePanel::Finish(const TCHAR* Kind){
    const FString K=Kind;
    if(Mode==1){
        ++ServiceFinishes;++HisFinishes;His=0.f;
        if(K==TEXT("inside")){Chain({TEXT("IN_Start"),TEXT("IN_Loop"),TEXT("Oral_Idle_IN"),TEXT("Oral_Idle")});OralIdleSince=0.;const int32 Kind=Poses.IsValidIndex(ActivePose)?Poses[ActivePose].KindHoushi:1;Note(OKT(Kind==1?TEXT("ui.hscene.event_service_inside"):Kind==2?TEXT("ui.hscene.event_service_chest"):TEXT("ui.hscene.event_service_hand")));}
        else{Chain({TEXT("M_OUT_Start"),TEXT("M_OUT_Loop"),TEXT("OUT_A")});Note(OKT(TEXT("ui.hscene.event_service_outside")));}
        return;
    }
    const bool Org=K.StartsWith(TEXT("org")),Same=K.StartsWith(TEXT("same"));
    if(Org||Same){++HerOrgasms;++(bAnalPlay?AnalClimaxes:CrotchClimaxes);Her=0.f;}
    if(Same||K==TEXT("inside")||K==TEXT("outside")){++HisFinishes;His=0.f;}
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
    const FString Base=HBase(Current());
    if(Kind>=TabHand&&Kind<=TabClothes){Tab=Kind;BuildList();return;}
    if(Kind>=PoseBase&&Kind<PoseBase+1000){SelectPose(Kind-PoseBase);return;}
    if(Kind>=ClothesBase){Gallery->OnClothesState(Kind-ClothesBase);Note(OKT(Kind-ClothesBase==102?TEXT("ui.hscene.event_undressed"):Kind-ClothesBase==100?TEXT("ui.hscene.event_dressed"):TEXT("ui.hscene.event_clothes")));BuildList();return;}
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
    case Drink:Chain({TEXT("Drink_IN"),TEXT("Drink"),TEXT("Drink_A")});Note(OKT(TEXT("ui.hscene.event_drink")));break;
    case Vomit:Chain({TEXT("Vomit_IN"),TEXT("Vomit"),TEXT("Vomit_A")});Note(OKT(TEXT("ui.hscene.event_vomit")));break;
    case Again:Want(TEXT("WLoop"));bStrong=false;break;
    case Condom:bCondom=!bCondom;Note(OKT(bCondom?TEXT("ui.hscene.condom_on"):TEXT("ui.hscene.condom_off")));break;
    case Insert:
        // Raw only when she lets him (NamaInsertCheck): otherwise she has him put one on (the original's voice 302).
        if(!bCondom&&!(X.IsValid()&&X->GetBoolField(TEXT("raw_ok")))){bCondom=true;Note(OKT(TEXT("ui.hscene.wants_condom")));break;}
        bAnalPlay=false;Chain({TEXT("Insert"),TEXT("InsertIdle")});Note(OKT(bCondom?TEXT("ui.hscene.event_insert"):TEXT("ui.hscene.event_insert_raw")));break;
    case InsertAnal:{
        // Asked once a scene: her anal experience is the chance she agrees (isAnalInsertOK), more for a lover or a lewd girl.
        if(!bAnalAsked){bAnalAsked=true;float Chance=X.IsValid()?float(X->GetNumberField(TEXT("anal_chance"))):0.f;if(Level()>=3)Chance=1.f;if(Gallery->HHeroineStage==TEXT("lover"))Chance+=.3f;bAnalRefused=FMath::FRand()>=FMath::Max(Chance,.1f);}
        if(bAnalRefused){Note(OKT(TEXT("ui.hscene.anal_refused")));break;}
        bAnalPlay=true;Chain({TEXT("A_Insert"),TEXT("A_InsertIdle")});Note(OKT(TEXT("ui.hscene.event_insert_anal")));break;}
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
    const FString Cur=Current(),Base=HBase(Cur);
    const bool Busy=!Queue.IsEmpty()&&!(Queue.Num()==1&&Cur==Queue[0]);
    const FString Key=FString::Printf(TEXT("%d|%s|%d|%d|%d|%d|%d"),Mode,*Cur,Busy,His>=70.f,bCondom,ActivePose,PendingPose);
    if(Key==ActionsKey)return;ActionsKey=Key;Actions->ClearChildren();
    auto Add=[this](int32 Kind,const TCHAR* Label){auto* B=WidgetTree->ConstructWidget<UOKHSceneButton>();B->Kind=Kind;B->Owner=this;B->OnClicked.AddDynamic(B,&UOKHSceneButton::HandleClick);auto* L=WidgetTree->ConstructWidget<UTextBlock>();L->SetText(FText::FromString(OKT(Label)));B->SetContent(L);Actions->AddChildToWrapBox(B);};
    if(ActivePose==INDEX_NONE||PendingPose!=INDEX_NONE||Busy||Cur.IsEmpty())return;
    const FOKHPose& Pose=Poses[ActivePose];
    if(Mode==1){
        if(Base==TEXT("Idle")||Base==TEXT("Stop_Idle"))Add(Faster,TEXT("ui.hscene.act_start"));
        else if(HIsLoop(Base)){Add(Faster,TEXT("ui.hscene.act_faster"));Add(Slower,TEXT("ui.hscene.act_slower"));Add(Toggle,TEXT("ui.hscene.act_toggle"));Add(Stop,TEXT("ui.hscene.act_stop"));}
        else if(Base==TEXT("OLoop")){Add(Inside,Pose.KindHoushi==1?TEXT("ui.hscene.act_in_mouth"):Pose.KindHoushi==2?TEXT("ui.hscene.act_on_chest"):TEXT("ui.hscene.act_in_hand"));Add(Outside,TEXT("ui.hscene.act_outside"));}
        else if(Base==TEXT("Oral_Idle")){Add(Drink,TEXT("ui.hscene.act_drink"));Add(Vomit,TEXT("ui.hscene.act_vomit"));}
        else if(Base.EndsWith(TEXT("_A"))){Add(Again,TEXT("ui.hscene.act_again"));Add(Stop,TEXT("ui.hscene.act_rest"));}
        return;
    }
    const bool Out=Base==TEXT("Idle")||Base==TEXT("OUT_A");
    if(Out){Add(Insert,TEXT("ui.hscene.act_insert"));if(Pose.Anal)Add(InsertAnal,TEXT("ui.hscene.act_insert_anal"));Add(Condom,bCondom?TEXT("ui.hscene.act_condom_on"):TEXT("ui.hscene.act_condom_off"));}
    else if(Base==TEXT("InsertIdle")){Add(Faster,TEXT("ui.hscene.act_move"));Add(Pull,TEXT("ui.hscene.act_pull"));}
    else if(HIsLoop(Base)){Add(Faster,TEXT("ui.hscene.act_faster"));Add(Slower,TEXT("ui.hscene.act_slower"));Add(Toggle,TEXT("ui.hscene.act_toggle"));Add(Stop,TEXT("ui.hscene.act_stop"));
        if(His>=70.f){Add(Inside,TEXT("ui.hscene.act_inside"));Add(Outside,TEXT("ui.hscene.act_outside"));}}
    else if(Base.EndsWith(TEXT("_A"))){Add(Continue,TEXT("ui.hscene.act_continue"));Add(Pull,TEXT("ui.hscene.act_pull"));}
}

void UOKHScenePanel::NativeTick(const FGeometry& Geometry,float Delta){
    Super::NativeTick(Geometry,Delta);
    if(!Gallery)return;
    if(Poses.IsEmpty()&&!Gallery->NativePairs.IsEmpty()){LoadPoses();BuildList();}
    const double Now=FPlatformTime::Seconds();
    auto* Playback=Gallery->PairedPlayback.Get();
    // The pose actually playing: the one he picked once it starts, or the one the scene opened with.
    const int32 Playing=Gallery->SelectedPairCatalogueIndex();
    if(Playback&&!Gallery->bPairPreparing&&Gallery->PairStateCombo&&Gallery->PairStateCombo->GetOptionCount()>1){
        const int32 Found=Poses.IndexOfByPredicate([Playing](const FOKHPose& Pose){return Pose.Pair==Playing;});
        if(Found!=INDEX_NONE&&(Found!=ActivePose||PendingPose==Found)){
            ActivePose=Found;PendingPose=INDEX_NONE;Mode=Poses[Found].Mode;Queue.Reset();Wanted.Reset();bAnalPlay=false;bInsideFinished=false;bStrong=false;
            PosesPlayed.AddUnique(Poses[Found].Name);
            if(Has(TEXT("Idle")))Want(TEXT("Idle"));
            Tab=Mode==2?TabSonyu:TabHand+Poses[Found].KindHoushi;BuildList();
        }
    }
    const FString Cur=Current(),Base=HBase(Cur);
    if(Cur!=LastState){
        const FString Before=HBase(LastState);
        LastState=Cur;StateSeen=Now;
        // Inserted once the insertion has played (SetInsertKokan / SetInsertAnal): her first time is then.
        if(Base==TEXT("InsertIdle")&&Before==TEXT("Insert")){if(Cur.StartsWith(TEXT("A_")))bAnal=true;else{bCrotch=true;if(!bCondom)bRaw=true;}}
        if(HIsLoop(Base)){bPlayed=true;if(Mode==1)bService=true;}
        if(Base==TEXT("Oral_Idle"))OralIdleSince=Now;
    }
    // The queued flow: a one-shot state moves on when it has played once; a finish's own loop after two rounds.
    if(!Queue.IsEmpty()&&Playback){
        if(Cur==Queue[0]){
            if(Queue.Num()==1)Queue.Reset();
            else{
                const float Elapsed=Playback->GetElapsed(),Length=FMath::Max(.2f,Gallery->PreviewActor?Gallery->PreviewActor->AnimDuration:1.f);
                const bool LoopLike=Base.EndsWith(TEXT("_Loop"))||Base==TEXT("OLoop")||Base==TEXT("Oral_Idle_IN");
                if(Elapsed>=(LoopLike?FMath::Max(Length*2.f,2.5f):Length*.98f)){Queue.RemoveAt(0);Want(Queue[0]);}
            }
        }else if(Wanted!=Queue[0])Want(Queue[0]);
    }
    if(!Wanted.IsEmpty()&&Cur==Wanted)Wanted.Reset();
    if(!Wanted.IsEmpty()&&Playback&&!Gallery->bPairPreparing&&Now-LastRequest>.75&&Gallery->PairStateCombo){
        LastRequest=Now;
        if(Gallery->PairStateCombo->GetSelectedOption()!=Wanted)Gallery->PairStateCombo->SetSelectedOption(Wanted);
        else Gallery->OnPairStateSelected(Wanted,ESelectInfo::Direct);
    }
    // Gauges (HFlag gaugeFemale / gaugeMale): they rise with the movement, faster when strong; hers by her experience.
    if(Speed)SpeedValue=Speed->GetValue();
    const float Rate=SpeedValue,Strong=Base==TEXT("SLoop")?1.25f:1.f;
    static const float HerByLevel[]={.55f,.8f,1.f,1.35f};
    if(Queue.IsEmpty()&&HIsLoop(Base)){
        if(Mode==1){His+=Delta*(2.5f+8.f*Rate)*Strong;Her=FMath::Min(69.f,Her+Delta*.8f);if(His>=70.f&&Wanted.IsEmpty())Want(TEXT("OLoop"));}
        else{His+=Delta*(1.2f+3.5f*Rate)*Strong;Her+=Delta*(1.5f+6.f*Rate)*Strong*HerByLevel[FMath::Clamp(Level(),0,3)];
            if(Her>=100.f)Finish(His>=70.f?(Strong>1.f?TEXT("sameS"):TEXT("sameW")):(Strong>1.f?TEXT("orgS"):TEXT("orgW")));
            else if(His>=100.f)Finish(!bCondom?TEXT("outside"):Her>=70.f?(Strong>1.f?TEXT("sameS"):TEXT("sameW")):TEXT("inside"));}
    }else if(Queue.IsEmpty()&&Base==TEXT("OLoop")&&Mode==1){His=FMath::Min(100.f,His+Delta*3.f);if(His>=100.f)Finish(Poses.IsValidIndex(ActivePose)&&Poses[ActivePose].KindHoushi==1?TEXT("inside"):TEXT("outside"));}
    else if(Queue.IsEmpty()){Her=FMath::Max(0.f,Her-Delta*3.f);His=FMath::Max(0.f,His-Delta*3.f);}
    Her=FMath::Clamp(Her,0.f,100.f);His=FMath::Clamp(His,0.f,100.f);
    // In her mouth she decides herself if he waits (HHoushi Oral_Idle: drink or spit after a while).
    if(Queue.IsEmpty()&&Base==TEXT("Oral_Idle")&&OralIdleSince>0.&&Now-OralIdleSince>8.){OralIdleSince=0.;Click(FMath::RandBool()?Drink:Vomit);}
    ApplySpeed();
    if(HerGauge)HerGauge->SetPercent(Her/100.f);if(HisGauge)HisGauge->SetPercent(His/100.f);
    if(Title){FString Name=Gallery->HHeroineName;const FString LevelName=Gallery->HExperience.IsValid()?Gallery->HExperience->GetStringField(TEXT("level_name")):FString();
        Title->SetText(FText::FromString(LevelName.IsEmpty()?Name:FString::Printf(TEXT("%s · %s"),*Name,*LevelName)));}
    if(Status){
        const FString PoseName=Poses.IsValidIndex(PendingPose)?Poses[PendingPose].Name+OKT(TEXT("ui.hscene.loading")):Poses.IsValidIndex(ActivePose)?Poses[ActivePose].Name:FString(OKT(TEXT("ui.hscene.pick_pose")));
        Status->SetText(FText::FromString(Cur.IsEmpty()?PoseName:FString::Printf(TEXT("%s · %s%s"),*PoseName,*Cur,Mode==2?(bCondom?OKT(TEXT("ui.hscene.condom_mark")):TEXT("")):TEXT(""))));
    }
    BuildActions();
}

TSharedRef<FJsonObject> UOKHScenePanel::Describe() const{
    auto O=MakeShared<FJsonObject>();
    O->SetStringField(TEXT("pose"),Poses.IsValidIndex(ActivePose)?Poses[ActivePose].Name:FString());O->SetStringField(TEXT("pending"),Poses.IsValidIndex(PendingPose)?Poses[PendingPose].Name:FString());
    O->SetStringField(TEXT("state"),Current());O->SetStringField(TEXT("wanted"),Wanted);O->SetNumberField(TEXT("mode"),Mode);
    TArray<TSharedPtr<FJsonValue>> Q;for(const FString& S:Queue)Q.Add(MakeShared<FJsonValueString>(S));O->SetArrayField(TEXT("queue"),Q);
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
    Counts->SetBoolField(TEXT("raw"),bRaw);Counts->SetNumberField(TEXT("her_orgasms"),HerOrgasms);Counts->SetNumberField(TEXT("his_finishes"),HisFinishes);
    auto Out=MakeShared<FJsonObject>();Out->SetObjectField(TEXT("counts"),Counts);
    TArray<TSharedPtr<FJsonValue>> P,E;for(const FString& S:PosesPlayed)P.Add(MakeShared<FJsonValueString>(S));for(const FString& S:Events)E.Add(MakeShared<FJsonValueString>(S));
    Out->SetArrayField(TEXT("poses"),P);Out->SetArrayField(TEXT("events"),E);
    return Out;
}
