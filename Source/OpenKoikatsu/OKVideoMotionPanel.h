#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "UObject/Object.h"
#include "OKVideoMotionPanel.generated.h"

class UButton;
class UCheckBox;
class UEditableTextBox;
class UOKCharacterController;
class UOKMotionComboBox;
class UPanelWidget;
class UTextBlock;
class UWidgetTree;

// Character editor section for user video motions (Docs/VIDEO_MOTION.md):
// a searchable list separate from the built-in motions, video import with
// recommended defaults (GVHMR + ok-extract repair loop, run in background),
// and deletion of user-imported motions.
UCLASS()
class OPENKOIKATSU_API UOKVideoMotionPanel : public UObject
{
	GENERATED_BODY()

public:
	struct FPair { FString Name, Female, Male, Work; };
	// Pair manifests (<name>.pair.json) in Saved/VideoMotions; Problems lists files that could not be read.
	static TArray<FPair> ReadPairs(TArray<FString>* Problems = nullptr);
	void Build(UWidgetTree* Tree, UPanelWidget* Column, UOKCharacterController* InOwner);
	void Refresh(const FString& Select = FString());
	void Tick();
	// Same as a user selection (MCP / tests); false when the item is unknown.
	bool SelectItem(const FString& Item);
	FString StatusText() const;
	bool IsImporting() const { return Proc.IsValid(); }
	// Fills the import fields and presses 開始轉換 (MCP / tests); false when it did not start.
	bool StartImport(const FString& Path, const FString& Name, bool bStatic, bool bPair, bool bMaleLeft);
	// Replaces the editor's playback (studio): MaleKey is empty for a single-person motion.
	TFunction<void(const FString& FemaleKey, const FString& MaleKey, const FString& Label)> PlayOverride;

	UFUNCTION() void OnSelected(FString Item, ESelectInfo::Type Type);
	UFUNCTION() void OnBrowse();
	UFUNCTION() void OnStart();
	UFUNCTION() void OnDelete();

	virtual void BeginDestroy() override;

private:
	FString SelectedKey() const;
	void SetStatus(const FString& Text, bool bError = false);
	void FinishImport();
	void ParseOutput(bool bFinal);
	// ok-video.exe (Rust converter), empty when not built.
	static FString ImporterExe();

	UPROPERTY() TObjectPtr<UOKCharacterController> Owner;
	UPROPERTY() TObjectPtr<UOKMotionComboBox> Combo;
	UPROPERTY() TObjectPtr<UTextBlock> Status;
	UPROPERTY() TObjectPtr<UEditableTextBox> PathBox;
	UPROPERTY() TObjectPtr<UEditableTextBox> NameBox;
	UPROPERTY() TObjectPtr<UCheckBox> StaticCamera;
	UPROPERTY() TObjectPtr<UCheckBox> PairVideo;
	UPROPERTY() TObjectPtr<UCheckBox> MaleLeft;
	UPROPERTY() TObjectPtr<UEditableTextBox> ArmReachBox;
	UPROPERTY() TObjectPtr<UEditableTextBox> CollarBox;
	UPROPERTY() TObjectPtr<UEditableTextBox> SmoothBox;
	UPROPERTY() TObjectPtr<UEditableTextBox> FocalBox;
	UPROPERTY() TObjectPtr<UTextBlock> StartLabel;
	UPROPERTY() TObjectPtr<UTextBlock> DeleteLabel;

	TMap<FString, FString> OptionKeys;
	TMap<FString, FPair> Pairs;
	FProcHandle Proc;
	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	FString Pending;
	FString LastLine;
	TArray<FString> ErrorLines;
	FString RunningName;
	double DeleteArmedUntil = 0.;
	bool bRefreshing = false;
};
