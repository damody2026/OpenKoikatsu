#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"
#include "KKAnimationCatalog.h"
#include "OKAnimationAssets.h"
#include "OKAnimationLibrary.h"
#include "OKAnimationPlayers.h"
#include "OKUnrealSkeleton.h"
#include "KKOkAnimation.h"
#include "KKPairedPlayback.h"
#include "KKPreviewActor.h"
#include "ok_extract.h"
#include "Animation/AnimSequence.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OKLibraryBrowser.h"
#include "KKCardWorkspace.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Widgets/Layout/SBox.h"

// Control-port test commands (OKAnim.*): convert what the running game plays, play it through the OK system on the
// same characters, compare with the native result, and restore the native playback.
namespace
{
	struct FOkAnimTestState
	{
		TWeakObjectPtr<AKKPreviewActor> Single;
		FString SingleClip;
		TStrongObjectPtr<UOKAnimationAsset> SingleAsset;
		TWeakObjectPtr<AKKPairedPlayback> Pair;
		TWeakObjectPtr<AOKGroupPlayer> Group;
		TStrongObjectPtr<UOKAnimationAsset> GroupAsset;
		/** Where the single character stood when OKAnim.RootMotion started (Report prints the travel since). */
		FVector RootStart = FVector::ZeroVector;
		double RootStartTime = 0.;
	};
	FOkAnimTestState GTest;

	AKKPairedPlayback* ActivePair(UWorld* World)
	{
		for (TActorIterator<AKKPairedPlayback> It(World); It; ++It)
			if (IsValid(It->FemaleActor) && IsValid(It->MaleActor) && It->FemaleActor->PoseMode == 2 && It->IsActorTickEnabled())
			{
				It->SyncAsync();   // commands read the characters' sessions: no frame may be solving in the background
				return *It;
			}
		return nullptr;
	}

	/** The character a single test acts on: the one playing a native clip outside any pair, else any loaded one. */
	AKKPreviewActor* SingleCharacter(UWorld* World)
	{
		if (GTest.Single.IsValid()) return GTest.Single.Get();
		AKKPairedPlayback* Pair = ActivePair(World);
		AKKPreviewActor* Any = nullptr;
		for (TActorIterator<AKKPreviewActor> It(World); It; ++It)
		{
			if (!It->Session.IsOpen() || (Pair && (*It == Pair->FemaleActor || *It == Pair->MaleActor))) continue;
			if (It->PoseMode == 2) return *It;
			if (!Any) Any = *It;
		}
		return Any ? Any : (Pair ? Pair->FemaleActor.Get() : nullptr);
	}

	float ArgFloat(const TArray<FString>& Args, int32 I, float Default) { return Args.IsValidIndex(I) && Args[I].IsNumeric() ? FCString::Atof(*Args[I]) : Default; }

	void StopAll(FOutputDevice& Out)
	{
		if (AOKGroupPlayer* G = GTest.Group.Get()) { G->Stop(); G->Destroy(); Out.Logf(TEXT("group stopped")); }
		GTest.Group.Reset();
		GTest.GroupAsset.Reset();
		AKKPairedPlayback* Pair = GTest.Pair.Get();
		if (AKKPreviewActor* A = GTest.Single.Get())
		{
			if (UOKAnimationComponent* C = A->FindComponentByClass<UOKAnimationComponent>()) C->Stop();
			A->AnimationPlaybackSpeed = 1.f;
			const bool bInPair = Pair && (Pair->FemaleActor == A || Pair->MaleActor == A);
			if (!bInPair && !GTest.SingleClip.IsEmpty())
				Out.Logf(TEXT("native %s %s"), *GTest.SingleClip.Right(60), A->SetGameAnimation(GTest.SingleClip) ? TEXT("restored") : TEXT("restore failed"));
		}
		if (Pair)
		{
			FString Error;
			Out.Logf(TEXT("pair restart %s %s"), Pair->Restart(Error) ? TEXT("ok") : TEXT("failed"), *Error);
		}
		GTest.Pair.Reset();
		GTest.Single.Reset();
		GTest.SingleClip.Reset();
		GTest.SingleAsset.Reset();
		GTest.RootStartTime = 0.;
	}

	bool PlaySingle(AKKPreviewActor* A, UOKAnimationAsset* Asset, FOutputDevice& Out)
	{
		// A character of a running pair is driven by the pair: pause it (restored by OKAnim.Stop).
		if (AKKPairedPlayback* Pair = ActivePair(A->GetWorld()); Pair && (Pair->FemaleActor == A || Pair->MaleActor == A))
		{
			Pair->Stop();
			GTest.Pair = Pair;
		}
		if (!GTest.Single.IsValid())
		{
			GTest.Single = A;
			if (const UOKAnimSequence* Seq = Cast<UOKAnimSequence>(Asset)) if (const FString* Key = Seq->Meta.Find(TEXT("source_key"))) GTest.SingleClip = *Key;
		}
		GTest.SingleAsset.Reset(Asset);
		const float Speed = A->AnimationPlaybackSpeed;
		const bool bOk = UOKAnimationLibrary::PlayOnCharacter(A, Asset);
		A->AnimationPlaybackSpeed = Speed;
		Out.Logf(TEXT("play %s on %s: %s %s"), *Asset->GetName(), *A->GetName(), bOk ? TEXT("ok") : TEXT("failed"), bOk ? TEXT("") : *A->OkReport);
		return bOk;
	}

