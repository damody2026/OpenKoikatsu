#include "OKSceneCard.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "KKAnimatedPng.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OKSceneActor.h"
#include "OKSceneDocument.h"
#include "OKStudioImport.h"
#include "TextureResource.h"

namespace
{
	const uint8 PngSignature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	/** okSc payload header: magic, format version. */
	const uint8 PayloadMagic[4] = { 'O', 'K', 'S', 'C' };
	constexpr uint32 PayloadVersion = 1;
	/** Character card references inside the embedded scene JSON. */
	const TCHAR* EmbeddedCardPrefix = TEXT("okscene-card:");

	uint32 Be32(const uint8* P) { return (uint32(P[0]) << 24) | (uint32(P[1]) << 16) | (uint32(P[2]) << 8) | uint32(P[3]); }
	void Put32(TArray<uint8>& Out, uint32 V) { Out.Add(uint8(V >> 24)); Out.Add(uint8(V >> 16)); Out.Add(uint8(V >> 8)); Out.Add(uint8(V)); }

	/** PNG chunk CRC (CRC-32, ISO 3309). */
	uint32 PngCrc(const uint8* Data, int64 Size)
	{
		static uint32 Table[256];
		static bool bTable = false;
		if (!bTable)
		{
			for (uint32 N = 0; N < 256; ++N) { uint32 C = N; for (int32 K = 0; K < 8; ++K) C = C & 1 ? 0xEDB88320u ^ (C >> 1) : C >> 1; Table[N] = C; }
			bTable = true;
		}
		uint32 C = 0xFFFFFFFFu;
		for (int64 I = 0; I < Size; ++I) C = Table[(C ^ Data[I]) & 0xFF] ^ (C >> 8);
		return C ^ 0xFFFFFFFFu;
	}

	struct FChunk { FString Type; int64 Offset = 0; uint32 Size = 0; };

	/** The PNG's chunks up to IEND; OutEnd: the offset just after IEND (data a KStudio scene appends starts there). */
	bool Chunks(const TArray<uint8>& Png, TArray<FChunk>& Out, int64& OutEnd)
	{
		Out.Reset();
		OutEnd = 0;
		if (Png.Num() < 8 || FMemory::Memcmp(Png.GetData(), PngSignature, 8)) return false;
		int64 P = 8;
		while (P + 12 <= Png.Num())
		{
			const uint32 Size = Be32(Png.GetData() + P);
			if (P + 12 + int64(Size) > Png.Num()) return false;
			FChunk C;
			C.Type = FString(4, reinterpret_cast<const ANSICHAR*>(Png.GetData() + P + 4));
			C.Offset = P;
			C.Size = Size;
			Out.Add(C);
			P += 12 + int64(Size);
			if (C.Type == TEXT("IEND")) { OutEnd = P; return true; }
		}
		return false;
	}

	void AddChunk(TArray<uint8>& Out, const char* Type, const TArray<uint8>& Data)
	{
		Put32(Out, uint32(Data.Num()));
		const int32 TypeAt = Out.Num();
		Out.Append(reinterpret_cast<const uint8*>(Type), 4);
		Out.Append(Data);
		Put32(Out, PngCrc(Out.GetData() + TypeAt, 4 + Data.Num()));
	}

	/** The project-relative or absolute card path as a file. */
	FString CardFile(const FString& Card)
	{
		return FPaths::IsRelative(Card) ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Card) : Card;
	}

	/** A KStudio scene: a PNG followed by the scene's version string (.NET length-prefixed, like "1.1.2.1"). */
	bool LooksLikeKStudio(const TArray<uint8>& Bytes, int64 End)
	{
		if (End <= 0 || End + 2 > Bytes.Num()) return false;
		const int32 Len = Bytes[End];
		if (Len < 1 || Len > 16 || End + 1 + Len > Bytes.Num()) return false;
		for (int32 I = 0; I < Len; ++I) { const uint8 C = Bytes[End + 1 + I]; if (!((C >= '0' && C <= '9') || C == '.')) return false; }
		return true;
	}

	/** Camera transform looking along Dir with the image's up = Up. */
	FTransform Look(const FVector& At, const FVector& Dir, const FVector& Up)
	{
		return FTransform(FRotationMatrix::MakeFromXZ(Dir, Up).ToQuat(), At);
	}
}

