#include "OKStudioImport.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "KKRustExtract.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OKSceneDocument.h"
#include "OKStudioCatalog.h"
#include "OKSceneActor.h"
#include "OKStudioGame.h"
#include "OKStudioPose.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ok_extract.h"

namespace
{
	FVector Vec3(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FVector& Default = FVector::ZeroVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!O->TryGetArrayField(Key, A) || A->Num() != 3) return Default;
		return FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
	}
	FLinearColor Color(const TSharedPtr<FJsonObject>& O)
	{
		return O ? FLinearColor(O->GetNumberField(TEXT("r")), O->GetNumberField(TEXT("g")), O->GetNumberField(TEXT("b")), O->GetNumberField(TEXT("a"))) : FLinearColor::White;
	}

	struct FImporter
	{
		UOKSceneDocument* Doc = nullptr;
		FString CardDirectory;
		FOKStudioImport::FReport* Report = nullptr;
		TMap<FString, int32> Notes;

		void Note(const FString& What) { ++Notes.FindOrAdd(What); }

		FTransform Transform(const TSharedPtr<FJsonObject>& O)
		{
			const TSharedPtr<FJsonObject>* Change = nullptr;
			if (!O->TryGetObjectField(TEXT("change"), Change)) return FTransform::Identity;
			return FOKStudioImport::UnityToUe(Vec3(*Change, TEXT("pos")), Vec3(*Change, TEXT("rot")), Vec3(*Change, TEXT("scale"), FVector::OneVector));
		}

		/** Accessory point bone the next added object hangs on (characters' slot children). */
		FString PendingAttach;

		int32 Add(EOKSceneObjectKind Kind, const FString& Name, int32 Parent, const TSharedPtr<FJsonObject>& O)
		{
			const int32 Id = Doc->AddObject(Kind, Name, Parent);
			FOKSceneObject* X = Doc->Find(Id);
			X->Transform = Transform(O);
			X->AttachBone = MoveTemp(PendingAttach);
			PendingAttach.Reset();
			bool bVisible = true;
			if (O->TryGetBoolField(TEXT("visible"), bVisible)) X->bVisible = bVisible;
			return Id;
		}

		void Children(const TSharedPtr<FJsonObject>& O, int32 Parent)
		{
			const TArray<TSharedPtr<FJsonValue>>* Kids = nullptr;
			if (O->TryGetArrayField(TEXT("children"), Kids)) for (const TSharedPtr<FJsonValue>& K : *Kids) Object(K->AsObject(), Parent);
		}

		void Object(const TSharedPtr<FJsonObject>& O, int32 Parent)
		{
			if (!O) return;
			const FString Kind = O->GetStringField(TEXT("kind"));
			if (Kind == TEXT("Folder"))
			{
				++Report->Folders;
				const int32 Id = Add(EOKSceneObjectKind::Folder, O->GetStringField(TEXT("name")), Parent, O);
				Children(O, Id);
			}
			else if (Kind == TEXT("Route"))
			{
				// Route paths (objects moving along points) are not in OK Studio yet: the route becomes a folder.
				++Report->Routes;
				Note(TEXT("route paths become folders (no movement)"));
				const int32 Id = Add(EOKSceneObjectKind::Folder, TEXT("[route] ") + O->GetStringField(TEXT("name")), Parent, O);
				Children(O, Id);
			}
			else if (Kind == TEXT("Text"))
			{
				++Report->Texts;
				Note(TEXT("text objects are skipped"));
			}
			else if (Kind == TEXT("Camera"))
			{
				++Report->Cameras;
				const int32 Id = Add(EOKSceneObjectKind::Camera, O->GetStringField(TEXT("name")), Parent, O);
				// Unity's camera looks along its +Z, an Unreal camera along +X.
				FTransform& T = Doc->Find(Id)->Transform;
				T.SetRotation(T.GetRotation() * FQuat(FVector::UpVector, UE_HALF_PI));
				if (O->GetBoolField(TEXT("active"))) Doc->ActiveCamera = Id;
			}
			else if (Kind == TEXT("Light"))
			{
				++Report->Lights;
				const int32 Id = Add(EOKSceneObjectKind::Light, TEXT("Light"), Parent, O);
				FOKSceneLight& L = Doc->Find(Id)->Light;
				const int32 No = int32(O->GetNumberField(TEXT("no")));
				L.Type = No == 0 ? EOKSceneLightType::Directional : No == 2 ? EOKSceneLightType::Spot : EOKSceneLightType::Point;
				const TSharedPtr<FJsonObject>* C = nullptr;
				if (O->TryGetObjectField(TEXT("color"), C)) L.Color = Color(*C);
				// Unity intensity (about 0..2) to UE units; ranges are metres.
				const float Intensity = float(O->GetNumberField(TEXT("intensity")));
				L.Intensity = L.Type == EOKSceneLightType::Directional ? Intensity * 4.f : Intensity * 800.f;
				L.Range = float(O->GetNumberField(TEXT("range"))) * 100.f;
				L.SpotAngle = float(O->GetNumberField(TEXT("spot_angle"))) * 0.5f;
				L.bShadows = O->GetBoolField(TEXT("shadow"));
				if (!O->GetBoolField(TEXT("enable"))) Doc->Find(Id)->bVisible = false;
				Note(TEXT("light intensities are converted approximately"));
			}
			else if (Kind == TEXT("Item"))
			{
				++Report->Items;
				const int32 Group = int32(O->GetNumberField(TEXT("group"))), Category = int32(O->GetNumberField(TEXT("category"))), No = int32(O->GetNumberField(TEXT("no")));
				const FOKStudioCatalogItem* Entry = FOKStudioCatalog::Get().FindItem(Group, Category, No);
				// Particle items become OK Niagara effects (the game's ParticleSystems are not converted).
				if (const FOKStudioEffect* Effect = FOKStudioCatalog::Get().EffectForKks(Group, Category, No))
				{
					const int32 FxId = Add(EOKSceneObjectKind::Effect, Effect->Name, Parent, O);
					Doc->Find(FxId)->Effect.Effect = Effect->Id;
					Note(TEXT("particle items become OK Niagara effects"));
					Children(O, FxId);
					return;
				}
				const int32 Id = Add(Entry ? EOKSceneObjectKind::Item : EOKSceneObjectKind::Folder, Entry ? Entry->Name : FString::Printf(TEXT("[mod item %d/%d/%d]"), Group, Category, No), Parent, O);
				if (Entry)
				{
					++Report->CatalogItems;
					FOKSceneItem& I = Doc->Find(Id)->Item;
					I.Group = Group; I.Category = Category; I.No = No;
                    double ItemTime=0., ItemSpeed=1.; int32 ItemPattern=0;
                    O->TryGetNumberField(TEXT("anime_time"), ItemTime); O->TryGetNumberField(TEXT("anime_speed"), ItemSpeed); O->TryGetNumberField(TEXT("anime_pattern"), ItemPattern);
                    I.AnimeTime=float(ItemTime);
                    I.AnimeSpeed=float(ItemSpeed);
                    I.AnimePattern=ItemPattern;
                    if (Entry->bAnime && (ItemPattern != 0 || !FOKStudioCatalog::Get().ChildAnimations.Contains(FIntVector(Group, Category, No))))
                        Note(TEXT("animated item attachment has no converted track for its selected pattern"));
					// Item FK of an enabled item: its bones' local rotations.
					const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
					if (O->GetBoolField(TEXT("enable_fk")) && O->TryGetArrayField(TEXT("fk"), Bones))
						for (const TSharedPtr<FJsonValue>& B : *Bones)
						{
							const TArray<TSharedPtr<FJsonValue>>& Pair = B->AsArray();
							if (Pair.Num() == 2) I.Fk.Add({ Pair[0]->AsString(), Vec3(Pair[1]->AsObject(), TEXT("rot")) });
						}
					const TArray<TSharedPtr<FJsonValue>>* Colors = nullptr;
					if (O->TryGetArrayField(TEXT("colors"), Colors))
						for (int32 K = 0; K < 3 && K < Colors->Num(); ++K)
						{
							FLinearColor C = Color((*Colors)[K]->AsObject());
							I.Colors.Add(Entry->Colors[K] ? FLinearColor(C.R, C.G, C.B, 1.f) : FLinearColor(1.f, 1.f, 1.f, -1.f));
						}
					// colors[6]: the item's shadow colour (OIItemInfo; the game writes it to every material's _shadowcolor).
					if (Colors && Colors->IsValidIndex(6))
					{
						const FLinearColor C = Color((*Colors)[6]->AsObject());
						I.ShadowColor = FLinearColor(C.R, C.G, C.B, 1.f);
					}
					double Alpha = 1., LineWidth = 1.;
					if (O->TryGetNumberField(TEXT("alpha"), Alpha)) I.Alpha = FMath::Clamp(float(Alpha), 0.f, 1.f);
					if (O->TryGetNumberField(TEXT("line_width"), LineWidth)) I.LineWidth = FMath::Max(0.f, float(LineWidth));
					// Pattern slots the item offers (ItemList 柄 flags): mask, colour (colors[3..5]), UV, rotation, clamp.
					const TArray<TSharedPtr<FJsonValue>>* Patterns = nullptr;
					if (O->TryGetArrayField(TEXT("patterns"), Patterns))
					{
						bool bAny = false;
						TArray<FOKSceneItemPattern> Slots;
						for (int32 K = 0; K < 3 && K < Patterns->Num(); ++K)
						{
							const TSharedPtr<FJsonObject> P = (*Patterns)[K]->AsObject();
							FOKSceneItemPattern S;
							if (P && Entry->Patterns[K])
							{
								S.Pattern = FMath::Max(0, int32(P->GetNumberField(TEXT("key"))));
								S.bClamp = P->GetBoolField(TEXT("clamp"));
								S.Rotation = float(P->GetNumberField(TEXT("rot")));
								const TArray<TSharedPtr<FJsonValue>>* Uv = nullptr;
								if (P->TryGetArrayField(TEXT("uv"), Uv) && Uv->Num() == 4) S.Uv = FVector4((*Uv)[0]->AsNumber(), (*Uv)[1]->AsNumber(), (*Uv)[2]->AsNumber(), (*Uv)[3]->AsNumber());
								if (Colors && Colors->IsValidIndex(3 + K)) S.Color = Color((*Colors)[3 + K]->AsObject());
								FString UserFile;
								if (P->TryGetStringField(TEXT("file"), UserFile) && !UserFile.IsEmpty()) Note(TEXT("item patterns from user pattern files are not carried over"));
							}
							bAny |= S.Pattern > 0;
							Slots.Add(S);
						}
						if (bAny) I.Patterns = Slots;
					}
				}
				else Note(TEXT("mod items not in the base library become empty folders"));
				Children(O, Id);
			}
			else if (Kind == TEXT("Character"))
			{
				++Report->Characters;
				FString KksCard;
				O->TryGetStringField(TEXT("card"), KksCard);
				const int32 Key = int32(O->GetNumberField(TEXT("key")));
				const FString OkCard = CardDirectory / FString::Printf(TEXT("char_%d.png"), Key);
				FString Summary;
				// A Studio scene names a coordinate explicitly. Keep its numbering and record that outfit's resources;
				// the Gallery's automatic display outfit selection would exchange the saved outfit with another one.
				const TSharedPtr<FJsonObject>* SavedStatus = nullptr;
				int32 SavedOutfit = 0;
				if (O->TryGetObjectField(TEXT("status"), SavedStatus)) (*SavedStatus)->TryGetNumberField(TEXT("coordinate"), SavedOutfit);
				SavedOutfit = FMath::Max(0, SavedOutfit);
				const bool bCard = !KksCard.IsEmpty() && FOkSession::MakeOkCardForOutfit(KksCard, OkCard, SavedOutfit, Summary);
				if (bCard) ++Report->CharacterCards; else Report->Notes.Add(FString::Printf(TEXT("character %d: no OK card (%s)"), Key, *Summary));
				IFileManager::Get().Delete(*KksCard);
				const int32 Id = Add(EOKSceneObjectKind::Character, FString::Printf(TEXT("Character %d"), Key), Parent, O);
				FOKSceneCharacter& C = Doc->Find(Id)->Character;
				C.Card = bCard ? OkCard : FString();
				double Mouth = -1.;
				if (O->TryGetNumberField(TEXT("mouth_open"), Mouth)) C.MouthOpen = float(Mouth);
				// Character state: Studio's nipple and fluids, the card's tears and accessory slots (blush and gloss the card
				// already carries).
				double Nipple = -1.;
				if (O->TryGetNumberField(TEXT("nipple"), Nipple)) C.NipStand = FMath::Clamp(float(Nipple), 0.f, 1.f);
				const TArray<TSharedPtr<FJsonValue>>* Siru = nullptr;
				if (O->TryGetArrayField(TEXT("siru"), Siru) && Siru->Num() == 5)
				{
					TArray<uint8> Fluids;
					for (const TSharedPtr<FJsonValue>& V : *Siru) Fluids.Add(uint8(FMath::Clamp(int32(V->AsNumber()), 0, 2)));
					if (Fluids.ContainsByPredicate([](uint8 L) { return L != 0; })) C.Fluids = Fluids;
				}
				const TSharedPtr<FJsonObject>* Status = nullptr;
				if (O->TryGetObjectField(TEXT("status"), Status))
				{
					int32 Tears = 0;
					if ((*Status)->TryGetNumberField(TEXT("tears_lv"), Tears) && Tears > 0) C.TearsLevel = FMath::Min(Tears, 3);
					// The outfit and clothes state the scene was saved with (ChaFileStatus.coordinateType / clothesState).
					int32 Coordinate = -1;
					if ((*Status)->TryGetNumberField(TEXT("coordinate"), Coordinate) && Coordinate >= 0) C.Outfit = Coordinate;
					const TArray<TSharedPtr<FJsonValue>>* Clothes = nullptr;
					if ((*Status)->TryGetArrayField(TEXT("clothes_state"), Clothes) && Clothes->Num() > 0)
						for (int32 Part = 0; Part < FMath::Min(Clothes->Num(), C.ClothesStates.Num()); ++Part) C.ClothesStates[Part] = uint8(FMath::Clamp(int32((*Clothes)[Part]->AsNumber()), 0, 3));
					// The face the scene was saved with (ChaFileStatus eyebrowPtn / eyesPtn / mouthPtn), and where the eyes and neck look:
					// eyes 1 at the camera (0 straight ahead), neck 0 at the camera (3, the default, as the animation).
					int32 Face = -1;
					if ((*Status)->TryGetNumberField(TEXT("eyebrow_ptn"), Face) && Face >= 0) C.EyebrowPattern = Face;
					if ((*Status)->TryGetNumberField(TEXT("eyes_ptn"), Face) && Face >= 0) C.EyesPattern = Face;
					if ((*Status)->TryGetNumberField(TEXT("mouth_ptn"), Face) && Face >= 0) C.MouthPattern = Face;
					if ((*Status)->TryGetNumberField(TEXT("eyes_look_ptn"), Face) && Face >= 0) C.EyesLook = Face == 1 ? EOKSceneLook::Camera : EOKSceneLook::Animation;
					if ((*Status)->TryGetNumberField(TEXT("neck_look_ptn"), Face) && Face >= 0) C.NeckLook = Face == 0 ? EOKSceneLook::Camera : EOKSceneLook::Animation;
					const TArray<TSharedPtr<FJsonValue>>* Shown = nullptr;
					if ((*Status)->TryGetArrayField(TEXT("show_accessory"), Shown) && Shown->ContainsByPredicate([](const TSharedPtr<FJsonValue>& V) { return !V->AsBool(); }))
					{
						C.AccessoriesShown.Init(1, 21);
						for (int32 Slot = 0; Slot < FMath::Min(Shown->Num(), 20); ++Slot) C.AccessoriesShown[Slot] = (*Shown)[Slot]->AsBool() ? 1 : 0;
					}
				}
                const TSharedPtr<FJsonObject>* Neck = nullptr;
                int32 SavedNeckPattern = -1;
                if (Status) (*Status)->TryGetNumberField(TEXT("neck_look_ptn"), SavedNeckPattern);
                if (SavedNeckPattern == 4 && O->TryGetObjectField(TEXT("neck"), Neck) && (*Neck)->GetIntegerField(TEXT("pattern")) == 4)
                {
                    const TArray<TSharedPtr<FJsonValue>>* Rotations = nullptr;
                    if ((*Neck)->TryGetArrayField(TEXT("rotations"), Rotations) && Rotations->Num() == 2)
                        for (const auto& Value : *Rotations)
                        {
                            const auto& Q = Value->AsArray();
                            if (Q.Num() == 4) C.FixedNeck.Add(FQuat(Q[0]->AsNumber(), Q[1]->AsNumber(), Q[2]->AsNumber(), Q[3]->AsNumber()).GetNormalized());
                        }
                    if (C.FixedNeck.Num() == 2) C.NeckLook = EOKSceneLook::Fixed;
                    else Note(TEXT("saved FIX neck requires the native neck/head bone pair"));
                }
				C.EyesLook = EOKSceneLook::Animation;
				// FK of the enabled parts.
				const TArray<TSharedPtr<FJsonValue>>* Active = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
				if (O->GetBoolField(TEXT("enable_fk")) && O->TryGetArrayField(TEXT("active_fk"), Active) && O->TryGetArrayField(TEXT("fk"), Bones))
				{
					for (const TSharedPtr<FJsonValue>& B : *Bones)
					{
						const TArray<TSharedPtr<FJsonValue>>& Pair = B->AsArray();
						if (Pair.Num() != 2) continue;
						const int32 BoneNo = int32(Pair[0]->AsNumber());
						const FOKStudioFkBone* Info = FOKStudioCatalog::Get().FkBones.FindByPredicate([BoneNo](const FOKStudioFkBone& F) { return F.No == BoneNo; });
						if (!Info) continue;
						const int32 Part = FOKStudioPose::FkPart(Info->Group);
						if (!Active->IsValidIndex(Part) || !(*Active)[Part]->AsBool()) continue;
						C.Fk.Add({ Info->Bone, Vec3(Pair[1]->AsObject(), TEXT("rot")) });
					}
				}
				// IK targets of the enabled parts: the scene keeps their positions in the character's space. KK numbers them
				// 0 hips, 1-3 left arm (shoulder, elbow, hand), 4-6 right arm, 7-9 left leg (thigh, knee, foot), 10-12 right leg;
				// the parts switch as body, right leg, left leg, right arm, left arm. Elbows and knees are the limbs' bend goals (9..12).
				const TArray<TSharedPtr<FJsonValue>>* ActiveIk = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
				if (O->GetBoolField(TEXT("enable_ik")) && O->TryGetArrayField(TEXT("active_ik"), ActiveIk) && O->TryGetArrayField(TEXT("ik"), Targets))
				{
					struct FMap { int32 Effector; int32 Part; };
					static const FMap Map[13] = { {0, 0}, {1, 4}, {9, 4}, {5, 4}, {2, 3}, {10, 3}, {6, 3}, {3, 2}, {11, 2}, {7, 2}, {4, 1}, {12, 1}, {8, 1} };
					for (const TSharedPtr<FJsonValue>& T : *Targets)
					{
						const TArray<TSharedPtr<FJsonValue>>& Pair = T->AsArray();
						if (Pair.Num() != 2) continue;
						const int32 No = int32(Pair[0]->AsNumber());
						if (No < 0 || No >= 13 || !ActiveIk->IsValidIndex(Map[No].Part) || !(*ActiveIk)[Map[No].Part]->AsBool()) continue;
						const FVector P = Vec3(Pair[1]->AsObject(), TEXT("pos"));
						FOKSceneIkTarget Target;
                        Target.Effector = Map[No].Effector;
                        Target.Position = FVector(-P.X, P.Z, P.Y) * 100.0;
                        Target.bUseRotation = No == 3 || No == 6 || No == 9 || No == 12;
                        Target.Euler = Vec3(Pair[1]->AsObject(), TEXT("rot"));
                        C.Ik.Add(Target);
					}
				}
				// Hand shapes: 0 follows the animation, n is the game's shape n (OCIChar.ChangeHandAnime: SetShapeHandValue(side, n, 0, 0)).
				const TArray<TSharedPtr<FJsonValue>>* Hands = nullptr;
				if (O->TryGetArrayField(TEXT("hand"), Hands) && Hands->Num() == 2)
				{
					auto Shape = [](double V) { const int32 N = int32(V); return N <= 0 ? -1 : FMath::Min(N, 23); };
					C.HandLeft = Shape((*Hands)[0]->AsNumber());
					C.HandRight = Shape((*Hands)[1]->AsNumber());
				}
				const TArray<TSharedPtr<FJsonValue>>* Anime = nullptr;
				if (O->TryGetArrayField(TEXT("anime"), Anime) && Anime->Num() == 3 && (*Anime)[0]->AsNumber() >= 0)
				{
					const int32 G = int32((*Anime)[0]->AsNumber()), Cat = int32((*Anime)[1]->AsNumber()), N = int32((*Anime)[2]->AsNumber());
					if (const FOKStudioAnim* A = FOKStudioCatalog::Get().FindAnimAnyKind(G, Cat, N); A && !A->File.IsEmpty())
					{
						C.StudioAnimeKind = A->Kind; C.StudioAnimeGroup = G; C.StudioAnimeCategory = Cat; C.StudioAnimeNo = N;
						double Speed = 1.;
						if (O->TryGetNumberField(TEXT("anime_speed"), Speed)) C.Speed = float(Speed);
						double Time = 0.;
						if (O->TryGetNumberField(TEXT("anime_time"), Time)) C.Time = float(Time) * FMath::Max(A->Duration, 0.01f);
					}
					else Note(FString::Printf(TEXT("Studio animation %d/%d/%d is not in the OK animation library (a mod motion this installation lacks; anim-convert adds it once the mod is installed)"), G, Cat, N));
				}
				// Children on accessory slots: kept under the character.
				const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
				if (O->TryGetArrayField(TEXT("children"), Slots))
					for (const TSharedPtr<FJsonValue>& S : *Slots)
					{
						const TArray<TSharedPtr<FJsonValue>>& Pair = S->AsArray();
						if (Pair.Num() != 2) continue;
						const FOKStudioAccessoryPoint* Point = FOKStudioCatalog::Get().FindAccessoryPoint(int32(Pair[0]->AsNumber()));
						if (!Point) Note(TEXT("objects on accessory slots the base library does not list sit at the character root (re-run the conversion for the accessory points)"));
						for (const TSharedPtr<FJsonValue>& K : Pair[1]->AsArray()) { PendingAttach = Point ? Point->Bone : FString(); Object(K->AsObject(), Id); PendingAttach.Reset(); }
					}
			}
		}
	};
}

