#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "KKCardTypes.h"
#include "KKCardWriter.h"
#include "KKModIndex.h"
#include "KKLocalMods.h"
#include "KKRustExtract.h"
#include "KKSpeech.h"
#include "Components/Button.h"
#include "OKCharacterController.generated.h"

class UOKCharacterController;
class AKKPreviewActor;
struct FOKAnimationMenuJob;
struct FOKClothingWarmJob;
struct FOKAppearanceCardJob;
struct FOKCharacterMCPState;
// States of one paired pose prepared in the background: the first state plays as soon as it is ready, the others
// follow on several threads, and a selected state that is not ready yet jumps the queue.
struct FOKPairProgress {
    FCriticalSection Lock;
    TArray<FString> Pending;
    TSet<FString> Ready,Failed;
    FThreadSafeBool bCancelled=false; // another pose was requested: stop (the finished states stay prepared)
    FThreadSafeBool bFinished=false;
    bool Pop(FString& Out){FScopeLock Guard(&Lock);if(Pending.IsEmpty())return false;Out=Pending[0];Pending.RemoveAt(0);return true;}
    void Finish(const FString& State,bool bOk){FScopeLock Guard(&Lock);(bOk?Ready:Failed).Add(State);}
    // 1 ready, -1 failed, 0 still pending or being prepared
    int32 StateOf(const FString& State){FScopeLock Guard(&Lock);return Ready.Contains(State)?1:Failed.Contains(State)?-1:0;}
    void Bump(const FString& State){FScopeLock Guard(&Lock);if(Pending.Remove(State))Pending.Insert(State,0);}
};
struct FOKReadyPair {
    FString Json,Label;
    TMap<FString,FString> FemaleStates,MaleStates,FemaleKeys,MaleKeys;
    TSharedPtr<FOKPairProgress,ESPMode::ThreadSafe> Progress; // null: every state prepared up front
};

UCLASS()
class OPENKOIKATSU_API UOKPartnerMaleButton : public UButton
{
	GENERATED_BODY()

public:
	int32 Index = INDEX_NONE;

	UPROPERTY()
	TObjectPtr<UOKCharacterController> Owner;

	UFUNCTION()
	void HandleClick();
};

UCLASS()
class OPENKOIKATSU_API UOKClothesStateButton : public UButton
{
	GENERATED_BODY()

public:
	// 0–8 cycles one garment. 100 wears everything, 101 half-removes the
	// four body garments, 102 takes all nine garment slots fully off.
	int32 Kind = INDEX_NONE;

	UPROPERTY()
	TObjectPtr<UOKCharacterController> Owner;

	UFUNCTION()
	void HandleClick();
};

class USlider;

class UButton;
class UCheckBox;
class UComboBoxString;
class UTextBlock;
class UImage;
class UEditableTextBox;
class UScrollBox;
class UVerticalBox;
class UHorizontalBox;
class UOKShapeSlider;
class FJsonObject;
class USizeBox;

