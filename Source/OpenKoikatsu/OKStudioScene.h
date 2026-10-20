#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OKStudioScene.generated.h"

class AKKPreviewActor;
class UButton;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class UOKCharacterController;
class UOKMotionComboBox;
class UOKVideoMotionPanel;
class UPanelWidget;
class UPanelSlot;
class USlider;
class UTextBlock;
class UVerticalBox;
class UWidget;
class FJsonObject;

// Studio scene modelled on the original CharaStudio (Docs/STUDIO_SCENE.md):
// workspace list, guide object (move / rotate / scale), per-object motion,
// a keyframe timeline for each character's transform, video motion import,
// and scene files under Saved/Studio.

// Timeline layout (pixels) and key rate (video clips are 30 fps).
namespace OKStudio { constexpr float Gutter = 76.f, Ruler = 20.f, Row = 24.f, Fps = 30.f; }

struct FOKStudioKey
{
	float Time = 0.f;
	FVector Pos = FVector::ZeroVector;
	FQuat Rot = FQuat::Identity;
	FVector Scale = FVector::OneVector;
	uint8 Interp = 0;  // to the next key: 0 linear, 1 smooth, 2 constant
};

struct FOKStudioTrack
{
	TArray<FOKStudioKey> Keys;  // sorted by Time
	float Speed = 1.f;
	FString MaleClip;  // partner motion key (the main character's motion lives in the editor's selection)
	bool Evaluate(float Time, FTransform& Out) const;
	int32 KeyAt(float Time, float Tolerance) const;
};

// Paint-only overlay: floor grid and the guide object of the selected character.
UCLASS()
class OPENKOIKATSU_API UOKStudioGuide : public UUserWidget
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<class UOKStudioWidget> Studio;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
};

// Key rows per character with a playhead; click / drag scrubs, drag a key to retime it.
UCLASS()
class OPENKOIKATSU_API UOKStudioTimeline : public UUserWidget
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<class UOKStudioWidget> Studio;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
private:
	float TimeAt(const FGeometry& Geometry, const FVector2D& Screen) const;
	int32 DragTrack = INDEX_NONE, DragKey = INDEX_NONE;
	bool bScrubbing = false;
};

USTRUCT()
struct FOKStudioBorrowed
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UPanelWidget> Parent;
	UPROPERTY() TArray<TObjectPtr<UWidget>> Children;
	UPROPERTY() TArray<TObjectPtr<UPanelSlot>> Slots;
};

UCLASS()
class OPENKOIKATSU_API UOKStudioWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	static UOKStudioWidget* Open(UOKCharacterController* Gallery);
	void Close();

	UPROPERTY() TObjectPtr<UOKCharacterController> Gallery;

	// Objects: 0 = the editor's character (female), 1 = the loaded partner.
	AKKPreviewActor* ObjectActor(int32 Index) const;
	FString ObjectName(int32 Index) const;
	FOKStudioTrack Tracks[2];
	int32 Selected = 0;

	float Time = 0.f, Length = 10.f;
	bool bPlaying = false;
	void SetTime(float NewTime);
	void SetPlaying(bool bPlay);

	// Guide object. Axis 0..2 = X / Y(up) / Z in the original's coordinates, 3 = uniform scale.
	int32 GuideMode = 0;  // 0 move, 1 rotate, 2 scale
	int32 HoverAxis = INDEX_NONE, DragAxis = INDEX_NONE;
	FVector2D DragStart = FVector2D::ZeroVector, DragDirection = FVector2D::ZeroVector;
	double DragUnits = 1.;
	FTransform DragFrom;
	float FloorZ = 0.f;
	FVector Origin = FVector::ZeroVector;
	bool bTimelineOwnsMouse = false;
	static FVector GuideAxis(int32 Axis);
	static FLinearColor AxisColor(int32 Axis);
	// Screen segments of each handle for the selected object (viewport widget space).
	struct FHandle { int32 Axis = 0; TArray<FVector2D> Points; bool bClosed = false; };
	bool BuildGuide(TArray<FHandle>& Out, FVector2D& Center) const;
	struct FHitResult2D { FHandle Handle; int32 Segment = 0; FVector2D Center = FVector2D::ZeroVector; };
	bool HitGuide(const FVector2D& Mouse, double Radius, FHitResult2D& Out) const;
	void BeginDrag(const FHitResult2D& Hit, const FVector2D& Mouse);
	void DragTo(const FVector2D& Mouse);

	// Keys on the selected object (Time snapped to frames).
	void AddKey(int32 Track);
	void DeleteKey(int32 Track);
	void MoveKey(int32 Track, int32 Key, float NewTime);
	int32 SelectedTrackKey = INDEX_NONE;  // key index on the selected track

	// Applies a guide / field edit to the selected object; with auto key it is keyed.
	void EditSelected(const FTransform& Transform, bool bCommit);
	FString Status;
	void SetStatus(const FString& Text, bool bError = false);

	// Scene files (Saved/Studio/<name>.okstudio.json).
	static FString SceneDirectory();
	bool SaveScene(const FString& Name, FString& Error);
	bool LoadScene(const FString& Name, FString& Error);

	// CharacterMCP "studio" action (tests and automation).
	TSharedPtr<FJsonObject> Command(const FJsonObject& Request, FString& Error);
	TSharedRef<FJsonObject> StateJson() const;

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& Geometry, float Delta) override;

	UFUNCTION() void OnReturn();
	UFUNCTION() void OnSelectFemale();
	UFUNCTION() void OnSelectMale();
	UFUNCTION() void OnLoadMale();
	UFUNCTION() void OnModeMove();
	UFUNCTION() void OnModeRotate();
	UFUNCTION() void OnModeScale();
	UFUNCTION() void OnFieldCommitted(const FText& Text, ETextCommit::Type Type);
	UFUNCTION() void OnResetTransform();
	UFUNCTION() void OnMaleMotion(FString Item, ESelectInfo::Type Type);
	UFUNCTION() void OnStopMotion();
	UFUNCTION() void OnSpeed(float Value);
	UFUNCTION() void OnPlay();
	UFUNCTION() void OnRewind();
	UFUNCTION() void OnPrevFrame();
	UFUNCTION() void OnNextFrame();
	UFUNCTION() void OnPrevKey();
	UFUNCTION() void OnNextKey();
	UFUNCTION() void OnAddKey();
	UFUNCTION() void OnDeleteKey();
	UFUNCTION() void OnClearKeys();
	UFUNCTION() void OnInterp(FString Item, ESelectInfo::Type Type);
	UFUNCTION() void OnLengthCommitted(const FText& Text, ETextCommit::Type Type);
	UFUNCTION() void OnSaveScene();
	UFUNCTION() void OnLoadScene();