FTransform FOKStudioImport::UnityToUe(const FVector& Position, const FVector& EulerDegrees, const FVector& Scale)
{
	// Unity rotation order Z, X, Y (q = qy qx qz); the converter's basis change maps a quaternion (x, y, z, w) to (-x, z, y, w).
	const FQuat Qx(FVector(1, 0, 0), FMath::DegreesToRadians(EulerDegrees.X));
	const FQuat Qy(FVector(0, 1, 0), FMath::DegreesToRadians(EulerDegrees.Y));
	const FQuat Qz(FVector(0, 0, 1), FMath::DegreesToRadians(EulerDegrees.Z));
	const FQuat U = Qy * Qx * Qz;
	return FTransform(FQuat(-U.X, U.Z, U.Y, U.W).GetNormalized(), FVector(-Position.X, Position.Z, Position.Y) * 100.0, FVector(Scale.X, Scale.Z, Scale.Y));
}

FString FOKStudioImport::FReport::ToJson() const
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("source"), Source);
	J->SetStringField(TEXT("version"), Version);
	J->SetNumberField(TEXT("characters"), Characters);
	J->SetNumberField(TEXT("character_cards"), CharacterCards);
	J->SetNumberField(TEXT("items"), Items);
	J->SetNumberField(TEXT("catalog_items"), CatalogItems);
	J->SetNumberField(TEXT("lights"), Lights);
	J->SetNumberField(TEXT("cameras"), Cameras);
	J->SetNumberField(TEXT("folders"), Folders);
	J->SetNumberField(TEXT("routes"), Routes);
	J->SetNumberField(TEXT("texts"), Texts);
	J->SetNumberField(TEXT("map"), Map);
	TArray<TSharedPtr<FJsonValue>> N;
	for (const FString& S : Notes) N.Add(MakeShared<FJsonValueString>(S));
	J->SetArrayField(TEXT("notes"), N);
	FString Out;
	FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Out));
	return Out;
}

