#pragma once

#include "CoreMinimal.h"

class AKKPreviewActor;

/** Helpers of OKStudioGame.cpp shared with its self tests (OKStudioGameTests.cpp). */
namespace OKStudioGameInternal
{
	bool BoneWorld(const AKKPreviewActor* Actor, const FString& Bone, FTransform& Out);
	/** Unity Euler degrees (Z, X, Y order: q = qy qx qz) to and from a quaternion in the same (Unity) basis. */
	FQuat UnityQuatFromEuler(const FVector& Euler);
	FVector UnityEulerFromQuat(const FQuat& Q);
	/** Whether an IK effector can take a rotation (Studio's hand and foot targets, 5..8). */
	inline bool IkEffectorTurns(int32 Effector) { return Effector >= 5 && Effector <= 8; }
	/** The hand or foot bone's rotation now, in the character's Unity space, as a Studio IK target's Euler. */
	bool IkEffectorEuler(const AKKPreviewActor* Actor, int32 Effector, FVector& OutEuler);
}

/** The studio's control channel (OKStudioControl.cpp): listens while a studio runs with -Control. */
namespace OKStudioControl
{
	void Start();
	void Stop();
}
