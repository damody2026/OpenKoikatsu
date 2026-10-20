#include "OKStudioSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/TargetPoint.h"
#include "LevelSequence.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "OKSceneDocument.h"
#include "OKStudioCatalog.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "OKSceneActor.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	/** The actor an object is exported as (a transient instance the sequence copies into its spawnable template). */
	AActor* TemplateActor(const FOKSceneObject& O, const FOKScenePost& Post)
	{
		UObject* Outer = GetTransientPackage();
		switch (O.Kind)
		{
		case EOKSceneObjectKind::Camera:
		{
			ACineCameraActor* A = NewObject<ACineCameraActor>(Outer, NAME_None, RF_Transient);
			UCineCameraComponent* Cine = A->GetCineCameraComponent();
			Cine->SetFieldOfView(O.Camera.Fov);
			// The scene's post settings, and a fixed exposure: auto exposure adapts to the (often empty, black) background
			// and washes the characters out.
			Post.ApplyTo(Cine->PostProcessSettings);
			Cine->PostProcessSettings.bOverride_AutoExposureMethod = true;
			Cine->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
			Cine->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
			Cine->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
			Cine->PostProcessBlendWeight = 1.f;
			return A;
		}
		case EOKSceneObjectKind::Light:
		{
			ALight* A = O.Light.Type == EOKSceneLightType::Directional ? static_cast<ALight*>(NewObject<ADirectionalLight>(Outer, NAME_None, RF_Transient))
				: O.Light.Type == EOKSceneLightType::Spot ? static_cast<ALight*>(NewObject<ASpotLight>(Outer, NAME_None, RF_Transient))
				: static_cast<ALight*>(NewObject<APointLight>(Outer, NAME_None, RF_Transient));
			if (ULightComponent* L = A->GetLightComponent()) { L->Intensity = O.Light.Intensity; L->LightColor = O.Light.Color.ToFColor(true); L->CastShadows = O.Light.bShadows; }
			if (UPointLightComponent* P = Cast<UPointLightComponent>(A->GetLightComponent())) P->AttenuationRadius = O.Light.Range;
			if (USpotLightComponent* S = Cast<USpotLightComponent>(A->GetLightComponent())) S->OuterConeAngle = O.Light.SpotAngle;
			return A;
		}
		case EOKSceneObjectKind::Effect:
		{
			ANiagaraActor* A = NewObject<ANiagaraActor>(Outer, NAME_None, RF_Transient);
			const FOKStudioEffect* E = FOKStudioCatalog::Get().FindEffect(O.Effect.Effect);
			if (E && A->GetNiagaraComponent()) A->GetNiagaraComponent()->SetAsset(LoadObject<UNiagaraSystem>(nullptr, *E->Asset));
			return A;
		}
		default:
			return NewObject<ATargetPoint>(Outer, NAME_None, RF_Transient);
		}
	}

	/** Brings an angle within 180 degrees of the previous key's, so the curve turns the short way. */
	/** A spawnable added from code exists only where its spawn track says so (the editor adds one when you drag an
	 *  actor in): spawned for the whole sequence. */
	void SpawnWholeSequence(UMovieScene* Scene, const FGuid& Binding)
	{
		UMovieSceneSpawnTrack* SpawnTrack = Cast<UMovieSceneSpawnTrack>(Scene->AddTrack(UMovieSceneSpawnTrack::StaticClass(), Binding));
		UMovieSceneBoolSection* SpawnSection = SpawnTrack ? Cast<UMovieSceneBoolSection>(SpawnTrack->CreateNewSection()) : nullptr;
		if (!SpawnSection) return;
		SpawnSection->GetChannel().SetDefault(true);
		SpawnSection->SetRange(TRange<FFrameNumber>::All());
		SpawnTrack->AddSection(*SpawnSection);
		SpawnTrack->SetObjectId(Binding);
	}

	double Unwind(double Angle, double Previous)
	{
		while (Angle - Previous > 180.0) Angle -= 360.0;
		while (Angle - Previous < -180.0) Angle += 360.0;
		return Angle;
	}
}

