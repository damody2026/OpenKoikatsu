#include "OKAnimationPlayers.h"
#include "OKAnimationAssets.h"
#include "KKOkAnimation.h"
#include "KKPreviewActor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	void PushParams(FOkAnimPlayer& Player, const UOKAnimationAsset* Asset, const TMap<FName, float>& Parameters)
	{
		if (!Asset) return;
		const TArray<FName> Names = Asset->GetParameterNames();
		for (int32 I = 0; I < Names.Num() && I < 8; ++I)
			if (const float* V = Parameters.Find(Names[I])) Player.SetParam(I, *V);
	}

	void ReadReport(const FString& Json, FOKFrameReport& Out)
	{
		Out.Json = Json;
		TSharedPtr<FJsonObject> J;
		const auto Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, J) || !J) return;
		Out.Contacts = int32(J->GetNumberField(TEXT("contacts")));
		Out.MaxContactError = float(J->GetNumberField(TEXT("contact_max")));
	}
}

// ------------------------------------------------------------------------------------------------ component

UOKAnimationComponent::UOKAnimationComponent()
{
	// Ticks only while a montage branches (SetComponentTickEnabled), before the character's own tick advances it.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

AKKPreviewActor* UOKAnimationComponent::GetCharacter() const { return Cast<AKKPreviewActor>(GetOwner()); }

void UOKAnimationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoPlay && Animation) Play(Animation);
}

void UOKAnimationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Stop();
	Super::EndPlay(Reason);
}

bool UOKAnimationComponent::Play(UOKAnimationAsset* Asset, float BlendTime)
{
	AKKPreviewActor* Character = GetCharacter();
	if (!Asset || !Character) return false;
	if (Asset->GetRoleCount() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("OK animation %s has %d roles: play it with an AOKGroupPlayer"), *Asset->GetName(), Asset->GetRoleCount());
		return false;
	}
	if (BlendTime < 0.f) { const UOKAnimSequence* Seq = Cast<UOKAnimSequence>(Asset); BlendTime = Seq ? Seq->Info.FadeIn : 0.2f; }
	// Cross-fade from what plays now (at its current time) when asked.
	const bool bFade = BlendTime > 0.f && Animation && Player && Character->OkPlayer == Player && Character->PoseMode == 3;
	FString Error;
	TSharedPtr<FOkAnimPlayer> NewPlayer = bFade ? Asset->CreateCrossfadePlayer(Animation, Character->AnimPhase, BlendTime, Error) : Asset->CreatePlayer(Error);
	if (!NewPlayer) { UE_LOG(LogTemp, Error, TEXT("OK animation %s: %s"), *Asset->GetName(), *Error); return false; }
	Animation = Asset;
	Player = NewPlayer;
	PushParameters();
	Character->AnimationPlaybackSpeed = PlayRate;
	SectionRanges.Reset();
	SectionNames.Reset();
	SectionEntry.Reset();
	NextOf.Reset();
	if (const UOKAnimMontage* Montage = Cast<UOKAnimMontage>(Asset))
	{
		SectionRanges = Montage->GetSectionRanges();
		for (const FOKMontageSection& S : Montage->Sections)
		{
			// Entering a section by a jump lands after its fade-in (the section it fades from is not playing).
			SectionEntry.Add(SectionNames.Num() ? S.BlendIn : 0.f);
			SectionNames.Add(S.Name);
			if (!S.Next.IsNone()) NextOf.Add(S.Name, S.Next);
		}
	}
	const UOKAnimSequence* Seq = Cast<UOKAnimSequence>(Asset);
	bRootMotion = Seq && Seq->Info.RootMotion == EOKRootMotion::RootMotion;
	HipsLength = bRootMotion ? FOkAnimBridge::SessionHipsLength(Character->Session) : 1.f;
	LastDriftTime = 0.f;
	LastSectionPhase = -1.f;
	SetComponentTickEnabled(!NextOf.IsEmpty() || bRootMotion);
	return Character->SetOkAnimation(Player);
}

FName UOKAnimationComponent::GetCurrentSection() const
{
	const AKKPreviewActor* Character = GetCharacter();
	if (!Character || SectionRanges.IsEmpty()) return NAME_None;
	// The latest section that has started (later sections fade in over earlier ones).
	FName Current = NAME_None;
	for (int32 I = 0; I < SectionRanges.Num(); ++I) if (Character->AnimPhase >= SectionRanges[I].Key) Current = SectionNames[I];
	return Current;
}

