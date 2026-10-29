#include "OKStudioThumbnails.h"
#include "Async/Async.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

FOKStudioThumbnails& FOKStudioThumbnails::Get()
{
	static FOKStudioThumbnails Thumbnails;
	return Thumbnails;
}

void FOKStudioThumbnails::Shrink(const TArray<FColor>& In, int32 W, int32 H, int32 Size, FDecoded& Out)
{
	// Box filter down to Size on the long side (pictures are only ever shown small).
	const float K = FMath::Max(1.f, float(FMath::Max(W, H)) / float(FMath::Max(Size, 8)));
	Out.Width = FMath::Max(1, FMath::RoundToInt(W / K));
	Out.Height = FMath::Max(1, FMath::RoundToInt(H / K));
	Out.Bgra.SetNumUninitialized(Out.Width * Out.Height * 4);
	for (int32 Y = 0; Y < Out.Height; ++Y)
		for (int32 X = 0; X < Out.Width; ++X)
		{
			const int32 X0 = FMath::Min(W - 1, int32(X * K)), X1 = FMath::Clamp(int32((X + 1) * K), X0 + 1, W);
			const int32 Y0 = FMath::Min(H - 1, int32(Y * K)), Y1 = FMath::Clamp(int32((Y + 1) * K), Y0 + 1, H);
			uint32 Sum[4] = {};
			int32 Count = 0;
			for (int32 SY = Y0; SY < Y1; SY += FMath::Max(1, (Y1 - Y0) / 4))
				for (int32 SX = X0; SX < X1; SX += FMath::Max(1, (X1 - X0) / 4))
				{
					const FColor& C = In[SY * W + SX];
					Sum[0] += C.B; Sum[1] += C.G; Sum[2] += C.R; Sum[3] += C.A; ++Count;
				}
			uint8* P = &Out.Bgra[(Y * Out.Width + X) * 4];
			for (int32 I = 0; I < 4; ++I) P[I] = uint8(Sum[I] / FMath::Max(Count, 1));
		}
}

const FSlateBrush* FOKStudioThumbnails::File(const FString& Path, int32 Size)
{
	const FString Key = FString::Printf(TEXT("%s|%d"), *Path, Size);
	if (FEntry* E = Cache.Find(Key)) { E->Used = ++Clock; return E->Brush.Get(); }
	if (Requested.Contains(Key)) return nullptr;
	Requested.Add(Key);
	++InFlight;
	// Loaded here on the game thread: the worker only looks it up.
	FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	Async(EAsyncExecution::ThreadPool, [this, Key, Path, Size]
	{
		FDecoded D;
		D.Key = Key;
		TArray<uint8> Bytes;
		IImageWrapperModule& Module = FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		if (FFileHelper::LoadFileToArray(Bytes, *Path) && Bytes.Num() > 8)
		{
			// The PNG's own image (an APNG's default image is its first frame); the card data after IEND is ignored.
			TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
			TArray<uint8> Raw;
			if (Png && Png->SetCompressed(Bytes.GetData(), Bytes.Num()) && Png->GetRaw(ERGBFormat::BGRA, 8, Raw))
			{
				const int32 W = int32(Png->GetWidth()), H = int32(Png->GetHeight());
				TArray<FColor> Pixels;
				Pixels.SetNumUninitialized(W * H);
				FMemory::Memcpy(Pixels.GetData(), Raw.GetData(), FMath::Min<int64>(Raw.Num(), int64(W) * H * 4));
				Shrink(Pixels, W, H, Size, D);
			}
		}
		FScopeLock Guard(&Lock);
		Ready.Add(MoveTemp(D));
		--InFlight;
	});
	return nullptr;
}

void FOKStudioThumbnails::Put(const FString& Key, const TArray<FColor>& Pixels, int32 Width, int32 Height, int32 Size)
{
	if (Pixels.Num() < Width * Height || Width <= 0 || Height <= 0) return;
	FDecoded D;
	D.Key = Key;
	Shrink(Pixels, Width, Height, Size, D);
	Make(MoveTemp(D));
}

const FSlateBrush* FOKStudioThumbnails::Find(const FString& Key) const
{
	const FEntry* E = Cache.Find(Key);
	return E ? E->Brush.Get() : nullptr;
}

void FOKStudioThumbnails::Forget(const FString& Key)
{
	for (auto It = Cache.CreateIterator(); It; ++It) if (It.Key() == Key || It.Key().StartsWith(Key + TEXT("|"))) It.RemoveCurrent();
	for (auto It = Requested.CreateIterator(); It; ++It) if (*It == Key || It->StartsWith(Key + TEXT("|"))) It.RemoveCurrent();
}

void FOKStudioThumbnails::Make(FDecoded&& D)
{
	FEntry& E = Cache.FindOrAdd(D.Key);
	E.Used = ++Clock;
	if (D.Width > 0 && D.Bgra.Num() == D.Width * D.Height * 4)
	{
		static int32 Serial = 0;
		E.Brush = FSlateDynamicImageBrush::CreateWithImageData(FName(*FString::Printf(TEXT("OKStudioThumb%d"), ++Serial)), FVector2D(D.Width, D.Height), D.Bgra);
	}
	// At most 400 pictures: the least recently shown go.
	while (Cache.Num() > 400)
	{
		FString Oldest;
		uint64 Best = MAX_uint64;
		for (const TPair<FString, FEntry>& P : Cache) if (P.Value.Used < Best) { Best = P.Value.Used; Oldest = P.Key; }
		Cache.Remove(Oldest);
		Requested.Remove(Oldest);
	}
}

void FOKStudioThumbnails::Tick()
{
	TArray<FDecoded> Now;
	{
		FScopeLock Guard(&Lock);
		// A few brushes a frame (each uploads a texture).
		const int32 Take = FMath::Min(Ready.Num(), 6);
		for (int32 I = 0; I < Take; ++I) Now.Add(MoveTemp(Ready[I]));
		Ready.RemoveAt(0, Take);
	}
	for (FDecoded& D : Now) Make(MoveTemp(D));
}

int32 FOKStudioThumbnails::Pending() const
{
	return InFlight.Load();
}