FOKSceneCard::EKind FOKSceneCard::Kind(const FString& Path)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return EKind::None;
	TArray<FChunk> List;
	int64 End = 0;
	if (!Chunks(Bytes, List, End))
	{
		// Not a PNG: an .okscene is JSON.
		const int32 Skip = Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF ? 3 : 0;
		for (int32 I = Skip; I < Bytes.Num(); ++I) { const uint8 C = Bytes[I]; if (C == ' ' || C == '\t' || C == '\r' || C == '\n') continue; return C == '{' ? EKind::Json : EKind::None; }
		return EKind::None;
	}
	if (List.ContainsByPredicate([](const FChunk& C) { return C.Type == TEXT("okSc"); })) return EKind::OkScene;
	return LooksLikeKStudio(Bytes, End) ? EKind::KStudio : EKind::None;
}

bool FOKSceneCard::Capture(AOKSceneActor* Scene, const FTransform& View, float FovDegrees, const FPostProcessSettings* Post,
	int32 InWidth, int32 InHeight, bool bTransparent, TArray<FColor>& OutPixels, FString& OutError)
{
	UWorld* World = Scene ? Scene->GetWorld() : nullptr;
	if (!World) { OutError = TEXT("no scene world"); return false; }
	const int32 W = FMath::Clamp(InWidth, 16, 8192), H = FMath::Clamp(InHeight, 16, 8192);
	const TArray<AActor*> Objects = bTransparent ? Scene->ObjectPreviews() : TArray<AActor*>();
	auto Shot = [&](ESceneCaptureSource Source, ETextureRenderTargetFormat Format) -> UTextureRenderTarget2D*
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Scene);
		Target->RenderTargetFormat = Format;
		Target->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
		Target->InitAutoFormat(W, H);
		Target->UpdateResourceImmediate(true);
		USceneCaptureComponent2D* C = NewObject<USceneCaptureComponent2D>(Scene);
		C->RegisterComponentWithWorld(World);
		C->SetWorldTransform(View);
		C->FOVAngle = FovDegrees;
		C->CaptureSource = Source;
		C->bCaptureEveryFrame = false;
		C->bCaptureOnMovement = false;
		C->bAlwaysPersistRenderingState = true;
		if (Post) { C->PostProcessSettings = *Post; C->PostProcessBlendWeight = 1.f; }
		if (bTransparent) { C->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList; C->ShowOnlyActors = Objects; C->ShowFlags.SetAtmosphere(false); C->ShowFlags.SetFog(false); }
		C->TextureTarget = Target;
		C->CaptureScene();
		C->DestroyComponent();
		return Target;
	};
	FTextureRenderTargetResource* Resource = Shot(SCS_FinalColorLDR, RTF_RGBA8_SRGB)->GameThread_GetRenderTargetResource();
	if (!Resource || !Resource->ReadPixels(OutPixels) || OutPixels.Num() != W * H) { OutError = TEXT("could not read the capture"); return false; }
	if (bTransparent)
	{
		// Covered where the objects have depth (nothing drawn: the far plane or the cleared value).
		TArray<FLinearColor> Depth;
		FTextureRenderTargetResource* DepthResource = Shot(SCS_SceneDepth, RTF_R32f)->GameThread_GetRenderTargetResource();
		if (!DepthResource || !DepthResource->ReadLinearColorPixels(Depth) || Depth.Num() != OutPixels.Num()) { OutError = TEXT("could not read the coverage"); return false; }
		for (int32 I = 0; I < OutPixels.Num(); ++I) OutPixels[I].A = Depth[I].R > 0.f && Depth[I].R < 1.e6f ? 255 : 0;
	}
	else for (FColor& C : OutPixels) C.A = 255;
	return true;
}

