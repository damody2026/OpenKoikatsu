#include "OKPaintSprayer.h"
#include "OKPaintMask.h"
#include "OKPaintSkinning.h"
#include "KKPreviewActor.h"
#include "KKPrivateGpuSource.h"
#include "KKAffineProceduralMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TextureResource.h"
#include "RHICommandList.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Algo/Sort.h"
#include "Async/ParallelFor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include <algorithm>

static TAutoConsoleVariable<int32> CVarPaintCoatingParallel(TEXT("ok.Paint.CoatingParallel"),1,TEXT("0 serial oracle, 1 adaptive parallel coating, 2 force parallel for regression"));
static TAutoConsoleVariable<int32> CVarPaintGpuCoatingSparseGather(TEXT("ok.Paint.GpuCoatingSparseGather"),1,TEXT("Gather only attachment vertices when resident GPU coating is ready; 0 retains gather/discard oracle."));
static TAutoConsoleVariable<int32> CVarPaintGpuCoating(TEXT("ok.Paint.GpuCoating"),0,TEXT("Runtime resident GPU coating for acknowledged private GPU sources; 0 retains CPU oracle."));
static TAutoConsoleVariable<int32> CVarPaintCoatingVertexTransforms(TEXT("ok.Paint.CoatingVertexTransforms"),1,TEXT("Transform each consumed coating vertex once in the current gather; 0 retains per-face exact oracle."));
static TAutoConsoleVariable<int32> CVarPaintSharedPoseSnapshot(TEXT("ok.Paint.SharedPoseSnapshot"),1,TEXT("Reuse the exact geometry snapshot computed earlier in this sprayer tick; 0 independent deformation oracle"));
static TAutoConsoleVariable<int32> CVarPaintGeometryParallel(TEXT("ok.Paint.GeometryParallel"),1,TEXT("0 serial geometry oracle, 1 parallel immutable vertex reads for large surfaces"));
static TAutoConsoleVariable<int32> CVarPaintSurfaceParallel(TEXT("ok.Paint.SurfaceParallel"),1,TEXT("Solve independent captured surface geometry in parallel; 0 retains sequential surfaces."));
static TAutoConsoleVariable<int32> CVarPaintAnimatedCoating(TEXT("ok.Paint.AnimatedCoating"),1,TEXT("Create owned coating meshes with compact mutable streams; 0 creates stock procedural oracle meshes. Applies at coating creation."));

static TAutoConsoleVariable<int32> CVarPaintCachedSweepBounds(TEXT("ok.Paint.CachedSweepBounds"),1,TEXT("Reuse animation-refitted triangle bounds; 0 recomputes bounds for diagnostics"));

static TAutoConsoleVariable<int32> CVarPaintCoatingBufferReuse(TEXT("ok.Paint.CoatingBufferReuse"),1,TEXT("Reuse per-surface coating buffers and unchanged chart topology; 0 rebuilds for diagnostic comparison"));
static TAutoConsoleVariable<int32> CVarPaintBatchCoatingStreams(TEXT("ok.Paint.BatchCoatingStreams"),1,TEXT("Publish unchanged coating topology as one blocking same-frame mutable-stream batch; 0 retains serial complete-stream oracle."));
static TAutoConsoleVariable<int32> CVarPaintBatchAtlasUploads(TEXT("ok.Paint.BatchAtlasUploads"),1,TEXT("Merge dirty chart uploads across all surfaces on the same atlas page within one Tick; 0 retains per-surface upload oracle."));
static TAutoConsoleVariable<int32> CVarPaintAnimatedDrops(TEXT("ok.Paint.AnimatedDrops"),1,TEXT("Keep attached-drop primitive transforms stable and update mutable vertices in the current frame; 0 retains stock moving-PMC oracle. Applies at drop creation."));
static TAutoConsoleVariable<int32> CVarPaintConservativeCoatingBounds(TEXT("ok.Paint.ConservativeCoatingBounds"),1,TEXT("Use conservative render envelopes for collision-free coatings; CPU section bounds remain exact. Applies at coating creation."));

static TAutoConsoleVariable<int32> CVarPaintCoatingTopologySlack(TEXT("ok.Paint.CoatingTopologySlack"),25,TEXT("Percent spare degenerate coating faces to avoid proxy recreation during spreading; 0 exact legacy size"));
static TAutoConsoleVariable<int32> CVarPaintPacketSweeps(TEXT("ok.Paint.PacketSweeps"),1,TEXT("0 sequential live queries, 1 batch immutable projectile sweeps for >=8 packets, 2 force batch for regression."));
static TAutoConsoleVariable<int32> CVarPaintSparseUploads(TEXT("ok.Paint.SparseUploads"),1,TEXT("Upload dirty chart runs, bounded to 256 rectangles; 0 retains the original 4x4 block upload oracle."));
static TAutoConsoleVariable<int32> CVarPaintSparseNormals(TEXT("ok.Paint.SparseNormals"),1,TEXT("Resolve coating normals only for consumed vertices from ordered incident faces; 0 retains full-surface normal oracle."));
static TAutoConsoleVariable<int32> CVarPaintResidentGeometry(TEXT("ok.Paint.ResidentGeometry"),1,TEXT("Evaluate private collision positions from the current resident GPU source/palette; 0 uses the full native geometry query oracle."));
static TAutoConsoleVariable<int32> CVarPaintGeometryJobs(TEXT("ok.Paint.GeometryJobs"),0,TEXT("Batch current collision vertices/faces across surfaces into blocking jobs; 0 retains the measured faster surface-by-surface path, 2 forces jobs for regression."));

