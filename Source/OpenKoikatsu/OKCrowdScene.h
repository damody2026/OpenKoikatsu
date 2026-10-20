#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "OpenKoikatsuPlayerController.h"
#include "Commandlets/Commandlet.h"
#include "KKImportedCharacter.h"
#include "KKSharedBakedSkinComponent.h"
#include "Animation/TransformProviderData.h"
#include "RenderTransform.h"
#include "OKCrowdBehavior.h"
#include "OKCrowdScene.generated.h"
class UKKImportedCharacter;
class UKKSharedAnimation;
class UKKImportedAnimationLibrary;
class AKKImportedCharacterActor;
class USkeletalMeshComponent;
class USkeletalMesh;
class UAnimSequence;
class UTransformProviderData;
class FOKCrowdIndependentGpu;
class UOKCrowdRenderSkin;
DECLARE_MULTICAST_DELEGATE_OneParam(FOKCrowdPoseFinalized,UOKCrowdRenderSkin*);
struct FStreamableHandle;

// Independently owned mesh instance; no animation asset evaluation on the CPU.
// Scene bridge registers this provider and supplies private GPU palettes.
UCLASS()
class OPENKOIKATSU_API UOKIndependentTransformProvider : public UTransformProviderData {
 GENERATED_BODY()
public:
 virtual bool IsGpuOnly() const override{return true;}
 virtual bool UsesSkeletonBatching() const override{return !RuntimePrimitiveOwnership;}
 // Diagnostic reference envelope, initialized before instance registration.
 // Production requires an independently baked whole-clip envelope.
 void SetDiagnosticBounds(const FBox& Bounds){DiagnosticBounds=Bounds;}
 virtual bool HasAnimationBounds() const override{return DiagnosticBounds.IsValid!=0;}
 virtual bool GetAnimationBounds(uint32 AnimationIndex,FRenderBounds& OutBounds) const override{if(AnimationIndex!=0||!DiagnosticBounds.IsValid)return false;OutBounds=FRenderBounds(DiagnosticBounds);return true;}
 virtual const FGuid& GetTransformProviderID() const override;
 void SetRuntimeProviderId(const FGuid& Id){OwnedProviderId=Id;RuntimePrimitiveOwnership=true;}
 virtual FTransformProviderRenderProxy* CreateRenderProxy(FInstancedSkinningSceneExtensionProxy*) const override;
private:
 FGuid OwnedProviderId=FGuid::NewGuid();
 bool RuntimePrimitiveOwnership=false;
 FBox DiagnosticBounds=FBox(ForceInit);
};

UCLASS()
class OPENKOIKATSU_API UOKCrowdFaceDetail : public UDataAsset {
 GENERATED_BODY()
public:
 UPROPERTY() FString SourceHash;
 UPROPERTY() TObjectPtr<UKKImportedCharacter> Character;
 UPROPERTY() TArray<int32> LowPackedSlots;
 UPROPERTY() TArray<FString> LowPartNames;
 UPROPERTY() int32 LowHairTriangles=0;
 UPROPERTY() int32 NearHairTriangles=0;
};

// Animation time advances every frame; optional pose evaluation is capped at
// 60 Hz. Completed render uploads can separately use visibility conditions.
UCLASS()
class OPENKOIKATSU_API UOKCrowdRenderSkin : public UKKSharedBakedSkinComponent {
 GENERATED_BODY()
public:
 void SetCrowdRenderNeeded(bool Needed){CrowdRenderNeeded.Store(Needed);}
 bool IsCrowdRenderNeeded() const{return CrowdRenderNeeded.Load();}
 virtual void SendRenderDynamicData_Concurrent() override;
 virtual void RefreshBoneTransforms(FActorComponentTickFunction* TickFunction=nullptr) override;
 virtual void FinalizeBoneTransform() override;
 virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
 int32 PoseRateHz=0;
 bool ProtectFacialBounds=false;
 bool GpuAnimationClockOwned=false;
 float GpuAnimationBoundsRadius=0;
 bool ForceDiagnosticRenderUpload=false;
 double PosePhaseCycles=0;
 uint64 PoseEvaluations=0,PoseSkips=0;
 TWeakObjectPtr<UAnimSequence> FinalizedPoseClip;
 float FinalizedPoseTime=0,PreviousFinalizedPoseTime=0;
 TWeakObjectPtr<UAnimSequence> PreviousFinalizedPoseClip;
 uint32 FinalizedBoneRevision=MAX_uint32;
 uint64 FinalizedPoseEvaluation=0;
 FOKCrowdPoseFinalized OnCrowdPoseFinalized;
private:
 TAtomic<bool> CrowdRenderNeeded{true};
 int64 LastPoseBucket=-1;
};
UCLASS()
class OPENKOIKATSU_API AOKCrowdRenderActor : public AKKImportedCharacterActor {
 GENERATED_BODY()
public:
 virtual void OnConstruction(const FTransform& Transform) override;
 int32 GroupedSourceParts=0,GroupedResultParts=0,GroupedAcceptedGroups=0,GroupedRejectedGroups=0;
 int64 GroupedAuditedVertices=0;double GroupedBuildSeconds=0;
 UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMesh>> GroupedMeshes;
};

