#include "KKRuntimeGpuRig.h"
#include "KKRuntimeNativeSession.h"
#include "Async/Async.h"
#include "Containers/StringConv.h"

namespace {
struct FOKRigReader {
 const uint8* P=nullptr;int64 N=0,At=0;bool Bad=false;
 uint32 U(){if(At+4>N){Bad=true;return 0;}uint32 V;FMemory::Memcpy(&V,P+At,4);At+=4;return V;}
 int32 I(){return int32(U());}
 float F(){const uint32 V=U();float R;FMemory::Memcpy(&R,&V,4);return R;}
 FString Name(){const uint32 L=U();if(Bad||At+L>N){Bad=true;return FString();}FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(P+At),L);At+=L;while(At%4)++At;return FString(Conv.Length(),Conv.Get());}
};
// Three rows of a Unity affine matrix (row-major, translation in column 3) to UE column form (KKRuntimePoseMath.h
// OKSkinPaletteToUnreal transposed): U[r][c] = s_r s_c X[m_r][m_c], U[r][3] = 100 s_r X[m_r][3].
void OKUnityToUeColumn(const float* X,FVector4f Out[3]){
 static const int32 M[3]={0,2,1};static const float S[3]={-1.f,1.f,1.f};
 for(int32 R=0;R<3;++R){for(int32 C=0;C<3;++C)Out[R][C]=S[R]*S[C]*X[M[R]*4+M[C]];Out[R][3]=100.f*S[R]*X[M[R]*4+3];}
}
FMatrix44d OKColumnToMatrix(const FVector4f R[3]){FMatrix44d M=FMatrix44d::Identity;for(int32 A=0;A<3;++A)for(int32 B=0;B<4;++B)M.M[A][B]=R[A][B];return M;}
void OKMatrixToColumn(const FMatrix44d& M,FVector4f R[3]){for(int32 A=0;A<3;++A)for(int32 B=0;B<4;++B)R[A][B]=float(M.M[A][B]);}
// Column-form product (the matrices act on column vectors: A then applied after B). FMatrix's operator* composes the
// other way (row vectors), so multiply explicitly.
FMatrix44d OKColumnMul(const FMatrix44d& A,const FMatrix44d& B){FMatrix44d O;for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C){double V=0;for(int32 K=0;K<4;++K)V+=A.M[R][K]*B.M[K][C];O.M[R][C]=V;}return O;}
struct FOKRigPaletteTerm {int32 Node=INDEX_NONE;float C[12];};
}

bool OKParseRuntimeGpuClip(const TArray<uint8>& Bytes,FKKRuntimeGpuClip& Out){
 Out={};FOKRigReader R{Bytes.GetData(),Bytes.Num()};
 if(R.U()!=0x43474B4F||R.U()!=1)return false; // "OKGC"
 const uint32 Channels=R.U();Out.Frames=R.U();Out.Rate=R.F();Out.Duration=R.F();Out.Flags=R.U();
 if(R.Bad||Channels>65536||!Out.Frames||Out.Frames>100000||!(Out.Rate>0))return false;
 for(uint32 C=0;C<Channels;++C){Out.Mask.Add(R.U());Out.Bones.Add(R.Name());}
 const uint64 Floats=uint64(Channels)*Out.Frames*10;if(R.Bad||R.At+int64(Floats*4)!=R.N)return false;
 Out.Samples.Reserve(int32(uint64(Channels)*Out.Frames*3));
 for(uint64 K=0;K<uint64(Channels)*Out.Frames;++K){const float Px=R.F(),Py=R.F(),Pz=R.F(),Qx=R.F(),Qy=R.F(),Qz=R.F(),Qw=R.F(),Sx=R.F(),Sy=R.F(),Sz=R.F();
  Out.Samples.Add(FVector4f(Px,Py,Pz,0));Out.Samples.Add(FVector4f(Qx,Qy,Qz,Qw));Out.Samples.Add(FVector4f(Sx,Sy,Sz,0));}
 return !R.Bad;
}

