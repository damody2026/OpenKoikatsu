#include "OKLibraryBrowser.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "OKLibraryBrowser"

namespace
{
	FString LibraryRoot() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OKAnim"))); }

	/** "...Base Layer.A_SLoop" -> "A_SLoop". */
	FString StateName(const FString& Key)
	{
		FString Name = Key;
		int32 I;
		if (Name.FindLastChar(TEXT('|'), I)) Name = Name.Mid(I + 1);
		if (Name.FindLastChar(TEXT('.'), I)) Name = Name.Mid(I + 1);
		return Name.TrimStartAndEnd();
	}

	TSharedPtr<FOKLibraryEntry> MakeEntry(FOKLibraryEntry::EKind Kind, const FString& Name, const FString& Key, const FString& File, float Duration)
	{
		TSharedPtr<FOKLibraryEntry> E = MakeShared<FOKLibraryEntry>();
		E->Kind = Kind;
		E->Name = Name;
		E->Key = Key;
		E->File = File;
		E->Duration = Duration;
		E->Search = (Name + TEXT(" ") + Key + TEXT(" ") + FOKLibraryEntry::KindName(Kind)).ToLower();
		return E;
	}
}

void FOKLibrary::Load()
{
	Entries.Reset();
	const FString Root = LibraryRoot();
	for (const FOKLibraryEntry::EKind Kind : { FOKLibraryEntry::EKind::Female, FOKLibraryEntry::EKind::Male })
	{
		const FString Dir = FPaths::Combine(Root, TEXT("KK"), FOKLibraryEntry::KindName(Kind));
		FString Text;
		TSharedPtr<FJsonObject> Catalog;
		TSet<FString> Listed;
		if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir, TEXT("catalog.json"))) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Catalog) && Catalog)
		{
			for (const TSharedPtr<FJsonValue>& V : Catalog->GetArrayField(TEXT("clips")))
			{
				const TSharedPtr<FJsonObject> C = V->AsObject();
				if (!C) continue;
				const FString File = FPaths::Combine(Dir, C->GetStringField(TEXT("file")));
				Listed.Add(FPaths::ConvertRelativePathToFull(File));
				const FString Key = C->GetStringField(TEXT("key"));
				Entries.Add(MakeEntry(Kind, StateName(Key), Key, File, float(C->HasField(TEXT("duration")) ? C->GetNumberField(TEXT("duration")) : 0.)));
			}
		}
		// Files a running (or cancelled) batch wrote before its catalog: listed by file name.
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Dir, TEXT("*.okanim"), true, false);
		for (const FString& F : Files)
		{
			const FString Full = FPaths::ConvertRelativePathToFull(F);
			if (Listed.Contains(Full)) continue;
			FString Base = FPaths::GetBaseFilename(F);
			int32 I;
			if (Base.FindLastChar(TEXT('_'), I)) Base = Base.Left(I);   // <state>_<crc>
			Entries.Add(MakeEntry(Kind, Base, FString(), Full, 0.f));
		}
	}
	// Paired groups: one JSON object per line (OKAnim.SavePair).
	const FString PairDir = FPaths::Combine(Root, TEXT("Pairs"));
	TArray<FString> Lines;
	TSet<FString> Seen;
	if (FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(PairDir, TEXT("catalog.jsonl"))))
	{
		for (const FString& Line : Lines)
		{
			TSharedPtr<FJsonObject> J;
			if (Line.IsEmpty() || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), J) || !J) continue;
			const FString File = FPaths::Combine(PairDir, J->GetStringField(TEXT("file")));
			if (Seen.Contains(File) || !FPaths::FileExists(File)) continue;
			Seen.Add(File);
			const FString Female = J->GetStringField(TEXT("female"));
			FString Label = J->GetStringField(TEXT("label"));
			const FString State = J->GetStringField(TEXT("state"));
			TSharedPtr<FOKLibraryEntry> E = MakeEntry(FOKLibraryEntry::EKind::Pair, Label.IsEmpty() ? State : Label + TEXT(" · ") + State, Female, File, 0.f);
			E->Search += TEXT(" ") + (J->GetStringField(TEXT("female_motion")) + TEXT(" ") + J->GetStringField(TEXT("male_motion"))).ToLower();
			Entries.Add(E);
		}
	}
	Entries.Sort([](const TSharedPtr<FOKLibraryEntry>& A, const TSharedPtr<FOKLibraryEntry>& B) { return A->Kind != B->Kind ? A->Kind < B->Kind : A->Name < B->Name; });
}