struct AOKPaintSprayer::FSurface
{
 TWeakObjectPtr<UPrimitiveComponent> Component;
 TWeakObjectPtr<USkeletalMesh> Asset;
 int32 Section=0,AtlasSize=0,Front=0,CoatingFaceCapacity=0;bool HasRenderedPaint=false;uint64 GeometryRevision=1,CoatingRevision=0;
 bool IsDisplayed() const {auto* C=Component.Get();if(!C||!C->IsVisible()||!C->bRenderInMainPass||(C->GetOwner()&&C->GetOwner()->IsHidden()))return false;if(auto* P=Cast<UProceduralMeshComponent>(C)){const auto* Part=P->GetProcMeshSection(Section);return Part&&Part->bSectionVisible;}return true;}
 TArray<int32> Indices;
 TArray<FVector> Positions,RenderPositions,VertexNormals,TopologyPositions,BuildCenters;
 TArray<uint8> GeometryChangedFlags;
 bool FilmTopologyReady=false,GeometrySnapshotAvailable=false;
 const FProcMeshSection* PreparedSection=nullptr;FMatrix PreparedRender=FMatrix::Identity;bool PreparedSkinned=false,PreparedValid=false;
 FKKPrivateGpuSurfaceView PreparedPrivate;
 void RefreshCapturedGeometry(bool VertexParallel);
 TArray<int32> NormalOffsets,NormalFaces;TArray<uint64> NormalRevisions;
 void DenseNormals(){VertexNormals.Init(FVector::ZeroVector,Positions.Num());for(int32 I=0;I<Film.Faces.Num();++I){const auto& F=Film.Faces[I];const FVector N=FVector::CrossProduct(F.P[1]-F.P[0],F.P[2]-F.P[0]);for(int32 E=0;E<3;++E)VertexNormals[Indices[I*3+E]]+=N;}for(auto& N:VertexNormals)N=N.GetSafeNormal();}
 void UpdateNormals(){
  if(CVarPaintSparseNormals.GetValueOnAnyThread()==0){DenseNormals();return;}
  if(NormalOffsets.Num()==Positions.Num()+1)return;
  VertexNormals.SetNumZeroed(Positions.Num());NormalRevisions.Init(0,Positions.Num());NormalOffsets.Init(0,Positions.Num()+1);
  for(int32 Id:Indices)++NormalOffsets[Id+1];for(int32 I=1;I<NormalOffsets.Num();++I)NormalOffsets[I]+=NormalOffsets[I-1];
  NormalFaces.SetNumUninitialized(Indices.Num());auto Cursor=NormalOffsets;
  // Append in exactly the dense face/edge order, including repeated indices
  // of degenerate triangles. Floating-point accumulation stays identical.
  for(int32 I=0;I<Film.Faces.Num();++I)for(int32 E=0;E<3;++E)NormalFaces[Cursor[Indices[I*3+E]]++]=I;
 }
 void ResolveNormal(int32 Vertex){
  if(CVarPaintSparseNormals.GetValueOnAnyThread()==0)return;UpdateNormals();
  if(NormalRevisions[Vertex]==GeometryRevision)return;FVector Normal=FVector::ZeroVector;
  for(int32 I=NormalOffsets[Vertex];I<NormalOffsets[Vertex+1];++I){const auto& Face=Film.Faces[NormalFaces[I]];Normal+=FVector::CrossProduct(Face.P[1]-Face.P[0],Face.P[2]-Face.P[0]);}
  VertexNormals[Vertex]=Normal.GetSafeNormal();NormalRevisions[Vertex]=GeometryRevision;
 }
 TArray<FVector> CoatingVertices,CoatingNormals,CoatingLocalPositions,CoatingLocalNormals;
 TArray<FVector2D> CoatingUV;TArray<int32> CoatingIndices,CoatingFaces;
 TArray<FColor> CoatingColors;TArray<FProcMeshTangent> CoatingTangents;
 FTransform CoatingTransform;
 struct FRenderFace {int32 Id;double Mean;const TArray<float>* Profile;};
 struct FVertexRead {int32 Id;FVector Fallback;};
 TArray<FRenderFace> RenderFaces;TArray<FVertexRead> VertexReads;FOKPaintPose CoatingPose;
 TArray<int32> SparsePrivateIndices;TArray<FVector> SparsePrivatePositions;
 TArray<uint32> RenderMarks;uint32 RenderStamp=0;
 double CoatingSide=0; // Choose the struck side; imported winding is not an outward-normal guarantee.
 FOKPaintFilm Film;
 void EnsureFilmTopology(){if(FilmTopologyReady)return;
  // Weld in the discovery pose, preserving the original seam graph even if
  // the character moves before its first hit. Collision-only candidates do
  // not need a film adjacency map or coating normals.
  for(int32 I=0;I<Film.Faces.Num();++I)for(int32 E=0;E<3;++E)Film.Faces[I].P[E]=TopologyPositions[Indices[I*3+E]];
  Film.BuildAdjacency();
  for(int32 I=0;I<Film.Faces.Num();++I)for(int32 E=0;E<3;++E)Film.Faces[I].P[E]=Positions[Indices[I*3+E]];
  FilmTopologyReady=true;TopologyPositions.Empty();UpdateNormals();
 }
 FOKPaintMask Mask;
 TMap<int32,FOKPaintAtlas::FAllocation> Charts;
 TMap<int32,UMaterialInstanceDynamic*> PageMaterials;
 TSet<int32> PageSections;
 TMap<int32,int32> PageCapacity;
 struct FPageStreams {
  TArray<int32> Works,Indices;
  TArray<FVector> Positions,Normals;
  TArray<FVector2D> UV;
  TArray<FColor> Colors;
  TArray<FProcMeshTangent> Tangents;
  TArray<FOKGpuCoatingBinding> GpuBindings;
  TSharedPtr<FKKGpuStreamReceipt,ESPMode::ThreadSafe> GpuReceipt;
  uint64 GpuRevision=0,GpuUploadedRevision=0,GpuSentRevision=0,GpuGeneration=0,GpuEpoch=0,GpuSourceEpoch=0,GpuSentSourceEpoch=0;
  bool GpuRejected=false;uint64 GpuRetryFrame=0;
 };
 TMap<int32,FPageStreams> PageStreams;
 uint64 GpuBindingRevision=0;
 uint64 GpuCoatingFrames=0;
 uint64 GpuCoatingPublished=0;
 bool GpuCandidateLast=false;
 TSet<int32> UploadedFaces[2];
 TSet<int32> PendingDirty[2];
 UProceduralMeshComponent* Overlay=nullptr;
 UTextureRenderTarget2D* RT[2]={nullptr,nullptr};
 UMaterialInstanceDynamic* Material=nullptr;
 FLinearColor MaterialColor=FLinearColor::White;
 struct FNode {FBox Bounds{ForceInit};int32 Left=-1,Right=-1,Start=0,Count=0;};
 static bool PreferBatchMasks(const TArray<FSurface*>& Work,double* Dominance=nullptr){int64 Total=0,Largest=0;
  for(const auto* S:Work){int64 Cost=0;for(int32 Id:S->Film.Active){const int32 N=S->Mask.Resolution(S->Film.Faces[Id]);Cost+=N*N+32;}Total+=Cost;Largest=FMath::Max(Largest,Cost);}
  const double Fraction=Total?double(Largest)/Total:1.;if(Dominance)*Dominance=Fraction;
  // A dominant surface already benefits from chart-level parallelism. Avoid
  // an outer task layer when there is little independent work to distribute.
  return Work.Num()>1&&Fraction<.70;
 }
 TArray<FNode> Nodes;TArray<int32> Order;TArray<FBox> FaceBounds;
 bool Sweep(const FVector& A,const FVector& B,double Radius,bool CachedBounds,int32& Triangle,FVector& Bary,double& Time) const {
  if(Nodes.IsEmpty())return false;bool Found=false;TArray<int32,TInlineAllocator<64>> Stack;Stack.Add(0);
  while(Stack.Num()){const auto& Node=Nodes[Stack.Pop(EAllowShrinking::No)];const FBox Bounds=Node.Bounds.ExpandBy(Radius);if(!Bounds.IsInside(A)&&!FMath::LineBoxIntersection(Bounds,A,B,B-A))continue;
   if(Node.Left>=0){Stack.Add(Node.Left);Stack.Add(Node.Right);continue;}
   for(int32 Index=Node.Start;Index<Node.Start+Node.Count;++Index){const int32 F=Order[Index];const auto& Face=Film.Faces[F];double T;FVector W;
    const FBox BoundsAtFace=(CachedBounds?FaceBounds[F]:FBox(Face.P,3)).ExpandBy(Radius+.001);const FVector End=A+(B-A)*Time;
    if(!BoundsAtFace.IsInside(A)&&!FMath::LineBoxIntersection(BoundsAtFace,A,End,End-A))continue;
    if(FOKPaintFilm::SweepTriangle(A,B,Radius,Face,T,W)&&T<Time){Triangle=F;Bary=W;Time=T;Found=true;if(Time==0)return true;}
   }
  }return Found;
 }
 void RefitNodes(){for(int32 I=Nodes.Num()-1;I>=0;--I){auto& Node=Nodes[I];Node.Bounds.Init();if(Node.Left<0){for(int32 J=Node.Start;J<Node.Start+Node.Count;++J)Node.Bounds+=FaceBounds[Order[J]];}else{Node.Bounds+=Nodes[Node.Left].Bounds;Node.Bounds+=Nodes[Node.Right].Bounds;}}}
 void Refit(){FaceBounds.SetNumUninitialized(Film.Faces.Num());ParallelFor(TEXT("PaintFaceBounds"),Film.Faces.Num(),128,[&](int32 I){FaceBounds[I]=FBox(Film.Faces[I].P,3);},Film.Faces.Num()<1024?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);RefitNodes();}
 int32 Build(int32 Start,int32 Count){const int32 Id=Nodes.AddDefaulted();Nodes[Id].Start=Start;Nodes[Id].Count=Count;FBox Box(ForceInit);for(int32 I=Start;I<Start+Count;++I)Box+=BuildCenters[Order[I]];const FVector Size=Box.GetSize();int32 Axis=Size.Y>Size.X?1:0;if(Size.Z>Size[Axis])Axis=2;
  if(Count>16){int32* Begin=Order.GetData()+Start;std::nth_element(Begin,Begin+Count/2,Begin+Count,[this,Axis](int32 A,int32 B){return BuildCenters[A][Axis]<BuildCenters[B][Axis];});const int32 Left=Build(Start,Count/2),Right=Build(Start+Count/2,Count-Count/2);Nodes[Id].Left=Left;Nodes[Id].Right=Right;}return Id;}
};
AOKPaintSprayer::AOKPaintSprayer(){Tags.Add(TEXT("OKPreviewInteraction"));PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));}
AOKPaintSprayer::~AOKPaintSprayer()=default;
void AOKPaintSprayer::BeginPlay()
{
 Super::BeginPlay();
 FilmBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/PaintDemo/M_PaintFilm.M_PaintFilm"));
 DropBase=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/PaintDemo/M_PaintDrop.M_PaintDrop"));
 JetSystem=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/PaintDemo/NS_PaintJet.NS_PaintJet"));
 SplashSystem=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/PaintDemo/NS_PaintSplash.NS_PaintSplash"));
 DropSystem=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/PaintDemo/NS_PaintDrip.NS_PaintDrip"));
 if(JetSystem){JetFX=UNiagaraFunctionLibrary::SpawnSystemAttached(JetSystem,GetRootComponent(),NAME_None,FVector::ZeroVector,FRotator::ZeroRotator,EAttachLocation::KeepRelativeOffset,false,false);}
 if(!JetSystem||!SplashSystem)UE_LOG(LogTemp,Error,TEXT("Paint assets missing; run -run=OKBuildPaintDemo before playing"));
 BuildPanel();
 Gun=NewObject<UProceduralMeshComponent>(this);Gun->RegisterComponent();Gun->AttachToComponent(GetRootComponent(),FAttachmentTransformRules::SnapToTargetNotIncludingScale);Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);Gun->SetCastShadow(false);
 TArray<FVector> GunV={FVector(-25,-3,-3),FVector(-2,-3,-3),FVector(-2,3,-3),FVector(-25,3,-3),FVector(-25,-3,3),FVector(-2,-3,3),FVector(-2,3,3),FVector(-25,3,3)};
 Gun->CreateMeshSection(0,GunV,{0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,1,2,6,1,6,5,2,3,7,2,7,6,3,0,4,3,4,7},{},{},{},{},false);Gun->SetVisibility(false);
}
void AOKPaintSprayer::EndPlay(const EEndPlayReason::Type Reason){if(Panel&&GetWorld()&&GetWorld()->GetGameViewport())GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Panel.ToSharedRef());Panel.Reset();ClearPaint();Super::EndPlay(Reason);}
FVector AOKPaintSprayer::AimDirection(const FVector& Origin,const FVector& Target,double Speed)
{
 const FVector Delta=Target-Origin;Speed=FMath::Clamp(Speed,10.,10000.);
 const double A=Speed*Speed-980*Delta.Z,D=A*A-980*980*Delta.SquaredLength();
 if(D<0||A<=0)return Delta.GetSafeNormal();
 const double TimeSquared=2*Delta.SquaredLength()/FMath::Max(1.e-12,A+FMath::Sqrt(D));
 return (Delta+FVector(0,0,490*TimeSquared)).GetSafeNormal();
}
void AOKPaintSprayer::StopSpray(){if(Spraying)SetSpray(false,Nozzle,Direction);}
void AOKPaintSprayer::SetSpray(bool Enabled,FVector Origin,FVector Dir)
{
 if(Enabled&&!Spraying)GeometryTime=1.;
 Spraying=Enabled && !Dir.IsNearlyZero() && !Dir.ContainsNaN() && !Origin.ContainsNaN();Nozzle=Origin;Direction=Dir.GetSafeNormal();
 SetActorLocation(Nozzle);SetActorRotation(Direction.Rotation());
 if(Gun)Gun->SetVisibility(Spraying);
 if(JetFX){const bool Changed=VisualFlow!=Settings.Flow||VisualSpeed!=Settings.JetSpeed||VisualParticleLimit!=Settings.MaxJetParticles;VisualParticleLimit=Settings.MaxJetParticles;VisualFlow=Settings.Flow;VisualSpeed=Settings.JetSpeed;JetFX->SetVariableFloat(TEXT("User.JetSpeed"),Settings.JetSpeed);JetFX->SetVariableFloat(TEXT("User.Flow"),FMath::Min(Settings.Flow*200.f,float(FMath::Clamp(Settings.MaxJetParticles,32,4096))/3.1f/1.1f));JetFX->SetVariableFloat(TEXT("User.DropletFlow"),FMath::Min(Settings.Flow*20.f,float(FMath::Clamp(Settings.MaxJetParticles,32,4096))/3.1f/11.f));FLinearColor JetColor=Settings.Color;JetColor.A*=FMath::Clamp(Settings.JetOpacity,0.f,1.f);JetFX->SetVariableLinearColor(TEXT("User.PaintColor"),JetColor);if(Spraying&&(!JetFX->IsActive()||Changed))JetFX->Activate(true);else if(!Spraying&&JetFX->IsActive())JetFX->Deactivate();}
}
bool AOKPaintSprayer::PrepareGeometry(FSurface& S)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintPrepareGeometry);
 S.PreparedValid=false;S.PreparedSection=nullptr;S.PreparedSkinned=false;S.PreparedPrivate={};
 auto* C=S.Component.Get();if(!C)return false;
 if(auto* P=Cast<UProceduralMeshComponent>(C)){
  if(auto* Actor=Cast<AKKPreviewActor>(C->GetOwner())){
   if(CVarPaintResidentGeometry.GetValueOnGameThread()!=0&&Actor->CapturePrivateGpuSurface(P,S.Section,S.PreparedPrivate)){S.PreparedValid=true;return true;}
   Actor->EnsurePrivateGpuQueryGeometry(P,S.Section);
  }
  S.PreparedSection=P->GetProcMeshSection(S.Section);if(!S.PreparedSection)return false;S.PreparedRender=C->GetRenderMatrix();
 }
 else if(auto* K=Cast<USkinnedMeshComponent>(C)){if(!S.CoatingPose.Prepare(K))return false;S.PreparedSkinned=true;}
 else return false;
 S.PreparedValid=true;return true;
}
void AOKPaintSprayer::RefreshGeometry(FSurface& S){if(PrepareGeometry(S))S.RefreshCapturedGeometry(CVarPaintGeometryParallel.GetValueOnGameThread()!=0);}
void AOKPaintSprayer::RefreshGeometries(){
 TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintRefreshGeometries);
 // Capture UObjects once while the game thread owns the frame. The blocking
 // parallel operation cannot overlap animation or mutate another surface.
 TSet<UPrimitiveComponent*> SkinnedComponents;SkinnedGeometrySurfaces=0;
 TMap<AKKPreviewActor*,TArray<UProceduralMeshComponent*>> PrivateQueries;
 for(const auto& S:Surfaces)if(auto* P=Cast<UProceduralMeshComponent>(S->Component.Get()))if(auto* Actor=Cast<AKKPreviewActor>(P->GetOwner())){
  FKKPrivateGpuSurfaceView View;if(CVarPaintResidentGeometry.GetValueOnGameThread()==0||!Actor->CapturePrivateGpuSurface(P,S->Section,View))PrivateQueries.FindOrAdd(Actor).AddUnique(P);
 }
 for(auto& Entry:PrivateQueries)Entry.Key->PreparePrivateGpuQueryGeometry(Entry.Value);
 for(auto& S:Surfaces){PrepareGeometry(*S);if(S->PreparedSkinned&&S->PreparedValid){++SkinnedGeometrySurfaces;SkinnedComponents.Add(S->Component.Get());}}SkinnedGeometryComponents=SkinnedComponents.Num();
 if(CVarPaintGeometryJobs.GetValueOnGameThread()!=0){RefreshGeometryJobs();return;}
 const int32 Mode=CVarPaintSurfaceParallel.GetValueOnGameThread();const bool Parallel=Mode==2||(Mode!=0&&Surfaces.Num()>=8);
 const bool VertexParallel=!Parallel&&CVarPaintGeometryParallel.GetValueOnGameThread()!=0;
 ParallelFor(Surfaces.Num(),[&](int32 I){Surfaces[I]->RefreshCapturedGeometry(VertexParallel);},Parallel?EParallelForFlags::None:EParallelForFlags::ForceSingleThread);
}
void AOKPaintSprayer::RefreshGeometryJobs()
{
 TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintGeometryJobs);
 const bool ForceParallel=CVarPaintGeometryJobs.GetValueOnGameThread()==2;
 struct FWork {FSurface* Surface;bool CountChanged;};
 struct FJob {int32 Work,Begin,End;};
 TArray<FWork,TInlineAllocator<64>> Work;TArray<FJob> Jobs;
 for(auto& Surface:Surfaces){auto& S=*Surface;if(!S.PreparedValid)continue;
  const int32 Count=S.PreparedPrivate.Source?S.PreparedPrivate.Draw.Num():S.PreparedSection?S.PreparedSection->ProcVertexBuffer.Num():S.PreparedSkinned?int32(S.CoatingPose.LOD->GetNumVertices()):0;
  if(!S.PreparedPrivate.Source&&!S.PreparedSection&&!S.PreparedSkinned)continue;
  const bool CountChanged=S.Positions.Num()!=Count;S.Positions.SetNumUninitialized(Count,EAllowShrinking::No);S.GeometryChangedFlags.SetNumUninitialized(Count,EAllowShrinking::No);
  const int32 Index=Work.Num();Work.Add({&S,CountChanged});for(int32 Begin=0;Begin<Count;Begin+=512)Jobs.Add({Index,Begin,FMath::Min(Count,Begin+512)});
 }
 // UObjects, pose/morph snapshots and allocations were captured above.
 // Every job owns disjoint output vertices; all jobs join in this refresh.
 ParallelFor(TEXT("PaintGeometryVertexJobs"),Jobs.Num(),1,[&](int32 Index){const auto& Job=Jobs[Index];const auto& W=Work[Job.Work];auto& S=*W.Surface;
  for(int32 I=Job.Begin;I<Job.End;++I){FVector Current;
   if(S.PreparedPrivate.Source){FVector Local;const auto& View=S.PreparedPrivate;const bool Valid=OKEvaluatePrivateGpuPosition(*View.Source,*View.Pose,View.Draw[I],Local);checkf(Valid,TEXT("Validated resident GPU source became invalid during blocking collision query"));Current=View.Render.TransformPosition(Local);}
   else if(S.PreparedSection)Current=S.PreparedRender.TransformPosition(S.PreparedSection->ProcVertexBuffer[I].Position);
   else Current=S.CoatingPose.Position(I);
   S.GeometryChangedFlags[I]=W.CountChanged||S.Positions[I]!=Current;S.Positions[I]=Current;
  }
 },!ForceParallel&&Jobs.Num()<8?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);
 TArray<FSurface*,TInlineAllocator<64>> Changed;Jobs.Reset();
 for(const auto& W:Work){auto& S=*W.Surface;S.GeometrySnapshotAvailable=true;if(!(W.CountChanged&&S.PreparedSection)&&!S.GeometryChangedFlags.Contains(1))continue;++S.GeometryRevision;
  S.FaceBounds.SetNumUninitialized(S.Film.Faces.Num(),EAllowShrinking::No);const int32 Index=Changed.Num();Changed.Add(&S);
  for(int32 Begin=0;Begin<S.Film.Faces.Num();Begin+=512)Jobs.Add({Index,Begin,FMath::Min(S.Film.Faces.Num(),Begin+512)});
 }
 ParallelFor(TEXT("PaintGeometryFaceJobs"),Jobs.Num(),1,[&](int32 Index){const auto& Job=Jobs[Index];auto& S=*Changed[Job.Work];
  for(int32 I=Job.Begin;I<Job.End;++I){for(int32 E=0;E<3;++E)if(S.Positions.IsValidIndex(S.Indices[I*3+E]))S.Film.Faces[I].P[E]=S.Positions[S.Indices[I*3+E]];S.FaceBounds[I]=FBox(S.Film.Faces[I].P,3);}
 },!ForceParallel&&Jobs.Num()<8?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);
 // Preserve each BVH's original reverse-node and face accumulation order.
 ParallelFor(TEXT("PaintGeometryRefitJobs"),Changed.Num(),1,[&](int32 I){auto& S=*Changed[I];if(S.FilmTopologyReady)S.UpdateNormals();S.RefitNodes();},!ForceParallel&&Changed.Num()<4?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);
}
void AOKPaintSprayer::FSurface::RefreshCapturedGeometry(bool VertexParallel)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintCapturedGeometry);
 if(!PreparedValid)return;auto& S=*this;
 bool Changed=false;
 if(S.PreparedPrivate.Source){
  const auto& View=S.PreparedPrivate;const bool CountChanged=S.Positions.Num()!=View.Draw.Num();
  S.Positions.SetNumUninitialized(View.Draw.Num(),EAllowShrinking::No);S.GeometryChangedFlags.SetNumUninitialized(View.Draw.Num(),EAllowShrinking::No);
  ParallelFor(TEXT("PaintResidentCollisionGeometry"),View.Draw.Num(),512,[&](int32 I){FVector Local;const bool Valid=OKEvaluatePrivateGpuPosition(*View.Source,*View.Pose,View.Draw[I],Local);checkf(Valid,TEXT("Validated resident GPU source became invalid during blocking collision query"));const FVector Current=View.Render.TransformPosition(Local);S.GeometryChangedFlags[I]=CountChanged||S.Positions[I]!=Current;S.Positions[I]=Current;},!VertexParallel||View.Draw.Num()<2048?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);
  Changed=S.GeometryChangedFlags.Contains(1);
 }else if(const auto* CapturedSection=S.PreparedSection){
  const FMatrix RenderMatrix=S.PreparedRender;Changed=S.Positions.Num()!=CapturedSection->ProcVertexBuffer.Num();S.Positions.SetNumUninitialized(CapturedSection->ProcVertexBuffer.Num());for(int32 I=0;I<S.Positions.Num();++I){const FVector Current=RenderMatrix.TransformPosition(CapturedSection->ProcVertexBuffer[I].Position);Changed=Changed||S.Positions[I]!=Current;S.Positions[I]=Current;}
 }else if(S.PreparedSkinned){
  auto& Pose=S.CoatingPose;Changed=S.Positions.Num()!=int32(Pose.LOD->GetNumVertices());S.Positions.SetNumUninitialized(Pose.LOD->GetNumVertices(),EAllowShrinking::No);S.GeometryChangedFlags.SetNumUninitialized(S.Positions.Num(),EAllowShrinking::No);
  // UObject and morph state is captured on the game thread. Workers read the
  // immutable pose and own distinct vertices; no concurrent mesh queries.
  const bool CountChanged=Changed;
  ParallelFor(TEXT("PaintCollisionGeometry"),S.Positions.Num(),512,[&](int32 I){const FVector Current=Pose.Position(I);S.GeometryChangedFlags[I]=CountChanged||S.Positions[I]!=Current;S.Positions[I]=Current;},!VertexParallel||S.Positions.Num()<2048?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);
  Changed=S.GeometryChangedFlags.Contains(1);
 }
 S.GeometrySnapshotAvailable=true;
 if(!Changed)return;++S.GeometryRevision;
 for(int32 I=0;I<S.Film.Faces.Num();++I)for(int32 E=0;E<3;++E)if(S.Positions.IsValidIndex(S.Indices[I*3+E]))S.Film.Faces[I].P[E]=S.Positions[S.Indices[I*3+E]];
 if(S.FilmTopologyReady)S.UpdateNormals();S.Refit();
}
void AOKPaintSprayer::MakeSurface(UPrimitiveComponent* C,int32 SectionIndex,TArray<FVector>* CurrentPositions)
{
 auto S=MakeShared<FSurface>();S->Component=C;S->Section=SectionIndex;
 // Explicitly follow late-ticking gallery owners as well as component animation.
 if(C->GetOwner())AddTickPrerequisiteActor(C->GetOwner());AddTickPrerequisiteComponent(C);
 if(auto* P=Cast<UProceduralMeshComponent>(C)){
  auto* Section=P->GetProcMeshSection(SectionIndex);if(!Section)return;for(uint32 I:Section->ProcIndexBuffer)S->Indices.Add(I);
 }else if(auto* K=Cast<USkinnedMeshComponent>(C)){
  auto* Mesh=Cast<USkeletalMesh>(K->GetSkinnedAsset());if(!Mesh||!Mesh->GetResourceForRendering()||Mesh->GetResourceForRendering()->LODRenderData.IsEmpty())return;
  S->Asset=Mesh;const auto& LOD=Mesh->GetResourceForRendering()->LODRenderData[0];
  if(!LOD.StaticVertexBuffers.PositionVertexBuffer.GetAllowCPUAccess()||!LOD.StaticVertexBuffers.PositionVertexBuffer.GetVertexData()){UE_LOG(LogTemp,Warning,TEXT("Paint requires retained CPU LOD0 vertices: %s"),*C->GetPathName());return;}
  auto* Indices=LOD.MultiSizeIndexContainer.GetIndexBuffer();if(!Indices)return;
  for(const auto& Section:LOD.RenderSections)if(!Section.bDisabled)for(uint32 I=Section.BaseIndex;I<Section.BaseIndex+Section.NumTriangles*3;++I)S->Indices.Add(Indices->Get(I));
 }else return;
 if(S->Indices.IsEmpty()||S->Indices.Num()/3>262144){UE_LOG(LogTemp,Warning,TEXT("Paint unsupported empty/oversized topology %s"),*C->GetPathName());return;}
 S->Film.Faces.SetNum(S->Indices.Num()/3);
 if(CurrentPositions){S->Positions=MoveTemp(*CurrentPositions);S->GeometrySnapshotAvailable=true;for(int32 I=0;I<S->Film.Faces.Num();++I)for(int32 E=0;E<3;++E)if(S->Positions.IsValidIndex(S->Indices[I*3+E]))S->Film.Faces[I].P[E]=S->Positions[S->Indices[I*3+E]];}else RefreshGeometry(*S);
 if(S->Positions.IsEmpty())return;for(int32 Index:S->Indices)if(!S->Positions.IsValidIndex(Index))return;
 if(Surfaces.IsEmpty()&&Targets.IsEmpty())SharedAtlas.Reset(Settings.AtlasResolution);
 S->AtlasSize=SharedAtlas.Resolution;
 for(auto& Face:S->Film.Faces)Face.ChartSize=FOKPaintMask::PhysicalChartSize(Face.Area(),SharedAtlas.Resolution);
 S->TopologyPositions=S->Positions;
 for(int32 I=0;I<S->Film.Faces.Num();++I){S->Order.Add(I);S->BuildCenters.Add(S->Film.Faces[I].Center());}
 S->Build(0,S->Order.Num());S->BuildCenters.Empty();S->Refit();
 UE_LOG(LogTemp,Display,TEXT("Paint surface faces=%d atlas=%dx%d chart=%d component=%s"),S->Film.Faces.Num(),S->AtlasSize,S->AtlasSize,S->Mask.Size,*C->GetPathName());Surfaces.Add(MoveTemp(S));
}
bool AOKPaintSprayer::InitializeCoating(FSurface& S)
{
 auto* C=S.Component.Get();if(!C)return false;
 auto* Base=FilmBase.Get();if(!Base)return false;
 S.Material=UMaterialInstanceDynamic::Create(Base,this);Materials.Add(S.Material);S.Material->SetVectorParameterValue(TEXT("PaintColor"),Settings.Color);S.MaterialColor=Settings.Color;
 if(CVarPaintAnimatedCoating.GetValueOnGameThread()!=0){auto* Coating=NewObject<UKKAffineProceduralMeshComponent>(this);Coating->bUseAnimatedStreams=true;Coating->bUseConservativeAnimatedBounds=CVarPaintConservativeCoatingBounds.GetValueOnGameThread()!=0;S.Overlay=Coating;}else S.Overlay=NewObject<UProceduralMeshComponent>(this);
 S.Overlay->ComponentTags.Add(TEXT("OKPaintOverlay"));S.Overlay->RegisterComponent();S.Overlay->AttachToComponent(C,FAttachmentTransformRules::SnapToTargetIncludingScale);S.Overlay->SetCollisionEnabled(ECollisionEnabled::NoCollision);S.Overlay->SetCastShadow(false);S.Overlay->SetMaterial(0,S.Material);S.Overlay->SetTranslucentSortPriority(C->TranslucencySortPriority+1);Overlays.Add(S.Overlay);
 return true;
}
void AOKPaintSprayer::DiscoverSurfaces(const FVector& A,const FVector& B,bool SourceCone)
{
 for(TActorIterator<AActor> It(GetWorld());It;++It){if(*It==this||It->IsHidden())continue;
  TInlineComponentArray<UPrimitiveComponent*> Components;It->GetComponents(Components);
  for(auto* C:Components){if(!C->IsVisible()||C->ComponentHasTag(TEXT("OKPaintOverlay")))continue;
   if(!C->bRenderInMainPass)continue;
   // KKS palette carriers and outline passes are not displayed material surfaces.
   if(auto* Preview=Cast<AKKPreviewActor>(*It)){
    const bool PartSurface=Preview->Parts.ContainsByPredicate([C](const auto& Part){return Part.Comp==C||(Part.SurfaceMat&&Part.SurfaceMat==C->GetMaterial(0));});
    // Body/face/clothing morphs replace the hidden poseable follower with a
    // skeletal display component carrying its own dynamic material instance.
    const bool MorphSurface=Preview->MouthSlots.ContainsByPredicate([C](const auto& Slot){return Slot.Comp==C||Slot.CpuComp==C;});
    if(!PartSurface&&!MorphSurface)continue;
   }
   if(!Cast<USkinnedMeshComponent>(C)&&!(Cast<UProceduralMeshComponent>(C)&&(It->IsA<AKKPreviewActor>()||It->ActorHasTag(TEXT("OKPaintable")))))continue;
   auto* Procedural=Cast<UProceduralMeshComponent>(C);
   if(!Procedural&&Surfaces.ContainsByPredicate([C](const auto& S){return S->Component==C;}))continue;
   // Renderer bounds may describe the tiny bind mesh rather than the current
   // KKS bone palette. Broad phase must use the same deformed vertices as hits.
   TArray<FVector> CurrentPositions;FBox SurfaceBounds=C->Bounds.GetBox();
   if(auto* K=Cast<USkinnedMeshComponent>(C)){
    auto* Mesh=Cast<USkeletalMesh>(K->GetSkinnedAsset());if(!Mesh||!Mesh->GetResourceForRendering()||Mesh->GetResourceForRendering()->LODRenderData.IsEmpty())continue;
    const auto& Positions=Mesh->GetResourceForRendering()->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;
    if(!Positions.GetAllowCPUAccess()||!Positions.GetVertexData())continue;
    FSurface Probe;Probe.Component=C;Probe.Asset=Mesh;RefreshGeometry(Probe);CurrentPositions=MoveTemp(Probe.Positions);if(CurrentPositions.IsEmpty())continue;
    SurfaceBounds.Init();for(const FVector& P:CurrentPositions)SurfaceBounds+=P;
   }
   auto Intersects=[&](const FVector& From,const FVector& To,bool Cone){const double Along=FMath::Max(0.,FVector::DotProduct(SurfaceBounds.GetCenter()-From,(To-From).GetSafeNormal())+SurfaceBounds.GetExtent().Length());
    const double Angle=Cone?FMath::DegreesToRadians(FMath::Clamp(Settings.ScatterDegrees,0.f,30.f)):0.;FBox Box=SurfaceBounds.ExpandBy(Settings.BrushRadius+5+Along*FMath::Tan(Angle));
    if(Cone){const double Travel=FMath::Min(5.,Along/(FMath::Max(10.f,Settings.JetSpeed)*FMath::Cos(Angle)));Box.Max.Z+=490*Travel*Travel;}
    return Box.IsInside(From)||FMath::LineBoxIntersection(Box,From,To,To-From);};
   bool Candidate=Spraying&&Intersects(A,B,SourceCone);const double Interval=1./FMath::Clamp(Settings.SurfaceHz,5.f,60.f);
   if(!Candidate)for(const auto& Packet:Packets)if(Intersects(Packet.P,Packet.P+Packet.V*Interval+FVector(0,0,-490)*Interval*Interval,false)){Candidate=true;break;}
   if(!Candidate)continue;
   // Native KKS PMC sections beyond zero are duplicated outline passes.
   // Other paintable PMCs may contain independent material regions.
   if(Procedural){const int32 Count=It->IsA<AKKPreviewActor>()?FMath::Min(1,Procedural->GetNumSections()):Procedural->GetNumSections();for(int32 Section=0;Section<Count;++Section){const auto* Part=Procedural->GetProcMeshSection(Section);if(!Part||!Part->bSectionVisible||Part->ProcIndexBuffer.IsEmpty())continue;if(!Surfaces.ContainsByPredicate([C,Section](const auto& S){return S->Component==C&&S->Section==Section;}))MakeSurface(C,Section);}}
   else MakeSurface(C,0,&CurrentPositions);
  }
 }
}
bool AOKPaintSprayer::FindPaintHit(const FVector& A,const FVector& B,double Radius,int32& Target,int32& Triangle,FVector& Bary,double& Time)
{
 const bool CachedBounds=CVarPaintCachedSweepBounds.GetValueOnGameThread()!=0;
 bool Found=false;Time=1.;
 for(int32 S=0;S<Surfaces.Num();++S)if(Surfaces[S]->IsDisplayed()&&Surfaces[S]->Sweep(A,B,Radius,CachedBounds,Triangle,Bary,Time)){Target=S;Found=true;if(Time==0)return true;}
 return Found;
}
void AOKPaintSprayer::DrawFilm(FSurface& S,bool UpdateMask,TArray<FKKAnimatedStreamRequest>* PendingStreams,TMap<int32,TArray<FIntRect>>* PendingAtlas,TArray<FAtlasChartWrite>* PendingCharts)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintCoating);
 if(!S.Component.IsValid())return;
 // Collision candidates do not need two atlases and an overlay. Allocate
 // render resources only when this particular material actually gets paint.
 if(!S.Overlay){if(S.Film.Active.IsEmpty()||!InitializeCoating(S))return;UpdateMask=true;}
 const bool ColorChanged=S.MaterialColor!=Settings.Color;
 if(ColorChanged){S.MaterialColor=Settings.Color;S.Material->SetVectorParameterValue(TEXT("PaintColor"),Settings.Color);}
 if(UpdateMask){for(int32 I:S.Mask.DirtyFaces)S.PendingDirty[0].Add(I);S.Mask.DirtyFaces.Reset();if(S.PendingDirty[0].IsEmpty())UpdateMask=false;}
 auto& V=S.CoatingVertices;auto& N=S.CoatingNormals;auto& UV=S.CoatingUV;auto& Indices=S.CoatingIndices;auto& Colors=S.CoatingColors;auto& Tangents=S.CoatingTangents;
 auto& RenderFaces=S.RenderFaces;auto& VertexReads=S.VertexReads;
 if(CVarPaintCoatingBufferReuse.GetValueOnGameThread()==0){V.Empty();N.Empty();UV.Empty();Indices.Empty();Colors.Empty();Tangents.Empty();RenderFaces.Empty();VertexReads.Empty();S.CoatingFaces.Empty();S.CoatingPose.Matrices.Empty();S.CoatingPose.Morph.Empty();S.CoatingPose.Fallback.Empty();}
 // Scratch capacity survives frames, but profile pointers and pose reads are
 // rebuilt after every mask-map update; no cached pointer crosses a simulation.
 RenderFaces.Reset();VertexReads.Reset();
 const FMatrix RenderMatrix=S.Component->GetRenderMatrix();const auto Transform=S.Component->GetComponentTransform();const int32 PageSize=SharedAtlas.Resolution;
 // Rendering follows the current pose every frame; film/BVH simulation retains
 // its independent cadence. Do not move the layer farther from the skin.
 const FMatrix WorldToLocal=Transform.ToInverseMatrixWithScale();const FQuat InverseRotation=Transform.GetRotation().Inverse();
 const bool HasPreviousPose=S.HasRenderedPaint&&S.RenderPositions.Num()==S.Positions.Num();
 if(S.RenderPositions.Num()!=S.Positions.Num()){S.RenderPositions=S.Positions;S.RenderMarks.SetNumZeroed(S.Positions.Num());}
 if(++S.RenderStamp==0){for(auto& Mark:S.RenderMarks)Mark=0;S.RenderStamp=1;}
 const int32 ParallelMode=CVarPaintCoatingParallel.GetValueOnGameThread();
 const int32 Capacity=S.Film.Active.Num()*3;V.Reserve(Capacity);N.Reserve(Capacity);UV.Reserve(Capacity);Indices.Reserve(Capacity);Colors.Reserve(Capacity);Tangents.Reserve(Capacity);
 auto* Procedural=Cast<UProceduralMeshComponent>(S.Component.Get());
 const auto* Section=Procedural?Procedural->GetProcMeshSection(S.Section):nullptr;
 auto* Skinned=Cast<USkinnedMeshComponent>(S.Component.Get());
 const bool SharedSnapshot=S.GeometrySnapshotAvailable&&CVarPaintSharedPoseSnapshot.GetValueOnGameThread()!=0;
 auto& Pose=S.CoatingPose;const bool HasPose=!SharedSnapshot&&Skinned&&S.Asset.IsValid()&&Skinned->GetSkinnedAsset()==S.Asset.Get()&&[&](){TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintCoatingPose);return Pose.Prepare(Skinned);}();
 RenderFaces.Reserve(S.Film.Active.Num());VertexReads.Reserve(Capacity);
 auto QueueVertex=[&](int32 Vertex,const FVector& Fallback){if(S.RenderMarks.IsValidIndex(Vertex)&&S.RenderMarks[Vertex]!=S.RenderStamp){S.RenderMarks[Vertex]=S.RenderStamp;VertexReads.Add({Vertex,Fallback});}};
 auto QueueFace=[&](int32 Id){const auto& Face=S.Film.Faces[Id];for(int32 E=0;E<3;++E)QueueVertex(S.Indices[Id*3+E],Face.P[E]);};
 for(int32 Id:S.Film.Active){if(S.Film.Faces[Id].Volume<1.e-9)continue;const auto* Profile=S.Mask.Height.Find(Id);if(!Profile)continue;RenderFaces.Add({Id,1.,Profile});}
 for(const auto& RenderFace:RenderFaces){const int32 Id=RenderFace.Id;if(S.Charts.Contains(Id))continue;
  auto Allocation=SharedAtlas.Allocate(S.Mask.Resolution(S.Film.Faces[Id]));S.Charts.Add(Id,Allocation);S.PendingDirty[0].Add(Id);UpdateMask=true;
  while(Targets.Num()<=Allocation.Page){auto* RT=NewObject<UTextureRenderTarget2D>(this);RT->ClearColor=FLinearColor::Transparent;RT->bAutoGenerateMips=false;RT->Filter=TF_Bilinear;RT->InitCustomFormat(PageSize,PageSize,PF_FloatRGBA,true);RT->UpdateResourceImmediate();Targets.Add(RT);AtlasPixels.AddDefaulted_GetRef().SetNumZeroed(PageSize*PageSize);}
  auto& Face=S.Film.Faces[Id];const FVector2D Base(Allocation.Origin.X,Allocation.Origin.Y);Face.UV[0]=(Base+FVector2D(.5,.5))/PageSize;Face.UV[1]=(Base+FVector2D(Allocation.Size-1.5,.5))/PageSize;Face.UV[2]=(Base+FVector2D(.5,Allocation.Size-1.5))/PageSize;
  if(!S.PageMaterials.Contains(Allocation.Page)){auto* MID=S.PageMaterials.IsEmpty()?S.Material:UMaterialInstanceDynamic::Create(FilmBase.Get(),this);MID->SetTextureParameterValue(TEXT("Film"),Targets[Allocation.Page]);MID->SetVectorParameterValue(TEXT("PaintColor"),Settings.Color);Materials.AddUnique(MID);S.PageMaterials.Add(Allocation.Page,MID);S.Overlay->SetMaterial(Allocation.Page,MID);}
 }
 if(ColorChanged)for(auto& Entry:S.PageMaterials)Entry.Value->SetVectorParameterValue(TEXT("PaintColor"),Settings.Color);
 // TSet removal/reinsertion during transport is not a topology change.
 RenderFaces.Sort([](const auto& A,const auto& B){return A.Id<B.Id;});
 auto* GpuSource=Cast<UKKAffineProceduralMeshComponent>(Procedural);auto* GpuTarget=Cast<UKKAffineProceduralMeshComponent>(S.Overlay);
 FKKPrivateGpuSurfaceView GpuView;
 const bool GpuCandidate=CVarPaintGpuCoating.GetValueOnGameThread()!=0&&GpuSource&&GpuTarget&&GpuSource->UsesAnimatedStreams()&&GpuTarget->UsesAnimatedStreams()&&Cast<AKKPreviewActor>(GpuSource->GetOwner())&&Cast<AKKPreviewActor>(GpuSource->GetOwner())->CapturePrivateGpuSurface(GpuSource,S.Section,GpuView);
 const bool PreviousGpuCandidate=S.GpuCandidateLast;S.GpuCandidateLast=GpuCandidate;
 bool GpuFast=GpuCandidate&&PreviousGpuCandidate&&HasPreviousPose&&S.CoatingRevision==S.GeometryRevision&&S.CoatingTransform.Equals(Transform)&&S.CoatingFaces.Num()==RenderFaces.Num()&&!RenderFaces.IsEmpty()&&!S.PageStreams.IsEmpty();
 if(GpuFast)for(int32 I=0;I<RenderFaces.Num();++I)if(S.CoatingFaces[I]!=RenderFaces[I].Id){GpuFast=false;break;}
 if(GpuCandidate)for(auto& Entry:S.PageStreams){auto& Page=Entry.Value;
  if(Page.GpuEpoch!=GpuTarget->GetGpuStreamProxyEpoch()){Page.GpuGeneration=Page.GpuUploadedRevision=0;Page.GpuReceipt.Reset();Page.GpuRejected=false;Page.GpuRetryFrame=0;Page.GpuEpoch=GpuTarget->GetGpuStreamProxyEpoch();}
  if(Page.GpuSourceEpoch!=GpuSource->GetGpuStreamProxyEpoch()){Page.GpuUploadedRevision=0;Page.GpuRejected=false;Page.GpuRetryFrame=0;Page.GpuSourceEpoch=GpuSource->GetGpuStreamProxyEpoch();GpuFast=false;}
  if(Page.GpuReceipt){const int32 Status=Page.GpuReceipt->Status.Load();if(Status==FKKGpuStreamReceipt::Pending)GpuFast=false;else{Page.GpuGeneration=Page.GpuReceipt->Generation;if(Status==FKKGpuStreamReceipt::Published){++S.GpuCoatingPublished;if(Page.GpuSentSourceEpoch==Page.GpuSourceEpoch)Page.GpuUploadedRevision=Page.GpuSentRevision;}else{GpuFast=false;Page.GpuRejected=!Page.GpuReceipt->Retryable;Page.GpuRetryFrame=GFrameCounter+8;if(Page.GpuRejected)UE_LOG(LogTemp,Warning,TEXT("GPU coating retains CPU fallback: %s"),*Page.GpuReceipt->Error);}Page.GpuReceipt.Reset();}}
  if(!Page.Works.IsEmpty()&&(Page.GpuBindings.IsEmpty()||Page.GpuRejected||GFrameCounter<Page.GpuRetryFrame))GpuFast=false;
 }
 // GPU render vertices do not require a second CPU skinning pass. Attached
 // droplet collision/visual positions still query their exact current vertices.
 {TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintCoatingGather);
  // Preserve original TSet traversal and first-face fallback selection.
  // Sorting render output must not change the CPU query transaction.
  if(!GpuFast||CVarPaintGpuCoatingSparseGather.GetValueOnGameThread()==0)for(int32 Id:S.Film.Active){if(S.Film.Faces[Id].Volume<1.e-9||!S.Mask.Height.Contains(Id))continue;QueueFace(Id);}
  if(GpuFast){for(const auto& Read:VertexReads)S.RenderMarks[Read.Id]=0;VertexReads.Reset();}
 }
 for(const auto& Drop:Attached)if(Drop.Visual.IsValid()&&Surfaces.IsValidIndex(Drop.Target)&&Surfaces[Drop.Target].Get()==&S&&S.Film.Faces.IsValidIndex(Drop.Face))QueueFace(Drop.Face);
 bool SparsePrivate=false;
 if(!SharedSnapshot&&Procedural)if(auto* Actor=Cast<AKKPreviewActor>(Procedural->GetOwner())){
  S.SparsePrivateIndices.Reset();for(const auto& Read:VertexReads)S.SparsePrivateIndices.Add(Read.Id);
  SparsePrivate=Actor->QueryPrivateGpuSurfaceVertices(Procedural,S.Section,S.SparsePrivateIndices,S.SparsePrivatePositions);
  if(!SparsePrivate){Actor->EnsurePrivateGpuQueryGeometry(Procedural,S.Section);Section=Procedural->GetProcMeshSection(S.Section);}
 }
 {TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintCoatingNormals);for(const auto& Read:VertexReads)S.ResolveNormal(Read.Id);}
 const bool UniqueTransforms=CVarPaintCoatingVertexTransforms.GetValueOnGameThread()!=0;
 if(UniqueTransforms){S.CoatingLocalPositions.SetNumUninitialized(S.RenderPositions.Num(),EAllowShrinking::No);S.CoatingLocalNormals.SetNumUninitialized(S.RenderPositions.Num(),EAllowShrinking::No);}
 // Gather UObject pose/morph state above, then skin distinct vertices from
 // immutable buffers. Each worker owns one output slot; marks never race.
 TArray<uint8> ChangedVertices;ChangedVertices.SetNumUninitialized(VertexReads.Num());
 ParallelFor(TEXT("PaintCoatingVertices"),VertexReads.Num(),ParallelMode==2?1:64,[&](int32 Work){const auto& Read=VertexReads[Work];FVector P=Read.Fallback;if(SparsePrivate)P=S.SparsePrivatePositions[Work];else if(SharedSnapshot&&S.Positions.IsValidIndex(Read.Id))P=S.Positions[Read.Id];else if(Section&&Section->ProcVertexBuffer.IsValidIndex(Read.Id))P=RenderMatrix.TransformPosition(Section->ProcVertexBuffer[Read.Id].Position);else if(HasPose)P=Pose.Position(Read.Id);ChangedVertices[Work]=!HasPreviousPose||S.RenderPositions[Read.Id]!=P;S.RenderPositions[Read.Id]=P;
  // All incident faces consume the same smooth normal and current position.
  // Retain the original arithmetic, including double precision transforms.
  if(UniqueTransforms){const FVector Smooth=S.VertexNormals[Read.Id]*S.CoatingSide;S.CoatingLocalPositions[Read.Id]=FVector(WorldToLocal.TransformPosition(P+Smooth*.025));S.CoatingLocalNormals[Read.Id]=(InverseRotation.RotateVector(Smooth)*Transform.GetScale3D()).GetSafeNormal();}
 },(ParallelMode==0||(ParallelMode!=2&&VertexReads.Num()<256))?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);
 const bool PoseChanged=!GpuFast&&ChangedVertices.Contains(1);
 auto Position=[&](int32 Vertex,const FVector& Fallback){return S.RenderMarks.IsValidIndex(Vertex)&&S.RenderMarks[Vertex]==S.RenderStamp?S.RenderPositions[Vertex]:Fallback;};
 const int32 LiveVertexCount=RenderFaces.Num()*3;
 const int32 Slack=FMath::Clamp(CVarPaintCoatingTopologySlack.GetValueOnGameThread(),0,100);
 if(RenderFaces.IsEmpty())S.CoatingFaceCapacity=0;
 else if(Slack==0)S.CoatingFaceCapacity=RenderFaces.Num();
 else if(RenderFaces.Num()>S.CoatingFaceCapacity)S.CoatingFaceCapacity=RenderFaces.Num()+FMath::Max(16,FMath::DivideAndRoundUp(RenderFaces.Num()*Slack,100));
 const int32 VertexCount=S.CoatingFaceCapacity*3;V.SetNumUninitialized(VertexCount,EAllowShrinking::No);N.SetNumUninitialized(VertexCount,EAllowShrinking::No);Tangents.SetNumUninitialized(VertexCount,EAllowShrinking::No);
 bool TopologyChanged=S.CoatingFaces.Num()!=RenderFaces.Num()||UV.Num()!=VertexCount;
 if(!TopologyChanged)for(int32 I=0;I<RenderFaces.Num();++I)if(S.CoatingFaces[I]!=RenderFaces[I].Id){TopologyChanged=true;break;}
 if(TopologyChanged){S.CoatingFaces.SetNumUninitialized(RenderFaces.Num(),EAllowShrinking::No);UV.SetNumUninitialized(VertexCount,EAllowShrinking::No);Indices.SetNumUninitialized(VertexCount,EAllowShrinking::No);Colors.SetNumUninitialized(VertexCount,EAllowShrinking::No);for(auto& Color:Colors)Color=FColor::White;
  for(int32 I=0;I<RenderFaces.Num();++I){S.CoatingFaces[I]=RenderFaces[I].Id;for(int32 E=0;E<3;++E){const int32 Out=I*3+E;Indices[Out]=Out;UV[Out]=S.Film.Faces[RenderFaces[I].Id].UV[E];}}
  for(int32 I=LiveVertexCount;I<VertexCount;++I){Indices[I]=I;UV[I]=UV[0];}
 }
 const bool UpdateVertices=TopologyChanged||PoseChanged||S.CoatingRevision!=S.GeometryRevision||!S.CoatingTransform.Equals(Transform);
 if(UpdateMask||UpdateVertices)ParallelFor(TEXT("PaintCoatingFaces"),RenderFaces.Num(),ParallelMode==2?1:64,[&](int32 Work){const auto& RenderFace=RenderFaces[Work];const int32 I=RenderFace.Id;auto F=S.Film.Faces[I];for(int32 E=0;E<3;++E)F.P[E]=Position(S.Indices[I*3+E],F.P[E]);const double Thickness=S.Film.Faces[I].Volume/S.Film.Faces[I].Area();const FVector Normal=F.Normal()*S.CoatingSide;
  const auto* Profile=RenderFace.Profile;
  if(!UpdateVertices)return;
  // Shared vertex offset and normals keep adjoining coating faces closed.
  const FProcMeshTangent Tangent(FVector(WorldToLocal.TransformVector(F.P[1]-F.P[0])).GetSafeNormal(),false);
  for(int32 E=0;E<3;++E){const int32 Out=Work*3+E;const int32 Vertex=S.Indices[I*3+E];
   if(UniqueTransforms){V[Out]=S.CoatingLocalPositions[Vertex];N[Out]=S.CoatingLocalNormals[Vertex];}
   else {const FVector Smooth=S.VertexNormals[Vertex]*S.CoatingSide;V[Out]=FVector(WorldToLocal.TransformPosition(F.P[E]+Smooth*.025));N[Out]=(InverseRotation.RotateVector(Smooth)*Transform.GetScale3D()).GetSafeNormal();}
   Tangents[Out]=Tangent;}
 },(ParallelMode==0||(ParallelMode!=2&&RenderFaces.Num()<128))?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);
 // Unused slots are exactly degenerate at a real coating vertex. They
 // neither rasterize paint nor enlarge bounds away from the animated skin.
 if(UpdateVertices)for(int32 I=LiveVertexCount;I<VertexCount;++I){V[I]=V[0];N[I]=N[0];Tangents[I]=Tangents[0];}
 // CPU page mirrors preserve every other component's charts when a block
 // upload contains shared neighbors. Materials only read these textures;
 // standard RHI texture updates are ordered on the render thread.
 if(UpdateMask){TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintAtlasUploadPrepare);TMap<int32,TArray<FIntRect>> Blocks;const bool SparseUploads=CVarPaintSparseUploads.GetValueOnGameThread()!=0;
  for(int32 Id:S.PendingDirty[0])if(const auto* A=S.Charts.Find(Id)){
   auto& Mirror=AtlasPixels[A->Page];const auto* Profile=S.Mask.Height.Find(Id);const int32 ChartEdge=A->Size;const auto& Face=S.Film.Faces[Id];const double Mean=Profile?FOKPaintMask::Mean(*Profile,ChartEdge):0.;const double Scale=Mean>0?Face.Volume/Face.Area()/Mean:0.;const FVector2D Flow=Face.GravityUV();
   const FAtlasChartWrite Write{Mirror.GetData(),Profile,PageSize,ChartEdge,A->Origin,Scale,Flow};
   if(PendingCharts&&CVarPaintBatchAtlasUploads.GetValueOnGameThread()!=0)PendingCharts->Add(Write);else FillAtlasChart(Write);
   auto& Rects=Blocks.FindOrAdd(A->Page);if(SparseUploads){Rects.Add(FIntRect(A->Origin,A->Origin+FIntPoint(ChartEdge,ChartEdge)));continue;}if(Rects.IsEmpty())Rects.Init(FIntRect(PageSize,PageSize,0,0),16);const int32 Block=PageSize/4;
   for(int32 BY=A->Origin.Y/Block;BY<=(A->Origin.Y+ChartEdge-1)/Block;++BY)for(int32 BX=A->Origin.X/Block;BX<=(A->Origin.X+ChartEdge-1)/Block;++BX){auto& R=Rects[BY*4+BX];R.Min.X=FMath::Min(R.Min.X,A->Origin.X);R.Min.Y=FMath::Min(R.Min.Y,A->Origin.Y);R.Max.X=FMath::Max(R.Max.X,A->Origin.X+ChartEdge);R.Max.Y=FMath::Max(R.Max.Y,A->Origin.Y+ChartEdge);}
  }
  if(PendingAtlas&&CVarPaintBatchAtlasUploads.GetValueOnGameThread()!=0){for(const auto& Entry:Blocks)PendingAtlas->FindOrAdd(Entry.Key).Append(Entry.Value);}
  else UploadAtlasBlocks(Blocks,SparseUploads);
  S.PendingDirty[0].Reset();
 }
 // Attached droplets share the same current-pose mapping as the coating.
 for(auto& Drop:Attached)if(Drop.Visual.IsValid()&&Surfaces.IsValidIndex(Drop.Target)&&Surfaces[Drop.Target].Get()==&S&&S.Film.Faces.IsValidIndex(Drop.Face)){
  auto Face=S.Film.Faces[Drop.Face];for(int32 E=0;E<3;++E)Face.P[E]=Position(S.Indices[Drop.Face*3+E],Face.P[E]);
  const FVector Center=Face.P[0]*Drop.Bary.X+Face.P[1]*Drop.Bary.Y+Face.P[2]*Drop.Bary.Z+Face.Normal()*S.CoatingSide*.2;
  if(auto* Owned=Cast<UKKAffineProceduralMeshComponent>(Drop.Visual.Get());Owned&&Owned->UsesAnimatedStreams()){
   // Keep float vertex offsets near their LWC component anchor. A teleport
   // rebases the component; ordinary attachment movement changes streams only.
   if((Center-Owned->GetComponentLocation()).GetAbsMax()>1024.)Owned->SetWorldLocation(Center);
   const FVector Offset=Center-Owned->GetComponentLocation();const double R=Drop.Radius;
   Drop.RenderVertices.SetNumUninitialized(6,EAllowShrinking::No);
   const FVector Local[]={FVector(R,0,0),FVector(-R,0,0),FVector(0,R,0),FVector(0,-R,0),FVector(0,0,R),FVector(0,0,-R)};
   for(int32 I=0;I<6;++I)Drop.RenderVertices[I]=Offset+Local[I];
   const FBox WorldBox=S.Component->CalcBounds(S.Component->GetComponentTransform()).GetBox();
   Owned->ExpandAnimatedRenderBounds(WorldBox.ShiftBy(-Owned->GetComponentLocation()).ExpandBy(R+.2));
   if(PendingStreams)PendingStreams->Add({Owned,0,&Drop.RenderVertices,nullptr,nullptr});
   else UKKAffineProceduralMeshComponent::UpdateStreams(Owned,0,Drop.RenderVertices,{}, {}, {}, {});
  }else Drop.Visual->SetWorldLocation(Center);
 }
 S.HasRenderedPaint=!RenderFaces.IsEmpty();if(UpdateVertices){S.CoatingRevision=S.GeometryRevision;S.CoatingTransform=Transform;
  // Page membership and immutable streams change with wet topology, not with
  // animation. Keep owned buffers; no map/UV/index/color rebuild per pose.
  const bool RebuildPages=TopologyChanged||S.PageStreams.IsEmpty()||CVarPaintCoatingBufferReuse.GetValueOnGameThread()==0;
  auto PublishStreams=[&](int32 Page,const TArray<FVector>& Positions,const TArray<FVector>& Normals,const TArray<FVector2D>& Texcoords,const TArray<FColor>& VertexColors,const TArray<FProcMeshTangent>& FrameTangents){
   auto* Owned=Cast<UKKAffineProceduralMeshComponent>(S.Overlay);
   // Arrays are surface/page-owned and no other surface can mutate them.
   // The caller joins this batch before returning from the same Tick.
   if(PendingStreams&&!RebuildPages&&Owned&&Owned->UsesAnimatedStreams()&&CVarPaintBatchCoatingStreams.GetValueOnGameThread()!=0)
    PendingStreams->Add({Owned,Page,&Positions,&Normals,&FrameTangents});
   else UKKAffineProceduralMeshComponent::UpdateStreams(S.Overlay,Page,Positions,Normals,Texcoords,VertexColors,FrameTangents);
  };
  if(RebuildPages){
   for(auto& Entry:S.PageStreams)Entry.Value.Works.Reset();
   for(int32 Work=0;Work<RenderFaces.Num();++Work)S.PageStreams.FindOrAdd(S.Charts[RenderFaces[Work].Id].Page).Works.Add(Work);
   for(int32 Page:S.PageSections)if(!S.PageStreams.Contains(Page)||S.PageStreams[Page].Works.IsEmpty())S.Overlay->ClearMeshSection(Page);
   S.PageSections.Reset();
  }
  for(auto& Group:S.PageStreams){const int32 Page=Group.Key;auto& Streams=Group.Value;if(Streams.Works.IsEmpty())continue;S.PageSections.Add(Page);
   auto& PageVertexCapacity=S.PageCapacity.FindOrAdd(Page);const int32 Live=Streams.Works.Num()*3;if(Live>PageVertexCapacity)PageVertexCapacity=Live+3*FMath::Max(16,FMath::DivideAndRoundUp(Streams.Works.Num()*Slack,100));if(Slack==0)PageVertexCapacity=Live;
   if(CVarPaintCoatingBufferReuse.GetValueOnGameThread()!=0&&S.PageSections.Num()==1&&Streams.Works.Num()==RenderFaces.Num()&&PageVertexCapacity==V.Num()){
    // One atlas page consumes the already assembled surface streams directly.
    // Preserve the same degenerate capacity and vertex order as the oracle.
    if(const auto* Existing=S.Overlay->GetProcMeshSection(Page);Existing&&Existing->ProcVertexBuffer.Num()==V.Num()){++CoatingUpdates;PublishStreams(Page,V,N,UV,Colors,Tangents);}else{++CoatingRebuilds;S.Overlay->CreateMeshSection(Page,V,Indices,N,UV,Colors,Tangents,false);}
    continue;
   }
   auto& PV=Streams.Positions;auto& PN=Streams.Normals;auto& PU=Streams.UV;auto& PC=Streams.Colors;auto& PT=Streams.Tangents;auto& PageIndices=Streams.Indices;
   const bool Resize=PV.Num()!=PageVertexCapacity;
   PV.SetNumUninitialized(PageVertexCapacity,EAllowShrinking::No);PN.SetNumUninitialized(PageVertexCapacity,EAllowShrinking::No);PT.SetNumUninitialized(PageVertexCapacity,EAllowShrinking::No);
   if(RebuildPages||Resize){PU.SetNumUninitialized(PageVertexCapacity,EAllowShrinking::No);PC.SetNumUninitialized(PageVertexCapacity,EAllowShrinking::No);PageIndices.SetNumUninitialized(PageVertexCapacity,EAllowShrinking::No);for(int32 I=0;I<PageVertexCapacity;++I){PageIndices[I]=I;PC[I]=FColor::White;}}
   for(int32 Work=0;Work<Streams.Works.Num();++Work)for(int32 E=0;E<3;++E){const int32 In=Streams.Works[Work]*3+E,Out=Work*3+E;PV[Out]=V[In];PN[Out]=N[In];PT[Out]=Tangents[In];if(RebuildPages||Resize)PU[Out]=UV[In];}
   for(int32 I=Live;I<PageVertexCapacity;++I){PV[I]=PV[0];PN[I]=PN[0];PT[I]=PT[0];if(RebuildPages||Resize)PU[I]=PU[0];}
   if(const auto* Existing=S.Overlay->GetProcMeshSection(Page);Existing&&Existing->ProcVertexBuffer.Num()==PV.Num()){++CoatingUpdates;PublishStreams(Page,PV,PN,PU,PC,PT);}else{++CoatingRebuilds;S.Overlay->CreateMeshSection(Page,PV,PageIndices,PN,PU,PC,PT,false);}
  }
  // Bindings derive only from runtime wet topology and the existing geometry
  // normal cadence. They never refer to card-specific prebaked resources.
  if(GpuCandidate){++S.GpuBindingRevision;for(auto& Group:S.PageStreams){auto& Page=Group.Value;if(Page.Works.IsEmpty())continue;const auto* Existing=GpuTarget->GetProcMeshSection(Group.Key);if(!Existing)continue;
   const int32 Count=Existing->ProcVertexBuffer.Num(),Live=Page.Works.Num()*3;Page.GpuBindings.SetNumUninitialized(Count);Page.GpuRevision=S.GpuBindingRevision;
   for(int32 Work=0;Work<Page.Works.Num();++Work){const int32 Face=RenderFaces[Page.Works[Work]].Id;for(int32 E=0;E<3;++E){const int32 Vertex=S.Indices[Face*3+E];const FVector Smooth=S.VertexNormals[Vertex]*S.CoatingSide;auto& B=Page.GpuBindings[Work*3+E];B.Vertices=FUintVector4(Vertex,S.Indices[Face*3],S.Indices[Face*3+1],0);B.Offset=FVector4f(FVector3f(FVector(WorldToLocal.TransformVector(Smooth*.025))),0);B.Normal=FVector4f(FVector3f((InverseRotation.RotateVector(Smooth)*Transform.GetScale3D()).GetSafeNormal()),0);}}
   for(int32 I=Live;I<Count;++I)Page.GpuBindings[I]=Page.GpuBindings[0];
  }}
 }
 if(GpuFast){++S.GpuCoatingFrames;TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintGpuCoatingSubmit);const FMatrix44f SourceToOverlay(RenderMatrix*WorldToLocal);const FBox Bounds=S.Component->CalcBounds(Transform).GetBox().TransformBy(WorldToLocal).ExpandBy(.03);
  for(auto& Group:S.PageStreams){auto& Page=Group.Value;if(Page.Works.IsEmpty())continue;FKKGpuCoatingRequest Request;Request.SourceEpoch=Page.GpuSourceEpoch;Request.TargetEpoch=Page.GpuEpoch;Request.ExpectedGeneration=Page.GpuGeneration;Request.Revision=Page.GpuRevision;Request.SourceToOverlay=SourceToOverlay;Request.ConservativeLocalBounds=Bounds;if(Page.GpuUploadedRevision!=Page.GpuRevision)Request.Bindings=Page.GpuBindings;Page.GpuSentRevision=Page.GpuRevision;Page.GpuSentSourceEpoch=Page.GpuSourceEpoch;Page.GpuReceipt=GpuTarget->SubmitGpuCoating(Group.Key,GpuSource,S.Section,MoveTemp(Request));}
 }
 RenderFaces.Reset();VertexReads.Reset();
}
void AOKPaintSprayer::FillAtlasChart(const FAtlasChartWrite& Write)
{
 for(int32 Y=0;Y<Write.Edge;++Y)for(int32 X=0;X<Write.Edge;++X){const double H=Write.Heights?(*Write.Heights)[Y*Write.Edge+X]*Write.Scale:0.;Write.Pixels[(Write.Origin.Y+Y)*Write.PageSize+Write.Origin.X+X]=FFloat16Color(H>0?FLinearColor(FMath::Clamp(H/.025,0.,1.),Write.Flow.X,Write.Flow.Y,H):FLinearColor::Transparent);}
}
void AOKPaintSprayer::UploadAtlasBlocks(const TMap<int32,TArray<FIntRect>>& Blocks,bool SparseUploads)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintAtlasUploadCopy);
 const int32 PageSize=SharedAtlas.Resolution;
 // All charts have finished writing their disjoint CPU mirror regions. Copy
 // the final page once; merged rectangles retain untouched neighbors exactly.
 for(const auto& Entry:Blocks){const int32 Page=Entry.Key;struct FRow{FUpdateTextureRegion2D Region;uint32 Offset,Pitch;};TArray<FRow> Uploads;auto Pixels=MakeShared<TArray<FFloat16Color>,ESPMode::ThreadSafe>();const auto Regions=SparseUploads?SharedAtlas.UploadRects(Entry.Value):Entry.Value;
  for(const auto& R:Regions)if(R.Max.X>R.Min.X&&R.Max.Y>R.Min.Y){const int32 Offset=Pixels->Num();Pixels->AddUninitialized(R.Width()*R.Height());for(int32 Y=0;Y<R.Height();++Y)FMemory::Memcpy(Pixels->GetData()+Offset+Y*R.Width(),AtlasPixels[Page].GetData()+(R.Min.Y+Y)*PageSize+R.Min.X,R.Width()*sizeof(FFloat16Color));Uploads.Add({FUpdateTextureRegion2D(R.Min.X,R.Min.Y,0,0,R.Width(),R.Height()),uint32(Offset),uint32(R.Width()*sizeof(FFloat16Color))});}
  auto* Resource=Targets[Page]->GameThread_GetRenderTargetResource();ENQUEUE_RENDER_COMMAND(OKPaintSharedUpload)([Resource,Pixels,Uploads=MoveTemp(Uploads)](FRHICommandListImmediate& Cmd){if(!Resource)return;const auto Texture=Resource->GetRenderTargetTexture();if(!Texture)return;for(const auto& Row:Uploads)Cmd.UpdateTexture2D(Texture,0,Row.Region,Row.Pitch,reinterpret_cast<const uint8*>(Pixels->GetData()+Row.Offset));});
 }
}
void AOKPaintSprayer::Splash(FVector P,FVector N,bool Floor,double Volume)
{
 if(SplashSystem&&OwnedFX.Num()<FMath::Clamp(Settings.MaxImpactEffects,1,64)&&GetWorld()->GetTimeSeconds()>=NextSplashAt){NextSplashAt=GetWorld()->GetTimeSeconds()+.05;auto* FX=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),SplashSystem,P,N.Rotation(),FVector(1),true,true);if(FX){FX->SetVariableLinearColor(TEXT("User.PaintColor"),Settings.Color);OwnedFX.Add(FX);}}
 if(!Floor)return;
 auto* M=DropBase.Get();if(!M)return;
 // A small geometry stain follows any rigid floor/wall, never used on skin.
 auto* Stain=NewObject<UProceduralMeshComponent>(this);Stain->RegisterComponent();Stain->ComponentTags.Add(TEXT("OKPaintOverlay"));Stain->SetCollisionEnabled(ECollisionEnabled::NoCollision);Stain->SetCastShadow(false);auto* MID=UMaterialInstanceDynamic::Create(M,this);MID->SetVectorParameterValue(TEXT("PaintColor"),Settings.Color);Materials.Add(MID);Stain->SetMaterial(0,MID);Overlays.Add(Stain);
 const double Radius=FMath::Clamp(FMath::Sqrt(Volume/(PI*.015)),.15,2.);FVector U,V;N.FindBestAxisVectors(U,V);TArray<FVector> Vertices={P+N*.1-U*Radius-V*Radius,P+N*.1+U*Radius-V*Radius,P+N*.1+U*Radius+V*Radius,P+N*.1-U*Radius+V*Radius};TArray<FVector> Normals;Normals.Init(N,4);TArray<FVector2D> UV={FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)};TArray<FColor> Colors;Colors.Init(FColor::White,4);TArray<FProcMeshTangent> Tangents;
 Stain->CreateMeshSection(0,Vertices,{0,1,2,0,2,3},Normals,UV,Colors,Tangents,false);
 FloorStains.Add(Stain);if(FloorStains.Num()>FMath::Clamp(Settings.MaxFloorStains,8,512)){auto Old=FloorStains[0];FloorStains.RemoveAt(0);Overlays.Remove(Old);Materials.Remove(Cast<UMaterialInstanceDynamic>(Old->GetMaterial(0)));Old->DestroyComponent();}
}
void AOKPaintSprayer::Step(double Dt)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintStep);
 const double BallisticStart=FPlatformTime::Seconds();
 if(Spraying&&Settings.Flow>0&&LiveSimulationParticles()<FMath::Clamp(Settings.MaxSimulationParticles,8,256)){JetTime+=Dt;while(JetTime+1.e-10>=1./30. && LiveSimulationParticles()<FMath::Clamp(Settings.MaxSimulationParticles,8,256)){JetTime-=1./30.;FPacket P;P.P=Nozzle;P.V=Random.VRandCone(Direction,FMath::DegreesToRadians(FMath::Clamp(Settings.ScatterDegrees,0.f,30.f)))*FMath::Clamp(Settings.JetSpeed,10.f,10000.f);P.Volume=FMath::Max(0.f,Settings.Flow)/30.;Emitted+=P.Volume;Packets.Add(P);}}
 else JetTime=0;
 // Every sweep in this fixed step uses the same ignored components.
 // Rebuild at each step so removal/replacement is never hidden by a cache.
 FCollisionQueryParams Q(SCENE_QUERY_STAT(OKPaint),false,this);
 if(!Packets.IsEmpty())for(const auto& S:Surfaces)if(S->Component.IsValid())Q.AddIgnoredComponent(S->Component.Get());
 struct FPacketSweep{FVector Next=FVector::ZeroVector,Bary=FVector::ZeroVector;double Radius=0.,Time=1.;int32 Target=INDEX_NONE,Face=INDEX_NONE;bool Hit=false;};
 TArray<FPacketSweep,TInlineAllocator<64>> Sweeps;
 const int32 SweepMode=CVarPaintPacketSweeps.GetValueOnGameThread();const bool BatchSweeps=SweepMode==2||(SweepMode==1&&Packets.Num()>=8);
 if(BatchSweeps){
  TArray<TPair<int32,const FSurface*>,TInlineAllocator<64>> VisibleSurfaces;for(int32 I=0;I<Surfaces.Num();++I)if(Surfaces[I]->IsDisplayed())VisibleSurfaces.Emplace(I,Surfaces[I].Get());
  const bool CachedBounds=CVarPaintCachedSweepBounds.GetValueOnGameThread()!=0;Sweeps.SetNum(Packets.Num());
  // Geometry is immutable throughout the packet loop. Workers read captured
  // surfaces only; world collision, deposits, RNG and removals keep GT order.
  ParallelFor(TEXT("PaintPacketSweeps"),Packets.Num(),1,[&](int32 I){const auto& P=Packets[I];auto& R=Sweeps[I];R.Next=P.P+P.V*Dt+FVector(0,0,-490)*Dt*Dt;R.Radius=P.Drip?FMath::Pow(P.Volume*3/(4*PI),1./3.):.15;
   for(const auto& Surface:VisibleSurfaces)if(Surface.Value->Sweep(P.P,R.Next,R.Radius,CachedBounds,R.Face,R.Bary,R.Time)){R.Hit=true;R.Target=Surface.Key;if(R.Time==0)break;}
  });
 }
 for(int32 I=Packets.Num()-1;I>=0;--I){auto& P=Packets[I];P.Age+=Dt;const FVector Next=P.P+P.V*Dt+FVector(0,0,-490)*Dt*Dt;
  FHitResult Hit;
  const double HitRadius=P.Drip?FMath::Pow(P.Volume*3/(4*PI),1./3.):.15;const bool WorldHit=GetWorld()->SweepSingleByChannel(Hit,P.P,Next,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(HitRadius),Q);
  int32 S,F;FVector Bary;double Time;bool PaintHit;
  if(BatchSweeps){const auto& R=Sweeps[I];S=R.Target;F=R.Face;Bary=R.Bary;Time=R.Time;PaintHit=R.Hit;}else PaintHit=FindPaintHit(P.P,Next,HitRadius,S,F,Bary,Time);
  if(PaintHit&&(!WorldHit||Time<Hit.Time)){auto& Surface=*Surfaces[S];Surface.EnsureFilmTopology();const FVector Point=Surface.Film.Faces[F].P[0]*Bary.X+Surface.Film.Faces[F].P[1]*Bary.Y+Surface.Film.Faces[F].P[2]*Bary.Z;
   if(Surface.CoatingSide==0){const double Incoming=FVector::DotProduct(Surface.Film.Faces[F].Normal(),-P.V.GetSafeNormal());Surface.CoatingSide=Incoming>=0?1.:-1.;UE_LOG(LogTemp,Display,TEXT("Paint contact winding side=%.0f incoming_dot=%.6f component=%s"),Surface.CoatingSide,Incoming,*Surface.Component->GetPathName());}
   TMap<int32,TArray<float>> Brushes;double Sum=0;const FVector N=Surface.Film.Faces[F].Normal();
   TArray<int32> Visit={F};TSet<int32> Seen;for(int32 Cursor=0;Cursor<Visit.Num();++Cursor){const int32 J=Visit[Cursor];if(Seen.Contains(J))continue;Seen.Add(J);const auto& Face=Surface.Film.Faces[J];const double Dist=FVector::Dist(FMath::ClosestPointOnTriangleToPoint(Point,Face.P[0],Face.P[1],Face.P[2]),Point);if(J!=F&&Dist>=Settings.BrushRadius)continue;if(FVector::DotProduct(Face.Normal(),N)>.25){auto Brush=Surface.Mask.Brush(Face,Point,Settings.BrushRadius);const double W=Surface.Mask.BrushArea(Face,Brush);if(W>1.e-12){Sum+=W;Brushes.Add(J,MoveTemp(Brush));}}for(int32 Other:Face.Neighbor)if(Other>=0&&!Seen.Contains(Other))Visit.Add(Other);}
   if(Sum>0){for(const auto& W:Brushes){const double Amount=P.Volume*Surface.Mask.BrushArea(Surface.Film.Faces[W.Key],W.Value)/Sum;Surface.Mask.Deposit(W.Key,Surface.Film.Faces[W.Key],W.Value,Amount);Surface.Film.AddVolume(W.Key,Amount);}}else{auto Brush=Surface.Mask.Brush(Surface.Film.Faces[F],Point,FMath::Max(1.f,Settings.BrushRadius));Surface.Mask.Deposit(F,Surface.Film.Faces[F],Brush,P.Volume);Surface.Film.AddVolume(F,P.Volume);}
   if(!P.Drip)Deposited+=P.Volume;else Dripped-=P.Volume;++HitCount;Splash(Point,N,false,P.Volume);Packets.RemoveAtSwap(I);
  }else if(WorldHit){FloorVolume+=P.Volume;Splash(Hit.ImpactPoint,Hit.ImpactNormal,true,P.Volume);Packets.RemoveAtSwap(I);}
  else if(P.Age>5||Next.Z < -5000){Escaped+=P.Volume;Packets.RemoveAtSwap(I);}else{P.P=Next;P.V.Z-=980*Dt;}
 }
 for(int32 I=Attached.Num()-1;I>=0;--I){auto& D=Attached[I];if(!Surfaces.IsValidIndex(D.Target)||!Surfaces[D.Target]->Component.IsValid()){Escaped+=D.Volume;if(D.Visual.IsValid())D.Visual->DestroyComponent();Attached.RemoveAtSwap(I);continue;}
  if(!Surfaces[D.Target]->IsDisplayed())continue;D.Age+=Dt;
  // DrawFilm updates attachment transforms once from the current render pose.
  if(D.Age>=FMath::Max(.01f,Settings.AdhesionSeconds)){const auto& F=Surfaces[D.Target]->Film.Faces[D.Face];FPacket Drop;Drop.P=F.P[0]*D.Bary.X+F.P[1]*D.Bary.Y+F.P[2]*D.Bary.Z+F.Normal()*Surfaces[D.Target]->CoatingSide*.5;Drop.V=FVector(0,0,-10);Drop.Volume=D.Volume;Drop.Drip=true;Packets.Add(Drop);Dripped+=D.Volume;if(D.Visual.IsValid()){Overlays.Remove(D.Visual.Get());D.Visual->DestroyComponent();}Attached.RemoveAtSwap(I);if(DropSystem&&OwnedFX.Num()<FMath::Clamp(Settings.MaxImpactEffects,1,64)){auto* FX=UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(),DropSystem,Drop.P,FRotator(-90,0,0),FVector(1),true,true);if(FX){FX->SetVariableLinearColor(TEXT("User.PaintColor"),Settings.Color);OwnedFX.Add(FX);}}}}
 // Ballistic sweeps remain at 120 Hz. Surface transport uses its own fixed
 // clock; texture work must not be repeated for each projectile substep.
 BallisticMs+=(FPlatformTime::Seconds()-BallisticStart)*1000;
 SurfaceAccumulator+=Dt;const double SurfaceDt=1./FMath::Clamp(Settings.SurfaceHz,5.f,60.f);
 while(SurfaceAccumulator+1.e-10>=SurfaceDt){AdvanceSurface(SurfaceDt);SurfaceAccumulator-=SurfaceDt;}
}
void AOKPaintSprayer::AdvanceSurface(double Dt)
{
 // Evaluate visibility on the game thread. Each worker owns one surface's
 // mask and only reads its film; workers never query components or UObjects.
 TArray<FSurface*> MaskWork;
 for(auto& S:Surfaces)if(S->IsDisplayed()&&(!S->Film.Active.IsEmpty()||!S->Mask.Height.IsEmpty()))MaskWork.Add(S.Get());
 const double MaskStart=FPlatformTime::Seconds();
 const double Speed=Settings.SurfaceSpeed,Viscosity=Settings.Viscosity,Residual=Settings.ResidualThickness;
 const bool BatchMasks=FSurface::PreferBatchMasks(MaskWork);
 ParallelFor(TEXT("PaintSurfaceMasks"),MaskWork.Num(),1,[&](int32 I){auto& S=*MaskWork[I];S.Mask.Transport(S.Film,Dt,Speed,Viscosity,Residual,S.GeometryRevision);},BatchMasks?EParallelForFlags::None:EParallelForFlags::ForceSingleThread);
 MaskTransportMs+=(FPlatformTime::Seconds()-MaskStart)*1000;
 for(int32 S=0;S<Surfaces.Num();++S){auto& Film=Surfaces[S]->Film;if(!Surfaces[S]->IsDisplayed())continue;
  const double FilmStart=FPlatformTime::Seconds();
  // Mask.Transport already moves conserved mass and footprint together.
  FilmTransportMs+=(FPlatformTime::Seconds()-FilmStart)*1000;const double DripStart=FPlatformTime::Seconds();
  if(LiveSimulationParticles()>=FMath::Clamp(Settings.MaxSimulationParticles,8,256))continue;
  TSet<int32> AttachedFaces;for(const auto& Drop:Attached)if(Drop.Target==S)AttachedFaces.Add(Drop.Face);
  for(int32 F:Film.Active.Array()){auto& Face=Film.Faces[F];
   if(AttachedFaces.Contains(F))continue;
   FVector Anchor;const double Amount=Surfaces[S]->Mask.TakeDrip(F,Face,Settings.DripThickness,FMath::Max(0.f,Settings.ResidualThickness),.008,Anchor,Surfaces[S]->CoatingSide);if(Amount<=0)continue;
   Film.AddVolume(F,-Amount);FAttached Drop;Drop.Target=S;Drop.Face=F;Drop.Bary=Anchor;Drop.Volume=Amount;
   UProceduralMeshComponent* Visual;
   if(CVarPaintAnimatedDrops.GetValueOnGameThread()!=0){auto* Owned=NewObject<UKKAffineProceduralMeshComponent>(this);Owned->bUseAnimatedStreams=true;Owned->bUseConservativeAnimatedBounds=true;Visual=Owned;}else Visual=NewObject<UProceduralMeshComponent>(this);
   Visual->RegisterComponent();Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);Visual->SetCastShadow(false);Visual->ComponentTags.Add(TEXT("OKPaintOverlay"));if(auto* Base=DropBase.Get()){auto* MID=UMaterialInstanceDynamic::Create(Base,Visual);MID->SetVectorParameterValue(TEXT("PaintColor"),Settings.Color);Visual->SetMaterial(0,MID);}const double R=FMath::Pow(Amount*3/(4*PI),1./3.);Drop.Radius=R;TArray<FVector> DV={FVector(R,0,0),FVector(-R,0,0),FVector(0,R,0),FVector(0,-R,0),FVector(0,0,R),FVector(0,0,-R)};TArray<FVector2D> DUV;DUV.Init(FVector2D(.5,.5),6);Visual->CreateMeshSection(0,DV,{0,2,4,2,1,4,1,3,4,3,0,4,2,0,5,1,2,5,3,1,5,0,3,5},{},DUV,{},{},false);Visual->SetWorldLocation(Face.P[0]*Anchor.X+Face.P[1]*Anchor.Y+Face.P[2]*Anchor.Z+Face.Normal()*Surfaces[S]->CoatingSide*.2);Drop.Visual=Visual;Overlays.Add(Visual);Attached.Add(MoveTemp(Drop));
   if(LiveSimulationParticles()>=FMath::Clamp(Settings.MaxSimulationParticles,8,256))break;
  }
  DripUpdateMs+=(FPlatformTime::Seconds()-DripStart)*1000;
 }
}
void AOKPaintSprayer::Tick(float Dt)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(OK_PaintTick);
 Super::Tick(Dt);const double Start=FPlatformTime::Seconds();
 // Ownership is this tick, not GFrameCounter: explicit/manual updates can
 // publish more than one pose in the same engine frame. Never reuse across ticks.
 for(auto& Surface:Surfaces)Surface->GeometrySnapshotAvailable=false;
 ON_SCOPE_EXIT{for(auto& Surface:Surfaces)Surface->GeometrySnapshotAvailable=false;};
 OwnedFX.RemoveAll([](const auto& FX){return !IsValid(FX)||!FX->IsRegistered();});while(OwnedFX.Num()>FMath::Clamp(Settings.MaxImpactEffects,1,64)){auto FX=OwnedFX.Pop();if(IsValid(FX))FX->DestroyComponent();}
 if(!Spraying&&Surfaces.IsEmpty()&&Packets.IsEmpty())return;
 if(Spraying&&JetFX){JetFX->SetWorldRotation(Direction.Rotation());int32 Target,Face;double T;FVector Bary;const bool Hit=FindPaintHit(Nozzle,Nozzle+Direction*5000,.3,Target,Face,Bary,T);JetFX->SetVariableFloat(TEXT("User.JetLife"),FMath::Clamp(float(Hit?T*5000/FMath::Max(10.f,Settings.JetSpeed):.5),.01f,3.f));}
 const double GeometryStart=FPlatformTime::Seconds();GeometryTime+=Dt;const double Interval=1./FMath::Clamp(Settings.SurfaceHz,5.f,60.f);
 const bool UpdateGeometry=GeometryTime>=Interval;
 if(UpdateGeometry){GeometryTime=FMath::Fmod(GeometryTime,Interval);
  for(int32 Index=Surfaces.Num()-1;Index>=0;--Index){auto& S=*Surfaces[Index];auto* C=S.Component.Get();bool Changed=!C;
   if(auto* K=Cast<USkinnedMeshComponent>(C))Changed|=K->GetSkinnedAsset()!=S.Asset.Get();
   if(auto* P=Cast<UProceduralMeshComponent>(C)){const auto* Part=P->GetProcMeshSection(S.Section);Changed|=!Part||Part->ProcIndexBuffer.Num()!=S.Indices.Num()||Part->ProcVertexBuffer.Num()!=S.Positions.Num();if(!Changed)for(int32 I=0;I<S.Indices.Num();++I)if(Part->ProcIndexBuffer[I]!=uint32(S.Indices[I])){Changed=true;break;}}
   if(!Changed)continue;
   UE_LOG(LogTemp,Display,TEXT("Paint surface topology invalidated component=%s section=%d vertices=%d indices=%d"),*GetNameSafe(C),S.Section,S.Positions.Num(),S.Indices.Num());
   Escaped+=S.Film.Volume();if(S.Overlay){Overlays.Remove(S.Overlay);S.Overlay->DestroyComponent();}ReleaseSurfaceAtlas(S);Materials.Remove(S.Material);
   const int32 Last=Surfaces.Num()-1;for(int32 I=Attached.Num()-1;I>=0;--I){auto& D=Attached[I];if(D.Target==Index){Escaped+=D.Volume;if(D.Visual.IsValid()){Overlays.Remove(D.Visual.Get());D.Visual->DestroyComponent();}Attached.RemoveAtSwap(I);}else if(D.Target==Last)D.Target=Index;}Surfaces.RemoveAtSwap(Index);
  }
  if(Spraying||!Packets.IsEmpty())DiscoverSurfaces(Nozzle,Nozzle+Direction*(5*FMath::Clamp(Settings.JetSpeed,10.f,10000.f)));
  RefreshGeometries();}
 // Fixed 120Hz integrator. A hitch is retained as backlog, never converted
 // into a larger unstable step or discarded volume/time.
 GeometryUpdateMs=(FPlatformTime::Seconds()-GeometryStart)*1000;const double SimulationStart=FPlatformTime::Seconds();
 BallisticMs=MaskTransportMs=FilmTransportMs=DripUpdateMs=0;
 Accumulator+=FMath::Max(0.f,Dt);int32 Steps=0;while(Accumulator+1.e-10>=1./120. && Steps<32){Step(1./120.);Accumulator-=1./120.;++Steps;}
 SimulationUpdateMs=(FPlatformTime::Seconds()-SimulationStart)*1000;const double CoatingStart=FPlatformTime::Seconds();
 for(auto& S:Surfaces){if(S->Overlay)S->Overlay->SetVisibility(S->IsDisplayed());}
 for(auto& D:Attached)if(D.Visual.IsValid())D.Visual->SetVisibility(Surfaces.IsValidIndex(D.Target)&&Surfaces[D.Target]->IsDisplayed());
 TArray<FKKAnimatedStreamRequest> PendingStreams;TMap<int32,TArray<FIntRect>> PendingAtlas;TArray<FAtlasChartWrite> PendingCharts;
 FKKGpuCoatingBatchScope GpuCoatingFrame;
 for(auto& S:Surfaces)if(S->IsDisplayed()&&(S->Film.Volume()>0||S->HasRenderedPaint||Attached.ContainsByPredicate([&S,this](const auto& D){return Surfaces.IsValidIndex(D.Target)&&Surfaces[D.Target]==S;})))DrawFilm(*S,UpdateGeometry,&PendingStreams,&PendingAtlas,&PendingCharts);
 // Allocations/maps/morphs are captured above. Charts occupy disjoint atlas
 // blocks, profiles remain immutable until this blocking operation joins.
 if(!PendingCharts.IsEmpty())ParallelFor(TEXT("PaintAtlasCharts"),PendingCharts.Num(),16,[&](int32 I){FillAtlasChart(PendingCharts[I]);},PendingCharts.Num()<32?EParallelForFlags::ForceSingleThread:EParallelForFlags::None);
 if(!PendingAtlas.IsEmpty())UploadAtlasBlocks(PendingAtlas,CVarPaintSparseUploads.GetValueOnGameThread()!=0);
 if(!PendingStreams.IsEmpty())UKKAffineProceduralMeshComponent::UpdateAnimatedStreamsBatch(PendingStreams);
 CoatingUpdateMs=(FPlatformTime::Seconds()-CoatingStart)*1000;LastUpdateMs=(FPlatformTime::Seconds()-Start)*1000;MaxUpdateMs=FMath::Max(MaxUpdateMs,LastUpdateMs);
 TotalUpdateMs+=LastUpdateMs;TotalGeometryMs+=GeometryUpdateMs;TotalSimulationMs+=SimulationUpdateMs;TotalCoatingMs+=CoatingUpdateMs;++UpdateSamples;
 TotalBallisticMs+=BallisticMs;TotalMaskTransportMs+=MaskTransportMs;TotalFilmTransportMs+=FilmTransportMs;TotalDripUpdateMs+=DripUpdateMs;
 if((LastUpdateMs>16.||FPlatformTime::Seconds()>NextProfileLog)&&FPlatformMisc::GetEnvironmentVariable(TEXT("OK_PAINT_PROFILE"))==TEXT("1")){NextProfileLog=FPlatformTime::Seconds()+1;int32 Wet=0;for(const auto& S:Surfaces)Wet+=S->Film.Active.Num();UE_LOG(LogTemp,Display,TEXT("Paint profile spraying=%d dt=%.3fms geometry=%.3fms simulation=%.3fms coating=%.3fms ballistic=%.3fms mask=%.3fms film=%.3fms drip=%.3fms steps=%d backlog=%.4fs surfaces=%d wet=%d packets=%d attached=%d"),Spraying,Dt*1000,GeometryUpdateMs,SimulationUpdateMs,CoatingUpdateMs,BallisticMs,MaskTransportMs,FilmTransportMs,DripUpdateMs,Steps,Accumulator,Surfaces.Num(),Wet,Packets.Num(),Attached.Num());}
}
void AOKPaintSprayer::ClearPaintForActor(AActor* Actor)
{
 if(!Actor)return;
 for(int32 Index=0;Index<Surfaces.Num();++Index){auto& S=*Surfaces[Index];if(!S.Component.IsValid()||S.Component->GetOwner()!=Actor)continue;
  Cleared+=S.Film.Volume();for(int32 F:S.Film.Active.Array())S.Film.AddVolume(F,-S.Film.Faces[F].Volume);
  for(const auto& E:S.Mask.Height)S.Mask.DirtyFaces.Add(E.Key);S.Mask.Height.Reset();S.Mask.EdgeCoverage.Reset();
  for(int32 I=Attached.Num()-1;I>=0;--I)if(Attached[I].Target==Index){Cleared+=Attached[I].Volume;if(Attached[I].Visual.IsValid()){Overlays.Remove(Attached[I].Visual.Get());Attached[I].Visual->DestroyComponent();}Attached.RemoveAtSwap(I);}
  // Upload zero thickness and remove geometry, retaining the immutable chart
  // and BVH for subsequent strokes. Other characters and detached drops survive.
  DrawFilm(S);
 }
}
void AOKPaintSprayer::ReleaseSurfaceAtlas(FSurface& S){for(const auto& Entry:S.Charts)SharedAtlas.Release(Entry.Value);for(const auto& Entry:S.PageMaterials)Materials.Remove(Entry.Value);S.Charts.Reset();S.PageMaterials.Reset();}
void AOKPaintSprayer::ClearPaint()
{
 UE_LOG(LogTemp,Display,TEXT("Paint clear surfaces=%d volume=%.9f"),Surfaces.Num(),RemainingVolume());
 Spraying=false;if(JetFX)JetFX->DeactivateImmediate();if(Gun)Gun->SetVisibility(false);for(auto FX:OwnedFX)if(IsValid(FX))FX->DestroyComponent();OwnedFX.Reset();FloorStains.Reset();
 for(const auto& S:Surfaces)Cleared+=S->Film.Volume();for(const auto& D:Attached)Cleared+=D.Volume;
 for(auto C:Overlays)if(IsValid(C))C->DestroyComponent();Overlays.Reset();Materials.Reset();Targets.Reset();AtlasPixels.Reset();SharedAtlas.Reset(Settings.AtlasResolution);Surfaces.Reset();Attached.Reset();Packets.Reset();Accumulator=SurfaceAccumulator=GeometryTime=JetTime=0;Emitted=Deposited=Dripped=FloorVolume=Escaped=Cleared=0;HitCount=0;CoatingRebuilds=CoatingUpdates=0;
}
#if WITH_DEV_AUTOMATION_TESTS
bool AOKPaintSprayer::VerifyGeometryJobsParity(int32& ComparedVertices,int32& ComparedFaces)
{
 ComparedVertices=ComparedFaces=0;
 auto Original=MoveTemp(Surfaces);const int32 Saved=CVarPaintGeometryJobs.GetValueOnGameThread(),SavedSkinnedSurfaces=SkinnedGeometrySurfaces,SavedSkinnedComponents=SkinnedGeometryComponents;
 ON_SCOPE_EXIT{Surfaces=MoveTemp(Original);CVarPaintGeometryJobs.AsVariable()->Set(Saved,ECVF_SetByCode);SkinnedGeometrySurfaces=SavedSkinnedSurfaces;SkinnedGeometryComponents=SavedSkinnedComponents;};
 TArray<TSharedPtr<FSurface>> Captured[2];
 for(int32 Mode=0;Mode<2;++Mode){for(const auto& S:Original){auto Copy=MakeShared<FSurface>(*S);
   // Force real skin/refit work even when called immediately after a Tick.
   if(!Copy->Positions.IsEmpty())Copy->Positions[0]+=FVector(.125,.25,.5);Surfaces.Add(MoveTemp(Copy));
  }
  CVarPaintGeometryJobs.AsVariable()->Set(Mode?2:0,ECVF_SetByCode);RefreshGeometries();Captured[Mode]=MoveTemp(Surfaces);
 }
 if(Captured[0].Num()!=Captured[1].Num())return false;
 for(int32 I=0;I<Captured[0].Num();++I){const auto& A=*Captured[0][I];const auto& B=*Captured[1][I];
  if(A.Positions!=B.Positions||A.GeometryRevision!=B.GeometryRevision||A.GeometrySnapshotAvailable!=B.GeometrySnapshotAvailable||A.Indices!=B.Indices||A.Film.Faces.Num()!=B.Film.Faces.Num()||A.FaceBounds.Num()!=B.FaceBounds.Num()||A.Nodes.Num()!=B.Nodes.Num()||A.Order!=B.Order)return false;
  ComparedVertices+=A.Positions.Num();ComparedFaces+=A.Film.Faces.Num();
  for(int32 F=0;F<A.Film.Faces.Num();++F){for(int32 E=0;E<3;++E)if(A.Film.Faces[F].P[E]!=B.Film.Faces[F].P[E])return false;}
  auto SameBox=[](const FBox& X,const FBox& Y){return X.IsValid==Y.IsValid&&X.Min==Y.Min&&X.Max==Y.Max;};
  for(int32 F=0;F<A.FaceBounds.Num();++F)if(!SameBox(A.FaceBounds[F],B.FaceBounds[F]))return false;
  for(int32 N=0;N<A.Nodes.Num();++N)if(!SameBox(A.Nodes[N].Bounds,B.Nodes[N].Bounds))return false;
 }
 return ComparedVertices>0&&ComparedFaces>0;
}
bool AOKPaintSprayer::VerifyAttachedVisuals(const AOKPaintSprayer& Oracle,int32& Compared,double& MaximumError) const
{
 Compared=0;MaximumError=0;if(Attached.Num()!=Oracle.Attached.Num())return false;
 for(int32 I=0;I<Attached.Num();++I){const auto& A=Attached[I];const auto& B=Oracle.Attached[I];
  if(A.Target!=B.Target||A.Face!=B.Face||A.Bary!=B.Bary||A.Volume!=B.Volume||A.Age!=B.Age||!A.Visual.IsValid()||!B.Visual.IsValid())return false;
  auto* Stream=Cast<UKKAffineProceduralMeshComponent>(A.Visual.Get());const auto* X=A.Visual->GetProcMeshSection(0);const auto* Y=B.Visual->GetProcMeshSection(0);
  if(!Stream||!Stream->UsesAnimatedStreams()||!X||!Y||X->ProcIndexBuffer!=Y->ProcIndexBuffer||X->ProcVertexBuffer.Num()!=Y->ProcVertexBuffer.Num()||!Stream->VerifyAnimatedRenderStreams(0,0,0))return false;
  const FBox Bounds=Stream->CalcBounds(Stream->GetComponentTransform()).GetBox();
  for(int32 V=0;V<X->ProcVertexBuffer.Num();++V){const auto& VX=X->ProcVertexBuffer[V];const auto& VY=Y->ProcVertexBuffer[V];
   const FVector PX=A.Visual->GetRenderMatrix().TransformPosition(VX.Position),PY=B.Visual->GetRenderMatrix().TransformPosition(VY.Position);MaximumError=FMath::Max(MaximumError,FVector::Dist(PX,PY));
   if(VX.Normal!=VY.Normal||VX.UV0!=VY.UV0||VX.Color!=VY.Color||VX.Tangent.TangentX!=VY.Tangent.TangentX||VX.Tangent.bFlipTangentY!=VY.Tangent.bFlipTangentY||!Bounds.ExpandBy(.0001).IsInside(PX))return false;
  }++Compared;
 }
 return MaximumError<=1.e-6;
}
#endif
double AOKPaintSprayer::RemainingVolume() const {double Total=0;for(const auto& S:Surfaces)Total+=S->Film.Volume();for(const auto& D:Attached)Total+=D.Volume;for(const auto& P:Packets)Total+=P.Volume;return Total;}
double AOKPaintSprayer::SurfaceVolume(int32 I) const {return Surfaces.IsValidIndex(I)?Surfaces[I]->Film.Volume():0;}
#if !UE_BUILD_SHIPPING
void AOKPaintSprayer::GetPaintAtlasRects(int32 Index,UTextureRenderTarget2D* Texture,TArray<FIntRect>& Rects) const {Rects.Reset();if(!Surfaces.IsValidIndex(Index))return;for(const auto& Entry:Surfaces[Index]->Charts){const auto& A=Entry.Value;if(Targets.IsValidIndex(A.Page)&&Targets[A.Page]==Texture)Rects.Add(FIntRect(A.Origin,A.Origin+FIntPoint(A.Size,A.Size)));}}
bool AOKPaintSprayer::VerifyPrivateGpuPaintGeometry(const AActor* Actor,int32& Checked,int32& Painted,int32& Anchor,double& MaxError,FString& Error){
 Checked=Painted=0;Anchor=INDEX_NONE;MaxError=0.;Error.Reset();
 RefreshGeometries();
#if WITH_DEV_AUTOMATION_TESTS
 int32 ComparedVertices=0,ComparedFaces=0;
 if(!VerifyGeometryJobsParity(ComparedVertices,ComparedFaces)){Error=TEXT("Current APNG geometry jobs differ from full surface oracle");return false;}
 UE_LOG(LogTemp,Display,TEXT("Paint geometry jobs actual APNG vertices=%d faces=%d exact=true"),ComparedVertices,ComparedFaces);
#endif
 for(int32 I=0;I<Surfaces.Num();++I){const auto& S=*Surfaces[I];auto* C=Cast<UProceduralMeshComponent>(S.Component.Get());
  auto* Preview=C?Cast<AKKPreviewActor>(C->GetOwner()):nullptr;
  if(!Preview||Preview!=Actor||!Preview->Parts.ContainsByPredicate([C](const auto& Part){return Part.Comp==C&&Part.PrivateGpuDisplay.IsValid();}))continue;
  // Resident collision queries intentionally do not publish a CPU section.
  // Refresh the independent native oracle explicitly before comparing.
  Preview->EnsurePrivateGpuQueryGeometry(C,S.Section);
  const auto* Section=C->GetProcMeshSection(S.Section);
  if(!S.PreparedValid||!Section||S.Positions.Num()!=Section->ProcVertexBuffer.Num()){Error=TEXT("Private GPU paint geometry layout invalid");return false;}
  const FMatrix Render=C->GetRenderMatrix();
  for(int32 V=0;V<S.Positions.Num();++V){const FVector Expected=Render.TransformPosition(Section->ProcVertexBuffer[V].Position);
   if(Expected.ContainsNaN()||S.Positions[V].ContainsNaN()){Error=TEXT("Nonfinite private GPU paint geometry");return false;}
   MaxError=FMath::Max(MaxError,FVector::Distance(Expected,S.Positions[V]));}
  ++Checked;if(S.Film.Volume()>1.e-9){++Painted;if(Anchor==INDEX_NONE)Anchor=I;}
 }
 if(MaxError>.001){Error=TEXT("Private GPU paint geometry differs from current query pose");return false;}
 return true;
}
#endif
bool AOKPaintSprayer::FilmCenterOfMass(FVector& Center,double& Volume) const {
 Center=FVector::ZeroVector;Volume=0;
 for(const auto& S:Surfaces)if(S->IsDisplayed())for(int32 Id:S->Film.Active){const auto& Face=S->Film.Faces[Id];if(Face.Volume>0){Center+=Face.Center()*Face.Volume;Volume+=Face.Volume;}}
 if(Volume<=1.e-12)return false;Center/=Volume;return !Center.ContainsNaN();
}
bool AOKPaintSprayer::GetPaintAnchor(int32 I,int32 Face,FVector& Point,UTextureRenderTarget2D*& Texture,int32& FoundFace) const {if(!Surfaces.IsValidIndex(I))return false;const auto& S=*Surfaces[I];if(!S.Film.Faces.IsValidIndex(Face)){Face=INDEX_NONE;for(int32 F:S.Film.Active)if(S.Film.Faces[F].Volume>0){Face=F;break;}}if(!S.Film.Faces.IsValidIndex(Face))return false;FoundFace=Face;Point=S.Film.Faces[Face].Center();const auto* A=S.Charts.Find(Face);Texture=A&&Targets.IsValidIndex(A->Page)?Targets[A->Page].Get():nullptr;return true;}
FString AOKPaintSprayer::Statistics() const
{
 double Film=0,AttachedVolume=0;int32 Faces=0;uint64 WetCharts=0,WetTexels=0;for(const auto& S:Surfaces){Film+=S->Film.Volume();Faces+=S->Film.Faces.Num();WetCharts+=S->Mask.Height.Num();for(const auto& Entry:S->Mask.Height){const int32 N=S->Mask.Resolution(S->Film.Faces[Entry.Key]);WetTexels+=uint64(N)*N;}}for(const auto& D:Attached)AttachedVolume+=D.Volume;
 uint64 GpuFrames=0,GpuPublished=0,GpuRejectedPages=0;for(const auto& Surface:Surfaces){GpuFrames+=Surface->GpuCoatingFrames;GpuPublished+=Surface->GpuCoatingPublished;for(const auto& Page:Surface->PageStreams)GpuRejectedPages+=Page.Value.GpuRejected?1:0;}
 const FString Workload=FString::Printf(TEXT("atlas_resolution=%d shared_pages=%d wet_charts=%llu wet_chart_texels=%llu fixed_step_backlog_s=%.6f gpu_coating_frames=%llu gpu_coating_published=%llu gpu_coating_rejected_pages=%llu "),SharedAtlas.Resolution,Targets.Num(),WetCharts,WetTexels,Accumulator,GpuFrames,GpuPublished,GpuRejectedPages);
 return Workload+FString::Printf(TEXT("Paint surfaces=%d faces=%d atlases=%d film=%.4f attached=%.4f floor=%.4f hits=%d packets=%d impact_fx=%d update=%.3fms max=%.3fms actor_mean=%.6fms samples=%llu coating_rebuilds=%llu coating_updates=%llu geometry_mean=%.6fms simulation_mean=%.6fms coating_mean=%.6fms skinned_surfaces=%d skinned_components=%d"),Surfaces.Num(),Faces,Targets.Num(),Film,AttachedVolume,FloorVolume,HitCount,LiveSimulationParticles(),LiveImpactEffects(),LastUpdateMs,MaxUpdateMs,TotalUpdateMs/FMath::Max(uint64(1),UpdateSamples),UpdateSamples,CoatingRebuilds,CoatingUpdates,TotalGeometryMs/FMath::Max(uint64(1),UpdateSamples),TotalSimulationMs/FMath::Max(uint64(1),UpdateSamples),TotalCoatingMs/FMath::Max(uint64(1),UpdateSamples),SkinnedGeometrySurfaces,SkinnedGeometryComponents);
}

