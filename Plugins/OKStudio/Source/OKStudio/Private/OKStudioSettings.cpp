#include "OKStudioSettings.h"
#include "Input/Events.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FOKStudioSettings& FOKStudioSettings::Get()
{
	static FOKStudioSettings Settings;
	if (!Settings.bLoaded) Settings.Load();
	return Settings;
}

FString FOKStudioSettings::Directory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OKStudio"));
}

const TArray<TPair<FString, FString>>& FOKStudioSettings::DefaultBindings()
{
	static const TArray<TPair<FString, FString>> Defaults = []
	{
		TArray<TPair<FString, FString>> D = {
			{ TEXT("edit.undo"), TEXT("Ctrl+Z") }, { TEXT("edit.redo"), TEXT("Ctrl+Y, Ctrl+Shift+Z") },
			{ TEXT("edit.duplicate"), TEXT("Ctrl+D") }, { TEXT("edit.copy"), TEXT("Ctrl+C") }, { TEXT("edit.paste"), TEXT("Ctrl+V") },
			{ TEXT("edit.delete"), TEXT("Delete") }, { TEXT("edit.rename"), TEXT("F2") }, { TEXT("edit.hide"), TEXT("H") },
			{ TEXT("transform.copy"), TEXT("Ctrl+Shift+C") }, { TEXT("transform.paste"), TEXT("Ctrl+Shift+V") },
			{ TEXT("scene.save"), TEXT("Ctrl+S") }, { TEXT("scene.saveas"), TEXT("Ctrl+Shift+S") }, { TEXT("scene.browser"), TEXT("Ctrl+O") },
			{ TEXT("gizmo.move"), TEXT("W") }, { TEXT("gizmo.rotate"), TEXT("E") }, { TEXT("gizmo.scale"), TEXT("R") },
			{ TEXT("gizmo.local"), TEXT("L") }, { TEXT("gizmo.snap"), TEXT("Ctrl+Shift+G") }, { TEXT("edit.group"), TEXT("Ctrl+G") }, { TEXT("edit.ungroup"), TEXT("Ctrl+Shift+U") }, { TEXT("edit.duplicateoffset"), TEXT("Ctrl+Shift+D") }, { TEXT("edit.solo"), TEXT("Shift+H") },
			{ TEXT("toggle.ui"), TEXT("Z") }, { TEXT("app.settings"), TEXT("F1") }, { TEXT("app.screenshot"), TEXT("F12") },
			{ TEXT("app.quicksearch"), TEXT("Ctrl+F") }, { TEXT("app.poselibrary"), TEXT("Ctrl+P") },
			{ TEXT("camera.focus"), TEXT("F") }, { TEXT("camera.reset"), TEXT("Ctrl+SpaceBar") },
			{ TEXT("camera.front"), TEXT("NumPadOne") }, { TEXT("camera.side"), TEXT("NumPadThree") }, { TEXT("camera.top"), TEXT("NumPadSeven") },
			{ TEXT("camera.next"), TEXT("C") }, { TEXT("camera.fly"), TEXT("Ctrl+Shift+F") },
			{ TEXT("timeline.key"), TEXT("K") }, { TEXT("timeline.prev"), TEXT("Comma") }, { TEXT("timeline.next"), TEXT("Period") },
			{ TEXT("timeline.first"), TEXT("Home") }, { TEXT("timeline.last"), TEXT("End") }, { TEXT("timeline.play"), TEXT("SpaceBar") },
			{ TEXT("pose.mirror"), TEXT("Ctrl+M") },
		};
		static const TCHAR* Digits[] = { TEXT("Zero"), TEXT("One"), TEXT("Two"), TEXT("Three"), TEXT("Four"), TEXT("Five"), TEXT("Six"), TEXT("Seven"), TEXT("Eight"), TEXT("Nine") };
		for (int32 N = 0; N < 10; ++N)
		{
			D.Add({ FString::Printf(TEXT("view.save.%d"), N), FString(TEXT("Ctrl+")) + Digits[N] });
			D.Add({ FString::Printf(TEXT("view.load.%d"), N), Digits[N] });
		}
		return D;
	}();
	return Defaults;
}

