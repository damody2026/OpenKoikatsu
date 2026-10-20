#include "OKBvh.h"
#include "Misc/FileHelper.h"

namespace
{
	struct FJoint
	{
		FString Name;
		int32 Parent = INDEX_NONE;
		FVector Offset = FVector::ZeroVector;   // file axes and units
		TArray<int32> Channels;                 // 0..2 = X/Y/Z position, 3..5 = X/Y/Z rotation
		int32 FirstChannel = 0;
	};

	/** BVH axes -> Unreal axes: x stays, y (up) -> z, z (forward) -> y. A reflection (BVH is right-handed, Unreal left),
	 *  so rotations are carried by conjugation: R' = M R M^-1. */
	FVector ToUnreal(const FVector& V) { return FVector(V.X, V.Z, V.Y); }
	FMatrix Basis()
	{
		FMatrix M = FMatrix::Identity;
		M.M[1][1] = 0.; M.M[1][2] = 1.; M.M[2][1] = 1.; M.M[2][2] = 0.;
		return M;
	}

	FMatrix AxisRotation(int32 Axis, double Degrees)
	{
		// Right-handed rotation about a file axis, column-vector convention (v' = R v), stored transposed for FMatrix.
		const double R = FMath::DegreesToRadians(Degrees), C = FMath::Cos(R), S = FMath::Sin(R);
		FMatrix M = FMatrix::Identity;
		const int32 A = (Axis + 1) % 3, B = (Axis + 2) % 3;
		// Column-vector R: [A][A]=c [A][B]=-s [B][A]=s [B][B]=c; FMatrix multiplies row vectors, so transpose.
		M.M[A][A] = C; M.M[B][A] = -S; M.M[A][B] = S; M.M[B][B] = C;
		return M;
	}
}

bool FOKBvh::Load(const FString& Path, FString& OutError, float InScale)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path)) { OutError = FString::Printf(TEXT("cannot read %s"), *Path); return false; }
	return Parse(Text, OutError, InScale);
}

