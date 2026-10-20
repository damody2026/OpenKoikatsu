#include "KKGameRoot.h"
#include "Misc/Paths.h"

UKKGameRoot::UKKGameRoot()
{
	Root = TEXT("C:/portable/KoikatuSunshine");
}

UKKGameRoot* UKKGameRoot::Get()
{
	return GetMutableDefault<UKKGameRoot>();
}

FString UKKGameRoot::Abdata() const
{
	return FPaths::Combine(Root, TEXT("abdata"));
}

FString UKKGameRoot::Mods() const
{
	return FPaths::Combine(Root, TEXT("mods"));
}

FString UKKGameRoot::CharaFemale() const
{
	return FPaths::Combine(Root, TEXT("UserData"), TEXT("chara"), TEXT("female"));
}

FString UKKGameRoot::Coordinate() const
{
	return FPaths::Combine(Root, TEXT("UserData"), TEXT("coordinate"));
}