ULevelSequence* FOKStudioSequence::Export(const UOKSceneDocument* Document, const FString& Name, FString& OutReport, FString& OutError)
{
	if (!Document) { OutError = TEXT("no scene"); return nullptr; }
	const FString Clean = FPaths::MakeValidFileName(Name.IsEmpty() ? FString(TEXT("OKStudioScene")) : Name).Replace(TEXT(" "), TEXT("_"));
	const FString PackageName = TEXT("/Game/OKStudio/Sequences/") + Clean;
	UPackage* Package = CreatePackage(*PackageName);
	// Exporting again replaces the sequence: a package already on disk is loaded completely (a partly loaded one cannot
	// be saved) and the old sequence moved out of the way.
	Package->FullyLoad();
	if (UObject* Old = StaticFindObject(nullptr, Package, *Clean))
		Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
	ULevelSequence* Sequence = NewObject<ULevelSequence>(Package, *Clean, RF_Public | RF_Standalone | RF_Transactional);
	Sequence->Initialize();
	UMovieScene* Scene = Sequence->GetMovieScene();
	Scene->SetDisplayRate(FFrameRate(30, 1));
	const FFrameRate Tick = Scene->GetTickResolution();
	const float Duration = FMath::Max(Document->Duration, 1.f / 30.f);
	Scene->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), Tick.AsFrameNumber(Duration)));
	int32 Bindings = 0, Keys = 0, PropertyTracks = 0;
	FGuid CameraCut;
	// The whole OK scene (map, characters with their animation / FK / IK, items, lights, effects) is one spawnable
	// scene actor that shows the scene timeline at its SequenceTime, keyed from 0 to the scene length; Sequencer and
	// Movie Render Queue see the real characters. Cameras stay their own spawnables (camera cut, focal length).
	{
		AOKSceneActor* Instance = NewObject<AOKSceneActor>(GetTransientPackage(), NAME_None, RF_Transient);
		Instance->SceneJson = Document->ToJson();
		UObject* Template = Sequence->MakeSpawnableTemplateFromInstance(*Instance, TEXT("OK Scene"));
		Instance->MarkAsGarbage();
		if (!Template) { OutError = TEXT("the sequence made no template for the scene"); return nullptr; }
		const FGuid Binding = Scene->AddSpawnable(TEXT("OK Scene"), *Template);
		SpawnWholeSequence(Scene, Binding);
		++Bindings;
		UMovieSceneFloatTrack* TimeTrack = Scene->AddTrack<UMovieSceneFloatTrack>(Binding);
		TimeTrack->SetPropertyNameAndPath(TEXT("SequenceTime"), TEXT("SequenceTime"));
		UMovieSceneFloatSection* TimeSection = Cast<UMovieSceneFloatSection>(TimeTrack->CreateNewSection());
		TimeSection->SetRange(TRange<FFrameNumber>::All());
		TimeTrack->AddSection(*TimeSection);
		TimeSection->GetChannel().AddLinearKey(FFrameNumber(0), 0.f);
		TimeSection->GetChannel().AddLinearKey(Tick.AsFrameNumber(Duration), Duration);
		++PropertyTracks;
	}
	for (const FOKSceneTrack& Track : Document->Tracks)
	{
		const FOKSceneObject* O = Document->Find(Track.ObjectId);
		// Everything but cameras is drawn by the scene spawnable.
		if (!O || Track.Keys.IsEmpty() || O->Kind != EOKSceneObjectKind::Camera) continue;
		AActor* Instance = TemplateActor(*O, Document->Post);
		UObject* Template = Sequence->MakeSpawnableTemplateFromInstance(*Instance, *O->Name);
		Instance->MarkAsGarbage();
		if (!Template) { OutError = TEXT("the sequence made no template for ") + O->Name; return nullptr; }
		const FGuid Binding = Scene->AddSpawnable(O->Name, *Template);
		SpawnWholeSequence(Scene, Binding);
		++Bindings;
		if (Track.ObjectId == Document->ActiveCamera) CameraCut = Binding;
		UMovieScene3DTransformTrack* Transform = Scene->AddTrack<UMovieScene3DTransformTrack>(Binding);
		UMovieScene3DTransformSection* Section = Cast<UMovieScene3DTransformSection>(Transform->CreateNewSection());
		Section->SetRange(TRange<FFrameNumber>::All());
		Transform->AddSection(*Section);
		TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
		if (Channels.Num() < 9) { OutError = TEXT("unexpected transform channels"); return nullptr; }
		// Keys hold the object's transform in its parent: the parent's current world transform places them.
		const FTransform ParentWorld = Document->ParentFrame(O->Id);
		FRotator Previous = FRotator::ZeroRotator;
		for (int32 K = 0; K < Track.Keys.Num(); ++K)
		{
			const FOKSceneKey& Key = Track.Keys[K];
			const FTransform World = Key.Transform * ParentWorld;
			const FVector L = World.GetLocation(), S = World.GetScale3D();
			FRotator R = World.Rotator();
			if (K > 0) { R.Roll = Unwind(R.Roll, Previous.Roll); R.Pitch = Unwind(R.Pitch, Previous.Pitch); R.Yaw = Unwind(R.Yaw, Previous.Yaw); }
			Previous = R;
			const double Values[9] = { L.X, L.Y, L.Z, R.Roll, R.Pitch, R.Yaw, S.X, S.Y, S.Z };
			const FFrameNumber Frame = Tick.AsFrameNumber(Key.Time);
			for (int32 C = 0; C < 9; ++C)
			{
				switch (Key.Ease)
				{
				case EOKSceneEase::Linear: Channels[C]->AddLinearKey(Frame, Values[C]); break;
				case EOKSceneEase::Step: Channels[C]->AddConstantKey(Frame, Values[C]); break;
				default: Channels[C]->AddCubicKey(Frame, Values[C], RCTM_Auto); break;
				}
			}
			++Keys;
		}
		// The key value (light intensity / camera view angle) as a property track of the spawnable's component.
		USceneComponent* Component = nullptr;
		FName Property;
		TFunction<float(float)> ToProperty;
		if (const ACineCameraActor* Camera = Cast<ACineCameraActor>(Template))
		{
			UCineCameraComponent* Cine = Camera->GetCineCameraComponent();
			const float SensorWidth = Cine->Filmback.SensorWidth;
			Component = Cine;
			Property = TEXT("CurrentFocalLength");
			ToProperty = [SensorWidth](float Fov) { return SensorWidth * 0.5f / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Fov, 1.f, 170.f)) * 0.5f); };
		}
		else if (const ALight* Light = Cast<ALight>(Template))
		{
			Component = Light->GetLightComponent();
			Property = TEXT("Intensity");
			ToProperty = [](float V) { return V; };
		}
		if (Component)
		{
			const FGuid ComponentBinding = Scene->AddPossessable(Component->GetName(), Component->GetClass());
			if (FMovieScenePossessable* Possessable = Scene->FindPossessable(ComponentBinding)) Possessable->SetParent(Binding, Scene);
			if (FMovieSceneSpawnable* Spawnable = Scene->FindSpawnable(Binding)) Spawnable->AddChildPossessable(ComponentBinding);
			UMovieSceneFloatTrack* ValueTrack = Scene->AddTrack<UMovieSceneFloatTrack>(ComponentBinding);
			ValueTrack->SetPropertyNameAndPath(Property, Property.ToString());
			UMovieSceneFloatSection* ValueSection = Cast<UMovieSceneFloatSection>(ValueTrack->CreateNewSection());
			ValueSection->SetRange(TRange<FFrameNumber>::All());
			ValueTrack->AddSection(*ValueSection);
			FMovieSceneFloatChannel& Channel = ValueSection->GetChannel();
			for (const FOKSceneKey& Key : Track.Keys)
			{
				const FFrameNumber Frame = Tick.AsFrameNumber(Key.Time);
				const float V = ToProperty(Key.Value);
				switch (Key.Ease)
				{
				case EOKSceneEase::Linear: Channel.AddLinearKey(Frame, V); break;
				case EOKSceneEase::Step: Channel.AddConstantKey(Frame, V); break;
				default: Channel.AddCubicKey(Frame, V, RCTM_Auto); break;
				}
			}
			++PropertyTracks;
		}
	}
	if (CameraCut.IsValid())
	{
		UMovieSceneCameraCutTrack* Cuts = Cast<UMovieSceneCameraCutTrack>(Scene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
		Cuts->AddNewCameraCut(UE::MovieScene::FRelativeObjectBindingID(CameraCut), FFrameNumber(0));
	}
	FAssetRegistryModule::AssetCreated(Sequence);
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(Package, Sequence, *File, SaveArgs)) { OutError = TEXT("cannot save ") + File; return nullptr; }
	OutReport = FString::Printf(TEXT("%s: %d objects, %d keys, %d value tracks, %.2f s%s"), *PackageName, Bindings, Keys, PropertyTracks, Duration, CameraCut.IsValid() ? TEXT(", camera cut on the active camera") : TEXT(""));
	return Sequence;
}
