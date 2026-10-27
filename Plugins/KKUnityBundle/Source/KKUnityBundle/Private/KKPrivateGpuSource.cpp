#include "KKPrivateGpuSource.h"
#include "KKRustExtract.h"
#include "ok_extract.h"

namespace {
bool ConvertPose(uint64 Revision,TConstArrayView<float> Values,const float* Post,FKKPrivateGpuPose& Pose,FString& Error){
 const int32 Actual=Values.Num()/16;
 if(!Post||Actual<2||Actual>65535||Values.Num()!=Actual*16){Error=TEXT("Private GPU palette layout invalid");return false;}
 FKKPrivateGpuPose Next;Next.Revision=Revision;Next.Palette.SetNumUninitialized(Actual);
 auto Convert=[](const float* Native,FMatrix44f& Out){
  // Native skinning consumes upper affine rows. Its unused homogeneous row
  // can contain inverse-bind roundoff; preserve all consumed coefficients.
  for(int32 I=0;I<16;++I)if(!FMath::IsFinite(Native[I]))return false;
  Out=FMatrix44f::Identity;
  for(int32 R=0;R<3;++R)for(int32 C=0;C<4;++C)Out.M[C][R]=Native[R*4+C];
  return true;
 };
 for(int32 B=0;B<Actual;++B)if(!Convert(Values.GetData()+B*16,Next.Palette[B])){Error=TEXT("Nonfinite native GPU palette");return false;}
 // Conversion validates every native coefficient and writes an affine row.
 // No throwaway palette-row allocation is needed to validate that same value.
 if(!Convert(Post,Next.PostCorrection)||!Next.Palette.Last().Equals(FMatrix44f::Identity,0.f)){Error=TEXT("Private GPU post/fallback invalid");return false;}
 Pose=MoveTemp(Next);Error.Reset();return true;
}
bool ReadPose(const FOkSession& Session,int32 Part,uint64 Revision,uint32 Count,OkSkinPaletteView& View,FKKPrivateGpuPose& Pose,FString& Error){
 TArray<float> Values,Post;const int32 Actual=Session.PrivateSkinStagesRevision(Part,Revision,View,Values,Post);
 if(Actual<2||Actual>65535||Values.Num()!=Actual*16||Post.Num()!=16||(Count&&Count!=uint32(Actual))){Error=TEXT("Private GPU source revision/layout unavailable");return false;}
 return ConvertPose(Revision,Values,Post.GetData(),Pose,Error);
}
}
bool OKReadPrivateGpuPoses(const FOkSession& Session,TConstArrayView<int32> Parts,TConstArrayView<const FKKPrivateGpuSource*> Sources,TArray<FKKPrivateGpuPose>& Poses,FString& Error){
 if(Parts.IsEmpty()||Parts.Num()!=Sources.Num()){Error=TEXT("Private GPU batch layout invalid");return false;}
 TArray<uint64> Revisions;TArray<int32> Counts;
 for(const auto* Source:Sources){if(!Source||!Source->Revision||Source->PaletteCount<2||Source->FallbackSlot!=Source->PaletteCount-1||Source->Vertices.IsEmpty()){Error=TEXT("Private GPU batch source unavailable");return false;}Revisions.Add(Source->Revision);Counts.Add(Source->PaletteCount);}
 TArray<float> Values,Posts;if(!Session.PrivateSkinStagesBatch(Parts,Revisions,Counts,Values,Posts)){Error=TEXT("Private GPU batch revision/layout unavailable");return false;}
 TArray<FKKPrivateGpuPose> Next;Next.SetNum(Parts.Num());int32 Offset=0;
 for(int32 I=0;I<Parts.Num();++I){if(!ConvertPose(Revisions[I],MakeArrayView(Values.GetData()+Offset,Counts[I]*16),Posts.GetData()+I*16,Next[I],Error))return false;Offset+=Counts[I]*16;}
 Poses=MoveTemp(Next);Error.Reset();return true;
}
bool OKCapturePrivateGpuSource(const FOkSession& Session,int32 Part,FKKPrivateGpuSource& Out,FKKPrivateGpuPose& Pose,FString& Error){
 const uint64 Revision=Session.PrivateSkinSourceRevision(Part);
 if(!Revision){Error=TEXT("Private native GPU source unavailable");return false;}
 OkSkinPaletteView View{};FKKPrivateGpuPose NextPose;
 if(!ReadPose(Session,Part,Revision,0,View,NextPose,Error))return false;
 if(View.n_verts<=0||!View.positions||!View.normals||!View.weights||!View.bone_idx){Error=FString::Printf(TEXT("Private source has incomplete GPU direction streams (%s%s%s%s); retain CPU"),
  View.positions?TEXT(""):TEXT("positions "),View.normals?TEXT(""):TEXT("normals "),View.weights?TEXT(""):TEXT("weights "),View.bone_idx?TEXT(""):TEXT("bones "));return false;}
 // A mesh without tangents (Alice's accessories: six parts skinned on the CPU every frame of the one-on-one scene) gets
 // one at right angles to each normal; such parts have no normal map to read it.
 TArray<float> MadeTangents;
 if(!View.tangents){
  MadeTangents.SetNumUninitialized(View.n_verts*4);
  for(int32 I=0;I<View.n_verts;++I){const FVector3f N(View.normals[I*3],View.normals[I*3+1],View.normals[I*3+2]);
   const FVector3f Axis=FMath::Abs(N.X)<.9f?FVector3f(1,0,0):FVector3f(0,1,0);const FVector3f T=FVector3f::CrossProduct(Axis,N).GetSafeNormal();
   MadeTangents[I*4]=T.X;MadeTangents[I*4+1]=T.Y;MadeTangents[I*4+2]=T.Z;MadeTangents[I*4+3]=1.f;}
 }
 const float* Tangents=View.tangents?View.tangents:MadeTangents.GetData();
 FKKPrivateGpuSource Next;Next.Revision=Revision;Next.PaletteCount=NextPose.Palette.Num();Next.FallbackSlot=Next.PaletteCount-1;
 Next.Vertices.SetNumUninitialized(View.n_verts);
 for(int32 I=0;I<View.n_verts;++I){auto& V=Next.Vertices[I];const float* P=View.positions+I*3,*N=View.normals+I*3,*T=Tangents+I*4,*W=View.weights+I*4;const uint16* B=View.bone_idx+I*4;
  V.Position=FVector4f(P[0],P[1],P[2],1);V.Normal=FVector4f(N[0],N[1],N[2],0);
  V.Tangent=FVector4f(T[0],T[1],T[2],T[3]);V.Weights=FVector4f(W[0],W[1],W[2],W[3]);V.Bones=FUintVector4(B[0],B[1],B[2],B[3]);
 }
 const FOKGpuDeformDraw Probe{};
 if(!OKValidateGpuDeformSource(Next.Vertices,MakeArrayView(&Probe,1),Next.PaletteCount,Next.FallbackSlot,Error))return false;
 Out=MoveTemp(Next);Pose=MoveTemp(NextPose);Error.Reset();return true;
}
bool OKReadPrivateGpuPose(const FOkSession& Session,int32 Part,const FKKPrivateGpuSource& Source,FKKPrivateGpuPose& Pose,FString& Error){
 if(!Source.Revision||Source.PaletteCount<2||Source.FallbackSlot!=Source.PaletteCount-1||Source.Vertices.IsEmpty()){Error=TEXT("Private GPU source was not captured");return false;}
 OkSkinPaletteView View{};FKKPrivateGpuPose Next;
 if(!ReadPose(Session,Part,Source.Revision,Source.PaletteCount,View,Next,Error))return false;
 if(View.n_verts!=Source.Vertices.Num()){Error=TEXT("Private GPU source vertex layout changed");return false;}
 Pose=MoveTemp(Next);Error.Reset();return true;
}
bool OKEvaluatePrivateGpuPosition(const FKKPrivateGpuSource& Source,const FKKPrivateGpuPose& Pose,const FOKGpuDeformDraw& Draw,FVector& Out){
 if(Source.Revision!=Pose.Revision||Pose.Palette.Num()!=int32(Source.PaletteCount)||!Source.Vertices.IsValidIndex(Draw.Binding.X)||!Pose.Palette.IsValidIndex(Source.FallbackSlot))return false;
 const auto& V=Source.Vertices[Draw.Binding.X];const FVector3f Reference(V.Position.X,V.Position.Y,V.Position.Z),RestNormal(V.Normal.X,V.Normal.Y,V.Normal.Z);
 const FVector Offset(Draw.Offset.X,Draw.Offset.Y,Draw.Offset.Z);const bool NormalNeeded=Offset!=FVector::ZeroVector;
 FVector3f P=FVector3f::ZeroVector,N=FVector3f::ZeroVector;float Total=0;
 for(int32 K=0;K<4;++K){const float Weight=V.Weights[K];const uint32 Bone=V.Bones[K];if(FMath::Abs(Weight)<1.e-6f||!Pose.Palette.IsValidIndex(Bone))continue;
  P+=FVector3f(Pose.Palette[Bone].TransformPosition(Reference))*Weight;if(NormalNeeded)N+=FVector3f(Pose.Palette[Bone].TransformVector(RestNormal))*Weight;Total+=Weight;
 }
 if(Total>1.e-5f){P/=Total;if(NormalNeeded)N/=Total;}else{P=FVector3f(Pose.Palette[Source.FallbackSlot].TransformPosition(Reference));if(NormalNeeded)N=FVector3f(Pose.Palette[Source.FallbackSlot].TransformVector(RestNormal));}
 P=FVector3f(Pose.PostCorrection.TransformPosition(P));FVector Result(-double(P.X)*100.,double(P.Z)*100.,double(P.Y)*100.);
 if(NormalNeeded){const float Length=N.Size();N=Length>1.e-8f?N/Length:FVector3f(0,0,1);const FVector3f PostN(Pose.PostCorrection.TransformVector(N));const float PostLength=PostN.Size();if(PostLength>1.e-8f)N=PostN/PostLength;
  const FVector Current=FVector(-N.X,N.Z,N.Y).GetSafeNormal(),Rest=FVector(Draw.RestNormal.X,Draw.RestNormal.Y,Draw.RestNormal.Z).GetSafeNormal();const FQuat Rotation=Rest.IsNearlyZero()||Current.IsNearlyZero()?FQuat::Identity:FQuat::FindBetweenNormals(Rest,Current);Result+=Rotation.RotateVector(Offset);
 }
 if(Result.ContainsNaN())return false;Out=Result;return true;
}
bool OKBuildPrivateGpuDraw(const FKKPrivateGpuSource& Source,TConstArrayView<int32> Indices,uint32 Section,
 TConstArrayView<FVector> Offsets,TConstArrayView<FVector> SectionNormals,TConstArrayView<FVector> RestNormals,TArray<FOKGpuDeformDraw>& Out,FString& Error){
 const int32 N=Source.Vertices.Num();
 if(!Source.Revision||!N||Offsets.Num()!=N||SectionNormals.Num()!=N||RestNormals.Num()!=N){Error=TEXT("Private GPU draw source layout invalid");return false;}
 TArray<FOKGpuDeformDraw> Next;Next.SetNumUninitialized(Indices.IsEmpty()?N:Indices.Num());
 for(int32 I=0;I<Next.Num();++I){const int32 V=Indices.IsEmpty()?I:Indices[I];if(V<0||V>=N){Error=TEXT("Private GPU draw source index invalid");return false;}
  auto& D=Next[I];D.Binding=FUintVector4(uint32(V),Section,0,0);D.Offset=FVector4f(FVector3f(Offsets[V]),0);
  D.SectionNormal=FVector4f(FVector3f(SectionNormals[V]),0);D.RestNormal=FVector4f(FVector3f(RestNormals[V]),0);
 }
 if(!OKValidateGpuDeformSource(Source.Vertices,Next,Source.PaletteCount,Source.FallbackSlot,Error))return false;
 Out=MoveTemp(Next);Error.Reset();return true;
}
