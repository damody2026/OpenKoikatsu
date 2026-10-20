#include "OKAnimationEditing.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetImportTask.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxFactory.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/SkeletalMeshActor.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "MovieScene.h"
#include "LevelSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimMontage.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/FileManager.h"
#include "OKAnimationAssets.h"
#include "OKAnimationLibrary.h"
#include "OKAnimationPlayers.h"
#include "OKUnrealSkeleton.h"
#include "KKOkAnimation.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimCurveTypes.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace OKAnimationEditing
{
UObject* CreateAsset(UClass* Class, const FString& PackagePath, const FString& Name)
{
	IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	FString PackageName, AssetName;
	Tools.CreateUniqueAssetName(PackagePath / Name, FString(), PackageName, AssetName);
	UPackage* Package = CreatePackage(*PackageName);
	UObject* Asset = NewObject<UObject>(Package, Class, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	FAssetRegistryModule::AssetCreated(Asset);
	Package->MarkPackageDirty();
	return Asset;
}

bool SaveAsset(UObject* Asset)
{
	if (!Asset) return false;
	UPackage* Package = Asset->GetOutermost();
	const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, Asset, *File, Args);
}

UOKAnimSequence* ConvertUnrealAnimation(UAnimSequence* Sequence, UOKRetargetProfile* Profile, const FString& DestPath, float Fps, FString& OutError)
{
	if (!Sequence) { OutError = TEXT("no animation"); return nullptr; }
	UOKAnimSequence* Temp = UOKAnimationLibrary::ConvertUnrealAnimation(Sequence, Profile, nullptr, Fps, nullptr, NAME_None, OutError);
	if (!Temp) return nullptr;
	const FString Path = DestPath.IsEmpty() ? FPackageName::GetLongPackagePath(Sequence->GetOutermost()->GetName()) : DestPath;
	UOKAnimSequence* Asset = Cast<UOKAnimSequence>(CreateAsset(UOKAnimSequence::StaticClass(), Path, Sequence->GetName() + TEXT("_OK")));
	if (!Asset || !Asset->SetData(Temp->Data, OutError)) return nullptr;
	Asset->EditProfile = Profile;
	return Asset;
}

USkeleton* DefaultExpandSkeleton(const UOKAnimSequence* Animation)
{
	const UOKSkeletonFamily* Family = Animation && Animation->EditProfile && Animation->EditProfile->Family ? Animation->EditProfile->Family.Get() : UOKAnimationLibrary::GetDefaultFamily();
	USkeletalMesh* Mesh = Family ? Family->PreviewMesh.LoadSynchronous() : nullptr;
	if (!Mesh && Animation)
		if (UAnimSequence* Proxy = Animation->EditProxy.LoadSynchronous()) return Proxy->GetSkeleton();
	return Mesh ? Mesh->GetSkeleton() : nullptr;
}

UAnimSequence* ExpandToAnimSequence(UOKAnimSequence* Animation, USkeleton* Skeleton, FString& OutError)
{
	if (!Animation || Animation->Data.IsEmpty()) { OutError = TEXT("no OK animation data"); return nullptr; }
	if (!Skeleton) { OutError = TEXT("no skeleton: set a preview mesh on the family (or the retarget profile's family)"); return nullptr; }
	const UOKSkeletonFamily* Family = Animation->EditProfile && Animation->EditProfile->Family ? Animation->EditProfile->Family.Get() : UOKAnimationLibrary::GetDefaultFamily();
	FOKUnrealSkeleton Binding;
	if (!Binding.Build(Skeleton->GetReferenceSkeleton(), Animation->EditProfile, Family, OutError)) return nullptr;

	UAnimSequence* Seq = Animation->EditProxy.LoadSynchronous();
	if (!Seq || Seq->GetSkeleton() != Skeleton)
	{
		const FString Path = FPackageName::GetLongPackagePath(Animation->GetOutermost()->GetName());
		Seq = Cast<UAnimSequence>(CreateAsset(UAnimSequence::StaticClass(), Path, Animation->GetName() + TEXT("_Edit")));
		if (!Seq) { OutError = TEXT("cannot create the AnimSequence"); return nullptr; }
		Seq->SetSkeleton(Skeleton);
	}
	Seq->Modify();
	const int32 Frames = FMath::Max(1, Animation->Frames);
	const float Fps = FMath::Max(1.f, Animation->Fps);
	const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
	TArray<TArray<FVector3f>> Pos;
	TArray<TArray<FQuat4f>> Rot;
	TArray<TArray<FVector3f>> Scl;
	Pos.SetNum(Ref.GetNum());
	Rot.SetNum(Ref.GetNum());
	Scl.SetNum(Ref.GetNum());
	TArray<FTransform> Local;
	for (int32 F = 0; F < Frames; ++F)
	{
		if (!Binding.SamplePose(Animation, F / Fps, Local)) { OutError = TEXT("sampling failed"); return nullptr; }
		for (int32 B = 0; B < Ref.GetNum(); ++B)
		{
			Pos[B].Add(FVector3f(Local[B].GetTranslation()));
			Rot[B].Add(FQuat4f(Local[B].GetRotation()));
			Scl[B].Add(FVector3f(Local[B].GetScale3D()));
		}
	}
	IAnimationDataController& C = Seq->GetController();
	C.OpenBracket(NSLOCTEXT("OKAnimation", "Expand", "Expand OK animation"));
	C.InitializeModel();
	C.SetFrameRate(FFrameRate(FMath::RoundToInt(Fps), 1));
	C.SetNumberOfFrames(FFrameNumber(FMath::Max(1, Frames - 1)));
	C.RemoveAllBoneTracks();
	for (int32 B = 0; B < Ref.GetNum(); ++B)
	{
		const FName Bone = Ref.GetBoneName(B);
		C.AddBoneCurve(Bone);
		C.SetBoneTrackKeys(Bone, Pos[B], Rot[B], Scl[B]);
	}
	for (const FOKCurveTrack& Track : Animation->Curves)
	{
		if (Track.Name.IsNone()) continue;
		const FAnimationCurveIdentifier Id(Track.Name, ERawCurveTrackTypes::RCT_Float);
		C.AddCurve(Id);
		TArray<FRichCurveKey> Keys;
		for (auto It = Track.Curve.GetRichCurveConst()->GetKeyIterator(); It; ++It) Keys.Add(*It);
		C.SetCurveKeys(Id, Keys);
	}
	C.NotifyPopulated();
	C.CloseBracket();

	// Contacts of the playing character (role 0) as notify states on one track.
	Seq->Notifies.RemoveAll([](const FAnimNotifyEvent& E) { return Cast<UOKContactNotifyState>(E.NotifyStateClass) != nullptr; });
#if WITH_EDITORONLY_DATA
	if (Seq->AnimNotifyTracks.IsEmpty()) Seq->AnimNotifyTracks.Add(FAnimNotifyTrack(TEXT("1"), FLinearColor::White));
	int32 Track = Seq->AnimNotifyTracks.IndexOfByPredicate([](const FAnimNotifyTrack& T) { return T.TrackName == FName(TEXT("OK Contacts")); });
	if (Track == INDEX_NONE) Track = Seq->AnimNotifyTracks.Add(FAnimNotifyTrack(TEXT("OK Contacts"), FLinearColor(0.85f, 0.35f, 0.55f)));
#else
	const int32 Track = 0;
#endif
	for (const FOKContact& Contact : Animation->Contacts)
	{
		UOKContactNotifyState* State = NewObject<UOKContactNotifyState>(Seq, NAME_None, RF_Transactional);
		State->Contact = Contact;
		FAnimNotifyEvent& E = Seq->Notifies.AddDefaulted_GetRef();
		E.NotifyStateClass = State;
		E.NotifyName = FName(*State->GetNotifyName_Implementation());
		E.Link(Seq, Contact.Start);
		E.TriggerTimeOffset = GetTriggerTimeOffsetForType(Seq->CalculateOffsetForNotify(Contact.Start));
		E.SetDuration(FMath::Max(0.f, Contact.End - Contact.Start));
		E.EndLink.Link(Seq, Contact.End);
		E.TrackIndex = Track;
	}
	Seq->RefreshCacheData();
	Seq->bLoop = Animation->bLooping;
	Seq->PostEditChange();
	Seq->MarkPackageDirty();
	Animation->Modify();
	Animation->EditProxy = Seq;
	Animation->MarkPackageDirty();
	return Seq;
}

bool WriteBack(UOKAnimSequence* Animation, FString& OutError)
{
	UAnimSequence* Seq = Animation ? Animation->EditProxy.LoadSynchronous() : nullptr;
	if (!Seq) { OutError = TEXT("this OK animation has not been expanded to an AnimSequence"); return false; }
	const UOKSkeletonFamily* Family = Animation->EditProfile && Animation->EditProfile->Family ? Animation->EditProfile->Family.Get() : UOKAnimationLibrary::GetDefaultFamily();
	FOKUnrealSkeleton Binding;
	if (!Binding.Build(Seq->GetSkeleton()->GetReferenceSkeleton(), Animation->EditProfile, Family, OutError)) return false;
	TArray<uint8> Bytes;
	if (!Binding.ConvertAnimation(Seq, Animation->Fps, Bytes, OutError)) return false;
	// Parameters survive the new bone data: kept from the asset, contacts and curves re-read from the AnimSequence.
	const bool bLooping = Animation->bLooping;
	const EOKSourceScene Scene = Animation->Scene;
	TMap<FString, FString> Meta = Animation->Meta;
	TArray<FOKContact> Contacts;
	for (const FAnimNotifyEvent& E : Seq->Notifies)
	{
		const UOKContactNotifyState* State = Cast<UOKContactNotifyState>(E.NotifyStateClass);
		if (!State) continue;
		FOKContact C = State->Contact;
		C.Start = E.GetTime();
		C.End = E.GetTime() + E.GetDuration();
		Contacts.Add(C);
	}
	TArray<FOKCurveTrack> Curves;
	if (const IAnimationDataModel* Model = Seq->GetDataModel())
	{
		for (const FFloatCurve& Curve : Model->GetFloatCurves())
		{
			FOKCurveTrack& T = Curves.AddDefaulted_GetRef();
			T.Name = Curve.GetName();
			*T.Curve.GetRichCurve() = Curve.FloatCurve;
		}
	}
	Animation->Modify();
	if (!Animation->SetData(Bytes, OutError)) return false;
	Animation->bLooping = bLooping;
	Animation->Scene = Scene;
	Meta.Add(TEXT("edited_from"), Seq->GetPathName());
	Animation->Meta = Meta;
	Animation->Contacts = Contacts;
	Animation->Curves = Curves;
	if (!Animation->ApplyEdits(OutError)) return false;
	Animation->MarkPackageDirty();
	return true;
}

bool ExportFile(UOKAnimSequence* Animation, const FString& Path, FString& OutError)
{
	if (!UOKAnimationLibrary::SaveOkAnimFile(Animation, Path)) { OutError = FString::Printf(TEXT("cannot write %s"), *Path); return false; }
	return true;
}

TArray<FString> Validate(const UOKAnimationAsset* Asset)
{
	TArray<FString> Issues;
	if (!Asset) { Issues.Add(TEXT("no asset")); return Issues; }
	FString Spec, Error;
	TArray<TArray<uint8>> Blobs;
	if (!Asset->BuildSpec(Spec, Blobs, Error)) { Issues.Add(Error); return Issues; }
	const TSharedPtr<FOkAnimPlayer> Player = Asset->CreatePlayer(Error);
	if (!Player) Issues.Add(FString::Printf(TEXT("player: %s"), *Error));
	else if (Player->Duration() <= 0.f) Issues.Add(TEXT("zero length"));

	auto CheckContacts = [&](const TArray<FOKContact>& Contacts, TFunctionRef<const UOKAnimSequence*(int32)> RoleSequence, float Length)
	{
		for (const FOKContact& C : Contacts)
		{
			const FString Name = FString::Printf(TEXT("contact %d.%s -> %d.%s"), C.Role, *C.Effector.ToString(), C.TargetRole, *C.TargetBone.ToString());
			if (C.End < C.Start) Issues.Add(Name + TEXT(": ends before it starts"));
			if (Length > 0.f && C.Start > Length) Issues.Add(Name + TEXT(": starts after the end"));
			if (C.Blend * 2.f > C.End - C.Start + 1.e-4f) Issues.Add(Name + TEXT(": fades longer than it lasts"));
			if (const UOKAnimSequence* S = RoleSequence(C.Role); S && !S->Bones.Contains(C.Effector)) Issues.Add(Name + TEXT(": effector bone not animated"));
			if (C.TargetRole >= 0)
				if (const UOKAnimSequence* S = RoleSequence(C.TargetRole); S && !S->Bones.Contains(C.TargetBone)) Issues.Add(Name + TEXT(": target bone not animated"));
		}
	};
	if (const UOKAnimSequence* Seq = Cast<UOKAnimSequence>(Asset))
	{
		if (const UOKSkeletonFamily* Family = UOKAnimationLibrary::GetDefaultFamily(); Family && Seq->Family == Family->FamilyId)
		{
			int32 Missing = 0;
			for (const FName Bone : Family->GetBoneNames()) Missing += Seq->Bones.Contains(Bone) ? 0 : 1;
			if (Missing) Issues.Add(FString::Printf(TEXT("%d family bones not animated (they keep their rest)"), Missing));
		}
		CheckContacts(Seq->Contacts, [Seq](int32) { return Seq; }, Seq->Duration);
		for (const FOKCurveTrack& T : Seq->Curves) if (T.Name.IsNone()) Issues.Add(TEXT("a curve has no name"));
	}
	else if (const UOKGroupAnimSequence* Group = Cast<UOKGroupAnimSequence>(Asset))
	{
		CheckContacts(Group->Contacts, [Group](int32 R) { return Group->Roles.IsValidIndex(R) ? Cast<UOKAnimSequence>(Group->Roles[R].Animation) : nullptr; },
			Player ? Player->Duration() : 0.f);
		TSet<FName> Names;
		for (const FOKGroupRole& R : Group->Roles)
		{
			bool bRepeated = false;
			if (!R.Name.IsNone()) Names.Add(R.Name, &bRepeated);
			if (bRepeated) Issues.Add(FString::Printf(TEXT("role name %s repeated"), *R.Name.ToString()));
		}
	}
	else if (const UOKBlendSpace* Blend = Cast<UOKBlendSpace>(Asset))
	{
		for (int32 I = 0; I < Blend->Samples.Num(); ++I)
		{
			const FOKBlendSample& S = Blend->Samples[I];
			for (int32 A = 0; A < Blend->Axes.Num(); ++A)
			{
				const float V = A == 0 ? S.Position.X : S.Position.Y;
				if (V < Blend->Axes[A].Min || V > Blend->Axes[A].Max) Issues.Add(FString::Printf(TEXT("sample %d outside axis %s"), I, *Blend->Axes[A].Name.ToString()));
			}
			for (int32 J = 0; J < I; ++J)
				if (Blend->Samples[J].Position.Equals(S.Position, 1.e-4)) Issues.Add(FString::Printf(TEXT("samples %d and %d at the same position"), J, I));
		}
		if (Blend->Axes.Num() == 2 && Blend->Samples.Num() < 3) Issues.Add(TEXT("a 2D blend space needs 3 samples or more to triangulate"));
	}
	return Issues;
}
}