TFuture<FKKRuntimeGpuRigResult> OKPrepareRuntimeGpuRigAsync(TSharedPtr<const FKKRuntimePoseRequest,ESPMode::ThreadSafe> Recipe,TArray<TArray<uint8>> Clips){
 return Async(EAsyncExecution::ThreadPool,[Recipe,Clips=MoveTemp(Clips)](){
  const double Start=FPlatformTime::Seconds();FKKRuntimeGpuRigResult Out;
  auto Fail=[&](const FString& Why){Out.Error=Why;Out.PrepareMs=(FPlatformTime::Seconds()-Start)*1000;return MoveTemp(Out);};
  if(!Recipe||Recipe->Snapshot.IsEmpty()||Recipe->Bones.IsEmpty()||Clips.IsEmpty())return Fail(TEXT("Invalid GPU rig request"));
  FKKRuntimeNativeSession S;if(!S.OpenAnimationSnapshot(Recipe->Snapshot))return Fail(TEXT("GPU rig snapshot rejected"));
  TArray<uint8> Bytes;if(!S.GpuRig(Clips[0],Bytes))return Fail(TEXT("GPU rig export failed"));
  // "OKGR" (export.rs FlatRig::to_bytes).
  FOKRigReader R{Bytes.GetData(),Bytes.Num()};
  const uint32 Magic=R.U(),Version=R.U();if(Magic!=0x52474B4F||(Version!=1&&Version!=2))return Fail(TEXT("GPU rig export format"));
  const uint32 Bones=R.U(),Flags=R.U(),Always=R.U(),Forearms=R.U(),Hands=R.U(),Pv=R.U(),Abm=R.U(),NamedFirst=R.U(),NamedLast=R.U(),Parts=R.U();
  if(R.Bad||!Bones||Bones>65536||NamedFirst+NamedLast!=66||Parts>4096)return Fail(TEXT("GPU rig export header"));
  Out.Female=(Flags&1)!=0;
  for(uint32 B=0;B<Bones;++B){Out.Parent.Add(R.I());const float Px=R.F(),Py=R.F(),Pz=R.F();Out.Pos.Add(FVector3f(Px,Py,Pz));const float Qx=R.F(),Qy=R.F(),Qz=R.F(),Qw=R.F();Out.Rot.Add(FVector4f(Qx,Qy,Qz,Qw));const float Sx=R.F(),Sy=R.F(),Sz=R.F();Out.Scale.Add(FVector3f(Sx,Sy,Sz));}
  for(uint32 K=0;K<Always;++K){const int32 Bone=R.I(),Axis=R.I();Out.Always.Add(FIntPoint(Bone,Axis));}
  for(uint32 K=0;K<Forearms;++K)Out.Forearms.Add(R.I());
  for(uint32 K=0;K<Hands;++K)Out.Hands.Add(R.I());
  for(uint32 K=0;K<Pv;++K){const int32 A=R.I(),C=R.I();Out.Pv.Add(FIntPoint(A,C));}
  for(uint32 K=0;K<Abm;++K){FKKRuntimeGpuRigAbm Op;Op.Bone=R.I();const float Qx=R.F(),Qy=R.F(),Qz=R.F(),Qw=R.F();Op.Rot=FVector4f(Qx,Qy,Qz,Qw);Op.Length=R.F();const float Px=R.F(),Py=R.F(),Pz=R.F();Op.Pos=FVector3f(Px,Py,Pz);const float Sx=R.F(),Sy=R.F(),Sz=R.F();Op.Scale=FVector3f(Sx,Sy,Sz);Out.Abm.Add(Op);}
  for(uint32 K=0;K<NamedFirst+NamedLast;++K)Out.Named.Add(R.I());
  TArray<TArray<FOKRigPaletteTerm>> Palettes;Palettes.SetNum(Parts);
  for(uint32 P=0;P<Parts;++P){const uint32 Count=R.U();if(R.Bad||Count>65536)return Fail(TEXT("GPU rig export palettes"));
   for(uint32 J=0;J<Count;++J){FOKRigPaletteTerm T;T.Node=R.I();const float Error=R.F();if(Error>1e-4f)++Out.LoosePalettes;for(int32 K=0;K<12;++K)T.C[K]=R.F();Palettes[P].Add(T);}}
  for(uint32 B=0;B<Bones;++B)Out.Names.Add(R.Name());
  if(Version>=2){
   // The physics section (export.rs Physics::write), into the shader's rows.
   auto& Ph=Out.Physics;const uint32 Nodes=R.U(),Chains=R.U(),Particles=R.U(),Colliders=R.U(),Follow=R.U(),Sets=R.U();
   if(R.Bad||Nodes>65536||Chains>65536||Particles>262144||Colliders>1048576||Follow>65536||Sets>4096)return Fail(TEXT("GPU rig physics header"));
   for(uint32 K=0;K<Sets;++K){const uint32 Clothes=R.U(),NF=R.U(),NC=R.U(),CF=R.U(),CC=R.U();Ph.Sets.Add(FUintVector4(Clothes,NF,NC,CF));Ph.Sets.Add(FUintVector4(CC,0,0,0));}
   auto Bits=[](uint32 V){return reinterpret_cast<const float&>(V);};
   for(uint32 K=0;K<Nodes;++K){const uint32 Kind=R.U(),Parent=R.U(),Alias=R.U(),LocalFrom=R.U();float V[26];for(float& X:V)X=R.F();
    Ph.Nodes.Add(FVector4f(V[0],V[1],V[2],Bits(Kind)));Ph.Nodes.Add(FVector4f(V[3],V[4],V[5],V[6]));Ph.Nodes.Add(FVector4f(V[7],V[8],V[9],Bits(Parent)));
    for(int32 Row=0;Row<3;++Row)Ph.Nodes.Add(FVector4f(V[10+Row*4],V[11+Row*4],V[12+Row*4],V[13+Row*4]));
    Ph.Nodes.Add(FVector4f(V[22],V[23],V[24],V[25]));Ph.Nodes.Add(FVector4f(Bits(Alias),Bits(LocalFrom),0,0));}
   for(uint32 K=0;K<Chains;++K){const uint32 Root=R.U(),Component=R.U(),ChainFlags=R.U(),PF=R.U(),PC=R.U(),CF=R.U(),CC=R.U(),Steps=R.U();float V[14];for(float& X:V)X=R.F();
    Ph.Chains.Add(FUintVector4(Root,Component,ChainFlags,Steps));Ph.Chains.Add(FUintVector4(PF,PC,CF,CC));
    Ph.ChainValues.Add(FVector4f(V[0],V[1],V[2],V[9]));Ph.ChainValues.Add(FVector4f(V[3],V[4],V[5],0));Ph.ChainValues.Add(FVector4f(V[6],V[7],V[8],0));Ph.ChainValues.Add(FVector4f(V[10],V[11],V[12],V[13]));}
   for(uint32 K=0;K<Particles;++K){const uint32 Node=R.U(),Parent=R.U(),ParticleFlags=R.U();float V[12];for(float& X:V)X=R.F();
    const uint32 Ver02Flags=R.U();float W[16];for(float& X:W)X=R.F();
    Ph.Particles.Add(FVector4f(Bits(Node),Bits(Parent),Bits(ParticleFlags),V[6]));Ph.Particles.Add(FVector4f(V[0],V[1],V[2],V[7]));
    Ph.Particles.Add(FVector4f(V[3],V[4],V[5],V[8]));Ph.Particles.Add(FVector4f(V[9],V[10],V[11],W[15]));
    // Ver02: keep min/max, next length, move box, crush, initial scale (export.rs order).
    Ph.Particles.Add(FVector4f(W[0],W[1],W[2],Bits(Ver02Flags)));Ph.Particles.Add(FVector4f(W[3],W[4],W[5],W[9]));
    Ph.Particles.Add(FVector4f(W[6],W[7],W[8],W[10]));Ph.Particles.Add(FVector4f(W[11],W[12],W[13],W[14]));}
   for(uint32 K=0;K<Colliders;++K){const uint32 Ref=R.U(),Direction=R.U(),Inside=R.U();float V[5];for(float& X:V)X=R.F();
    Ph.Colliders.Add(FVector4f(V[0],V[1],V[2],V[3]));Ph.Colliders.Add(FVector4f(V[4],Bits(Direction),Bits(Inside),Bits(Ref)));}
   for(uint32 K=0;K<Follow;++K)Ph.Follow.Add(R.U());
  }
  if(R.Bad||R.At!=R.N)return Fail(TEXT("GPU rig export length"));
  for(uint32 B=0;B<Bones;++B)if(Out.Parent[B]>=int32(B))return Fail(TEXT("GPU rig export bone order"));
  TMap<FString,int32> ByName;for(int32 B=0;B<Out.Names.Num();++B)ByName.FindOrAdd(Out.Names[B],B);
  const int32* HeadIndex=ByName.Find(TEXT("cf_s_head"));
  // A bone the rig lacks (a head bone): it rides on cf_s_head as long as the head does not move on its own (no physics);
  // its constant is read off the session's current pose.
  auto UnityWorld=[&](const FString& Name,FMatrix44d& Out)->bool{float W[16];if(!S.BoneWorldMatrix(Name,W))return false;FVector4f Col[3];OKUnityToUeColumn(W,Col);Out=OKColumnToMatrix(Col);return true;};
  auto Riding=[&](const FMatrix44d& Target,FKKRuntimeGpuRigTerm& T)->bool{FMatrix44d Head;if(!HeadIndex||!UnityWorld(TEXT("cf_s_head"),Head))return false;T.Node=*HeadIndex;OKMatrixToColumn(OKColumnMul(Head.Inverse(),Target),T.C);++Out.EmpiricalTerms;return true;};
  auto Identity=[](FKKRuntimeGpuRigTerm& T){T.Node=INDEX_NONE;T.C[0]=FVector4f(1,0,0,0);T.C[1]=FVector4f(0,1,0,0);T.C[2]=FVector4f(0,0,1,0);};
  auto PaletteTerm=[&](int32 Part,int32 J,float Weight,FKKRuntimeGpuRigTerm& T)->bool{if(!Palettes.IsValidIndex(Part)||!Palettes[Part].IsValidIndex(J))return false;const auto& P=Palettes[Part][J];T.Node=P.Node;T.Weight=Weight;OKUnityToUeColumn(P.C,T.C);return true;};
  TMap<int32,const FKKRuntimePalettePart*> PartOf;for(const auto& Part:Recipe->Parts)PartOf.Add(Part.NativePart,&Part);
  TMap<int32,TPair<OkSkinPaletteView,TArray<float>>> Raw;
  Out.BoneTerms.SetNum(Recipe->Bones.Num());
  for(int32 B=0;B<Recipe->Bones.Num();++B){
   const FString Name=Recipe->Bones[B].Name.ToString();auto& Terms=Out.BoneTerms[B];
   if(Name.StartsWith(TEXT("__ok_palette_"))){
    TArray<FString> Tokens;Name.RightChop(13).ParseIntoArray(Tokens,TEXT("_"));if(Tokens.Num()!=3)return Fail(TEXT("GPU rig palette bone name: ")+Name);
    const int32 Part=FCString::Atoi(*Tokens[0]),Slot=FCString::Atoi(*Tokens[1]);const auto* Found=PartOf.Find(Part);if(!Found)return Fail(TEXT("GPU rig palette part missing: ")+Name);
    const auto& P=**Found;
    if(!P.RigidFollow.IsEmpty()){
     // Rigid: InverseBind x the follow bone's world (KKRuntimePoseBank.cpp), in column form UE(W) x InverseBind^T.
     const FMatrix InverseBind=P.RigidBindWorld.Inverse();FMatrix44d Bind=FMatrix44d::Identity;for(int32 A=0;A<4;++A)for(int32 C=0;C<4;++C)Bind.M[A][C]=InverseBind.M[C][A];
     FKKRuntimeGpuRigTerm T;
     if(const int32* Node=ByName.Find(P.RigidFollow)){T.Node=*Node;OKMatrixToColumn(Bind,T.C);}
     else{FMatrix44d Follow;if(!UnityWorld(P.RigidFollow,Follow)||!Riding(OKColumnMul(Follow,Bind),T))return Fail(TEXT("GPU rig rigid follow bone missing: ")+P.RigidFollow);}
     Terms.Add(T);continue;
    }
    if(Slot>=0){FKKRuntimeGpuRigTerm T;if(!PaletteTerm(Part,Slot,1.f,T))return Fail(TEXT("GPU rig palette slot missing: ")+Name);Terms.Add(T);continue;}
    // A signed vertex: its four palette matrices blended by its normalized weights (OKExactPaletteMatrix).
    auto& Entry=Raw.FindOrAdd(Part);if(Entry.Value.IsEmpty()&&S.SkinPalette(Part,Entry.Key,Entry.Value)<=0)return Fail(TEXT("GPU rig native palette missing"));
    const auto& View=Entry.Key;const int64 Vertex=-int64(Slot)-1;if(Vertex>=View.n_verts||!View.weights||!View.bone_idx)return Fail(TEXT("GPU rig signed vertex outside the part"));
    double Total=0;for(int32 K=0;K<4;++K)Total+=View.weights[Vertex*4+K];
    if(Total<=1e-5){FKKRuntimeGpuRigTerm T;if(!PaletteTerm(Part,Palettes[Part].Num()-1,1.f,T))return Fail(TEXT("GPU rig fallback palette missing"));Terms.Add(T);continue;}
    double Sum=0;for(int32 K=0;K<4;++K){const float W=View.weights[Vertex*4+K];if(FMath::Abs(W)>=1e-6f)Sum+=W;}
    for(int32 K=0;K<4;++K){const float W=View.weights[Vertex*4+K];if(FMath::Abs(W)<1e-6f)continue;FKKRuntimeGpuRigTerm T;if(!PaletteTerm(Part,View.bone_idx[Vertex*4+K],float(W/Sum),T))return Fail(TEXT("GPU rig signed palette missing"));Terms.Add(T);}
    continue;
   }
   FKKRuntimeGpuRigTerm T;
   if(B==0&&Name==TEXT("__ok_scene")){Identity(T);Terms.Add(T);continue;}
   if(const int32* Node=ByName.Find(Name)){Identity(T);T.Node=*Node;Terms.Add(T);continue;}
   FMatrix44d World;if(UnityWorld(Name,World)&&Riding(World,T)){Terms.Add(T);continue;}
   Identity(T);Terms.Add(T);
  }
  for(const auto& Clip:Clips){TArray<uint8> Channels;S.GpuClip(Clip,Channels);Out.Clips.Add(MoveTemp(Channels));}
  Out.PrepareMs=(FPlatformTime::Seconds()-Start)*1000;return Out;
 });
}
