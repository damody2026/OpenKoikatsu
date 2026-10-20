#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OKPaintAtlas.h"
#include "Math/Float16Color.h"
#include "Commandlets/Commandlet.h"
#include "OKPaintFilm.h"
#include "OKPaintSprayer.generated.h"
class UProceduralMeshComponent;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraSystem;
class UNiagaraComponent;
class SWidget;

USTRUCT(BlueprintType)
struct FOKPaintSettings
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="10",ClampMax="10000")) float JetSpeed=1600;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="0",ClampMax="200")) float Flow=12;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="0",ClampMax="30")) float ScatterDegrees=.15f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="32",ClampMax="4096")) int32 MaxJetParticles=512;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="1",ClampMax="64")) int32 MaxImpactEffects=16;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="8",ClampMax="256")) int32 MaxSimulationParticles=64;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="8",ClampMax="512")) int32 MaxFloorStains=128;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="0",ClampMax="1")) float JetOpacity=1;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="512",ClampMax="4096")) int32 AtlasResolution=1024;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="0.01",ClampMax="100")) float Viscosity=8;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="0",ClampMax="100")) float SurfaceSpeed=24;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="0.001",ClampMax="1")) float DripThickness=.12f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint") FLinearColor Color=FLinearColor::White;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="5",ClampMax="60")) float SurfaceHz=20;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="0.1",ClampMax="10")) float BrushRadius=2;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="0",ClampMax="5")) float AdhesionSeconds=1;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint",meta=(ClampMin="0",ClampMax="0.1")) float ResidualThickness=.004f;
};

