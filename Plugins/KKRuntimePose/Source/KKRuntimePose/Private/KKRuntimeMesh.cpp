#include "KKRuntimeMesh.h"
#include "KKRuntimeNativeSession.h"
#include "KKRuntimePoseBankTypes.h"
#include "KKRuntimePoseMath.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"

bool OKRuntimeEyeExpressionUV(FVector2f Sample,float Turns,FVector2f& Out){
 if(Sample.ContainsNaN()||!FMath::IsFinite(Turns))return false;if(Turns==0){Out=Sample;return true;}float Sin,Cos;FMath::SinCos(&Sin,&Cos,-Turns*2.f*PI);const FVector2f Center=Sample-FVector2f(.5f,.5f);const FVector2f Result(Cos*Center.X+Sin*Center.Y+.5f,-Sin*Center.X+Cos*Center.Y+.5f);if(Result.ContainsNaN())return false;Out=Result;return true;
}

// Rank-deficient source skinning can deliberately flatten/disable a body region.
// Retain its authored rest coordinates in the null space instead of inventing an inverse.
static FVector RecoverSingularSkin(const FMatrix& M,const FVector& Target,const FVector& Hint,bool Point){
 const FVector Residual=Target-FVector(Point?M.TransformPosition(Hint):M.TransformVector(Hint));if(Residual.SizeSquared()<1e-16)return Hint;
 double Gram[3][3]={},Q[3][3]={{1,0,0},{0,1,0},{0,0,1}},B[3]={};for(int32 R=0;R<3;++R){for(int32 C=0;C<3;++C)for(int32 K=0;K<3;++K)Gram[R][C]+=M.M[R][K]*M.M[C][K];B[R]=M.M[R][0]*Residual.X+M.M[R][1]*Residual.Y+M.M[R][2]*Residual.Z;}
 for(int32 Iteration=0;Iteration<24;++Iteration){int32 P=0,R=1;double Largest=FMath::Abs(Gram[0][1]);for(int32 A=0;A<3;++A)for(int32 C=A+1;C<3;++C)if(FMath::Abs(Gram[A][C])>Largest){Largest=FMath::Abs(Gram[A][C]);P=A;R=C;}if(Largest<1e-20)break;const double Angle=.5*FMath::Atan2(2*Gram[P][R],Gram[R][R]-Gram[P][P]),C=FMath::Cos(Angle),S=FMath::Sin(Angle);const double PP=Gram[P][P],RR=Gram[R][R],PR=Gram[P][R];for(int32 K=0;K<3;++K)if(K!=P&&K!=R){const double A=Gram[K][P],D=Gram[K][R];Gram[K][P]=Gram[P][K]=C*A-S*D;Gram[K][R]=Gram[R][K]=S*A+C*D;}Gram[P][P]=C*C*PP-2*S*C*PR+S*S*RR;Gram[R][R]=S*S*PP+2*S*C*PR+C*C*RR;Gram[P][R]=Gram[R][P]=0;for(int32 K=0;K<3;++K){const double A=Q[K][P],D=Q[K][R];Q[K][P]=C*A-S*D;Q[K][R]=S*A+C*D;}}
 FVector Delta=FVector::ZeroVector;const double Threshold=FMath::Max3(Gram[0][0],Gram[1][1],Gram[2][2])*1e-12;for(int32 K=0;K<3;++K)if(Gram[K][K]>FMath::Max(Threshold,1e-20)){const double Projection=(Q[0][K]*B[0]+Q[1][K]*B[1]+Q[2][K]*B[2])/Gram[K][K];Delta+=FVector(Q[0][K],Q[1][K],Q[2][K])*Projection;}return Hint+Delta;
}
// Every morph referenced by the control is kept, not only the selected pair.
// Weights are applied later as desired-minus-baseline. The catalog and sparse
// deltas are plain data and remain valid after the native session closes.
static bool OKCaptureRuntimeFace(OkSession* Session,int32 Part,const OkMeshView& V,const OkRustBindings& B,TSharedPtr<const FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>& Catalog,TArray<TArray<FKKRuntimeFaceSparse>>& PerVertex,uint64& Payload,FString& Error,uint64 Limit){
 Catalog.Reset();PerVertex.Reset();Payload=0;
 auto Fail=[&](const TCHAR* Why){Error=Why;Catalog.Reset();PerVertex.Reset();Payload=0;return false;};
 auto Built=MakeShared<FKKRuntimeFaceCatalog,ESPMode::ThreadSafe>();bool Any=false;
 for(int32 Control=0;Control<3;++Control){
  const int32 Count=B.face_pattern_count(Session,Part,Control);
  if(Count<0||Count>256)return Fail(TEXT("Runtime face pattern count invalid"));
  if(!Count)continue;Any=true;
  auto& C=Built->Control[Control];C.Close.SetNum(Count);C.Open.SetNum(Count);
  TMap<int32,int16> Local;TArray<int32> Morphs;
  auto Id=[&](int32 Native)->int16{if(Native<0)return -1;if(const int16* Found=Local.Find(Native))return *Found;if(Morphs.Num()>=512)return -2;const int16 Next=int16(Morphs.Num());Local.Add(Native,Next);Morphs.Add(Native);return Next;};
  for(int32 P=0;P<Count;++P){int32 Close=INDEX_NONE,Open=INDEX_NONE;if(B.face_pair(Session,Part,Control,P,&Close,&Open)!=0)return Fail(TEXT("Runtime face pattern missing"));
   const int16 CloseId=Id(Close),OpenId=Id(Open);if(CloseId==-2||OpenId==-2)return Fail(TEXT("Runtime face pattern capacity exceeded"));
   C.Close[P]=CloseId;C.Open[P]=OpenId;
  }
  C.Baseline.SetNum(Morphs.Num());
  const uint64 CatalogBytes=uint64(C.Close.GetAllocatedSize()+C.Open.GetAllocatedSize()+C.Baseline.GetAllocatedSize());if(Payload+CatalogBytes>Limit)return Fail(TEXT("Runtime face catalog exceeds preallocation budget"));Payload+=CatalogBytes;
  for(int32 M=0;M<Morphs.Num();++M){const float Baseline=B.face_baseline_weight(Session,Part,Morphs[M])/100.f;if(!FMath::IsFinite(Baseline))return Fail(TEXT("Runtime face baseline nonfinite"));C.Baseline[M]=Baseline;
   const char* Name=nullptr;const int32 N=ok_part_raw_morph(Session,Part,Morphs[M],&Name,nullptr,nullptr,0);if(N<0||N>V.n_verts)return Fail(TEXT("Runtime face morph count invalid"));if(!N)continue;
   if(Payload+uint64(PerVertex.GetAllocatedSize())+uint64(N)*16>Limit)return Fail(TEXT("Runtime face morph read buffers exceed budget"));TArray<uint32> Indices;TArray<float> Deltas;Indices.SetNumUninitialized(N);Deltas.SetNumUninitialized(N*3);
   if(ok_part_raw_morph(Session,Part,Morphs[M],&Name,Indices.GetData(),Deltas.GetData(),N)!=N)return Fail(TEXT("Runtime face morph copy failed"));
   if(PerVertex.IsEmpty()){const uint64 Temporary=uint64(DefaultCalculateSlackReserve(V.n_verts,sizeof(TArray<FKKRuntimeFaceSparse>),true,alignof(TArray<FKKRuntimeFaceSparse>)))*sizeof(TArray<FKKRuntimeFaceSparse>);if(Payload+Temporary+uint64(N)*16>Limit)return Fail(TEXT("Runtime face temporary buffers exceed budget"));PerVertex.SetNum(V.n_verts);}
   for(int32 K=0;K<N;++K){if(Indices[K]>=uint32(V.n_verts))return Fail(TEXT("Runtime face morph vertex outside renderer"));const float* D=Deltas.GetData()+K*3;const FVector3f Offset(-D[0]*100.f,D[2]*100.f,D[1]*100.f);if(Offset.ContainsNaN())return Fail(TEXT("Runtime face morph delta nonfinite"));if(Offset==FVector3f::ZeroVector)continue;auto& Row=PerVertex[Indices[K]];const uint64 Old=Row.GetAllocatedSize();if(Row.Num()==Row.Max()){const uint64 Next=uint64(DefaultCalculateSlackGrow(Row.Num()+1,Row.Max(),sizeof(FKKRuntimeFaceSparse),true,alignof(FKKRuntimeFaceSparse)))*sizeof(FKKRuntimeFaceSparse);if(Payload+Next-Old+uint64(PerVertex.GetAllocatedSize())+uint64(N)*16>Limit)return Fail(TEXT("Runtime face sparse buffers exceed budget"));}Row.Add({uint8(Control),uint16(M),Morphs[M],Offset});Payload+=uint64(Row.GetAllocatedSize())-Old;}
  }
 }
 if(!Any)return true;

 Catalog=Built;return true;
}
bool OKCaptureRuntimeMeshes(OkSession* Session,TConstArrayView<FKKRuntimePoseBone> Bones,
 TConstArrayView<FKKRuntimePalettePart> Parts,TArray<FKKRuntimeMeshPart>& Out,uint64 Budget,FString& Error,bool CaptureFacePatterns){
 auto Fail=[&](const FString& Why){Error=Why;return false;};const auto& B=OKRuntimeNativeBindings();
 TMap<FName,int32> ByName;for(int32 I=0;I<Bones.Num();++I)ByName.Add(Bones[I].Name,I);
 // -CrowdLegwearInflate=<cm>: socks and stockings are pushed out along their
 // drawn normal so the leg underneath cannot show through them.
 float LegwearInflate=0;FParse::Value(FCommandLine::Get(),TEXT("CrowdLegwearInflate="),LegwearInflate);
 if(!FMath::IsFinite(LegwearInflate)||LegwearInflate<0||LegwearInflate>5)LegwearInflate=0;
 uint64 Bytes=0,FaceBytes=0;for(const auto& Part:Parts){OkMeshView V{};if(B.part_at(Session,Part.NativePart,&V)!=0||(!V.render_visible&&!OKRuntimeIsTearRenderer(V.name))||V.n_verts<=0||V.n_idx<=0||V.n_idx%3||!V.positions||!V.normals||!V.indices||!V.uvs)return Fail(TEXT("Runtime visible mesh stream invalid"));
  Bytes+=uint64(DefaultCalculateSlackReserve(V.n_verts,sizeof(FKKRuntimeMeshVertex),true,alignof(FKKRuntimeMeshVertex)))*sizeof(FKKRuntimeMeshVertex)+uint64(DefaultCalculateSlackReserve(V.n_idx,sizeof(uint32),true,alignof(uint32)))*sizeof(uint32);
  if(Bytes>Budget)return Fail(TEXT("Runtime mesh buffers exceed preallocation budget"));
 }
 TArray<FKKRuntimeMeshPart> Meshes;
 for(const auto& Part:Parts){OkMeshView V{};if(B.part_at(Session,Part.NativePart,&V)!=0)return Fail(TEXT("Runtime mesh disappeared"));
  OkSkinPaletteView Raw{};TArray<float> Matrices;
  if(Part.RigidFollow.IsEmpty()){const int32 Count=ok_part_skin_palette(Session,Part.NativePart,nullptr,nullptr,0);if(Count<=0||Count>65535)return Fail(TEXT("Runtime mesh palette unavailable"));Matrices.SetNumUninitialized(Count*16);if(ok_part_skin_palette(Session,Part.NativePart,&Raw,Matrices.GetData(),Count)!=Count||Raw.n_verts!=V.n_verts)return Fail(TEXT("Runtime mesh/palette vertex layouts differ"));}
  FMatrix Rigid=FMatrix::Identity;if(!Part.RigidFollow.IsEmpty()){float M[16];if(ok_bone_world_matrix(Session,TCHAR_TO_UTF8(*Part.RigidFollow),M)!=0)return Fail(TEXT("Runtime rigid mesh socket unavailable"));Rigid=Part.RigidBindWorld.Inverse()*OKSkinPaletteToUnreal(M);}
  const FString PartRole=V.role?FString(UTF8_TO_TCHAR(V.role)).ToLower():FString();
  const float Inflate=(PartRole.StartsWith(TEXT("clothes_socks"))||PartRole.StartsWith(TEXT("clothes_panst")))?LegwearInflate:0.f;
  FKKRuntimeMeshPart Mesh;Mesh.NativePart=Part.NativePart;Mesh.Name=V.name?UTF8_TO_TCHAR(V.name):TEXT("");Mesh.Role=V.role?UTF8_TO_TCHAR(V.role):TEXT("");Mesh.Blend=V.blend?UTF8_TO_TCHAR(V.blend):TEXT("");Mesh.ShaderKind=V.shader_kind;Mesh.Vertices.Reserve(V.n_verts);Mesh.Indices.Reserve(V.n_idx);
  for(int32 I=0;I<V.n_idx;I+=3){if(V.indices[I]>=uint32(V.n_verts)||V.indices[I+1]>=uint32(V.n_verts)||V.indices[I+2]>=uint32(V.n_verts))return Fail(TEXT("Runtime triangle index outside renderer"));Mesh.Indices.Append({V.indices[I],V.indices[I+2],V.indices[I+1]});}
  TArray<float> ExpressionUVs;if(V.eye_draw==1||V.eye_draw==2){if(ok_part_eye_expression_uvs(Session,Part.NativePart,nullptr,0)!=V.n_verts)return Fail(TEXT("Runtime expression source UV count differs"));ExpressionUVs.SetNumUninitialized(V.n_verts*2);if(ok_part_eye_expression_uvs(Session,Part.NativePart,ExpressionUVs.GetData(),V.n_verts)!=V.n_verts)return Fail(TEXT("Runtime expression source UV copy failed"));}
  TArray<FVector3f> MouthClosed,MouthOpen;MouthClosed.Init(FVector3f::ZeroVector,V.n_verts);MouthOpen=MouthClosed;
  int32 ClosedMorph=INDEX_NONE,OpenMorph=INDEX_NONE;
  if(B.mouth_pattern_count(Session,Part.NativePart)>0&&B.mouth_pair(Session,Part.NativePart,0,&ClosedMorph,&OpenMorph)==0){
   TSet<int32> Morphs;if(ClosedMorph>=0)Morphs.Add(ClosedMorph);if(OpenMorph>=0)Morphs.Add(OpenMorph);
   for(int32 Morph:Morphs){const float Baseline=B.face_baseline_weight(Session,Part.NativePart,Morph)/100.f;
    if(!FMath::IsFinite(Baseline))return Fail(TEXT("Runtime mouth baseline nonfinite"));
    const char* MorphName=nullptr;const int32 Count=ok_part_raw_morph(Session,Part.NativePart,Morph,&MorphName,nullptr,nullptr,0);
    if(Count<0||Count>V.n_verts)return Fail(TEXT("Runtime mouth morph count invalid"));if(!Count)continue;
    TArray<uint32> Indices;TArray<float> Deltas;Indices.SetNumUninitialized(Count);Deltas.SetNumUninitialized(Count*3);
    if(ok_part_raw_morph(Session,Part.NativePart,Morph,&MorphName,Indices.GetData(),Deltas.GetData(),Count)!=Count)return Fail(TEXT("Runtime mouth morph copy failed"));
    for(int32 K=0;K<Count;++K){if(Indices[K]>=uint32(V.n_verts))return Fail(TEXT("Runtime mouth morph vertex outside renderer"));const float* Delta=Deltas.GetData()+K*3;const FVector3f Offset(-Delta[0]*100,Delta[2]*100,Delta[1]*100);if(Offset.ContainsNaN())return Fail(TEXT("Runtime mouth morph delta nonfinite"));
     MouthClosed[Indices[K]]+=Offset*((Morph==ClosedMorph?1.f:0.f)-Baseline);MouthOpen[Indices[K]]+=Offset*((Morph==OpenMorph?1.f:0.f)-Baseline);
    }
   }
  }
  TArray<FVector3f> BlinkClosed;BlinkClosed.Init(FVector3f::ZeroVector,V.n_verts);
  const int32 EyePatterns=B.face_pattern_count(Session,Part.NativePart,1);if(EyePatterns<0||EyePatterns>256)return Fail(TEXT("Runtime eye pattern count invalid"));
  TSet<int32> EyeMorphs;int32 EyeClosed=INDEX_NONE;
  for(int32 P=0;P<EyePatterns;++P){int32 C=INDEX_NONE,O=INDEX_NONE;if(B.face_pair(Session,Part.NativePart,1,P,&C,&O)!=0)return Fail(TEXT("Runtime eye pattern missing"));if(P==0)EyeClosed=C;if(C>=0)EyeMorphs.Add(C);if(O>=0)EyeMorphs.Add(O);}
  for(int32 Morph:EyeMorphs){const float Baseline=B.face_baseline_weight(Session,Part.NativePart,Morph)/100.f,Weight=(Morph==EyeClosed?1.f:0.f)-Baseline;
   if(!FMath::IsFinite(Weight))return Fail(TEXT("Runtime eye baseline nonfinite"));if(FMath::Abs(Weight)<1e-8f)continue;
   const char* Name=nullptr;const int32 Count=ok_part_raw_morph(Session,Part.NativePart,Morph,&Name,nullptr,nullptr,0);if(Count<0||Count>V.n_verts)return Fail(TEXT("Runtime eye morph count invalid"));if(!Count)continue;
   TArray<uint32> Indices;TArray<float> Deltas;Indices.SetNumUninitialized(Count);Deltas.SetNumUninitialized(Count*3);
   if(ok_part_raw_morph(Session,Part.NativePart,Morph,&Name,Indices.GetData(),Deltas.GetData(),Count)!=Count)return Fail(TEXT("Runtime eye morph copy failed"));
   for(int32 K=0;K<Count;++K){if(Indices[K]>=uint32(V.n_verts))return Fail(TEXT("Runtime eye morph vertex outside renderer"));const float* D=Deltas.GetData()+K*3;const FVector3f Offset(-D[0]*100,D[2]*100,D[1]*100);if(Offset.ContainsNaN())return Fail(TEXT("Runtime eye morph delta nonfinite"));BlinkClosed[Indices[K]]+=Offset*Weight;}
  }
  TSharedPtr<const FKKRuntimeFaceCatalog,ESPMode::ThreadSafe> FaceCatalog;TArray<TArray<FKKRuntimeFaceSparse>> FaceDeltas;uint64 FacePayload=0;
  if(CaptureFacePatterns&&!OKCaptureRuntimeFace(Session,Part.NativePart,V,B,FaceCatalog,FaceDeltas,FacePayload,Error,Budget-Bytes-FaceBytes))return false;
  if(Bytes+FaceBytes+FacePayload>Budget)return Fail(TEXT("Runtime mesh buffers exceed preallocation budget"));FaceBytes+=FacePayload;
  if(FParse::Param(FCommandLine::Get(),TEXT("RuntimeUVInputs"))){const float* Channels[]={V.uvs,V.uv2,V.uv3,V.uv4};for(int32 U=0;U<4;++U){FVector2f Lo(FLT_MAX,FLT_MAX),Hi(-FLT_MAX,-FLT_MAX);if(Channels[U])for(int32 I=0;I<V.n_verts;++I){const FVector2f UV(Channels[U][I*2],Channels[U][I*2+1]);Lo.X=FMath::Min(Lo.X,UV.X);Lo.Y=FMath::Min(Lo.Y,UV.Y);Hi.X=FMath::Max(Hi.X,UV.X);Hi.Y=FMath::Max(Hi.Y,UV.Y);}UE_LOG(LogTemp,Display,TEXT("Runtime UV input part=%s channel=%d present=%d min=(%.6f,%.6f) max=(%.6f,%.6f)"),UTF8_TO_TCHAR(V.name),U,Channels[U]!=nullptr,Lo.X,Lo.Y,Hi.X,Hi.Y);}}
  for(int32 I=0;I<V.n_verts;++I){TMap<int32,double> Combined;
   if(!Part.RigidFollow.IsEmpty())Combined.Add(0,1.);
   else {if(!Raw.weights||!Raw.bone_idx)return Fail(TEXT("Runtime mesh influences unavailable"));double Sum=0;bool Signed=false;
    for(int32 K=0;K<4;++K){const float W=Raw.weights[I*4+K];if(!FMath::IsFinite(W))return Fail(TEXT("Runtime mesh influence nonfinite"));Sum+=W;Signed|=W<=-1e-6f;}
    if(Signed||Sum<=1e-5)Combined.Add(-I-1,1.);else for(int32 K=0;K<4;++K)if(Raw.weights[I*4+K]>=1e-6f)Combined.FindOrAdd(Raw.bone_idx[I*4+K])+=Raw.weights[I*4+K];
   }
   TArray<int32> Slots;Combined.GetKeys(Slots);Slots.Sort();double Total=0;for(int32 Slot:Slots)Total+=Combined[Slot];if(Total<=0||Slots.Num()>4)return Fail(TEXT("Runtime mesh influence count invalid"));
   FMatrix Blend(ForceInit);TArray<int32> Quantized;int32 Acc=0,Largest=0;
   for(int32 K=0;K<Slots.Num();++K){const double W=Combined[Slots[K]]/Total;FMatrix M=Rigid;if(Part.RigidFollow.IsEmpty()&&!OKExactPaletteMatrix(Raw,Matrices,Slots[K],M))return Fail(TEXT("Runtime exact mesh palette slot unavailable"));
    for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Blend.M[R][C]+=M.M[R][C]*W;
    Quantized.Add(FMath::RoundToInt(W*21845.));Acc+=Quantized.Last();if(Combined[Slots[K]]>Combined[Slots[Largest]])Largest=K;
   }Quantized[Largest]+=21845-Acc;
   if(Blend.ContainsNaN())return Fail(FString::Printf(TEXT("Runtime mesh reference skin is singular part=%d vertex=%d"),Part.NativePart,I));
   const bool Singular=FMath::Abs(Blend.Determinant())<1e-12;const FMatrix Inverse=Singular?FMatrix::Identity:Blend.Inverse();auto Point=[](const float* P){return FVector(-P[0],P[2],P[1])*100.;};auto Direction=[](const float* P){return FVector(-P[0],P[2],P[1]);};
   FKKRuntimeMeshVertex Vertex;// Rigid native streams remain in their neutral, world-baked placement.
   // Unlike skinned streams, skin() does not apply the attachment delta to them.
   // Apply it once for the initial-frame oracle; retain neutral vertices/normals
   // so every private pose uses the absolute delta from RigidBindWorld.
   const bool RigidPart=!Part.RigidFollow.IsEmpty();const FVector SourcePosition=Point(V.positions+I*3);
   FVector Initial=RigidPart?FVector(Rigid.TransformPosition(SourcePosition)):SourcePosition;
   // Inflate in drawn space before the reference inverse, so skinning returns
   // exactly the inflated surface at the capture pose.
   if(Inflate>0&&!RigidPart)Initial+=Direction(V.normals+I*3).GetSafeNormal()*Inflate;
   Vertex.InitialPosition=FVector3f(Initial);Vertex.Position=RigidPart?FVector3f(SourcePosition):FVector3f(FVector(Inverse.TransformPosition(Initial)));
   Vertex.Normal=FVector3f((RigidPart?Direction(V.normals+I*3):FVector(Inverse.TransformVector(Direction(V.normals+I*3)))).GetSafeNormal());
   if(Singular){if(!V.rest_positions)return Fail(TEXT("Singular runtime skin lacks authored rest positions"));const FVector Hint=Point(V.rest_positions+I*3);const FVector Local=RecoverSingularSkin(Blend,Initial,Hint,true);if(Local.ContainsNaN()||FVector::Dist(FVector(Blend.TransformPosition(Local)),Initial)>.001)return Fail(FString::Printf(TEXT("Singular runtime skin cannot reproduce source geometry part=%d vertex=%d residual=%.9g determinant=%.9g target=(%.9g,%.9g,%.9g) local=(%.9g,%.9g,%.9g) matrix=(%.9g,%.9g,%.9g;%.9g,%.9g,%.9g;%.9g,%.9g,%.9g;%.9g,%.9g,%.9g)"),Part.NativePart,I,FVector::Dist(FVector(Blend.TransformPosition(Local)),Initial),Blend.Determinant(),Initial.X,Initial.Y,Initial.Z,Local.X,Local.Y,Local.Z,Blend.M[0][0],Blend.M[0][1],Blend.M[0][2],Blend.M[1][0],Blend.M[1][1],Blend.M[1][2],Blend.M[2][0],Blend.M[2][1],Blend.M[2][2],Blend.M[3][0],Blend.M[3][1],Blend.M[3][2]));Vertex.Position=FVector3f(Local);const FVector NormalHint=Direction(V.normals+I*3);Vertex.Normal=FVector3f(RecoverSingularSkin(Blend,Direction(V.normals+I*3),NormalHint,false).GetSafeNormal());}
   if(V.rest_tangents){Vertex.Tangent=FVector3f((RigidPart?Direction(V.rest_tangents+I*4):FVector(Inverse.TransformVector(Direction(V.rest_tangents+I*4)))).GetSafeNormal());Vertex.TangentSign=V.rest_tangents[I*4+3];}else Vertex.Tangent=FVector3f::CrossProduct(Vertex.Normal,FVector3f(0,0,1)).GetSafeNormal();
   if(V.eye_draw&&(!V.eye_sample_uvs||!V.uv2||!V.uv3))return Fail(TEXT("Runtime iris sample/highlight UV streams missing"));
   // CardReader's single-UV hair vertex factory repeats its last available
   // stream when the gloss coordinate requests another channel. Preserve that
   // display contract for legacy hair meshes without authored UV1; declared
   // UV1 (including zero-valued coordinates) always wins. Other families keep
   // their native missing-channel default, especially independent eye overlays.
   const float* HairUV1=V.uv2;
   if(!HairUV1&&(V.shader_kind==1||V.shader_kind==10))HairUV1=V.uvs;
   const float* UVs[]={V.eye_draw?V.eye_sample_uvs:V.uvs,HairUV1,V.uv3,V.uv4};for(int32 U=0;U<4;++U)Vertex.UV[U]=UVs[U]?FVector2f(UVs[U][I*2],1.f-UVs[U][I*2+1]):FVector2f(0.f,1.f);
   if(!ExpressionUVs.IsEmpty())Vertex.UV[3]=FVector2f(ExpressionUVs[I*2],1.f-ExpressionUVs[I*2+1]);
   for(const auto& UV:Vertex.UV)if(UV.ContainsNaN())return Fail(TEXT("Runtime mesh UV nonfinite"));
   if(V.vertex_colors)Vertex.Color=FColor(V.vertex_colors[I*4],V.vertex_colors[I*4+1],V.vertex_colors[I*4+2],V.vertex_colors[I*4+3]);
   if(Vertex.Position.ContainsNaN()||Vertex.Normal.ContainsNaN()||Vertex.InitialPosition.ContainsNaN())return Fail(TEXT("Runtime mesh position/normal nonfinite"));
   // Raw morph deltas precede the native palette. Convert through that exact
   // palette and the reconstructed reference inverse; never use body bindings
   // for private face/mouth renderers or treat a vector as a point.
   TArray<FKKRuntimeFaceSparse> Sparse;if(FaceDeltas.IsValidIndex(I))Sparse=MoveTemp(FaceDeltas[I]);
   if(!MouthClosed[I].IsNearlyZero()||!MouthOpen[I].IsNearlyZero()||!BlinkClosed[I].IsNearlyZero()||!Sparse.IsEmpty()){
    // A rigid part keeps its source-space vertices, so its deltas stay as they are. A singular skin (a collapsed
    // bone scale, e.g. jeanne / megumin) has no inverse: take the least-squares local delta the skin maps onto the
    // drawn one; whatever lies in its null space the skin cannot draw anyway. Failing here dropped the whole heroine.
    FMatrix NativeSkin=FMatrix::Identity;if(RigidPart==false&&!OKExactPaletteMatrix(Raw,Matrices,-I-1,NativeSkin))return Fail(TEXT("Runtime mouth native vertex palette missing"));
    auto Bind=[&](const FVector3f& Value){
     if(RigidPart)return Value;
     const FVector Drawn(NativeSkin.TransformVector(FVector(Value)));
     return FVector3f(Singular?RecoverSingularSkin(Blend,Drawn,FVector::ZeroVector,false):FVector(Inverse.TransformVector(Drawn)));};
    Vertex.MouthClosed=Bind(MouthClosed[I]);Vertex.MouthOpen=Bind(MouthOpen[I]);Vertex.BlinkClosed=Bind(BlinkClosed[I]);
    for(auto& Sample:Sparse){Sample.Delta=Bind(Sample.Delta);if(Sample.Delta.ContainsNaN())return Fail(TEXT("Runtime face morph delta nonfinite"));}
    if(Vertex.BlinkClosed.ContainsNaN()||Vertex.MouthClosed.ContainsNaN()||Vertex.MouthOpen.ContainsNaN())return Fail(TEXT("Runtime reference mouth delta nonfinite"));
   }
   Vertex.Face=MoveTemp(Sparse);Vertex.FaceCatalog=FaceCatalog;
   for(int32 K=0;K<Slots.Num();++K)if(Quantized[K]>0)for(int32 Axis=0;Axis<3;++Axis){const auto* Bone=ByName.Find(OKPaletteBoneName(Part.NativePart,Slots[K],Axis));if(!Bone||*Bone>65535)return Fail(TEXT("Runtime mesh palette bone absent from rig"));Vertex.Skin[Vertex.Influences++]={uint16(*Bone),uint16(Quantized[K])};}
   Mesh.Vertices.Add(Vertex);
  }
  // The tears lie a hair under the lower lid's skin (the source draws them over it): lifted 0.3 along their own
  // normals, they show as the wet line the source shows.
  if(OKRuntimeTearLevel(Mesh.Name))for(auto& X:Mesh.Vertices){X.Position+=X.Normal*0.3f;X.InitialPosition+=X.Normal*0.3f;}
  Meshes.Add(MoveTemp(Mesh));
 }
 Out=MoveTemp(Meshes);Error.Reset();return true;
}
bool OKRemapRuntimeMeshBones(TArray<FKKRuntimeMeshPart>& Meshes,TConstArrayView<int32> Map,int32 Bones,FString& Error){
 if(Meshes.IsEmpty()||Bones<=0||Bones>65536){Error=TEXT("Invalid runtime mesh remap target");return false;}
 TSet<int32> Targets;for(int32 Target:Map)if(Target!=INDEX_NONE){if(Target<0||Target>=Bones||Targets.Contains(Target)){Error=TEXT("Runtime mesh remap is not a valid injective pose map");return false;}Targets.Add(Target);}
 for(const auto& P:Meshes)for(const auto& V:P.Vertices){uint32 Sum=0;if(!V.Influences||V.Influences>12){Error=TEXT("Runtime mesh remap influence count invalid");return false;}for(int32 I=0;I<V.Influences;++I){const auto W=V.Skin[I];if(W.Bone>=Map.Num()||Map[W.Bone]<0||Map[W.Bone]>=Bones){Error=TEXT("Runtime mesh remap references removed/outside bone");return false;}Sum+=W.Weight;}if(Sum!=65535){Error=TEXT("Runtime mesh quantized weight total differs");return false;}}
 for(auto& P:Meshes)for(auto& V:P.Vertices)for(int32 I=0;I<V.Influences;++I)V.Skin[I].Bone=uint16(Map[V.Skin[I].Bone]);Error.Reset();return true;
}
bool OKAuditRuntimeMeshInitialPose(TConstArrayView<FKKRuntimeMeshPart> Meshes,TConstArrayView<FVector4f> Rows,double& Maximum,FString& Error){
 Maximum=0;if(Meshes.IsEmpty()||Rows.IsEmpty()||Rows.Num()%3){Error=TEXT("Runtime mesh initial-pose audit input missing");return false;}
 for(const auto& P:Meshes)for(const auto& V:P.Vertices){if(!V.Influences||V.Influences>12){Error=TEXT("Runtime mesh audit influence count invalid");return false;}FVector3d Position=FVector3d::ZeroVector;
  for(int32 I=0;I<V.Influences;++I){const auto W=V.Skin[I];if(int32(W.Bone)*3+2>=Rows.Num()){Error=TEXT("Runtime mesh binding outside GPU pose");return false;}const auto& A=Rows[W.Bone*3];const auto& B=Rows[W.Bone*3+1];const auto& C=Rows[W.Bone*3+2];const double Weight=double(W.Weight)/65535.;const FVector3d Local(V.Position);Position+=Weight*FVector3d(A.X*Local.X+A.Y*Local.Y+A.Z*Local.Z+A.W,B.X*Local.X+B.Y*Local.Y+B.Z*Local.Z+B.W,C.X*Local.X+C.Y*Local.Y+C.Z*Local.Z+C.W);}
  const double Distance=FVector3d::Distance(Position,FVector3d(V.InitialPosition));if(!FMath::IsFinite(Distance)){Error=TEXT("Runtime mesh GPU skin is nonfinite");return false;}Maximum=FMath::Max(Maximum,Distance);
 }if(Maximum>.01){Error=FString::Printf(TEXT("Runtime mesh GPU initial position differs %.9f cm"),Maximum);return false;}Error.Reset();return true;
}