FVector2D AOKPaintSprayer::MeasureCoatingPair(int32 Repeats)
{
 // Diagnostic only: rebuild from the same held pose and film, alternating
 // order. No simulation step, texture update, shape change or frame sample.
 const int32 Saved=CVarPaintCoatingParallel.GetValueOnGameThread();FVector2D Totals=FVector2D::ZeroVector;Repeats=FMath::Clamp(Repeats,1,32);
 for(int32 Pass=-1;Pass<Repeats;++Pass)for(int32 Turn=0;Turn<2;++Turn){const int32 Mode=(Pass&1)?1-Turn:Turn;CVarPaintCoatingParallel.AsVariable()->Set(Mode,ECVF_SetByCode);const double Start=FPlatformTime::Seconds();
  for(auto& S:Surfaces)if(S->IsDisplayed()&&(S->Film.Volume()>0||S->HasRenderedPaint||Attached.ContainsByPredicate([&S,this](const auto& D){return Surfaces.IsValidIndex(D.Target)&&Surfaces[D.Target]==S;})))DrawFilm(*S,false);
  if(Pass>=0)Totals[Mode]+=(FPlatformTime::Seconds()-Start)*1000;
 }
 CVarPaintCoatingParallel.AsVariable()->Set(Saved,ECVF_SetByCode);return Totals/Repeats;
}