bool OKAnimationEditing::SetMontageSectionTiming(UOKAnimMontage* Montage, int32 Section, float Start, float Length)
{
	if (!Montage || !Montage->Sections.IsValidIndex(Section)) return false;
	Montage->Modify();
	// Earlier automatic starts become explicit first, so moving this one does not shift them.
	const TArray<TPair<float, float>> Ranges = Montage->GetSectionRanges();
	for (int32 I = 0; I < Montage->Sections.Num() && I < Ranges.Num(); ++I) if (Montage->Sections[I].Start < 0.f) Montage->Sections[I].Start = Ranges[I].Key;
	FOKMontageSection& S = Montage->Sections[Section];
	S.Start = FMath::Max(0.f, Start);
	if (Length > 0.f) S.Length = Length;
	Montage->PostEditChange();
	return true;
}

bool OKAnimationEditing::SetMontageSectionBlend(UOKAnimMontage* Montage, int32 Section, float BlendIn)
{
	if (!Montage || !Montage->Sections.IsValidIndex(Section)) return false;
	Montage->Modify();
	Montage->Sections[Section].BlendIn = FMath::Max(0.f, BlendIn);
	Montage->PostEditChange();
	return true;
}

bool OKAnimationEditing::SetMontageSectionNext(UOKAnimMontage* Montage, int32 Section, FName Next)
{
	if (!Montage || !Montage->Sections.IsValidIndex(Section)) return false;
	if (!Next.IsNone() && !Montage->Sections.ContainsByPredicate([Next](const FOKMontageSection& S) { return S.Name == Next; })) return false;
	Montage->Modify();
	Montage->Sections[Section].Next = Next;
	Montage->PostEditChange();
	return true;
}