UCLASS()
class OPENKOIKATSU_API UOKCharacterController : public UUserWidget
{
	GENERATED_BODY()

public:
	void TickCharacterMCP();
	void CancelCharacterMCP();
	TSharedPtr<FOKCharacterMCPState> CharacterMCP;
	virtual TSharedRef<SWidget> RebuildWidget() override;

	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	void BeginAnimationPreparation(int32 Kind, const FString& Item, ESelectInfo::Type Type);
	void LaunchAnimationPreparation();
	void FinishAnimationPreparation(TSharedRef<FOKAnimationMenuJob> Job);
	void ShowAnimationStatus(const FString& Text, bool bError = false);
	// "影片動作 名稱  t=1.23s / 10.37s  第 37 幀" while a video clip plays; empty otherwise.
	FString VideoClipTimeLabel() const;
	// Plays a user video motion (`video:|name`) from the video motion panel.
	bool PlayVideoMotion(const FString& Key, const FString& Label);
	// Two-person video motion: female clip on the female, male clip on the loaded
	// partner, same origin and clock. Returns the status text.
	FString PlayVideoPair(const FString& FemaleKey, const FString& MaleKey, const FString& Label);
	// {female key, male key, label} waiting for an auto-loaded partner.
	TArray<FString> PendingVideoPair;
	// The partner is playing the male half of a video pair (cleared by any other selection).
	bool bVideoPairActive = false;
	// Same-card reloads (clothes, outfit) keep the partner; 1 = resync video pair, 2 = restart native pair state.
	int32 PairRestoreKind = 0;
	void KeepPartnerForReload();
	void RestorePairAfterReload();
	UPROPERTY() TObjectPtr<class UOKVideoMotionPanel> VideoMotionPanel;
	double VideoClipLabelAt = 0.;
	void PopulateGameClips(const TArray<FString>& Clips, const FString& Selection = FString());
 void BuildAppearanceControls(class UVerticalBox* Parent);
 void BuildPropertyControls(class UVerticalBox* Parent);
 void RebuildPropertyControls();
 void SetEditorProperty(const FString& Key,TSharedPtr<FJsonValue> Value);
 void ResetEditorProperty(const FString& Key);
 UFUNCTION() void OnOpenProperties();
 UFUNCTION() void OnApplyProperties();
 UFUNCTION() void OnResetProperties();
 UFUNCTION() void OnPropertySearch(const FText& Value);
 UFUNCTION() void OnPropertyPrev();
 UFUNCTION() void OnPropertyNext();
 int32 PropertyPage=0;
 int32 PropertyMatches=0;
 UPROPERTY() TObjectPtr<class UTextBlock> PropertyPageText;
 UFUNCTION() void OnPropertyColorTarget(FString Option,ESelectInfo::Type SelectionType);
 UFUNCTION() void OnPropertyColorHex(const FText& Text,ETextCommit::Type CommitType);
 void RefreshPropertyColor();
 UPROPERTY() TObjectPtr<class UComboBoxString> PropertyColorTarget;
 UPROPERTY() TObjectPtr<class UEditableTextBox> PropertyColorHex;
 UPROPERTY() TObjectPtr<class UBorder> PropertyColorSwatch;
 TMap<FString,TArray<FString>> PropertyColorGroups;
 TMap<FString,FString> PropertyColorLabels;
 FString PropertyColorKey;
 UPROPERTY() TObjectPtr<class UVerticalBox> PropertyPanel;
 UPROPERTY() TObjectPtr<class UVerticalBox> PropertyRows;
 UPROPERTY() TObjectPtr<class UTextBlock> PropertyStatus;
 UPROPERTY() TObjectPtr<class UEditableTextBox> PropertySearch;
 TArray<TSharedPtr<FJsonObject>> EditorProperties;
 TArray<TSharedPtr<FJsonObject>> EditorPropertyParents;
 TMap<FString,TSharedPtr<FJsonValue>> PendingProperties;
 bool bPropertyBusy=false;
 FString PropertySource;
 int32 PropertyOutfit=INDEX_NONE;
 int32 PropertyGeneration=0;
 void BuildAppearancePanel();
 void RebuildAppearancePage();
 void LoadAppearanceThumbnails();
 void ApplyAppearanceItem(const FString& Key);
 void RunAppearanceRequest(TSharedRef<FJsonObject> Request,TFunction<void(UOKCharacterController*,TSharedPtr<FJsonObject>,const FString&)> Complete,TSharedPtr<FOKAppearanceCardJob,ESPMode::ThreadSafe> CardJob=nullptr);
 UFUNCTION() void OnOpenAppearance();
 UFUNCTION() void OnCloseAppearance();
 UFUNCTION() void OnAppearanceTarget(FString Value,ESelectInfo::Type Type);
 UFUNCTION() void OnAppearanceSearch(const FText& Value);
 UFUNCTION() void OnAppearancePrev();
 UFUNCTION() void OnAppearanceNext();
 UFUNCTION() void OnAppearanceRandom();
 UFUNCTION() void OnAppearanceReset();
 UFUNCTION() void OnAddAccessorySlot();
 void RefreshAppearanceCatalog(bool bSelectNewestSlot=false);
 UPROPERTY() TObjectPtr<class USizeBox> AppearancePanel;
 UPROPERTY() TObjectPtr<class UWrapBox> AppearanceGrid;
 UPROPERTY() TObjectPtr<class UComboBoxString> AppearanceTarget;
 UPROPERTY() TObjectPtr<class UEditableTextBox> AppearanceSearch;
 UPROPERTY() TObjectPtr<class UTextBlock> AppearanceStatus;
 UPROPERTY() TObjectPtr<class UTextBlock> AppearancePageText;
 UPROPERTY() TArray<TObjectPtr<class UOKAppearanceButton>> AppearanceButtons;
 UPROPERTY() TArray<TObjectPtr<class UTexture2D>> AppearanceTextures;
 TArray<TSharedPtr<FJsonObject>> AppearanceTargets,AppearanceItems;
 TSharedPtr<FJsonObject> AppearanceLastTiming;
 TArray<int32> AppearanceFiltered;
 TMap<FString,FString> AppearanceSelectedKeys;
 FString AppearanceRoot,AppearanceTargetKey;
 TSharedPtr<FOkAppearanceContext,ESPMode::ThreadSafe> AppearanceContext;
 FString AppearanceContextPath;
 int32 AppearanceContextGeneration=INDEX_NONE,AppearanceContextOutfit=INDEX_NONE;
 bool ApplyPreparedAppearance(const FString& Key);
 bool ApplyPreparedGeometry(const FString& Key);
 // Returns true when the requested action must wait for resource persistence.
 bool MaterializeAppearance(TFunction<void(UOKCharacterController*)> Complete);
 static FString PortableGallerySignature(const FString& CardPath);
 int32 AppearancePage=0,AppearancePageGeneration=0;
 bool bAppearanceBusy=false,bAppearanceThumbBusy=false;
 void BuildEditorDisplayControls(class UVerticalBox* Parent);
 void ApplyEditorDisplaySettings();
 void SaveEditorDisplaySettings();
 UFUNCTION() void OnEditorPanelWidthChanged(float Value);
 UFUNCTION() void OnEditorFontSizeChanged(float Value);
 UPROPERTY() TObjectPtr<class UScaleBox> EditorContentScale;
 UPROPERTY() TObjectPtr<class USpinBox> EditorWidthInput;
 UPROPERTY() TObjectPtr<class USpinBox> EditorFontInput;
 float EditorPanelWidth=360.f,EditorFontSize=16.f;
 bool bEditorDisplayDirty=false;
 double EditorDisplayChangedAt=0.;
	void BuildAnimationLookup();
	void AddDemoAnimations();
	void AddAnimationSourceClips(const FString& Source, const TArray<FString>& Clips);
	FString AnimationOptionForSelection() const;
	void ApplyStudioMotion(FName Row, const FString& Label);
	void ScanAndShow();
	void RefreshCardList();
 void TickCardListMetadata();
	void SelectIndex(int32 Index);
 void BuildBoneControls();
 void BuildPregnancyControls();
 void OnBodyPhysicsChanged(const FString& Parameter,float Value);
 void OnPregnancyChanged(const FString& Parameter,float Value);
 UFUNCTION() void OnPregnancyEnabled(bool Value);
 UFUNCTION() void OnPregnancyPreset(FString Value,ESelectInfo::Type Type);
 UPROPERTY() TObjectPtr<UCheckBox> PregnancyEnabled;
 UPROPERTY() TObjectPtr<UComboBoxString> PregnancyPreset;
 UPROPERTY() TObjectPtr<UVerticalBox> PregnancyBox;
 bool bEditorModsDirty=false;

