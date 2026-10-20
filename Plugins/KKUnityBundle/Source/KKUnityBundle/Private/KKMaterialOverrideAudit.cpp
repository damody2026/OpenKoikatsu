// ok.AuditMaterialOverrides: AKKPreviewActor::SetMaterialOverrides on a Studio item with a pattern slot (the sphere,
// Shader Forge/main_item_studio). Sets a pattern before the load, checks the overrides survive the progressive
// stand-in -> final swap, changes a colour live, clears the list, and screenshots each step.
// Env: OK_MATOVR_OUTPUT (result .json; screenshots next to it), OK_MATOVR_MODEL / OK_MATOVR_PREFAB (default: sphere).
#include "KKPreviewActor.h"
#include "KKRustExtract.h"
#include "KKMaterialExposure.h"
#include "ok_extract.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/MeshComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_EDITOR
namespace OKMaterialOverrideAudit
{
struct FRun
{
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AKKPreviewActor> Actor;
	TWeakObjectPtr<ACameraActor> Camera;
	TStrongObjectPtr<UTexture2D> Stripes, Second;
	FString Output;
	int32 Stage = 0, Frames = 0, InitialPending = 0, Patterned = 0, Target = INDEX_NONE;
	bool bPatternMode = true;
	double Start = FPlatformTime::Seconds();
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	bool Stop(const FString& Error)
	{
		Result->SetBoolField(TEXT("valid"), Error.IsEmpty());
		Result->SetStringField(TEXT("error"), Error);
		FString Json; FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
		FFileHelper::SaveStringToFile(Json, *Output);
		UE_LOG(LogTemp, Display, TEXT("OK material override audit valid=%d error=%s"), Error.IsEmpty(), *Error);
		if (Actor.IsValid()) Actor->Destroy();
		FPlatformMisc::RequestExitWithStatus(false, Error.IsEmpty() ? 0 : 1);
		return false;
	}
	static UTexture2D* MakeStripes(FColor A, FColor B)
	{
		// Pattern masks are linear (native _PatternMask is RGB24 Linear): red 1 shows _Color1, 0 shows _Color1_2.
		const int32 Size = 64;
		UTexture2D* T = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
		FColor* Pixels = static_cast<FColor*>(T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Y = 0; Y < Size; ++Y) for (int32 X = 0; X < Size; ++X) Pixels[Y * Size + X] = ((X / 8 + Y / 8) % 2) ? A : B;
		T->GetPlatformData()->Mips[0].BulkData.Unlock();
		T->SRGB = false; T->Filter = TF_Nearest; T->AddressX = T->AddressY = TA_Wrap; T->UpdateResource();
		return T;
	}
	static FOKMaterialOverride Make(const TCHAR* Name, EOKMaterialOverrideKind Kind, const FLinearColor& Value = FLinearColor::White, UTexture* Texture = nullptr)
	{
		FOKMaterialOverride O; O.Parameter = Name; O.Kind = Kind; O.Vector = Value; O.Scalar = Value.R; O.Texture = Texture; return O;
	}
	// How many part material slots read Name with this value now.
	int32 Count(const TCHAR* Name, const FLinearColor* Vector, UTexture* Texture)
	{
		int32 N = 0;
		TInlineComponentArray<UMeshComponent*> Components; Actor->GetComponents(Components);
		for (auto* C : Components) for (int32 S = 0; S < C->GetNumMaterials(); ++S)
		{
			UMaterialInterface* M = C->GetMaterial(S); if (!M) continue;
			if (Texture) { UTexture* T = nullptr; if (M->GetTextureParameterValue(FMaterialParameterInfo(Name), T) && T == Texture) ++N; }
			else { FLinearColor V; if (M->GetVectorParameterValue(FMaterialParameterInfo(Name), V) && V.Equals(*Vector, 1e-4f)) ++N; }
		}
		return N;
	}
	void Shot(const TCHAR* Suffix)
	{
		const FString Path = FPaths::GetPath(Output) / (FPaths::GetBaseFilename(Output) + Suffix + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		Result->SetStringField(FString(TEXT("shot")) + Suffix, Path);
	}
	void Frame()
	{
		FBox Box(ForceInit);
		TInlineComponentArray<UMeshComponent*> Components; Actor->GetComponents(Components);
		const FName Tag(*FString::Printf(TEXT("KKSessionPart_%d"), Target));
		for (auto* C : Components)
		{
			if (!C->IsVisible()) continue;
			bool bTarget = Target == INDEX_NONE || C->ComponentHasTag(Tag);
			for (const auto& Part : Actor->Parts) bTarget |= Part.SessionIndex == Target && Part.Comp == C;
			if (bTarget) Box += C->Bounds.GetBox();
		}
		if (!Box.IsValid) return;
		const FVector Center = Box.GetCenter(); const float Radius = FMath::Max(Box.GetExtent().Size(), 5.f);
		// The preview faces +Y (its studio lens sits at +Y looking back).
		const FVector Eye = Center + FVector(0.25f, 1.f, 0.2f).GetSafeNormal() * FMath::Max(Radius * 3.2f, 60.f);
		ACameraActor* Cam = World->SpawnActor<ACameraActor>(Eye, (Center - Eye).Rotation());
		Cam->GetCameraComponent()->SetFieldOfView(40.f); Camera = Cam;
		if (APlayerController* PC = World->GetFirstPlayerController()) { PC->bAutoManageActiveCameraTarget = false; PC->SetViewTarget(Cam); }
	}
	bool Tick(float)
	{
		if (!World.IsValid() || FPlatformTime::Seconds() - Start > 900) return Stop(TEXT("Audit timed out"));
		if (Stage == 0)
		{
			FString Model = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MATOVR_MODEL")), Prefab = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MATOVR_PREFAB")), Error;
			FString Library = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_BASE_LIBRARY"));
			if (Library.IsEmpty()) Library = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("IndependentResources/base_v1"));
			FOkSession Session;
			auto HasPatternPart = [](const FOkSession& S) { for (int32 P = 0; P < S.PartCount(); ++P) { OkMeshView View{}; if (S.PartAt(P, View) && View.shader_kind == 4 && View.item_draw) return true; } return false; };
			const FString Card = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MATOVR_CARD"));
			if (!Card.IsEmpty())
			{
				// A character card: the override goes to one part by Slot — the pattern on a main_item_studio part, or
				// else _Color on the first Forge item / MainItemPlus part.
				if (!Session.Open(FString(), Card, 0, Error)) return Stop(TEXT("open card: ") + Error);
				for (int32 P = 0; P < Session.PartCount() && Target == INDEX_NONE; ++P) { OkMeshView View{}; if (Session.PartAt(P, View) && View.render_visible && View.shader_kind == 4 && View.item_draw) Target = P; }
				// The largest coloured Forge item / MainItemPlus part, so the colour shows in the shots.
				int32 Most = 0;
				if (Target == INDEX_NONE) for (int32 P = 0; P < Session.PartCount(); ++P) { OkMeshView View{}; if (Session.PartAt(P, View) && View.render_visible && (View.shader_kind == 8 || View.shader_kind == 9) && !(View.role && FCStringAnsi::Stricmp(View.role, "head") == 0) && View.n_verts > Most) { Most = View.n_verts; Target = P; bPatternMode = false; } }
				if (Target != INDEX_NONE) { OkMeshView View{}; Session.PartAt(Target, View); Result->SetStringField(TEXT("target_name"), View.name ? UTF8_TO_TCHAR(View.name) : TEXT("")); Result->SetStringField(TEXT("target_role"), View.role ? UTF8_TO_TCHAR(View.role) : TEXT("")); }
				if (Target == INDEX_NONE) return Stop(TEXT("card has no item part"));
				Result->SetStringField(TEXT("card"), Card); Result->SetNumberField(TEXT("target_part"), Target); Result->SetBoolField(TEXT("pattern_mode"), bPatternMode);
			}
			else if (!Model.IsEmpty()) { if (!Session.OpenOkModelFk(Library, Model, Prefab, FString(), Error)) return Stop(TEXT("open item: ") + Error); }
			else
			{
				// The first catalogue item with a first pattern slot whose model is Shader Forge/main_item_studio.
				FString Json; TSharedPtr<FJsonObject> Catalog; const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
				if (!FFileHelper::LoadFileToString(Json, *(Library / TEXT("studio_items.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Catalog) || !Catalog || !Catalog->TryGetArrayField(TEXT("items"), Items)) return Stop(TEXT("no studio_items.json"));
				int32 Tried = 0; TArray<TSharedPtr<FJsonValue>> Scanned;
				for (const auto& Value : *Items)
				{
					const TSharedPtr<FJsonObject> Item = Value->AsObject(); const TArray<TSharedPtr<FJsonValue>>* Patterns = nullptr;
					if (!Item || !Item->TryGetArrayField(TEXT("patterns"), Patterns) || Patterns->IsEmpty() || !(*Patterns)[0]->AsBool()) continue;
					Model = Item->GetStringField(TEXT("model")); Prefab = Item->GetStringField(TEXT("prefab"));
					if (Model.IsEmpty() || ++Tried > 80) { if (Tried > 80) break; continue; }
					FOkSession Candidate; FString CandidateError;
					const bool bOpen = Candidate.OpenOkModelFk(Library, Model, Prefab, FString(), CandidateError);
					if (Tried <= 25)
					{
						FString Row = Item->GetStringField(TEXT("name")) + TEXT(" ") + Prefab + (bOpen ? TEXT("") : TEXT(" open-failed: ") + CandidateError);
						for (int32 P = 0; bOpen && P < Candidate.PartCount() && P < 4; ++P) { OkMeshView View{}; Candidate.PartAt(P, View); Row += FString::Printf(TEXT(" | %d kind=%d draw=%d %s"), P, View.shader_kind, View.item_draw, *Candidate.MaterialMetadataJson(P).Left(160)); }
						Scanned.Add(MakeShared<FJsonValueString>(Row));
					}
					if (bOpen && HasPatternPart(Candidate)) { Session = MoveTemp(Candidate); Result->SetStringField(TEXT("item"), Item->GetStringField(TEXT("name"))); break; }
				}
				Result->SetArrayField(TEXT("scanned"), Scanned);
				if (!Session.IsOpen() || !HasPatternPart(Session)) return Stop(TEXT("no main_item_studio pattern item among the first catalogue items"));
			}
			Result->SetStringField(TEXT("model"), Model); Result->SetStringField(TEXT("prefab"), Prefab);
			TArray<TSharedPtr<FJsonValue>> Shaders;
			for (int32 P = 0; P < Session.PartCount(); ++P) { OkMeshView View{}; Session.PartAt(P, View); Shaders.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%d %s kind=%d item_draw=%d"), P, *Session.MaterialShaderName(P), View.shader_kind, View.item_draw))); }
			Result->SetArrayField(TEXT("parts"), Shaders);
			FActorSpawnParameters Params; Params.ObjectFlags = RF_Transient;
			AKKPreviewActor* A = World->SpawnActor<AKKPreviewActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!A) return Stop(TEXT("actor not spawned"));
			Actor = A; A->Session = MoveTemp(Session);
			Stripes.Reset(MakeStripes(FColor::White, FColor(0, 0, 0, 255)));Second.Reset(MakeStripes(FColor(255, 40, 200, 255), FColor(40, 220, 255, 255)));
			// Before the load: the item graph is built with its pattern slots (the override is part of its key).
			A->MaterialOverrides = bPatternMode ? TArray<FOKMaterialOverride>{
				Make(TEXT("_PatternMask1"), EOKMaterialOverrideKind::Texture, FLinearColor::White, Stripes.Get()),
				Make(TEXT("_Patternuv1"), EOKMaterialOverrideKind::Vector, FLinearColor(0.f, 0.f, 4.f, 4.f)),
				Make(TEXT("_Color1_2"), EOKMaterialOverrideKind::Color, FLinearColor(1.f, 0.f, 0.f, 1.f)),
				Make(TEXT("_Color"), EOKMaterialOverrideKind::Color, FLinearColor(1.f, 1.f, 0.f, 1.f)) }
				: TArray<FOKMaterialOverride>{ Make(TEXT("_MainTex"), EOKMaterialOverrideKind::Texture, FLinearColor::White, Stripes.Get()) };
			for (auto& O : A->MaterialOverrides) O.Slot = Target;
			if (!A->LoadSession(Error)) return Stop(TEXT("load: ") + Error);
			Frame();
			Stage = 1; return true;
		}
		AKKPreviewActor* A = Actor.Get(); if (!A) return Stop(TEXT("actor gone"));
		if (Stage == 1)
		{
			if (!A->BeginProgressiveMaterials()) return true;
			InitialPending = A->PendingPreviewMaterials.Num();
			Result->SetNumberField(TEXT("progressive_pending"), InitialPending);
			Stage = 2; return true;
		}
		if (Stage == 2)
		{
			if (!A->TickProgressiveMaterials()) return true;
			const FLinearColor Red(1.f, 0.f, 0.f, 1.f);
			Patterned = Count(bPatternMode ? TEXT("_PatternMask1") : TEXT("_MainTex"), nullptr, Stripes.Get());
			const int32 SecondSlots = bPatternMode ? Count(TEXT("_Color1_2"), &Red, nullptr) : Patterned;
			Result->SetNumberField(TEXT("after_swap_pattern_slots"), Patterned);
			Result->SetNumberField(TEXT("after_swap_color1_2_slots"), SecondSlots);
			if (!Patterned || !SecondSlots) return Stop(TEXT("Overrides missing after the progressive swap"));
			if (++Frames < 20) return true; // let the final shaders render
			Shot(TEXT("_pattern")); Frames = 0; Stage = 3; return true;
		}
		if (Stage == 3)
		{
			if (++Frames < 5) return true;
			// Live change, no rebuild: green pattern colour, rotated and clamped pattern.
			TArray<FString> Unapplied;
			TArray<FOKMaterialOverride> Live = bPatternMode ? TArray<FOKMaterialOverride>{
				Make(TEXT("_PatternMask1"), EOKMaterialOverrideKind::Texture, FLinearColor::White, Stripes.Get()),
				Make(TEXT("_Patternuv1"), EOKMaterialOverrideKind::Vector, FLinearColor(0.f, 0.f, 2.f, 2.f)),
				Make(TEXT("_patternrotator1"), EOKMaterialOverrideKind::Scalar, FLinearColor(0.25f, 0.f, 0.f, 0.f)),
				Make(TEXT("_Color1_2"), EOKMaterialOverrideKind::Color, FLinearColor(0.f, 0.8f, 0.2f, 1.f)) }
				: TArray<FOKMaterialOverride>{ Make(TEXT("_MainTex"), EOKMaterialOverrideKind::Texture, FLinearColor::White, Second.Get()) };
			for (auto& O : Live) O.Slot = Target;
			const int32 Writes = A->SetMaterialOverrides(Live, &Unapplied);
			Result->SetNumberField(TEXT("live_writes"), Writes);
			Result->SetNumberField(TEXT("live_unapplied"), Unapplied.Num());
			const FLinearColor Yellow(1.f, 1.f, 0.f, 1.f);
			Result->SetNumberField(TEXT("live_color_restored_slots"), Count(TEXT("_Color"), &Yellow, nullptr));
			if (!Writes || Unapplied.Num()) return Stop(TEXT("Live override did not apply: ") + FString::Join(Unapplied, TEXT("; ")));
			if (bPatternMode && Count(TEXT("_Color"), &Yellow, nullptr)) return Stop(TEXT("A removed override (_Color) was not restored"));
			Frames = 0; Stage = 4; return true;
		}
		if (Stage == 4)
		{
			if (++Frames < 10) return true;
			Shot(TEXT("_live")); Frames = 0; Stage = 5; return true;
		}
		if (Stage == 5)
		{
			if (++Frames < 5) return true;
			A->SetMaterialOverrides({});
			Result->SetNumberField(TEXT("cleared_pattern_slots"), Count(TEXT("_PatternMask1"), nullptr, Stripes.Get()));

			if (bPatternMode ? Count(TEXT("_PatternMask1"), nullptr, Stripes.Get()) : Count(TEXT("_MainTex"), nullptr, Second.Get())) return Stop(TEXT("Clearing the overrides left the override"));
			Frames = 0; Stage = 6; return true;
		}
		if (Stage == 6)
		{
			if (++Frames < 10) return true;
			Shot(TEXT("_cleared")); Frames = 0; Stage = 7; return true;
		}
		if (++Frames < 5) return true;
		return Stop(FString());
	}
};
}

static FAutoConsoleCommandWithWorld MaterialOverrideAudit(TEXT("ok.AuditMaterialOverrides"),
	TEXT("Verify AKKPreviewActor::SetMaterialOverrides on a patterned Studio item (OK_MATOVR_OUTPUT)"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		auto Run = MakeShared<OKMaterialOverrideAudit::FRun>();
		Run->World = World;
		Run->Output = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_MATOVR_OUTPUT"));
		if (!World || Run->Output.IsEmpty()) { UE_LOG(LogTemp, Error, TEXT("ok.AuditMaterialOverrides needs OK_MATOVR_OUTPUT")); return; }
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float Delta) { return Run->Tick(Delta); }));
	}));
#endif
