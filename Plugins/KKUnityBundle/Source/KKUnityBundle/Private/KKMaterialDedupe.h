#pragma once
#include "CoreMinimal.h"

class UMaterial;
class UMaterialInterface;

// Runtime material graphs that come out identical (the same expressions, constants, textures and settings) share
// one finalized UMaterial instead of each paying PostEditChange (on by default; OK_MATERIAL_DEDUPE=0 turns it off). A body,
// face or hair part is rebuilt with the same textures on an outfit switch or when a card is opened again.
//
// Call OKFindEqualMaterial on the built graph before finalizing it: a hit is the material to use instead (the new
// graph is dropped). On a miss finalize as usual, then OKRememberMaterial(Key, Mat). Extra names whatever the
// caller changes on the material after finalizing (it is part of the key).
UMaterial* OKFindEqualMaterial(UMaterial* Mat, const FString& Extra, FString& OutKey);
void OKRememberMaterial(const FString& Key, UMaterial* Mat);
// The same for a graph whose scalar and vector parameters a part may set differently (an item's colours exposed as
// parameters): their default values are not part of the key, so graphs differing only there share one material
// (one shader compile); a hit is that material, or a MID of it carrying this graph's values (owned by `Outer`).
// On a miss finalize and OKRememberMaterial(Key, Mat) as usual.
UMaterialInterface* OKFindEqualMaterialGraph(UMaterial* Mat, const FString& Extra, FString& OutKey, UObject* Outer);

struct FOKMaterialDedupeStats { int32 Hits = 0; int32 Misses = 0; double SignatureSeconds = 0.0; };
FOKMaterialDedupeStats OKMaterialDedupeStats();
void OKResetMaterialDedupeStats();
