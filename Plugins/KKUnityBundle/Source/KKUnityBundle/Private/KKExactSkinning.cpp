#include "KKExactSkinning.h"
#include "KKSkeletalBuilder.h"
#include "ok_extract.h"
#include "Misc/AutomationTest.h"
#include <limits>
#if WITH_EDITOR
#include "KKImportedAnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimCompressionTypes.h"
#endif

FMatrix OKSkinPaletteToUnreal(const float* M){
 // Match KKSkeletalBuilder: Unity (X,Y,Z) -> UE (-X,Z,Y).
 const int32 Map[3]={0,2,1};const double Sign[3]={-1.,1.,1.};FMatrix Result=FMatrix::Identity;
 for(int32 A=0;A<3;++A){for(int32 B=0;B<3;++B)Result.M[A][B]=Sign[A]*Sign[B]*M[Map[B]*4+Map[A]];
  Result.M[3][A]=100.*Sign[A]*M[Map[A]*4+3];}
 return Result;
}
bool OKPrepareExactSkinPalette(const OkSkinPaletteView& Raw,const TArray<float>& Values,TConstArrayView<int32> Slots,TArray<FMatrix44f>& Out,FString& Error){
 if(Values.IsEmpty()||Values.Num()%16||Slots.IsEmpty()||Slots.Num()>65536){Error=TEXT("Exact native palette layout invalid");return false;}
 TArray<FMatrix> Prepared;Prepared.Reserve(Values.Num()/16);
 for(int32 B=0;B<Values.Num()/16;++B){const float* Source=Values.GetData()+B*16;
  for(int32 K=0;K<16;++K)if(!FMath::IsFinite(Source[K])){Error=TEXT("Exact native palette nonfinite source");return false;}
  // Native point/vector skinning consumes the upper 3x4 rows only. Bind
  // inversion can leave roundoff in the unused homogeneous row; retain the
  // existing OKSkinPaletteToUnreal contract rather than rejecting valid rigs.
  Prepared.Add(OKSkinPaletteToUnreal(Source));
 }
 TArray<FMatrix44f> Next;Next.Reserve(Slots.Num());
 for(int32 Slot:Slots){FMatrix Matrix;
  if(!OKExactPreparedPaletteMatrix(Raw,Prepared,Slot,Matrix)){Error=TEXT("Exact native palette binding invalid");return false;}
  const FMatrix44f Converted(Matrix);
  for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)if(!FMath::IsFinite(Converted.M[R][C])){Error=TEXT("Exact native palette float overflow");return false;}
  Next.Add(Converted);
 }
 Out=MoveTemp(Next);Error.Reset();return true;
}
bool OKHasSignedSkinVertex(const OkSkinPaletteView& Raw,int32 Vertex){
 if(!Raw.weights||Vertex<0||Vertex>=Raw.n_verts)return false;
 double Sum=0;for(int32 K=0;K<4;++K){Sum+=Raw.weights[Vertex*4+K];if(Raw.weights[Vertex*4+K]<=-1e-6f)return true;}return Sum<=1e-5;
}
template<typename ReadMatrix>static bool ExactPaletteMatrix(const OkSkinPaletteView& Raw,int32 Count,ReadMatrix Read,int32 Slot,FMatrix& Out){
 if(Count<1)return false;
 if(Slot>=0){if(Slot>=Count)return false;Out=Read(Slot);return !Out.ContainsNaN();}
 const int64 Vertex=-int64(Slot)-1;if(Vertex>=Raw.n_verts||!Raw.weights||!Raw.bone_idx)return false;
 double Total=0;for(int32 K=0;K<4;++K){const float W=Raw.weights[Vertex*4+K];if(!FMath::IsFinite(W))return false;Total+=W;}
 if(Total<=1e-5){Out=Read(Count-1);return !Out.ContainsNaN();}
 FMatrix Blend(ForceInit);double Sum=0;
 for(int32 K=0;K<4;++K){const float W=Raw.weights[Vertex*4+K];if(!FMath::IsFinite(W))return false;if(FMath::Abs(W)<1e-6f)continue;
  const int32 Bone=Raw.bone_idx[Vertex*4+K];if(Bone>=Count)return false;
  const FMatrix M=Read(Bone);Sum+=W;
  for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Blend.M[R][C]+=M.M[R][C]*W;
 }
 if(Sum<=1e-5)return false;for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Blend.M[R][C]/=Sum;
 if(Blend.ContainsNaN())return false;Out=Blend;return true;
}
bool OKExactPaletteMatrix(const OkSkinPaletteView& Raw,const TArray<float>& Matrices,int32 Slot,FMatrix& Out){
 if(Matrices.Num()%16)return false;
 return ExactPaletteMatrix(Raw,Matrices.Num()/16,[&](int32 Bone){return OKSkinPaletteToUnreal(Matrices.GetData()+Bone*16);},Slot,Out);
}
bool OKExactPreparedPaletteMatrix(const OkSkinPaletteView& Raw,const TArray<FMatrix>& Matrices,int32 Slot,FMatrix& Out){
 return ExactPaletteMatrix(Raw,Matrices.Num(),[&](int32 Bone){return Matrices[Bone];},Slot,Out);
}
FKKScopedNativeSkinSource::FKKScopedNativeSkinSource(bool EnableNativeSource){
 PreviousMode=OKCaptureSkinEnvironmentState();
 for(const TCHAR* Key:{TEXT("OK_CPU_SKIN"),TEXT("OK_GPU_SKIN"),TEXT("OK_SKELMESH"),TEXT("OK_NOPOSE"),TEXT("OK_UE_ANIM")})Previous.Add(Key,FPlatformMisc::GetEnvironmentVariable(Key));
 if(EnableNativeSource){FPlatformMisc::SetEnvironmentVar(TEXT("OK_CPU_SKIN"),TEXT("1"));OKPrepareLiveGpuEnv();}
}
FKKScopedNativeSkinSource::~FKKScopedNativeSkinSource(){
 for(const auto& Entry:Previous)FPlatformMisc::SetEnvironmentVar(*Entry.Key,Entry.Value.IsEmpty()?nullptr:*Entry.Value);
 OKRestoreSkinEnvironmentState(PreviousMode);
}
FTransform OKSkinPaletteColumn(const FMatrix& Matrix,int32 Axis){
 check(Axis>=0&&Axis<3);const FVector Basis=Axis==0?FVector::XAxisVector:Axis==1?FVector::YAxisVector:FVector::ZAxisVector;
 const FVector Column(Matrix.M[Axis][0],Matrix.M[Axis][1],Matrix.M[Axis][2]);const double Length=Column.Size();
 FVector Scale=FVector::ZeroVector;Scale[Axis]=3.*Length;
 const FQuat Rotation=Length>1e-12?FQuat::FindBetweenNormals(Basis,Column/Length):FQuat::Identity;
 return FTransform(Rotation,Matrix.GetOrigin(),Scale);
}
FTransform OKSkinPaletteColumnContinuous(const FMatrix& Matrix,int32 Axis,const FQuat& Previous){
 FTransform Result=OKSkinPaletteColumn(Matrix,Axis);const FVector Basis=Axis==0?FVector::XAxisVector:Axis==1?FVector::YAxisVector:FVector::ZAxisVector;
 const FVector Column(Matrix.M[Axis][0],Matrix.M[Axis][1],Matrix.M[Axis][2]);
 const FQuat Rotation=Column.Size()>1e-12?(FQuat::FindBetweenNormals(Previous.RotateVector(Basis),Column.GetSafeNormal())*Previous).GetNormalized():Previous;
 Result.SetRotation(Rotation);return Result;
}
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKSignedPaletteMath,"OpenKoikatsu.Animation.SignedPalette",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKSignedPaletteMath::RunTest(const FString&){
 float Weights[]={.80685407f,.21141423f,-.011098274f,-.007169951f};uint16 Bones[]={0,1,2,3};
 OkSkinPaletteView Raw{};Raw.weights=Weights;Raw.bone_idx=Bones;Raw.n_verts=1;
 TArray<float> Matrices;Matrices.Init(0.f,64);
 for(int32 B=0;B<4;++B){for(int32 A=0;A<4;++A)Matrices[B*16+A*4+A]=1.f;
  Matrices[B*16+1]=B*.3f;Matrices[B*16+3]=B*.02f;Matrices[B*16+7]=B*-.03f;Matrices[B*16+11]=B*.01f;}
 FMatrix M;if(!TestTrue(TEXT("Signed native vertex supported"),OKExactPaletteMatrix(Raw,Matrices,-1,M)))return false;
 const FVector P(7,9,-13);FVector Reference=FVector::ZeroVector;double Sum=0;
 for(int32 B=0;B<4;++B){const float* N= Matrices.GetData()+B*16;const FVector U(-P.X/100.,P.Z/100.,P.Y/100.);
  const FVector Native(N[0]*U.X+N[1]*U.Y+N[2]*U.Z+N[3],N[4]*U.X+N[5]*U.Y+N[6]*U.Z+N[7],N[8]*U.X+N[9]*U.Y+N[10]*U.Z+N[11]);
  Reference+=FVector(-Native.X,Native.Z,Native.Y)*100.*Weights[B];Sum+=Weights[B];}
 Reference/=Sum;FVector Reconstructed=FVector::ZeroVector;
 for(int32 A=0;A<3;++A)Reconstructed+=OKSkinPaletteColumn(M,A).TransformPosition(P)/3.;
 TestTrue(TEXT("Positive UE weights reproduce signed native skin"),Reference.Equals(Reconstructed,1e-6));
 TestFalse(TEXT("Signed vertex outside source rejected"),OKExactPaletteMatrix(Raw,Matrices,-2,M));
 Weights[0]=std::numeric_limits<float>::quiet_NaN();TestFalse(TEXT("Nonfinite weights rejected"),OKExactPaletteMatrix(Raw,Matrices,-1,M));
 for(float& W:Weights)W=0;
 TestTrue(TEXT("Unweighted source vertex selects native placement"),OKExactPaletteMatrix(Raw,Matrices,-1,M));
 TestTrue(TEXT("Unweighted placement includes authored transform"),M.Equals(OKSkinPaletteToUnreal(Matrices.GetData()+48),1e-6));
 TArray<FMatrix> Prepared;for(int32 Bone=0;Bone<4;++Bone)Prepared.Add(OKSkinPaletteToUnreal(Matrices.GetData()+Bone*16));
 const float Cases[][4]={{1,0,0,0},{.80685407f,.21141423f,-.011098274f,-.007169951f},{0,0,0,0},{-1,0,0,0},{.0000001f,1,0,0},{std::numeric_limits<float>::quiet_NaN(),0,0,0}};
 for(const auto& Case:Cases)for(int32 Invalid:{0,1})for(int32 Slot:{-2,-1,0,3,4}){
  for(int32 K=0;K<4;++K)Weights[K]=Case[K];Bones[0]=Invalid?99:0;
  FMatrix Expected=FMatrix::Identity,Actual=FMatrix::Identity;const bool Before=OKExactPaletteMatrix(Raw,Matrices,Slot,Expected),After=OKExactPreparedPaletteMatrix(Raw,Prepared,Slot,Actual);
  TestEqual(TEXT("Prepared palette preserves validity for signed, empty and invalid inputs"),After,Before);
  if(Before){bool Exact=true;for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Exact&=Expected.M[R][C]==Actual.M[R][C];TestTrue(TEXT("Prepared palette preserves every matrix coefficient"),Exact);}
 }
 return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKExactPaletteMath,"OpenKoikatsu.Animation.ExactPaletteMath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FKKExactPaletteMath::RunTest(const FString&){
 const uint8 BeforeMode=OKCaptureSkinEnvironmentState();TMap<FString,FString> Before;
 for(const TCHAR* Key:{TEXT("OK_CPU_SKIN"),TEXT("OK_GPU_SKIN"),TEXT("OK_SKELMESH"),TEXT("OK_NOPOSE"),TEXT("OK_UE_ANIM")})Before.Add(Key,FPlatformMisc::GetEnvironmentVariable(Key));
 {FKKScopedNativeSkinSource Scope;TestEqual(TEXT("Source mode retains native SMR"),FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GPU_SKIN")),FString(TEXT("0")));}
 TestEqual(TEXT("Caller renderer cache restored"),OKCaptureSkinEnvironmentState(),BeforeMode);for(const auto& Pair:Before)TestEqual(TEXT("Caller environment restored"),FPlatformMisc::GetEnvironmentVariable(*Pair.Key),Pair.Value);
 float Buffer[16];for(float& V:Buffer)V=123.f;OkSkinPaletteView Sentinel{};Sentinel.n_verts=456;
 TestEqual(TEXT("Null session rejected"),ok_part_skin_palette(nullptr,0,&Sentinel,Buffer,1),-1);
 TestEqual(TEXT("Negative capacity rejected"),ok_part_skin_palette(nullptr,0,&Sentinel,Buffer,-1),-1);
 TestEqual(TEXT("Failure preserves view"),Sentinel.n_verts,456);for(float V:Buffer)TestEqual(TEXT("Failure preserves matrix buffer"),V,123.f);
 const float Matrices[][16]={
  {1.,.7f,-.2f,2.,0.,-2.,.4f,-3.,.1f,0.,.6f,4.,0.,0.,0.,1.},
  {0.,0.,0.,2.,0.,0.,0.,-3.,0.,0.,0.,4.,0.,0.,0.,1.},
  {-1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.}
 };
 {
  TArray<float> Values;for(const auto& Source:Matrices)Values.Append(Source,16);
  float Weights[4]={.81f,.21f,-.02f,0};uint16 Bones[4]={0,1,2,0};OkSkinPaletteView Raw{};Raw.weights=Weights;Raw.bone_idx=Bones;Raw.n_verts=1;
  const TArray<int32> Slots={2,-1,0,1};TArray<FMatrix44f> Owned;FString Error;
  TestTrue(TEXT("Native palette adapter accepts full affine ordered/signed slots"),OKPrepareExactSkinPalette(Raw,Values,Slots,Owned,Error));
  for(int32 B=0;B<Slots.Num();++B){FMatrix Expected;const bool Valid=OKExactPaletteMatrix(Raw,Values,Slots[B],Expected);TestTrue(TEXT("Adapter retains original signed/fallback formula"),Valid&&Owned.IsValidIndex(B)&&Owned[B].Equals(FMatrix44f(Expected),0));}
  const auto Stable=Owned;auto Invalid=Slots;Invalid.Add(99);
  TestFalse(TEXT("Invalid native binding rejects entire publication"),OKPrepareExactSkinPalette(Raw,Values,Invalid,Owned,Error));TestTrue(TEXT("Invalid binding keeps owned output"),Owned==Stable);
  for(float& W:Weights)W=0;
  TestTrue(TEXT("Native unweighted placement accepted"),OKPrepareExactSkinPalette(Raw,Values,Slots,Owned,Error));TestTrue(TEXT("Native unweighted placement uses appended fallback"),Owned[1].Equals(FMatrix44f(OKSkinPaletteToUnreal(Matrices[2])),0));
  Values[0]=std::numeric_limits<float>::infinity();const auto Last=Owned;
  TestFalse(TEXT("Native infinity rejected"),OKPrepareExactSkinPalette(Raw,Values,Slots,Owned,Error));TestTrue(TEXT("Nonfinite source keeps last publication"),Owned==Last);
  Values[0]=Matrices[0][0];Values[12]=.5f;TestTrue(TEXT("Unused native homogeneous row follows existing 3x4 contract"),OKPrepareExactSkinPalette(Raw,Values,Slots,Owned,Error));TestTrue(TEXT("Unused native row cannot change affine renderer matrix"),Owned==Last);
  Values.Pop();TestFalse(TEXT("Native truncated source rejected"),OKPrepareExactSkinPalette(Raw,Values,Slots,Owned,Error));
 }
 for(const auto& Source:Matrices){const FMatrix M=OKSkinPaletteToUnreal(Source);
  for(const FVector P:{FVector(7.,9.,-13.),FVector::ZeroVector,FVector(100.,-200.,300.)}){
   const FVector Unity(-P.X/100.,P.Z/100.,P.Y/100.);
   const FVector Native(Source[0]*Unity.X+Source[1]*Unity.Y+Source[2]*Unity.Z+Source[3],Source[4]*Unity.X+Source[5]*Unity.Y+Source[6]*Unity.Z+Source[7],Source[8]*Unity.X+Source[9]*Unity.Y+Source[10]*Unity.Z+Source[11]);
   TestTrue(TEXT("Source coordinate convention"),FVector(M.TransformPosition(P)).Equals(FVector(-Native.X,Native.Z,Native.Y)*100.,1e-5));
   FVector Reconstructed=FVector::ZeroVector;FVector Normal=FVector::ZeroVector;
   for(int32 A=0;A<3;++A){const auto T=OKSkinPaletteColumn(M,A);TestFalse(TEXT("Zero scales remain finite"),T.ContainsNaN());
    Reconstructed+=T.TransformPosition(P)/3.;Normal+=T.TransformVector(P)/3.;}
   TestTrue(TEXT("Full affine position including shear/reflection/zero"),Reconstructed.Equals(FVector(M.TransformPosition(P)),1e-5));
   TestTrue(TEXT("Same linear map for directions"),Normal.Equals(FVector(M.TransformVector(P)),1e-5));
  }
#if WITH_EDITOR
  // Prove Unreal's actual animation codec preserves rank-one/zero scales.
  auto* Skeleton=NewObject<USkeleton>();auto* Mesh=NewObject<USkeletalMesh>();FReferenceSkeleton Ref;
  {FReferenceSkeletonModifier Modifier(Ref,Skeleton);Modifier.Add(FMeshBoneInfo(TEXT("Root"),TEXT("Root"),INDEX_NONE),FTransform::Identity);
   for(int32 A=0;A<3;++A){const FString Name=FString::Printf(TEXT("Column%d"),A);Modifier.Add(FMeshBoneInfo(FName(*Name),Name,0),FTransform::Identity);}}
  Mesh->SetRefSkeleton(Ref);Skeleton->MergeAllBonesToBoneTree(Mesh);
  auto* Seq=NewObject<UKKImportedAnimSequence>();Seq->SetSkeleton(Skeleton);auto& Ctrl=Seq->GetController();
  Ctrl.OpenBracket(NSLOCTEXT("KK","PaletteCodecAudit","Verify exact palette codec"),false);Ctrl.InitializeModel();Ctrl.SetFrameRate(FFrameRate(30,1),false);Ctrl.SetNumberOfFrames(FFrameNumber(1),false);
  for(int32 B=0;B<4;++B){const FTransform T=B==0?FTransform::Identity:OKSkinPaletteColumn(M,B-1);const FName Name=Ref.GetBoneName(B);
   TestTrue(TEXT("Column track created"),Ctrl.AddBoneCurve(Name,false));TestTrue(TEXT("Column keys authored"),Ctrl.SetBoneTrackKeys(Name,{T.GetTranslation(),T.GetTranslation()},{T.GetRotation(),T.GetRotation()},{T.GetScale3D(),T.GetScale3D()},false));}
  Ctrl.NotifyPopulated();Ctrl.CloseBracket(false);Seq->PostEditChange();Seq->CacheDerivedDataForCurrentPlatform();Seq->WaitOnExistingCompression();
  {const auto Data=Seq->GetCompressedData();TestTrue(TEXT("Actual compressed palette valid"),Data.Get().IsValid(Seq));}
  const FVector P(100.,-200.,300.);FVector Reconstructed=FVector::ZeroVector;
  for(int32 A=0;A<3;++A){FTransform T;FAnimExtractContext Context(0.);Context.bEnforceCompressedDataSampling=true;Seq->GetBoneTransform(T,FSkeletonPoseBoneIndex(A+1),Context,false);
   TestFalse(TEXT("Decoded zero scales finite"),T.ContainsNaN());Reconstructed+=T.TransformPosition(P)/3.;}
  TestTrue(TEXT("Compressed affine palette matches source"),Reconstructed.Equals(FVector(M.TransformPosition(P)),.001));
#endif
 }
 return !HasAnyErrors();
}
#endif
