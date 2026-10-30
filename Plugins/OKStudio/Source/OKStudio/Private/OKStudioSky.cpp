#include "OKStudioSky.h"

#include "OKSceneActor.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FOKSkyBuffer { int32 Slot; int32 Size; };
	/** A uniform of a program: name, constant buffer slot, byte offset, components (16: a matrix, column-major), the
	 * shader property type (0 colour, 1 vector, 2 float, 3 range, -1 not a property) and its [Gamma] flag. */
	struct FOKSkyUniform { const TCHAR* Name; int32 Slot; int32 Offset; int32 Dim; int32 Type; bool bGamma; };
	struct FOKSkyTexture { const TCHAR* Name; int32 Index; int32 Sampler; };
	struct FOKSkyProgram
	{
		const TCHAR* Keywords;
		const FOKSkyBuffer* VertexBuffers; int32 NumVertexBuffers;
		const FOKSkyBuffer* PixelBuffers; int32 NumPixelBuffers;
		const FOKSkyUniform* VertexUniforms; int32 NumVertexUniforms;
		const FOKSkyUniform* PixelUniforms; int32 NumPixelUniforms;
		const FOKSkyTexture* Textures; int32 NumTextures;
		const TCHAR* Vertex; const TCHAR* Glue; const TCHAR* Pixel;
	};
#include "StudioSkyPrograms.inl"

	TSharedPtr<FJsonObject> LoadCatalog()
	{
		static TSharedPtr<FJsonObject> Catalog;
		static bool bLoaded = false;
		if (bLoaded) return Catalog;
		bLoaded = true;
		FString Text;
		const FString File = AOKSceneActor::LibraryDirectory() / TEXT("studio/sky.json");
		if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Catalog)) Catalog.Reset();
		if (!Catalog.IsValid()) UE_LOG(LogTemp, Warning, TEXT("OK studio sky: %s not readable (ok-extract <game> --oklib studio-sky <library>)"), *File);
		return Catalog;
	}

	TSharedPtr<FJsonObject> FindPattern(int32 No)
	{
		const TSharedPtr<FJsonObject> Catalog = LoadCatalog();
		const TArray<TSharedPtr<FJsonValue>>* Patterns = nullptr;
		if (!Catalog.IsValid() || !Catalog->TryGetArrayField(TEXT("patterns"), Patterns)) return nullptr;
		for (const TSharedPtr<FJsonValue>& P : *Patterns)
		{
			const TSharedPtr<FJsonObject> O = P->AsObject();
			if (O.IsValid() && int32(O->GetNumberField(TEXT("no"))) == No) return O;
		}
		return nullptr;
	}

	/** Unity's GammaToLinearSpace (the exact sRGB curve): what a colour property becomes in Studio's linear space. */
	float ToLinear(float V) { return V <= 0.04045f ? V / 12.92f : FMath::Pow((V + 0.055f) / 1.055f, 2.4f); }

	FString Bits(float V) { uint32 Word; FMemory::Memcpy(&Word, &V, 4); return FString::Printf(TEXT("asfloat(0x%08xu)"), Word); }

	/** The program sharing the most keywords with the pattern, then the one with the fewest others. */
	const FOKSkyProgram* PickProgram(const TSet<FString>& Want)
	{
		const FOKSkyProgram* Best = nullptr;
		int32 BestMatch = -1, BestExtra = MAX_int32;
		for (const FOKSkyProgram& P : GOKSkyPrograms)
		{
			TArray<FString> Have;
			FString(P.Keywords).ParseIntoArrayWS(Have);
			int32 Match = 0;
			for (const FString& K : Have) Match += Want.Contains(K) ? 1 : 0;
			const int32 Extra = Have.Num() - Match;
			if (Match > BestMatch || (Match == BestMatch && Extra < BestExtra)) { Best = &P; BestMatch = Match; BestExtra = Extra; }
		}
		return Best;
	}

	UTexture2D* LoadTexture(const FString& File, bool bSRGB, bool bPoint, bool bClamp)
	{
		static TMap<FString, TStrongObjectPtr<UTexture2D>> Cache;
		const FString Key = FString::Printf(TEXT("%s|%d%d%d"), *File, bSRGB, bPoint, bClamp);
		if (const TStrongObjectPtr<UTexture2D>* Hit = Cache.Find(Key)) return Hit->Get();
		UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(File);
		if (!Texture) { UE_LOG(LogTemp, Warning, TEXT("OK studio sky: texture %s not readable"), *File); return nullptr; }
		Texture->SRGB = bSRGB;
		Texture->Filter = bPoint ? TF_Nearest : TF_Bilinear;
		Texture->AddressX = Texture->AddressY = bClamp ? TA_Clamp : TA_Wrap;
		Texture->UpdateResource();
		Cache.Add(Key, TStrongObjectPtr<UTexture2D>(Texture));
		return Texture;
	}
}