FString FOKStudioSettings::BindingOf(const FString& Id) const
{
	if (const FString* B = Bindings.Find(Id)) return *B;
	for (const TPair<FString, FString>& D : DefaultBindings()) if (D.Key == Id) return D.Value;
	return FString();
}

FString FOKStudioSettings::ChordText(const FKeyEvent& Event)
{
	FString S;
	if (Event.IsControlDown()) S += TEXT("Ctrl+");
	if (Event.IsShiftDown()) S += TEXT("Shift+");
	if (Event.IsAltDown()) S += TEXT("Alt+");
	return S + Event.GetKey().GetFName().ToString();
}

FString FOKStudioSettings::CommandFor(const FKeyEvent& Event) const
{
	const FString Chord = ChordText(Event);
	TSet<FString> Seen;
	auto Matches = [&Chord](const FString& List)
	{
		TArray<FString> Parts;
		List.ParseIntoArray(Parts, TEXT(","));
		for (FString& P : Parts) if (P.TrimStartAndEnd().Equals(Chord, ESearchCase::IgnoreCase)) return true;
		return false;
	};
	for (const TPair<FString, FString>& B : Bindings) { Seen.Add(B.Key); if (Matches(B.Value)) return B.Key; }
	for (const TPair<FString, FString>& D : DefaultBindings()) if (!Seen.Contains(D.Key) && Matches(D.Value)) return D.Key;
	return FString();
}

TArray<FString> FOKStudioSettings::Conflicts() const
{
	TMap<FString, TArray<FString>> ByChord;
	for (const TPair<FString, FString>& D : DefaultBindings())
	{
		TArray<FString> Parts;
		BindingOf(D.Key).ParseIntoArray(Parts, TEXT(","));
		for (FString& P : Parts) { P.TrimStartAndEndInline(); if (!P.IsEmpty()) ByChord.FindOrAdd(P.ToLower()).AddUnique(D.Key); }
	}
	TArray<FString> Out;
	for (const TPair<FString, TArray<FString>>& C : ByChord) if (C.Value.Num() > 1) Out.Add(C.Key + TEXT(": ") + FString::Join(C.Value, TEXT(", ")));
	return Out;
}

void FOKStudioSettings::UseRecent(const FString& Key)
{
	Recent.Remove(Key);
	Recent.Insert(Key, 0);
	if (Recent.Num() > 40) Recent.SetNum(40);
	SaveFavorites();
}

void FOKStudioSettings::UseColor(const FLinearColor& Color)
{
	RecentColors.RemoveAll([&Color](const FLinearColor& C) { return C.Equals(Color, 1e-3f); });
	RecentColors.Insert(Color, 0);
	if (RecentColors.Num() > 16) RecentColors.SetNum(16);
	Save();
}

void FOKStudioSettings::Reset()
{
	const TSet<FString> F = Favorites, H = Hidden;
	const TArray<FString> R = Recent;
	const TArray<FLinearColor> C = RecentColors;
	*this = FOKStudioSettings();
	bLoaded = true;
	Favorites = F; Hidden = H; Recent = R; RecentColors = C;
	Save();
}

