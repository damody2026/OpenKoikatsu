#include "OKStudioImportSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "XmlFile.h"

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
