#pragma once

#include "CoreMinimal.h"
#include "ReferenceSkeleton.h"

/**
 * A BVH motion capture file as an Unreal skeleton and local poses: the hierarchy (joint offsets = the rest, rest
 * rotations identity), and per frame the joints' local rotations (Euler in the file's channel order) and the root
 * position. BVH axes (right-handed, Y up, character facing +Z) become Unreal axes (Z up, facing +Y) by a basis change
 * applied to positions and rotations alike; units are scaled to centimetres (Scale, or a guess from the hips height).
 */
struct OKANIMATION_API FOKBvh
{
	FReferenceSkeleton Ref;
	/** Local transforms of every bone, per frame. */
	TArray<TArray<FTransform>> Frames;
	float Fps = 30.f;
	/** Centimetres per file unit that was applied. */
	float Scale = 1.f;

	/** Scale <= 0: guessed (CMU files are in inches-ish units, others in cm or m) so the hips stand about 1 m high. */
	bool Parse(const FString& Text, FString& OutError, float InScale = 0.f);
	bool Load(const FString& Path, FString& OutError, float InScale = 0.f);
};