 void ApplyPendingEditorMods();
 void SyncBoneSliders();
 void OnBoneChanged(UOKShapeSlider* Slider,float Value);
 FString EditorModsJson() const;
 bool CommitEditorMods(const TSharedPtr<FJsonObject>& Mods);
 UFUNCTION() void OnBoneCategoryChanged(FString Value,ESelectInfo::Type Type);
 UPROPERTY() TObjectPtr<UComboBoxString> BoneCategoryCombo;
 UPROPERTY() TObjectPtr<UCheckBox> BoneAxesCheck;
 UFUNCTION() void OnBoneAxesChanged(bool Value);
 UPROPERTY() TArray<TObjectPtr<UOKShapeSlider>> BoneSliders;
 bool bSyncingBoneControls=false;
 // Bone rows the current model cannot use, as last applied to ShapeBox (they are moved to the end).
 FString BoneUnusableOrder;



	UFUNCTION()
	void OnSave();
	UFUNCTION()
	void OnSaveApng();
	/** OK clothes (.okclothes, OKGalleryClothing.cpp): export the shown outfit, wear one as the shown outfit (a new
	 *  card in the save folder), or turn a game coordinate card into one. */
	UFUNCTION()
	void OnExportOkClothes();
	UFUNCTION()
	void OnWearOkClothes();
	UFUNCTION()
	void OnImportClothesCard();
	UFUNCTION()
	void OnOpenCardFolder();
	UFUNCTION()
	void OnUseGameCards();
	UFUNCTION()
	void OnCardSourceChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	FString CurrentCardDirectory;
	bool bCardDirectoryInitialized=false;
	UPROPERTY()
	TObjectPtr<UComboBoxString> CardSourceCombo;
	UFUNCTION()
	void OnEditShapes();

	UFUNCTION()
	void OnLoad3D();
	void StartLoad3D(bool bAppearanceGeometryOnly);
	int32 AppearanceGeometryLoadGeneration = INDEX_NONE;