bool FOKSceneCard::CaptureViews(AOKSceneActor* Scene, const FTransform& View, float FovDegrees, const FPostProcessSettings* Post,
	TArray<TArray<uint8>>& OutPngs, FString& OutError)
{
	OutPngs.Reset();
	if (!Scene || !Scene->Document) { OutError = TEXT("no scene"); return false; }
	// Frame the characters (standing height) and items, in the world.
	FBox Box(ForceInit);
	for (const FOKSceneObject& O : Scene->Document->Objects)
	{
		if (O.Kind != EOKSceneObjectKind::Character && O.Kind != EOKSceneObjectKind::Item) continue;
		const FVector P = Scene->Document->WorldTransform(O.Id).GetLocation() + Scene->GetActorLocation();
		Box += O.Kind == EOKSceneObjectKind::Character ? FBox(P + FVector(-40.f, -40.f, 0.f), P + FVector(40.f, 40.f, 170.f)) : FBox(P - FVector(40.f), P + FVector(40.f));
	}
	if (!Box.IsValid) Box = FBox(Scene->GetActorLocation() + FVector(-100.f, -100.f, 0.f), Scene->GetActorLocation() + FVector(100.f, 100.f, 170.f));
	const FVector C = Box.GetCenter(), E = Box.GetExtent() * 1.15f;
	constexpr float Fov = 40.f;
	const float TanH = FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f)), TanV = TanH * float(Height) / float(Width);
	// Distance that fits a half-width A across and a half-height B up, in front of a half-depth D.
	auto Distance = [&](float A, float B, float D) { return FMath::Max(A / TanH, B / TanV) + D; };
	const FTransform Views[4] = {
		View,
		Look(C + FVector(0.f, Distance(E.X, E.Z, E.Y), 0.f), FVector(0.f, -1.f, 0.f), FVector::UpVector),   // front (characters face +Y)
		Look(C + FVector(Distance(E.Y, E.Z, E.X), 0.f, 0.f), FVector(-1.f, 0.f, 0.f), FVector::UpVector),   // side
		Look(C + FVector(0.f, 0.f, Distance(E.X, E.Y, E.Z)), FVector(0.f, 0.f, -1.f), FVector(0.f, -1.f, 0.f)), // top, the front at the bottom
	};
	for (int32 I = 0; I < 4; ++I)
	{
		TArray<FColor> Pixels;
		if (!Capture(Scene, Views[I], I == 0 ? FovDegrees : Fov, Post, Width, Height, false, Pixels, OutError)) return false;
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		if (Png.IsEmpty()) { OutError = TEXT("PNG encoding failed"); return false; }
		OutPngs.Emplace(Png.GetData(), int32(Png.Num()));
	}
	return true;
}