int32 OKAnimationEditing::ScoreFolder(const FString& Dir, int32 MaxReview, FString& OutSummary)
{
	const UOKSkeletonFamily* Family = UOKAnimationLibrary::GetDefaultFamily();
	if (!Family) { OutSummary = TEXT("no family"); return 0; }
	// Bodies: the family rest, and the same with 15% longer legs (feet reach lower: ground problems show).
	TArray<FString> Bodies = { Family->RestHeaderJson };
	{
		TSharedPtr<FJsonObject> H;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Family->RestHeaderJson), H) && H)
		{
			for (const TSharedPtr<FJsonValue>& V : H->GetArrayField(TEXT("bones")))
			{
				TSharedPtr<FJsonObject> B = V->AsObject();
				B->RemoveField(TEXT("world_pos"));
				const FString N = B->GetStringField(TEXT("name"));
				if (!(N.Contains(TEXT("thigh")) || N.Contains(TEXT("leg01")) || N.Contains(TEXT("leg03")))) continue;
				TArray<TSharedPtr<FJsonValue>> P = B->GetArrayField(TEXT("rest_pos"));
				for (TSharedPtr<FJsonValue>& C : P) C = MakeShared<FJsonValueNumber>(C->AsNumber() * 1.15);
				B->SetArrayField(TEXT("rest_pos"), P);
			}
			FString Long;
			FJsonSerializer::Serialize(H.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Long));
			Bodies.Add(Long);
		}
	}
	static const TMap<FString, float> Weight = { { TEXT("joint_extreme"), 3.f }, { TEXT("knee_backward"), 4.f }, { TEXT("foot_slide"), 1.f }, { TEXT("pop"), 2.f },
		{ TEXT("finger_flip"), 0.5f }, { TEXT("hand_in_body"), 1.f }, { TEXT("below_ground"), 2.f } };
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Dir, TEXT("*.okanim"), true, false);
	Files.Sort();
	struct FScore { FString File; float Score; TMap<FString, int32> Kinds; int32 Frames; };
	TArray<FScore> Scores;
	TMap<FString, int32> Totals;
	for (const FString& File : Files)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *File)) continue;
		FScore S{ File, 100.f, {}, 0 };
		FString Info;
		if (FOkAnimBridge::Info(Bytes, Info)) S.Frames = FCString::Atoi(*Info.Mid(Info.Find(TEXT("\"frames\":")) + 9));
		for (const FString& Body : Bodies)
		{
			FString Json;
			TArray<TSharedPtr<FJsonValue>> Issues;
			if (!FOkAnimBridge::Quality(Bytes, Body, Json) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Issues)) continue;
			for (const TSharedPtr<FJsonValue>& V : Issues)
			{
				const TSharedPtr<FJsonObject> I = V->AsObject();
				const FString Kind = I->GetStringField(TEXT("kind"));
				const int32 Len = I->GetIntegerField(TEXT("end")) - I->GetIntegerField(TEXT("start")) + 1;
				// A problem costs its weight, more when it lasts (relative to the clip).
				S.Score -= Weight.FindRef(Kind) * (1.f + 4.f * Len / FMath::Max(1, S.Frames));
				S.Kinds.FindOrAdd(Kind) += 1;
				Totals.FindOrAdd(Kind) += 1;
			}
		}
		S.Score = FMath::Max(0.f, S.Score);
		Scores.Add(MoveTemp(S));
	}
	Scores.Sort([](const FScore& A, const FScore& B) { return A.Score < B.Score; });
	TArray<TSharedPtr<FJsonValue>> All;
	FString Review = FString::Printf(TEXT("OK animation review list: %d files scored on %d bodies, worst first\n"), Scores.Num(), Bodies.Num());
	for (int32 I = 0; I < Scores.Num(); ++I)
	{
		const FScore& S = Scores[I];
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		FString Rel = S.File;
		FPaths::MakePathRelativeTo(Rel, *(Dir / TEXT("")));
		O->SetStringField(TEXT("file"), Rel);
		O->SetNumberField(TEXT("score"), S.Score);
		TSharedRef<FJsonObject> K = MakeShared<FJsonObject>();
		FString KindText;
		for (const auto& P : S.Kinds) { K->SetNumberField(P.Key, P.Value); KindText += FString::Printf(TEXT(" %s=%d"), *P.Key, P.Value); }
		O->SetObjectField(TEXT("issues"), K);
		All.Add(MakeShared<FJsonValueObject>(O));
		if (I < MaxReview && S.Score < 100.f) Review += FString::Printf(TEXT("%6.1f  %s %s\n"), S.Score, *Rel, *KindText);
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("clips"), All);
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	FFileHelper::SaveStringToFile(Text, *(Dir / TEXT("score.json")));
	FFileHelper::SaveStringToFile(Review, *(Dir / TEXT("review.txt")));
	int32 Clean = 0;
	float Sum = 0.f;
	for (const FScore& S : Scores) { Clean += S.Score >= 100.f ? 1 : 0; Sum += S.Score; }
	OutSummary = FString::Printf(TEXT("%d files, %d clean, mean score %.1f, worst %.1f;"), Scores.Num(), Clean, Scores.Num() ? Sum / Scores.Num() : 0.f, Scores.Num() ? Scores[0].Score : 0.f);
	for (const auto& P : Totals) OutSummary += FString::Printf(TEXT(" %s=%d"), *P.Key, P.Value);
	return Scores.Num();
}


