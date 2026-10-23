#include "OKStudioImportSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "XmlFile.h"
#include "ImageUtils.h"
#include "ImageCore.h"

FLinearColor OKStudioClearColorFromSettings(const FString& SystemXml)
{
    if (SystemXml.IsEmpty()) return FLinearColor::Black;
    const FXmlFile Xml(SystemXml, EConstructMethod::ConstructFromBuffer);
    const FXmlNode* Root = Xml.IsValid() ? Xml.GetRootNode() : nullptr;
    const FXmlNode* Est = Root && Root->GetTag() == TEXT("System") ? Root->FindChildNode(TEXT("Est")) : nullptr;
    const FXmlNode* Color = Est ? Est->FindChildNode(TEXT("BackColor")) : nullptr;
    TArray<FString> Parts;
    if (Color) Color->GetContent().ParseIntoArray(Parts, TEXT(","), false);
    if (Parts.Num() != 4) return FLinearColor::Black;
    FLinearColor Result;
    for (int32 I = 0; I < 4; ++I)
        if (!LexTryParseString((&Result.R)[I], *Parts[I].TrimStartAndEnd()) || !FMath::IsFinite((&Result.R)[I]))
            return FLinearColor::Black;
    // Camera background uses its RGB regardless of the saved alpha.
    Result.A = 1.f;
    return Result;
}

FLinearColor OKStudioConfiguredClearColor()
{
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("OK_GAME_ROOT"));
    FString Xml;
    if (!Root.IsEmpty()) FFileHelper::LoadFileToString(Xml, *(Root / TEXT("UserData/config/system.xml")));
    return OKStudioClearColorFromSettings(Xml);
}

bool OKStudioClearColorFromPicture(const FString& Png, FLinearColor& OutColor)
{
    // A scene with no map, background picture or sky shows the main camera's clear colour (the author's own
    // EstData.BackColor, not saved in the scene) wherever nothing is drawn; its picture's corners then all hold it.
    FImage Image;
    if (!FImageUtils::LoadImage(*Png, Image)) return false;
    FImage Bgra;
    Image.CopyTo(Bgra, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
    const int32 W = Bgra.SizeX, H = Bgra.SizeY, Patch = 8;
    if (W < Patch * 4 || H < Patch * 4) return false;
    const TArrayView64<const FColor> Pixels = Bgra.AsBGRA8();
    FVector3f Corners[4];
    for (int32 C = 0; C < 4; ++C)
    {
        const int32 X0 = (C & 1) ? W - Patch : 0, Y0 = (C & 2) ? H - Patch : 0;
        FVector3f Sum(0), Min(255), Max(0);
        for (int32 Y = Y0; Y < Y0 + Patch; ++Y)
            for (int32 X = X0; X < X0 + Patch; ++X)
            {
                const FColor P = Pixels[int64(Y) * W + X];
                const FVector3f V(P.R, P.G, P.B);
                Sum += V; Min = FVector3f::Min(Min, V); Max = FVector3f::Max(Max, V);
            }
        // A patch with detail in it (an item, a gradient) is not a plain clear colour.
        if ((Max - Min).GetMax() > 6.f) return false;
        Corners[C] = Sum / float(Patch * Patch);
    }
    for (int32 C = 1; C < 4; ++C)
        if ((Corners[C] - Corners[0]).GetAbsMax() > 6.f) return false;
    const FVector3f Mean = (Corners[0] + Corners[1] + Corners[2] + Corners[3]) / 4.f;
    OutColor = FLinearColor(Mean.X / 255.f, Mean.Y / 255.f, Mean.Z / 255.f, 1.f);
    return true;
}
