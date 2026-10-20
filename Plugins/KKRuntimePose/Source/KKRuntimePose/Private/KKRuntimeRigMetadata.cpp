#include "KKRuntimePoseNative.h"
#include "KKRuntimeNativeSession.h"
#include "KKRuntimePoseBankTypes.h"
#include "KKRuntimeSharedMaterial.h"
#include "KKRuntimeMaterial.h"
#include "KKRuntimePoseMath.h"

static bool DescribeRuntimeRig(FKKRuntimeSnapshotResult& R,const TArray<uint8>& Curves,FKKRuntimeNativeSession& Session){
 auto Fail=[&](const FString& Why){R.Error=Why;R.Bones.Reset();R.Parts.Reset();return false;};
 const int32 Count=Session.SkelBoneCount();if(Count<=0||Count>=65536)return Fail(TEXT("Runtime native bone count invalid"));
 TSet<FName> Names;Names.Add(TEXT("__ok_scene"));R.Bones.Add({TEXT("__ok_scene"),INDEX_NONE,FTransform::Identity});
 for(int32 B=0;B<Count;++B){OkBoneView V{};if(!Session.SkelBoneAt(B,V)||!V.name||V.parent<INDEX_NONE||V.parent>=B)return Fail(TEXT("Runtime native hierarchy is not topological"));
  const FName Name(UTF8_TO_TCHAR(V.name));if(Name.IsNone()||Names.Contains(Name)||Name.ToString().StartsWith(TEXT("__ok_")))return Fail(TEXT("Runtime native bone identity is ambiguous"));Names.Add(Name);
  const FTransform Ref=OKUnityLocalToUnreal(V.rest_pos,V.rest_rot,V.rest_scl);if(Ref.ContainsNaN())return Fail(TEXT("Runtime native reference is nonfinite"));R.Bones.Add({Name,V.parent<0?0:V.parent+1,Ref});
 }
 // Capture rigid placement before evaluating a frame; later animation poses
 // must not become the accessory's inverse bind.
 TMap<int32,FKKRuntimePalettePart> Rigid;
 for(int32 P=0;P<Session.PartCount();++P){OkMeshView V{};if(!Session.PartAt(P,V))return Fail(TEXT("Runtime renderer metadata missing"));
  if(V.rigid_follow_bone&&V.rigid_follow_bone[0]){float World[16];FKKRuntimePalettePart Part;Part.NativePart=P;Part.RigidFollow=UTF8_TO_TCHAR(V.rigid_follow_bone);if(!Session.BoneWorldMatrix(Part.RigidFollow,World))return Fail(TEXT("Runtime neutral attachment bone missing"));Part.RigidBindWorld=OKSkinPaletteToUnreal(World);Part.Slots={0};Rigid.Add(P,MoveTemp(Part));}
 }
 if(Curves.IsEmpty()||!Session.InstallSharedClip(Curves)||!Session.ApplyAnimFrame(0,0))return Fail(TEXT("Runtime rig initialization frame rejected"));
 for(int32 P=0;P<Session.PartCount();++P){OkMeshView V{};if(!Session.PartAt(P,V))return Fail(TEXT("Runtime initialized renderer missing"));
  // The tears (cf_O_namida_S/M/L) are hidden by default like in the source (tearsLv 0) but built, so a scene can show
  // them per character (OKSetCrowdRuntimeFaceLook); every other hidden renderer stays out.
  if(!V.render_visible&&!OKRuntimeIsTearRenderer(V.name))continue;
  // A tear whose material the runtime cannot draw stays out (logged) instead of failing the whole character.
  if(!V.render_visible){FKKRuntimeMaterialPart M;M.Name=UTF8_TO_TCHAR(V.name);M.Blend=V.blend?UTF8_TO_TCHAR(V.blend):TEXT("");M.ShaderKind=V.shader_kind;M.Unlit=V.unlit;M.EyeDraw=V.eye_draw;M.ItemDraw=V.item_draw;
   M.SourceMetadataJson=Session.PartMaterialMetadata(P);
   if(OKRuntimeSharedMaterialRecipe(M)==EKKRuntimeMaterialRecipe::None){UE_LOG(LogTemp,Display,TEXT("Runtime tear renderer %s left out: material unsupported (kind=%d blend=%s) %s"),*M.Name,V.shader_kind,*M.Blend,*M.SourceMetadataJson.Left(300));continue;}}
  // The native snapshot includes optional genital geometry for editing. Its
  // presence (or an uncensor asset GUID) is not a display request. Match the
  // preview's default-hidden genital policy before building either CPU or GPU
  // scene meshes. Keep the snapshot untouched for future explicit toggles.
  const FString RendererName=V.name?UTF8_TO_TCHAR(V.name):TEXT("");
  if(RendererName.Equals(TEXT("o_dankon"),ESearchCase::IgnoreCase)||RendererName.Equals(TEXT("o_dan_f"),ESearchCase::IgnoreCase)||RendererName.Equals(TEXT("cm_o_dan00"),ESearchCase::IgnoreCase)||RendererName.Equals(TEXT("cm_o_dan_f"),ESearchCase::IgnoreCase)){
   UE_LOG(LogTemp,Display,TEXT("APNG runtime default-hidden genital renderer: part=%d name=%s"),P,*RendererName);continue;
  }
  FKKRuntimePalettePart Part;Part.NativePart=P;OkSkinPaletteView Raw{};TArray<float> Matrices;
  if(Session.SkinPalette(P,Raw,Matrices)>0){TSet<int32> Slots;if(!Raw.weights||!Raw.bone_idx||Raw.n_verts<=0)return Fail(TEXT("Runtime renderer weights missing"));
   for(int32 Vertex=0;Vertex<Raw.n_verts;++Vertex){double Sum=0;bool Signed=false;for(int32 K=0;K<4;++K){const float W=Raw.weights[Vertex*4+K];if(!FMath::IsFinite(W))return Fail(TEXT("Runtime renderer weights nonfinite"));Sum+=W;Signed|=W<=-1e-6f;}
    if(Signed||Sum<=1e-5)Slots.Add(-Vertex-1);else for(int32 K=0;K<4;++K)if(Raw.weights[Vertex*4+K]>=1e-6f)Slots.Add(Raw.bone_idx[Vertex*4+K]);
   }Part.Slots=Slots.Array();Part.Slots.Sort();
  }else if(const auto* Placement=Rigid.Find(P))Part=*Placement;
  else {if(V.n_verts<=0)continue;return Fail(FString::Printf(TEXT("Visible runtime renderer lacks skin or rigid binding: part=%d name=%s role=%s parent=%s"),P,V.name?UTF8_TO_TCHAR(V.name):TEXT(""),V.role?UTF8_TO_TCHAR(V.role):TEXT(""),V.parent_bone?UTF8_TO_TCHAR(V.parent_bone):TEXT("")));}
  if(uint64(R.Bones.Num())+uint64(Part.Slots.Num())*3>65535)return Fail(TEXT("Runtime exact rig exceeds bone index budget before low-mesh compaction"));
  for(int32 Slot:Part.Slots)for(int32 Axis=0;Axis<3;++Axis){const FName Name=OKPaletteBoneName(P,Slot,Axis);if(Names.Contains(Name))return Fail(TEXT("Runtime palette bone identity repeated"));Names.Add(Name);R.Bones.Add({Name,0,FTransform::Identity});}
  R.Parts.Add(MoveTemp(Part));
 }
 if(R.Parts.IsEmpty())return Fail(TEXT("Runtime card has no visible bound renderers"));return true;
}

bool OKDescribeRuntimeRig(FKKRuntimeSnapshotResult& R,const TArray<uint8>& Curves){FKKRuntimeNativeSession Session;if(!Session.OpenAnimationSnapshot(R.Snapshot)){R.Error=TEXT("Runtime rig snapshot rejected");return false;}return DescribeRuntimeRig(R,Curves,Session);}
bool OKDescribeRuntimeRigFromSession(FKKRuntimeSnapshotResult& R,const TArray<uint8>& Curves,OkSession* Native){FKKRuntimeNativeSession Session(Native);return DescribeRuntimeRig(R,Curves,Session);}
