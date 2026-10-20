#include "KKRuntimeRenderRig.h"
#include "KKRuntimePoseMath.h"

bool OKCompactRuntimeRenderRig(TConstArrayView<FKKRuntimePoseBone> Source,
 TConstArrayView<FKKRuntimePalettePart> Parts,TConstArrayView<FName> Extra,
 TArray<FKKRuntimePoseBone>& Out,TArray<int32>& OldToNew,FString& Error){
 auto Fail=[&](const TCHAR* Why){Error=Why;return false;};
 if(Source.IsEmpty()||Source.Num()>65536||Source[0].Name!=TEXT("__ok_scene")||Source[0].Parent!=INDEX_NONE||Parts.IsEmpty())return Fail(TEXT("Invalid runtime render rig root or renderer set"));
 TMap<FName,int32> ByName;
 for(int32 I=0;I<Source.Num();++I){const auto& B=Source[I];
  if(B.Name.IsNone()||ByName.Contains(B.Name)||B.Parent<INDEX_NONE||B.Parent>=I||B.Reference.ContainsNaN())return Fail(TEXT("Invalid runtime render rig hierarchy or identity"));
  ByName.Add(B.Name,I);
 }
 TArray<uint8> Keep;Keep.SetNumZeroed(Source.Num());Keep[0]=1;
 auto Require=[&](FName Name){const auto* Found=ByName.Find(Name);if(!Found)return false;int32 I=*Found;while(I!=INDEX_NONE&&!Keep[I]){Keep[I]=1;I=Source[I].Parent;}return true;};
 TSet<int32> NativeParts;
 for(const auto& P:Parts){if(P.NativePart<0||NativeParts.Contains(P.NativePart)||P.Slots.IsEmpty()||P.RigidBindWorld.ContainsNaN())return Fail(TEXT("Invalid runtime renderer palette identity"));NativeParts.Add(P.NativePart);TSet<int32> Slots;
  for(int32 Slot:P.Slots){if(Slots.Contains(Slot))return Fail(TEXT("Duplicate runtime renderer palette slot"));Slots.Add(Slot);
   for(int32 Axis=0;Axis<3;++Axis)if(!Require(OKPaletteBoneName(P.NativePart,Slot,Axis)))return Fail(TEXT("Runtime renderer palette column absent from pose rig"));
  }
 }
 for(FName Name:Extra)if(!Require(Name))return Fail(TEXT("Required runtime socket bone absent from pose rig"));
 TArray<int32> Map;Map.Init(INDEX_NONE,Source.Num());TArray<FKKRuntimePoseBone> Bones;Bones.Reserve(Source.Num());
 for(int32 I=0;I<Source.Num();++I)if(Keep[I]){auto B=Source[I];if(B.Parent!=INDEX_NONE){if(Map[B.Parent]==INDEX_NONE)return Fail(TEXT("Runtime render rig ancestor closure broken"));B.Parent=Map[B.Parent];}Map[I]=Bones.Num();Bones.Add(MoveTemp(B));}
 Bones.Shrink();Out=MoveTemp(Bones);OldToNew=MoveTemp(Map);Error.Reset();return true;
}
