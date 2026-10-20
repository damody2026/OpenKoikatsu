#include "OKBatchConvertCommandlet.h"
#include "OKAnimationAssets.h"
#include "OKAnimationEditing.h"
#include "Engine/SkeletalMesh.h"
#include "OKAnimationLibrary.h"
#include "OKUnrealSkeleton.h"
#include "KKOkAnimation.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "AnimGraphNode_OKPlayer.h"
#include "AnimGraphNode_OKContactIK.h"
#include "AnimGraphNode_Root.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/World.h"
#include "OKBvh.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Exporters/Exporter.h"
#include "AssetExportTask.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "MovieScene.h"
#include "LevelSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimMontage.h"
#include "OKAnimThumbnailRenderer.h"

UOKBatchConvertCommandlet::UOKBatchConvertCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

namespace
{
	/** Builds an AnimBlueprint (OK Player -> OK Contact IK -> Output) for Mesh, compiles it, evaluates it at 0.3 s and compares
	 *  with FOKUnrealSkeleton::SamplePose; a ground contact lifts the left foot to a known height. */
	template <typename FCheck>
	int32 AnimBlueprintTest(USkeletalMesh* Mesh, UOKSkeletonFamily* Family, UOKAnimSequence* Source, const FString& Dest, FCheck&& Check)
	{
		FString Error;
		// A copy of the clip with one ground contact on the left foot for the whole clip, 0.3 hips heights high.
		UOKAnimSequence* Seq = NewObject<UOKAnimSequence>(GetTransientPackage(), TEXT("OKAnimBPClip"));
		Seq->SetData(Source->Data, Error);
		Seq->Contacts.Reset();
		FOKContact& Foot = Seq->Contacts.AddDefaulted_GetRef();
		Foot.Effector = TEXT("cf_j_leg03_L");
		Foot.TargetRole = -1;
		Foot.Offset = FVector(0., 0.3, 0.);
		Foot.Start = 0.f;
		Foot.End = Seq->Duration;
		Foot.Blend = 0.f;
		// A transient blueprint (an earlier run's saved one would be only partially loaded here).
		UAnimBlueprint* BP = CastChecked<UAnimBlueprint>(FKismetEditorUtilities::CreateBlueprint(UAnimInstance::StaticClass(), GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UAnimBlueprint::StaticClass(), TEXT("OK_AnimBP")), BPTYPE_Normal,
			UAnimBlueprint::StaticClass(), UAnimBlueprintGeneratedClass::StaticClass()));
		BP->TargetSkeleton = Mesh->GetSkeleton();
		UEdGraph* Graph = nullptr;
		for (UEdGraph* G : BP->FunctionGraphs) if (G && G->GetFName() == TEXT("AnimGraph")) Graph = G;
		UAnimGraphNode_Root* Root = nullptr;
		if (Graph) for (UEdGraphNode* N : Graph->Nodes) if (UAnimGraphNode_Root* R = Cast<UAnimGraphNode_Root>(N)) Root = R;
		if (!Graph || !Root) { Check(false, TEXT("AnimBlueprint: no AnimGraph / root")); return 1; }
		FGraphNodeCreator<UAnimGraphNode_OKPlayer> PlayerCreator(*Graph);
		UAnimGraphNode_OKPlayer* PlayerNode = PlayerCreator.CreateNode();
		PlayerNode->Node.Animation = Seq;
		PlayerNode->NodePosX = -600;
		PlayerCreator.Finalize();
		FGraphNodeCreator<UAnimGraphNode_OKContactIK> IKCreator(*Graph);
		UAnimGraphNode_OKContactIK* IKNode = IKCreator.CreateNode();
		IKNode->Node.Animation = Seq;
		IKNode->NodePosX = -300;
		IKCreator.Finalize();
		auto Pin = [](UEdGraphNode* N, EEdGraphPinDirection Dir) -> UEdGraphPin*
		{
			for (UEdGraphPin* P : N->Pins) if (P->Direction == Dir && P->PinType.PinCategory == TEXT("struct") && P->PinType.PinSubCategoryObject.IsValid()
				&& P->PinType.PinSubCategoryObject->GetName().Contains(TEXT("PoseLink"))) return P;
			return nullptr;
		};
		const UEdGraphSchema* Schema = Graph->GetSchema();
		const bool bLinked = Schema->TryCreateConnection(Pin(PlayerNode, EGPD_Output), Pin(IKNode, EGPD_Input)) && Schema->TryCreateConnection(Pin(IKNode, EGPD_Output), Pin(Root, EGPD_Input));
		FKismetEditorUtilities::CompileBlueprint(BP);
		Check(bLinked && BP->Status != BS_Error && BP->GeneratedClass, FString::Printf(TEXT("AnimBlueprint built and compiled (linked=%d status=%d, %d graph nodes)"), bLinked, int32(BP->Status), Graph->Nodes.Num()));
		if (BP->Status == BS_Error || !BP->GeneratedClass) return 1;
		// Evaluate on a mesh component in a scratch world.
		UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("OKAnimBPTest"));
		USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(World);
		Comp->SetSkeletalMesh(Mesh);
		Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Comp->SetAnimInstanceClass(BP->GeneratedClass);
		Comp->RegisterComponentWithWorld(World);
		Comp->InitAnim(true);
		Comp->bEnableUpdateRateOptimizations = false;
		Comp->TickAnimation(0.3f, false);
		Comp->RefreshBoneTransforms();
		Comp->FinalizeBoneTransform();
		const TArray<FTransform>& Got = Comp->GetBoneSpaceTransforms();
		FOKUnrealSkeleton Binding;
		TArray<FTransform> Expected;
		const bool bSampled = Binding.Build(Mesh->GetRefSkeleton(), nullptr, Family, Error) && Binding.SamplePose(Seq, 0.3f, Expected);
		// The foot chain is the IK's; every other bone must be the OK Player's pose.
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		const int32 FootBone = Ref.FindBoneIndex(TEXT("cf_j_leg03_L"));
		TSet<int32> Chain;
		for (int32 B = FootBone, K = 0; B != INDEX_NONE && K < 3; B = Ref.GetParentIndex(B), ++K) Chain.Add(B);
		double MaxDeg = 0.;
		int32 Compared = 0;
		for (int32 B = 0; bSampled && B < FMath::Min(Got.Num(), Expected.Num()); ++B)
		{
			if (Chain.Contains(B)) continue;
			MaxDeg = FMath::Max(MaxDeg, FOKUnrealSkeleton::AngleDegrees(Got[B].GetRotation(), Expected[B].GetRotation()));
			++Compared;
		}
		Check(bSampled && Compared > 100 && MaxDeg < 0.05, FString::Printf(TEXT("AnimBlueprint OK Player pose = SamplePose: %d bones, max %.4f deg %s"), Compared, MaxDeg, *Error));
		// The foot: at 0.3 hips heights above the ground (component space).
		const FTransform FootCS = Comp->GetBoneTransform(FootBone, FTransform::Identity);
		const int32 HipsBone = Ref.FindBoneIndex(TEXT("cf_j_hips"));
		const float HipsZ = Comp->GetBoneTransform(HipsBone, FTransform::Identity).GetTranslation().Z;
		const float Want = 0.3f * HipsZ;
		Check(FootBone != INDEX_NONE && FMath::Abs(FootCS.GetTranslation().Z - Want) < 1.5f,
			FString::Printf(TEXT("AnimBlueprint contact IK: left foot at %.1f cm (wanted %.1f = 0.3 x hips %.1f)"), FootCS.GetTranslation().Z, Want, HipsZ));
		Comp->UnregisterComponent();
		World->DestroyWorld(false);
		return 0;
	}

	/** Editor round trip of the OK animation tools; one PASS/FAIL line per check. */
	int32 SelfTest(const FString& Dest)
	{
		int32 Failures = 0;
		auto Check = [&](bool bOk, const FString& What) { UE_LOG(LogTemp, Display, TEXT("OK selftest %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What); Failures += bOk ? 0 : 1; };
		FString Error;
		// 1. Unreal animation (UE4 mannequin preset) -> OK asset.
		UAnimSequence* Walk = LoadObject<UAnimSequence>(nullptr, TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Walk_Fwd.Tutorial_Walk_Fwd"));
		UOKAnimSequence* Ok = Walk ? OKAnimationEditing::ConvertUnrealAnimation(Walk, nullptr, Dest / TEXT("SelfTest"), 30.f, Error) : nullptr;
		Check(Ok && Ok->Bones.Num() > 40 && Ok->Frames > 10, FString::Printf(TEXT("unreal->OK %s bones=%d frames=%d %s"), Walk ? *Walk->GetName() : TEXT("(missing)"), Ok ? Ok->Bones.Num() : 0, Ok ? Ok->Frames : 0, *Error));
		if (Ok)
		{
			const TArray<FString> Issues = OKAnimationEditing::Validate(Ok);
			Check(Issues.Num() <= 1, FString::Printf(TEXT("validate: %s"), *FString::Join(Issues, TEXT("; "))));
			// 2. Expand onto the source skeleton, write back, and compare the poses on that skeleton.
			FOKUnrealSkeleton Binding;
			Binding.Build(Walk->GetSkeleton()->GetReferenceSkeleton(), nullptr, UOKAnimationLibrary::GetDefaultFamily(), Error);
			TArray<TArray<float>> Before;
			for (int32 K = 0; K < 6; ++K) { TArray<float> S; Ok->SampleOnto(Binding.HeaderJson, Ok->Duration * K / 6.f, S); Before.Add(S); }
			FOKContact& Contact = Ok->Contacts.AddDefaulted_GetRef();
			Contact.Effector = TEXT("cf_j_leg03_L");
			Contact.Start = 0.2f;
			Contact.End = 0.6f;
			Ok->ApplyEdits(Error);
			UAnimSequence* Proxy = OKAnimationEditing::ExpandToAnimSequence(Ok, Walk->GetSkeleton(), Error);
			Check(Proxy && Proxy->GetNumberOfSampledKeys() == Ok->Frames, FString::Printf(TEXT("expand to AnimSequence keys=%d %s"), Proxy ? Proxy->GetNumberOfSampledKeys() : 0, *Error));
			const bool bBack = Proxy && OKAnimationEditing::WriteBack(Ok, Error);
			double MaxDiff = 0., MaxHips = 0.;
			for (int32 K = 0; K < 6 && bBack; ++K)
			{
				TArray<float> S;
				Ok->SampleOnto(Binding.HeaderJson, Ok->Duration * K / 6.f, S);
				// Rotations by angle (q and -q are the same rotation), then the hips offset.
				const int32 Bones = (FMath::Min(S.Num(), Before[K].Num()) - 3) / 4;
				for (int32 B = 0; B < Bones; ++B)
				{
					const FQuat4f Q0(Before[K][B * 4], Before[K][B * 4 + 1], Before[K][B * 4 + 2], Before[K][B * 4 + 3]);
					const FQuat4f Q1(S[B * 4], S[B * 4 + 1], S[B * 4 + 2], S[B * 4 + 3]);
					MaxDiff = FMath::Max(MaxDiff, FOKUnrealSkeleton::AngleDegrees(FQuat(Q0), FQuat(Q1)));
				}
				for (int32 I = Bones * 4; I < Bones * 4 + 3; ++I) MaxHips = FMath::Max(MaxHips, double(FMath::Abs(S[I] - Before[K][I])));
			}
			Check(bBack && MaxDiff < 0.1 && MaxHips < 1.e-3, FString::Printf(TEXT("write back round trip max %.4f deg, hips %.5f m %s"), MaxDiff, MaxHips, *Error));
			Check(Ok->Contacts.Num() == 1 && FMath::IsNearlyEqual(Ok->Contacts[0].Start, 0.2f, 0.02f) && FMath::IsNearlyEqual(Ok->Contacts[0].End, 0.6f, 0.02f),
				FString::Printf(TEXT("contacts survive as notifies (%d)"), Ok->Contacts.Num()));
			const FString LiveFile = FPaths::Combine(UOKAnimationLibrary::LiveFolder(), Ok->GetName() + TEXT(".okanim"));
			IFileManager::Get().Delete(*LiveFile);
			OKAnimationEditing::SaveAsset(Ok);
			if (Proxy) OKAnimationEditing::SaveAsset(Proxy);
			// Saving an OK animation writes its live copy for a running game (R3.10).
			Check(IFileManager::Get().FileSize(*LiveFile) > 100, FString::Printf(TEXT("live copy written on save: %s"), *LiveFile));
			// FBX: the walk exported to an .fbx and imported back as an OK animation equals the direct conversion.
			{
				const FString Fbx = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OKAnim/selftest_walk.fbx"));
				UAssetExportTask* Export = NewObject<UAssetExportTask>();
				Export->Object = Walk;
				Export->Filename = Fbx;
				Export->bAutomated = true;
				Export->bPrompt = false;
				Export->bReplaceIdentical = true;
				const bool bExported = UExporter::RunAssetExportTask(Export) && FPaths::FileExists(Fbx);
				const TArray<UOKAnimSequence*> FromFbx = bExported ? OKAnimationEditing::ImportFbxAsOk(Fbx, Dest / TEXT("SelfTest/Fbx"), Walk->GetSkeleton(), Error) : TArray<UOKAnimSequence*>();
				double FbxDiff = -1.;
				if (FromFbx.Num())
				{
					TArray<float> S0, S1;
					Ok->SampleOnto(Binding.HeaderJson, 0.4f, S0);
					FromFbx[0]->SampleOnto(Binding.HeaderJson, 0.4f, S1);
					FbxDiff = 0.;
					for (int32 Bn = 0; Bn + 7 < FMath::Min(S0.Num(), S1.Num()); Bn += 4)
						FbxDiff = FMath::Max(FbxDiff, FOKUnrealSkeleton::AngleDegrees(FQuat(S0[Bn], S0[Bn + 1], S0[Bn + 2], S0[Bn + 3]), FQuat(S1[Bn], S1[Bn + 1], S1[Bn + 2], S1[Bn + 3])));
				}
				Check(bExported && FromFbx.Num() >= 1 && FbxDiff >= 0. && FbxDiff < 0.5, FString::Printf(TEXT("FBX -> OK animation: exported=%d, %d animation(s), max %.3f deg from the direct conversion %s"), bExported, FromFbx.Num(), FbxDiff, *Error));
			}
			// Sequencer: a level sequence with the mesh spawned and the expanded animation on it.
			if (USkeletalMesh* WalkMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP")))
			{
				const ULevelSequence* Level = OKAnimationEditing::MakeEditSequence(Ok, WalkMesh, false, Error);
				const UMovieScene* Scene = Level ? Level->GetMovieScene() : nullptr;
				int32 Sections = 0;
				if (Scene && Scene->GetSpawnableCount() == 1)
					for (const FMovieSceneBinding& B : Scene->GetBindings())
						for (const UMovieSceneTrack* T : B.GetTracks()) if (T->IsA<UMovieSceneSkeletalAnimationTrack>()) Sections += T->GetAllSections().Num();
				Check(Level && Sections == 1, FString::Printf(TEXT("edit in Sequencer: level sequence %s, %d animation section %s"), Level ? *Level->GetName() : TEXT("-"), Sections, *Error));
				// Back through the same converter: the level sequence as an OK montage of its one shot.
				UOKAnimationAsset* Back = Level ? OKAnimationEditing::ConvertUnrealAsset(const_cast<ULevelSequence*>(Level), nullptr, Dest / TEXT("SelfTest/Other"), 30.f, Error) : nullptr;
				Check(Back && FMath::IsNearlyEqual(Back->GetPlayLength(), Ok->Duration, 0.05f), FString::Printf(TEXT("level sequence -> OK montage, length %.2f (animation %.2f) %s"), Back ? Back->GetPlayLength() : 0.f, Ok->Duration, *Error));
			}
			else Check(false, TEXT("TutorialTPP mesh missing"));
			// Other Unreal assets: a montage (two sections, the second loops), a 1D blend space, an additive copy.
			{
				UAnimMontage* M = NewObject<UAnimMontage>(GetTransientPackage(), TEXT("OKTestMontage"));
				M->SetSkeleton(Walk->GetSkeleton());
				FSlotAnimationTrack& Slot = M->SlotAnimTracks.Num() ? M->SlotAnimTracks[0] : M->SlotAnimTracks.AddDefaulted_GetRef();
				Slot.SlotName = TEXT("DefaultSlot");
				FAnimSegment Seg;
				Seg.SetAnimReference(Walk);
				Seg.StartPos = 0.f;
				Seg.AnimStartTime = 0.f;
				Seg.AnimEndTime = Walk->GetPlayLength();
				Seg.AnimPlayRate = 1.f;
				Slot.AnimTrack.AnimSegments.Add(Seg);
				M->SetCompositeLength(Walk->GetPlayLength());
				M->CompositeSections.Reset();
				FCompositeSection A; A.SectionName = TEXT("Start"); A.SetTime(0.f); A.NextSectionName = TEXT("Loop");
				FCompositeSection B; B.SectionName = TEXT("Loop"); B.SetTime(Walk->GetPlayLength() * 0.5f); B.NextSectionName = TEXT("Loop");
				M->CompositeSections = { A, B };
				UOKAnimMontage* OkM = Cast<UOKAnimMontage>(OKAnimationEditing::ConvertUnrealAsset(M, nullptr, Dest / TEXT("SelfTest/Other"), 30.f, Error));
				Check(OkM && OkM->Sections.Num() == 2 && OkM->Sections[1].Next == FName(TEXT("Loop")) && FMath::IsNearlyEqual(OkM->Sections[1].From, Walk->GetPlayLength() * 0.5f, 0.02f)
					&& FMath::IsNearlyEqual(OkM->GetPlayLength(), Walk->GetPlayLength(), 0.05f),
					FString::Printf(TEXT("montage -> OK montage: %d sections, second from %.2f s, length %.2f %s"), OkM ? OkM->Sections.Num() : 0, OkM && OkM->Sections.Num() > 1 ? OkM->Sections[1].From : -1.f, OkM ? OkM->GetPlayLength() : 0.f, *Error));
				UBlendSpace1D* BS = NewObject<UBlendSpace1D>(GetTransientPackage(), TEXT("OKTestBlend1D"));
				BS->SetSkeleton(Walk->GetSkeleton());
				BS->AddSample(Walk, FVector(0.f, 0.f, 0.f));
				BS->AddSample(Walk, FVector(100.f, 0.f, 0.f));
				UOKBlendSpace* OkB = Cast<UOKBlendSpace>(OKAnimationEditing::ConvertUnrealAsset(BS, nullptr, Dest / TEXT("SelfTest/Other"), 30.f, Error));
				Check(OkB && OkB->Axes.Num() == 1 && OkB->Samples.Num() == 2 && FMath::IsNearlyEqual(float(OkB->Samples[1].Position.X), 100.f),
					FString::Printf(TEXT("blend space 1D -> OK blend space: %d axes, %d samples %s"), OkB ? OkB->Axes.Num() : 0, OkB ? OkB->Samples.Num() : 0, *Error));
				UAnimSequence* Add = DuplicateObject<UAnimSequence>(Walk, GetTransientPackage(), TEXT("OKTestAdditive"));
				Add->AdditiveAnimType = AAT_LocalSpaceBase;
				Add->RefPoseType = ABPT_RefPose;
				UOKAnimSequence* OkAdd = UOKAnimationLibrary::ConvertUnrealAnimation(Add, nullptr, nullptr, 30.f, nullptr, NAME_None, Error);
				double AddDiff = -1.;
				if (OkAdd)
				{
					TArray<float> S0, S1;
					Ok->SampleOnto(Binding.HeaderJson, 0.3f, S0);
					OkAdd->SampleOnto(Binding.HeaderJson, 0.3f, S1);
					AddDiff = 0.;
					for (int32 Bn = 0; Bn + 3 < FMath::Min(S0.Num(), S1.Num()) - 3; Bn += 4)
						AddDiff = FMath::Max(AddDiff, FOKUnrealSkeleton::AngleDegrees(FQuat(S0[Bn], S0[Bn + 1], S0[Bn + 2], S0[Bn + 3]), FQuat(S1[Bn], S1[Bn + 1], S1[Bn + 2], S1[Bn + 3])));
				}
				Check(OkAdd && AddDiff >= 0. && AddDiff < 0.1, FString::Printf(TEXT("additive source -> the full pose it was authored as (max %.4f deg) %s"), AddDiff, *Error));
			}
			// Modifiers on a copy of the walk: foot plants, root travel baked out, a loop cut and closed.
			{
				UOKAnimSequence* W = NewObject<UOKAnimSequence>(GetTransientPackage(), TEXT("OKModifierTest"));
				W->SetData(Ok->Data, Error);
				W->Contacts.Reset();
				W->ApplyEdits(Error);
				const bool bFeet = W->ApplyModifier(TEXT("{\"op\":\"foot_contacts\"}"), Error);
				int32 Left = 0, Right = 0;
				for (const FOKContact& C : W->Contacts) { Left += C.Effector == TEXT("cf_j_leg03_L") && C.TargetRole == -1; Right += C.Effector == TEXT("cf_j_leg03_R") && C.TargetRole == -1; }
				Check(bFeet && Left >= 1 && Right >= 1, FString::Printf(TEXT("modifier foot_contacts on the walk: left %d, right %d plants %s"), Left, Right, *Error));
				const bool bInPlace = W->ApplyModifier(TEXT("{\"op\":\"in_place\"}"), Error);
				const TSharedPtr<FOkAnimPlayer> P = bInPlace ? W->CreatePlayer(Error) : nullptr;
				const float Travel = P ? FVector(P->RootDrift(0, W->Duration)).Size() : -1.f;
				Check(bInPlace, FString::Printf(TEXT("modifier in_place %s"), *Error));
				const int32 FramesBefore = W->Frames;
				const bool bLoop = W->ApplyModifier(TEXT("{\"op\":\"auto_loop\",\"min_seconds\":0.5}"), Error);
				Check(bLoop && W->bLooping && W->Frames <= FramesBefore && W->Frames >= 15 && !W->Meta.FindRef(TEXT("loop_error")).IsEmpty(),
					FString::Printf(TEXT("modifier auto_loop: %d -> %d frames, loop error %s %s"), FramesBefore, W->Frames, *W->Meta.FindRef(TEXT("loop_error")), *Error));
				const bool bSmooth = W->ApplyModifier(TEXT("{\"op\":\"smooth\",\"radius\":2}"), Error) && W->ApplyModifier(TEXT("{\"op\":\"renormalize\"}"), Error);
				Check(bSmooth && Travel >= 0.f, FString::Printf(TEXT("modifiers smooth + renormalize %s"), *Error));
				// Time tools: resample to 60 fps keeps the length, speed x2 halves it, trim keeps a range, insert adds a frame.
				const float Len = W->Duration;
				const bool bRes = W->ApplyModifier(TEXT("{\"op\":\"resample\",\"fps\":60}"), Error);
				const bool bResOk = bRes && FMath::IsNearlyEqual(W->Duration, Len, 0.02f) && FMath::IsNearlyEqual(W->Fps, 60.f);
				const bool bSpeed = W->ApplyModifier(TEXT("{\"op\":\"speed\",\"factor\":2}"), Error) && FMath::IsNearlyEqual(W->Duration, Len * 0.5f, 0.03f);
				const int32 F0 = W->Frames;
				const bool bInsert = W->ApplyModifier(TEXT("{\"op\":\"insert_frames\",\"at\":3,\"count\":2}"), Error) && W->Frames == F0 + 2;
				const bool bTrim = W->ApplyModifier(TEXT("{\"op\":\"trim\",\"start\":0.1,\"end\":0.3}"), Error) && FMath::IsNearlyEqual(W->Duration, 0.2f, 0.02f);
				Check(bResOk && bSpeed && bInsert && bTrim, FString::Printf(TEXT("time tools: resample=%d speed=%d insert=%d trim=%d (length %.3f) %s"), bResOk, bSpeed, bInsert, bTrim, W->Duration, *Error));
			}
		}
		// 3. Files from the game (OKAnim.BatchKK / OKAnim.PlayPair).
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *(FPaths::ProjectSavedDir() / TEXT("OKAnim/KK/female")), TEXT("*Loop*.okanim"), true, false);
		Files.Sort();
		TArray<UOKAnimSequence*> Kk;
		for (int32 I = 0; I < Files.Num() && Kk.Num() < 3; ++I)
			if (UOKAnimSequence* Seq = UOKAnimationLibrary::LoadOkAnimFile(Files[I], nullptr, NAME_None, Error)) Kk.Add(Seq);
		Check(Kk.Num() == 3, FString::Printf(TEXT("load .okanim files (%d of %d) %s"), Kk.Num(), Files.Num(), *Error));
		if (Kk.Num() == 3)
		{
			UOKBlendSpace* Blend = NewObject<UOKBlendSpace>(GetTransientPackage());
			Blend->Axes = { { TEXT("X"), 0.f, 1.f }, { TEXT("Y"), 0.f, 1.f } };
			Blend->Samples = { { Kk[0], FVector2D(0, 0) }, { Kk[1], FVector2D(1, 0) }, { Kk[2], FVector2D(0, 1) } };
			const TSharedPtr<FOkAnimPlayer> P = Blend->CreatePlayer(Error);
			TArray<float> S;
			const UOKSkeletonFamily* Family = UOKAnimationLibrary::GetDefaultFamily();
			if (P) { P->SetParam(0, 0.3f); P->SetParam(1, 0.4f); }
			const bool bSampled = P && Family && P->SampleOnto(0, Family->RestHeaderJson, 0.3f, S);
			Check(P && P->Duration() > 0.f && bSampled && S.Num() > 100,
				FString::Printf(TEXT("2D blend space player length=%.2f floats=%d %s"), P ? P->Duration() : 0.f, S.Num(), *Error));
			const TArray<FString> BlendIssues = OKAnimationEditing::Validate(Blend);
			Check(BlendIssues.IsEmpty(), FString::Printf(TEXT("blend validate: %s"), *FString::Join(BlendIssues, TEXT("; "))));
			UOKAnimMontage* Montage = NewObject<UOKAnimMontage>(GetTransientPackage());
			for (UOKAnimSequence* Seq : Kk) { FOKMontageSection& Sec = Montage->Sections.AddDefaulted_GetRef(); Sec.Name = FName(*Seq->GetName()); Sec.Animation = Seq; }
			const float Expected = Kk[0]->Duration + Kk[1]->Duration + Kk[2]->Duration - 2 * 0.2f;
			Check(FMath::IsNearlyEqual(Montage->GetPlayLength(), Expected, 0.01f), FString::Printf(TEXT("montage length %.3f (expected %.3f)"), Montage->GetPlayLength(), Expected));
			// Section editing: move the second section to 3 s with a 0.5 s length, blend 0.3, loop the third.
			const bool bTiming = OKAnimationEditing::SetMontageSectionTiming(Montage, 1, 3.f, 0.5f) && OKAnimationEditing::SetMontageSectionBlend(Montage, 1, 0.3f)
				&& OKAnimationEditing::SetMontageSectionNext(Montage, 2, Montage->Sections[2].Name) && !OKAnimationEditing::SetMontageSectionNext(Montage, 0, TEXT("NoSuchSection"));
			const TArray<TPair<float, float>> R = Montage->GetSectionRanges();
			Check(bTiming && R.Num() == 3 && FMath::IsNearlyEqual(R[1].Key, 3.f, 0.001f) && FMath::IsNearlyEqual(R[1].Value, 3.5f, 0.001f) && FMath::IsNearlyEqual(R[0].Key, 0.f, 0.001f)
				&& Montage->Sections[2].Next == Montage->Sections[2].Name && FMath::IsNearlyEqual(Montage->Sections[1].BlendIn, 0.3f),
				FString::Printf(TEXT("montage section editing: second %.2f-%.2f, blend %.2f, third loops"), R.Num() > 1 ? R[1].Key : -1.f, R.Num() > 1 ? R[1].Value : -1.f, Montage->Sections[1].BlendIn));
		}
		TArray<FString> Groups;
		IFileManager::Get().FindFilesRecursive(Groups, *(FPaths::ProjectSavedDir() / TEXT("OKAnim")), TEXT("*.okgroup"), true, false);
		Groups.Sort();
		if (Groups.Num())
		{
			TArray<uint8> Bytes;
			FFileHelper::LoadFileToArray(Bytes, *Groups[0]);
			UOKGroupAnimSequence* Group = Cast<UOKGroupAnimSequence>(OKAnimationEditing::CreateAsset(UOKGroupAnimSequence::StaticClass(), Dest / TEXT("SelfTest"), TEXT("Group")));
			const bool bGroup = Group && Group->SetFromPackage(Bytes, Group, Error);
			const TSharedPtr<FOkAnimPlayer> P = bGroup ? Group->CreatePlayer(Error) : nullptr;
			Check(P && P->Roles() == 2 && Group->Contacts.Num() > 0, FString::Printf(TEXT("group %s roles=%d contacts=%d %s"), *Groups[0], P ? P->Roles() : 0, Group ? Group->Contacts.Num() : 0, *Error));
			if (bGroup) OKAnimationEditing::SaveAsset(Group);
		}
		else Check(false, TEXT("no .okgroup file (run OKAnim.PlayPair in the game first)"));
		// 4. A preview mesh of the default family (bones = family + root, a body-sized box figure).
		{
			UOKSkeletonFamily* Family = OKAnimationEditing::EnsureDefaultFamilyAsset(Error, FParse::Param(FCommandLine::Get(), TEXT("RebuildFamilyPreview")));
			USkeletalMesh* Mesh = Family ? Family->PreviewMesh.LoadSynchronous() : nullptr;
			const int32 FamilyBones = Family ? Family->GetBoneNames().Num() : 0;
			const FBox Box = Mesh ? Mesh->GetBounds().GetBox() : FBox(ForceInit);
			Check(Mesh && Mesh->GetRefSkeleton().GetNum() == FamilyBones + 1 && Box.GetSize().Z > 100.f && Box.GetSize().Z < 250.f,
				FString::Printf(TEXT("family preview mesh %s bones=%d (family %d) height=%.1f cm %s"), Mesh ? *Mesh->GetPathName() : TEXT("-"),
					Mesh ? Mesh->GetRefSkeleton().GetNum() : 0, FamilyBones, Box.GetSize().Z, *Error));
			// The male rest's own preview mesh (KK men have their own rest).
			{
				const TSoftObjectPtr<USkeletalMesh>* MaleRef = Family ? Family->VariantPreviewMeshes.Find(TEXT("male")) : nullptr;
				USkeletalMesh* Male = MaleRef ? MaleRef->LoadSynchronous() : nullptr;
				int32 MaleBones = 0;
				TSharedPtr<FJsonObject> MH;
				if (Family && Family->RestVariants.Contains(TEXT("male")) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Family->RestVariants[TEXT("male")]), MH) && MH) MaleBones = MH->GetArrayField(TEXT("bones")).Num();
				const FBox MaleBox = Male ? Male->GetBounds().GetBox() : FBox(ForceInit);
				Check(Male && MaleBones > 100 && Male->GetRefSkeleton().GetNum() == MaleBones + 1 && MaleBox.GetSize().Z > 100.f,
					FString::Printf(TEXT("male preview mesh %s bones=%d (male rest %d) height=%.1f cm"), Male ? *Male->GetPathName() : TEXT("-"), Male ? Male->GetRefSkeleton().GetNum() : 0, MaleBones, MaleBox.GetSize().Z));
			}
			// The KK clips map onto it one to one (KK preset), so they can be animated directly.
			if (Mesh && Kk.Num())
			{
				FOKUnrealSkeleton Binding;
				const bool bBound = Binding.Build(Mesh->GetRefSkeleton(), nullptr, Family, Error);
				TArray<FTransform> Pose;
				const bool bPosed = bBound && Binding.SamplePose(Kk[0], 0.3f, Pose);
				Check(bPosed && Binding.BoneIndex.Num() == FamilyBones, FString::Printf(TEXT("KK animation on the preview mesh: %d bones mapped %s"), Binding.BoneIndex.Num(), *Error));
				// A blank (rest pose) KK animation to look at the preview figure's rest.
				if (!LoadObject<UOKAnimSequence>(nullptr, *(Dest / TEXT("SelfTest/KK_Blank.KK_Blank"))))
					if (UOKAnimSequence* Blank = UOKAnimationLibrary::MakeRestAnimation(Family, 31, 30.f, CreatePackage(*(Dest / TEXT("SelfTest/KK_Blank"))), TEXT("KK_Blank"), Error))
						OKAnimationEditing::SaveAsset(Blank);
				// Kept as an asset for the editor self test on a KK clip (OKAnim.OpenEditor /Game/OKAnimation/SelfTest/KK_Clip).
				if (!LoadObject<UOKAnimSequence>(nullptr, *(Dest / TEXT("SelfTest/KK_Clip.KK_Clip"))))
					if (UOKAnimSequence* Clip = Cast<UOKAnimSequence>(OKAnimationEditing::CreateAsset(UOKAnimSequence::StaticClass(), Dest / TEXT("SelfTest"), TEXT("KK_Clip"))))
						if (Clip->SetData(Kk[0]->Data, Error)) OKAnimationEditing::SaveAsset(Clip);
			}
		}
		// Quality check: a KK clip with a broken forearm at frame 10 and the body sunk into the ground from frame 20.
		if (Kk.Num())
		{
			UOKAnimSequence* Q = NewObject<UOKAnimSequence>(GetTransientPackage(), TEXT("OKQualityTest"));
			TArray<float> Motion;
			TArray<uint8> Broken;
			const int32 Tracks = Kk[0]->Bones.Num(), Frames = Kk[0]->Frames;
			const int32 Arm = Kk[0]->Bones.IndexOfByKey(FName(TEXT("cf_j_forearm01_L")));
			bool bMade = Frames > 25 && Arm != INDEX_NONE && FOkAnimBridge::GetTracks(Kk[0]->Data, Motion) && Motion.Num() == Frames * Tracks * 4 + Frames * 3;
			if (bMade)
			{
				const FQuat Bad(FVector(1., 0., 0.), FMath::DegreesToRadians(160.));
				float* Qv = Motion.GetData() + (10 * Tracks + Arm) * 4;
				Qv[0] = Bad.X; Qv[1] = Bad.Y; Qv[2] = Bad.Z; Qv[3] = Bad.W;
				for (int32 F = 20; F < Frames; ++F) Motion[Frames * Tracks * 4 + F * 3 + 1] -= 0.6f;
				bMade = FOkAnimBridge::SetTracks(Kk[0]->Data, Motion, Broken) && Q->SetData(Broken, Error);
			}
			TArray<UOKAnimSequence::FQualityIssue> Clean, Found;
			Kk[0]->CheckQuality(Clean, Error);
			const bool bChecked = bMade && Q->CheckQuality(Found, Error);
			bool bArm = false, bGround = false;
			for (const UOKAnimSequence::FQualityIssue& I : Found)
			{
				bArm |= I.Bone == FName(TEXT("cf_j_forearm01_L")) && I.Start <= 11 && I.End >= 10 && (I.Kind == TEXT("pop") || I.Kind == TEXT("joint_extreme"));
				bGround |= I.Kind == TEXT("below_ground") && I.Start >= 19;
			}
			FString List;
			for (int32 I = 0; I < FMath::Min(6, Found.Num()); ++I) List += FString::Printf(TEXT(" %s:%s@%d-%d"), *Found[I].Kind, *Found[I].Bone.ToString(), Found[I].Start, Found[I].End);
			Check(bChecked && bArm && bGround, FString::Printf(TEXT("quality check finds the injected problems (clean clip %d issues, broken %d:%s) %s"), Clean.Num(), Found.Num(), *List, *Error));
		}
		// Metadata: the structured info goes into the bytes and comes back from a saved .okanim.
		if (Kk.Num())
		{
			UOKAnimSequence* M = NewObject<UOKAnimSequence>(GetTransientPackage(), TEXT("OKMetaTest"));
			M->SetData(Kk[0]->Data, Error);
			M->Info.Category = TEXT("selftest");
			M->Info.Tags = { TEXT("a"), TEXT("b") };
			M->Info.License = TEXT("CC0");
			M->Info.FadeIn = 0.35f;
			M->Info.FadeOut = 0.15f;
			M->Info.RootMotion = EOKRootMotion::InPlace;
			M->Info.HandIK = 0.5f;
			M->Info.FootIK = 0.25f;
			M->Info.bFootLock = false;
			const FString File = FPaths::ProjectSavedDir() / TEXT("OKAnim/selftest_meta.okanim");
			const bool bSaved = M->ApplyEdits(Error) && UOKAnimationLibrary::SaveOkAnimFile(M, File);
			UOKAnimSequence* Back = bSaved ? UOKAnimationLibrary::LoadOkAnimFile(File, GetTransientPackage(), TEXT("OKMetaBack"), Error) : nullptr;
			const bool bSame = Back && Back->Info.Category == TEXT("selftest") && Back->Info.Tags.Num() == 2 && Back->Info.License == TEXT("CC0")
				&& FMath::IsNearlyEqual(Back->Info.FadeIn, 0.35f) && FMath::IsNearlyEqual(Back->Info.FadeOut, 0.15f) && Back->Info.RootMotion == EOKRootMotion::InPlace
				&& FMath::IsNearlyEqual(Back->Info.HandIK, 0.5f) && FMath::IsNearlyEqual(Back->Info.FootIK, 0.25f) && !Back->Info.bFootLock;
			Check(bSame, FString::Printf(TEXT("metadata round trip through a .okanim file (category=%s tags=%d root=%d) %s"), Back ? *Back->Info.Category : TEXT("-"), Back ? Back->Info.Tags.Num() : 0,
				Back ? int32(Back->Info.RootMotion) : -1, *Error));
			IFileManager::Get().Delete(*File);
		}
		// Thumbnail: the middle frame of a KK clip as a stick figure filling the tile.
		if (Kk.Num())
		{
			TArray<TPair<FVector2D, FVector2D>> Lines;
			const bool bLines = UOKAnimThumbnailRenderer::PoseLines(Kk[0], Kk[0]->Duration * 0.5f, Lines);
			FBox2D Box(ForceInit);
			for (const auto& L : Lines) { Box += L.Key; Box += L.Value; }
			Check(bLines && Lines.Num() > 100 && Box.Min.X >= 0. && Box.Max.X <= 1. && Box.Min.Y >= 0. && Box.Max.Y <= 1. && FMath::Max(Box.GetSize().X, Box.GetSize().Y) > 0.85,
				FString::Printf(TEXT("thumbnail stick figure: %d lines in (%.2f,%.2f)-(%.2f,%.2f)"), Lines.Num(), Box.Min.X, Box.Min.Y, Box.Max.X, Box.Max.Y));
		}
		// Family mode data: mirror pairs from the names, an IK chain, aliases; saved as an asset.
		if (UOKSkeletonFamily* Base = UOKAnimationLibrary::GetDefaultFamily())
		{
			// A fresh package each run (saving over an earlier run's, only partially loaded here, fails: error #85).
			const FString Name = FString::Printf(TEXT("KK_FamilyEdit_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
			UPackage* Pkg = CreatePackage(*(Dest / TEXT("SelfTest") / Name));
			UOKSkeletonFamily* F = DuplicateObject<UOKSkeletonFamily>(Base, Pkg, FName(*Name));
			F->SetFlags(RF_Public | RF_Standalone);
			F->MirrorPairs.Reset();
			const int32 Pairs = F->AutoMirrorPairs();
			FOKIKChain& Arm = F->IKChains.AddDefaulted_GetRef();
			Arm.Name = TEXT("ArmL"); Arm.Root = TEXT("cf_j_arm00_L"); Arm.Mid = TEXT("cf_j_forearm01_L"); Arm.End = TEXT("cf_j_hand_L");
			F->Aliases.Add(TEXT("mod_hand_L"), TEXT("cf_j_hand_L"));
			const bool bSaved = OKAnimationEditing::SaveAsset(F);
			Check(Pairs > 20 && F->MirrorOf(TEXT("cf_j_hand_L")) == FName(TEXT("cf_j_hand_R")) && F->MirrorOf(TEXT("cf_j_hand_R")) == FName(TEXT("cf_j_hand_L")) && F->MirrorOf(TEXT("cf_j_hips")).IsNone() && bSaved,
				FString::Printf(TEXT("family edit: %d mirror pairs, IK chain, alias, saved=%d"), Pairs, bSaved));
			IFileManager::Get().Delete(*FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension()));
		}
		// 5. AnimBlueprint: OK Player -> OK Contact IK -> Output, compiled and evaluated on the family preview mesh.
		{
			UOKSkeletonFamily* Family = UOKAnimationLibrary::GetDefaultFamily();
			USkeletalMesh* Mesh = Family ? Family->PreviewMesh.LoadSynchronous() : nullptr;
			if (Mesh && Kk.Num())
			{
				AnimBlueprintTest(Mesh, Family, Kk[0], Dest, Check);   // its checks count the failures
			}
			else Check(false, TEXT("AnimBlueprint test needs the family preview mesh and a KK clip"));
		}
		// 6. BVH import: a CMU-style file, the left elbow bent 90 degrees in frame 1.
		{
			auto J = [](const TCHAR* Name, const TCHAR* Offset, const FString& Inner)
			{
				return FString::Printf(TEXT("JOINT %s\n{\nOFFSET %s\nCHANNELS 3 Zrotation Xrotation Yrotation\n%s}\n"), Name, Offset, *Inner);
			};
			const FString End = TEXT("End Site\n{\nOFFSET 0 0 0\n}\n");
			const FString LeftArm = J(TEXT("LeftShoulder"), TEXT("3 20 0"), J(TEXT("LeftArm"), TEXT("15 0 0"), J(TEXT("LeftForeArm"), TEXT("28 0 0"), J(TEXT("LeftHand"), TEXT("25 0 0"), End))));
			const FString RightArm = J(TEXT("RightShoulder"), TEXT("-3 20 0"), J(TEXT("RightArm"), TEXT("-15 0 0"), J(TEXT("RightForeArm"), TEXT("-28 0 0"), J(TEXT("RightHand"), TEXT("-25 0 0"), End))));
			const FString Head = J(TEXT("Neck"), TEXT("0 8 0"), J(TEXT("Head"), TEXT("0 10 0"), End));
			const FString Spine = J(TEXT("LowerBack"), TEXT("0 8 0"), J(TEXT("Spine"), TEXT("0 12 0"), J(TEXT("Spine1"), TEXT("0 12 0"), Head + LeftArm + RightArm)));
			const FString LeftLeg = J(TEXT("LeftUpLeg"), TEXT("9 -5 0"), J(TEXT("LeftLeg"), TEXT("0 -42 0"), J(TEXT("LeftFoot"), TEXT("0 -40 0"), J(TEXT("LeftToeBase"), TEXT("0 -5 12"), End))));
			const FString RightLeg = J(TEXT("RightUpLeg"), TEXT("-9 -5 0"), J(TEXT("RightLeg"), TEXT("0 -42 0"), J(TEXT("RightFoot"), TEXT("0 -40 0"), J(TEXT("RightToeBase"), TEXT("0 -5 12"), End))));
			FString Text = TEXT("HIERARCHY\nROOT Hips\n{\nOFFSET 0 0 0\nCHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation\n") + Spine + LeftLeg + RightLeg + TEXT("}\n");
			// Joints in file order: Hips, LowerBack, Spine, Spine1, Neck, Head, LeftShoulder, LeftArm, LeftForeArm, LeftHand, RightShoulder..RightHand, LeftUpLeg..LeftToeBase, RightUpLeg..RightToeBase.
			const int32 Joints = 22, ForeArm = 8;
			Text += TEXT("MOTION\nFrames: 3\nFrame Time: 0.0333333\n");
			for (int32 F = 0; F < 3; ++F)
			{
				Text += TEXT("0 92 0 0 0 0");
				for (int32 K = 1; K < Joints; ++K) Text += K == ForeArm && F == 1 ? TEXT(" 0 0 90") : TEXT(" 0 0 0");
				Text += TEXT("\n");
			}
			FOKBvh Bvh;
			const bool bParsed = Bvh.Parse(Text, Error);
			Check(bParsed && Bvh.Ref.GetNum() == Joints && Bvh.Frames.Num() == 3 && FMath::IsNearlyEqual(Bvh.Fps, 30.f, 0.1f),
				FString::Printf(TEXT("BVH parse: %d joints, %d frames, %.1f fps, scale %.2f %s"), Bvh.Ref.GetNum(), Bvh.Frames.Num(), Bvh.Fps, Bvh.Scale, *Error));
			UOKAnimSequence* Seq = bParsed ? UOKAnimationLibrary::ImportBvhText(Text, TEXT("selftest.bvh"), GetTransientPackage(), TEXT("OKBvhTest"), Error) : nullptr;
			Check(Seq && Seq->Frames == 3, FString::Printf(TEXT("BVH -> OK animation: %d bones, %d frames %s"), Seq ? Seq->Bones.Num() : 0, Seq ? Seq->Frames : 0, *Error));
			if (Seq)
			{
				// Back onto the BVH skeleton: every mapped joint's local rotation as in the file.
				FOKUnrealSkeleton Binding;
				TArray<FTransform> Pose;
				double MaxDeg = 0.;
				if (Binding.Build(Bvh.Ref, nullptr, UOKAnimationLibrary::GetDefaultFamily(), Error) && Binding.SamplePose(Seq, 1.f / Bvh.Fps, Pose))
					for (const int32 B : Binding.BoneIndex) MaxDeg = FMath::Max(MaxDeg, FOKUnrealSkeleton::AngleDegrees(Pose[B].GetRotation(), Bvh.Frames[1][B].GetRotation()));
				Check(Pose.Num() == Joints && MaxDeg < 0.5, FString::Printf(TEXT("BVH round trip on its skeleton: %d mapped joints, max %.3f deg %s"), Binding.BoneIndex.Num(), MaxDeg, *Error));
				// On the KK preview figure: the left elbow bends 90 degrees in frame 1 (and is straight-ish in frame 0).
				UOKSkeletonFamily* Family = UOKAnimationLibrary::GetDefaultFamily();
				USkeletalMesh* Mesh = Family ? Family->PreviewMesh.LoadSynchronous() : nullptr;
				FOKUnrealSkeleton KkBinding;
				auto Elbow = [&](float T) -> double
				{
					TArray<FTransform> Local;
					if (!KkBinding.SamplePose(Seq, T, Local)) return -1.;
					const FReferenceSkeleton& R = Mesh->GetRefSkeleton();
					TArray<FTransform> C;
					C.SetNum(Local.Num());
					for (int32 I = 0; I < Local.Num(); ++I) { const int32 P = R.GetParentIndex(I); C[I] = P == INDEX_NONE ? Local[I] : Local[I] * C[P]; }
					const FVector A = C[R.FindBoneIndex(TEXT("cf_j_arm00_L"))].GetTranslation(), Fo = C[R.FindBoneIndex(TEXT("cf_j_forearm01_L"))].GetTranslation(),
						H = C[R.FindBoneIndex(TEXT("cf_j_hand_L"))].GetTranslation();
					return FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct((Fo - A).GetSafeNormal(), (H - Fo).GetSafeNormal())));
				};
				const bool bKk = Mesh && KkBinding.Build(Mesh->GetRefSkeleton(), nullptr, Family, Error);
				const double Bend0 = bKk ? Elbow(0.f) : -1., Bend1 = bKk ? Elbow(1.f / Bvh.Fps) : -1.;
				Check(bKk && Bend0 >= 0. && Bend0 < 15. && FMath::Abs(Bend1 - 90.) < 3., FString::Printf(TEXT("BVH on the KK figure: left elbow %.1f deg -> %.1f deg (90 wanted) %s"), Bend0, Bend1, *Error));
			}
		}
		UE_LOG(LogTemp, Display, TEXT("OK selftest done: %d failures"), Failures);
		return Failures == 0 ? 0 : 1;
	}
}