const TArray<FOKStudioSkyPattern>& OKStudioSkyPatterns()
{
	static TArray<FOKStudioSkyPattern> Patterns;
	static bool bLoaded = false;
	if (bLoaded) return Patterns;
	bLoaded = true;
	const TSharedPtr<FJsonObject> Catalog = LoadCatalog();
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (Catalog.IsValid() && Catalog->TryGetArrayField(TEXT("patterns"), List))
		for (const TSharedPtr<FJsonValue>& P : *List)
			if (const TSharedPtr<FJsonObject> O = P->AsObject())
				// The maps' own skies are numbered from 1000 (studio_sky::MAP_PATTERN_BASE): not the scene's choices.
				if (int32(O->GetNumberField(TEXT("no"))) < 1000) Patterns.Add({ int32(O->GetNumberField(TEXT("no"))), O->GetStringField(TEXT("name")) });
	return Patterns;
}

bool OKStudioMapAmbient(int32 Map, FLinearColor& OutSHAr, FLinearColor& OutSHAg, FLinearColor& OutSHAb, TArray<FLinearColor>* OutHigher, int32* OutMode)
{
    if (OutMode) *OutMode = -1;
    if (Map < 0)
    {
        OutSHAr = OutSHAg = OutSHAb = FLinearColor(0,0,0,0);
        if (OutHigher) *OutHigher = { FLinearColor(0,0,0,0), FLinearColor(0,0,0,0), FLinearColor(0,0,0,0), FLinearColor(0,0,0,1) };
        return true;
    }
	static FString LoadedFile;
	static TMap<int32, TArray<FLinearColor>> Probes;
	static TMap<int32, int32> Modes;
	const FString File = AOKSceneActor::LibraryDirectory() / TEXT("studio/map_environments.json");
	if (LoadedFile != File)
	{
		LoadedFile = File;
		Probes.Reset();
		Modes.Reset();
		FString Text, Format;
		TSharedPtr<FJsonObject> Root;
		const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
		if (FFileHelper::LoadFileToString(Text, *File) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)
			&& Root && Root->TryGetStringField(TEXT("format"), Format) && Format == TEXT("ok-studio-map-environments-1") && Root->TryGetArrayField(TEXT("maps"), Maps))
		{
			for (const auto& Entry : *Maps)
			{
				const auto Row = Entry->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* Channels = nullptr;
				double No = -1;
				if (!Row || !Row->TryGetNumberField(TEXT("map"), No) || !Row->TryGetArrayField(TEXT("probe"), Channels) || Channels->Num() != 3) continue;
				TArray<FLinearColor> Packed;
				TArray<FLinearColor> Higher;
				FLinearColor Last(0,0,0,1);
				// Ambient mode Trilight (1) or Flat (3, one colour): Unity derives the ambient probe from the sky, equator
				// and ground colours at run time (the scene's baked m_AmbientProbe is stale: bath map 62 saved zeros). The
				// coefficients, fitted exactly (to 1e-5 in all three channels) on native renderer_sh of map 62: C0 = (sky +
				// ground) / 6 + 2 equator / 3, C1 (up) = sqrt(2/27) (sky - ground), C6 = 5/96 (equator - (sky + ground) / 2),
				// C8 = 3 C6; the colours linear, times the ambient intensity.
				double Mode = 0, Intensity = 1;
				Row->TryGetNumberField(TEXT("ambient_mode"), Mode);
				Row->TryGetNumberField(TEXT("ambient_intensity"), Intensity);
				auto AmbientColor = [&](const TCHAR* Key, FLinearColor& Out)
				{
					const TArray<TSharedPtr<FJsonValue>>* V = nullptr;
					if (!Row->TryGetArrayField(Key, V) || V->Num() < 3) return false;
					Out = FLinearColor(0, 0, 0, 0);
					for (int32 I = 0; I < 3; ++I) { const float G = float((*V)[I]->AsNumber()); (&Out.R)[I] = G <= .04045f ? G / 12.92f : FMath::Pow((G + .055f) / 1.055f, 2.4f); }
					Out *= float(Intensity);
					return true;
				};
				FLinearColor Sky, Equator, Ground;
				const bool bGradient = (Mode == 1 || Mode == 3) && AmbientColor(TEXT("ambient_sky"), Sky)
					&& (Mode == 3 || (AmbientColor(TEXT("ambient_equator"), Equator) && AmbientColor(TEXT("ambient_ground"), Ground)));
				if (bGradient && Mode == 3) Equator = Ground = Sky;
				for (const auto& Channel : *Channels)
				{
					const TArray<TSharedPtr<FJsonValue>>* Coefficients = nullptr;
					if (!Channel->TryGetArray(Coefficients) || Coefficients->Num() != 9) break;
					float C[9];
					bool Valid = true;
					for (int32 I = 0; I < 9; ++I)
					{
						double V = 0;
						if (!(*Coefficients)[I]->TryGetNumber(V) || !FMath::IsFinite(V)) { Valid = false; break; }
						C[I] = float(V);
					}
					if (!Valid) break;
					if (bGradient)
					{
						const int32 Ch = Packed.Num();
						const float S = (&Sky.R)[Ch], E = (&Equator.R)[Ch], G = (&Ground.R)[Ch];
						for (float& V : C) V = 0.f;
						C[0] = (S + G) / 6.f + 2.f * E / 3.f;
						C[1] = FMath::Sqrt(2.f / 27.f) * (S - G);
						C[6] = 5.f / 96.f * (E - (S + G) / 2.f);
						C[8] = 3.f * C[6];
					}
					Packed.Add(FLinearColor(C[3], C[1], C[2], C[0] - C[6]));
					Higher.Add(FLinearColor(C[4], C[5], 3.f*C[6], C[7]));
					(&Last.R)[Packed.Num()-1] = C[8];
				}
				if (Packed.Num() == 3) { Packed.Append(Higher); Packed.Add(Last); Probes.Add(int32(No), MoveTemp(Packed)); Modes.Add(int32(No), int32(Mode)); }
			}
		}
		else UE_LOG(LogTemp, Warning, TEXT("OK Studio map environment missing or invalid: %s"), *File);
	}
	const auto* Probe = Probes.Find(Map);
	if (!Probe) return false;
	OutSHAr = (*Probe)[0]; OutSHAg = (*Probe)[1]; OutSHAb = (*Probe)[2];
	if (OutHigher) { OutHigher->Reset(); for (int32 I=3; I<7; ++I) OutHigher->Add((*Probe)[I]); }
	if (OutMode) *OutMode = Modes.FindRef(Map);
	return true;
}