FVector AOKPaintSprayer::MeasureGeometryPair(int32 Repeats)
{
 const int32 Saved=CVarPaintGeometryParallel.GetValueOnGameThread();FVector Result=FVector::ZeroVector;Repeats=FMath::Clamp(Repeats,1,16);
 for(int32 Pass=0;Pass<Repeats;++Pass)for(const auto& Source:Surfaces){
  FSurface Copies[2]={*Source,*Source};
  for(int32 Turn=0;Turn<2;++Turn){const int32 Which=(Pass&1)?1-Turn:Turn;CVarPaintGeometryParallel.AsVariable()->Set(Which,ECVF_SetByCode);const double Start=FPlatformTime::Seconds();RefreshGeometry(Copies[Which]);Result[Which]+=(FPlatformTime::Seconds()-Start)*1000.;}
  if(Copies[0].Positions.Num()!=Copies[1].Positions.Num()||Copies[0].Film.Faces.Num()!=Copies[1].Film.Faces.Num())Result.Z=DBL_MAX;
  else {for(int32 I=0;I<Copies[0].Positions.Num();++I)Result.Z=FMath::Max(Result.Z,FVector::Dist(Copies[0].Positions[I],Copies[1].Positions[I]));for(int32 I=0;I<Copies[0].Film.Faces.Num();++I)for(int32 E=0;E<3;++E)Result.Z=FMath::Max(Result.Z,FVector::Dist(Copies[0].Film.Faces[I].P[E],Copies[1].Film.Faces[I].P[E]));}
 }
 CVarPaintGeometryParallel.AsVariable()->Set(Saved,ECVF_SetByCode);Result.X/=Repeats;Result.Y/=Repeats;return Result;
}