namespace
{
	/** One converted sequence per source sequence within a conversion. */
	UOKAnimSequence* ConvertCached(UAnimSequence* Seq, UOKRetargetProfile* Profile, const FString& DestPath, float Fps, TMap<UAnimSequence*, UOKAnimSequence*>& Cache, FString& OutError)
	{
		if (UOKAnimSequence** Found = Cache.Find(Seq)) return *Found;
		UOKAnimSequence* Ok = OKAnimationEditing::ConvertUnrealAnimation(Seq, Profile, DestPath, Fps, OutError);
		if (Ok) Cache.Add(Seq, Ok);
		return Ok;
	}
}

UOKAnimationAsset* OKAnimationEditing::ConvertUnrealAsset(UObject* Source, UOKRetargetProfile* Profile, const FString& InDest, float Fps, FString& OutError)
{
	if (!Source) { OutError = TEXT("no asset"); return nullptr; }
	const FString DestPath = InDest.IsEmpty() ? FPackageName::GetLongPackagePath(Source->GetOutermost()->GetName()) : InDest;
	TMap<UAnimSequence*, UOKAnimSequence*> Cache;
	if (UAnimSequence* Seq = Cast<UAnimSequence>(Source)) return ConvertUnrealAnimation(Seq, Profile, DestPath, Fps, OutError);
	if (UAnimMontage* Montage = Cast<UAnimMontage>(Source))
	{
		// The first slot that has animation (a new montage carries an empty default slot).
		const FSlotAnimationTrack* Slot = Montage->SlotAnimTracks.FindByPredicate([](const FSlotAnimationTrack& S) { return S.AnimTrack.AnimSegments.Num() > 0; });
		if (!Slot) { OutError = TEXT("montage has no animation segments"); return nullptr; }
		const TArray<FAnimSegment>& Segments = Slot->AnimTrack.AnimSegments;
		UOKAnimMontage* Out = Cast<UOKAnimMontage>(CreateAsset(UOKAnimMontage::StaticClass(), DestPath, Montage->GetName() + TEXT("_OK")));
		TArray<FCompositeSection> Sections = Montage->CompositeSections;
		if (Sections.IsEmpty()) { FCompositeSection Whole; Whole.SectionName = TEXT("Default"); Whole.SetTime(0.f); Sections.Add(Whole); }
		Sections.Sort([](const FCompositeSection& A, const FCompositeSection& B) { return A.GetTime() < B.GetTime(); });
		const float Length = Montage->GetPlayLength();
		for (int32 I = 0; I < Sections.Num(); ++I)
		{
			const float T0 = Sections[I].GetTime(), T1 = I + 1 < Sections.Num() ? Sections[I + 1].GetTime() : Length;
			// The segment the section starts in; the section reads that animation from the matching position.
			const FAnimSegment* Seg = Segments.FindByPredicate([T0](const FAnimSegment& S) { return T0 >= S.StartPos - 1.e-4f && T0 < S.StartPos + S.GetLength() - 1.e-4f; });
			if (!Seg) Seg = &Segments.Last();
			UAnimSequence* Anim = Cast<UAnimSequence>(Seg->GetAnimReference());
			UOKAnimSequence* Ok = Anim ? ConvertCached(Anim, Profile, DestPath, Fps, Cache, OutError) : nullptr;
			if (!Ok) { if (OutError.IsEmpty()) OutError = TEXT("montage segment is not an AnimSequence"); return nullptr; }
			FOKMontageSection& S = Out->Sections.AddDefaulted_GetRef();
			S.Name = Sections[I].SectionName;
			S.Animation = Ok;
			S.Start = T0;
			S.Length = FMath::Max(1.f / 30.f, T1 - T0);
			S.Rate = FMath::Max(0.01f, FMath::Abs(Seg->AnimPlayRate));
			S.From = FMath::Max(0.f, Seg->AnimStartTime + (T0 - Seg->StartPos) * S.Rate);
			S.BlendIn = I == 0 ? 0.f : 0.f;   // Unreal sections cut; blends come from the montage's blend in/out, not per section
			S.Next = Sections[I].NextSectionName;
		}
		return Out;
	}
	if (UBlendSpace* Blend = Cast<UBlendSpace>(Source))
	{
		UOKBlendSpace* Out = Cast<UOKBlendSpace>(CreateAsset(UOKBlendSpace::StaticClass(), DestPath, Blend->GetName() + TEXT("_OK")));
		const int32 Dims = Blend->IsA<UBlendSpace1D>() ? 1 : 2;
		for (int32 A = 0; A < Dims; ++A)
		{
			const FBlendParameter& P = Blend->GetBlendParameter(A);
			Out->Axes.Add({ FName(*P.DisplayName), P.Min, P.Max });
		}
		for (const FBlendSample& Sample : Blend->GetBlendSamples())
		{
			UOKAnimSequence* Ok = Sample.Animation ? ConvertCached(Sample.Animation.Get(), Profile, DestPath, Fps, Cache, OutError) : nullptr;
			if (!Ok) return nullptr;
			Out->Samples.Add({ Ok, FVector2D(Sample.SampleValue.X, Dims > 1 ? Sample.SampleValue.Y : 0.) });
		}
		return Out;
	}
	if (ULevelSequence* Level = Cast<ULevelSequence>(Source))
	{
		// The skeletal animation sections of the sequence, in time order, as one OK montage (first bound skeletal track).
		UMovieScene* Scene = Level->GetMovieScene();
		if (!Scene) { OutError = TEXT("no movie scene"); return nullptr; }
		const double Rate = Scene->GetTickResolution().AsDecimal();
		TArray<UMovieSceneSkeletalAnimationSection*> Found;
		for (const FMovieSceneBinding& Binding : Scene->GetBindings())
			for (UMovieSceneTrack* Track : Binding.GetTracks())
				if (UMovieSceneSkeletalAnimationTrack* Skel = Cast<UMovieSceneSkeletalAnimationTrack>(Track))
				{
					for (UMovieSceneSection* S : Skel->GetAllSections()) if (UMovieSceneSkeletalAnimationSection* A = Cast<UMovieSceneSkeletalAnimationSection>(S)) Found.Add(A);
					if (Found.Num()) break;
				}
		if (Found.IsEmpty()) { OutError = TEXT("level sequence has no skeletal animation section"); return nullptr; }
		Found.Sort([](const UMovieSceneSkeletalAnimationSection& A, const UMovieSceneSkeletalAnimationSection& B) { return A.GetInclusiveStartFrame() < B.GetInclusiveStartFrame(); });
		UOKAnimMontage* Out = Cast<UOKAnimMontage>(CreateAsset(UOKAnimMontage::StaticClass(), DestPath, Level->GetName() + TEXT("_OK")));
		const double First = Found[0]->GetInclusiveStartFrame().Value / Rate;
		for (int32 I = 0; I < Found.Num(); ++I)
		{
			UMovieSceneSkeletalAnimationSection* A = Found[I];
			UAnimSequence* Anim = Cast<UAnimSequence>(A->Params.Animation);
			UOKAnimSequence* Ok = Anim ? ConvertCached(Anim, Profile, DestPath, Fps, Cache, OutError) : nullptr;
			if (!Ok) { if (OutError.IsEmpty()) OutError = TEXT("section animation is not an AnimSequence"); return nullptr; }
			FOKMontageSection& S = Out->Sections.AddDefaulted_GetRef();
			S.Name = FName(*FString::Printf(TEXT("Shot%d"), I + 1));
			S.Animation = Ok;
			S.Start = float(A->GetInclusiveStartFrame().Value / Rate - First);
			S.Length = float((A->GetExclusiveEndFrame().Value - A->GetInclusiveStartFrame().Value) / Rate);
			// A fixed play rate (time-warp curves bake at their fixed value).
			S.Rate = FMath::Max(0.01f, A->Params.PlayRate.GetType() == EMovieSceneTimeWarpType::FixedPlayRate ? A->Params.PlayRate.AsFixedPlayRateFloat() : 1.f);
			S.From = float(A->Params.FirstLoopStartFrameOffset.Value / Rate + A->Params.StartFrameOffset.Value / Rate);
			S.BlendIn = 0.f;
		}
		return Out;
	}
	OutError = FString::Printf(TEXT("%s: unsupported type %s"), *Source->GetName(), *Source->GetClass()->GetName());
	return nullptr;
}