bool OKStudioSkyAmbient(int32 No, FLinearColor& OutSHAr, FLinearColor& OutSHAg, FLinearColor& OutSHAb)
{
	const TSharedPtr<FJsonObject> Pattern = FindPattern(No);
	if (!Pattern.IsValid()) return false;
	const TSharedPtr<FJsonObject> Colors = Pattern->GetObjectField(TEXT("colors"));
	const TSharedPtr<FJsonObject> Floats = Pattern->GetObjectField(TEXT("floats"));
	auto Color = [&](const TCHAR* Name)
	{
		const TArray<TSharedPtr<FJsonValue>>* C = nullptr;
		if (!Colors.IsValid() || !Colors->TryGetArrayField(Name, C) || C->Num() < 3) return FLinearColor::Black;
		return FLinearColor(ToLinear((*C)[0]->AsNumber()), ToLinear((*C)[1]->AsNumber()), ToLinear((*C)[2]->AsNumber()), 1.f);
	};
	auto Float = [&](const TCHAR* Name, double Default) { double V = Default; if (Floats.IsValid()) Floats->TryGetNumberField(Name, V); return float(V); };
	const FLinearColor Upper = Color(TEXT("_GradientSkyUpperColor")), Middle = Color(TEXT("_GradientSkyMiddleColor")), Lower = Color(TEXT("_GradientSkyLowerColor"));
	const float Mid = Float(TEXT("_GradientFadeMiddlePosition"), 0.5), Begin = Float(TEXT("_GradientFadeBegin"), -0.1), End = Float(TEXT("_GradientFadeEnd"), 1.0);
	// The gradient of the sky program (Funly 3D Standard, GRADIENT_BACKGROUND): the height y of the view direction
	// smooth-stepped from the lower to the middle colour below MidY = Begin + (End - Begin) Middle, then to the upper.
	const float MidY = Begin + (End - Begin) * Mid;
	auto Smooth = [](float X) { X = FMath::Clamp(X, 0.f, 1.f); return X * X * (3.f - 2.f * X); };
	auto Sky = [&](float Y)
	{
		if (Y >= MidY) return FMath::Lerp(Middle, Upper, Smooth((Y - MidY) / FMath::Max(End - MidY, 1e-4f)));
		return FMath::Lerp(Lower, Middle, Smooth((Y - Begin) / FMath::Max(MidY - Begin, 1e-4f)));
	};
	// The cosine-weighted ambient of an environment symmetric about the up axis: constant (1/2) Integral C(y) dy, up
	// term Integral C(y) y dy (the L1 band times 2/3, as Unity's probe evaluates), over y in [-1, 1].
	FLinearColor Constant(0, 0, 0, 0), Up(0, 0, 0, 0);
	const int32 Steps = 256;
	for (int32 I = 0; I < Steps; ++I)
	{
		const float Y = -1.f + (I + 0.5f) * 2.f / Steps;
		const FLinearColor C = Sky(Y);
		Constant += C * (1.f / Steps);
		Up += C * (Y * 2.f / Steps);
	}
	// RenderSettings.ambientIntensity of the Studio scene (level2).
	const float Intensity = 1.5f;
	Constant *= Intensity;
	Up *= Intensity;
	// Unity space: y is up.
	OutSHAr = FLinearColor(0.f, Up.R, 0.f, Constant.R);
	OutSHAg = FLinearColor(0.f, Up.G, 0.f, Constant.G);
	OutSHAb = FLinearColor(0.f, Up.B, 0.f, Constant.B);
	return true;
}