// Low-poly sidecar written by ok-extract beside an exported APNG
// (`<card>.lowpoly.okmap`): per remeshed renderer, every low vertex is a
// barycentric point on a source triangle (geometry and skin) plus one on a
// UV-island triangle. Each low vertex is rebuilt in drawn space from the
// source streams of this session and then goes through the same reference
// inverse and slot quantization as source vertices, so palettes, pose banks
// and inverse binds are untouched. Renderers are matched by topology; any
// mismatch or non-invertible skin keeps that renderer's source mesh.
namespace {
struct FOKLowPolyCorner {uint32 Tri=0;float B[3]={1,0,0};uint32 UvTri=0;float U[3]={1,0,0};float Offset=0;};
struct FOKLowPolyPart {uint32 Vertices=0,Indices=0;uint64 Hash=0;TArray<FOKLowPolyCorner> Corners;TArray<uint32> Triangles;};
uint64 OKLowPolyIndexHash(const uint32* Indices,int32 Count){
 uint64 H=0xcbf29ce484222325ull;for(int32 I=0;I<Count;++I){const uint32 V=Indices[I];for(int32 K=0;K<4;++K){H^=(V>>(K*8))&0xff;H*=0x100000001b3ull;}}return H;
}
bool OKReadLowPolyMap(const FString& Path,TArray<FOKLowPolyPart>& Out,FString& Error){
 TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Path,FILEREAD_Silent)){Error=TEXT("missing");return false;}
 int64 At=0;auto Read=[&](void* Dst,int64 Size){if(At+Size>Bytes.Num())return false;FMemory::Memcpy(Dst,Bytes.GetData()+At,Size);At+=Size;return true;};
 char Magic[8];uint32 Parts=0;if(!Read(Magic,8)||FMemory::Memcmp(Magic,"OKLPMAP",7)!=0||(Magic[7]!='1'&&Magic[7]!='2')||!Read(&Parts,4)||Parts>4096){Error=TEXT("not an OKLPMAP1/2 file");return false;}
 const bool Offsets=Magic[7]=='2';
 for(uint32 P=0;P<Parts;++P){FOKLowPolyPart Part;uint32 Low=0,Tris=0;
  if(!Read(&Part.Vertices,4)||!Read(&Part.Indices,4)||!Read(&Part.Hash,8)||!Read(&Low,4)||!Read(&Tris,4)||Low>(1u<<22)||Tris>(1u<<22)){Error=TEXT("truncated part header");return false;}
  Part.Corners.SetNum(Low);for(auto& C:Part.Corners){float B[2],U[2];if(!Read(&C.Tri,4)||!Read(B,8)||!Read(&C.UvTri,4)||!Read(U,8)||(Offsets&&!Read(&C.Offset,4))){Error=TEXT("truncated vertex map");return false;}
   if(!FMath::IsFinite(C.Offset)||C.Offset<0||C.Offset>1){Error=TEXT("invalid low vertex offset");return false;}
   C.B[0]=B[0];C.B[1]=B[1];C.B[2]=1.f-B[0]-B[1];C.U[0]=U[0];C.U[1]=U[1];C.U[2]=1.f-U[0]-U[1];}
  Part.Triangles.SetNumUninitialized(Tris*3);if(!Read(Part.Triangles.GetData(),int64(Tris)*12)){Error=TEXT("truncated triangles");return false;}
  for(uint32 I:Part.Triangles)if(I>=Low){Error=TEXT("low triangle outside low vertices");return false;}
  Out.Add(MoveTemp(Part));}
 return true;
}
}