UCLASS()
class OPENKOIKATSU_API AOKPaintSprayer : public AActor
{
 GENERATED_BODY()
public:
 AOKPaintSprayer();
 virtual ~AOKPaintSprayer();
 virtual void Tick(float Dt) override;
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Paint") FOKPaintSettings Settings;
 UFUNCTION(BlueprintCallable,Category="Paint") void SetSpray(bool Enabled,FVector Origin,FVector Direction);
 bool IsSpraying() const {return Spraying;}
 void StopSpray();
 static FVector AimDirection(const FVector& Origin,const FVector& Target,double Speed);
 UFUNCTION(BlueprintCallable,Category="Paint") void ClearPaint();
 UFUNCTION(BlueprintCallable,Category="Paint") void ClearPaintForActor(AActor* Actor);
 void SetControlsVisible(bool Visible);
 UFUNCTION(BlueprintCallable,Category="Paint") FString Statistics() const;
 bool FindPaintHit(const FVector& A,const FVector& B,double Radius,int32& Target,int32& Triangle,FVector& Bary,double& Time);
 double Deposited=0,Dripped=0,FloorVolume=0,Cleared=0,Escaped=0;
 double LastUpdateMs=0,MaxUpdateMs=0;
 double TotalUpdateMs=0;uint64 UpdateSamples=0;
 double TotalGeometryMs=0,TotalSimulationMs=0,TotalCoatingMs=0;
 int32 SkinnedGeometrySurfaces=0,SkinnedGeometryComponents=0;
 uint64 CoatingRebuilds=0,CoatingUpdates=0;
 double GeometryUpdateMs=0,SimulationUpdateMs=0,CoatingUpdateMs=0,NextProfileLog=0;
 double BallisticMs=0,MaskTransportMs=0,FilmTransportMs=0,DripUpdateMs=0;
 double TotalBallisticMs=0,TotalMaskTransportMs=0,TotalFilmTransportMs=0,TotalDripUpdateMs=0;
 double Emitted=0;
 FVector2D MeasureCoatingPair(int32 Repeats=8);
 FVector MeasureSurfaceQueryPair(int32 Repeats=8);
 FVector MeasureGeometryPair(int32 Repeats=4);
 FVector MeasureSurfaceGeometryPair(int32 Repeats=4);
 FVector MeasureSweepPair(int32 Repeats=8);
 double MeasureNormalParity();
 FVector2D MeasureBufferPair(int32 Repeats=8);
 double RemainingVolume() const;
 double ConservationError() const {return Emitted-RemainingVolume()-FloorVolume-Escaped-Cleared;}
 int32 SurfaceCount() const {return Surfaces.Num();}
#if !UE_BUILD_SHIPPING
 bool VerifyPrivateGpuPaintGeometry(const AActor* Actor,int32& Checked,int32& Painted,int32& Anchor,double& MaxError,FString& Error);
 void GetPaintAtlasRects(int32 Surface,UTextureRenderTarget2D* Texture,TArray<FIntRect>& Rects) const;
#endif
 double SurfaceVolume(int32 Index) const;
 bool FilmCenterOfMass(FVector& Center,double& Volume) const;
#if WITH_DEV_AUTOMATION_TESTS
 bool VerifyAttachedVisuals(const AOKPaintSprayer& Oracle,int32& Compared,double& MaximumError) const;
 bool VerifyGeometryJobsParity(int32& ComparedVertices,int32& ComparedFaces);
#endif
 bool GetPaintAnchor(int32 Surface,int32 Face,FVector& Point,class UTextureRenderTarget2D*& Texture,int32& FoundFace) const;
 int32 HitCount=0;
 int32 LiveSimulationParticles() const {return Packets.Num()+Attached.Num();}
 int32 LiveImpactEffects() const {return OwnedFX.Num();}
private:
 struct FSurface;
 struct FPacket {FVector P,V;double Volume=0,Age=0;bool Drip=false;};
 struct FAttached {int32 Target=0,Face=0;FVector Bary;double Volume=0,Age=0,Radius=0;TWeakObjectPtr<UProceduralMeshComponent> Visual;TArray<FVector> RenderVertices;};
 TArray<TSharedPtr<FSurface>> Surfaces;
 FOKPaintAtlas SharedAtlas;
 TArray<TArray<FFloat16Color>> AtlasPixels;
 void ReleaseSurfaceAtlas(FSurface& Surface);
 TArray<FPacket> Packets;
 TArray<FAttached> Attached;
 double Accumulator=0,GeometryTime=0,JetTime=0;
 bool Spraying=false;
 FVector Nozzle,Direction;
 FRandomStream Random{74111};
 float VisualFlow=-1,VisualSpeed=-1;int32 VisualParticleLimit=-1;
 UPROPERTY() TArray<TObjectPtr<UTextureRenderTarget2D>> Targets;
 UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Overlays;
 UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
 UPROPERTY() TObjectPtr<UNiagaraSystem> JetSystem;
 UPROPERTY() TObjectPtr<UMaterialInterface> FilmBase;
 UPROPERTY() TObjectPtr<UMaterialInterface> DropBase;
 UPROPERTY() TObjectPtr<UNiagaraSystem> SplashSystem;
 UPROPERTY() TObjectPtr<UNiagaraSystem> DropSystem;
 UPROPERTY() TObjectPtr<UNiagaraComponent> JetFX;
 UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> OwnedFX;
 UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> FloorStains;
 double NextSplashAt=0;
 UPROPERTY() TObjectPtr<UProceduralMeshComponent> Gun;
 void DiscoverSurfaces(const FVector& A,const FVector& B,bool SourceCone=true);
 void RefreshGeometry(FSurface& Surface);
 bool PrepareGeometry(FSurface& Surface);
 void RefreshGeometries();
 void RefreshGeometryJobs();
 bool InitializeCoating(FSurface& Surface);
 struct FAtlasChartWrite {FFloat16Color* Pixels;const TArray<float>* Heights;int32 PageSize,Edge;FIntPoint Origin;double Scale;FVector2D Flow;};
 static void FillAtlasChart(const FAtlasChartWrite& Write);
 void DrawFilm(FSurface& Surface,bool UpdateMask=true,TArray<struct FKKAnimatedStreamRequest>* PendingStreams=nullptr,TMap<int32,TArray<FIntRect>>* PendingAtlas=nullptr,TArray<FAtlasChartWrite>* PendingCharts=nullptr);
 void UploadAtlasBlocks(const TMap<int32,TArray<FIntRect>>& Blocks,bool SparseUploads);
 void Step(double Dt);
 void AdvanceSurface(double Dt);
 double SurfaceAccumulator=0;
 void Splash(FVector P,FVector N,bool Floor,double Volume);
 void MakeSurface(class UPrimitiveComponent* Component,int32 Section,TArray<FVector>* CurrentPositions=nullptr);
 TSharedPtr<SWidget> Panel;
 void BuildPanel();
};

UCLASS()
class OPENKOIKATSU_API UOKBuildPaintDemoCommandlet : public UCommandlet
{
 GENERATED_BODY()
public:
 UOKBuildPaintDemoCommandlet();
 virtual int32 Main(const FString& Params) override;
};
