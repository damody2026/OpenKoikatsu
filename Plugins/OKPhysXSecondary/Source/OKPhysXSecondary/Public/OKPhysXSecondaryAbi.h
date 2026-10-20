#pragma once
#include "CoreMinimal.h"

// Binary layout shared with Tools/ok-extract/src/px.rs. Keep both in sync.
struct FOKPxParticle
{
	int32 Parent;
	uint32 Flags;       // 1 = root (kinematic, follows the animated pose); root only: 2 body/soft tissue, 4 audit only, 8 worn over clothes, 16 skin chain (no body capsules), 32 tail (OKPhysXSecondary.TailBounce); bits 8..11 solver + 1
	float Radius;
	float Damping;
	float Elasticity;
	float Stiffness;
	float RestLen;
	float MinFraction;
	float MaxFraction;
	float Pos[3];
	float Prev[3];
	float GoalOffset[3]; // world vector from the simulated parent to this particle's animated goal
	float FreezeNormal[3]; // FreezeAxis plane normal through the simulated parent (zero = free)
	float BodyRadius;      // mesh thickness around the bone ending here (0 = unknown): other characters' bodies are met at max(Radius, BodyRadius)
};

struct FOKPxCapsule
{
	float A[3];
	float B[3];
	float Radius;
};

struct FOKPxChainStep
{
	uint64 Key;
	uint32 ParticleCount;
	uint32 ColliderCount;
	FOKPxParticle* Particles;
	const FOKPxCapsule* Colliders;
	float GravityStep[3];
	float Dt;
	float ReferenceStep;
	float VelocityScale;
	uint64 Group;        // character id (0 = none): chains of a group share body capsules and collide with each other
};

// One ragdoll part. Pos/Rot (x,y,z,w) are the animated pose in, the simulated pose out.
struct FOKPxBone
{
	int32 Parent;
	float Radius;
	float Pos[3];
	float Rot[4];
	float Tip[3];
};