	UFUNCTION()
	void OnBackToGallery();
	UFUNCTION() void OnEnterInteraction();
	// Studio scene (Docs/STUDIO_SCENE.md): timeline keys, guide object and video import on the loaded characters.
	UFUNCTION() void OnEnterStudio();
	void OnExitStudio();
	// OK Studio (Plugins/OKStudio): the scene tree with the loaded card as its first character; the gallery returns when it closes.
	UFUNCTION() void OnEnterOkStudio();
	void OnOkStudioStopped();
	bool bOkStudioOpen = false;
	ESlateVisibility VisibilityBeforeOkStudio = ESlateVisibility::Visible;
	UPROPERTY() TObjectPtr<class UOKStudioWidget> StudioWidget;
	// While the studio is open the partner belongs to the studio: stopping a pair keeps it.
	bool bStudioMode = false;
	// Card file of the loaded PartnerActor (studio scenes record it).
	FString PartnerCardPath;
	UFUNCTION() void OnExitInteraction();
    // Shared interaction services; no card browser is constructed by this class.
    FString InteractionReturnLabel=TEXT("返回人物編輯器");
    TFunction<void()> InteractionReturned;
    // What the player chose in the interaction scene (pair, state, clip, scene, motion) with the seconds since the
    // scene opened: returned with the interaction's result so her memory keeps the whole session (StreetSocial).
    TArray<TSharedPtr<FJsonValue>> ActionLog; double ActionLogStart=0;
    void NoteAction(const TCHAR* Kind,const FString& Label){
        if(Label.IsEmpty())return;const double Now=FPlatformTime::Seconds();if(ActionLog.IsEmpty()&&ActionLogStart<=0)ActionLogStart=Now;
        TSharedRef<FJsonObject> O=MakeShared<FJsonObject>();O->SetStringField(TEXT("kind"),Kind);O->SetStringField(TEXT("label"),Label);O->SetNumberField(TEXT("seconds"),FMath::RoundToDouble(Now-ActionLogStart));
        ActionLog.Add(MakeShared<FJsonValueObject>(O));
    }
    bool bInteractionOnly=false;
    void InitializeInteractionControls(bool Bind=true);
    void BindInteractionControls();
    void ActivateInteraction();
	UPROPERTY() TObjectPtr<class UOKInteractionWidget> InteractionWidget;
	UPROPERTY() TObjectPtr<class AOKInteractionScene> InteractionScene;
 bool bInteractionPreviousPoseSharing=false;
	bool bEnterInteractionAfterLoad=false;

	void FinishLoad3D(int32 Generation, FOkSession Session, bool bOk, const FString& Err, const FString& PreparedMods, FVector2D PreparedPhysics, const FString& PreparedPairs);
	void TickPreviewPreparation();
	void FinishPreparedPreview(AKKPreviewActor* Actor, bool bOk, const FString& Err);
	UPROPERTY(Transient) TObjectPtr<AKKPreviewActor> PreparingPreview;
	int32 PreparingPreviewGeneration = 0;
	FString PendingLoadPairJson;
	int32 PendingLoadPairGeneration = INDEX_NONE;

	UFUNCTION()
	void OnOutfitPrev();

	UFUNCTION()
	void OnOutfitNext();

	UFUNCTION()
	void OnOutfitSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

	void RefreshOutfitList();

	void OnShapeChanged(bool bBody, int32 Index, float Value);
	void ResetShape(bool bBody, int32 Index);
	UFUNCTION()
	void OnResetAllShapes();
	void OnInteractionBellyChanged(float Value);
	private:
	void ApplyShapeValue(bool bBody, int32 Index, float Value);
	public:
	void OnGameAnimationParameter(const FString& Name, float Value);
	void RefreshGameAnimationParameters();
    void SyncGameAnimationSelection(const FString& Key);
	UPROPERTY()
	TObjectPtr<UVerticalBox> GameAnimationParameters;

