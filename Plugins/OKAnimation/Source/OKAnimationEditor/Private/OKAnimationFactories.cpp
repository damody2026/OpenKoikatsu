#include "OKAnimationFactories.h"
#include "Misc/PackageName.h"
#include "OKAnimationEditing.h"
#include "OKAnimationAssets.h"
#include "OKAnimationEditorToolkit.h"
#include "OKAnimationLibrary.h"
#include "KKOkAnimation.h"
#include "AssetTypeCategories.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "OKAnimationEditor"

UOKAnimationImportFactory::UOKAnimationImportFactory()
{
	SupportedClass = UOKAnimationAsset::StaticClass();
	bEditorImport = true;
	bText = false;
	bCreateNew = false;
	Formats.Add(TEXT("okanim;OK Animation"));
	Formats.Add(TEXT("okgroup;OK Group Animation"));
	Formats.Add(TEXT("bvh;BVH motion capture (as OK Animation)"));
}

UOKFbxImportFactory::UOKFbxImportFactory()
{
	SupportedClass = UOKAnimSequence::StaticClass();
	bEditorImport = true;
	bCreateNew = false;
	bText = false;
	Formats.Add(TEXT("fbx;FBX (as OK Animation)"));
	ImportPriority = DefaultImportPriority + 10;   // ahead of Unreal's FBX factory, when enabled
}

bool UOKFbxImportFactory::FactoryCanImport(const FString& Filename)
{
	return GetDefault<UOKAnimationSettings>()->bImportFbxAsOK && FPaths::GetExtension(Filename).Equals(TEXT("fbx"), ESearchCase::IgnoreCase);
}

UObject* UOKFbxImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	if (!GetDefault<UOKAnimationSettings>()->bImportFbxAsOK) return nullptr;
	FString Error;
	const TArray<UOKAnimSequence*> Made = OKAnimationEditing::ImportFbxAsOk(Filename, FPackageName::GetLongPackagePath(InParent->GetName()), nullptr, Error);
	if (Made.IsEmpty()) { Warn->Logf(ELogVerbosity::Error, TEXT("FBX as OK animation %s: %s"), *Filename, *Error); return nullptr; }
	return Made[0];
}

bool UOKAnimationImportFactory::FactoryCanImport(const FString& Filename)
{
	const FString Ext = FPaths::GetExtension(Filename).ToLower();
	return Ext == TEXT("okanim") || Ext == TEXT("okgroup") || Ext == TEXT("bvh");
}

UObject* UOKAnimationImportFactory::FactoryCreateBinary(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type,
	const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn)
{
	const TArray<uint8> Bytes(Buffer, int32(BufferEnd - Buffer));
	FString Error;
	if (FPaths::GetExtension(CurrentFilename).Equals(TEXT("bvh"), ESearchCase::IgnoreCase))
	{
		const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		if (UOKAnimSequence* Seq = UOKAnimationLibrary::ImportBvhText(FString(Text.Length(), Text.Get()), CurrentFilename, InParent, InName, Error))
		{
			Seq->SetFlags(Flags | RF_Transactional);
			return Seq;
		}
	}
	else if (Bytes.Num() >= 8 && FMemory::Memcmp(Bytes.GetData(), "OKGROUP1", 8) == 0)
	{
		UOKGroupAnimSequence* Group = NewObject<UOKGroupAnimSequence>(InParent, InName, Flags | RF_Transactional);
		// The role animations live inside the group's package.
		if (Group->SetFromPackage(Bytes, Group, Error)) return Group;
	}
	else
	{
		UOKAnimSequence* Seq = NewObject<UOKAnimSequence>(InParent, InName, Flags | RF_Transactional);
		if (Seq->SetData(Bytes, Error))
		{
			Seq->Meta.Add(TEXT("imported_from"), CurrentFilename);
			return Seq;
		}
	}
	Warn->Logf(ELogVerbosity::Error, TEXT("OK animation import %s: %s"), *CurrentFilename, *Error);
	return nullptr;
}

UOKAssetFactoryNew::UOKAssetFactoryNew()
{
	bCreateNew = true;
	bEditAfterNew = true;
	bEditorImport = false;
}

uint32 UOKAssetFactoryNew::GetMenuCategories() const { return EAssetTypeCategories::Animation; }

UObject* UOKAssetFactoryNew::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if (InClass == UOKAnimSequence::StaticClass())
	{
		FString Error;
		UOKAnimSequence* Seq = UOKAnimationLibrary::MakeRestAnimation(nullptr, 61, 30.f, InParent, InName, Error);
		if (!Seq) Warn->Logf(ELogVerbosity::Error, TEXT("New OK animation: %s"), *Error);
		return Seq;
	}
	UObject* Asset = NewObject<UObject>(InParent, InClass, InName, Flags | RF_Transactional);
	if (UOKSkeletonFamily* Family = Cast<UOKSkeletonFamily>(Asset))
	{
		if (const UOKSkeletonFamily* BuiltIn = UOKAnimationLibrary::GetDefaultFamily()) Family->RestHeaderJson = BuiltIn->RestHeaderJson;
	}
	else if (UOKRetargetProfile* Profile = Cast<UOKRetargetProfile>(Asset))
	{
		Profile->ApplyPreset(TEXT("UE5Mannequin"));
	}
	else if (UOKBlendSpace* Blend = Cast<UOKBlendSpace>(Asset))
	{
		Blend->Axes.Add(FOKBlendAxis());
	}
	return Asset;
}