FVector AOKPaintSprayer::MeasureSurfaceGeometryPair(int32 Repeats)
{
 const int32 SavedMode=CVarPaintSurfaceParallel.GetValueOnGameThread();const auto Originals=Surfaces;FVector Result=FVector::ZeroVector;Repeats=FMath::Clamp(Repeats,1,8);
 ON_SCOPE_EXIT{Surfaces=Originals;CVarPaintSurfaceParallel.AsVariable()->Set(SavedMode,ECVF_SetByCode);};
 for(int32 Pass=0;Pass<Repeats;++Pass){TArray<TSharedPtr<FSurface>> Copies[2];for(int32 Mode=0;Mode<2;++Mode)for(const auto& S:Originals)Copies[Mode].Add(MakeShared<FSurface>(*S));
  for(int32 Turn=0;Turn<2;++Turn){const int32 Mode=(Pass&1)?1-Turn:Turn;Surfaces=Copies[Mode];CVarPaintSurfaceParallel.AsVariable()->Set(Mode?2:0,ECVF_SetByCode);const double Start=FPlatformTime::Seconds();RefreshGeometries();Result[Mode]+=(FPlatformTime::Seconds()-Start)*1000.;}
  for(int32 S=0;S<Originals.Num();++S){const auto& A=*Copies[0][S];const auto& B=*Copies[1][S];
   if(A.Positions.Num()!=B.Positions.Num()||A.Nodes.Num()!=B.Nodes.Num()||A.VertexNormals.Num()!=B.VertexNormals.Num()||A.GeometryRevision!=B.GeometryRevision){Result.Z=DBL_MAX;continue;}
   for(int32 I=0;I<A.Positions.Num();++I)Result.Z=FMath::Max(Result.Z,FVector::Dist(A.Positions[I],B.Positions[I]));
   for(int32 I=0;I<A.VertexNormals.Num();++I)Result.Z=FMath::Max(Result.Z,FVector::Dist(A.VertexNormals[I],B.VertexNormals[I]));
   for(int32 I=0;I<A.Film.Faces.Num();++I)for(int32 E=0;E<3;++E)Result.Z=FMath::Max(Result.Z,FVector::Dist(A.Film.Faces[I].P[E],B.Film.Faces[I].P[E]));
   for(int32 I=0;I<A.Nodes.Num();++I)if(A.Nodes[I].Bounds.Min!=B.Nodes[I].Bounds.Min||A.Nodes[I].Bounds.Max!=B.Nodes[I].Bounds.Max)Result.Z=DBL_MAX;
  }
 }
 Result.X/=Repeats;Result.Y/=Repeats;return Result;
}