UCLASS()
class OPENKOIKATSU_API AOKCrowdGameMode : public AGameModeBase {
 GENERATED_BODY()
public:
 AOKCrowdGameMode();
};

UCLASS()
class OPENKOIKATSU_API AOKCrowdController : public AOpenKoikatsuPlayerController {
 GENERATED_BODY()
public:
 virtual void BeginPlay() override;
 virtual void PlayerTick(float DeltaTime) override;
 bool RuntimeApngScene=false;
};

// Only saved game assets are loaded here. Card parsing belongs to the builder.
UCLASS()
class OPENKOIKATSU_API AOKCrowdScene : public AActor {
 GENERATED_BODY()
public:
 AOKCrowdScene();
 UFUNCTION(BlueprintCallable,Category="Crowd") void SetHighDetailFocus(int32 ResidentIndex);
 // Request and execution are separate: CPU remains active until the renderer
 // adapter has validated this component's private animation and palette.
 UFUNCTION(BlueprintCallable,Category="Crowd|GPU Animation") void SetIndependentGpuAnimationEnabled(bool Enabled);
 UFUNCTION(BlueprintPure,Category="Crowd|GPU Animation") bool IsIndependentGpuAnimationRequested() const{return IndependentGpuAnimationRequested;}
 UFUNCTION(BlueprintPure,Category="Crowd|GPU Animation") FString GetIndependentGpuAnimationStatus() const;
 UPROPERTY(EditAnywhere,Category="Crowd") TArray<TSoftObjectPtr<UKKImportedCharacter>> Characters;
 UPROPERTY(EditAnywhere,Category="Crowd") TArray<TSoftObjectPtr<UKKImportedAnimationLibrary>> Walks;
 UPROPERTY(EditAnywhere,Category="Crowd") TArray<FString> CardNames;
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 void UpdateBehaviors(float DeltaSeconds);
 bool CrowdBehaviors=false;
 TArray<FOKCrowdBehavior> Behaviors;
 TArray<TWeakObjectPtr<AKKImportedCharacterActor>> BehaviorActors;
 int32 BehaviorClipSwitches=0, BehaviorMissingIdle=0;
 void LoadNext();
 void FinishCharacter();
 void SaveReport();
 void PollPerformanceControl(double Now);
 void PollIndependentGpuAnimationControl();
 void ShowIndependentGpuAnimationStatus();
 bool IndependentGpuAnimationRequested=false;
 int32 IndependentGpuAnimationRequestChanges=0;
 bool IndependentGpuAnimationControlInitialized=false;
 bool IndependentGpuAnimationChangedDuringSamples=false;
 TSharedPtr<FOKCrowdIndependentGpu> IndependentGpuRuntime;
 void UpdateDetail(double Now);
 void UpdateFaceLOD(const FVector& Origin);
 void FinishFaceLOD();
 void RestoreLowFace(int32 Index);
 void UpdateShadowPolicy(const FVector& Camera);
 void RestoreLoadingBudget();
 void FinishDetail();
 bool LoadSectionLOD();
 void BindSectionLOD(AKKImportedCharacterActor* Actor,bool IsLow);
 void UpdateThirdPersonCamera();
 void UpdatePlayerMovement(float DeltaSeconds);
 void BindRenderUploads(AKKImportedCharacterActor* Actor,bool IsLow);
 void UpdateRenderUploads();
 void BalancePosePhases();
 void ApplyBalancedPosePhase(AKKImportedCharacterActor* Actor,int32 Index);
 bool BalancedPose=false;
 TArray<double> BalancedPhases;
 double PoseBalanceMin=0,PoseBalanceMax=0;
 int32 ShadowTransitions=0;
 bool CacheShadowPolicy=false;double ShadowHysteresisCm=0;
 int32 ShadowCacheRebuilds=0,ShadowPolicyUpdates=0,ShadowActiveResidents=0;
 struct FShadowBinding {TWeakObjectPtr<AKKImportedCharacterActor> Actor;int32 ComponentCount=0;bool Casting=false;TArray<TWeakObjectPtr<USkeletalMeshComponent>> Skins;};
 TArray<FShadowBinding> ShadowBindings;
 FString FrameSeriesPath;
 bool FrameSeriesWritten=false;
 double TraceAnchorReadySeconds=0;
 double TraceDurationSeconds=20;
 bool SuppressPeriodicReports=false;
 double PendingReportMilliseconds=0,ReportTotalMilliseconds=0,ReportMaxMilliseconds=0;
 uint64 CompletedReportCalls=0;
 struct FFrameSample {double Time=0,Frame=0,Game=0,Render=0,RHI=0,GPU=0,PoseBones=0,ReportMilliseconds=0,IdleMilliseconds=0,IdleOvershootMilliseconds=0;int32 Poses=0,Shadows=0,Details=0;};
 TArray<FFrameSample> FrameSeries;
 TArray<TWeakObjectPtr<UOKCrowdRenderSkin>> FrameSeenLeaders;
 TArray<uint64> FrameSeenEvaluations;
 bool CullRenderUploads=false;
 int32 AnimationHz=0;
 double ShadowRadius=1200,UploadNearRadius=1800,OutlineRadius=1600;
 float PreviousNearClip=-1;
 bool FaceLOD=false;
 int32 FaceLoads=0,FaceRestores=0;
 int32 FacePendingIndex=INDEX_NONE;
 TSharedPtr<FStreamableHandle> FacePending;
 struct FFaceBinding {TWeakObjectPtr<AKKImportedCharacterActor> Actor;TObjectPtr<UOKCrowdFaceDetail> Asset=nullptr;TSharedPtr<FStreamableHandle> Handle;TArray<TWeakObjectPtr<UOKCrowdRenderSkin>> Components;TArray<TWeakObjectPtr<USkeletalMeshComponent>> LowParts;TWeakObjectPtr<USkeletalMeshComponent> LowPacked;};
 TMap<int32,FFaceBinding> Faces;
 UPROPERTY() TArray<TObjectPtr<UOKCrowdFaceDetail>> FaceAssets;
 struct FUploadSkin {TWeakObjectPtr<UOKCrowdRenderSkin> Skin;TArray<int32> WPOSlots;};
 struct FUploadBinding {TWeakObjectPtr<AKKImportedCharacterActor> Actor;double Radius=0;TArray<FUploadSkin> Skins;};
 TArray<FUploadBinding> UploadBindings;
 int32 UploadBoundSkins=0,UploadSuppressedCandidates=0;
 bool ThirdPersonCamera=false;
 bool FocusedHighOnly=false;
 bool FocusOnlyShadows=false;
 int32 DetailFocus=INDEX_NONE;
 int32 CameraResident=44;
 float ThirdPersonYaw=0,ThirdPersonPitch=-10;
 bool PlayerControl=false,PlayerMovementTest=false,PlayerMovementInitialized=false,PlayerWasMoving=false,PlayerReachedBoundary=false;
 float PlayerCameraYaw=0;
 TWeakObjectPtr<AKKImportedCharacterActor> PlayerAnimationActor;
 FVector PlayerPreviousPosition=FVector::ZeroVector;
 double PlayerDistanceCm=0,PlayerIdleDriftCm=0;
 double MovementTestDistances[5]={0,0,0,0,0};
 void UpdateSectionLOD(APlayerController* PC);
 struct FSectionLODMesh {FString SourceHash;int32 Materials=0,Sections=0;double ActorRadius=0;TArray<double> Radii;};
 TMap<FString,FSectionLODMesh> SectionLODMeshes;
 struct FSectionLODSlot {int32 Material=0,Section=0,Count=0;double Radius=0;};
 struct FSectionLODBinding {TWeakObjectPtr<AKKImportedCharacterActor> Actor;TWeakObjectPtr<USkeletalMeshComponent> Skin;TWeakObjectPtr<USkeletalMesh> Mesh;double ActorRadius=0;TArray<FSectionLODSlot> Slots;};
 TArray<FSectionLODBinding> SectionLODBindings;
 double SectionLODPixels=0;
 int32 SectionLODHidden=0,SectionLODMaxHidden=0,SectionLODTransitions=0,SectionLODRestores=0,SectionLODMatched=0;
 TArray<TSoftObjectPtr<UKKImportedCharacter>> HighCharacters;
 TArray<TSoftObjectPtr<UKKImportedAnimationLibrary>> HighWalks;
 TArray<bool> HighDetail;
 TSharedPtr<FStreamableHandle> DetailPending;
 int32 DetailIndex=INDEX_NONE;
 bool DetailRequestedHigh=false;
 bool LightweightGame=false;
 double SceneStarted=0;
 double StartupSeconds=0;
 bool DeferResidentsDuringLoad=false;
 bool LoadingBudgetChanged=false;
 float PreviousLoadingBudget=5;
 int32 PreviousFullLoadingBudget=0;
 double InitialReadyAt=0;
 FVector EntryCameraLocation=FVector::ZeroVector;
 bool DetailInteractionStarted=false;
 int32 InitialHighMeshCount=-1;
 int32 InitialLargeTextureCount=-1;
 double DetailPollAt=0;
 int32 DetailSwitches=0;
 double DetailPhaseError=0;
 UPROPERTY() TArray<TObjectPtr<AKKImportedCharacterActor>> Residents;
 UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> MotionProbes;
 UPROPERTY() TArray<TObjectPtr<UTransformProviderData>> GpuMotionProbes;
 TArray<int32> GpuProbeTracks;
 bool GpuPose=false;
 TArray<float> InitialAnimationTimes;
 TArray<bool> AnimationAdvanced;
 TSharedPtr<FStreamableHandle> Pending;
 TSharedPtr<FStreamableHandle> Preload;
 int32 PrefetchedUntil=0;
 int32 SampleCursor=0,SampleCapacity=3600;
 uint64 SamplesSeen=0;
 TArray<double> Frames;
 TArray<double> GameFrames;
 TArray<double> RenderFrames;
 TArray<double> RHIFrames;
 TArray<double> GPUFrames;
 TArray<double> LoadMilliseconds;
 TArray<double> PackageWaitMilliseconds;
 TArray<double> CharacterBuildMilliseconds;
 int32 Next=0;
 int32 Failed=0;
 double LoadStarted=0;
 double ReadyAt=0;
 double LastWall=0;
 double LastReport=0;
 double TravelTime=0;
 bool Stopping=false;
 bool ScreenshotTaken=false;
 int32 VisualPoseShots=0;
 bool VisualComparisonFrozen=false,VisualComparisonCaptured=false;
 bool CompactSkinReferences=false;
 bool PackedSkinParts=false;
 FString AssetVariant;
 bool QualityAnimationRates=false;
 bool QualityPilot=false;
 double ProfileStarted=0;
 bool ProfileTaken=false;
 FString PerformanceControl;
 FString ControlledTag;
 FString ControlledReport;
 FString ControlledTrace;
 FString ControlledScreenshot;
 double ControlPollAt=0;
 double ControlledDuration=0;
 int32 ControlRevision=0;
 int32 PreviousNamedEvents=0;
 bool ControlledFinished=false;
 bool ControlledReportWritten=false;
 bool OwnNamedEvents=false;
};

UCLASS()
class OPENKOIKATSU_API UOKBuildCrowdMapCommandlet : public UCommandlet {
 GENERATED_BODY()
public:
 UOKBuildCrowdMapCommandlet();
 virtual int32 Main(const FString& Params) override;
};
