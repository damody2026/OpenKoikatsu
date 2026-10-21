#include "OKCharacterController.h"
#include "KKPreviewActor.h"
#include "OKComboBox.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/ComboBoxString.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "HAL/IConsoleManager.h"

TSharedRef<SWidget> UOKCharacterController::RebuildWidget(){
    if(!WidgetTree->RootWidget)InitializeInteractionControls();
    return Super::RebuildWidget();
}
void UOKCharacterController::InitializeInteractionControls(bool Bind){
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();if(!WidgetTree->RootWidget)WidgetTree->RootWidget=Root;
    auto Combo=[&](){auto* W=WidgetTree->ConstructWidget<UOKComboBoxString>();Root->AddChild(W);return W;};
    auto Text=[&](){auto* W=WidgetTree->ConstructWidget<UTextBlock>();Root->AddChild(W);return W;};
    EyeLookCameraCheck=WidgetTree->ConstructWidget<UCheckBox>();Root->AddChild(EyeLookCameraCheck);
    MouthPatternCombo=Combo();MouthOpenSlider=WidgetTree->ConstructWidget<USlider>();Root->AddChild(MouthOpenSlider);
    for(int32 I=0;I<54;++I)MouthPatternCombo->AddOption(FString::Printf(TEXT("嘴型 %d"),I));
    MouthPatternCombo->SetSelectedIndex(SelectedMouthPattern);MouthOpenSlider->SetValue(SelectedMouthOpen);
    GameSourceCombo=Combo();GameClipCombo=Combo();GameSceneCombo=Combo();StudioCategoryCombo=Combo();StudioMotionCombo=Combo();AnimationStatus=Text();
    PartnerMaleCombo=Combo();PairCategoryCombo=Combo();PairCombo=Combo();PairStateCombo=Combo();PairSpeedSlider=WidgetTree->ConstructWidget<USlider>();Root->AddChild(PairSpeedSlider);PairSpeedSlider->SetValue(.25f);PairStatus=Text();PairPreparationStatus=Text();
    PhysicsBackendCombo=Combo();FillPhysicsBackendCombo();
    PhysicsSoftnessSlider=WidgetTree->ConstructWidget<USlider>();Root->AddChild(PhysicsSoftnessSlider);
    PhysicsTailSlider=WidgetTree->ConstructWidget<USlider>();Root->AddChild(PhysicsTailSlider);if(IConsoleVariable* Tail=IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.TailBounce"))){PhysicsTailSlider->SetMinValue(0.f);PhysicsTailSlider->SetMaxValue(2.f);PhysicsTailSlider->SetStepSize(.1f);PhysicsTailSlider->SetValue(Tail->GetFloat());}PhysicsTailSlider->OnValueChanged.AddDynamic(this,&UOKCharacterController::OnPhysicsTail);
    PhysicsWeightSlider=WidgetTree->ConstructWidget<USlider>();Root->AddChild(PhysicsWeightSlider);if(IConsoleVariable* W=IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.Weight"))){PhysicsWeightSlider->SetMinValue(1.f);PhysicsWeightSlider->SetMaxValue(10.f);PhysicsWeightSlider->SetStepSize(1.f);PhysicsWeightSlider->SetValue(W->GetFloat());}
    if(IConsoleVariable* Soft=IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.Hardness"))){PhysicsSoftnessSlider->SetMinValue(1.f);PhysicsSoftnessSlider->SetMaxValue(10.f);PhysicsSoftnessSlider->SetStepSize(1.f);PhysicsSoftnessSlider->SetValue(Soft->GetFloat());}
    SpeechRootInput=WidgetTree->ConstructWidget<UEditableTextBox>();Root->AddChild(SpeechRootInput);SpeechRootInput->SetText(FText::FromString(TEXT("C:/portable/KoikatuSunshine/voice_extract")));
    SpeechVoiceCombo=Combo();SpeechCombo=Combo();SpeechStatus=Text();
    SpeechVoiceCombo->AddOption(TEXT("跟隨角色聲線"));for(int32 I=0;I<40;++I)SpeechVoiceCombo->AddOption(HeroineVoiceLabel(I));SpeechVoiceCombo->SetSelectedIndex(0);
    RefreshPartnerMales();
    if(Bind)BindInteractionControls();
}

void UOKCharacterController::BindInteractionControls(){
    EyeLookCameraCheck->OnCheckStateChanged.AddDynamic(this,&UOKCharacterController::OnEyeLookCamera);
    MouthPatternCombo->OnSelectionChanged.AddDynamic(this,&UOKCharacterController::OnMouthPatternSelected);
    MouthOpenSlider->OnValueChanged.AddDynamic(this,&UOKCharacterController::OnMouthChanged);
    PairCategoryCombo->OnSelectionChanged.AddDynamic(this,&UOKCharacterController::OnPairCategorySelected);
    PairCombo->OnSelectionChanged.AddDynamic(this,&UOKCharacterController::OnPairSelected);
    PairStateCombo->OnSelectionChanged.AddDynamic(this,&UOKCharacterController::OnPairStateSelected);
    PairSpeedSlider->OnValueChanged.AddDynamic(this,&UOKCharacterController::OnPairSpeed);
    PartnerMaleCombo->OnSelectionChanged.AddDynamic(this,&UOKCharacterController::OnPartnerMaleSelected);
    GameSourceCombo->OnSelectionChanged.AddDynamic(this,&UOKCharacterController::OnGameSourceSelected);
    GameClipCombo->OnSelectionChanged.AddDynamic(this,&UOKCharacterController::OnGameClipSelected);
    SpeechVoiceCombo->OnSelectionChanged.AddDynamic(this,&UOKCharacterController::OnSpeechVoiceSelected);
    PhysicsBackendCombo->OnSelectionChanged.AddDynamic(this,&UOKCharacterController::OnPhysicsBackendSelected);
    PhysicsSoftnessSlider->OnValueChanged.AddDynamic(this,&UOKCharacterController::OnPhysicsSoftness);
    PhysicsWeightSlider->OnValueChanged.AddDynamic(this,&UOKCharacterController::OnPhysicsWeight);

}

namespace
{
    // Menu option -> OKPhysXSecondary.Backend (-1 = per component, -2 = the built-in Verlet: OKPhysXSecondary.Enabled 0).
    // Backend 3 (Bullet) is not shipped; its number stays reserved, so the menu skips it.
    struct FOKPhysicsBackendOption{const TCHAR* Label;int32 Backend;};
    // Jolt and MuJoCo are comparison backends, not in Shipping builds (OKPhysXSecondary.Build.cs).
    const FOKPhysicsBackendOption PhysicsBackendOptions[]={{TEXT("物理：依元件（卡片／預設）"),-1},{TEXT("物理：原始 Verlet"),-2},{TEXT("物理：原作 DynamicBone＋碰撞"),6},{TEXT("物理：PhysX 5.11"),0},{TEXT("物理：KawaiiPhysics"),1},{TEXT("物理：Chaos PBD"),2}
#if !UE_BUILD_SHIPPING
        ,{TEXT("物理：Jolt 5.6"),4},{TEXT("物理：MuJoCo 3.15"),5}
#endif
    };
}
void UOKCharacterController::FillPhysicsBackendCombo(){
    if(!PhysicsBackendCombo)return;
    PhysicsBackendCombo->ClearOptions();
    for(const FOKPhysicsBackendOption& Option:PhysicsBackendOptions)PhysicsBackendCombo->AddOption(Option.Label);
    IConsoleVariable* Enabled=IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.Enabled"));
    IConsoleVariable* Backend=IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.Backend"));
    const int32 Current=(!Enabled||!Enabled->GetBool())?-2:(Backend?Backend->GetInt():-1);
    int32 Index=3;// an unknown or reserved number runs as PhysX (OKPhysXSecondary falls back to 0)
    for(int32 I=0;I<UE_ARRAY_COUNT(PhysicsBackendOptions);++I)if(PhysicsBackendOptions[I].Backend==Current)Index=I;
    PhysicsBackendCombo->SetSelectedIndex(Index);
    PhysicsBackendCombo->SetToolTipText(FText::FromString(TEXT("頭髮／衣物／胸部的物理解算器，執行中即時切換（OKPhysXSecondary.Enabled / Backend）")));
}
void UOKCharacterController::OnPhysicsTail(float Value){
    if(!FMath::IsFinite(Value))return;
    if(IConsoleVariable* Tail=IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.TailBounce")))Tail->Set(FMath::Clamp(FMath::RoundToFloat(Value*10.f)/10.f,0.f,2.f),ECVF_SetByConsole);
}
void UOKCharacterController::OnPhysicsWeight(float Value){
    if(!FMath::IsFinite(Value))return;
    if(IConsoleVariable* W=IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.Weight")))W->Set(FMath::Clamp(FMath::RoundToFloat(Value),1.f,10.f),ECVF_SetByConsole);
}
void UOKCharacterController::OnPhysicsSoftness(float Value){
    if(!FMath::IsFinite(Value))return;
    if(IConsoleVariable* Hard=IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.Hardness")))Hard->Set(FMath::Clamp(FMath::RoundToFloat(Value),1.f,10.f),ECVF_SetByConsole);
}
void UOKCharacterController::OnPhysicsBackendSelected(FString Item,ESelectInfo::Type Type){
    if(Type==ESelectInfo::Direct||!PhysicsBackendCombo)return;
    const int32 Index=PhysicsBackendCombo->FindOptionIndex(Item);
    if(Index<0||Index>=UE_ARRAY_COUNT(PhysicsBackendOptions))return;
    IConsoleVariable* Enabled=IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.Enabled"));
    IConsoleVariable* Backend=IConsoleManager::Get().FindConsoleVariable(TEXT("OKPhysXSecondary.Backend"));
    if(!Enabled||!Backend)return;
    // SetByConsole: same priority as -ExecCmds / the console, so the menu always wins over earlier settings.
    const int32 Chosen=PhysicsBackendOptions[Index].Backend;
    if(Chosen>=-1)Backend->Set(Chosen,ECVF_SetByConsole);
    Enabled->Set(Chosen>=-1?1:0,ECVF_SetByConsole);
    // The CVars only reach a character whose session allows the host solver. Arm the ones this menu is showing.
    if(IsValid(PreviewActor))PreviewActor->UseGalleryPhysics();
    if(IsValid(PartnerActor))PartnerActor->UseGalleryPhysics();
    UE_LOG(LogTemp,Display,TEXT("Gallery physics backend %d (%s)"),Chosen,*Item);
}