FVector AOKPaintSprayer::MeasureSurfaceQueryPair(int32 Repeats)
{
 // Diagnostic runs on copies of one held scene, excluding allocation/copy
 // time and normal frame statistics. Neither path advances live paint.
 auto* Mode=IConsoleManager::Get().FindConsoleVariable(TEXT("ok.Paint.SeamQueryReuse"));if(!Mode)return FVector::ZeroVector;
 const int32 Saved=Mode->GetInt();FVector Result=FVector::ZeroVector;Repeats=FMath::Clamp(Repeats,1,32);
 TArray<FSurface*> Work;for(auto& S:Surfaces)if(S->IsDisplayed()&&!S->Film.Active.IsEmpty())Work.Add(S.Get());
 double Dominance=0;const bool BatchMasks=FSurface::PreferBatchMasks(Work,&Dominance);
 const double Dt=1./FMath::Clamp(Settings.SurfaceHz,5.f,60.f),Speed=Settings.SurfaceSpeed,Viscosity=Settings.Viscosity,Residual=Settings.ResidualThickness;
 for(int32 Pass=-1;Pass<Repeats;++Pass){TArray<FOKPaintMask> Copies[2];for(auto* S:Work){Copies[0].Add(S->Mask);Copies[1].Add(S->Mask);}
  for(int32 Turn=0;Turn<2;++Turn){const int32 Which=(Pass&1)?1-Turn:Turn;Mode->Set(Which,ECVF_SetByCode);const double Start=FPlatformTime::Seconds();
   if(Which==0){for(int32 I=0;I<Work.Num();++I)Copies[0][I].Advance(Work[I]->Film,Dt,Speed,Viscosity,Residual);}
   else ParallelFor(TEXT("PaintSurfaceMaskComparison"),Work.Num(),1,[&](int32 I){Copies[1][I].Advance(Work[I]->Film,Dt,Speed,Viscosity,Residual,!BatchMasks||Work[I]->Film.Active.Num()>=1024);},BatchMasks?EParallelForFlags::None:EParallelForFlags::ForceSingleThread);
   if(Pass>=0)Result[Which]+=(FPlatformTime::Seconds()-Start)*1000;
  }
  for(int32 I=0;I<Work.Num();++I)for(const auto& Entry:Copies[0][I].Height){const auto* Other=Copies[1][I].Height.Find(Entry.Key);if(!Other||Other->Num()!=Entry.Value.Num()){Result.Z=DBL_MAX;continue;}for(int32 P=0;P<Entry.Value.Num();++P)Result.Z=FMath::Max(Result.Z,FMath::Abs(double(Entry.Value[P])-(*Other)[P]));}
 }
 UE_LOG(LogTemp,Display,TEXT("Paint mask scheduler jobs=%d dominant_fraction=%.6f batch=%d"),Work.Num(),Dominance,BatchMasks);
 Mode->Set(Saved,ECVF_SetByCode);Result.X/=Repeats;Result.Y/=Repeats;return Result;
}