bool OKApplyRuntimeLowPoly(OkSession* Session,TConstArrayView<FKKRuntimePoseBone> Bones,TConstArrayView<FKKRuntimePalettePart> Parts,
 TArray<FKKRuntimeMeshPart>& Meshes,const FString& MapPath,FString& Summary){
 TArray<FOKLowPolyPart> Map;FString Why;if(!OKReadLowPolyMap(MapPath,Map,Why)){Summary=TEXT("low-poly map ")+Why;return false;}
 const auto& B=OKRuntimeNativeBindings();TMap<FName,int32> ByName;for(int32 I=0;I<Bones.Num();++I)ByName.Add(Bones[I].Name,I);
 int32 Applied=0,Kept=0;int64 Before=0,After=0;TArray<FString> Rejected;
 for(auto& Mesh:Meshes){Before+=Mesh.Indices.Num()/3;
  const FKKRuntimePalettePart* Part=Parts.FindByPredicate([&](const FKKRuntimePalettePart& P){return P.NativePart==Mesh.NativePart;});
  OkMeshView V{};if(!Part||B.part_at(Session,Mesh.NativePart,&V)!=0||V.n_verts!=Mesh.Vertices.Num()||!V.positions||!V.normals||!V.indices){After+=Mesh.Indices.Num()/3;++Kept;continue;}
  const uint64 Hash=OKLowPolyIndexHash(V.indices,V.n_idx);
  const FOKLowPolyPart* Low=Map.FindByPredicate([&](const FOKLowPolyPart& L){return L.Vertices==uint32(V.n_verts)&&L.Indices==uint32(V.n_idx)&&L.Hash==Hash;});
  if(!Low){After+=Mesh.Indices.Num()/3;++Kept;continue;}
  auto Reject=[&](const TCHAR* Reason){Rejected.Add(FString::Printf(TEXT("%s: %s"),*Mesh.Name,Reason));After+=Mesh.Indices.Num()/3;++Kept;};
  const bool RigidPart=!Part->RigidFollow.IsEmpty();
  OkSkinPaletteView Raw{};TArray<float> Matrices;
  if(!RigidPart){const int32 Count=ok_part_skin_palette(Session,Mesh.NativePart,nullptr,nullptr,0);if(Count<=0||Count>65535){Reject(TEXT("palette unavailable"));continue;}
   Matrices.SetNumUninitialized(Count*16);if(ok_part_skin_palette(Session,Mesh.NativePart,&Raw,Matrices.GetData(),Count)!=Count||Raw.n_verts!=V.n_verts||!Raw.weights||!Raw.bone_idx){Reject(TEXT("palette layout differs"));continue;}}
  // Source slot weights and blended reference skin, exactly as the capture forms them.
  auto Slots=[&](int32 I,TMap<int32,double>& Out){Out.Reset();if(RigidPart){Out.Add(0,1.);return true;}double Sum=0;bool Signed=false;
   for(int32 K=0;K<4;++K){const float W=Raw.weights[I*4+K];if(!FMath::IsFinite(W))return false;Sum+=W;Signed|=W<=-1e-6f;}
   if(Signed||Sum<=1e-5)Out.Add(-I-1,1.);else for(int32 K=0;K<4;++K)if(Raw.weights[I*4+K]>=1e-6f)Out.FindOrAdd(Raw.bone_idx[I*4+K])+=Raw.weights[I*4+K];
   double Total=0;for(const auto& S:Out)Total+=S.Value;if(Total<=0)return false;for(auto& S:Out)S.Value/=Total;return true;};
  auto BlendOf=[&](const TMap<int32,double>& W,FMatrix& Out){if(RigidPart){Out=FMatrix::Identity;return true;}Out=FMatrix(ForceInit);
   for(const auto& S:W){FMatrix M;if(!OKExactPaletteMatrix(Raw,Matrices,S.Key,M))return false;for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)Out.M[R][C]+=M.M[R][C]*S.Value;}
   return !Out.ContainsNaN()&&FMath::Abs(Out.Determinant())>=1e-12;};
  TArray<FMatrix> SourceBlend;SourceBlend.SetNum(V.n_verts);TBitArray<> Known(false,V.n_verts);
  auto Source=[&](int32 I,FMatrix& Out){if(!Known[I]){TMap<int32,double> W;if(!Slots(I,W)||!BlendOf(W,SourceBlend[I]))return false;Known[I]=true;}Out=SourceBlend[I];return true;};
  auto Point=[](const float* P){return FVector(-P[0],P[2],P[1])*100.;};auto Direction=[](const float* P){return FVector(-P[0],P[2],P[1]);};
  TArray<FKKRuntimeMeshVertex> Vertices;Vertices.Reserve(Low->Corners.Num());bool Good=true;const TCHAR* Reason=TEXT("");
  const int32 SourceTris=V.n_idx/3;
  for(const auto& C:Low->Corners){
   if(C.Tri>=uint32(SourceTris)||C.UvTri>=uint32(SourceTris)){Good=false;Reason=TEXT("source triangle outside renderer");break;}
   int32 G[3],U[3];for(int32 K=0;K<3;++K){G[K]=V.indices[C.Tri*3+K];U[K]=V.indices[C.UvTri*3+K];}
   TMap<int32,double> Weights;FVector Drawn(0),Normal(0),Tangent(0),Initial(0),Neutral(0);FVector Morph[3]={FVector(0),FVector(0),FVector(0)};TMap<uint32,FKKRuntimeFaceSparse> Face;
   int32 Dominant=0;for(int32 K=1;K<3;++K)if(C.B[K]>C.B[Dominant])Dominant=K;
   for(int32 K=0;K<3;++K){const double W=C.B[K];if(!FMath::IsFinite(W)){Good=false;Reason=TEXT("non-finite barycentric");break;}const int32 I=G[K];FMatrix M;if(!Source(I,M)){Good=false;Reason=TEXT("source skin not invertible");break;}
    TMap<int32,double> S;Slots(I,S);for(const auto& E:S)Weights.FindOrAdd(E.Key)+=E.Value*W;
    const auto& SV=Mesh.Vertices[I];Drawn+=Point(V.positions+I*3)*W;Normal+=Direction(V.normals+I*3)*W;Initial+=FVector(SV.InitialPosition)*W;Neutral+=FVector(SV.Position)*W;
    if(V.rest_tangents)Tangent+=Direction(V.rest_tangents+I*4)*W;
    // Morph deltas were stored through this corner's reference inverse; return them to drawn space first.
    const FVector3f* Deltas[3]={&SV.MouthClosed,&SV.MouthOpen,&SV.BlinkClosed};for(int32 D=0;D<3;++D)if(!Deltas[D]->IsNearlyZero())Morph[D]+=FVector(M.TransformVector(FVector(*Deltas[D])))*W;
    for(const auto& F:SV.Face){auto& Row=Face.FindOrAdd((uint32(F.Control)<<16)|F.Local);if(Row.Native==INDEX_NONE){Row.Control=F.Control;Row.Local=F.Local;Row.Native=F.Native;}Row.Delta+=FVector3f(FVector(M.TransformVector(FVector(F.Delta)))*W);}}
   if(!Good)break;
   // Keep the four strongest slots, as the source contract allows.
   Weights.ValueSort([](double L,double R){return L>R;});TMap<int32,double> Top;double Total=0;for(const auto& E:Weights){if(Top.Num()==4||E.Value<=1e-9)break;Top.Add(E.Key,E.Value);Total+=E.Value;}
   if(Total<=0){Good=false;Reason=TEXT("empty skin");break;}for(auto& E:Top)E.Value/=Total;
   // Outward lift (metres) along the source normal keeps clothing layers ordered.
   if(C.Offset>0&&!RigidPart){const FVector Lift=Normal.GetSafeNormal()*(double(C.Offset)*100.);Drawn+=Lift;Initial+=Lift;}
   FMatrix Blend;if(!BlendOf(Top,Blend)){Good=false;Reason=TEXT("low skin not invertible");break;}const FMatrix Inverse=RigidPart?FMatrix::Identity:Blend.Inverse();
   FKKRuntimeMeshVertex Out;const auto& D=Mesh.Vertices[G[Dominant]];
   Out.InitialPosition=FVector3f(RigidPart?Initial:Drawn);Out.Position=FVector3f(RigidPart?Neutral:FVector(Inverse.TransformPosition(Drawn)));
   Out.Normal=FVector3f((RigidPart?Normal:FVector(Inverse.TransformVector(Normal))).GetSafeNormal());
   Out.Tangent=V.rest_tangents?FVector3f((RigidPart?Tangent:FVector(Inverse.TransformVector(Tangent))).GetSafeNormal()):D.Tangent;Out.TangentSign=D.TangentSign;
   if(Out.Normal.IsNearlyZero())Out.Normal=D.Normal;if(Out.Tangent.IsNearlyZero())Out.Tangent=D.Tangent;
   Out.MouthClosed=FVector3f(FVector(Inverse.TransformVector(Morph[0])));Out.MouthOpen=FVector3f(FVector(Inverse.TransformVector(Morph[1])));Out.BlinkClosed=FVector3f(FVector(Inverse.TransformVector(Morph[2])));
   for(auto& F:Face){F.Value.Delta=FVector3f(FVector(Inverse.TransformVector(FVector(F.Value.Delta))));if(!F.Value.Delta.IsNearlyZero())Out.Face.Add(F.Value);}Out.FaceCatalog=D.FaceCatalog;
   for(int32 Channel=0;Channel<4;++Channel){FVector2f UV(0,0);for(int32 K=0;K<3;++K)UV+=Mesh.Vertices[U[K]].UV[Channel]*C.U[K];Out.UV[Channel]=UV;}
   FLinearColor Color(0,0,0,0);for(int32 K=0;K<3;++K){const FColor& S=Mesh.Vertices[G[K]].Color;Color+=FLinearColor(S.R,S.G,S.B,S.A)*C.B[K];}
   auto Byte=[](float X){return uint8(FMath::Clamp(FMath::RoundToInt(X),0,255));};Out.Color=FColor(Byte(Color.R),Byte(Color.G),Byte(Color.B),Byte(Color.A));
   if(Out.Position.ContainsNaN()||Out.Normal.ContainsNaN()||Out.InitialPosition.ContainsNaN()){Good=false;Reason=TEXT("non-finite low vertex");break;}
   // Same quantization as the source capture: 21845 per slot, three column bones each.
   TArray<int32> Keys;Top.GetKeys(Keys);Keys.Sort();TArray<int32> Quantized;int32 Acc=0,Largest=0;
   for(int32 K=0;K<Keys.Num();++K){Quantized.Add(FMath::RoundToInt(Top[Keys[K]]*21845.));Acc+=Quantized.Last();if(Top[Keys[K]]>Top[Keys[Largest]])Largest=K;}Quantized[Largest]+=21845-Acc;
   if(RigidPart){FMemory::Memcpy(Out.Skin,D.Skin,sizeof(Out.Skin));Out.Influences=D.Influences;}
   else for(int32 K=0;K<Keys.Num()&&Good;++K)if(Quantized[K]>0)for(int32 Axis=0;Axis<3;++Axis){const int32* Bone=ByName.Find(OKPaletteBoneName(Mesh.NativePart,Keys[K],Axis));if(!Bone||*Bone>65535){Good=false;Reason=TEXT("palette bone absent from rig");break;}Out.Skin[Out.Influences++]={uint16(*Bone),uint16(Quantized[K])};}
   if(!Good)break;Vertices.Add(MoveTemp(Out));}
  if(!Good){Reject(Reason);continue;}
  TArray<uint32> Indices;Indices.Reserve(Low->Triangles.Num());for(int32 I=0;I<Low->Triangles.Num();I+=3)Indices.Append({Low->Triangles[I],Low->Triangles[I+2],Low->Triangles[I+1]});
  Mesh.Vertices=MoveTemp(Vertices);Mesh.Indices=MoveTemp(Indices);After+=Mesh.Indices.Num()/3;++Applied;
 }
 Summary=FString::Printf(TEXT("low-poly parts=%d kept=%d triangles %lld -> %lld"),Applied,Kept,Before,After);
 for(const auto& R:Rejected)Summary+=TEXT("; rejected ")+R;
 return Applied>0;
}