bool FOKSceneCard::Write(const UOKSceneDocument* Document, const TArray<TArray<uint8>>& Views, const FString& Path, FString& OutError)
{
	if (!Document || Views.Num() != 4) { OutError = TEXT("a scene and four views are needed"); return false; }
	// The scene with its character cards replaced by references to the embedded copies.
	UOKSceneDocument* Copy = DuplicateObject(Document, GetTransientPackage());
	TArray<TArray<uint8>> Cards;
	TMap<FString, int32> CardIndex;
	for (FOKSceneObject& O : Copy->Objects)
	{
		if (O.Kind != EOKSceneObjectKind::Character || O.Character.Card.IsEmpty()) continue;
		const FString File = CardFile(O.Character.Card);
		int32* Index = CardIndex.Find(File);
		if (!Index)
		{
			TArray<uint8> Bytes;
			if (!FFileHelper::LoadFileToArray(Bytes, *File)) { OutError = TEXT("cannot read the character card ") + File; return false; }
			Index = &CardIndex.Add(File, Cards.Add(MoveTemp(Bytes)));
		}
		O.Character.Card = FString::Printf(TEXT("%s%d"), EmbeddedCardPrefix, *Index);
	}
	const FTCHARToUTF8 Json(*Copy->ToJson());
	TArray<uint8> Raw;
	Put32(Raw, uint32(Json.Length()));
	Raw.Append(reinterpret_cast<const uint8*>(Json.Get()), Json.Length());
	Put32(Raw, uint32(Cards.Num()));
	for (const TArray<uint8>& Card : Cards) { Put32(Raw, uint32(Card.Num())); Raw.Append(Card); }
	int32 Packed = FCompression::CompressMemoryBound(NAME_Zlib, Raw.Num());
	TArray<uint8> Payload;
	Payload.Append(PayloadMagic, 4);
	Put32(Payload, PayloadVersion);
	Put32(Payload, uint32(Raw.Num()));
	const int32 Header = Payload.Num();
	Payload.AddUninitialized(Packed);
	if (!FCompression::CompressMemory(NAME_Zlib, Payload.GetData() + Header, Packed, Raw.GetData(), Raw.Num())) { OutError = TEXT("compressing the scene failed"); return false; }
	Payload.SetNum(Header + Packed);
	// The first view with the okSc chunk before IEND; the APNG keeps private chunks of its source image.
	TArray<FChunk> List;
	int64 End = 0;
	if (!Chunks(Views[0], List, End)) { OutError = TEXT("the scene view is not a PNG"); return false; }
	const FChunk& Iend = List.Last();
	TArray<uint8> Source(Views[0].GetData(), int32(Iend.Offset));
	AddChunk(Source, "okSc", Payload);
	Source.Append(Views[0].GetData() + Iend.Offset, int32(End - Iend.Offset));
	TArray<uint8> Card;
	if (!FKKAnimatedPng::BuildFrames(Source, Views, { SecondsPerView, SecondsPerView, SecondsPerView, SecondsPerView }, Card, OutError)) return false;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveArrayToFile(Card, *Path)) { OutError = TEXT("cannot write ") + Path; return false; }
	return true;
}

bool FOKSceneCard::ReadPayload(const TArray<uint8>& Bytes, FString& OutSceneJson, TArray<TArray<uint8>>& OutCards, FString& OutError)
{
	TArray<FChunk> List;
	int64 End = 0;
	if (!Chunks(Bytes, List, End)) { OutError = TEXT("not a PNG"); return false; }
	const FChunk* Chunk = List.FindByPredicate([](const FChunk& C) { return C.Type == TEXT("okSc"); });
	if (!Chunk) { OutError = TEXT("not an OK scene card (no okSc chunk)"); return false; }
	const uint8* P = Bytes.GetData() + Chunk->Offset + 8;
	if (PngCrc(Bytes.GetData() + Chunk->Offset + 4, 4 + int64(Chunk->Size)) != Be32(P + Chunk->Size)) { OutError = TEXT("the scene data is damaged (CRC)"); return false; }
	if (Chunk->Size < 12 || FMemory::Memcmp(P, PayloadMagic, 4)) { OutError = TEXT("unknown scene data"); return false; }
	if (Be32(P + 4) != PayloadVersion) { OutError = FString::Printf(TEXT("scene card version %u is newer than this build"), Be32(P + 4)); return false; }
	const uint32 RawSize = Be32(P + 8);
	if (RawSize > 1u << 30) { OutError = TEXT("scene data too large"); return false; }
	TArray<uint8> Raw;
	Raw.SetNumUninitialized(int32(RawSize));
	if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), Raw.Num(), P + 12, int32(Chunk->Size) - 12)) { OutError = TEXT("the scene data cannot be unpacked"); return false; }
	int64 R = 0;
	auto Take = [&](uint32& V) { if (R + 4 > Raw.Num()) return false; V = Be32(Raw.GetData() + R); R += 4; return true; };
	uint32 JsonSize = 0, Count = 0;
	if (!Take(JsonSize) || R + JsonSize > Raw.Num()) { OutError = TEXT("scene data truncated"); return false; }
	const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Raw.GetData() + R), int32(JsonSize));
	OutSceneJson = FString(Text.Length(), Text.Get());
	R += JsonSize;
	if (!Take(Count) || Count > 4096) { OutError = TEXT("scene data truncated"); return false; }
	OutCards.Reset();
	for (uint32 I = 0; I < Count; ++I)
	{
		uint32 Size = 0;
		if (!Take(Size) || R + Size > Raw.Num()) { OutError = TEXT("scene data truncated"); return false; }
		OutCards.Emplace(Raw.GetData() + R, int32(Size));
		R += Size;
	}
	return true;
}