	UFUNCTION()
	void OnMouthChanged(float NewValue);
	UFUNCTION()
	void OnMouthPatternSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void OnLoadSpeech();
	UFUNCTION() void OnSpeak();
	UFUNCTION() void OnStopSpeech();
	UFUNCTION() void OnSpeechVoiceSelected(FString Item, ESelectInfo::Type Type);
	void SyncSpeechVoiceToCard();
	static FString HeroineVoiceLabel(int32 Personality);
	UPROPERTY() TObjectPtr<UEditableTextBox> SpeechRootInput;
	UPROPERTY() TObjectPtr<UComboBoxString> SpeechVoiceCombo;
	UPROPERTY() TObjectPtr<UComboBoxString> SpeechCombo;
	UPROPERTY() TObjectPtr<UTextBlock> SpeechStatus;
	TArray<FString> SpeechIds;
	// -1 follows the loaded card. 0–39 is an explicit heroine voice (c00–c39).
	int32 SelectedSpeechVoice=-1;
	bool bSuppressSpeechVoice=false;
    bool bSpeechWasPlaying=false;
	UFUNCTION()
	void OnEyesPattern(FString Item, ESelectInfo::Type Type);
	UFUNCTION()
	void OnBrowPattern(FString Item, ESelectInfo::Type Type);
	UFUNCTION()
	void OnEyesOpen(float Value);
	UFUNCTION()
	void OnBrowOpen(float Value);
	void RefreshFaceControls();
	void UpdateFaceControl(int32 Index, bool bBlend = false);
	UFUNCTION()
	void OnAutomaticBlink(bool bEnabled);
	UFUNCTION()
	void OnForceBlink();
	UFUNCTION()
	void OnGazeLeft(float Value);
	UFUNCTION()
	void OnGazeRight(float Value);
	UFUNCTION()
	void OnGazeVertical(float Value);
	void UpdateGaze();
	UFUNCTION()
	void OnEyeLookCamera(bool Enabled);
	UPROPERTY()
	TObjectPtr<class UCheckBox> EyeLookCameraCheck;
	UPROPERTY()
	TArray<TObjectPtr<USlider>> GazeSliders;
	UPROPERTY()
	TObjectPtr<UCheckBox> AutomaticBlinkCheck;
	UFUNCTION()
	void OnLeftHandA(FString Item, ESelectInfo::Type Type);
	UFUNCTION()
	void OnLeftHandB(FString Item, ESelectInfo::Type Type);
	UFUNCTION()
	void OnRightHandA(FString Item, ESelectInfo::Type Type);
	UFUNCTION()
	void OnRightHandB(FString Item, ESelectInfo::Type Type);
	UFUNCTION()
	void OnLeftHandBlend(float Value);
	UFUNCTION()
	void OnRightHandBlend(float Value);
    UFUNCTION() void OnOpenHandControls();
    UFUNCTION() void OnResetLeftHand();
    UFUNCTION() void OnResetRightHand();
    UPROPERTY() TObjectPtr<class UScrollBox> HandMotionScroll;
	void RefreshHandControls();
	void ApplyHandControl(int32 Side);
    UFUNCTION() void OnPairSpeed(float Value);
    UFUNCTION() void OnPausePlayback();
    UFUNCTION() void OnReplayPlayback();
    UFUNCTION() void OnLoadPartner();
    void LoadPartnerFromPath(const FString& Path);
    int32 PartnerLoadGeneration=0;
    bool bPartnerLoadInFlight=false;
    int32 PartnerLoadsPending=0;
    FString PendingPartnerPath;
    void RefreshPartnerMales();
    void RebuildPartnerMaleButtons();
    void SelectPartnerMale(int32 Index);
    void ShowPairStatus(const FString& Text, bool bOnScreen);
    UFUNCTION() void OnPartnerMaleSelected(FString Item, ESelectInfo::Type Type);
    UFUNCTION() void OnPairPosePrev();
    UFUNCTION() void OnPairPoseNext();
    void StepPairPose(int32 Delta);
    void TryStartPreparedPair();
    UFUNCTION() void OnStopPair();
    UFUNCTION() void OnPenisVisible(bool bEnabled);
    void ApplyPenisVisible();
    void OnClothesState(int32 Kind);
    void RefreshClothesStateLabels();
    uint8 ClothesStates[9] = {};
    uint8 LoadedClothesStates[9] = {};
    UPROPERTY() TMap<FString,TObjectPtr<class AKKPreviewActor>> ClothingPreviews;
    TArray<FString> ClothingPreviewOrder,ClothingWarmQueue;
    TMap<FString,FString> ClothingPreviewMods;
    FString ClothingPreviewContext;
    int32 ClothingWarmGeneration=0;
    bool bClothingWarmInFlight=false;
    TSharedPtr<FOKClothingWarmJob,ESPMode::ThreadSafe> ClothingWarmReady;
    bool bClothingRulesPending=false;
    double LastClothingSwitchMs=0.;
    bool bLastClothingSwitchCached=false;
    void ResetClothingPreviews(const FString& Context=FString());
    void CacheClothingPreview(AKKPreviewActor* Actor,const FString& Key);
    bool TryClothingPreview(const FString& Key);
    void WarmClothingPreviews();
    void TickClothingPreviews();
    // The next card in the list, opened and built hidden while the reader is idle (OK_PRELOAD_NEIGHBOR=1), so
    // loading it only swaps the display (OKGalleryNeighbor.cpp).
    UPROPERTY() TObjectPtr<class AKKPreviewActor> NeighborActor;
    TSharedPtr<struct FOKNeighborJob,ESPMode::ThreadSafe> NeighborJob;
    FString NeighborPath;
    int32 NeighborGeneration=0;
    bool bNeighborInFlight=false;
    void ResetNeighborPreload();
    void TickNeighborPreload();
    bool TryNeighborPreload();
    // The neighbor is being prepared: clothing previews of the shown card wait for it.
    bool NeighborPreloadPending() const;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> ClothesStateLabels;
    UPROPERTY() TArray<TObjectPtr<UButton>> ClothesStateButtons;
    void StartPairBreath();
    int32 PartnerPersonality=-1;
    float PartnerVoiceRate=.5f;
    bool bPairBreathArmed=false,bPairBreathMoving=false;
    TArray<FKKHBreathCue> PairMovingBreaths,PairStillBreaths;
    UFUNCTION() void OnPairSelected(FString Item,ESelectInfo::Type Type);
    UFUNCTION() void OnPairStateSelected(FString Item,ESelectInfo::Type Type);
    void RefreshPairs();
    virtual void NativeDestruct() override;
    UPROPERTY() TObjectPtr<class AKKPreviewActor> PartnerActor;
    UPROPERTY() TObjectPtr<class AKKPairedPlayback> PairedPlayback;
    UPROPERTY() TObjectPtr<UComboBoxString> PartnerMaleCombo;
    UPROPERTY() TObjectPtr<UVerticalBox> PartnerMaleList;
    UPROPERTY() TObjectPtr<UTextBlock> PairStatus;
    TArray<FString> PartnerMalePaths;
    TArray<FString> PartnerMaleLabels;
    int32 SelectedPartnerMale = INDEX_NONE;
    bool bSuppressPartnerMale = false;
    UPROPERTY() TObjectPtr<USlider> PairSpeedSlider;
    // Runtime switch of the hair/cloth/breast solver (OKPhysXSecondary CVars): Verlet, PhysX, KawaiiPhysics, Chaos.
    UPROPERTY() TObjectPtr<UComboBoxString> PhysicsBackendCombo;
    void FillPhysicsBackendCombo();
    UFUNCTION() void OnPhysicsBackendSelected(FString Item,ESelectInfo::Type Type);
    // Hair/cloth hardness (OKPhysXSecondary.Hardness, 1 = softest .. 10 = as authored).
    UPROPERTY() TObjectPtr<USlider> PhysicsSoftnessSlider;
    UFUNCTION() void OnPhysicsSoftness(float Value);
    // Hair/cloth weight (OKPhysXSecondary.Weight, 1 = the card's gravity .. 10 = plus full real gravity).
    UPROPERTY() TObjectPtr<USlider> PhysicsWeightSlider;
    UFUNCTION() void OnPhysicsWeight(float Value);
    // Tail springiness (OKPhysXSecondary.TailBounce, 0 = as authored .. 1 default .. 2 springiest).
    UPROPERTY() TObjectPtr<USlider> PhysicsTailSlider;
    UFUNCTION() void OnPhysicsTail(float Value);
    UPROPERTY() TObjectPtr<UComboBoxString> PairCategoryCombo;
    UPROPERTY() TObjectPtr<UComboBoxString> PairCombo;
    TArray<int32> PairVisibleIndices;
    int32 SelectedPairCatalogueIndex() const;
    UFUNCTION() void OnPairCategorySelected(FString Item,ESelectInfo::Type Type);
    void PreparePairSelection(int32 Index);
    UPROPERTY() TObjectPtr<UComboBoxString> PairStateCombo;
    UPROPERTY() TObjectPtr<UCheckBox> PenisCheck;
    TArray<TSharedPtr<class FJsonObject>> NativePairs;
    FString ActiveBodyPairKey;
    TArray<TSharedPtr<FOKReadyPair,ESPMode::ThreadSafe>> ReadyPairs;
    TSharedPtr<FThreadSafeBool,ESPMode::ThreadSafe> PairCatalogueCancelled;
    TSharedPtr<FThreadSafeBool,ESPMode::ThreadSafe> PairCatalogueFinished;
    TSharedPtr<FOKPairProgress,ESPMode::ThreadSafe> ActivePairProgress; // background completion of the selected pose
    int32 PairCatalogueGeneration=0;
    bool bPairCataloguePreparing=false;
    TArray<FString> PairCatalogueJsonRows,PairCatalogueLabels;
    int32 PairCatalogueProcessed=0;
    UPROPERTY() TObjectPtr<UTextBlock> PairPreparationStatus;
    void CancelPairCatalogue();
    void PreparePairCatalogue(const TArray<TSharedPtr<FJsonObject>>& Rows,const TArray<FString>& Labels);
    void ResumePairCatalogue();
    void RefreshInteractionMotionSources();
    TMap<FString,FString> InteractionMotionSources;
    double PairRequestAt=0.,LastPairSwitchMs=0.;
    TMap<FString,FString> FemalePairStates,MalePairStates;
    bool bSuppressPairControls=false;
    int32 PairPrepareGeneration=0;
    bool bPairCataloguePrepared=false;
    bool bPairClipPrepared=false;
    bool bPairPreparing=false;
    bool bPairAutoStartPending=false;
    void CancelPairPreparation();
	UFUNCTION()
	void OnGameClipSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void OnGameSourceSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void OnGameSceneSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void OnLoadGameSource();
	UFUNCTION()
	void OnStudioCategorySelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void OnStudioMotionSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	void RefreshStudioCatalog();

