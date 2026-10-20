#pragma once
#include "CoreMinimal.h"
#include "ok_extract.h"

const OkRustBindings& OKRuntimeNativeBindings();
class FKKRuntimeNativeSession {
 OkSession* Handle=nullptr;bool Owned=true;
public:
 FKKRuntimeNativeSession()=default;
 explicit FKKRuntimeNativeSession(OkSession* Borrowed):Handle(Borrowed),Owned(false){}
 FKKRuntimeNativeSession(const FKKRuntimeNativeSession&)=delete;
 ~FKKRuntimeNativeSession(){if(Handle&&Owned)OKRuntimeNativeBindings().session_free(Handle);}
 bool OpenAnimationSnapshot(const TArray<uint8>& Bytes){if(Handle||Bytes.IsEmpty())return false;Handle=ok_session_open_animation_snapshot(Bytes.GetData(),Bytes.Num());return Handle!=nullptr;}
 bool InstallSharedClip(const TArray<uint8>& Bytes){return Handle&&!Bytes.IsEmpty()&&ok_session_prepare_shared_clip(Handle,Bytes.GetData(),Bytes.Num())==0;}
 float AnimDuration(){return Handle?OKRuntimeNativeBindings().anim_duration(Handle):0;}
 int32 SkelBoneCount(){return Handle?OKRuntimeNativeBindings().skel_bone_count(Handle):0;}
 int32 PartCount(){return Handle?OKRuntimeNativeBindings().part_count(Handle):0;}
 bool PartAt(int32 P,OkMeshView& V){return Handle&&OKRuntimeNativeBindings().part_at(Handle,P,&V)==0;}
 // The renderer's source material description (JSON, schema 1); empty when unavailable.
 FString PartMaterialMetadata(int32 P){if(!Handle)return FString();const int32 Size=ok_part_material_metadata(Handle,P,nullptr,0);if(Size<=0||Size>1024*1024)return FString();
  TArray<uint8> Json;Json.SetNumZeroed(Size+1);return ok_part_material_metadata(Handle,P,Json.GetData(),Size)==Size?FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Json.GetData()))):FString();}
 bool SkelBoneAt(int32 B,OkBoneView& V){return Handle&&OKRuntimeNativeBindings().skel_bone_at(Handle,B,&V)==0;}
 bool ApplyAnimFrame(float T,float Delta){return Handle&&OKRuntimeNativeBindings().apply_anim_frame(Handle,T,Delta)==0;}
 bool ApplyContactFrame(float T,float Delta,const TArray<uint8>& Json){return Handle&&!Json.IsEmpty()&&ok_session_apply_contact_frame(Handle,T,Delta,Json.GetData(),Json.Num())==0;}
 bool BoneWorldMatrix(const FString& Name,float* Matrix){return Handle&&ok_bone_world_matrix(Handle,TCHAR_TO_UTF8(*Name),Matrix)==0;}
 int32 SkinPalette(int32 Part,OkSkinPaletteView& Raw,TArray<float>& Matrices){
  if(!Handle)return -1;
  // The native size query performs the same palette construction and vertex
  // validation as the copy. Reuse known capacity, but retry size/copy on failure
  // so changing palettes never rely on a fixed bone-count assumption.
  const int32 Capacity=Matrices.Num()/16;
  if(Capacity>0&&Capacity<=65535){OkSkinPaletteView View{};const int32 Copied=ok_part_skin_palette(Handle,Part,&View,Matrices.GetData(),Capacity);if(Copied>0&&Copied<=Capacity){Matrices.SetNumUninitialized(Copied*16,EAllowShrinking::No);Raw=View;return Copied;}}
  const int32 Count=ok_part_skin_palette(Handle,Part,nullptr,nullptr,0);if(Count<=0||Count>65535){Raw={};return -1;}
  Matrices.SetNumUninitialized(Count*16,EAllowShrinking::No);OkSkinPaletteView View{};
  if(ok_part_skin_palette(Handle,Part,&View,Matrices.GetData(),Count)!=Count){Raw={};return -1;}Raw=View;return Count;
 }
};
