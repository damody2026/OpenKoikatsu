#pragma once
#include "KKRuntimeMesh.h"
struct FKKRuntimeReductionPartStats {
 int32 NativePart=INDEX_NONE,Group=0;uint32 SourceTriangles=0,TargetTriangles=0,Triangles=0;
 FString RetentionReason;
};
struct FKKRuntimeMeshReductionResult {
 TArray<FKKRuntimeMeshPart> Meshes;TArray<FKKRuntimeReductionPartStats> Parts;
 FString Error;double PrepareMs=0;bool WorkerThread=false,BudgetsMet=false,SourceGeometryPreserved=false;
};
// Owned worker data only. Original endpoint attributes, no welding, no editor assets.
// A quality-preserving retention is explicitly reported and can exceed the target.
// ClassRules (CrowdRoom low-poly rules): face, body and socks/stockings are not
// reduced; the first budget is all hair, the second clothes plus accessories.
KKRUNTIMEPOSE_API TFuture<FKKRuntimeMeshReductionResult> OKReduceRuntimeMeshesAsync(
 TArray<FKKRuntimeMeshPart> Meshes,uint32 BodyTriangles=15000,
 uint32 ClothesTriangles=15000,uint32 HairAccessoryTriangles=30000,
 bool PreserveSourceGeometry=false,bool ClassRules=false);
