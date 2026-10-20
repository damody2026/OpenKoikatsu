#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

/** One converted OK animation on disk (Saved/OKAnim): single clips per sex and paired groups. */
struct FOKLibraryEntry
{
	enum class EKind : uint8 { Female, Male, Pair };
	EKind Kind = EKind::Female;
	FString Name;       // state / clip name
	FString Key;        // KK clip key (or the pair's female clip)
	FString File;       // absolute path of the .okanim / .okgroup
	float Duration = 0.f;
	FString Search;     // lower-case text the filter matches

	static const TCHAR* KindName(EKind K) { return K == EKind::Female ? TEXT("female") : K == EKind::Male ? TEXT("male") : TEXT("pair"); }
};

/** The OK animation library: reads the batch catalogs (or the files when a catalog is missing). */
struct FOKLibrary
{
	TArray<TSharedPtr<FOKLibraryEntry>> Entries;
	void Load();
	/** Entries matching every space-separated word of Filter (name, key, kind); Kind filters when set. */
	TArray<TSharedPtr<FOKLibraryEntry>> Find(const FString& Filter, TOptional<FOKLibraryEntry::EKind> Kind = {}) const;
};

/** In-game browser: search box, kind buttons, list; clicking an entry plays it on the current characters. */
class SOKLibraryBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOKLibraryBrowser) {}
		SLATE_ARGUMENT(TSharedPtr<FOKLibrary>, Library)
		SLATE_ARGUMENT(FString, InitialFilter)
		SLATE_EVENT(TDelegate<void(const FOKLibraryEntry&)>, OnPlay)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetFilter(const FString& Filter);

private:
	void Refresh();
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FOKLibraryEntry> Item, const TSharedRef<STableViewBase>& Owner);

	TSharedPtr<FOKLibrary> Library;
	TDelegate<void(const FOKLibraryEntry&)> OnPlay;
	FSimpleDelegate OnClose;
	FString Filter;
	TOptional<FOKLibraryEntry::EKind> Kind;
	TArray<TSharedPtr<FOKLibraryEntry>> Shown;
	TSharedPtr<SListView<TSharedPtr<FOKLibraryEntry>>> List;
	FText Status;
};