bool UOKAnimationComponent::JumpToSection(FName Section)
{
	AKKPreviewActor* Character = GetCharacter();
	const int32 I = SectionNames.IndexOfByKey(Section);
	if (!Character || I == INDEX_NONE) return false;
	Character->AnimPhase = SectionRanges[I].Key + SectionEntry[I];
	LastSectionPhase = Character->AnimPhase;
	Character->ApplyPoseFrame(0.f, false);
	return true;
}

void UOKAnimationComponent::SetNextSection(FName Section, FName Next)
{
	if (Next.IsNone()) NextOf.Remove(Section); else NextOf.Add(Section, Next);
	SetComponentTickEnabled(!NextOf.IsEmpty() || bRootMotion);
}

void UOKAnimationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AKKPreviewActor* Character = GetCharacter();
	if (!Character || Character->OkPlayer != Player) return;
	if (bRootMotion)
	{
		// The character moves by the animation's travel since the last tick (OK space -> this actor's frame).
		const float T = Character->AnimPhase;
		const FVector Delta = (Player->RootDrift(0, T) - Player->RootDrift(0, LastDriftTime)) * HipsLength;
		LastDriftTime = T;
		const FVector Ue(-Delta.X * 100., Delta.Z * 100., Delta.Y * 100.);
		if (!Ue.IsNearlyZero()) Character->AddActorWorldOffset(Character->GetActorRotation().RotateVector(Ue));
	}
	if (SectionRanges.IsEmpty()) return;
	// A section with a chosen next one: when playback crosses its boundary (its end, or where the following section
	// starts fading in) between two ticks, continue in the next section with the overshoot carried over.
	const float Now = Character->AnimPhase;
	const float Before = LastSectionPhase >= 0.f && LastSectionPhase <= Now ? LastSectionPhase : Now;
	for (int32 I = 0; I < SectionRanges.Num(); ++I)
	{
		const FName* Next = NextOf.Find(SectionNames[I]);
		const int32 J = Next ? SectionNames.IndexOfByKey(*Next) : INDEX_NONE;
		if (J == INDEX_NONE) continue;
		const float Boundary = I + 1 < SectionRanges.Num() ? FMath::Min(SectionRanges[I].Value, SectionRanges[I + 1].Key) : SectionRanges[I].Value;
		if (Before >= SectionRanges[I].Key && Before < Boundary && Now >= Boundary)
		{
			Character->AnimPhase = SectionRanges[J].Key + SectionEntry[J] + (Now - Boundary);
			break;
		}
	}
	LastSectionPhase = Character->AnimPhase;
}

void UOKAnimationComponent::Stop()
{
	AKKPreviewActor* Character = GetCharacter();
	if (Character && Player && Character->OkPlayer == Player) Character->SetPoseMode(0);
	Player.Reset();
}

void UOKAnimationComponent::PushParameters() { if (Player) PushParams(*Player, Animation, Parameters); }

void UOKAnimationComponent::SetParameter(FName Name, float Value)
{
	Parameters.Add(Name, Value);
	PushParameters();
}

void UOKAnimationComponent::SetSequenceTime(float InTime)
{
	const float Delta = FMath::Abs(InTime - SequenceTime);
	SequenceTime = InTime;
	AKKPreviewActor* Character = GetCharacter();
	if (!Character) return;
	if (!Player && Animation && !Play(Animation)) return;
	if (Character->OkPlayer != Player) return;
	// Sequencer owns the clock: pose at exactly this time, the physics stepping by the scrub distance (capped).
	Character->AnimationPlaybackSpeed = 0.f;
	Character->AnimPhase = InTime;
	Character->ApplyPoseFrame(FMath::Min(Delta, 1.f / 15.f), false);
}

float UOKAnimationComponent::GetTime() const
{
	const AKKPreviewActor* Character = GetCharacter();
	return Character && Player && Character->OkPlayer == Player ? Character->AnimPhase : 0.f;
}

bool UOKAnimationComponent::IsPlaying() const
{
	const AKKPreviewActor* Character = GetCharacter();
	return Character && Player && Character->OkPlayer == Player && Character->PoseMode == 3;
}

