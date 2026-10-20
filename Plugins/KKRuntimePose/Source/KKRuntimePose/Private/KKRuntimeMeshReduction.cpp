#include "KKRuntimeMeshReduction.h"
#include "Async/Async.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "MeshSimplification.h"
#include "MeshConstraints.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif
using namespace UE::Geometry;
namespace {
// Low-poly rules: 0 face, body, socks/stockings (kept); 1 hair; 2 clothes and
// accessories. Same classes as ok-extract lowpoly::category.
int32 RuleGroup(const FKKRuntimeMeshPart& P){
 const FString R=P.Role.ToLower(),N=P.Name.ToLower();
 if(R==TEXT("head")||R==TEXT("body")||R.StartsWith(TEXT("clothes_socks"))||R.StartsWith(TEXT("clothes_panst")))return 0;
 if(P.ShaderKind==1||P.ShaderKind==10||N.Contains(TEXT("hair"))||R.Contains(TEXT("hair")))return 1;
 return 2;
}
int32 Group(const FKKRuntimeMeshPart& P){
 const FString R=P.Role.ToLower(),N=P.Name.ToLower();
 if(R==TEXT("head")||R.Contains(TEXT("eye"))||R.Contains(TEXT("brow"))||R.Contains(TEXT("lash"))||N.Contains(TEXT("_mayu"))||N.Contains(TEXT("_eyeline")))return 0;
 if(R==TEXT("body"))return 1;if(R.Contains(TEXT("clothes")))return 2;return 3;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKPreserveRuntimeGeometryTest,"OpenKoikatsu.RuntimeMesh.PreserveSourceGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKPreserveRuntimeGeometryTest::RunTest(const FString&){
 FKKRuntimeMeshPart P;P.NativePart=7;P.Name=TEXT("source detail");P.Role=TEXT("body");P.Blend=TEXT("opaque");P.ShaderKind=4;
 for(int32 I=0;I<4;++I){FKKRuntimeMeshVertex V{};V.Position=FVector3f(I%2,I/2,.2f);V.InitialPosition=V.Position;
  V.Normal=FVector3f(0,0,1);V.Tangent=FVector3f(1,0,0);V.TangentSign=-1;
  V.MouthClosed=FVector3f(.1f,.2f,.3f);V.MouthOpen=FVector3f(.4f,.5f,.6f);V.Color=FColor(17,53,97,211);
  for(int32 K=0;K<4;++K)V.UV[K]=FVector2f(I*.13f,K*.21f);
  V.Influences=2;V.Skin[0]={3,32768};V.Skin[1]={7,32767};P.Vertices.Add(V);
 }P.Indices={0,1,2,2,1,3};
 auto Result=OKReduceRuntimeMeshesAsync({P},1,1,1,true).Get();
 TestTrue(TEXT("source preservation accepted"),Result.Error.IsEmpty()&&Result.SourceGeometryPreserved&&Result.WorkerThread);
 TestFalse(TEXT("quality does not falsely certify triangle budget"),Result.BudgetsMet);
 if(!TestEqual(TEXT("part count"),Result.Meshes.Num(),1)||!TestEqual(TEXT("statistics count"),Result.Parts.Num(),1))return false;
 const auto& Actual=Result.Meshes[0];TestTrue(TEXT("original topology"),Actual.Indices==P.Indices);
 TestTrue(TEXT("part identity"),Actual.NativePart==P.NativePart&&Actual.Name==P.Name&&Actual.Role==P.Role&&Actual.Blend==P.Blend&&Actual.ShaderKind==P.ShaderKind);
 TestEqual(TEXT("source count"),Result.Parts[0].SourceTriangles,2u);TestEqual(TEXT("preserved target count"),Result.Parts[0].TargetTriangles,2u);
 if(!TestEqual(TEXT("vertex count"),Actual.Vertices.Num(),P.Vertices.Num()))return false;
 for(int32 I=0;I<P.Vertices.Num();++I){const auto& A=Actual.Vertices[I];const auto& B=P.Vertices[I];
  TestTrue(TEXT("position and basis"),A.Position==B.Position&&A.InitialPosition==B.InitialPosition&&A.Normal==B.Normal&&A.Tangent==B.Tangent&&A.TangentSign==B.TangentSign);
  TestTrue(TEXT("morph color and skin count"),A.MouthClosed==B.MouthClosed&&A.MouthOpen==B.MouthOpen&&A.Color==B.Color&&A.Influences==B.Influences);
  for(int32 K=0;K<4;++K)TestTrue(TEXT("UV channel"),A.UV[K]==B.UV[K]);
  for(int32 K=0;K<12;++K)TestTrue(TEXT("skin slot"),A.Skin[K].Bone==B.Skin[K].Bone&&A.Skin[K].Weight==B.Skin[K].Weight);
 }
 auto Legacy=OKReduceRuntimeMeshesAsync({P}).Get();TestFalse(TEXT("default policy not silently changed"),Legacy.SourceGeometryPreserved);TestTrue(TEXT("default policy remains valid"),Legacy.Error.IsEmpty());
 P.Indices[0]=99;auto Invalid=OKReduceRuntimeMeshesAsync({P},1,1,1,true).Get();
 TestTrue(TEXT("invalid indices rejected before quality success"),!Invalid.Error.IsEmpty()&&!Invalid.SourceGeometryPreserved&&Invalid.Meshes.IsEmpty());
 return true;
}
#endif
bool SameSupport(const FKKRuntimeMeshVertex& A,const FKKRuntimeMeshVertex& B){
 TSet<uint16> X,Y;for(int32 K=0;K<A.Influences;++K)if(A.Skin[K].Weight)X.Add(A.Skin[K].Bone);
 for(int32 K=0;K<B.Influences;++K)if(B.Skin[K].Weight)Y.Add(B.Skin[K].Bone);
 if(X.Num()!=Y.Num())return false;for(uint16 Bone:X)if(!Y.Contains(Bone))return false;return true;
}
class FEndpointSimplifier:public FQEMSimplification {
 public:TArray<int32> Source;const TArray<FKKRuntimeMeshVertex>& Original;
 FEndpointSimplifier(FDynamicMesh3* M,const TArray<FKKRuntimeMeshVertex>& V):FQEMSimplification(M),Original(V){Source.SetNum(V.Num());for(int32 I=0;I<V.Num();++I)Source[I]=I;}
 virtual void OnEdgeCollapse(int Edge,int A,int B,const FDynamicMesh3::FEdgeCollapseInfo& Info)override{
  // The retained mesh ID need not be the selected endpoint. Carry its attributes too.
  const FVector3d P=Mesh->GetVertex(A);
  if(FVector3d::DistSquared(P,FVector3d(Original[Source[B]].Position))<FVector3d::DistSquared(P,FVector3d(Original[Source[A]].Position)))Source[A]=Source[B];
 }
};
bool ReducePart(const FKKRuntimeMeshPart& P,uint32 Target,FKKRuntimeMeshPart& Out,FString& Retain){
 FDynamicMesh3 M;for(const auto& V:P.Vertices)M.AppendVertex(FVector3d(V.Position));
 for(int32 I=0;I<P.Indices.Num();I+=3)if(M.AppendTriangle(P.Indices[I],P.Indices[I+1],P.Indices[I+2])<0){Retain=TEXT("source non-manifold/degenerate/duplicate topology retained");return false;}
 // Original vertex components. No spatial welding, even for coincident vertices.
 TArray<int32> Component;Component.Init(INDEX_NONE,P.Vertices.Num());int32 Components=0;
 for(int32 V:M.VertexIndicesItr())if(M.GetVtxEdgeCount(V)>0&&Component[V]==INDEX_NONE){TArray<int32> Stack{V};Component[V]=Components;
  while(!Stack.IsEmpty()){const int32 A=Stack.Pop(EAllowShrinking::No);for(int32 B:M.VtxVerticesItr(A))if(Component[B]==INDEX_NONE){Component[B]=Components;Stack.Add(B);}}++Components;}
 FMeshConstraints C;
 auto Lock=[&](int32 V){C.SetOrUpdateVertexConstraint(V,FVertexConstraint::FullyConstrained());};
 for(int32 E:M.EdgeIndicesItr()){const auto V=M.GetEdgeV(E);
  if(M.IsBoundaryEdge(E)||!SameSupport(P.Vertices[V.A],P.Vertices[V.B])){Lock(V.A);Lock(V.B);C.SetOrUpdateEdgeConstraint(E,FEdgeConstraint::FullyConstrained());}
 }
 FEndpointSimplifier S(&M,P.Vertices);S.CollapseMode=FQEMSimplification::ESimplificationCollapseModes::MinimalExistingVertexError;
 S.bPreserveBoundaryShape=true;S.bAllowSeamCollapse=false;S.bPreventTinyTriangles=true;S.SetExternalConstraints(MoveTemp(C));S.SimplifyToTriangleCount(Target);
 FKKRuntimeMeshPart Reduced;Reduced.NativePart=P.NativePart;Reduced.ShaderKind=P.ShaderKind;Reduced.Name=P.Name;Reduced.Role=P.Role;Reduced.Blend=P.Blend;
 TArray<int32> Map;Map.Init(INDEX_NONE,M.MaxVertexID());TSet<int32> Remaining;
 for(int32 T:M.TriangleIndicesItr()){const auto Tri=M.GetTriangle(T);const int32 IDs[3]={Tri.A,Tri.B,Tri.C};int32 Comp=INDEX_NONE;
  for(int32 V:IDs){const int32 Src=S.Source[V];if(Comp!=INDEX_NONE&&Comp!=Component[Src]){Retain=TEXT("component merge rejected");return false;}Comp=Component[Src];
   if(FVector3d(P.Vertices[Src].Position)!=M.GetVertex(V)){Retain=TEXT("non-endpoint movement rejected");return false;}
   if(Map[V]==INDEX_NONE){Map[V]=Reduced.Vertices.Num();Reduced.Vertices.Add(P.Vertices[Src]);}Reduced.Indices.Add(Map[V]);
  }Remaining.Add(Comp);
 }
 if(Remaining.Num()!=Components||Reduced.Indices.IsEmpty()){Retain=TEXT("component disappearance rejected");return false;}
 Out=MoveTemp(Reduced);return true;
}
}
TFuture<FKKRuntimeMeshReductionResult> OKReduceRuntimeMeshesAsync(TArray<FKKRuntimeMeshPart> Meshes,uint32 Body,uint32 Clothes,uint32 Others,bool PreserveSourceGeometry,bool ClassRules){
 return Async(EAsyncExecution::ThreadPool,[Meshes=MoveTemp(Meshes),Body,Clothes,Others,PreserveSourceGeometry,ClassRules]()mutable{
  auto Classify=[ClassRules](const FKKRuntimeMeshPart& P){return ClassRules?RuleGroup(P):Group(P);};
  if(ClassRules)Others=MAX_uint32;
  FKKRuntimeMeshReductionResult R;const double Start=FPlatformTime::Seconds();R.WorkerThread=!IsInGameThread();
  auto Finish=[&](){R.PrepareMs=(FPlatformTime::Seconds()-Start)*1000;return MoveTemp(R);};
  if(Meshes.IsEmpty()||!Body||!Clothes||!Others){R.Error=TEXT("Invalid runtime reduction budgets/parts");return Finish();}
  for(const auto& P:Meshes){if(P.Indices.IsEmpty()||P.Indices.Num()%3){R.Error=TEXT("Invalid reduction triangle stream");return Finish();}
   for(uint32 I:P.Indices)if(I>=uint32(P.Vertices.Num())){R.Error=TEXT("Reduction index out of range");return Finish();}
   for(const auto& V:P.Vertices)if(V.Position.ContainsNaN()||!V.Influences||V.Influences>12){R.Error=TEXT("Invalid reduction vertex stream");return Finish();}
  }
  if(PreserveSourceGeometry){
   uint64 Totals[4]={};R.Parts.Reserve(Meshes.Num());
   for(const auto& P:Meshes){FKKRuntimeReductionPartStats S;S.NativePart=P.NativePart;S.Group=Classify(P);
    S.SourceTriangles=S.TargetTriangles=S.Triangles=P.Indices.Num()/3;
    S.RetentionReason=TEXT("source geometry explicitly preserved");Totals[S.Group]+=S.Triangles;R.Parts.Add(MoveTemp(S));
   }
   // Geometry quality and triangle-budget compliance are independent facts.
   R.BudgetsMet=Totals[1]<=Body&&Totals[2]<=Clothes&&Totals[3]<=Others;
   R.SourceGeometryPreserved=true;R.Meshes=MoveTemp(Meshes);return Finish();
  }
  TArray<uint32> Targets;Targets.SetNumZeroed(Meshes.Num());const uint32 Limits[4]={MAX_uint32,Body,Clothes,Others};
  for(int32 G=0;G<4;++G){TArray<int32> Pending;uint64 Sum=0;
   for(int32 I=0;I<Meshes.Num();++I)if(Classify(Meshes[I])==G){Pending.Add(I);Sum+=Meshes[I].Indices.Num()/3;}
   if(Sum<=Limits[G]){for(int32 I:Pending)Targets[I]=Meshes[I].Indices.Num()/3;continue;}
   // Reserve small features, then distribute a capped proportional budget.
   int64 Left=Limits[G];TArray<int32> Small=Pending;Small.Sort([&](int32 A,int32 B){return Meshes[A].Indices.Num()<Meshes[B].Indices.Num();});
   uint32 Kept=0;for(int32 I:Small){const uint32 N=Meshes[I].Indices.Num()/3;if(N<=512&&Kept+N<=Limits[G]/4){Targets[I]=N;Kept+=N;Left-=N;Pending.Remove(I);}}
   Left=FMath::Max<int64>(0,Left-int64(Pending.Num())*24);
   while(!Pending.IsEmpty()){double W=0;for(int32 I:Pending)W+=double(Meshes[I].Indices.Num()/3);TArray<int32> Capped;
    for(int32 I:Pending){const uint32 N=Meshes[I].Indices.Num()/3;if(24.+double(Left)*N/W>=N)Capped.Add(I);}
    if(Capped.IsEmpty()){for(int32 I:Pending)Targets[I]=FMath::Min<uint32>(Meshes[I].Indices.Num()/3,24+uint32(double(Left)*(Meshes[I].Indices.Num()/3)/W));break;}
    for(int32 I:Capped){Targets[I]=Meshes[I].Indices.Num()/3;Left=FMath::Max<int64>(0,Left-int64(Targets[I])+24);Pending.Remove(I);}
   }
  }
  uint64 Totals[4]={};R.Parts.Reserve(Meshes.Num());
  for(int32 I=0;I<Meshes.Num();++I){auto& P=Meshes[I];FKKRuntimeReductionPartStats S;S.NativePart=P.NativePart;S.Group=Classify(P);S.SourceTriangles=P.Indices.Num()/3;S.TargetTriangles=Targets[I];
   if(S.SourceTriangles>S.TargetTriangles){FKKRuntimeMeshPart Reduced;if(ReducePart(P,Targets[I],Reduced,S.RetentionReason))P=MoveTemp(Reduced);}
   S.Triangles=P.Indices.Num()/3;if(S.Triangles>S.TargetTriangles&&S.RetentionReason.IsEmpty())S.RetentionReason=TEXT("protected topology/skin boundaries prevent target");Totals[S.Group]+=S.Triangles;R.Parts.Add(MoveTemp(S));
  }
  // Refill within each role group: a protected part must not strand spare
  // reduction capacity in parts that reached their first allocation.
  for(int32 G=1;G<4;++G)for(int32 Round=0;Round<3&&Totals[G]>Limits[G];++Round){
   TArray<int32> Candidates;uint64 Weight=0;for(int32 I=0;I<R.Parts.Num();++I){const auto& S=R.Parts[I];if(S.Group==G&&S.Triangles<S.SourceTriangles&&S.Triangles<=S.TargetTriangles+2&&S.Triangles>24){Candidates.Add(I);Weight+=S.Triangles;}}
   if(Candidates.IsEmpty())break;const uint64 Deficit=Totals[G]-Limits[G];uint64 Saved=0;
   for(int32 I:Candidates){auto& S=R.Parts[I];const uint32 Remove=FMath::Min<uint64>(S.Triangles-24,(Deficit*S.Triangles+Weight-1)/Weight);const uint32 Target=S.Triangles-Remove;
    FKKRuntimeMeshPart Reduced;FString Retain;if(ReducePart(Meshes[I],Target,Reduced,Retain)){const uint32 N=Reduced.Indices.Num()/3;Saved+=S.Triangles-N;Meshes[I]=MoveTemp(Reduced);S.Triangles=N;}
    S.TargetTriangles=Target;S.RetentionReason=S.Triangles>Target?(Retain.IsEmpty()?TEXT("protected topology/skin boundaries prevent target"):Retain):FString();
   }Totals[G]-=Saved;if(!Saved)break;
  }
  R.BudgetsMet=Totals[1]<=Body&&Totals[2]<=Clothes&&Totals[3]<=Others;R.Meshes=MoveTemp(Meshes);return Finish();
 });
}
