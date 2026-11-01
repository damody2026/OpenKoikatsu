#include "OKStudioPose.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "OKStudioCatalog.h"
#include "OKStudioSettings.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ok_extract.h"

int32 FOKStudioPose::FkPart(int32 Group)
{
	if (Group <= 4) return 3;
	if (Group == 5) return 4;
	if (Group == 6) return 5;
	if (Group <= 9) return 0;
	if (Group == 10) return 1;
	if (Group <= 12) return 2;
	return 6;
}

namespace
{
	TArray<TSharedPtr<FJsonValue>> Vec3(const FVector& V)
	{
		return { MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) };
	}
}

FString FOKStudioPose::Directory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UserData/Studio/pose"));
}

TArray<FString> FOKStudioPose::Files()
{
	// UserData/Studio/pose and the pose folders added in the settings (资源目录).
	TArray<FString> Folders = { Directory() };
	for (const FString& R : FOKStudioSettings::Get().PoseRoots) Folders.AddUnique(R);
	TArray<FString> Out;
	for (const FString& Dir : Folders)
	{
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(Dir / TEXT("*.dat")), true, false);
		for (const FString& N : Names) Out.AddUnique(Dir / N);
	}
	Out.Sort([](const FString& A, const FString& B) { return IFileManager::Get().GetTimeStamp(*A) > IFileManager::Get().GetTimeStamp(*B); });
	return Out;
}

bool FOKStudioPose::Load(const FString& Path, FOKSceneCharacter& Character, FString& OutError, FString* OutName)
{
	TArray<ANSICHAR> Buffer;
	Buffer.SetNumZeroed(1 << 20);
	if (ok_studio_pose_read(TCHAR_TO_UTF8(*Path), Buffer.GetData(), Buffer.Num()) != 0) { OutError = UTF8_TO_TCHAR(Buffer.GetData()); return false; }
	TSharedPtr<FJsonObject> Pose;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UTF8_TO_TCHAR(Buffer.GetData())), Pose) || !Pose) { OutError = TEXT("bad pose JSON"); return false; }
	if (OutName) Pose->TryGetStringField(TEXT("name"), *OutName);
	const FOKStudioCatalog& Catalog = FOKStudioCatalog::Get();
	if (Catalog.FkBones.IsEmpty()) { OutError = TEXT("the base library has no Studio FK bone list"); return false; }
	TArray<bool> Active;
	const TArray<TSharedPtr<FJsonValue>>* ActiveFk = nullptr;
	if (Pose->TryGetArrayField(TEXT("active_fk"), ActiveFk)) for (const TSharedPtr<FJsonValue>& V : *ActiveFk) Active.Add(V->AsBool());
	Character.Fk.Reset();
	if (!Pose->GetBoolField(TEXT("enable_fk"))) return true;
	const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
	if (!Pose->TryGetArrayField(TEXT("fk"), Bones)) return true;
	int32 Unknown = 0;
	for (const TSharedPtr<FJsonValue>& Entry : *Bones)
	{
		const TArray<TSharedPtr<FJsonValue>>& Pair = Entry->AsArray();
		if (Pair.Num() != 2) continue;
		const int32 No = int32(Pair[0]->AsNumber());
		const FOKStudioFkBone* Bone = Catalog.FkBones.FindByPredicate([No](const FOKStudioFkBone& B) { return B.No == No; });
		if (!Bone) { ++Unknown; continue; }
		const int32 Part = FkPart(Bone->Group);
		if (Active.IsValidIndex(Part) && !Active[Part]) continue;
		const TArray<TSharedPtr<FJsonValue>>& Rot = Pair[1]->AsObject()->GetArrayField(TEXT("rot"));
		if (Rot.Num() == 3) Character.Fk.Add({ Bone->Bone, FVector(Rot[0]->AsNumber(), Rot[1]->AsNumber(), Rot[2]->AsNumber()) });
	}
	if (Unknown) UE_LOG(LogTemp, Warning, TEXT("OK studio pose %s: %d FK bones not in the Studio bone list"), *Path, Unknown);
	return true;
}

bool FOKStudioPose::Save(const FString& Path, const FOKSceneCharacter& Character, const FString& Name, FString& OutError)
{
	const FOKStudioCatalog& Catalog = FOKStudioCatalog::Get();
	TSharedRef<FJsonObject> Pose = MakeShared<FJsonObject>();
	Pose->SetNumberField(TEXT("sex"), 1);
	Pose->SetStringField(TEXT("name"), Name);
	Pose->SetArrayField(TEXT("anime"), { MakeShared<FJsonValueNumber>(-1), MakeShared<FJsonValueNumber>(-1), MakeShared<FJsonValueNumber>(-1) });
	Pose->SetNumberField(TEXT("normalized_time"), 0);
	Pose->SetBoolField(TEXT("enable_ik"), false);
	TArray<TSharedPtr<FJsonValue>> ActiveIk;
	for (int32 I = 0; I < 5; ++I) ActiveIk.Add(MakeShared<FJsonValueBoolean>(true));
	Pose->SetArrayField(TEXT("active_ik"), ActiveIk);
	Pose->SetArrayField(TEXT("ik"), {});
	bool Active[7] = {};
	TArray<TSharedPtr<FJsonValue>> Bones;
	for (const FOKSceneFkBone& B : Character.Fk)
	{
		const FOKStudioFkBone* Info = Catalog.FindFkBone(B.Bone);
		if (!Info) { OutError = TEXT("not a Studio FK bone: ") + B.Bone; return false; }
		Active[FkPart(Info->Group)] = true;
		TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetArrayField(TEXT("pos"), Vec3(FVector::ZeroVector));
		Change->SetArrayField(TEXT("rot"), Vec3(B.Euler));
		Change->SetArrayField(TEXT("scale"), Vec3(FVector::OneVector));
		Bones.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(Info->No), MakeShared<FJsonValueObject>(Change) }));
	}
	Pose->SetBoolField(TEXT("enable_fk"), Character.Fk.Num() > 0);
	TArray<TSharedPtr<FJsonValue>> ActiveFk;
	for (bool A : Active) ActiveFk.Add(MakeShared<FJsonValueBoolean>(A));
	Pose->SetArrayField(TEXT("active_fk"), ActiveFk);
	Pose->SetArrayField(TEXT("fk"), Bones);
	TArray<TSharedPtr<FJsonValue>> Expression;
	for (int32 I = 0; I < 4; ++I) Expression.Add(MakeShared<FJsonValueBoolean>(true));
	Pose->SetArrayField(TEXT("expression"), Expression);
	FString Json;
	FJsonSerializer::Serialize(Pose, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
	ANSICHAR Error[1024] = {};
	if (ok_studio_pose_write(TCHAR_TO_UTF8(*Path), TCHAR_TO_UTF8(*Json), Error, UE_ARRAY_COUNT(Error)) != 0) { OutError = UTF8_TO_TCHAR(Error); return false; }
	return true;
}