TArray<TSharedPtr<FOKLibraryEntry>> FOKLibrary::Find(const FString& Filter, TOptional<FOKLibraryEntry::EKind> Kind) const
{
	TArray<FString> Words;
	Filter.ToLower().ParseIntoArrayWS(Words);
	return Entries.FilterByPredicate([&](const TSharedPtr<FOKLibraryEntry>& E)
	{
		if (Kind.IsSet() && E->Kind != Kind.GetValue()) return false;
		for (const FString& W : Words) if (!E->Search.Contains(W)) return false;
		return true;
	});
}

void SOKLibraryBrowser::Construct(const FArguments& InArgs)
{
	Library = InArgs._Library;
	OnPlay = InArgs._OnPlay;
	OnClose = InArgs._OnClose;
	Filter = InArgs._InitialFilter;
	auto KindButton = [this](const FText& Label, TOptional<FOKLibraryEntry::EKind> K)
	{
		return SNew(SButton).Text(Label).OnClicked_Lambda([this, K]() { Kind = K; Refresh(); return FReply::Handled(); });
	};
	ChildSlot
	[
		SNew(SBox).WidthOverride(520.f).HeightOverride(620.f)
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.85f)).Padding(8.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("Title", "OK 動作庫"))]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("Reload", "重新整理")).OnClicked_Lambda([this]() { if (Library) Library->Load(); Refresh(); return FReply::Handled(); })]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("Close", "關閉")).OnClicked_Lambda([this]() { OnClose.ExecuteIfBound(); return FReply::Handled(); })]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
				[
					SNew(SEditableTextBox).Text(FText::FromString(Filter)).HintText(LOCTEXT("Hint", "搜尋名稱、狀態、來源…（空白分隔多個關鍵字）"))
						.OnTextChanged_Lambda([this](const FText& T) { Filter = T.ToString(); Refresh(); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[KindButton(LOCTEXT("All", "全部"), {})]
					+ SHorizontalBox::Slot().AutoWidth()[KindButton(LOCTEXT("Female", "女"), FOKLibraryEntry::EKind::Female)]
					+ SHorizontalBox::Slot().AutoWidth()[KindButton(LOCTEXT("Male", "男"), FOKLibraryEntry::EKind::Male)]
					+ SHorizontalBox::Slot().AutoWidth()[KindButton(LOCTEXT("Pair", "雙人"), FOKLibraryEntry::EKind::Pair)]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8.f, 0.f)[SNew(STextBlock).Text_Lambda([this]() { return Status; })]
				]
				+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 4.f)
				[
					SAssignNew(List, SListView<TSharedPtr<FOKLibraryEntry>>)
						.ListItemsSource(&Shown)
						.OnGenerateRow(this, &SOKLibraryBrowser::MakeRow)
						.SelectionMode(ESelectionMode::Single)
						.OnMouseButtonDoubleClick_Lambda([this](TSharedPtr<FOKLibraryEntry> E) { if (E) OnPlay.ExecuteIfBound(*E); })
				]
			]
		]
	];
	Refresh();
}

void SOKLibraryBrowser::SetFilter(const FString& InFilter)
{
	Filter = InFilter;
	Refresh();
}

void SOKLibraryBrowser::Refresh()
{
	// A large library: show the first matches only (the count says how many there are).
	static constexpr int32 MaxShown = 500;
	TArray<TSharedPtr<FOKLibraryEntry>> All = Library ? Library->Find(Filter, Kind) : TArray<TSharedPtr<FOKLibraryEntry>>();
	Status = FText::Format(LOCTEXT("Count", "{0} 筆{1}"), All.Num(), All.Num() > MaxShown ? FText::Format(LOCTEXT("Shown", "（顯示前 {0}）"), MaxShown) : FText::GetEmpty());
	if (All.Num() > MaxShown) All.SetNum(MaxShown);
	Shown = MoveTemp(All);
	if (List) List->RequestListRefresh();
}

TSharedRef<ITableRow> SOKLibraryBrowser::MakeRow(TSharedPtr<FOKLibraryEntry> Item, const TSharedRef<STableViewBase>& Owner)
{
	const FString Text = FString::Printf(TEXT("[%s] %s%s"), FOKLibraryEntry::KindName(Item->Kind), *Item->Name,
		Item->Duration > 0.f ? *FString::Printf(TEXT("  %.2fs"), Item->Duration) : TEXT(""));
	return SNew(STableRow<TSharedPtr<FOKLibraryEntry>>, Owner)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Text)).ToolTipText(FText::FromString(Item->Key.IsEmpty() ? Item->File : Item->Key))]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("Play", "播放")).OnClicked_Lambda([this, Item]() { OnPlay.ExecuteIfBound(*Item); return FReply::Handled(); })]
	];
}

#undef LOCTEXT_NAMESPACE