bool FOKBvh::Parse(const FString& Text, FString& OutError, float InScale)
{
	TArray<FString> Tok;
	Text.ParseIntoArrayWS(Tok);
	int32 I = 0;
	auto Next = [&]() -> FString { return Tok.IsValidIndex(I) ? Tok[I++] : FString(); };
	auto Expect = [&](const TCHAR* What) { const FString T = Next(); if (!T.Equals(What, ESearchCase::IgnoreCase)) { OutError = FString::Printf(TEXT("BVH: expected %s, got '%s'"), What, *T); return false; } return true; };
	if (!Expect(TEXT("HIERARCHY"))) return false;
	TArray<FJoint> Joints;
	TArray<int32> Stack;
	int32 Channels = 0;
	bool bEndSite = false;
	int32 EndDepth = 0;
	while (Tok.IsValidIndex(I))
	{
		const FString T = Next();
		if (T.Equals(TEXT("ROOT"), ESearchCase::IgnoreCase) || T.Equals(TEXT("JOINT"), ESearchCase::IgnoreCase))
		{
			FJoint& J = Joints.AddDefaulted_GetRef();
			J.Name = Next();
			J.Parent = Stack.Num() ? Stack.Last() : INDEX_NONE;
			if (!Expect(TEXT("{"))) return false;
			Stack.Add(Joints.Num() - 1);
		}
		else if (T.Equals(TEXT("End"), ESearchCase::IgnoreCase))
		{
			Next();   // Site
			if (!Expect(TEXT("{"))) return false;
			bEndSite = true;
			++EndDepth;
		}
		else if (T.Equals(TEXT("OFFSET"), ESearchCase::IgnoreCase))
		{
			const FVector O(FCString::Atod(*Next()), FCString::Atod(*Next()), FCString::Atod(*Next()));
			if (!bEndSite && Stack.Num()) Joints[Stack.Last()].Offset = O;
		}
		else if (T.Equals(TEXT("CHANNELS"), ESearchCase::IgnoreCase))
		{
			if (!Stack.Num()) { OutError = TEXT("BVH: CHANNELS outside a joint"); return false; }
			FJoint& J = Joints[Stack.Last()];
			const int32 N = FCString::Atoi(*Next());
			J.FirstChannel = Channels;
			for (int32 C = 0; C < N; ++C)
			{
				const FString Ch = Next().ToLower();
				const int32 Axis = Ch.StartsWith(TEXT("x")) ? 0 : Ch.StartsWith(TEXT("y")) ? 1 : 2;
				J.Channels.Add((Ch.EndsWith(TEXT("rotation")) ? 3 : 0) + Axis);
			}
			Channels += N;
		}
		else if (T == TEXT("}"))
		{
			if (bEndSite && EndDepth > 0) { --EndDepth; bEndSite = EndDepth > 0; }
			else if (Stack.Num()) Stack.Pop();
		}
		else if (T.Equals(TEXT("MOTION"), ESearchCase::IgnoreCase)) break;
	}
	if (Joints.IsEmpty()) { OutError = TEXT("BVH: no joints"); return false; }
	if (!Expect(TEXT("Frames:"))) return false;
	const int32 FrameCount = FCString::Atoi(*Next());
	if (!Expect(TEXT("Frame")) || !Expect(TEXT("Time:"))) return false;
	const double FrameTime = FCString::Atod(*Next());
	if (FrameCount <= 0 || FrameTime <= 0. || Tok.Num() - I < FrameCount * Channels) { OutError = FString::Printf(TEXT("BVH: %d frames x %d channels, %d values"), FrameCount, Channels, Tok.Num() - I); return false; }
	Fps = float(1. / FrameTime);

	// Units: the root's height in the first frame (or the leg chain's length) should be about 1 m.
	if (InScale <= 0.f)
	{
		double Height = 0.;
		for (int32 J = 1; J < Joints.Num(); ++J)
		{
			double Y = 0.;
			for (int32 K = J; K != INDEX_NONE && K != 0; K = Joints[K].Parent) Y += Joints[K].Offset.Y;
			Height = FMath::Max(Height, -Y);   // feet hang below the hips
		}
		InScale = Height > 0. ? float(95. / Height) : 1.f;
		// Snap to the usual units when close (cm, inch, m, mm).
		for (const float U : { 1.f, 2.54f, 100.f, 0.1f }) if (FMath::Abs(InScale / U - 1.f) < 0.5f) { InScale = U; break; }
	}
	Scale = InScale;

	Ref = FReferenceSkeleton();
	{
		FReferenceSkeletonModifier Mod(Ref, nullptr);
		for (const FJoint& J : Joints)
		{
			const FTransform Rest(FQuat::Identity, ToUnreal(J.Offset) * Scale);
			Mod.Add(FMeshBoneInfo(FName(*J.Name), J.Name, J.Parent), Rest);
		}
	}
	const FMatrix M = Basis();
	Frames.SetNum(FrameCount);
	for (int32 F = 0; F < FrameCount; ++F)
	{
		TArray<FTransform>& Pose = Frames[F];
		Pose = Ref.GetRefBonePose();
		const int32 Base = I + F * Channels;
		for (int32 J = 0; J < Joints.Num(); ++J)
		{
			const FJoint& Joint = Joints[J];
			FVector Pos = Joint.Offset;
			bool bPos = false;
			FMatrix R = FMatrix::Identity;
			for (int32 C = 0; C < Joint.Channels.Num(); ++C)
			{
				const double V = FCString::Atod(*Tok[Base + Joint.FirstChannel + C]);
				const int32 Ch = Joint.Channels[C];
				if (Ch < 3) { if (!bPos) { Pos = FVector::ZeroVector; bPos = true; } Pos[Ch] = V; }
				// Channels listed Z X Y mean v' = Rz Rx Ry v: with row vectors that is v Ry^T Rx^T Rz^T, i.e. later channels first.
				else R = AxisRotation(Ch - 3, V) * R;
			}
			// Position channels (normally the root's only) replace the offset.
			const FMatrix Ru = M * R * M;   // M is its own inverse
			Pose[J] = FTransform(FQuat(Ru).GetNormalized(), ToUnreal(Pos) * Scale);
		}
	}
	return true;
}