double AOKPaintSprayer::MeasureNormalParity()
{
 const int32 Saved=CVarPaintSparseNormals.GetValueOnGameThread();CVarPaintSparseNormals.AsVariable()->Set(1,ECVF_SetByCode);double Error=0.;
 for(const auto& Surface:Surfaces)if(Surface->FilmTopologyReady){FSurface Sparse=*Surface,Dense=*Surface;Dense.DenseNormals();Sparse.UpdateNormals();
  for(int32 Vertex=0;Vertex<Sparse.Positions.Num();++Vertex){Sparse.ResolveNormal(Vertex);Error=FMath::Max(Error,FVector::Dist(Sparse.VertexNormals[Vertex],Dense.VertexNormals[Vertex]));}
 }
 CVarPaintSparseNormals.AsVariable()->Set(Saved,ECVF_SetByCode);return Error;
}

FVector AOKPaintSprayer::MeasureSweepPair(int32 Repeats)
{
 struct FQuery {FVector A,B;double Radius;};TArray<FQuery> Queries;
 FRandomStream Rays(288);
 for(const auto& Surface:Surfaces)if(Surface->IsDisplayed()){int32 Count=0;for(int32 Id:Surface->Film.Active){const auto& F=Surface->Film.Faces[Id];const FVector Center=F.Center(),Offset=F.Normal()*20+Rays.VRand()*5;Queries.Add({Center+Offset,Center-Offset,.15});if(++Count>=4)break;}}
 const int32 Seeds=Queries.Num();if(Seeds)while(Queries.Num()<64){auto Q=Queries[Queries.Num()%Seeds];const FVector Offset=Rays.VRand()*3;Q.A+=Offset;Q.B+=Offset;Queries.Add(Q);}
 struct FResult {bool Hit=false;int32 Surface=-1,Face=-1;FVector Bary=FVector::ZeroVector;double Time=1;};
 TArray<FResult> Results[2];Results[0].SetNum(Queries.Num());Results[1].SetNum(Queries.Num());
 const int32 Saved=CVarPaintCachedSweepBounds.GetValueOnGameThread();FVector Totals=FVector::ZeroVector;Repeats=FMath::Clamp(Repeats,1,32);
 for(int32 Pass=-1;Pass<Repeats;++Pass){for(int32 Turn=0;Turn<2;++Turn){const int32 Mode=(Pass&1)?1-Turn:Turn;CVarPaintCachedSweepBounds.AsVariable()->Set(Mode,ECVF_SetByCode);const double Start=FPlatformTime::Seconds();
  for(int32 I=0;I<Queries.Num();++I){const auto& Q=Queries[I];auto& R=Results[Mode][I];R.Hit=FindPaintHit(Q.A,Q.B,Q.Radius,R.Surface,R.Face,R.Bary,R.Time);}
  if(Pass>=0)Totals[Mode]+=(FPlatformTime::Seconds()-Start)*1000;
 }
 for(int32 I=0;I<Queries.Num();++I){const auto& A=Results[0][I];const auto& B=Results[1][I];if(A.Hit!=B.Hit||(A.Hit&&(A.Surface!=B.Surface||A.Face!=B.Face||FMath::Abs(A.Time-B.Time)>1.e-12||!A.Bary.Equals(B.Bary,1.e-12))))++Totals.Z;}
 }
 // Independent triangle oracle also verifies median partitioning and BVH
 // pruning. Keep this diagnostic work outside the measured sweep timings.
 TArray<TPair<int32,const FSurface*>> Captured;for(int32 I=0;I<Surfaces.Num();++I)if(Surfaces[I]->IsDisplayed())Captured.Emplace(I,Surfaces[I].Get());
 TArray<FResult> ParallelResults;ParallelResults.SetNum(Queries.Num());
 ParallelFor(TEXT("PaintPacketSweepOracle"),Queries.Num(),1,[&](int32 I){const auto& Q=Queries[I];auto& R=ParallelResults[I];for(const auto& Surface:Captured)if(Surface.Value->Sweep(Q.A,Q.B,Q.Radius,true,R.Face,R.Bary,R.Time)){R.Hit=true;R.Surface=Surface.Key;if(R.Time==0)break;}});
 for(int32 I=0;I<Queries.Num();++I){const auto& Q=Queries[I];FResult Oracle;
  const auto& Original=Results[1][I];const auto& Parallel=ParallelResults[I];if(Original.Hit!=Parallel.Hit||(Original.Hit&&(Original.Surface!=Parallel.Surface||Original.Face!=Parallel.Face||Original.Time!=Parallel.Time||Original.Bary!=Parallel.Bary)))++Totals.Z;
  for(int32 S=0;S<Surfaces.Num();++S)if(Surfaces[S]->IsDisplayed())for(int32 F=0;F<Surfaces[S]->Film.Faces.Num();++F){double T;FVector W;
   if(FOKPaintFilm::SweepTriangle(Q.A,Q.B,Q.Radius,Surfaces[S]->Film.Faces[F],T,W)&&T<Oracle.Time){Oracle.Hit=true;Oracle.Surface=S;Oracle.Face=F;Oracle.Bary=W;Oracle.Time=T;}
  }
  const auto& Actual=Results[1][I];bool Mismatch=Actual.Hit!=Oracle.Hit;
  if(Actual.Hit&&Oracle.Hit){const auto& A=Surfaces[Actual.Surface]->Film.Faces[Actual.Face];const auto& B=Surfaces[Oracle.Surface]->Film.Faces[Oracle.Face];
   const FVector AP=A.P[0]*Actual.Bary.X+A.P[1]*Actual.Bary.Y+A.P[2]*Actual.Bary.Z,BP=B.P[0]*Oracle.Bary.X+B.P[1]*Oracle.Bary.Y+B.P[2]*Oracle.Bary.Z;
   Mismatch=FMath::Abs(Actual.Time-Oracle.Time)>1.e-12||!AP.Equals(BP,1.e-7);
  }
  if(Mismatch)++Totals.Z;
 }
 CVarPaintCachedSweepBounds.AsVariable()->Set(Saved,ECVF_SetByCode);Totals.X/=Repeats;Totals.Y/=Repeats;return Totals;
}

FVector2D AOKPaintSprayer::MeasureBufferPair(int32 Repeats)
{
 const int32 Saved=CVarPaintCoatingBufferReuse.GetValueOnGameThread();FVector2D Totals=FVector2D::ZeroVector;Repeats=FMath::Clamp(Repeats,1,32);
 for(int32 Pass=-1;Pass<Repeats;++Pass)for(int32 Turn=0;Turn<2;++Turn){const int32 Mode=(Pass&1)?1-Turn:Turn;CVarPaintCoatingBufferReuse.AsVariable()->Set(Mode,ECVF_SetByCode);const double Start=FPlatformTime::Seconds();
  for(auto& S:Surfaces)if(S->IsDisplayed()&&(S->Film.Volume()>0||S->HasRenderedPaint||Attached.ContainsByPredicate([&S,this](const auto& D){return Surfaces.IsValidIndex(D.Target)&&Surfaces[D.Target]==S;})))DrawFilm(*S,false);
  if(Pass>=0)Totals[Mode]+=(FPlatformTime::Seconds()-Start)*1000;
 }
 CVarPaintCoatingBufferReuse.AsVariable()->Set(Saved,ECVF_SetByCode);return Totals/Repeats;
}