bool OKStudioMapSky(int32 Map, int32 SunType, int32& OutPattern)
{
	OutPattern = -1;
	const TSharedPtr<FJsonObject> Catalog = LoadCatalog();
	const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
	if (Map < 0 || !Catalog.IsValid() || !Catalog->TryGetArrayField(TEXT("maps"), Maps)) return false;
	for (const TSharedPtr<FJsonValue>& V : *Maps)
	{
		const TSharedPtr<FJsonObject> M = V->AsObject();
		if (!M.IsValid() || int32(M->GetNumberField(TEXT("map"))) != Map) continue;
		// Map.RangeTimeZone: the scene's time when the map's SunLightInfo has it, else its first.
		TArray<int32> Types;
		for (const TSharedPtr<FJsonValue>& T : M->GetArrayField(TEXT("sun_types"))) Types.Add(int32(T->AsNumber()));
		const int32 Type = Types.Contains(SunType) ? SunType : (Types.Num() ? Types[0] : 0);
		// MapSkyInfo.Change(type): _infos[type]'s profile. Past the end, or without a profile, the controller keeps the
		// one it has (its own; the library has the infos' only): the first one there is.
		const TArray<TSharedPtr<FJsonValue>>& Skies = M->GetArrayField(TEXT("skies"));
		auto PatternAt = [&Skies](int32 I) { return Skies.IsValidIndex(I) ? int32(Skies[I]->AsObject()->GetNumberField(TEXT("pattern"))) : -1; };
		OutPattern = PatternAt(Type);
		for (int32 I = 0; OutPattern < 0 && I < Skies.Num(); ++I) OutPattern = PatternAt(I);
		return true;
	}
	return false;
}