	UFUNCTION()
	void OnPoseTpose();

	UFUNCTION()
	void OnPoseWalk();

	UPROPERTY()
	TObjectPtr<UHorizontalBox> RootBox;

	UPROPERTY() TObjectPtr<class UOKCardListView> CardListView;
	UPROPERTY() TObjectPtr<UTextBlock> CardCountLabel;
 // Working appearance cards must not replace the identity of a listed source card.
 TMap<int32, TPair<FString, bool>> CardSources;
 void SetWorkingCardPath(const FString& Path);
 int32 CardListGeneration=0;
 bool bCardMetadataBusy=false;
 TSet<int32> CardMetadataAttempts;

	UPROPERTY()
	TObjectPtr<UTextBlock> InspectorText;

	UPROPERTY()
	TObjectPtr<UImage> PreviewImage;
	UPROPERTY() TArray<TObjectPtr<class UTexture2D>> AnimatedPreviewTextures;
	TArray<double> AnimatedPreviewDurations;
	double AnimatedPreviewElapsed = 0;
	uint32 AnimatedPreviewPlays = 0;
	// APNG preview frames after the first: decoded on a worker (the first frame shows for 3 s on OK cards);
	// the generation drops a decode that finishes after another card was selected.
	int32 AnimatedPreviewGeneration = 0;
	TArray<TArray<uint8>> PendingPreviewFrames;
	void DecodeRemainingPreviewFramesNow();
	int32 AnimatedPreviewFrame = INDEX_NONE;
	bool bApngCaptureInFlight = false;
	// OnSave re-entered after the OK card APNG views were captured.
	bool bOkCardPreviewCaptured = false;
	// Puts back the clip / Studio animation / walk the preview had before a T-pose capture.
	void RestoreAnimationAfterCapture(int32 PoseBefore);
 bool bPortableSaveInFlight = false;
 void FinishPortableSave(const FString& Source,const FString& Output,const FString& Message,bool CopyStates);
 void FinishOkCardSave(const FString& Output,const FString& Message);
 static bool OKPngHasChunk(const FString& Path,const ANSICHAR* Type);
	FString PreviewCardPath;
	FString PreviewResourceSignature,PendingPreviewResourceSignature;
	int32 PreviewOutfitIndex = INDEX_NONE;