// ------------------------------------------------------------------------------------------------ group player

AOKGroupPlayer::AOKGroupPlayer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

void AOKGroupPlayer::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoPlay && Animation)
	{
		TArray<AKKPreviewActor*> List;
		for (AKKPreviewActor* A : Characters) List.Add(A);
		Play(Animation, List);
	}
}

void AOKGroupPlayer::EndPlay(const EEndPlayReason::Type Reason)
{
	Stop();
	Super::EndPlay(Reason);
}

bool AOKGroupPlayer::Play(UOKAnimationAsset* Asset, const TArray<AKKPreviewActor*>& InCharacters)
{
	Stop();
	LastError.Reset();
	if (!Asset) { LastError = TEXT("no animation"); return false; }
	if (Asset->GetRoleCount() != InCharacters.Num())
	{
		LastError = FString::Printf(TEXT("%s has %d roles, %d characters given"), *Asset->GetName(), Asset->GetRoleCount(), InCharacters.Num());
		return false;
	}
	for (int32 I = 0; I < InCharacters.Num(); ++I)
	{
		if (!IsValid(InCharacters[I]) || !InCharacters[I]->Session.IsOpen()) { LastError = FString::Printf(TEXT("character %d not loaded"), I); return false; }
		for (int32 J = 0; J < I; ++J) if (InCharacters[J] == InCharacters[I]) { LastError = TEXT("a character plays two roles"); return false; }
	}
	TSharedPtr<FOkAnimPlayer> NewPlayer = Asset->CreatePlayer(LastError);
	if (!NewPlayer) return false;
	Animation = Asset;
	Characters.Reset();
	for (AKKPreviewActor* A : InCharacters)
	{
		Characters.Add(A);
		A->SetExternalPoseDriver(true);
		// This player poses them; their own tick (gaze, face, shadow) follows.
		A->PrimaryActorTick.AddPrerequisite(this, PrimaryActorTick);
	}
	Player = NewPlayer;
	PushParameters();
	Time = 0.f;
	ResetReport();
	SetActorTickEnabled(true);
	return Step(0.f);
}

void AOKGroupPlayer::Stop()
{
	if (!Player) return;
	Player.Reset();
	for (AKKPreviewActor* A : Characters)
	{
		if (!IsValid(A)) continue;
		A->PrimaryActorTick.RemovePrerequisite(this, PrimaryActorTick);
		A->SetExternalPoseDriver(false);
	}
}

void AOKGroupPlayer::PushParameters() { if (Player) PushParams(*Player, Animation, Parameters); }

void AOKGroupPlayer::SetParameter(FName Name, float Value)
{
	Parameters.Add(Name, Value);
	PushParameters();
}

void AOKGroupPlayer::SetTime(float InTime)
{
	const float Delta = FMath::Abs(InTime - Time);
	Time = InTime;
	if (Player) Step(FMath::Min(Delta, 1.f / 15.f));
}

bool AOKGroupPlayer::Step(float Dt)
{
	TArray<AKKPreviewActor*> List;
	for (AKKPreviewActor* A : Characters) { if (!IsValid(A)) { LastError = TEXT("a character was destroyed"); Stop(); return false; } List.Add(A); }
	FString Report;
	if (!Player->Apply(List, Time, Dt, &Report))
	{
		LastError = Report;
		UE_LOG(LogTemp, Error, TEXT("OK group frame failed: %s"), *Report);
		return false;
	}
	ReadReport(Report, LastReport);
	PeakContactError = FMath::Max(PeakContactError, LastReport.MaxContactError);
	++FramesPlayed;
	return true;
}

void AOKGroupPlayer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Player || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.f) return;
	Time += DeltaSeconds * PlayRate;
	Step(DeltaSeconds);
}

// ------------------------------------------------------------------------------------------------ notify

FString UOKContactNotifyState::GetNotifyName_Implementation() const
{
	const FString Target = Contact.TargetRole == -1 ? FString(TEXT("ground")) : Contact.TargetRole == -2 ? FString(TEXT("point"))
		: FString::Printf(TEXT("%d.%s"), Contact.TargetRole, *Contact.TargetBone.ToString());
	return FString::Printf(TEXT("OK %d.%s -> %s"), Contact.Role, *Contact.Effector.ToString(), *Target);
}