ULevelSequence* OKAnimationEditing::MakeEditSequence(UOKAnimSequence* Animation, USkeletalMesh* Mesh, bool bOpen, FString& OutError)
{
	if (!Animation || !Mesh || !Mesh->GetSkeleton()) { OutError = TEXT("needs an OK animation and a preview mesh"); return nullptr; }
	UAnimSequence* Proxy = ExpandToAnimSequence(Animation, Mesh->GetSkeleton(), OutError);
	if (!Proxy) return nullptr;
	const FString Path = FPackageName::GetLongPackagePath(Animation->GetOutermost()->GetName());
	ULevelSequence* Seq = Cast<ULevelSequence>(CreateAsset(ULevelSequence::StaticClass(), Path, Animation->GetName() + TEXT("_Seq")));
	if (!Seq) { OutError = TEXT("cannot create the level sequence"); return nullptr; }
	Seq->Initialize();
	UMovieScene* Scene = Seq->GetMovieScene();
	const FFrameRate Display(FMath::RoundToInt(FMath::Max(Animation->Fps, 1.f)), 1);
	Scene->SetDisplayRate(Display);
	const FFrameRate Tick = Scene->GetTickResolution();
	const FFrameNumber End = (Proxy->GetPlayLength() * Tick).RoundToFrame();
	Scene->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), End));
	// The preview mesh as a spawnable actor carrying the expanded animation.
	ASkeletalMeshActor* Template = NewObject<ASkeletalMeshActor>(Scene, ASkeletalMeshActor::StaticClass(), FName(*(Animation->GetName() + TEXT("_Actor"))), RF_Transactional);
	Template->GetSkeletalMeshComponent()->SetSkeletalMesh(Mesh);
	const FGuid Guid = Scene->AddSpawnable(Animation->GetName(), *Template);
	UMovieSceneSkeletalAnimationTrack* Track = Scene->AddTrack<UMovieSceneSkeletalAnimationTrack>(Guid);
	if (!Track) { OutError = TEXT("cannot add the animation track"); return nullptr; }
	Track->AddNewAnimation(FFrameNumber(0), Proxy);
	Seq->MarkPackageDirty();
	if (bOpen && GEditor) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Seq);
	return Seq;
}