UOKAnimSequenceFactoryNew::UOKAnimSequenceFactoryNew() { SupportedClass = UOKAnimSequence::StaticClass(); }
UOKGroupAnimSequenceFactoryNew::UOKGroupAnimSequenceFactoryNew() { SupportedClass = UOKGroupAnimSequence::StaticClass(); }
UOKBlendSpaceFactoryNew::UOKBlendSpaceFactoryNew() { SupportedClass = UOKBlendSpace::StaticClass(); }
UOKAnimMontageFactoryNew::UOKAnimMontageFactoryNew() { SupportedClass = UOKAnimMontage::StaticClass(); }
UOKSkeletonFamilyFactoryNew::UOKSkeletonFamilyFactoryNew() { SupportedClass = UOKSkeletonFamily::StaticClass(); }
UOKRetargetProfileFactoryNew::UOKRetargetProfileFactoryNew() { SupportedClass = UOKRetargetProfile::StaticClass(); }
UOKPoseLibraryFactoryNew::UOKPoseLibraryFactoryNew() { SupportedClass = UOKPoseLibrary::StaticClass(); }

TConstArrayView<FAssetCategoryPath> UAssetDefinition_OKBase::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { FAssetCategoryPath(EAssetCategoryPaths::Animation, LOCTEXT("OKCategory", "OK Animation")) };
	return Categories;
}

EAssetCommandResult UAssetDefinition_OKPlayable::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UOKAnimationAsset* Asset : OpenArgs.LoadObjects<UOKAnimationAsset>())
	{
		const TSharedRef<FOKAnimationEditor> Editor = MakeShared<FOKAnimationEditor>();
		Editor->InitEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Asset);
	}
	return EAssetCommandResult::Handled;
}

FText UAssetDefinition_OKAnimSequence::GetAssetDisplayName() const { return LOCTEXT("OKAnimSequence", "OK Animation Sequence"); }
TSoftClassPtr<UObject> UAssetDefinition_OKAnimSequence::GetAssetClass() const { return UOKAnimSequence::StaticClass(); }
FText UAssetDefinition_OKGroupAnimSequence::GetAssetDisplayName() const { return LOCTEXT("OKGroup", "OK Group Animation"); }
TSoftClassPtr<UObject> UAssetDefinition_OKGroupAnimSequence::GetAssetClass() const { return UOKGroupAnimSequence::StaticClass(); }
FText UAssetDefinition_OKBlendSpace::GetAssetDisplayName() const { return LOCTEXT("OKBlendSpace", "OK Blend Space"); }
TSoftClassPtr<UObject> UAssetDefinition_OKBlendSpace::GetAssetClass() const { return UOKBlendSpace::StaticClass(); }
FText UAssetDefinition_OKAnimMontage::GetAssetDisplayName() const { return LOCTEXT("OKMontage", "OK Animation Montage"); }
TSoftClassPtr<UObject> UAssetDefinition_OKAnimMontage::GetAssetClass() const { return UOKAnimMontage::StaticClass(); }
EAssetCommandResult UAssetDefinition_OKSkeletonFamily::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UOKSkeletonFamily* Family : OpenArgs.LoadObjects<UOKSkeletonFamily>())
	{
		// The family's rest as a (transient) one-second animation on its preview mesh; the Family tab edits the family itself.
		FString Error;
		UOKAnimSequence* Rest = UOKAnimationLibrary::MakeRestAnimation(Family, 31, 30.f, GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UOKAnimSequence::StaticClass(), FName(*(Family->GetName() + TEXT("_Rest")))), Error);
		if (!Rest) { UE_LOG(LogTemp, Error, TEXT("OK family %s: %s"), *Family->GetName(), *Error); continue; }
		const TSharedRef<FOKAnimationEditor> Editor = MakeShared<FOKAnimationEditor>();
		Editor->EditedFamily = Family;
		Editor->InitEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Rest);
	}
	return EAssetCommandResult::Handled;
}

FText UAssetDefinition_OKSkeletonFamily::GetAssetDisplayName() const { return LOCTEXT("OKFamily", "OK Skeleton Family"); }
TSoftClassPtr<UObject> UAssetDefinition_OKSkeletonFamily::GetAssetClass() const { return UOKSkeletonFamily::StaticClass(); }
FText UAssetDefinition_OKPoseLibrary::GetAssetDisplayName() const { return LOCTEXT("OKPoseLibrary", "OK Pose Library"); }
TSoftClassPtr<UObject> UAssetDefinition_OKPoseLibrary::GetAssetClass() const { return UOKPoseLibrary::StaticClass(); }
FText UAssetDefinition_OKRetargetProfile::GetAssetDisplayName() const { return LOCTEXT("OKProfile", "OK Retarget Profile"); }
TSoftClassPtr<UObject> UAssetDefinition_OKRetargetProfile::GetAssetClass() const { return UOKRetargetProfile::StaticClass(); }

#undef LOCTEXT_NAMESPACE