FString FOKSceneCard::CardDirectoryFor(const FString& Path)
{
	// Beside a scene of this project's own UserData/Studio; a scene anywhere else (the OK base library, the game's
	// folder, a download) gets its card files under UserData/Studio/import instead: its folder may be read-only, the
	// library's files are served by its replay (files written there are not found), and the game is not ours to write.
	const FString Full = FPaths::ConvertRelativePathToFull(Path);
	const FString Own = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UserData/Studio"));
	if (FPaths::IsUnderDirectory(Full, Own)) return FPaths::GetPath(Full) / FPaths::GetBaseFilename(Full) + TEXT("_cards");
	return Own / TEXT("import") / FString::Printf(TEXT("%s_%08x_cards"), *FPaths::GetBaseFilename(Full), GetTypeHash(Full.ToLower()));
}

bool FOKSceneCard::Open(const FString& Path, UOKSceneDocument* Document, FString& OutError, FString* OutNote)
{
	if (!Document) { OutError = TEXT("no document"); return false; }
	const FString CardDirectory = CardDirectoryFor(Path);
	switch (Kind(Path))
	{
	case EKind::Json:
		if (OutNote) *OutNote = TEXT("OK scene (.okscene)");
		return Document->LoadFromFile(Path, OutError);
	case EKind::KStudio:
	{
		FOKStudioImport::FReport Report;
		if (!FOKStudioImport::Import(Path, Document, CardDirectory, Report, OutError)) return false;
		if (OutNote) *OutNote = TEXT("KStudio scene card imported: ") + Report.ToJson();
		return true;
	}
	case EKind::OkScene:
	{
		TArray<uint8> Bytes;
		FString Json;
		TArray<TArray<uint8>> Cards;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path) || !ReadPayload(Bytes, Json, Cards, OutError)) { if (OutError.IsEmpty()) OutError = TEXT("cannot read ") + Path; return false; }
		IFileManager::Get().MakeDirectory(*CardDirectory, true);
		TArray<FString> Files;
		for (int32 I = 0; I < Cards.Num(); ++I)
		{
			Files.Add(CardDirectory / FString::Printf(TEXT("card_%d.png"), I));
			if (!FFileHelper::SaveArrayToFile(Cards[I], *Files.Last())) { OutError = TEXT("cannot write ") + Files.Last(); return false; }
		}
		if (!Document->FromJson(Json, OutError)) return false;
		for (FOKSceneObject& O : Document->Objects)
		{
			if (O.Kind != EOKSceneObjectKind::Character || !O.Character.Card.StartsWith(EmbeddedCardPrefix)) continue;
			const int32 Index = FCString::Atoi(*O.Character.Card.RightChop(FCString::Strlen(EmbeddedCardPrefix)));
			O.Character.Card = Files.IsValidIndex(Index) ? Files[Index] : FString();
		}
		Document->NotifyChanged();
		if (OutNote) *OutNote = FString::Printf(TEXT("OK scene card (%d character cards)"), Cards.Num());
		return true;
	}
	default:
		OutError = TEXT("not an OK scene card, KStudio scene card or .okscene: ") + Path;
		return false;
	}
}