TArray<UOKAnimSequence*> OKAnimationEditing::ImportFbxAsOk(const FString& File, const FString& DestPath, USkeleton* FallbackSkeleton, FString& OutError)
{
	TArray<UOKAnimSequence*> Out;
	if (!FPaths::FileExists(File)) { OutError = FString::Printf(TEXT("no file %s"), *File); return Out; }
	auto Import = [&](bool bWithMesh) -> TArray<UObject*>
	{
		UFbxFactory* Factory = NewObject<UFbxFactory>();
		Factory->AddToRoot();
		UFbxImportUI* UI = NewObject<UFbxImportUI>();
		UI->bAutomatedImportShouldDetectType = false;
		UI->MeshTypeToImport = FBXIT_SkeletalMesh;
		UI->bImportAsSkeletal = true;
		UI->bImportMesh = bWithMesh;
		UI->bImportAnimations = true;
		UI->bImportMaterials = false;
		UI->bImportTextures = false;
		UI->Skeleton = bWithMesh ? nullptr : FallbackSkeleton;
		if (!bWithMesh) UI->MeshTypeToImport = FBXIT_Animation;
		Factory->ImportUI = UI;
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = File;
		Task->DestinationPath = DestPath / TEXT("FbxSource");
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = false;
		Task->Factory = Factory;
		Task->Options = UI;
		FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().ImportAssetTasks({ Task });
		Factory->RemoveFromRoot();
		TArray<UObject*> Objects = Task->GetObjects();
		// Animations land next to the mesh: everything imported into the folder counts.
		TArray<FAssetData> Found;
		IAssetRegistry::GetChecked().GetAssetsByPath(FName(*Task->DestinationPath), Found, true);
		for (const FAssetData& D : Found) if (D.AssetClassPath == UAnimSequence::StaticClass()->GetClassPathName()) Objects.AddUnique(D.GetAsset());
		return Objects;
	};
	TArray<UObject*> Objects = Import(true);
	if (!Objects.ContainsByPredicate([](UObject* O) { return O && O->IsA<UAnimSequence>(); }) && FallbackSkeleton) Objects = Import(false);
	for (UObject* O : Objects)
	{
		UAnimSequence* Anim = Cast<UAnimSequence>(O);
		if (!Anim) continue;
		FString Error;
		if (UOKAnimSequence* Ok = ConvertUnrealAnimation(Anim, nullptr, DestPath, GetDefault<UOKAnimationSettings>()->ImportFps, Error))
		{
			Ok->Meta.Add(TEXT("imported_from"), File);
			Ok->ApplyEdits(Error);
			Out.Add(Ok);
		}
		else OutError += FString::Printf(TEXT("%s: %s; "), *Anim->GetName(), *Error);
	}
	if (Out.IsEmpty() && OutError.IsEmpty()) OutError = TEXT("the FBX import produced no animation (an animation-only file needs a skeleton)");
	return Out;
}