	/** Max / mean angle (deg) between the Animator bones of two pose snapshots. */
	void ReadPose(FOkSession& S, TMap<FString, FQuat4f>& Out)
	{
		Out.Reset();
		const int32 N = S.SkelBoneCount();
		for (int32 I = 0; I < N; ++I)
		{
			OkBoneView V{};
			if (!S.SkelBoneAt(I, V) || !V.name) continue;
			const FString Name = UTF8_TO_TCHAR(V.name);
			if (Name.StartsWith(TEXT("cf_j_"))) Out.Add(Name, FQuat4f(V.pose_rot[0], V.pose_rot[1], V.pose_rot[2], V.pose_rot[3]));
		}
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdPlay(TEXT("OKAnim.Play"),
		TEXT("OKAnim.Play [fps]: converts the clip the character plays to an OK animation and plays that instead."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			AKKPreviewActor* A = SingleCharacter(World);
			if (!A) { Out.Logf(TEXT("no character")); return; }
			const double T0 = FPlatformTime::Seconds();
			FString Error;
			UOKAnimSequence* Seq = UOKAnimationLibrary::ConvertKKClip(A, GTest.SingleClip, ArgFloat(Args, 0, 30.f), nullptr, TEXT("OKTestClip"), Error);
			if (!Seq) { Out.Logf(TEXT("convert failed: %s"), *Error); return; }
			Out.Logf(TEXT("converted %s: %d bones %d frames %.2fs loop=%d bytes=%d in %.0f ms"), *Seq->Meta.FindRef(TEXT("source_key")), Seq->Bones.Num(), Seq->Frames,
				Seq->Duration, Seq->bLooping, Seq->Data.Num(), (FPlatformTime::Seconds() - T0) * 1000.);
			PlaySingle(A, Seq, Out);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdValidate(TEXT("OKAnim.Validate"),
		TEXT("OKAnim.Validate [samples]: native KK frame vs the OK animation on the same character (Animator bone angles)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			AKKPreviewActor* A = SingleCharacter(World);
			if (!A || A->PoseMode != 2) { Out.Logf(TEXT("needs a character playing a native clip")); return; }
			FString Error;
			UOKAnimSequence* Seq = UOKAnimationLibrary::ConvertKKClip(A, FString(), 30.f, nullptr, TEXT("OKValidateClip"), Error);
			if (!Seq) { Out.Logf(TEXT("convert failed: %s"), *Error); return; }
			const TSharedPtr<FOkAnimPlayer> Player = Seq->CreatePlayer(Error);
			if (!Player) { Out.Logf(TEXT("player: %s"), *Error); return; }
			const int32 Samples = FMath::Clamp(int32(ArgFloat(Args, 0, 12.f)), 2, 200);
			const float Phase = A->AnimPhase;
			// Bones a dynamics pass moves (ver02 breasts, ...) differ between two native evaluations of the same time:
			// they are reported apart, the animation error is measured on the others.
			TMap<FString, double> BoneError, Jitter;
			TMap<FString, FQuat4f> Native, Again, Ok;
			for (int32 K = 0; K < Samples; ++K)
			{
				const float T = Seq->Duration * K / Samples;
				if (!A->Session.ApplyAnimFrame(T, 0.f)) { Out.Logf(TEXT("native frame failed")); break; }
				ReadPose(A->Session, Native);
				FString Report;
				if (!Player->ApplySessions({ &A->Session }, T, 0.f, &Report)) { Out.Logf(TEXT("OK frame failed: %s"), *Report); break; }
				ReadPose(A->Session, Ok);
				// Native again after the OK frame: a bone whose result depends on the previous pose (dynamics) differs here.
				A->Session.ApplyAnimFrame(T, 0.f);
				ReadPose(A->Session, Again);
				for (const auto& Pair : Native)
				{
					const FQuat4f* Q = Ok.Find(Pair.Key);
					const FQuat4f* R = Again.Find(Pair.Key);
					if (!Q || !R) continue;
					double& E = BoneError.FindOrAdd(Pair.Key);
					E = FMath::Max(E, FOKUnrealSkeleton::AngleDegrees(FQuat(Pair.Value), FQuat(*Q)));
					double& J = Jitter.FindOrAdd(Pair.Key);
					J = FMath::Max(J, FOKUnrealSkeleton::AngleDegrees(FQuat(Pair.Value), FQuat(*R)));
				}
			}
			A->Session.ApplyAnimFrame(Phase, 0.f);
			A->RefreshSkinnedDisplay();
			// Bones the character's dynamics simulate are not the animation's: reported apart with the unstable ones.
			const TSet<FString> Simulated(FOkAnimBridge::DynamicBones(A->Session));
			double Max = 0., Sum = 0., DynMax = 0.;
			int32 Count = 0, Dynamic = 0;
			TArray<TPair<double, FString>> Ranked;
			for (const auto& Pair : BoneError)
			{
				if (Jitter.FindRef(Pair.Key) > 0.01 || Simulated.Contains(Pair.Key)) { ++Dynamic; DynMax = FMath::Max(DynMax, Pair.Value); continue; }
				Max = FMath::Max(Max, Pair.Value);
				Sum += Pair.Value;
				++Count;
				Ranked.Emplace(Pair.Value, Pair.Key);
			}
			Ranked.Sort([](const TPair<double, FString>& L, const TPair<double, FString>& R) { return L.Key > R.Key; });
			FString Worst;
			for (int32 I = 0; I < FMath::Min(3, Ranked.Num()); ++I) Worst += FString::Printf(TEXT(" %s=%.3f"), *Ranked[I].Value, Ranked[I].Key);
			Out.Logf(TEXT("validate %s samples=%d animated=%d max=%.3fdeg mean=%.4fdeg worst:%s | dynamic=%d (max %.2fdeg)"), *Seq->Meta.FindRef(TEXT("source_key")).Right(60),
				Samples, Count, Max, Count ? Sum / Count : 0., *Worst, Dynamic, DynMax);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdPair(TEXT("OKAnim.PlayPair"),
		TEXT("OKAnim.PlayPair [fps]: converts the playing KK paired state to an OK group and plays it with the group player."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			AKKPairedPlayback* Pair = GTest.Pair.IsValid() ? GTest.Pair.Get() : ActivePair(World);
			if (!Pair) { Out.Logf(TEXT("no paired playback")); return; }
			if (AOKGroupPlayer* G = GTest.Group.Get()) { G->Stop(); G->Destroy(); }
			if (!GTest.Pair.IsValid())
			{
				// Convert from the native state at its start, then hand the characters to the OK player.
				FString Error;
				if (!Pair->Restart(Error)) { Out.Logf(TEXT("pair restart failed: %s"), *Error); return; }
			}
			const double T0 = FPlatformTime::Seconds();
			FString Error;
			UOKGroupAnimSequence* Group = UOKAnimationLibrary::ConvertKKPair(Pair, ArgFloat(Args, 0, 30.f), nullptr, TEXT("OKTestPair"), Error);
			if (!Group) { Out.Logf(TEXT("convert failed: %s"), *Error); return; }
			const double Ms = (FPlatformTime::Seconds() - T0) * 1000.;
			{
				// The package as a file too (imported by the OKBatchConvert commandlet as an OK group asset).
				TArray<uint8> Package;
				if (FOkAnimBridge::GroupFromPair(Pair->FemaleActor->Session, Pair->MaleActor->Session, Pair->GetState(), Pair->GetFemaleMotion(), Pair->GetMaleMotion(),
					ArgFloat(Args, 0, 30.f), Package))
				{
					const FString Key = Pair->GetFemaleClip();
					FString Name;
					Key.Split(TEXT("."), nullptr, &Name, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
					FFileHelper::SaveArrayToFile(Package, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OKAnim"), TEXT("Groups"),
						FString::Printf(TEXT("%s_%08x.okgroup"), *FPaths::MakeValidFileName(Name.Left(60)), FCrc::StrCrc32(*Key))));
				}
			}
			GTest.Pair = Pair;
			GTest.GroupAsset.Reset(Group);
			const float Speed = Pair->GetSpeed();
			Pair->Stop();
			AOKGroupPlayer* Player = UOKAnimationLibrary::PlayGroup(World, Group, { Pair->FemaleActor.Get(), Pair->MaleActor.Get() }, Error);
			if (!Player) { Out.Logf(TEXT("group play failed: %s"), *Error); StopAll(Out); return; }
			Player->PlayRate = Speed;
			GTest.Group = Player;
			FString Contacts;
			for (const FOKContact& C : Group->Contacts) Contacts += FString::Printf(TEXT(" %d.%s->%d.%s[%.2f-%.2f]"), C.Role, *C.Effector.ToString(), C.TargetRole, *C.TargetBone.ToString(), C.Start, C.End);
			Out.Logf(TEXT("group %s: roles=%d contacts=%d length=%.2fs converted in %.0f ms;%s"), *Pair->GetState(), Group->Roles.Num(), Group->Contacts.Num(),
				Group->GetPlayLength(), Ms, *Contacts);
		}));

	/** A clip argument: a .okanim file (absolute or under Saved/OKAnim/KK), a full clip key, or a state name of the playing clip's source. */
	UOKAnimSequence* ResolveClip(AKKPreviewActor* A, const FString& Arg, FString& Error)
	{
		static int32 Counter = 0;
		const FName Name(*FString::Printf(TEXT("OKTestClip%d"), ++Counter));
		if (Arg.EndsWith(TEXT(".okanim")))
		{
			const FString Path = FPaths::FileExists(Arg) ? Arg : FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OKAnim"), TEXT("KK"), Arg);
			return UOKAnimationLibrary::LoadOkAnimFile(Path, nullptr, Name, Error);
		}
		if (Arg.IsEmpty() || Arg.Contains(TEXT("|"))) return UOKAnimationLibrary::ConvertKKClip(A, Arg, 30.f, nullptr, Name, Error);
		// A state name: look it up in the source of the clip the character plays.
		const UOKAnimSequence* Current = UOKAnimationLibrary::ConvertKKClip(A, GTest.SingleClip, 30.f, nullptr, NAME_None, Error);
		FString Source;
		if (!Current || !Current->Meta.FindRef(TEXT("source_key")).Split(TEXT("|"), &Source, nullptr)) return nullptr;
		for (const FString& Label : A->Session.ClipNamesForSource(Source))
			if (Label.EndsWith(TEXT(".") + Arg) || Label == Arg) return UOKAnimationLibrary::ConvertKKClip(A, Source + TEXT("|") + Label, 30.f, nullptr, Name, Error);
		Error = FString::Printf(TEXT("no state %s in the playing source"), *Arg);
		return nullptr;
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdBlend(TEXT("OKAnim.Blend"),
		TEXT("OKAnim.Blend <x> <clip B> [<y> <clip C>]: 1D (or 2D) OK blend space of the playing clip (at 0) and other clips (state name, key or .okanim)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			AKKPreviewActor* A = SingleCharacter(World);
			if (!A || Args.Num() < 2) { Out.Logf(TEXT("usage: OKAnim.Blend <x> <clipB> [<y> <clipC>]")); return; }
			FString Error;
			UOKAnimSequence* SeqA = ResolveClip(A, GTest.SingleClip, Error);
			UOKAnimSequence* SeqB = SeqA ? ResolveClip(A, Args[1], Error) : nullptr;
			UOKAnimSequence* SeqC = SeqB && Args.Num() >= 4 ? ResolveClip(A, Args[3], Error) : nullptr;
			if (!SeqA || !SeqB || (Args.Num() >= 4 && !SeqC)) { Out.Logf(TEXT("convert failed: %s"), *Error); return; }
			UOKBlendSpace* Blend = NewObject<UOKBlendSpace>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UOKBlendSpace::StaticClass()), RF_Transient);
			Blend->Axes.Add({ TEXT("X"), 0.f, 1.f });
			Blend->Samples.Add({ SeqA, FVector2D(0, 0) });
			Blend->Samples.Add({ SeqB, FVector2D(1, 0) });
			if (SeqC) { Blend->Axes.Add({ TEXT("Y"), 0.f, 1.f }); Blend->Samples.Add({ SeqC, FVector2D(0, 1) }); }
			if (!PlaySingle(A, Blend, Out)) return;
			UOKAnimationComponent* C = A->FindComponentByClass<UOKAnimationComponent>();
			C->SetParameter(TEXT("X"), ArgFloat(Args, 0, 0.5f));
			if (SeqC) C->SetParameter(TEXT("Y"), ArgFloat(Args, 2, 0.f));
			Out.Logf(TEXT("blend %dD length=%.2fs (A %.2fs %s, B %.2fs %s)"), Blend->Axes.Num(), Blend->GetPlayLength(), SeqA->Duration,
				*SeqA->Meta.FindRef(TEXT("name")), SeqB->Duration, *SeqB->Meta.FindRef(TEXT("name")));
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdMontage(TEXT("OKAnim.Montage"),
		TEXT("OKAnim.Montage <blend s> <clip> <clip> ...: an OK montage of clips (state names, keys or .okanim) cross-fading into each other."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			AKKPreviewActor* A = SingleCharacter(World);
			if (!A || Args.Num() < 3) { Out.Logf(TEXT("usage: OKAnim.Montage <blend> <clip> <clip> ...")); return; }
			UOKAnimMontage* Montage = NewObject<UOKAnimMontage>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UOKAnimMontage::StaticClass()), RF_Transient);
			Montage->bLooping = true;
			FString Error, Names;
			for (int32 I = 1; I < Args.Num(); ++I)
			{
				UOKAnimSequence* Seq = ResolveClip(A, Args[I], Error);
				if (!Seq) { Out.Logf(TEXT("%s: %s"), *Args[I], *Error); return; }
				FOKMontageSection& S = Montage->Sections.AddDefaulted_GetRef();
				S.Name = FName(*FString::Printf(TEXT("S%d"), I));
				S.Animation = Seq;
				S.BlendIn = I == 1 ? 0.f : ArgFloat(Args, 0, 0.3f);
				Names += FString::Printf(TEXT(" %s@%.2f"), *Seq->Meta.FindRef(TEXT("name")), Montage->GetSectionStart(S.Name));
			}
			if (!PlaySingle(A, Montage, Out)) return;
			Out.Logf(TEXT("montage length=%.2fs sections:%s"), Montage->GetPlayLength(), *Names);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdFade(TEXT("OKAnim.Fade"),
		TEXT("OKAnim.Fade <clip> [blend s]: cross-fades the playing OK animation into another clip (state name, key or .okanim); reports the largest pose jump between frames during the fade."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			AKKPreviewActor* A = GTest.Single.Get();
			UOKAnimationComponent* C = A ? A->FindComponentByClass<UOKAnimationComponent>() : nullptr;
			if (!C || !C->IsPlaying() || Args.IsEmpty()) { Out.Logf(TEXT("needs an OK animation playing (OKAnim.Play) and a clip")); return; }
			FString Error;
			UOKAnimSequence* Seq = ResolveClip(A, Args[0], Error);
			if (!Seq) { Out.Logf(TEXT("%s"), *Error); return; }
			const float Blend = ArgFloat(Args, 1, 0.3f);
			// Pose continuity: the step between consecutive 1/30 s frames across the switch, against the steps before it.
			auto Snapshot = [&](TMap<FString, FQuat4f>& Pose) { ReadPose(A->Session, Pose); };
			// Only the bones the OK animations drive (others belong to the character's own rigs and dynamics).
			TSet<FName> Animated(Seq->Bones);
			for (const FString& B : FOkAnimBridge::DynamicBones(A->Session)) Animated.Remove(FName(B));
			FString WorstBone;
			double WorstStep = 0.;
			auto Step = [&](const TMap<FString, FQuat4f>& P0, const TMap<FString, FQuat4f>& P1)
			{
				double M = 0.;
				for (const auto& P : P0) if (const FQuat4f* Q = P1.Find(P.Key); Q && Animated.Contains(FName(P.Key)))
				{
					const double G = FOKUnrealSkeleton::AngleDegrees(FQuat(P.Value), FQuat(*Q));
					M = FMath::Max(M, G);
					if (G > WorstStep) { WorstStep = G; WorstBone = P.Key; }
				}
				return M;
			};
			const float Speed = A->AnimationPlaybackSpeed;
			A->AnimationPlaybackSpeed = 1.f;
			TMap<FString, FQuat4f> Prev, Cur;
			A->ApplyPoseFrame(1.f / 30.f, true);
			Snapshot(Prev);
			double Before = 0., During = 0.;
			for (int32 F = 0; F < 6; ++F) { A->ApplyPoseFrame(1.f / 30.f, true); Snapshot(Cur); Before = FMath::Max(Before, Step(Prev, Cur)); Prev = Cur; }
			WorstStep = 0.;
			WorstBone.Reset();
			if (!C->Play(Seq, Blend)) { Out.Logf(TEXT("play failed")); return; }
			GTest.SingleAsset.Reset(Seq);
			A->ApplyPoseFrame(0.f, false);
			Snapshot(Cur);
			During = Step(Prev, Cur);
			Prev = Cur;
			for (int32 F = 0; F < FMath::CeilToInt(Blend * 30.f) + 3; ++F) { A->ApplyPoseFrame(1.f / 30.f, true); Snapshot(Cur); During = FMath::Max(During, Step(Prev, Cur)); Prev = Cur; }
			A->AnimationPlaybackSpeed = Speed;
			Out.Logf(TEXT("fade %.2fs into %s: largest frame step before %.2fdeg, during %.2fdeg (%s) (an instant switch would jump)"), Blend, *Seq->Meta.FindRef(TEXT("name")), Before, During, *WorstBone);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdSection(TEXT("OKAnim.Section"),
		TEXT("OKAnim.Section <name> | next <section> <next>: montage section jump / branch on the test playback; prints the current section."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Out)
		{
			AKKPreviewActor* A = GTest.Single.Get();
			UOKAnimationComponent* C = A ? A->FindComponentByClass<UOKAnimationComponent>() : nullptr;
			if (!C) { Out.Logf(TEXT("no OK test playback")); return; }
			if (Args.Num() >= 3 && Args[0] == TEXT("next")) C->SetNextSection(FName(Args[1]), FName(Args[2]));
			else if (Args.Num() == 1) Out.Logf(TEXT("jump %s "), C->JumpToSection(FName(Args[0])) ? TEXT("ok") : TEXT("failed"));
			Out.Logf(TEXT("section=%s t=%.2f"), *C->GetCurrentSection().ToString(), A->AnimPhase);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdRootMotion(TEXT("OKAnim.RootMotion"),
		TEXT("OKAnim.RootMotion <in_place|root_motion|none>: sets the root motion mode of the playing single OK animation and plays it again; OKAnim.Report prints the character's travel since."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Out)
		{
			AKKPreviewActor* A = GTest.Single.Get();
			UOKAnimSequence* Seq = Cast<UOKAnimSequence>(GTest.SingleAsset.Get());
			if (!A || !Seq || Args.IsEmpty()) { Out.Logf(TEXT("needs a single OK animation playing (OKAnim.Play / LibraryPlay) and a mode")); return; }
			Seq->Info.RootMotion = Args[0] == TEXT("in_place") ? EOKRootMotion::InPlace : Args[0] == TEXT("root_motion") ? EOKRootMotion::RootMotion : EOKRootMotion::AsAuthored;
			FString Error;
			// travel=<hips heights>: adds that much forward hips travel over the clip (KK clips play in place).
			float Travel = 0.f;
			if (Args.Num() > 1 && FParse::Value(*Args[1], TEXT("travel="), Travel) && Travel != 0.f)
			{
				TArray<float> Motion;
				TArray<uint8> Moved;
				const int32 N = Seq->Bones.Num(), Frames = Seq->Frames;
				if (FOkAnimBridge::GetTracks(Seq->Data, Motion) && Motion.Num() == Frames * N * 4 + Frames * 3)
				{
					for (int32 Fr = 0; Fr < Frames; ++Fr) Motion[Frames * N * 4 + Fr * 3 + 2] += Travel * Fr / FMath::Max(1, Frames - 1);
					if (FOkAnimBridge::SetTracks(Seq->Data, Motion, Moved)) Seq->SetData(Moved, Error);
				}
				Seq->Info.RootMotion = Args[0] == TEXT("in_place") ? EOKRootMotion::InPlace : Args[0] == TEXT("root_motion") ? EOKRootMotion::RootMotion : EOKRootMotion::AsAuthored;
			}
			if (!Seq->ApplyEdits(Error)) { Out.Logf(TEXT("edit failed: %s"), *Error); return; }
			UOKAnimationComponent* C = A->FindComponentByClass<UOKAnimationComponent>();
			if (!C || !C->Play(Seq, 0.f)) { Out.Logf(TEXT("play failed")); return; }
			GTest.RootStart = A->GetActorLocation();
			GTest.RootStartTime = FPlatformTime::Seconds();
			// Net hips travel over one cycle, in hips heights (what root motion hands to the character).
			Out.Logf(TEXT("root motion %s on %s (%.2fs, loop=%d)"), *Args[0], *Seq->Meta.FindRef(TEXT("name")), Seq->Duration, Seq->bLooping);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdParam(TEXT("OKAnim.Param"),
		TEXT("OKAnim.Param <name> <value>: sets an OK player parameter (blend space axis) on the test playback."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			if (Args.Num() < 2) return;
			if (AOKGroupPlayer* G = GTest.Group.Get()) G->SetParameter(FName(Args[0]), FCString::Atof(*Args[1]));
			if (AKKPreviewActor* A = GTest.Single.Get())
				if (UOKAnimationComponent* C = A->FindComponentByClass<UOKAnimationComponent>()) C->SetParameter(FName(Args[0]), FCString::Atof(*Args[1]));
			Out.Logf(TEXT("%s=%s"), *Args[0], *Args[1]);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdReport(TEXT("OKAnim.Report"),
		TEXT("OKAnim.Report [reset]: OK playback state and the group contact errors."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			if (AOKGroupPlayer* G = GTest.Group.Get())
			{
				Out.Logf(TEXT("group t=%.2f frames=%d contacts=%d err=%.4fm peak=%.4fm report=%s"), G->GetTime(), G->FramesPlayed, G->LastReport.Contacts,
					G->LastReport.MaxContactError, G->PeakContactError, *G->LastReport.Json);
				if (Args.Num() && Args[0] == TEXT("reset")) G->ResetReport();
			}
			if (AKKPreviewActor* A = GTest.Single.Get())
			{
				Out.Logf(TEXT("single %s mode=%d t=%.2f report=%s"), *A->GetName(), A->PoseMode, A->AnimPhase, *A->OkReport);
				if (GTest.RootStartTime > 0.)
				{
					const FVector D = A->GetActorLocation() - GTest.RootStart;
					Out.Logf(TEXT(" root travel %.1f cm (x %.1f y %.1f z %.1f) in %.1f s"), D.Size(), D.X, D.Y, D.Z, FPlatformTime::Seconds() - GTest.RootStartTime);
				}
			}
			if (AKKPairedPlayback* Pair = ActivePair(World))
				Out.Logf(TEXT("pair %s state=%s via %s %s"), *Pair->GetFemaleClip().Right(40), *Pair->GetState(), Pair->IsUsingOK() ? TEXT("OK") : TEXT("native"), *Pair->OkReport);
			else if (!GTest.Group.IsValid() && !GTest.Single.IsValid()) Out.Logf(TEXT("no OK playback"));
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdStop(TEXT("OKAnim.Stop"),
		TEXT("OKAnim.Stop: stops the OK test playback and restores the native one."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>&, UWorld*, FOutputDevice& Out) { StopAll(Out); }));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdFamily(TEXT("OKAnim.SaveFamily"),
		TEXT("OKAnim.SaveFamily: writes the KK family rest of every loaded character's sex (unshaped Animator rest) as the plugin's built-in family rests."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>&, UWorld* World, FOutputDevice& Out)
		{
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OKAnimation"));
			if (!Plugin) return;
			TSet<int32> Done;
			for (TActorIterator<AKKPreviewActor> It(World); It; ++It)
			{
				if (!It->Session.IsOpen()) continue;
				FString Error;
				UOKSkeletonFamily* Family = UOKAnimationLibrary::MakeFamilyFromCharacter(*It, nullptr, NAME_None, Error);
				if (!Family) { Out.Logf(TEXT("%s: %s "), *It->GetName(), *Error); continue; }
				// KK: sex 0 = male, 1 = female; the female rest is the family's main one.
				const int32 Sex = Family->RestHeaderJson.Contains(TEXT("\"sex\":0")) ? 0 : 1;
				if (Done.Contains(Sex)) continue;
				Done.Add(Sex);
				const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), Sex == 0 ? TEXT("kk_cf_j_rest_male.json") : TEXT("kk_cf_j_rest.json"));
				Out.Logf(TEXT("%s %s (%d bones) "), FFileHelper::SaveStringToFile(Family->RestHeaderJson, *Path) ? TEXT("saved") : TEXT("save failed"), *Path, Family->GetBoneNames().Num());
			}
		}));

	/** KK sex of a character (0 male, 1 female) from its rest header. */
	int32 CharacterSex(AKKPreviewActor* A)
	{
		FString Json;
		return FOkAnimBridge::SessionRestHeader(A->Session, Json) && Json.Contains(TEXT("\"sex\":0")) ? 0 : 1;
	}

	/** Whether an animation source belongs to a sex (the game's own rule: h/anim/female|male bundles are sex-specific). */
	bool SourceMatchesSex(const FString& Source, int32 Sex)
	{
		FString Decoded = Source;
		for (const FString Prefix : { FString(TEXT("states:studio@")), FString(TEXT("states:controller@")) })
		{
			if (!Source.StartsWith(Prefix)) continue;
			const FString Hex = Source.Mid(Prefix.Len());
			if (Hex.Len() % 2 != 0 || Hex.Len() > 65536) return false;
			TArray<uint8> Bytes;
			Bytes.SetNumZeroed(Hex.Len() / 2 + 1);
			if (HexToBytes(Hex, Bytes.GetData()) != Hex.Len() / 2) return false;
			Decoded = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()));
			break;
		}
		return !Decoded.Contains(Sex == 0 ? TEXT("h/anim/female/") : TEXT("h/anim/male/"));
	}

	/** Background KK -> OK conversion on Rust worker threads (the game keeps running), resumable, with a catalog. */
	struct FKKBatch
	{
		TSharedPtr<FOkAnimBatch> Worker;
		int32 Total = 0, Processed = 0, Done = 0, Skipped = 0, Failed = 0;
		int64 Bytes = 0;
		double Start = 0.;
		FString Dir, FirstError;
		TArray<TSharedPtr<FJsonValue>> Catalog;
		FTSTicker::FDelegateHandle Ticker;
		bool IsRunning() const { return Ticker.IsValid(); }
		FString Status() const
		{
			const double S = FPlatformTime::Seconds() - Start;
			return FString::Printf(TEXT("batch %d/%d converted=%d skipped=%d failed=%d %.1f MB %.0f s (%.1f/s) %s -> %s %s"), Processed, Total, Done, Skipped, Failed, Bytes / 1048576.,
				S, S > 0. ? Processed / S : 0., IsRunning() ? TEXT("running") : TEXT("finished"), *Dir, *FirstError);
		}
	};
	FKKBatch GBatch;
	TAutoConsoleVariable<int32> CVarBatchThreads(TEXT("ok.Anim.BatchThreads"), 6, TEXT("OKAnim.BatchKK worker threads."));

	FString FileNameFor(const FString& Key) { return FOkAnimBridge::ClipFileStem(Key); }

	void WriteBatchCatalog()
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("clips"), GBatch.Catalog);
		FString Text;
		const auto Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Root, Writer);
		FFileHelper::SaveStringToFile(Text, *FPaths::Combine(GBatch.Dir, TEXT("catalog.json")));
	}

	bool BatchTick()
	{
		FString Json;
		TSharedPtr<FJsonObject> J;
		if (!GBatch.Worker || !GBatch.Worker->Poll(GBatch.Catalog.Num(), Json) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), J) || !J)
		{
			GBatch.FirstError = TEXT("batch poll failed: ") + FOkAnimBridge::LastError();
			GBatch.Ticker.Reset();
			return false;
		}
		GBatch.Processed = J->GetIntegerField(TEXT("processed"));
		GBatch.Done = J->GetIntegerField(TEXT("converted"));
		GBatch.Skipped = J->GetIntegerField(TEXT("skipped"));
		GBatch.Failed = J->GetIntegerField(TEXT("failed"));
		GBatch.FirstError = J->GetStringField(TEXT("first_error"));
		for (const TSharedPtr<FJsonValue>& V : J->GetArrayField(TEXT("entries")))
		{
			const TSharedPtr<FJsonObject> E = V->AsObject();
			if (!E) continue;
			// Catalog paths are relative to the batch folder.
			FString File = FPaths::ConvertRelativePathToFull(E->GetStringField(TEXT("file")));
			FPaths::MakePathRelativeTo(File, *(GBatch.Dir + TEXT("/")));
			E->SetStringField(TEXT("file"), File);
			GBatch.Bytes += int64(E->HasField(TEXT("bytes")) ? E->GetNumberField(TEXT("bytes")) : 0.);
			GBatch.Catalog.Add(V);
		}
		if (J->GetBoolField(TEXT("running"))) return true;
		WriteBatchCatalog();
		GBatch.Worker.Reset();
		GBatch.Ticker.Reset();
		UE_LOG(LogTemp, Display, TEXT("OK %s"), *GBatch.Status());
		return false;
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdBatch(TEXT("OKAnim.BatchKK"),
		TEXT("OKAnim.BatchKK [all|*|source] [filter|*] [max] [sex=male|female]: converts KK clips of the character to .okanim in the background (Saved/OKAnim/KK/<sex>/<source hash>/, catalog.json; existing files are kept). all = every source of the character's sex, * = the source of the playing clip. OKAnim.BatchStatus / OKAnim.BatchCancel."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& InArgs, UWorld* World, FOutputDevice& Out)
		{
			if (GBatch.IsRunning()) { Out.Logf(TEXT("already %s"), *GBatch.Status()); return; }
			// sex=male|female picks a loaded character of that sex (e.g. the pair's male); otherwise the single test character.
			TArray<FString> Args;
			int32 WantSex = -1;
			for (const FString& Arg : InArgs)
			{
				if (Arg.StartsWith(TEXT("sex="))) WantSex = Arg.EndsWith(TEXT("male")) && !Arg.EndsWith(TEXT("female")) ? 0 : 1;
				else Args.Add(Arg);
			}
			AKKPreviewActor* A = nullptr;
			if (WantSex < 0) A = SingleCharacter(World);
			else for (TActorIterator<AKKPreviewActor> It(World); It && !A; ++It) if (It->Session.IsOpen() && CharacterSex(*It) == WantSex) A = *It;
			if (!A) { Out.Logf(TEXT("no character")); return; }
			const FString Mode = Args.IsValidIndex(0) ? Args[0] : FString(TEXT("*"));
			const FString Filter = Args.IsValidIndex(1) && Args[1] != TEXT("*") ? Args[1] : FString();
			const int32 Max = Args.IsValidIndex(2) ? FMath::Max(1, FCString::Atoi(*Args[2])) : MAX_int32;
			const int32 Sex = CharacterSex(A);
			TArray<FString> Sources;
			if (Mode == TEXT("all"))
			{
				Sources = A->Session.ClipSources();
				if (UKKAnimationCatalog* Catalog = UKKAnimationCatalog::LoadDefaultCatalog())
					for (const FString& Source : Catalog->GetAnimationSources()) Sources.AddUnique(Source);
				Sources.RemoveAll([Sex](const FString& Source) { return Source == TEXT("video:") || !SourceMatchesSex(Source, Sex); });
			}
			else if (Mode == TEXT("*"))
			{
				FString Error, Source;
				const UOKAnimSequence* Current = UOKAnimationLibrary::ConvertKKClip(A, GTest.SingleClip, 30.f, nullptr, TEXT("OKBatchCurrent"), Error);
				if (!Current || !Current->Meta.FindRef(TEXT("source_key")).Split(TEXT("|"), &Source, nullptr)) { Out.Logf(TEXT("no current clip source: %s"), *Error); return; }
				Sources.Add(Source);
			}
			else Sources.Add(Mode);
			GBatch = FKKBatch();
			GBatch.Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OKAnim"), TEXT("KK"), Sex == 0 ? TEXT("male") : TEXT("female")));
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const FString& Source : Sources)
				for (const FString& Label : A->Session.ClipNamesForSource(Source))
				{
					if (Items.Num() >= Max) break;
					const FString Key = Source + TEXT("|") + Label;
					if (!Filter.IsEmpty() && !Key.Contains(Filter)) continue;
					TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
					Item->SetStringField(TEXT("key"), Key);
					Item->SetStringField(TEXT("source"), Source);
					Item->SetStringField(TEXT("file"), FPaths::Combine(GBatch.Dir, FString::Printf(TEXT("%08x"), FCrc::StrCrc32(*Source)), FileNameFor(Key) + TEXT(".okanim")));
					Items.Add(MakeShared<FJsonValueObject>(Item));
				}
			FString ItemsJson;
			FJsonSerializer::Serialize(Items, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ItemsJson));
			GBatch.Total = Items.Num();
			GBatch.Worker = MakeShared<FOkAnimBatch>();
			const int32 Threads = FMath::Clamp(CVarBatchThreads.GetValueOnGameThread(), 1, 32);
			if (!GBatch.Worker->Start(A->Session, ItemsJson, 30.f, Threads)) { Out.Logf(TEXT("batch start failed: %s"), *FOkAnimBridge::LastError()); GBatch.Worker.Reset(); return; }
			GBatch.Start = FPlatformTime::Seconds();
			GBatch.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) { return BatchTick(); }), 0.25f);
			Out.Logf(TEXT("started: %d sources, %d clips, %d threads, sex=%s -> %s"), Sources.Num(), GBatch.Total, Threads, Sex == 0 ? TEXT("male") : TEXT("female"), *GBatch.Dir);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdBatchStatus(TEXT("OKAnim.BatchStatus"), TEXT("Progress of OKAnim.BatchKK."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>&, UWorld*, FOutputDevice& Out) { Out.Logf(TEXT("%s"), *GBatch.Status()); }));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdBatchCancel(TEXT("OKAnim.BatchCancel"), TEXT("Stops OKAnim.BatchKK (files written so far are kept)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>&, UWorld*, FOutputDevice& Out)
		{
			if (GBatch.IsRunning() && GBatch.Worker)
			{
				// Stop the workers, keep what they finished (catalog included).
				GBatch.Worker->Cancel();
				FTSTicker::GetCoreTicker().RemoveTicker(GBatch.Ticker);
				GBatch.Worker.Reset();   // waits for the clips in progress
				GBatch.Ticker.Reset();
				WriteBatchCatalog();
			}
			Out.Logf(TEXT("%s"), *GBatch.Status());
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdSavePair(TEXT("OKAnim.SavePair"),
		TEXT("OKAnim.SavePair [label]: converts the playing KK paired state to an OK group package (Saved/OKAnim/Pairs/<state>.okgroup) and adds it to Pairs/catalog.jsonl; existing files are kept."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			AKKPairedPlayback* Pair = ActivePair(World);
			if (!Pair) { Out.Logf(TEXT("no paired playback")); return; }
			const FString File = FOkAnimBridge::PairPackagePath(Pair->GetFemaleClip(), Pair->GetMaleClip(), Pair->GetState());
			const FString Dir = FPaths::GetPath(File);
			if (IFileManager::Get().FileExists(*File)) { Out.Logf(TEXT("exists %s"), *File); return; }
			const double T0 = FPlatformTime::Seconds();
			TArray<uint8> Package;
			if (!FOkAnimBridge::GroupFromPair(Pair->FemaleActor->Session, Pair->MaleActor->Session, Pair->GetState(), Pair->GetFemaleMotion(), Pair->GetMaleMotion(), 30.f, Package))
			{
				Out.Logf(TEXT("failed: %s"), *FOkAnimBridge::LastError());
				return;
			}
			FString Spec;
			TArray<TArray<uint8>> Anims;
			FOkAnimBridge::ParseGroupPackage(Package, Spec, Anims);
			// Each character's face during this state, as constant curves of its role animation.
			for (int32 Role = 0; Role < Anims.Num() && Role < 2; ++Role)
			{
				AKKPreviewActor* Actor = Role == 0 ? Pair->FemaleActor.Get() : Pair->MaleActor.Get();
				FString Info;
				if (!FOkAnimBridge::Info(Anims[Role], Info)) continue;
				const int32 Frames = FCString::Atoi(*Info.Mid(Info.Find(TEXT("\"frames\":")) + 9));
				FString Curves = TEXT("[");
				for (const auto& C : FOkAnimPlayer::FaceCurvesOf(*Actor))
				{
					Curves += FString::Printf(TEXT("%s{\"name\":\"%s\",\"values\":["), Curves.Len() > 1 ? TEXT(",") : TEXT(""), *C.Key);
					for (int32 F = 0; F < Frames; ++F) Curves += FString::Printf(TEXT("%s%.4f"), F ? TEXT(",") : TEXT(""), C.Value);
					Curves += TEXT("]}");
				}
				Curves += TEXT("]");
				TArray<uint8> Edited;
				if (Frames > 0 && FOkAnimBridge::Edit(Anims[Role], FString::Printf(TEXT("{\"curves\":%s}"), *Curves), Edited)) Anims[Role] = MoveTemp(Edited);
			}
			// Repack with the edited role animations.
			{
				TArray<uint8> Repacked;
				const FTCHARToUTF8 SpecUtf(*Spec);
				Repacked.Append(reinterpret_cast<const uint8*>("OKGROUP1"), 8);
				auto U32 = [&](uint32 V) { Repacked.Append(reinterpret_cast<const uint8*>(&V), 4); };
				U32(uint32(SpecUtf.Length()));
				Repacked.Append(reinterpret_cast<const uint8*>(SpecUtf.Get()), SpecUtf.Length());
				U32(uint32(Anims.Num()));
				for (const TArray<uint8>& A : Anims) { U32(uint32(A.Num())); Repacked.Append(A); }
				Package = MoveTemp(Repacked);
			}
			int32 Contacts = 0;
			{
				TSharedPtr<FJsonObject> J;
				const auto Reader = TJsonReaderFactory<>::Create(Spec);
				if (FJsonSerializer::Deserialize(Reader, J) && J) Contacts = J->GetArrayField(TEXT("contacts")).Num();
			}
			if (!FFileHelper::SaveArrayToFile(Package, *File)) { Out.Logf(TEXT("cannot write %s"), *File); return; }
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("file"), FPaths::GetCleanFilename(File));
			Entry->SetStringField(TEXT("label"), Args.Num() ? FString::Join(Args, TEXT(" ")) : FString());
			Entry->SetStringField(TEXT("state"), Pair->GetState());
			Entry->SetStringField(TEXT("female"), Pair->GetFemaleClip());
			Entry->SetStringField(TEXT("male"), Pair->GetMaleClip());
			Entry->SetStringField(TEXT("female_motion"), Pair->GetFemaleMotion());
			Entry->SetStringField(TEXT("male_motion"), Pair->GetMaleMotion());
			Entry->SetNumberField(TEXT("contacts"), Contacts);
			FString Line;
			const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
			FJsonSerializer::Serialize(Entry, Writer);
			FFileHelper::SaveStringToFile(Line + TEXT("\n"), *FPaths::Combine(Dir, TEXT("catalog.jsonl")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
			Out.Logf(TEXT("saved %s contacts=%d in %.0f ms"), *FPaths::GetCleanFilename(File), Contacts, (FPlatformTime::Seconds() - T0) * 1000.);
		}));

	/** World directions of the main limb segments (bone -> child) of a session's current pose. */
	TMap<FString, FVector> LimbDirections(FOkSession& S)
	{
		static const TCHAR* Segments[][2] = {
			{ TEXT("cf_j_hips"), TEXT("cf_j_spine01") }, { TEXT("cf_j_spine02"), TEXT("cf_j_spine03") }, { TEXT("cf_j_spine03"), TEXT("cf_j_neck") }, { TEXT("cf_j_neck"), TEXT("cf_j_head") },
			{ TEXT("cf_j_arm00_L"), TEXT("cf_j_forearm01_L") }, { TEXT("cf_j_forearm01_L"), TEXT("cf_j_hand_L") }, { TEXT("cf_j_arm00_R"), TEXT("cf_j_forearm01_R") }, { TEXT("cf_j_forearm01_R"), TEXT("cf_j_hand_R") },
			{ TEXT("cf_j_thigh00_L"), TEXT("cf_j_leg01_L") }, { TEXT("cf_j_leg01_L"), TEXT("cf_j_leg03_L") }, { TEXT("cf_j_thigh00_R"), TEXT("cf_j_leg01_R") }, { TEXT("cf_j_leg01_R"), TEXT("cf_j_leg03_R") } };
		TMap<FString, FVector> Out;
		for (const auto& Seg : Segments)
		{
			float A[16], B[16];
			if (!S.BoneWorldMatrix(Seg[0], A) || !S.BoneWorldMatrix(Seg[1], B)) continue;
			const FVector D = FVector(B[3] - A[3], B[7] - A[7], B[11] - A[11]).GetSafeNormal();
			if (!D.IsNearlyZero()) Out.Add(FString(Seg[0]) + TEXT(">") + Seg[1], D);
		}
		return Out;
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdCross(TEXT("OKAnim.ValidateCross"),
		TEXT("OKAnim.ValidateCross [samples]: the female's playing clip converted to OK and played on the male: main limb directions compared with the female's native pose."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			AKKPairedPlayback* Pair = GTest.Pair.IsValid() ? GTest.Pair.Get() : ActivePair(World);
			if (!Pair) { Out.Logf(TEXT("needs a paired playback (both characters loaded)")); return; }
			AKKPreviewActor* Female = Pair->FemaleActor;
			AKKPreviewActor* Male = Pair->MaleActor;
			FString Error;
			UOKAnimSequence* Seq = UOKAnimationLibrary::ConvertKKClip(Female, FString(), 30.f, nullptr, TEXT("OKCrossClip"), Error);
			const TSharedPtr<FOkAnimPlayer> Player = Seq ? Seq->CreatePlayer(Error) : nullptr;
			if (!Player) { Out.Logf(TEXT("convert failed: %s"), *Error); return; }
			Pair->Stop();
			GTest.Pair = Pair;
			const int32 Samples = FMath::Clamp(int32(ArgFloat(Args, 0, 12.f)), 2, 200);
			double Max = 0., Sum = 0.;
			int32 Count = 0;
			FString Worst;
			for (int32 K = 0; K < Samples; ++K)
			{
				const float T = Seq->Duration * K / Samples;
				if (!Female->Session.ApplyAnimFrame(T, 0.f)) { Out.Logf(TEXT("native frame failed")); break; }
				const TMap<FString, FVector> Native = LimbDirections(Female->Session);
				FString Report;
				if (!Player->ApplySessions({ &Male->Session }, T, 0.f, &Report)) { Out.Logf(TEXT("OK frame failed: %s"), *Report); break; }
				const TMap<FString, FVector> Ok = LimbDirections(Male->Session);
				for (const auto& P : Native)
				{
					const FVector* Q = Ok.Find(P.Key);
					if (!Q) continue;
					const double Deg = FMath::RadiansToDegrees(FMath::Atan2(FVector::CrossProduct(P.Value, *Q).Size(), FVector::DotProduct(P.Value, *Q)));
					Sum += Deg;
					++Count;
					if (Deg > Max) { Max = Deg; Worst = FString::Printf(TEXT("%s@%.2f"), *P.Key, T); }
				}
			}
			FString Restart;
			Pair->Restart(Restart);
			GTest.Pair.Reset();
			Out.Logf(TEXT("cross-sex %s: samples=%d segments=%d max=%.2fdeg mean=%.3fdeg worst=%s"), *Seq->Meta.FindRef(TEXT("name")), Samples, Count, Max, Count ? Sum / Count : 0., *Worst);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdShape(TEXT("OKAnim.Shape"),
		TEXT("OKAnim.Shape <female|male> <body index> <value>: changes a body shape value (0 = height) of a character, live (tests size adaptation)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			AKKPairedPlayback* Pair = GTest.Pair.IsValid() ? GTest.Pair.Get() : ActivePair(World);
			AKKPreviewActor* A = Pair && Args.Num() ? (Args[0] == TEXT("male") ? Pair->MaleActor.Get() : Pair->FemaleActor.Get()) : SingleCharacter(World);
			if (!A || Args.Num() < 3) { Out.Logf(TEXT("usage: OKAnim.Shape <female|male> <index> <value>")); return; }
			TArray<float> Body = A->Session.ShapeBody();
			const int32 Index = FCString::Atoi(*Args[1]);
			if (!Body.IsValidIndex(Index)) { Out.Logf(TEXT("body has %d values"), Body.Num()); return; }
			const float Old = Body[Index];
			Body[Index] = FCString::Atof(*Args[2]);
			A->MarkShapeDirty(A->Session.ShapeFace(), Body);
			Out.Logf(TEXT("%s body[%d] %.3f -> %.3f"), *A->GetName(), Index, Old, Body[Index]);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdPosture(TEXT("OKAnim.Posture"),
		TEXT("OKAnim.Posture <0|1>: posture adaptation (crouch / lean / head turn for out-of-reach contacts) of the OK players."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Out)
		{
			const bool bOn = !Args.Num() || Args[0] != TEXT("0");
			const bool bWas = FOkAnimBridge::SetPosture(bOn);
			Out.Logf(TEXT("posture %d (was %d)"), bOn, bWas);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdProfile(TEXT("OKAnim.Profile"),
		TEXT("OKAnim.Profile [reset]: per-frame cost of OK playback phases (pose, placement, walk, posture, IK, feet, finishing: body dynamics, clothes, export, accessories) and of the pair's whole advance, since the last reset."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			const bool bReset = Args.Num() && Args[0] == TEXT("reset");
			AKKPairedPlayback* Pair = ActivePair(World);
			if (bReset)
			{
				FOkAnimBridge::Profile(true);
				if (Pair) { Pair->bMeasureAdvance = true; Pair->ResetAdvanceMetrics(); }
				Out.Logf(TEXT("profile reset"));
				return;
			}
			TSharedPtr<FJsonObject> J;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FOkAnimBridge::Profile(false)), J) || !J) { Out.Logf(TEXT("no profile")); return; }
			const TArray<TSharedPtr<FJsonValue>>* Total = nullptr;
			const double Frames = J->TryGetArrayField(TEXT("apply_total"), Total) && Total->Num() == 2 ? FMath::Max(1., (*Total)[1]->AsNumber()) : 1.;
			FString Line = FString::Printf(TEXT("%d frames, ms per frame:"), int32(Frames));
			for (const auto& P : J->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>& V = P.Value->AsArray();
				if (V.Num() == 2) Line += FString::Printf(TEXT(" %s=%.3f"), *P.Key, V[0]->AsNumber() / 1000. / Frames);
			}
			if (Pair && Pair->AdvanceSamples) Line += FString::Printf(TEXT(" | pair advance %.3f ms (max %.3f) over %llu"), Pair->AdvanceTotalMs / Pair->AdvanceSamples, Pair->AdvanceMaximumMs, Pair->AdvanceSamples);
			Out.Logf(TEXT("%s"), *Line);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdParallel(TEXT("OKAnim.ParallelFinish"),
		TEXT("OKAnim.ParallelFinish <0|1>: the characters' finishing passes (shape, ABMX, dynamics) of OK group playback on worker threads."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Out)
		{
			const bool bOn = Args.Num() && Args[0] == TEXT("1");
			Out.Logf(TEXT("parallel finish %d (was %d)"), bOn, FOkAnimBridge::SetParallelFinish(bOn));
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdMatrix(TEXT("OKAnim.Matrix"),
		TEXT("OKAnim.Matrix [samples]: the running pair's OK group on 4 x 4 body heights (female x male), posture off and on: contact error (cm), posture moves, foot steps; worst pair listed. Report in Saved/OKAnim/matrix.txt."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			AKKPairedPlayback* Pair = GTest.Pair.IsValid() ? GTest.Pair.Get() : ActivePair(World);
			if (!Pair || !IsValid(Pair->FemaleActor) || !IsValid(Pair->MaleActor)) { Out.Logf(TEXT("no paired playback")); return; }
			TArray<uint8> Package;
			const FString File = FOkAnimBridge::PairPackagePath(Pair->GetFemaleClip(), Pair->GetMaleClip(), Pair->GetState());
			FString Spec;
			TArray<TArray<uint8>> Anims;
			TSharedPtr<FOkAnimPlayer> Player = MakeShared<FOkAnimPlayer>();
			if (!FFileHelper::LoadFileToArray(Package, *File) || !FOkAnimBridge::ParseGroupPackage(Package, Spec, Anims) || !Player->Create(Spec, Anims))
			{
				Out.Logf(TEXT("no OK group for this state (%s): run OKAnim.SavePair first"), *File);
				return;
			}
			const int32 Samples = FMath::Clamp(int32(ArgFloat(Args, 0, 8.f)), 2, 60);
			AKKPreviewActor* F = Pair->FemaleActor;
			AKKPreviewActor* M = Pair->MaleActor;
			const TArray<float> FBody = F->Session.ShapeBody(), MBody = M->Session.ShapeBody();
			const bool bPaused = Pair->IsPaused();
			Pair->SetPaused(true);
			const float Heights[] = { 0.f, 0.33f, 0.66f, 1.f };
			FString Table = FString::Printf(TEXT("OK group %s on 4 x 4 body heights, %d samples; contact error max / mean (cm), posture off -> on\n"), *Pair->GetState(), Samples);
			float WorstOn = -1.f, SumOff = 0.f, SumOn = 0.f;
			FString Worst;
			for (const float Fh : Heights)
				for (const float Mh : Heights)
				{
					TArray<float> Fb = FBody, Mb = MBody;
					if (Fb.Num()) Fb[0] = Fh;
					if (Mb.Num()) Mb[0] = Mh;
					F->ApplyShapes(F->Session.ShapeFace(), Fb);
					M->ApplyShapes(M->Session.ShapeFace(), Mb);
					float Max[2] = { 0.f, 0.f }, Mean[2] = { 0.f, 0.f };
					int32 Moves = 0, Steps = 0;
					FString WorstContact;
					for (int32 On = 0; On < 2; ++On)
					{
						FOkAnimBridge::SetPosture(On == 1);
						int32 N = 0;
						for (int32 K = 0; K < Samples; ++K)
						{
							FString Report;
							if (!Player->ApplySessions({ &F->Session, &M->Session }, Player->Duration() * K / Samples, 0.f, &Report)) continue;
							TSharedPtr<FJsonObject> J;
							if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Report), J) || !J) continue;
							const float CellMax = float(J->GetNumberField(TEXT("contact_max"))) * 100.f;
							if (On && CellMax > Max[On]) WorstContact = J->GetStringField(TEXT("worst"));
							Max[On] = FMath::Max(Max[On], CellMax);
							Mean[On] += float(J->GetNumberField(TEXT("contact_mean"))) * 100.f;
							if (On) { Moves += J->GetIntegerField(TEXT("posture")); Steps += J->GetIntegerField(TEXT("steps")); }
							++N;
						}
						Mean[On] /= FMath::Max(1, N);
					}
					SumOff += Mean[0];
					SumOn += Mean[1];
					Table += FString::Printf(TEXT("female %.2f male %.2f: max %5.1f -> %5.1f  mean %5.2f -> %5.2f  posture moves %d  steps %d  worst %s\n"), Fh, Mh, Max[0], Max[1], Mean[0], Mean[1], Moves, Steps, *WorstContact);
					if (Max[1] > WorstOn) { WorstOn = Max[1]; Worst = FString::Printf(TEXT("female %.2f male %.2f (%.1f cm)"), Fh, Mh, Max[1]); }
				}
			FOkAnimBridge::SetPosture(true);
			F->ApplyShapes(F->Session.ShapeFace(), FBody);
			M->ApplyShapes(M->Session.ShapeFace(), MBody);
			Pair->SetPaused(bPaused);
			Table += FString::Printf(TEXT("mean contact error over the matrix: posture off %.2f cm, on %.2f cm; worst %s\n"), SumOff / 16.f, SumOn / 16.f, *Worst);
			FFileHelper::SaveStringToFile(Table, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OKAnim"), TEXT("matrix.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			Out.Logf(TEXT("matrix: mean contact error posture off %.2f cm -> on %.2f cm; worst %s (Saved/OKAnim/matrix.txt)"), SumOff / 16.f, SumOn / 16.f, *Worst);
		}));

	TWeakObjectPtr<AKKPreviewActor> GTrioActor;

	/** OKGROUP1 package bytes from a spec and role animations. */
	TArray<uint8> PackGroup(const FString& Spec, const TArray<TArray<uint8>>& Anims)
	{
		TArray<uint8> Out;
		const FTCHARToUTF8 SpecUtf(*Spec);
		Out.Append(reinterpret_cast<const uint8*>("OKGROUP1"), 8);
		auto U32 = [&](uint32 V) { Out.Append(reinterpret_cast<const uint8*>(&V), 4); };
		U32(uint32(SpecUtf.Length()));
		Out.Append(reinterpret_cast<const uint8*>(SpecUtf.Get()), SpecUtf.Length());
		U32(uint32(Anims.Num()));
		for (const TArray<uint8>& A : Anims) { U32(uint32(A.Num())); Out.Append(A); }
		return Out;
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdTrio(TEXT("OKAnim.Trio"),
		TEXT("OKAnim.Trio [other.okgroup]: three roles from two paired states: the running pair's group (female, male) plus the male role of another saved group (its contacts on the same female), on a third character loaded from a demo card."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			AKKPairedPlayback* Pair = GTest.Pair.IsValid() ? GTest.Pair.Get() : ActivePair(World);
			if (!Pair) { Out.Logf(TEXT("no paired playback")); return; }
			const FString FileA = FOkAnimBridge::PairPackagePath(Pair->GetFemaleClip(), Pair->GetMaleClip(), Pair->GetState());
			FString FileB = Args.Num() ? Args[0] : FString();
			if (FileB.IsEmpty())
			{
				TArray<FString> Files;
				IFileManager::Get().FindFiles(Files, *(FPaths::GetPath(FileA) / TEXT("*.okgroup")), true, false);
				Files.Sort();
				for (const FString& F : Files) if (!FPaths::GetCleanFilename(FileA).Equals(F)) { FileB = FPaths::GetPath(FileA) / F; break; }
			}
			TArray<uint8> PackA, PackB;
			FString SpecA, SpecB;
			TArray<TArray<uint8>> AnimsA, AnimsB;
			if (!FFileHelper::LoadFileToArray(PackA, *FileA) || !FFileHelper::LoadFileToArray(PackB, *FileB) || !FOkAnimBridge::ParseGroupPackage(PackA, SpecA, AnimsA) || !FOkAnimBridge::ParseGroupPackage(PackB, SpecB, AnimsB) || AnimsA.Num() < 2 || AnimsB.Num() < 2)
			{
				Out.Logf(TEXT("needs two saved groups (%s, %s): OKAnim.SavePair"), *FileA, *FileB);
				return;
			}
			TSharedPtr<FJsonObject> JA, JB;
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SpecA), JA);
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SpecB), JB);
			if (!JA || !JB) { Out.Logf(TEXT("bad group spec")); return; }
			// Roles: A's two, then B's male (its animation index 2), standing aside so the two partners do not overlap.
			TArray<TSharedPtr<FJsonValue>> Roles = JA->GetArrayField(TEXT("roles"));
			TSharedRef<FJsonObject> Third = MakeShared<FJsonObject>();
			Third->SetStringField(TEXT("name"), TEXT("male2"));
			TSharedRef<FJsonObject> ThirdSpec = MakeShared<FJsonObject>();
			ThirdSpec->SetStringField(TEXT("type"), TEXT("anim"));
			ThirdSpec->SetNumberField(TEXT("anim"), 2);
			Third->SetObjectField(TEXT("spec"), ThirdSpec);
			Third->SetArrayField(TEXT("offset"), { MakeShared<FJsonValueNumber>(0.9), MakeShared<FJsonValueNumber>(0.), MakeShared<FJsonValueNumber>(0.) });
			Roles.Add(MakeShared<FJsonValueObject>(Third));
			// Contacts: A's as they are; B's with its male (role 1) becoming role 2.
			TArray<TSharedPtr<FJsonValue>> Contacts = JA->GetArrayField(TEXT("contacts"));
			for (const TSharedPtr<FJsonValue>& V : JB->GetArrayField(TEXT("contacts")))
			{
				TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>(*V->AsObject());
				auto Remap = [](int32 R) { return R == 1 ? 2 : R; };
				C->SetNumberField(TEXT("role"), Remap(C->GetIntegerField(TEXT("role"))));
				const int32 T = C->GetIntegerField(TEXT("target_role"));
				if (T >= 0) C->SetNumberField(TEXT("target_role"), Remap(T));
				Contacts.Add(MakeShared<FJsonValueObject>(C));
			}
			TSharedRef<FJsonObject> Spec = MakeShared<FJsonObject>();
			Spec->SetStringField(TEXT("type"), TEXT("group"));
			Spec->SetArrayField(TEXT("roles"), Roles);
			Spec->SetArrayField(TEXT("contacts"), Contacts);
			FString SpecText;
			FJsonSerializer::Serialize(Spec, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SpecText));
			UOKGroupAnimSequence* Group = NewObject<UOKGroupAnimSequence>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UOKGroupAnimSequence::StaticClass(), TEXT("OKTrio")));
			FString Error;
			if (!Group->SetFromPackage(PackGroup(SpecText, { AnimsA[0], AnimsA[1], AnimsB[1] }), Group, Error)) { Out.Logf(TEXT("trio group: %s"), *Error); return; }
			// The third character: a demo card, next to the male.
			AKKPreviewActor* Third3 = GTrioActor.Get();
			if (!Third3)
			{
				TArray<FString> Cards;
				IFileManager::Get().FindFiles(Cards, *(FPaths::ProjectContentDir() / TEXT("CharacterCards/Demo/*.png")), true, false);
				Cards.Sort();
				FOkSession Session;
				const FString Card = Cards.Num() ? FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("CharacterCards/Demo") / Cards.Last()) : FString();
				const FString Portable = UKKCardWorkspace::PortableSourcePath(Card);
				if (Card.IsEmpty() || !Session.Open(FString(), Portable.IsEmpty() ? Card : Portable, 0, Error)) { Out.Logf(TEXT("third character %s: %s"), *Card, *Error); return; }
				Third3 = World->SpawnActor<AKKPreviewActor>(Pair->MaleActor->GetActorLocation(), Pair->MaleActor->GetActorRotation());
				Third3->Session = MoveTemp(Session);
				if (!Third3->LoadSession(Error)) { Third3->Destroy(); Out.Logf(TEXT("third character: %s"), *Error); return; }
				Third3->SetActorTransform(Pair->FemaleActor->GetActorTransform());
				GTrioActor = Third3;
			}
			if (AOKGroupPlayer* G = GTest.Group.Get()) { G->Stop(); G->Destroy(); }
			GTest.Pair = Pair;
			GTest.GroupAsset.Reset(Group);
			Pair->Stop();
			AOKGroupPlayer* Player = UOKAnimationLibrary::PlayGroup(World, Group, { Pair->FemaleActor.Get(), Pair->MaleActor.Get(), Third3 }, Error);
			if (!Player) { Out.Logf(TEXT("trio play failed: %s"), *Error); StopAll(Out); return; }
			GTest.Group = Player;
			Out.Logf(TEXT("trio: roles=%d contacts=%d (%d + %d) from %s + %s"), Group->Roles.Num(), Group->Contacts.Num(), JA->GetArrayField(TEXT("contacts")).Num(), JB->GetArrayField(TEXT("contacts")).Num(),
				*FPaths::GetBaseFilename(FileA), *FPaths::GetBaseFilename(FileB));
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdWalkTest(TEXT("OKAnim.WalkTest"),
		TEXT("OKAnim.WalkTest: the pair's OK group frame by frame while the male's height changes fast (the placement moves him): walk weights and leg turn, against a still run."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>&, UWorld* World, FOutputDevice& Out)
		{
			AKKPairedPlayback* Pair = GTest.Pair.IsValid() ? GTest.Pair.Get() : ActivePair(World);
			if (!Pair || !IsValid(Pair->FemaleActor) || !IsValid(Pair->MaleActor)) { Out.Logf(TEXT("no paired playback")); return; }
			const TArray<uint8>& Walk = UOKAnimationAsset::DefaultWalk();
			TArray<uint8> Package;
			FString Spec;
			TArray<TArray<uint8>> Anims;
			TSharedPtr<FOkAnimPlayer> Player = MakeShared<FOkAnimPlayer>();
			const FString File = FOkAnimBridge::PairPackagePath(Pair->GetFemaleClip(), Pair->GetMaleClip(), Pair->GetState());
			// The male starts half a hips height behind where the group wants him: he walks in.
			bool bSpec = !Walk.IsEmpty() && FFileHelper::LoadFileToArray(Package, *File) && FOkAnimBridge::ParseGroupPackage(Package, Spec, Anims);
			if (bSpec)
			{
				TSharedPtr<FJsonObject> J;
				if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Spec), J) && J && J->GetArrayField(TEXT("roles")).Num() > 1)
				{
					J->GetArrayField(TEXT("roles"))[1]->AsObject()->SetArrayField(TEXT("offset"), { MakeShared<FJsonValueNumber>(0.), MakeShared<FJsonValueNumber>(0.), MakeShared<FJsonValueNumber>(0.5) });
					Spec.Reset();
					FJsonSerializer::Serialize(J.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Spec));
				}
			}
			if (!bSpec || !Player->Create(Spec, Anims) || !Player->SetWalk(Walk))
			{
				Out.Logf(TEXT("needs a walk clip (%d bytes) and this state's OK group %s"), Walk.Num(), *File);
				return;
			}
			AKKPreviewActor* M = Pair->MaleActor;
			AKKPreviewActor* F = Pair->FemaleActor;
			const TArray<float> Body = M->Session.ShapeBody();
			const bool bPaused = Pair->IsPaused();
			Pair->SetPaused(true);
			// 40 frames: he walks in (weight > 0) and stops walking once there.
			const TArray<float> FBody = F->Session.ShapeBody();
			float Before = 0.f, After = 0.f, Last = 0.f;
			FString Trace;
			for (int32 K = 0; K < 40; ++K)
			{
				FString Report;
				if (!Player->ApplySessions({ &F->Session, &M->Session }, K / 30.f, 1.f / 30.f, &Report)) continue;
				TSharedPtr<FJsonObject> J;
				if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Report), J) || !J) continue;
				const float W = float(J->GetNumberField(TEXT("walk")));
				if (K < 5) Before = FMath::Max(Before, W); else After = FMath::Max(After, W);
				Last = W;
				if (K % 4 == 0) Trace += FString::Printf(TEXT(" %d:%.2f"), K, W);
			}
			F->ApplyShapes(F->Session.ShapeFace(), FBody);
			M->ApplyShapes(M->Session.ShapeFace(), Body);
			Pair->SetPaused(bPaused);
			Out.Logf(TEXT("walk weight: first frames %.2f, later %.2f, at the end %.2f;%s (walk clip %d bytes)"), Before, After, Last, *Trace, Walk.Num());
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdGroupBlend(TEXT("OKAnim.GroupBlend"),
		TEXT("OKAnim.GroupBlend <x 0..1> [other.okgroup]: a 1D blend space of two paired OK groups (this state at 0, another at 1) played on the pair; OKAnim.Param X <v> moves it, OKAnim.Report shows the contacts."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			AKKPairedPlayback* Pair = GTest.Pair.IsValid() ? GTest.Pair.Get() : ActivePair(World);
			if (!Pair) { Out.Logf(TEXT("no paired playback")); return; }
			const FString FileA = FOkAnimBridge::PairPackagePath(Pair->GetFemaleClip(), Pair->GetMaleClip(), Pair->GetState());
			FString FileB = Args.Num() > 1 ? Args[1] : FString();
			if (FileB.IsEmpty())
			{
				TArray<FString> Files;
				IFileManager::Get().FindFiles(Files, *(FPaths::GetPath(FileA) / TEXT("*.okgroup")), true, false);
				Files.Sort();
				for (const FString& F : Files) if (!FPaths::GetCleanFilename(FileA).Equals(F)) { FileB = FPaths::GetPath(FileA) / F; break; }
			}
			FString Error;
			UOKGroupAnimSequence* A = UOKAnimationLibrary::LoadOkGroupFile(FileA, GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UOKGroupAnimSequence::StaticClass(), TEXT("OKBlendA")), Error);
			UOKGroupAnimSequence* B = A ? UOKAnimationLibrary::LoadOkGroupFile(FileB, GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UOKGroupAnimSequence::StaticClass(), TEXT("OKBlendB")), Error) : nullptr;
			if (!A || !B) { Out.Logf(TEXT("needs two saved groups: %s"), *Error); return; }
			UOKBlendSpace* Blend = NewObject<UOKBlendSpace>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UOKBlendSpace::StaticClass(), TEXT("OKGroupBlend")), RF_Transient);
			Blend->Axes.Add({ TEXT("X"), 0.f, 1.f });
			Blend->Samples.Add({ A, FVector2D(0, 0) });
			Blend->Samples.Add({ B, FVector2D(1, 0) });
			if (AOKGroupPlayer* G = GTest.Group.Get()) { G->Stop(); G->Destroy(); }
			GTest.Pair = Pair;
			GTest.GroupAsset.Reset(Blend);
			Pair->Stop();
			AOKGroupPlayer* Player = UOKAnimationLibrary::PlayGroup(World, Blend, { Pair->FemaleActor.Get(), Pair->MaleActor.Get() }, Error);
			if (!Player) { Out.Logf(TEXT("group blend play failed: %s"), *Error); StopAll(Out); return; }
			Player->SetParameter(TEXT("X"), ArgFloat(Args, 0, 0.5f));
			GTest.Group = Player;
			Out.Logf(TEXT("group blend X=%.2f roles=%d: %s (%d contacts) <-> %s (%d contacts), length %.2fs"), ArgFloat(Args, 0, 0.5f), Blend->GetRoleCount(), *FPaths::GetBaseFilename(FileA), A->Contacts.Num(),
				*FPaths::GetBaseFilename(FileB), B->Contacts.Num(), Blend->GetPlayLength());
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdDump(TEXT("OKAnim.DumpUnreal"),
		TEXT("OKAnim.DumpUnreal <AnimSequence path> [time]: writes the OK header of its skeleton and one frame (locals, positions) to Saved/OKAnim/debug."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*, FOutputDevice& Out)
		{
			UAnimSequence* Seq = Args.Num() ? LoadObject<UAnimSequence>(nullptr, *Args[0]) : nullptr;
			if (!Seq || !Seq->GetSkeleton()) { Out.Logf(TEXT("needs an AnimSequence path")); return; }
			FOKUnrealSkeleton Binding;
			FString Error;
			if (!Binding.Build(Seq->GetSkeleton()->GetReferenceSkeleton(), nullptr, UOKAnimationLibrary::GetDefaultFamily(), Error)) { Out.Logf(TEXT("%s"), *Error); return; }
			TArray<FTransform> Local;
			Binding.SampleSource(Seq, ArgFloat(Args, 1, 0.f), Local);
			TArray<float> Rot, Hips;
			Rot.SetNumZeroed(Binding.BoneIndex.Num() * 4);
			Hips.SetNumZeroed(3);
			Binding.PoseToLocals(Local, Rot.GetData(), Hips.GetData());
			const TArray<FVector> Pos = Binding.HeaderPositions(Local);
			FString Frame = TEXT("{\"rot\":[");
			for (int32 I = 0; I < Rot.Num(); ++I) Frame += FString::Printf(TEXT("%s%.7f"), I ? TEXT(",") : TEXT(""), Rot[I]);
			Frame += FString::Printf(TEXT("],\"hips\":[%.7f,%.7f,%.7f],\"names\":["), Hips[0], Hips[1], Hips[2]);
			for (int32 I = 0; I < Binding.FamilyName.Num(); ++I) Frame += FString::Printf(TEXT("%s\"%s\""), I ? TEXT(",") : TEXT(""), *Binding.FamilyName[I].ToString());
			Frame += TEXT("],\"pos\":[");
			for (int32 I = 0; I < Pos.Num(); ++I) Frame += FString::Printf(TEXT("%s[%.6f,%.6f,%.6f]"), I ? TEXT(",") : TEXT(""), Pos[I].X, Pos[I].Y, Pos[I].Z);
			Frame += TEXT("]}");
			const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OKAnim"), TEXT("debug"));
			FFileHelper::SaveStringToFile(Binding.HeaderJson, *FPaths::Combine(Dir, Seq->GetName() + TEXT("_header.json")));
			FFileHelper::SaveStringToFile(Frame, *FPaths::Combine(Dir, Seq->GetName() + TEXT("_frame.json")));
			FString Map;
			for (int32 I = 0; I < Binding.BoneIndex.Num(); ++I) Map += FString::Printf(TEXT(" %s=%s"), *Binding.Ref.GetBoneName(Binding.BoneIndex[I]).ToString(), *Binding.FamilyName[I].ToString());
			Out.Logf(TEXT("dumped %d bones to %s:%s"), Binding.BoneIndex.Num(), *Dir, *Map);
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdUnreal(TEXT("OKAnim.PlayUnreal"),
		TEXT("OKAnim.PlayUnreal <AnimSequence path>: converts an Unreal animation (any mapped skeleton) and plays it on the character."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			AKKPreviewActor* A = SingleCharacter(World);
			UAnimSequence* Seq = Args.Num() ? LoadObject<UAnimSequence>(nullptr, *Args[0]) : nullptr;
			if (!A || !Seq) { Out.Logf(TEXT("needs a character and an AnimSequence path")); return; }
			if (!GTest.Single.IsValid() && A->PoseMode == 2)
			{
				// Remember the native clip to restore on Stop.
				FString Error;
				if (UOKAnimSequence* Native = UOKAnimationLibrary::ConvertKKClip(A, FString(), 30.f, nullptr, TEXT("OKNativeKey"), Error)) GTest.SingleClip = Native->Meta.FindRef(TEXT("source_key"));
				GTest.Single = A;
			}
			FString Error;
			const double T0 = FPlatformTime::Seconds();
			UOKAnimSequence* Ok = UOKAnimationLibrary::ConvertUnrealAnimation(Seq, nullptr, nullptr, 30.f, nullptr, NAME_None, Error);
			if (!Ok) { Out.Logf(TEXT("convert failed: %s"), *Error); return; }
			Out.Logf(TEXT("converted %s: %d bones %d frames %.2fs in %.0f ms (family rest %s)"), *Seq->GetName(), Ok->Bones.Num(), Ok->Frames, Ok->Duration,
				(FPlatformTime::Seconds() - T0) * 1000., UOKAnimationLibrary::GetDefaultFamily() ? TEXT("aligned") : TEXT("missing: run OKAnim.SaveFamily"));
			PlaySingle(A, Ok, Out);
		}));

	// ---- Live reload (R3.10): Saved/OKAnim/Live/<name>.okanim replaces the animation of that name wherever it plays ----
	struct FLiveWatch
	{
		TMap<FString, FDateTime> Seen;
		FTSTicker::FDelegateHandle Ticker;
		int32 Reloads = 0;
		FString Last;
	};
	FLiveWatch GLive;

	bool LiveTick()
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(UOKAnimationLibrary::LiveFolder() / TEXT("*.okanim")), true, false);
		for (const FString& F : Files)
		{
			const FString Path = UOKAnimationLibrary::LiveFolder() / F;
			const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Path);
			FDateTime& Known = GLive.Seen.FindOrAdd(F, FDateTime::MinValue());
			if (Stamp <= Known) continue;
			const bool bFirst = Known == FDateTime::MinValue();
			Known = Stamp;
			if (bFirst) continue;   // present when the game started: only later changes reload
			const FString Name = FPaths::GetBaseFilename(F);
			for (TObjectIterator<UOKAnimationComponent> It; It; ++It)
			{
				UOKAnimationComponent* C = *It;
				const UOKAnimSequence* Playing = Cast<UOKAnimSequence>(C->Animation);
				if (!IsValid(C) || !C->GetWorld() || !C->GetWorld()->IsGameWorld() || !Playing || (Playing->GetName() != Name && Playing->Meta.FindRef(TEXT("live_name")) != Name)) continue;
				FString Error;
				UOKAnimSequence* Fresh = UOKAnimationLibrary::LoadOkAnimFile(Path, GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UOKAnimSequence::StaticClass(), FName(*Name)), Error);
				if (!Fresh) { UE_LOG(LogTemp, Warning, TEXT("OK live reload %s: %s"), *Name, *Error); continue; }
				// Keep the name the watch matches on, and the playback position.
				Fresh->Meta.Add(TEXT("live_name"), Name);
				AKKPreviewActor* A = C->GetOwner() ? Cast<AKKPreviewActor>(C->GetOwner()) : nullptr;
				const float T = A ? A->AnimPhase : 0.f;
				if (A && GTest.Single.Get() == A) GTest.SingleAsset.Reset(Fresh);
				if (C->Play(Fresh, 0.f) && A) A->AnimPhase = T;
				++GLive.Reloads;
				GLive.Last = FString::Printf(TEXT("%s (%d frames)"), *Name, Fresh->Frames);
				UE_LOG(LogTemp, Display, TEXT("OK live reload %s"), *GLive.Last);
			}
		}
		return true;
	}

	struct FLiveStarter
	{
		FLiveStarter() { GLive.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) { return LiveTick(); }), 0.5f); }
		~FLiveStarter() { if (GLive.Ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(GLive.Ticker); }
	};

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdLivePlay(TEXT("OKAnim.LivePlay"),
		TEXT("OKAnim.LivePlay <name> | status: plays Saved/OKAnim/Live/<name>.okanim on the test character (it reloads when the editor saves the asset again)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			if (Args.IsEmpty() || Args[0] == TEXT("status")) { Out.Logf(TEXT("live: %d reloads, last %s, %d files watched"), GLive.Reloads, *GLive.Last, GLive.Seen.Num()); return; }
			AKKPreviewActor* A = SingleCharacter(World);
			FString Error;
			const FString Path = UOKAnimationLibrary::LiveFolder() / (Args[0] + TEXT(".okanim"));
			UOKAnimSequence* Seq = A ? UOKAnimationLibrary::LoadOkAnimFile(Path, GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UOKAnimSequence::StaticClass(), FName(*Args[0])), Error) : nullptr;
			if (!Seq) { Out.Logf(TEXT("no character or %s: %s"), *Path, *Error); return; }
			Seq->Meta.Add(TEXT("live_name"), Args[0]);
			LiveTick();   // the file as it is now is the baseline
			Out.Logf(TEXT("live %s (%d frames): "), *Args[0], Seq->Frames);
			PlaySingle(A, Seq, Out);
		}));

	// ---- OK animation library (R1.7): browse Saved/OKAnim and play on the current characters ----
	TSharedPtr<FOKLibrary> GLibrary;
	TSharedPtr<SOKLibraryBrowser> GBrowser;
	TSharedPtr<SWidget> GBrowserHost;

	FOKLibrary& Library()
	{
		if (!GLibrary) { GLibrary = MakeShared<FOKLibrary>(); GLibrary->Load(); }
		return *GLibrary;
	}

	/** Plays a library entry: a single clip on a loaded character of that sex, a pair on the running pair's characters. */
	bool PlayLibraryEntry(UWorld* World, const FOKLibraryEntry& E, FOutputDevice& Out)
	{
		FString Error;
		if (E.Kind == FOKLibraryEntry::EKind::Pair)
		{
			AKKPairedPlayback* Pair = GTest.Pair.IsValid() ? GTest.Pair.Get() : ActivePair(World);
			if (!Pair) { Out.Logf(TEXT("no paired characters")); return false; }
			UOKGroupAnimSequence* Group = UOKAnimationLibrary::LoadOkGroupFile(E.File, nullptr, NAME_None, Error);
			if (!Group) { Out.Logf(TEXT("load failed: %s"), *Error); return false; }
			if (AOKGroupPlayer* G = GTest.Group.Get()) { G->Stop(); G->Destroy(); }
			GTest.Pair = Pair;
			GTest.GroupAsset.Reset(Group);
			const float Speed = Pair->GetSpeed();
			Pair->Stop();
			AOKGroupPlayer* Player = UOKAnimationLibrary::PlayGroup(World, Group, { Pair->FemaleActor.Get(), Pair->MaleActor.Get() }, Error);
			if (!Player) { Out.Logf(TEXT("group play failed: %s"), *Error); StopAll(Out); return false; }
			Player->PlayRate = Speed;
			GTest.Group = Player;
			Out.Logf(TEXT("library pair %s: roles=%d contacts=%d length=%.2fs"), *E.Name, Group->Roles.Num(), Group->Contacts.Num(), Group->GetPlayLength());
			return true;
		}
		const int32 Sex = E.Kind == FOKLibraryEntry::EKind::Male ? 0 : 1;
		AKKPreviewActor* A = GTest.Single.IsValid() && CharacterSex(GTest.Single.Get()) == Sex ? GTest.Single.Get() : nullptr;
		if (!A)
		{
			if (GTest.Single.IsValid()) StopAll(Out);
			for (TActorIterator<AKKPreviewActor> It(World); It && !A; ++It) if (It->Session.IsOpen() && CharacterSex(*It) == Sex) A = *It;
		}
		if (!A) { Out.Logf(TEXT("no %s character loaded"), FOKLibraryEntry::KindName(E.Kind)); return false; }
		UOKAnimSequence* Seq = UOKAnimationLibrary::LoadOkAnimFile(E.File, nullptr, NAME_None, Error);
		if (!Seq) { Out.Logf(TEXT("load failed: %s"), *Error); return false; }
		Out.Logf(TEXT("library %s %s %.2fs: "), FOKLibraryEntry::KindName(E.Kind), *E.Name, Seq->Duration);
		return PlaySingle(A, Seq, Out);
	}

	void CloseBrowser()
	{
		if (GBrowserHost && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(GBrowserHost.ToSharedRef());
		GBrowserHost.Reset();
		GBrowser.Reset();
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdLibrary(TEXT("OKAnim.Library"),
		TEXT("OKAnim.Library [filter words]: opens / closes the in-game OK animation library (search, double-click or Play to play on the current characters)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
		{
			const FString Filter = FString::Join(Args, TEXT(" "));
			if (GBrowser && Args.IsEmpty()) { CloseBrowser(); Out.Logf(TEXT("library closed")); return; }
			if (GBrowser) { GBrowser->SetFilter(Filter); Out.Logf(TEXT("library filter '%s': %d"), *Filter, Library().Find(Filter).Num()); return; }
			if (!GEngine || !GEngine->GameViewport) { Out.Logf(TEXT("no game viewport")); return; }
			Library();
			TWeakObjectPtr<UWorld> WeakWorld(World);
			GBrowserHost = SNew(SBox).HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(16.f, 48.f))   // right side: the game's own panels are on the left
			[
				SAssignNew(GBrowser, SOKLibraryBrowser)
					.Library(GLibrary)
					.InitialFilter(Filter)
					.OnPlay_Lambda([WeakWorld](const FOKLibraryEntry& E) { if (UWorld* W = WeakWorld.Get()) PlayLibraryEntry(W, E, *GLog); })
					.OnClose_Lambda([]() { CloseBrowser(); })
			];
			GEngine->GameViewport->AddViewportWidgetContent(GBrowserHost.ToSharedRef(), 100);
			Out.Logf(TEXT("library open: %d entries ('%s' matches %d)"), Library().Entries.Num(), *Filter, Library().Find(Filter).Num());
		}));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdLibraryPlay(TEXT("OKAnim.LibraryPlay"),
		TEXT("OKAnim.LibraryPlay [reload] [female|male|pair] <filter words>: plays the first library entry matching (reload re-reads the catalogs)."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& InArgs, UWorld* World, FOutputDevice& Out)
		{
			TArray<FString> Args = InArgs;
			if (Args.Num() && Args[0] == TEXT("reload")) { Library().Load(); Args.RemoveAt(0); }
			TOptional<FOKLibraryEntry::EKind> Kind;
			if (Args.Num() && Args[0] == TEXT("female")) Kind = FOKLibraryEntry::EKind::Female;
			else if (Args.Num() && Args[0] == TEXT("male")) Kind = FOKLibraryEntry::EKind::Male;
			else if (Args.Num() && Args[0] == TEXT("pair")) Kind = FOKLibraryEntry::EKind::Pair;
			if (Kind.IsSet()) Args.RemoveAt(0);
			const TArray<TSharedPtr<FOKLibraryEntry>> Found = Library().Find(FString::Join(Args, TEXT(" ")), Kind);
			Out.Logf(TEXT("%d of %d entries match; "), Found.Num(), Library().Entries.Num());
			if (Found.Num()) PlayLibraryEntry(World, *Found[0], Out);
		}));
}

class FOKAnimationModule : public IModuleInterface
{
	TUniquePtr<FLiveStarter> Live;
public:
	virtual void StartupModule() override
	{
		if (!IsRunningCommandlet()) Live = MakeUnique<FLiveStarter>();
		AKKPairedPlayback::WalkCycle = &UOKAnimationAsset::DefaultWalk();
	}
	virtual void ShutdownModule() override { Live.Reset(); }
};

IMPLEMENT_MODULE(FOKAnimationModule, OKAnimation)