void FOKStudioSettings::Load()
{
	bLoaded = true;
	FString Text;
	TSharedPtr<FJsonObject> J;
	if (FFileHelper::LoadFileToString(Text, *(Directory() / TEXT("settings.json"))) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) && J)
	{
		auto Num = [&J](const TCHAR* Name, float& V) { double D; if (J->TryGetNumberField(Name, D)) V = float(D); };
		Num(TEXT("orbit_speed"), OrbitSpeed); Num(TEXT("pan_speed"), PanSpeed); Num(TEXT("zoom_speed"), ZoomSpeed); Num(TEXT("fly_speed"), FlySpeed);
		Num(TEXT("gizmo_scale"), GizmoScale); Num(TEXT("snap_move"), SnapMove); Num(TEXT("snap_rotate"), SnapRotate); Num(TEXT("snap_scale"), SnapScale);
		Num(TEXT("ui_scale"), UiScale);
		J->TryGetBoolField(TEXT("fly_mode"), bFlyMode);
		double D;
		if (J->TryGetNumberField(TEXT("backup_minutes"), D)) BackupMinutes = int32(D);
		if (J->TryGetNumberField(TEXT("backup_keep"), D)) BackupKeep = FMath::Max(1, int32(D));
		J->TryGetStringArrayField(TEXT("card_roots"), CardRoots);
		const TSharedPtr<FJsonObject>* B = nullptr;
		if (J->TryGetObjectField(TEXT("bindings"), B)) for (const auto& P : (*B)->Values) Bindings.Add(FString(*P.Key), P.Value->AsString());
		const TArray<TSharedPtr<FJsonValue>>* Colors = nullptr;
		if (J->TryGetArrayField(TEXT("recent_colors"), Colors))
			for (const TSharedPtr<FJsonValue>& V : *Colors) RecentColors.Add(FLinearColor(FColor::FromHex(V->AsString())));
	}
	if (FFileHelper::LoadFileToString(Text, *(Directory() / TEXT("favorites.json"))) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) && J)
	{
		TArray<FString> L;
		if (J->TryGetStringArrayField(TEXT("favorites"), L)) Favorites.Append(L);
		if (J->TryGetStringArrayField(TEXT("hidden"), L)) Hidden.Append(L);
		J->TryGetStringArrayField(TEXT("recent"), Recent);
	}
}

void FOKStudioSettings::Save() const
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("orbit_speed"), OrbitSpeed); J->SetNumberField(TEXT("pan_speed"), PanSpeed); J->SetNumberField(TEXT("zoom_speed"), ZoomSpeed);
	J->SetNumberField(TEXT("fly_speed"), FlySpeed); J->SetBoolField(TEXT("fly_mode"), bFlyMode);
	J->SetNumberField(TEXT("gizmo_scale"), GizmoScale); J->SetNumberField(TEXT("snap_move"), SnapMove); J->SetNumberField(TEXT("snap_rotate"), SnapRotate); J->SetNumberField(TEXT("snap_scale"), SnapScale);
	J->SetNumberField(TEXT("backup_minutes"), BackupMinutes); J->SetNumberField(TEXT("backup_keep"), BackupKeep);
	J->SetNumberField(TEXT("ui_scale"), UiScale);
	TArray<TSharedPtr<FJsonValue>> Roots;
	for (const FString& R : CardRoots) Roots.Add(MakeShared<FJsonValueString>(R));
	J->SetArrayField(TEXT("card_roots"), Roots);
	TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& P : Bindings) B->SetStringField(P.Key, P.Value);
	J->SetObjectField(TEXT("bindings"), B);
	TArray<TSharedPtr<FJsonValue>> Colors;
	for (const FLinearColor& C : RecentColors) Colors.Add(MakeShared<FJsonValueString>(C.ToFColor(false).ToHex()));
	J->SetArrayField(TEXT("recent_colors"), Colors);
	FString Out;
	FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Out));
	FFileHelper::SaveStringToFile(Out, *(Directory() / TEXT("settings.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FOKStudioSettings::SaveFavorites() const
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	auto List = [&J](const TCHAR* Name, const TArray<FString>& L) { TArray<TSharedPtr<FJsonValue>> A; for (const FString& S : L) A.Add(MakeShared<FJsonValueString>(S)); J->SetArrayField(Name, A); };
	List(TEXT("favorites"), Favorites.Array());
	List(TEXT("hidden"), Hidden.Array());
	List(TEXT("recent"), Recent);
	FString Out;
	FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Out));
	FFileHelper::SaveStringToFile(Out, *(Directory() / TEXT("favorites.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
