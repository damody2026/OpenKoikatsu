#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/** Studio's sky patterns (StudioSky list): number and name, from the OK base library's studio/sky.json. */
struct FOKStudioSkyPattern
{
	int32 No = 0;
	FString Name;
};

/** The sky patterns of the OK base library's StudioSky list (empty when it has no studio/sky.json); the maps' own
 * skies are not among them. */
const TArray<FOKStudioSkyPattern>& OKStudioSkyPatterns();

/**
 * Whether map `Map` has its own sky (MapSkyInfo): Studio then always draws a sky, its controller's profile for the
 * time of day (Map.RangeTimeZone: `SunType` when the map's SunLightInfo has it, else its first), and ignores the
 * scene's sky setting. `OutPattern`: that time's pattern number (-1: none of the library's).
 */
bool OKStudioMapSky(int32 Map, int32 SunType, int32& OutPattern);

/**
 * The material drawing sky pattern `No` on a dome around the view: Studio's skybox shader (Funly Sky Studio
 * "3D Standard", translated by Tools/generate_studio_sky.lua) with the pattern's values and textures. The program
 * variant is the one sharing the most shader keywords with the pattern, as Unity picks it. Null (and `OutError`)
 * when the pattern or the library data is missing; built once per pattern.
 */
UMaterialInterface* OKStudioSkyMaterial(int32 No, FString& OutError);

/**
 * The ambient light the sky gives (the scene's RenderSettings: ambient mode Skybox, intensity 1.5; the sky controller
 * calls DynamicGI.UpdateEnvironment): Unity's packed L0+L1 spherical harmonics (unity_SHAr/g/b, xyz the Unity-space
 * linear term, w the constant) of pattern `No`'s gradient (upper, middle, lower colours and fades; sun and clouds left
 * out). False when the pattern is unknown.
 */
bool OKStudioSkyAmbient(int32 No, FLinearColor& OutSHAr, FLinearColor& OutSHAg, FLinearColor& OutSHAb);

/** A sky body's direction (`_SunPosition`, `_MoonPosition` of pattern `No`) in UE axes; false when unknown. */
bool OKStudioSkyBodyDirection(int32 No, const FString& Body, FVector& OutDirection);

/** Unity sky direction of a UE direction (OK Studio's Unity→UE mapping: UE = (-x, z, y)). */
inline FVector OKStudioSkyUnityDirection(const FVector& D) { return FVector(-D.X, D.Z, D.Y); }