bool FOKStudioImport::Import(const FString& ScenePng, UOKSceneDocument* Document, const FString& CardDirectory, FReport& Report, FString& OutError)
{
	if (!Document) { OutError = TEXT("no document"); return false; }
	IFileManager::Get().MakeDirectory(*CardDirectory, true);
	TArray<ANSICHAR> Buffer;
	Buffer.SetNumZeroed(64 << 20);
	if (ok_studio_scene_export(TCHAR_TO_UTF8(*ScenePng), TCHAR_TO_UTF8(*CardDirectory), Buffer.GetData(), Buffer.Num()) != 0) { OutError = UTF8_TO_TCHAR(Buffer.GetData()); return false; }
	TSharedPtr<FJsonObject> Scene;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UTF8_TO_TCHAR(Buffer.GetData())), Scene) || !Scene) { OutError = TEXT("bad scene JSON"); return false; }
	Report = FReport();
	Report.Source = ScenePng;
	Report.Version = Scene->GetStringField(TEXT("version"));
	Report.Map = int32(Scene->GetNumberField(TEXT("map")));
	Document->Objects.Reset();
    // Import replaces the scene. Optional/neutral settings must not inherit
    // the previous scene (e.g. aperture 0 left the previous DOF enabled).
    Document->Post = FOKScenePost();
    Document->MapTransform = FTransform::Identity;
    Document->Tracks.Reset();
    Document->Duration = 10.f;
    Document->ItemAnimationSeconds=0.f;
	Document->NextId = 1;
	Document->ActiveCamera = -1;
	Document->Map = FOKStudioCatalog::Get().FindMap(Report.Map) ? Report.Map : -1;
	if (Report.Map >= 0 && Document->Map < 0) Report.Notes.Add(FString::Printf(TEXT("map %d is not in the base library"), Report.Map));
	FImporter Importer;
	Importer.Doc = Document;
	Importer.CardDirectory = CardDirectory;
	Importer.Report = &Report;
	const TArray<TSharedPtr<FJsonValue>>* Objects = nullptr;
	if (Scene->TryGetArrayField(TEXT("objects"), Objects)) for (const TSharedPtr<FJsonValue>& O : *Objects) Importer.Object(O->AsObject(), -1);
	const TArray<TSharedPtr<FJsonValue>>* CardErrors = nullptr;
	if (Scene->TryGetArrayField(TEXT("card_errors"), CardErrors)) for (const TSharedPtr<FJsonValue>& E : *CardErrors) Report.Notes.Add(E->AsString());
	// Screen settings (System menu): the Amplify Color filter (the picture is lerp(LUT, original, aceBlend)), bloom and
	// vignette.
	const TSharedPtr<FJsonObject>* Effects = nullptr;
	if (Scene->TryGetObjectField(TEXT("effects"), Effects))
	{
		const FJsonObject& E = **Effects;
		int32 Ace = -1;
		double Blend = 0.;
		if (E.TryGetNumberField(TEXT("ace_no"), Ace) && FOKStudioCatalog::Get().FindFilter(Ace))
		{
			E.TryGetNumberField(TEXT("ace_blend"), Blend);
			Document->Post.ColorFilter = Ace;
			Document->Post.ColorFilterIntensity = float(FMath::Clamp(1. - Blend, 0., 1.));
		}
		else if (Ace >= 0) Report.Notes.Add(FString::Printf(TEXT("screen filter %d is not in the base library's Studio filter list"), Ace));
		bool bOn = false;
		double Value = 0.;
		if (E.TryGetBoolField(TEXT("bloom"), bOn))
		{
			Document->Post.bBloom = bOn;
			if (E.TryGetNumberField(TEXT("bloom_intensity"), Value)) Document->Post.BloomIntensity = float(Value);
			if (E.TryGetNumberField(TEXT("bloom_threshold"), Value)) Document->Post.BloomThreshold = float(Value);
			if (E.TryGetNumberField(TEXT("bloom_blur"), Value)) Document->Post.BloomSize = float(FMath::Clamp(Value / 4., 0.1, 8.));
		}
		if (E.TryGetBoolField(TEXT("vignette"), bOn)) Document->Post.bVignette = bOn;
		// The shading globals (ambient shadow colour and strength, outline darkness and width): Studio writes them to
		// _ambientshadowG, _LineColorG, _linewidthG for everything it draws.
		const TSharedPtr<FJsonObject>* Ambient = nullptr;
		if (E.TryGetObjectField(TEXT("ambient_shadow"), Ambient)) Document->Post.AmbientShadow = Color(*Ambient);
		if (E.TryGetNumberField(TEXT("ambient_shadow_g"), Value)) Document->Post.AmbientShadowG = float(Value);
		if (E.TryGetNumberField(TEXT("line_color_g"), Value)) Document->Post.LineColorG = float(Value);
		if (E.TryGetNumberField(TEXT("line_width_g"), Value)) Document->Post.LineWidthG = float(Value);
		// SceneInfo.rampG: the mt_ramp the scene's characters and items shade with (SystemButtonCtrl sets _RampG).
		if (E.TryGetNumberField(TEXT("ramp_g"), Value)) Document->Post.RampG = int32(Value);
		// Studio restores its saved shader mode after loading characters; system.xml is only the card default.
		if (E.TryGetNumberField(TEXT("shader_type"), Value)) Document->Post.ShaderType = int32(Value);
		// SceneInfo.sunLightType: the map's time of day (its own sky, when it has one, follows it).
		if (E.TryGetNumberField(TEXT("sun_light_type"), Value)) Document->Post.SunType = int32(Value);
		// SceneInfo.caMap: Studio's map position / rotation (MapCtrl.Reflect sets the map root's local transform).
		Document->MapTransform = UnityToUe(Vec3(*Effects, TEXT("map_pos")), Vec3(*Effects, TEXT("map_rot")), FVector::OneVector);
		// Fog, ambient occlusion and sun shafts (SystemButtonCtrl FogInfo, AmplifyOcculusionEffectInfo, SunShaftsInfo).
		const TSharedPtr<FJsonObject>* Field = nullptr;
		if (E.TryGetBoolField(TEXT("fog"), bOn)) Document->Post.bFog = bOn;
		if (E.TryGetObjectField(TEXT("fog_color"), Field)) Document->Post.FogColor = Color(*Field);
		if (E.TryGetBoolField(TEXT("aoe"), bOn)) Document->Post.bAmbientOcclusion = bOn;
		if (E.TryGetObjectField(TEXT("aoe_color"), Field)) Document->Post.AmbientOcclusionTint = Color(*Field);
		if (E.TryGetNumberField(TEXT("aoe_radius"), Value)) Document->Post.AmbientOcclusionRadius = float(Value);
		if (E.TryGetBoolField(TEXT("sun_shafts"), bOn)) Document->Post.bSunShafts = bOn;
		if (E.TryGetObjectField(TEXT("sun_color"), Field)) Document->Post.SunShaftColor = Color(*Field);
		if (E.TryGetObjectField(TEXT("sun_threshold_color"), Field)) Document->Post.SunShaftThreshold = Color(*Field);
		// Depth of field (Unity Standard Assets DepthOfField, focused on the Studio camera's target; the focal distance is
		// set with the camera below). Its blur grows with 1 / (1 - aperture) - 1 (aperture 0: no blur at all) and shrinks
		// with the in-focus width focalSize; as an f-stop: 3.7 focalSize / strength, calibrated on official scene 1901
		// (focalSize 0.89, aperture 0.62: the stage behind the singers blurred, the singers sharp).
		double FocalSize = 0.95, Aperture = 0.6;
		E.TryGetNumberField(TEXT("depth_focal_size"), FocalSize);
		E.TryGetNumberField(TEXT("depth_aperture"), Aperture);
		const double Strength = 1. / (1. - FMath::Clamp(Aperture, 0., 0.95)) - 1.;
		if (E.TryGetBoolField(TEXT("depth"), bOn) && bOn && Strength > 0.01)
		{
			Document->Post.bDepthOfField = true;
			Document->Post.Aperture = float(FMath::Clamp(3.7 * FMath::Max(FocalSize, 0.05) / Strength, 0.7, 22.));
		}
		// Background and frame pictures (the scene names a file of UserData/bg, UserData/frame): the game's own ones are in
		// the OK base library (studio/bg, studio/frame, kept as library-relative paths); a user's own picture is copied
		// beside the cards from the scene's game folder or from beside the scene card.
		auto Picture = [&](const TCHAR* Field, const TCHAR* Kind) -> FString
		{
			FString Name;
			if (!E.TryGetStringField(Field, Name) || Name.IsEmpty()) return FString();
			const FString File = FPaths::GetCleanFilename(Name);
			const FString Library = FString::Printf(TEXT("studio/%s/%s"), Kind, *File);
			if (FPaths::FileExists(AOKSceneActor::LibraryDirectory() / Library)) return Library;
			const FString Normal = FPaths::ConvertRelativePathToFull(ScenePng).Replace(TEXT("\\"), TEXT("/"));
			const int32 UserData = Normal.Find(TEXT("/UserData/"), ESearchCase::IgnoreCase);
			TArray<FString> Candidates;
			if (UserData >= 0) Candidates.Add(Normal.Left(UserData) / TEXT("UserData") / Kind / File);
			Candidates.Add(FPaths::GetPath(Normal) / File);
			const FString* Source = Candidates.FindByPredicate([](const FString& C) { return FPaths::FileExists(C); });
			const FString Copy = CardDirectory / File;
			if (Source && (IFileManager::Get().Copy(*Copy, **Source) == COPY_OK || FPaths::FileExists(Copy))) return Copy;
			Report.Notes.Add(FString::Printf(TEXT("%s picture %s is neither in the base library nor beside the scene"), Kind, *Name));
			return FString();
		};
		Document->Post.Background = Picture(TEXT("background"), TEXT("bg"));
		Document->Post.Frame = Picture(TEXT("frame"), TEXT("frame"));
		// Without a map, background picture or sky Studio's camera clears to its colour (Map.ReflectMapInfo: clearFlags
		// Color). Measured from scene cards: sRGB (139, 180, 222) before the default filter.
		bool bSky = false;
		E.TryGetBoolField(TEXT("sky"), bSky);
		// SceneInfo.skyInfo: Studio's sky dome and its StudioSky pattern.
		Document->Post.bSky = bSky;
		Document->Post.SkyPattern = int32(E.GetNumberField(TEXT("sky_pattern")));
		if (int32(Scene->GetNumberField(TEXT("map"))) < 0 && Document->Post.Background.IsEmpty() && !bSky)
			Document->Post.BackgroundColor = FLinearColor(139.f / 255.f, 180.f / 255.f, 222.f / 255.f, 1.f);
	}
	// The Studio view the scene was saved with: a camera object, the active view unless a camera object is.
	const TSharedPtr<FJsonObject>* Camera = nullptr;
	if (Scene->TryGetObjectField(TEXT("camera"), Camera))
	{
		const int32 Id = Document->AddObject(EOKSceneObjectKind::Camera, TEXT("Studio camera"), -1);
		FOKSceneObject* Cam = Document->Find(Id);
		const FTransform Base = UnityToUe(Vec3(*Camera, TEXT("pos")), Vec3(*Camera, TEXT("rotate")), FVector::OneVector);
		const FVector Distance = Vec3(*Camera, TEXT("distance"));
		// Camera at rotation * distance + pos; Unity's camera looks along +Z, an Unreal camera along +X.
		Cam->Transform = FTransform(Base.GetRotation() * FQuat(FVector::UpVector, UE_HALF_PI), Base.GetLocation() + Base.GetRotation().RotateVector(FVector(-Distance.X, Distance.Z, Distance.Y) * 100.0));
		// Studio's depth of field focuses on the camera target.
		if (Document->Post.bDepthOfField) Document->Post.FocalDistance = float(FMath::Max(Distance.Size() * 100.0, 10.0));
		// Unity's field of view is vertical; Studio pictures are 16:9.
		const double VerticalFov = FMath::DegreesToRadians(FMath::Clamp((*Camera)->GetNumberField(TEXT("fov")), 1.0, 170.0));
		Cam->Camera.Fov = float(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(VerticalFov * 0.5) * 16.0 / 9.0)));
		if (Document->ActiveCamera < 0) Document->ActiveCamera = Id;
		++Report.Cameras;
		// CameraLightCtrl.LightCalc rotates the scene-root "Light Chara", not
		// the camera. Its Directional Chara child retains its authored (40,180,0)
		// rotation (CharaStudio_Data/level2). The scene file also keeps mapLight, but Studio only saves and loads
		// it: CameraLightCtrl has no map light, nothing in the game lights the map with it (the map shows its baked
		// lightmaps). Made a light, it washed lightmapped maps out (the 1F toilet's partitions drew white).
		const TArray<TSharedPtr<FJsonValue>>* StudioLights = nullptr;
		if ((*Camera)->TryGetArrayField(TEXT("lights"), StudioLights))
			for (const TSharedPtr<FJsonValue>& V : *StudioLights)
			{
				const TSharedPtr<FJsonObject> L = V->AsObject();
				if (!L || !L->GetBoolField(TEXT("camera"))) continue;
				const TArray<TSharedPtr<FJsonValue>>* Rot = nullptr;
				FVector Euler(0.);
				if (L->TryGetArrayField(TEXT("rot"), Rot) && Rot->Num() == 2) Euler = FVector((*Rot)[0]->AsNumber(), (*Rot)[1]->AsNumber(), 0.);
				// Unity's light shines along +Z, an Unreal light along +X.
				const FQuat Turn(FVector::UpVector, UE_HALF_PI);
				const FQuat Child = UnityToUe(FVector::ZeroVector, FVector(40.f, 180.f, 0.f), FVector::OneVector).GetRotation();
				const FQuat Shine = UnityToUe(FVector::ZeroVector, Euler, FVector::OneVector).GetRotation() * Child * Turn;
				const int32 LightId = Document->AddObject(EOKSceneObjectKind::Light, TEXT("Studio character light"), -1);
				FOKSceneObject* Obj = Document->Find(LightId);
				Obj->Transform = FTransform(Shine);
				FOKSceneLight& Light = Obj->Light;
				Light.Type = int32(L->GetNumberField(TEXT("kind"))) == 1 ? EOKSceneLightType::Directional : EOKSceneLightType::Point;
				const TSharedPtr<FJsonObject>* C = nullptr;
				if (L->TryGetObjectField(TEXT("color"), C)) Light.Color = Color(*C);
				Light.Intensity = float(L->GetNumberField(TEXT("intensity"))) * (Light.Type == EOKSceneLightType::Directional ? 4.f : 800.f);
				Light.bShadows = L->GetBoolField(TEXT("shadow"));
				// Studio's character light lights the characters and items, not the map.
				Light.bCharactersOnly = true;
				++Report.Lights;
			}
	}
	for (const TPair<FString, int32>& N : Importer.Notes) Report.Notes.Add(FString::Printf(TEXT("%s (%d)"), *N.Key, N.Value));
	Document->NotifyChanged();
	return true;
}

bool FOKStudioImport::ImportToFile(const FString& ScenePng, FString& OutScene, FReport& Report, FString& OutError)
{
	const FString Name = FPaths::GetBaseFilename(ScenePng);
	const FString Dir = UOKStudioGame::SceneDirectory();
	UOKSceneDocument* Document = NewObject<UOKSceneDocument>();
	if (!Import(ScenePng, Document, Dir / (Name + TEXT("_cards")), Report, OutError)) return false;
	OutScene = Dir / (Name + TEXT(".okscene"));
	if (!Document->SaveToFile(OutScene, OutError)) return false;
	FFileHelper::SaveStringToFile(Report.ToJson(), *(Dir / (Name + TEXT(".import.json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	return true;
}
