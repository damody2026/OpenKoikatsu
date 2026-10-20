#include "KKSkeletalBuilder.h"
#include "KKRustExtract.h"
#include "ok_extract.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSkeletalMeshCacheTest,"OpenKoikatsu.Preview.SkeletalMeshCache",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSkeletalMeshCacheTest::RunTest(const FString&) {
 const FString Root=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_ROOT")),Card=FPlatformMisc::GetEnvironmentVariable(TEXT("OK_TEST_WARM_CARD"));
 if(Root.IsEmpty()||Card.IsEmpty()){AddError(TEXT("Set OK_TEST_WARM_ROOT and OK_TEST_WARM_CARD"));return false;}
 OKPrepareLiveGpuEnv();FOkSession Session;FString Error;
 if(!TestTrue(TEXT("Real portable source opens"),Session.Open(Root,Card,0,Error)))return false;
 TStrongObjectPtr<USkeleton> Skeleton(OKBuildSharedSkeleton(Session,GetTransientPackage()));
 if(!TestNotNull(TEXT("Complete reference skeleton"),Skeleton.Get()))return false;
 int32 Part=INDEX_NONE;OkMeshView View{};
 for(int32 I=0;I<Session.PartCount();++I){OkMeshView V{};if(Session.PartAt(I,V)&&V.n_verts>0&&V.rest_positions&&V.weights&&V.bone_idx&&V.uvs){Part=I;View=V;break;}}
 if(!TestTrue(TEXT("Source has a skinned renderer"),Part>=0))return false;
 OKClearSkeletalMeshCache();
 TStrongObjectPtr<USkeletalMesh> Base(OKBuildSkeletalMesh(Session,View,GetTransientPackage(),Skeleton.Get(),Part));
 if(!TestNotNull(TEXT("Initial GPU mesh"),Base.Get()))return false;
 TestTrue(TEXT("Mesh cannot retain its original actor through Outer"),Base->GetOuter()==GetTransientPackage());
 TestTrue(TEXT("Skeleton cannot retain its original actor through Outer"),Base->GetSkeleton()->GetOuter()==GetTransientPackage());
 TestTrue(TEXT("Identical inputs reuse immutable GPU asset"),OKBuildSkeletalMesh(Session,View,GetTransientPackage(),Skeleton.Get(),Part)==Base.Get());
 FOkSession Other;if(!TestTrue(TEXT("Independent native session opens"),Other.Open(Root,Card,0,Error)))return false;
 TStrongObjectPtr<USkeleton> OtherSkeleton(OKBuildSharedSkeleton(Other,GetTransientPackage()));OkMeshView OtherView{};Other.PartAt(Part,OtherView);
 auto Compare=[&](const TCHAR* Label,const void* A,const void* B,int32 Bytes){if((!A)!=(!B)||(A&&B&&FMemory::Memcmp(A,B,Bytes)!=0))UE_LOG(LogTemp,Warning,TEXT("Mesh cache fixture difference %s bytes=%d"),Label,Bytes);};
 if(View.n_verts==OtherView.n_verts){const int32 N=View.n_verts;
  Compare(TEXT("rest_positions"),View.rest_positions,OtherView.rest_positions,N*3*sizeof(float));Compare(TEXT("reference_normals"),View.reference_normals?View.reference_normals:View.normals,OtherView.reference_normals?OtherView.reference_normals:OtherView.normals,N*3*sizeof(float));
  Compare(TEXT("rest_tangents"),View.rest_tangents,OtherView.rest_tangents,N*4*sizeof(float));Compare(TEXT("weights"),View.weights,OtherView.weights,N*4*sizeof(float));Compare(TEXT("bone_idx"),View.bone_idx,OtherView.bone_idx,N*4*sizeof(uint16));
  Compare(TEXT("uvs"),View.uvs,OtherView.uvs,N*2*sizeof(float));Compare(TEXT("uv2"),View.uv2,OtherView.uv2,N*2*sizeof(float));Compare(TEXT("uv3"),View.uv3,OtherView.uv3,N*2*sizeof(float));Compare(TEXT("uv4"),View.uv4,OtherView.uv4,N*2*sizeof(float));Compare(TEXT("eye_uv"),View.eye_sample_uvs,OtherView.eye_sample_uvs,N*2*sizeof(float));Compare(TEXT("colors"),View.vertex_colors,OtherView.vertex_colors,N*4);
 }
 const auto& ARef=Skeleton->GetReferenceSkeleton();const auto& BRef=OtherSkeleton->GetReferenceSkeleton();
 if(ARef.GetNum()==BRef.GetNum())for(int32 B=0;B<ARef.GetNum();++B)if(ARef.GetBoneName(B)!=BRef.GetBoneName(B)||ARef.GetParentIndex(B)!=BRef.GetParentIndex(B)||!ARef.GetRefBonePose()[B].Equals(BRef.GetRefBonePose()[B],0.))UE_LOG(LogTemp,Warning,TEXT("Mesh cache fixture reference difference bone=%d %s %s"),B,*ARef.GetBoneName(B).ToString(),*BRef.GetBoneName(B).ToString());
 TestTrue(TEXT("Identical content across independent sessions and skeleton objects reuses asset"),OKBuildSkeletalMesh(Other,OtherView,GetTransientPackage(),OtherSkeleton.Get(),Part)==Base.Get());
 auto Changed=[&](const TCHAR* Label,OkMeshView V){auto* Mesh=OKBuildSkeletalMesh(Session,V,GetTransientPackage(),Skeleton.Get(),Part);TestTrue(Label,Mesh&&Mesh!=Base.Get());};
 TArray<float> UV;UV.Append(View.uvs,View.n_verts*2);UV[0]+=.03125f;auto V=View;V.uvs=UV.GetData();Changed(TEXT("UV change cannot reuse stale vertex buffer"),V);
 TArray<float> Positions;Positions.Append(View.rest_positions,View.n_verts*3);Positions[0]+=.01f;V=View;V.rest_positions=Positions.GetData();Changed(TEXT("Reference geometry change invalidates asset"),V);
 TArray<float> Weights;Weights.Append(View.weights,View.n_verts*4);Weights[0]+=.05f;V=View;V.weights=Weights.GetData();Changed(TEXT("Skin weight change invalidates asset"),V);
 TestTrue(TEXT("Outline winding has an independent asset"),OKBuildSkeletalMesh(Session,View,GetTransientPackage(),Skeleton.Get(),Part,0.f,true)!=Base.Get());
 // Colors are not baked into this mesh. They remain independent material overrides.
 V=View;V.item_colors[0][0]+=.125f;TestTrue(TEXT("Color edits retain geometry for independent materials"),OKBuildSkeletalMesh(Session,V,GetTransientPackage(),Skeleton.Get(),Part)==Base.Get());
 TStrongObjectPtr<USkeleton> Altered(NewObject<USkeleton>());FReferenceSkeleton Ref=OtherSkeleton->GetReferenceSkeleton();
 {FReferenceSkeletonModifier Modifier(Ref,Altered.Get());FTransform Pose=Ref.GetRefBonePose()[0];Pose.AddToTranslation(FVector(1,0,0));Modifier.UpdateRefPoseTransform(0,Pose);}
 auto* Temporary=NewObject<USkeletalMesh>();Temporary->SetRefSkeleton(Ref);Temporary->SetSkeleton(Altered.Get());Altered->MergeAllBonesToBoneTree(Temporary);
 TestTrue(TEXT("Changed reference rig cannot reuse stale inverse binds"),OKBuildSkeletalMesh(Session,View,GetTransientPackage(),Altered.Get(),Part)!=Base.Get());
 for(int32 I=0;I<Session.PartCount();++I)if(Session.MorphCount(I)>0){OkMeshView Face{};if(Session.PartAt(I,Face)&&Face.rest_positions&&Face.weights&&Face.bone_idx){
  auto* With=OKBuildSkeletalMesh(Session,Face,GetTransientPackage(),Skeleton.Get(),I);auto* Without=OKBuildSkeletalMesh(Session,Face,GetTransientPackage(),Skeleton.Get(),-1);
  TestTrue(TEXT("Morph target data is part of the mesh identity"),With&&Without&&With!=Without);break;}}
 const auto Stats=OKSkeletalMeshCacheStats();TestTrue(TEXT("Real cache hits recorded"),Stats.Hits>=3);TestTrue(TEXT("Metadata remains bounded"),Stats.Entries<=256);
 TWeakObjectPtr<USkeletalMesh> Weak=Base.Get();Base.Reset();CollectGarbage(RF_NoFlags);
 TestFalse(TEXT("Weak cache does not retain unreferenced GPU assets"),Weak.IsValid());OKClearSkeletalMeshCache();
 return !HasAnyErrors();
}
#endif