int32 UOKBatchConvertCommandlet::Main(const FString& Params)
{
	// -Fbx=<file or folder>: FBX files -> OK animations (Dest=).
	FString FbxPath;
	if (FParse::Value(*Params, TEXT("Fbx="), FbxPath))
	{
		FString FbxDest = GetDefault<UOKAnimationSettings>()->AssetRoot / TEXT("Fbx");
		FParse::Value(*Params, TEXT("Dest="), FbxDest);
		TArray<FString> FbxFiles;
		if (FPaths::DirectoryExists(FbxPath)) IFileManager::Get().FindFilesRecursive(FbxFiles, *FbxPath, TEXT("*.fbx"), true, false);
		else FbxFiles.Add(FbxPath);
		int32 Made = 0;
		for (const FString& F : FbxFiles)
		{
			FString Error;
			for (UOKAnimSequence* Ok : OKAnimationEditing::ImportFbxAsOk(F, FbxDest, nullptr, Error)) { OKAnimationEditing::SaveAsset(Ok); ++Made; }
			if (!Error.IsEmpty()) UE_LOG(LogTemp, Warning, TEXT("OK fbx %s: %s"), *F, *Error);
		}
		UE_LOG(LogTemp, Display, TEXT("OK fbx: %d OK animations from %d files"), Made, FbxFiles.Num());
		return Made > 0 ? 0 : 1;
	}
	// -Score[=<dir>]: quality scores of every converted clip (default Saved/OKAnim/KK) -> score.json + review.txt.
	FString ScoreDir;
	if (FParse::Param(*Params, TEXT("Score")) || FParse::Value(*Params, TEXT("Score="), ScoreDir))
	{
		if (ScoreDir.IsEmpty()) ScoreDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OKAnim/KK"));
		FString Summary;
		const double T0 = FPlatformTime::Seconds();
		const int32 N = OKAnimationEditing::ScoreFolder(ScoreDir, 300, Summary);
		UE_LOG(LogTemp, Display, TEXT("OK score %s: %s in %.0f s"), *ScoreDir, *Summary, FPlatformTime::Seconds() - T0);
		return N > 0 ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("SelfTest")))
	{
		FString Dest = GetDefault<UOKAnimationSettings>()->AssetRoot;
		FParse::Value(*Params, TEXT("Dest="), Dest);
		return SelfTest(Dest);
	}
	FString Source, Dest, ProfilePath, Files;
	float Fps = GetDefault<UOKAnimationSettings>()->ImportFps;
	FParse::Value(*Params, TEXT("Source="), Source);
	FParse::Value(*Params, TEXT("Files="), Files);
	FParse::Value(*Params, TEXT("Profile="), ProfilePath);
	FParse::Value(*Params, TEXT("Fps="), Fps);
	if (!FParse::Value(*Params, TEXT("Dest="), Dest)) Dest = GetDefault<UOKAnimationSettings>()->AssetRoot;
	int32 Done = 0, Failed = 0;
	const double Start = FPlatformTime::Seconds();

	if (!Source.IsEmpty())
	{
		UOKRetargetProfile* Profile = ProfilePath.IsEmpty() ? GetDefault<UOKAnimationSettings>()->DefaultUnrealProfile.LoadSynchronous() : LoadObject<UOKRetargetProfile>(nullptr, *ProfilePath);
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		Registry.SearchAllAssets(true);
		TArray<FAssetData> Assets;
		Registry.GetAssetsByPath(FName(*Source), Assets, true);
		for (const FAssetData& Data : Assets)
		{
			if (Data.AssetClassPath != UAnimSequence::StaticClass()->GetClassPathName()) continue;
			UAnimSequence* Seq = Cast<UAnimSequence>(Data.GetAsset());
			const FString Folder = Dest / Data.PackagePath.ToString().RightChop(Source.Len());
			FString Error;
			UOKAnimSequence* Ok = OKAnimationEditing::ConvertUnrealAnimation(Seq, Profile, Folder, Fps, Error);
			if (Ok && OKAnimationEditing::SaveAsset(Ok)) { ++Done; UE_LOG(LogTemp, Display, TEXT("OK batch: %s -> %s"), *Data.PackageName.ToString(), *Ok->GetPathName()); }
			else { ++Failed; UE_LOG(LogTemp, Warning, TEXT("OK batch: %s failed: %s"), *Data.PackageName.ToString(), *Error); }
		}
	}
	if (!Files.IsEmpty())
	{
		TArray<FString> Found;
		IFileManager::Get().FindFilesRecursive(Found, *Files, TEXT("*.okanim"), true, false);
		IFileManager::Get().FindFilesRecursive(Found, *Files, TEXT("*.okgroup"), true, false, false);
		for (const FString& File : Found)
		{
			TArray<uint8> Bytes;
			FString Error;
			const FString Relative = FPaths::GetPath(File).RightChop(Files.Len()).Replace(TEXT("\\"), TEXT("/"));
			const FString Folder = Dest / Relative;
			const FString Name = ObjectTools::SanitizeObjectName(FPaths::GetBaseFilename(File));
			UObject* Asset = nullptr;
			if (FFileHelper::LoadFileToArray(Bytes, *File))
			{
				if (FPaths::GetExtension(File) == TEXT("okgroup"))
				{
					UOKGroupAnimSequence* Group = Cast<UOKGroupAnimSequence>(OKAnimationEditing::CreateAsset(UOKGroupAnimSequence::StaticClass(), Folder, Name));
					if (Group && Group->SetFromPackage(Bytes, Group, Error)) Asset = Group;
				}
				else
				{
					UOKAnimSequence* Seq = Cast<UOKAnimSequence>(OKAnimationEditing::CreateAsset(UOKAnimSequence::StaticClass(), Folder, Name));
					if (Seq && Seq->SetData(Bytes, Error)) { Seq->Meta.Add(TEXT("imported_from"), File); Asset = Seq; }
				}
			}
			else Error = TEXT("unreadable");
			if (Asset && OKAnimationEditing::SaveAsset(Asset)) ++Done;
			else { ++Failed; UE_LOG(LogTemp, Warning, TEXT("OK batch: %s failed: %s"), *File, *Error); }
		}
	}
	UE_LOG(LogTemp, Display, TEXT("OK batch convert: %d converted, %d failed in %.1f s"), Done, Failed, FPlatformTime::Seconds() - Start);
	return Failed == 0 && Done > 0 ? 0 : 1;
}