private:
	void PlayVideo(const FString& FemaleKey, const FString& MaleKey, const FString& Label);
	bool PlayOn(int32 Index, const FString& Key);
	void RefreshMaleMotions();
	void RefreshScenes();
	void RefreshPanels();
	void RefreshFields();
	void UpdateGuide();
	void FitLength();
	void Borrow(UWidget* Widget, UPanelWidget* Into);
	void RestoreBorrowed();
	FTransform FieldTransform() const;

	UPROPERTY() TArray<FOKStudioBorrowed> Borrowed;
	UPROPERTY() TObjectPtr<UOKStudioGuide> Guide;
	UPROPERTY() TObjectPtr<UOKStudioTimeline> Timeline;
	UPROPERTY() TObjectPtr<UOKVideoMotionPanel> VideoPanel;
	UPROPERTY() TObjectPtr<UButton> FemaleButton;
	UPROPERTY() TObjectPtr<UButton> MaleButton;
	UPROPERTY() TObjectPtr<UTextBlock> FemaleLabel;
	UPROPERTY() TObjectPtr<UTextBlock> MaleLabel;
	UPROPERTY() TObjectPtr<UComboBoxString> MaleCardCombo;
	UPROPERTY() TArray<TObjectPtr<UButton>> ModeButtons;
	UPROPERTY() TArray<TObjectPtr<UEditableTextBox>> Fields;  // pos xyz, rot xyz, scale xyz
	UPROPERTY() TObjectPtr<UVerticalBox> FemaleMotion;
	UPROPERTY() TObjectPtr<UVerticalBox> MaleMotion;
	UPROPERTY() TObjectPtr<UOKMotionComboBox> MaleMotionCombo;
	UPROPERTY() TObjectPtr<USlider> SpeedSlider;
	UPROPERTY() TObjectPtr<UTextBlock> SpeedLabel;
	UPROPERTY() TObjectPtr<UTextBlock> MotionTitle;
	UPROPERTY() TObjectPtr<UTextBlock> PlayLabel;
	UPROPERTY() TObjectPtr<UTextBlock> TimeLabel;
	UPROPERTY() TObjectPtr<UTextBlock> StatusLabel;
	UPROPERTY() TObjectPtr<UComboBoxString> InterpCombo;
	UPROPERTY() TObjectPtr<UCheckBox> AutoKey;
	UPROPERTY() TObjectPtr<UCheckBox> Loop;
	UPROPERTY() TObjectPtr<UCheckBox> FollowLength;
	UPROPERTY() TObjectPtr<UEditableTextBox> LengthBox;
	UPROPERTY() TObjectPtr<UEditableTextBox> SceneName;
	UPROPERTY() TObjectPtr<UComboBoxString> SceneCombo;

	FTransform Original[2];
	bool bHadOriginal[2] = {false, false};
	TWeakObjectPtr<AKKPreviewActor> Seen[2];
	float EvaluatedAt = -1.f;
	bool bSelectedEdited = false;  // unkeyed edit at the current time; kept until time moves
	double FieldsAt = 0.;
	bool bRefreshingMale = false;
	bool bPreviousLeft = false;
	bool bClosing = false;
	TSharedPtr<FJsonObject> PendingMaleScene;
	FString PendingMaleCard;
	FString PendingPairMale;  // male clip of a pair whose partner is still loading
	ESlateVisibility ShapeVisibility = ESlateVisibility::Visible, MotionVisibility = ESlateVisibility::Visible;
};