	UPROPERTY()
	TObjectPtr<UEditableTextBox> NicknameBox;
	UPROPERTY()
	TObjectPtr<UEditableTextBox> CardDirectoryInput;
	UPROPERTY()
	TObjectPtr<UEditableTextBox> OutputDirectoryInput;
	UPROPERTY()
	TObjectPtr<UCheckBox> CopyZipmodsCheck;
	bool bIndependentCards = false;
	bool bEditShapes = false;
	FKKLocalModIndex LocalMods;

	TArray<FKKCardListEntry> Entries;
	FKKCharacterFile Selected;
	FKKModIndex ModIndex;
	int32 SelectedIndex = INDEX_NONE;
	int32 OutfitIndex = 0;
	int32 LoadGeneration = 0;
	bool bLoadInFlight = false;
	// OK_LOAD_PROFILE: wall time of each busy load; OK_AUTO_SEQUENCE drives timed card/outfit steps.
	double LoadBusyAt = 0;
	bool bAnimationCatalogPrewarmed = false;
	bool bPreviewParentsPrewarmed = false;
	bool bSharedClipsPrewarmed = false;
	TArray<FString> AutoSequence;
	bool bAutoSequenceInit = false;
	double AutoSequenceAt = 0;
	void TickAutoSequence();

	UPROPERTY()
	TObjectPtr<class AKKPreviewActor> PreviewActor;

