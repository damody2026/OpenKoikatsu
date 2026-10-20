#pragma once
#include "CoreMinimal.h"

// A Unity world*bind palette may contain shear. One Unreal FTransform cannot
// represent it. Three equally weighted rank-one TRS bones reproduce all three
// columns exactly; four source influences therefore require twelve UE ones.
KKUNITYBUNDLE_API FMatrix OKSkinPaletteToUnreal(const float* RowMajorMatrix);
KKUNITYBUNDLE_API FTransform OKSkinPaletteColumn(const FMatrix& Matrix,int32 Axis);
KKUNITYBUNDLE_API FTransform OKSkinPaletteColumnContinuous(const FMatrix& Matrix,int32 Axis,const FQuat& Previous);
struct OkSkinPaletteView;
// Negative slot -(vertex+1) stores that vertex's full signed affine blend.
// UE weights remain positive without dropping or clamping native influences.
// Unweighted vertices use the native fallback placement appended to the palette.
KKUNITYBUNDLE_API bool OKExactPaletteMatrix(const OkSkinPaletteView& Raw,const TArray<float>& Matrices,int32 Slot,FMatrix& Out);
KKUNITYBUNDLE_API bool OKExactPreparedPaletteMatrix(const OkSkinPaletteView& Raw,const TArray<FMatrix>& Matrices,int32 Slot,FMatrix& Out);
KKUNITYBUNDLE_API bool OKHasSignedSkinVertex(const OkSkinPaletteView& Raw,int32 Vertex);
// Ordered private rig slots, including signed/unweighted virtual vertices.
// Converts native row-major matrices once and commits owned output only after
// every binding is validated. No TRS decomposition or source matrix inverse.
KKUNITYBUNDLE_API bool OKPrepareExactSkinPalette(const OkSkinPaletteView& Raw,
 const TArray<float>& Matrices,TConstArrayView<int32> Slots,TArray<FMatrix44f>& Out,FString& Error);
inline FName OKPaletteBoneName(int32 Part,int32 Slot,int32 Axis){return FName(*FString::Printf(TEXT("__ok_palette_%d_%d_%d"),Part,Slot,Axis));}
// Preserve caller renderer settings while obtaining immutable native SMR data.
class KKUNITYBUNDLE_API FKKScopedNativeSkinSource {
 TMap<FString,FString> Previous;
 uint8 PreviousMode=0;
public:
 explicit FKKScopedNativeSkinSource(bool EnableNativeSource=true);
 ~FKKScopedNativeSkinSource();
 FKKScopedNativeSkinSource(const FKKScopedNativeSkinSource&)=delete;
 FKKScopedNativeSkinSource& operator=(const FKKScopedNativeSkinSource&)=delete;
};