bool OKStudioSkyBodyDirection(int32 No, const FString& Body, FVector& OutDirection)
{
	const TSharedPtr<FJsonObject> Pattern = FindPattern(No);
	const TSharedPtr<FJsonObject>* Colors = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* D = nullptr;
	if (!Pattern.IsValid() || !Pattern->TryGetObjectField(TEXT("colors"), Colors) || !(*Colors)->TryGetArrayField(Body, D) || D->Num() < 3) return false;
	// Unity (x, y, z) → UE (-x, z, y).
	OutDirection = FVector(-(*D)[0]->AsNumber(), (*D)[2]->AsNumber(), (*D)[1]->AsNumber()).GetSafeNormal();
	return !OutDirection.IsNearlyZero();
}

UMaterialInterface* OKStudioSkyMaterial(int32 No, FString& OutError)
{
#if WITH_EDITORONLY_DATA
	static TMap<int32, TStrongObjectPtr<UMaterial>> Built;
	if (const TStrongObjectPtr<UMaterial>* Hit = Built.Find(No)) return Hit->Get();
	const TSharedPtr<FJsonObject> Pattern = FindPattern(No);
	if (!Pattern.IsValid()) { OutError = FString::Printf(TEXT("sky pattern %d not in the library"), No); return nullptr; }
	TSet<FString> Keywords;
	for (const TSharedPtr<FJsonValue>& K : Pattern->GetArrayField(TEXT("keywords"))) Keywords.Add(K->AsString());
	const FOKSkyProgram* Program = PickProgram(Keywords);
	if (!Program) { OutError = TEXT("no sky programs"); return nullptr; }
	const TSharedPtr<FJsonObject> Floats = Pattern->GetObjectField(TEXT("floats"));
	const TSharedPtr<FJsonObject> Colors = Pattern->GetObjectField(TEXT("colors"));
	const TSharedPtr<FJsonObject> Textures = Pattern->GetObjectField(TEXT("textures"));
	const TSharedPtr<FJsonObject> Matrices = Pattern->GetObjectField(TEXT("matrices"));

	UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	Mat->MaterialDomain = MD_Surface;
	Mat->BlendMode = BLEND_Opaque;
	Mat->SetShadingModel(MSM_Unlit);
	// Seen from inside the dome.
	Mat->TwoSided = true;
	Mat->SetUsageByFlag(MATUSAGE_StaticMesh, true);
	auto Add = [Mat](UMaterialExpression* E) { E->Material = Mat; Mat->GetExpressionCollection().AddExpression(E); return E; };
	auto* Code = NewObject<UMaterialExpressionCustom>(Mat);
	Code->OutputType = CMOT_Float4;
	Code->Inputs.Reset();
	auto Input = [&](const FString& Name, UMaterialExpression* E) { FCustomInput& I = Code->Inputs.AddDefaulted_GetRef(); I.InputName = *Name; I.Input.Expression = E; };
	// The view direction from the camera: the dome is only where it is drawn.
	Input(TEXT("Toward"), Add(NewObject<UMaterialExpressionCameraVectorWS>(Mat)));
	Input(TEXT("FrameTime"), Add(NewObject<UMaterialExpressionTime>(Mat)));

	FString Hlsl = TEXT("const float UVFlip=1.0;\n");
	// Unity's skybox vertex: the direction on the unit sphere (OK Studio's Unity axes: UE (-x, z, y)).
	Hlsl += TEXT("float3 D=-Toward;\nfloat4 v0=float4(-D.x,D.z,D.y,1.0);\n");
	auto Buffers = [&](const TCHAR* Prefix, const FOKSkyBuffer* List, int32 Num)
	{
		for (int32 I = 0; I < Num; ++I)
		{
			Hlsl += FString::Printf(TEXT("float4 %s%d[%d];\n"), Prefix, List[I].Slot, List[I].Size);
			for (int32 J = 0; J < List[I].Size; ++J) Hlsl += FString::Printf(TEXT("%s%d[%d]=0.0;\n"), Prefix, List[I].Slot, J);
		}
	};
	auto Bind = [&](const TCHAR* Prefix, const FOKSkyUniform& U)
	{
		if (U.Slot < 0) return;
		const FString Name = U.Name;
		if (U.Dim == 16)
		{
			// Matrices: identity for the object / view-projection (the dome is drawn by direction only); the moon and
			// sun matrices from the library (column-major, as Unity uploads them).
			TArray<float> M = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (Matrices.IsValid() && Matrices->TryGetArrayField(Name, Values) && Values->Num() == 16)
				for (int32 K = 0; K < 16; ++K) M[K] = float((*Values)[K]->AsNumber());
			for (int32 Column = 0; Column < 4; ++Column)
				Hlsl += FString::Printf(TEXT("%s%d[%d]=float4(%s,%s,%s,%s);\n"), Prefix, U.Slot, U.Offset / 16 + Column,
					*Bits(M[Column * 4]), *Bits(M[Column * 4 + 1]), *Bits(M[Column * 4 + 2]), *Bits(M[Column * 4 + 3]));
			return;
		}
		FLinearColor V(0, 0, 0, 0);
		bool bKnown = true;
		const TArray<TSharedPtr<FJsonValue>>* C = nullptr;
		double F = 0;
		if (Name == TEXT("_Time")) bKnown = true;
		else if (Name.EndsWith(TEXT("_ST")))
		{
			V = FLinearColor(1, 1, 0, 0);
			const TSharedPtr<FJsonObject>* T = nullptr;
			if (Textures.IsValid() && Textures->TryGetObjectField(Name.LeftChop(3), T))
			{
				const TArray<TSharedPtr<FJsonValue>>& St = (*T)->GetArrayField(TEXT("st"));
				if (St.Num() == 4) V = FLinearColor(St[0]->AsNumber(), St[1]->AsNumber(), St[2]->AsNumber(), St[3]->AsNumber());
			}
		}
		else if (Colors.IsValid() && Colors->TryGetArrayField(Name, C) && C->Num() == 4)
			V = FLinearColor((*C)[0]->AsNumber(), (*C)[1]->AsNumber(), (*C)[2]->AsNumber(), (*C)[3]->AsNumber());
		else if (Floats.IsValid() && Floats->TryGetNumberField(Name, F)) V = FLinearColor(float(F), 0, 0, 0);
		else bKnown = false;
		if (!bKnown) UE_LOG(LogTemp, Warning, TEXT("OK studio sky %d: no value for %s (0)"), No, *Name);
		// Studio renders in linear space: colour properties and [Gamma] values are linearised on upload.
		if (U.Type == 0 || U.bGamma) { V.R = ToLinear(V.R); V.G = ToLinear(V.G); V.B = ToLinear(V.B); }
		for (int32 Component = 0; Component < U.Dim; ++Component)
		{
			FString Expr = Bits((&V.R)[Component]);
			if (Name == TEXT("_Time")) Expr = Component == 0 ? TEXT("FrameTime*0.05") : Component == 1 ? TEXT("FrameTime") : Component == 2 ? TEXT("FrameTime*2.0") : TEXT("FrameTime*3.0");
			const int32 Word = U.Offset / 4 + Component;
			Hlsl += FString::Printf(TEXT("%s%d[%d].%c=%s;\n"), Prefix, U.Slot, Word / 4, TEXT("xyzw")[Word % 4], *Expr);
		}
	};
	Buffers(TEXT("vcb"), Program->VertexBuffers, Program->NumVertexBuffers);
	Buffers(TEXT("cb"), Program->PixelBuffers, Program->NumPixelBuffers);
	for (int32 I = 0; I < Program->NumVertexUniforms; ++I) Bind(TEXT("vcb"), Program->VertexUniforms[I]);
	for (int32 I = 0; I < Program->NumPixelUniforms; ++I) Bind(TEXT("cb"), Program->PixelUniforms[I]);

	FString Pixel = Program->Pixel;
	for (int32 I = 0; I < Program->NumTextures; ++I)
	{
		const FOKSkyTexture& T = Program->Textures[I];
		UTexture2D* Texture = nullptr;
		const TSharedPtr<FJsonObject>* Info = nullptr;
		if (Textures.IsValid() && Textures->TryGetObjectField(T.Name, Info))
			Texture = LoadTexture(AOKSceneActor::LibraryDirectory() / (*Info)->GetStringField(TEXT("file")), (*Info)->GetBoolField(TEXT("srgb")),
				(*Info)->GetBoolField(TEXT("point")), (*Info)->GetBoolField(TEXT("clamp")));
		if (!Texture) { OutError = FString::Printf(TEXT("sky pattern %d: texture %s missing"), No, T.Name); return nullptr; }
		auto* Object = NewObject<UMaterialExpressionTextureObject>(Mat);
		Object->Texture = Texture;
		Object->SamplerType = Texture->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
		Input(FString::Printf(TEXT("Texture%d"), T.Index), Add(Object));
		// Each texture with its own sampler state (filter and wrap of its Unity import settings).
		Pixel.ReplaceInline(*FString::Printf(TEXT("Sampler%d,"), T.Sampler), *FString::Printf(TEXT("Texture%dSampler,"), T.Index));
	}
	Hlsl += Program->Vertex;
	Hlsl += Program->Glue;
	Hlsl += TEXT("\n{\n") + Pixel + TEXT("}\n");
	Code->Code = Hlsl;
	Add(Code);
	if (UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData())
	{
		Ed->EmissiveColor.Expression = Code;
		Ed->EmissiveColor.Mask = 1;
		Ed->EmissiveColor.MaskR = Ed->EmissiveColor.MaskG = Ed->EmissiveColor.MaskB = 1;
		Ed->EmissiveColor.MaskA = 0;
	}
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	Built.Add(No, TStrongObjectPtr<UMaterial>(Mat));
	UE_LOG(LogTemp, Display, TEXT("OK studio sky %d (%s): program [%s] for keywords [%s], %d textures"), No, *Pattern->GetStringField(TEXT("name")),
		Program->Keywords, *FString::Join(Keywords.Array(), TEXT(" ")), Program->NumTextures);
	return Mat;
#else
	OutError = TEXT("the sky material is built in editor builds only");
	return nullptr;
#endif
}

FLinearColor OKStudioLinearLightColor(const FLinearColor& Color,float Intensity)
{
    const auto Linear=[](float V){return V<=.04045f?V/12.92f:V<1.f?FMath::Pow((V+.055f)/1.055f,2.4f):FMath::Pow(V,2.2f);};
    return FLinearColor(Linear(Color.R*Intensity),Linear(Color.G*Intensity),Linear(Color.B*Intensity),Color.A*Intensity);
}