	TObjectPtr<class AKKPreviewActor> StalePreview;

	UPROPERTY()
	TObjectPtr<UTextBlock> OutfitLabel;

	UPROPERTY()
	TObjectPtr<UComboBoxString> OutfitCombo;
	UPROPERTY()
	TObjectPtr<UHorizontalBox> OutfitNav;
	UPROPERTY()
	TObjectPtr<UComboBoxString> GameClipCombo;
	UPROPERTY()
	TObjectPtr<UComboBoxString> GameSourceCombo;
	UPROPERTY()
	TObjectPtr<UTextBlock> AnimationStatus;
	UPROPERTY()
	TObjectPtr<UComboBoxString> GameSceneCombo;
	UPROPERTY()
	TObjectPtr<UEditableTextBox> GameSourceInput;
	UPROPERTY()
	TObjectPtr<UComboBoxString> MouthPatternCombo;
	UPROPERTY()
	TObjectPtr<class USlider> MouthOpenSlider;
	UPROPERTY()
	TObjectPtr<class UKKAnimationCatalog> AnimationCatalog;
	UPROPERTY()
	TObjectPtr<UComboBoxString> StudioCategoryCombo;
	UPROPERTY()
	TObjectPtr<UComboBoxString> StudioMotionCombo;
	TMap<FString, TArray<FName>> StudioCategoryRows;
	TArray<FName> StudioMotionRows;
	TMap<FString,FName> GameMotionRows;
	TMap<FString,FString> GameRawClipKeys;
	TMap<FString,FString> GameClipLabels;
	TArray<FString> DemoAnimationOptions;
	int32 AnimationLookupGeneration = 0;
	bool bAnimationLookupBuilding = false;
	TArray<FString> GameMotionOptions;
 double LastAnimationCommitMs=0.;
 int32 AnimationPrepareGeneration=0;
 bool bAnimationPreparing=false,bAnimationPrepared=false;
 int32 PendingAnimationKind=-1;
 FString PendingAnimationItem;
 ESelectInfo::Type PendingAnimationType=ESelectInfo::Direct;
 TArray<FString> PreparedAnimationNames,PreparedAnimationStateNames;
	bool bSuppressClipCombo = false;
	FString SelectedGameClip;
	FString SelectedGameSource;
	FName SelectedStudioRow = NAME_None;
	FVector SavedStudioOptions = FVector::ZeroVector;
	int32 SelectedGameSceneMode = 0;
	int32 SelectedMouthPattern = 0;
	float SelectedMouthOpen = 0.f;
	UPROPERTY()
	TArray<TObjectPtr<UComboBoxString>> FaceCombos;
	UPROPERTY()
	TArray<TObjectPtr<class USlider>> FaceOpenSliders;
	int32 SelectedFacePattern[2] = {0, 0};
	float SelectedFaceOpen[2] = {-1.f, -1.f}; // -1 uses the loaded card's default.
	bool bSuppressFaceControls = false;
	UPROPERTY()
	TArray<TObjectPtr<UComboBoxString>> HandCombos;
	UPROPERTY()
	TArray<TObjectPtr<class USlider>> HandBlendSliders;
	int32 SelectedHandA[2] = {-1, -1}; // -1 follows the current animation.
	int32 SelectedHandB[2] = {0, 0};
	float SelectedHandBlend[2] = {0.f, 0.f};
	bool bSuppressHandControls = false;

	bool bSuppressOutfitCombo = false;

	UPROPERTY()
	TObjectPtr<UScrollBox> ShapeScroll;
	UPROPERTY()
	TObjectPtr<USizeBox> ShapePanel;
	UPROPERTY()
	TObjectPtr<USizeBox> MotionPanel;
	UPROPERTY()
	TObjectPtr<USizeBox> PreviewSpace;
	bool bModelLayout = false;
	void SetModelLayout(bool bEnabled);

	UPROPERTY()
	TObjectPtr<UVerticalBox> ShapeBox;

	UPROPERTY()
	TArray<TObjectPtr<UOKShapeSlider>> ShapeSliders;
 UPROPERTY() TArray<TObjectPtr<UWidget>> ShapeSeparators;

	UPROPERTY()
	TObjectPtr<UVerticalBox> CardCol;

	UPROPERTY()
	TObjectPtr<UVerticalBox> InspectCol;

	UPROPERTY()
	TObjectPtr<UButton> Load3DButton;

	UPROPERTY()
	TObjectPtr<UButton> ShapeLoadButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> Load3DLabel;

	void SyncShapeSliders();
	void SetLoadBusy(bool bBusy);
	void ApplyClickInputMode();
};
