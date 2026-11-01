// OK Studio panel: the card browser, item previews, quick search, the effect list and map browser, the colour window and
// colour fields, the scene settings (畫面設定) tabs and their presets.
#include "SOKStudioPanel.h"
#include "KKCardReader.h"
#include "Materials/MaterialInstance.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/SToolTip.h"

using namespace OKStudioStyle;

namespace
{
	/** Colours of the scene are sRGB numbers: shown on screen they are converted to linear. */
	FLinearColor Display(const FLinearColor& C) { return C.A < 0.f ? FLinearColor(0.3f, 0.3f, 0.3f) : FLinearColor(FLinearColor(C.R, C.G, C.B, 1.f).ToFColor(false)); }

	/** Saturation / value square of one hue, and the hue bar (a colour window's two pickers). */
	class SColorField : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SColorField) : _bHue(false) {}
			SLATE_ARGUMENT(bool, bHue)
			SLATE_ARGUMENT(TFunction<FLinearColor()>, GetHsv)
			SLATE_ARGUMENT(TFunction<void(const FVector2D&)>, OnPick)
		SLATE_END_ARGS()
		void Construct(const FArguments& Args) { bHue = Args._bHue; GetHsv = Args._GetHsv; OnPick = Args._OnPick; }
		virtual FVector2D ComputeDesiredSize(float) const override { return bHue ? FVector2D(16.f, 160.f) : FVector2D(160.f, 160.f); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
		{
			const FLinearColor Hsv = GetHsv ? GetHsv() : FLinearColor::White;
			const FVector2D Size = G.GetLocalSize();
			auto Show = [](const FLinearColor& Srgb) { return FLinearColor(Srgb.ToFColor(false)); };
			if (bHue)
			{
				TArray<FSlateGradientStop> Stops;
				for (int32 I = 0; I <= 6; ++I) Stops.Add(FSlateGradientStop(FVector2D(0.f, Size.Y * I / 6.f), Show(FLinearColor(I * 60.f, 1.f, 1.f).HSVToLinearRGB())));
				FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(), Stops, Orient_Horizontal);
				const float Y = Hsv.R / 360.f * Size.Y;
				FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), { FVector2f(0.f, Y), FVector2f(Size.X, Y) }, ESlateDrawEffect::None, FLinearColor::White, true, 2.f);
				return Layer + 2;
			}
			const FLinearColor Pure = Show(FLinearColor(Hsv.R, 1.f, 1.f).HSVToLinearRGB());
			FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(), { FSlateGradientStop(FVector2D(0.f, 0.f), FLinearColor::White), FSlateGradientStop(FVector2D(Size.X, 0.f), Pure) }, Orient_Vertical);
			FSlateDrawElement::MakeGradient(Out, Layer + 1, G.ToPaintGeometry(), { FSlateGradientStop(FVector2D(0.f, 0.f), FLinearColor(0.f, 0.f, 0.f, 0.f)), FSlateGradientStop(FVector2D(0.f, Size.Y), FLinearColor::Black) }, Orient_Horizontal);
			const FVector2f At(Hsv.G * Size.X, (1.f - Hsv.B) * Size.Y);
			TArray<FVector2f> Ring;
			for (int32 I = 0; I <= 12; ++I) Ring.Add(At + FVector2f(FMath::Cos(I * UE_PI / 6.f), FMath::Sin(I * UE_PI / 6.f)) * 5.f);
			FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), Ring, ESlateDrawEffect::None, Hsv.B > 0.5f ? FLinearColor::Black : FLinearColor::White, true, 1.5f);
			return Layer + 3;
		}
		virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override { Pick(G, E); return FReply::Handled().CaptureMouse(SharedThis(this)); }
		virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override { if (HasMouseCapture()) Pick(G, E); return FReply::Handled(); }
		virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override { return FReply::Handled().ReleaseMouseCapture(); }
	private:
		void Pick(const FGeometry& G, const FPointerEvent& E)
		{
			const FVector2D L = G.AbsoluteToLocal(E.GetScreenSpacePosition()), S = G.GetLocalSize();
			if (OnPick) OnPick(FVector2D(FMath::Clamp(L.X / FMath::Max(S.X, 1.), 0., 1.), FMath::Clamp(L.Y / FMath::Max(S.Y, 1.), 0., 1.)));
		}
		bool bHue = false;
		TFunction<FLinearColor()> GetHsv;
		TFunction<void(const FVector2D&)> OnPick;
	};

	bool MatchesQuery(const FString& Text, const TArray<FString>& Include, const TArray<FString>& Exclude)
	{
		for (const FString& W : Include) if (!Text.Contains(W, ESearchCase::IgnoreCase)) return false;
		for (const FString& W : Exclude) if (Text.Contains(W, ESearchCase::IgnoreCase)) return false;
		return true;
	}

	FString PresetDirectory() { return FOKStudioSettings::Directory() / TEXT("PostPresets"); }
}

// --- Character cards ---

TArray<FString> SOKStudioPanel::CardRoots() const
{
	TArray<FString> Roots = { FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("CharacterCards")) };
	FString GameRoot;
	GConfig->GetString(TEXT("/Script/KKSideloader.KKGameRoot"), TEXT("Root"), GameRoot, GGameIni);
	if (!GameRoot.IsEmpty()) Roots.Add(FPaths::ConvertRelativePathToFull(GameRoot / TEXT("UserData/chara")));
	for (const FString& R : FOKStudioSettings::Get().CardRoots) Roots.AddUnique(R);
	Roots.RemoveAll([](const FString& R) { return !FPaths::DirectoryExists(R); });
	return Roots;
}

int32 SOKStudioPanel::CardSex(const FString& Card)
{
	// Read once per file version (the card's whole data is parsed; a folder of hundreds takes a moment the first time).
	static TMap<FString, TPair<FDateTime, int32>> Known;
	const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Card);
	if (const TPair<FDateTime, int32>* K = Known.Find(Card); K && K->Key == Stamp) return K->Value;
	FKKCharacterFile File;
	FString Error;
	const int32 Sex = UKKCardReader::LoadFromFile(Card, File, Error) ? File.Sex : -1;
	Known.Add(Card, { Stamp, Sex });
	return Sex;
}

FString SOKStudioPanel::DefaultCard(int32 Sex)
{
	const FString& Set = FOKStudioSettings::Get().DefaultCards[Sex == 0 ? 0 : 1];
	if (!Set.IsEmpty() && FPaths::FileExists(Set)) return Set;
	TArray<FString> Demo;
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("CharacterCards/Demo"));
	IFileManager::Get().FindFiles(Demo, *(Dir / TEXT("*.png")), true, false);
	Demo.Sort();
	for (const FString& N : Demo) if (CardSex(Dir / N) == (Sex == 0 ? 0 : 1)) return Dir / N;
	return FString();
}

void SOKStudioPanel::ScanCardFolder()
{
	if (CardFolder.IsEmpty() || !FPaths::DirectoryExists(CardFolder))
	{
		// The first root; when its cards are all in folders, the first folder with cards.
		const TArray<FString> Roots = CardRoots();
		CardFolder = Roots.Num() ? Roots[0] : FString();
		TArray<FString> Here, Subs;
		if (!CardFolder.IsEmpty()) IFileManager::Get().FindFiles(Here, *(CardFolder / TEXT("*.png")), true, false);
		if (Here.IsEmpty() && !CardFolder.IsEmpty())
		{
			IFileManager::Get().FindFiles(Subs, *(CardFolder / TEXT("*")), false, true);
			Subs.Sort();
			for (const FString& S : Subs)
			{
				TArray<FString> Pngs;
				IFileManager::Get().FindFiles(Pngs, *(CardFolder / S / TEXT("*.png")), true, false);
				if (!S.StartsWith(TEXT(".")) && Pngs.Num()) { CardFolder = CardFolder / S; break; }
			}
		}
	}
	CardFiles.Reset();
	if (CardFolder.IsEmpty()) return;
	TArray<FString> Names;
	IFileManager::Get().FindFiles(Names, *(CardFolder / TEXT("*.png")), true, false);
	Names.Sort();
	for (const FString& N : Names) if (CardSearch.IsEmpty() || N.Contains(CardSearch)) CardFiles.Add(CardFolder / N);
	// 女／男 (HS2Studio6 角色載入器): cards of the other sex (or unreadable) are left out.
	if (CardSexFilter >= 0) CardFiles.RemoveAll([this](const FString& F) { return CardSex(F) != CardSexFilter; });
	CardPage = FMath::Clamp(CardPage, 0, FMath::Max(0, (CardFiles.Num() - 1) / 18));
}

bool SOKStudioPanel::AddOrReplaceCard(const FString& Card, bool bReplace)
{
	if (!Game.IsValid() || Card.IsEmpty()) return false;
	// Cards under the project are kept relative (a scene moves with the project).
	FString Path = FPaths::ConvertRelativePathToFull(FPaths::IsRelative(Card) ? FPaths::ProjectDir() / Card : Card);
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	if (Path.StartsWith(Project)) FPaths::MakePathRelativeTo(Path, *Project);
	FOKStudioSettings::Get().UseRecent(TEXT("card:") + Path);
	FOKSceneObject* X = Obj();
	if (!bReplace || !X || X->Kind != EOKSceneObjectKind::Character)
	{
		Game->AddCharacter(Path, 0);
		SetStatus(TEXT("加入角色 ") + FPaths::GetBaseFilename(Path));
		return true;
	}
	// Replacing: a card that cannot be opened leaves the character as it was.
	const FString Old = X->Character.Card, OldName = X->Name;
	const int32 Id = X->Id;
	X->Character.Card = Path;
	X->Name = FPaths::GetBaseFilename(Path);
	Changed();
	Game->SyncDocumentNow();
	if (Game->SceneActor && Game->SceneActor->Errors.Contains(Id))
	{
		const FString Why = Game->SceneActor->Errors[Id];
		if (FOKSceneObject* Y = Game->Document->Find(Id)) { Y->Character.Card = Old; Y->Name = OldName; Changed(); }
		SetStatus(TEXT("無法讀這張卡，保留原角色：") + Why);
		return false;
	}
	SetStatus(TEXT("已替換為 ") + FPaths::GetBaseFilename(Path));
	return true;
}

TSharedRef<SWidget> SOKStudioPanel::BuildCardBrowser()
{
	ScanCardFolder();
	// Folders: the roots, the folders inside the current one, its parent.
	TSharedRef<SVerticalBox> Folders = SNew(SVerticalBox);
	auto FolderButton = [this, &Folders](const FString& Label, const FString& Folder)
	{
		Folders->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
			[SNew(SBox).ToolTipText(FText::FromString(Folder))[Button(FText::FromString(Label), [this, Folder] { CardFolder = Folder; CardPage = 0; DeferFloat(TEXT("cards")); }, 0.f, 18.f, CardFolder == Folder, 10.f)]];
	};
	const TArray<FString> Roots = CardRoots();
	for (const FString& R : Roots) FolderButton(FPaths::GetCleanFilename(R), R);
	if (!Roots.Contains(CardFolder) && !CardFolder.IsEmpty()) FolderButton(TEXT(".. 上一層"), FPaths::GetPath(CardFolder));
	TArray<FString> Subs;
	if (!CardFolder.IsEmpty()) IFileManager::Get().FindFiles(Subs, *(CardFolder / TEXT("*")), false, true);
	Subs.Sort();
	for (const FString& S : Subs) if (!S.StartsWith(TEXT("."))) FolderButton(TEXT("▸ ") + S, CardFolder / S);

	// A page of cards: 6 across, 3 down. Each keeps its size (a folder of two cards does not blow them up to fill the area).
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(2.f));
	const int32 PerPage = 18, First = CardPage * PerPage;
	for (int32 I = First; I < FMath::Min(First + PerPage, CardFiles.Num()); ++I)
	{
		const FString F = CardFiles[I];
		Grid->AddSlot((I - First) % 6, (I - First) / 6)
		[
			SNew(SBox).WidthOverride(52.f).HeightOverride(86.f)
			[
				Fill(TAttribute<bool>::CreateLambda([this, F] { return CardPreview == F; }), ListArea(),
					SNew(SButton).ButtonStyle(&FlatButton()).ContentPadding(FMargin(1.f)).ToolTipText(FText::FromString(FPaths::GetBaseFilename(F)))
					.OnClicked_Lambda([this, F] { CardPreview = F; DeferFloat(TEXT("cards")); return FReply::Handled(); })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SImage).Image_Lambda([F] { const FSlateBrush* B = FOKStudioThumbnails::Get().File(F, 96); return B ? B : Brush(Input()); })]
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font(8.f)).ColorAndOpacity(OKStudioStyle::Text()).OverflowPolicy(ETextOverflowPolicy::Ellipsis).AddMetaData<FTagMetaData>(TEXT("Ellipsis")).Text(FText::FromString(FPaths::GetBaseFilename(F)))]
					])
			]
		];
	}
	const int32 Pages = FMath::Max(1, (CardFiles.Num() + PerPage - 1) / PerPage);
	// The picked card: bigger, then add it or put it on the selected character.
	TSharedRef<SVerticalBox> Pick = SNew(SVerticalBox);
	if (!CardPreview.IsEmpty())
	{
		const FString F = CardPreview;
		const FOKSceneObject* X = Obj();
		const bool bCanReplace = X && X->Kind == EOKSceneObjectKind::Character;
		Pick->AddSlot().AutoHeight()[SNew(SBox).WidthOverride(128.f).HeightOverride(180.f)[SNew(SImage).Image_Lambda([F] { const FSlateBrush* B = FOKStudioThumbnails::Get().File(F, 256); return B ? B : Brush(Input()); })]];
		Pick->AddSlot().AutoHeight().Padding(0.f, 2.f)[SNew(STextBlock).Font(Font(10.f)).ColorAndOpacity(OKStudioStyle::Text()).AutoWrapText(true).Text(FText::FromString(FPaths::GetBaseFilename(F)))];
		Pick->AddSlot().AutoHeight().Padding(0.f, 2.f)[Button(FText::FromString(TEXT("加入")), [this, F] { AddOrReplaceCard(F, false); }, 0.f, 22.f)];
		Pick->AddSlot().AutoHeight().Padding(0.f, 2.f)[Button(FText::FromString(TEXT("替換所選角色")), [this, F] { AddOrReplaceCard(F, true); }, 0.f, 22.f, false, 11.f, ButtonFill(), bCanReplace)];
		Pick->AddSlot().AutoHeight().Padding(0.f, 2.f)[Button(FText::FromString(TEXT("設為預設角色")), [this, F]
		{
			const int32 Sex = CardSex(F);
			if (Sex < 0) { SetStatus(TEXT("讀不出這張卡的性別")); return; }
			FOKStudioSettings::Get().DefaultCards[Sex == 0 ? 0 : 1] = F;
			FOKStudioSettings::Get().Save();
			SetStatus(FString::Printf(TEXT("預設%s角色：%s"), Sex == 0 ? TEXT("男") : TEXT("女"), *FPaths::GetBaseFilename(F)));
		}, 0.f, 20.f, false, 10.f)];
	}
	else
	{
		TSharedRef<STextBlock> Hint = OKStudioStyle::Label(FText::FromString(TEXT("點一張卡預覽，再按「加入」或「替換所選角色」。")), 10.f, TextHint());
		Hint->SetAutoWrapText(true);
		Pick->AddSlot().AutoHeight()[Hint];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(CardFolder), 9.f, TextHint())]
			// 全部 → 女 → 男, and 加入預設角色 of the sex shown (female when all are).
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 3.f, 0.f)[Button(FText::FromString(CardSexFilter < 0 ? TEXT("全部") : CardSexFilter == 1 ? TEXT("女") : TEXT("男")),
				[this] { CardSexFilter = CardSexFilter < 0 ? 1 : CardSexFilter == 1 ? 0 : -1; CardPage = 0; DeferFloat(TEXT("cards")); }, 40.f, 20.f, CardSexFilter >= 0, 10.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 3.f, 0.f)[Button(FText::FromString(TEXT("加入預設角色")), Cmd(TEXT("add.defaultcharacter"), CardSexFilter == 0 ? TEXT("male") : TEXT("female")), 84.f, 20.f, false, 10.f)]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(150.f).HeightOverride(20.f)[SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("搜尋卡片"))).Text(FText::FromString(CardSearch))
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { CardSearch = T.ToString(); CardPage = 0; DeferFloat(TEXT("cards")); })]]
			+ SHorizontalBox::Slot().AutoWidth().Padding(3.f, 0.f, 0.f, 0.f)[IconButton(TEXT("eye"), TEXT("定位當前角色的卡"), [this]
			{
				const FOKSceneObject* X = Obj();
				if (!X || X->Kind != EOKSceneObjectKind::Character) return;
				const FString Path = FPaths::ConvertRelativePathToFull(FPaths::IsRelative(X->Character.Card) ? FPaths::ProjectDir() / X->Character.Card : X->Character.Card);
				CardFolder = FPaths::GetPath(Path); CardSearch.Reset(); CardPreview = Path;
				ScanCardFolder();
				CardPage = FMath::Max(0, CardFiles.IndexOfByKey(Path)) / 18;
				DeferFloat(TEXT("cards"));
			}, 22.f, 20.f)]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(120.f)[SNew(SScrollBox) + SScrollBox::Slot()[Folders]]]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(4.f, 0.f)[SNew(SBorder).BorderImage(Brush(ListArea())).Padding(2.f).HAlign(HAlign_Left).VAlign(VAlign_Top)[Grid]]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(132.f)[Pick]]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[IconButton(TEXT("prev"), TEXT("上一頁"), [this] { CardPage = FMath::Max(0, CardPage - 1); DeferFloat(TEXT("cards")); }, 30.f, 20.f)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f).VAlign(VAlign_Center)[OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("%d / %d 頁（%d 張）"), CardPage + 1, Pages, CardFiles.Num())), 10.f)]
			+ SHorizontalBox::Slot().AutoWidth()[IconButton(TEXT("next"), TEXT("下一頁"), [this, Pages] { CardPage = FMath::Min(Pages - 1, CardPage + 1); DeferFloat(TEXT("cards")); }, 30.f, 20.f)]
		];
}

// --- Items: adding, previews ---

bool SOKStudioPanel::AddItemTo(int32 Group, int32 Category, int32 No, bool bAsChild)
{
	if (!Game.IsValid() || !FOKStudioCatalog::Get().FindItem(Group, Category, No)) return false;
	const int32 Parent = bAsChild ? Game->SelectedId : -2;
	const int32 Id = Game->Add(EOKSceneObjectKind::Item);
	// Middle click: under the selected object (whatever it is), at its place.
	if (Parent >= 0 && Parent != Id && Game->Document->Find(Parent))
		if (FOKSceneObject* O = Game->Document->Find(Id)) { O->Parent = Parent; O->Transform = FTransform::Identity; }
	Game->Select(Id);
	PickItem(Group, Category, No);
	FOKStudioSettings::Get().UseRecent(ItemKey(Group, Category, No));
	AddedStatus();
	return true;
}

void SOKStudioPanel::TickItemPreview()
{
	if (!Game.IsValid() || !Game->SceneActor) return;
	// A picture is built once the mouse rested on an item a moment, in a scene of its own far below the studio.
	if (PreviewBuilding.X < 0)
	{
		if (HoverItem.X < 0 || FPlatformTime::Seconds() - HoverSince < 0.18) return;
		const FString Key = ItemKey(HoverItem.X, HoverItem.Y, HoverItem.Z);
		if (FOKStudioThumbnails::Get().Find(Key)) return;
		if (!PreviewDoc.IsValid()) PreviewDoc.Reset(NewObject<UOKSceneDocument>());
		if (!PreviewActor.IsValid())
		{
			FActorSpawnParameters Params;
			Params.ObjectFlags = RF_Transient;
			PreviewActor = Game->SceneActor->GetWorld()->SpawnActor<AOKSceneActor>(FVector(0.f, 0.f, -200000.f), FRotator::ZeroRotator, Params);
			if (!PreviewActor.IsValid()) return;
			PreviewActor->Document = PreviewDoc.Get();
		}
		UOKSceneDocument* Doc = PreviewDoc.Get();
		Doc->Objects.Reset();
		const int32 Id = Doc->AddObject(EOKSceneObjectKind::Item, TEXT("preview"));
		FOKSceneObject* O = Doc->Find(Id);
		O->Item.Group = HoverItem.X; O->Item.Category = HoverItem.Y; O->Item.No = HoverItem.Z;
		Doc->NotifyChanged();
		PreviewBuilding = HoverItem;
		PreviewWait = 0;
		return;
	}
	AOKSceneActor* Actor = PreviewActor.Get();
	if (!Actor) { PreviewBuilding = FIntVector(-1); return; }
	Actor->Sync();
	if ((Actor->HasPendingBuilds() || ++PreviewWait < 3) && PreviewWait < 120) return;
	const FString Key = ItemKey(PreviewBuilding.X, PreviewBuilding.Y, PreviewBuilding.Z);
	PreviewBuilding = FIntVector(-1);
	// Frame the item's bounds from the front, a little above.
	FBox Box(ForceInit);
	if (const FOKSceneObject* O = PreviewDoc->Objects.Num() ? &PreviewDoc->Objects[0] : nullptr)
		if (const AKKPreviewActor* Shown = Actor->PreviewOf(O->Id))
		{
			TInlineComponentArray<UPrimitiveComponent*> Parts(Shown);
			for (const UPrimitiveComponent* P : Parts) if (P->IsVisible() && P->IsRegistered()) Box += P->Bounds.GetBox();
		}
	TArray<FColor> Pixels;
	FString Error;
	if (Box.IsValid)
	{
		const FRotator Look(-15.f, -90.f, 0.f);
		const float Radius = FMath::Max(float(Box.GetExtent().Size()), 5.f);
		const FTransform Shot(Look, Box.GetCenter() - Look.Vector() * Radius / FMath::Tan(FMath::DegreesToRadians(15.f)) * 1.1f);
		FOKSceneCard::Capture(Actor, Shot, 30.f, nullptr, 256, 144, true, Pixels, Error);
	}
	if (Pixels.Num() == 256 * 144)
	{
		FOKStudioThumbnails::Get().Put(Key, Pixels, 256, 144);
		PreviewCache.Remove(Key);
		PreviewCache.Add(Key);
		// Twelve pictures at most.
		while (PreviewCache.Num() > 12) { FOKStudioThumbnails::Get().Forget(PreviewCache[0]); PreviewCache.RemoveAt(0); }
	}
	else UE_LOG(LogTemp, Display, TEXT("OK studio: no preview of %s (%s)"), *Key, Error.IsEmpty() ? TEXT("no bounds") : *Error);
	PreviewDoc->Objects.Reset();
	PreviewDoc->NotifyChanged();
	Actor->Sync();
}

// --- Quick search ---

TArray<SOKStudioPanel::FHit> SOKStudioPanel::SearchCatalog(const FString& Query, const TArray<FString>& Kinds, int32 Limit, bool bHidden) const
{
	TArray<FString> Words, Include, Exclude;
	Query.ParseIntoArrayWS(Words);
	for (const FString& W : Words) { if (W.StartsWith(TEXT("-")) && W.Len() > 1) Exclude.Add(W.Mid(1)); else Include.Add(W); }
	const FOKStudioCatalog& C = FOKStudioCatalog::Get();
	const FOKStudioSettings& S = FOKStudioSettings::Get();
	auto Wants = [&Kinds](const TCHAR* K) { return Kinds.IsEmpty() || Kinds.Contains(K); };
	TArray<FHit> Out;
	auto Add = [&](const FString& Kind, const FString& Key, const FString& Name, const FString& Path = FString())
	{
		if ((!bHidden && S.Hidden.Contains(Key)) || !MatchesQuery(Name + TEXT(" ") + Key, Include, Exclude)) return;
		Out.Add({ Kind, Key, Name, Path });
	};
	if (Wants(TEXT("item")))
		for (const FOKStudioCatalogItem& I : C.Items)
		{
			const FOKStudioCatalogGroup* Cat = C.Categories.FindByPredicate([&I](const FOKStudioCatalogGroup& G) { return G.Group == I.Group && G.Id == I.Category; });
			Add(TEXT("item"), ItemKey(I.Group, I.Category, I.No), I.Name + (Cat ? TEXT("　") + Cat->Name : FString()));
		}
	if (Wants(TEXT("effect"))) for (const FOKStudioEffect& E : C.Effects) Add(TEXT("effect"), TEXT("effect:") + E.Id, E.Category + TEXT("／") + E.Name);
	if (Wants(TEXT("map"))) for (const FOKStudioCatalogMap& M : C.Maps) Add(TEXT("map"), FString::Printf(TEXT("map:%d"), M.No), M.Name);
	if (Wants(TEXT("anim")))
		for (const FOKStudioAnim& A : C.Anims) if (!A.File.IsEmpty()) Add(TEXT("anim"), FString::Printf(TEXT("anim:%d/%d/%d/%d"), A.Kind, A.Group, A.Category, A.No), A.Name);
	if (Wants(TEXT("pose"))) for (const FString& F : FOKStudioPose::Files()) Add(TEXT("pose"), TEXT("pose:") + F, FPaths::GetBaseFilename(F), F);
	if (Wants(TEXT("card"))) for (const TSharedPtr<FString>& F : Cards) Add(TEXT("card"), TEXT("card:") + *F, FPaths::GetBaseFilename(*F), *F);
	// Favourites first, then the recently used, then by name.
	Out.Sort([&S](const FHit& A, const FHit& B)
	{
		const bool FA = S.Favorites.Contains(A.Key), FB = S.Favorites.Contains(B.Key);
		if (FA != FB) return FA;
		const int32 RA = S.Recent.IndexOfByKey(A.Key), RB = S.Recent.IndexOfByKey(B.Key);
		if ((RA >= 0) != (RB >= 0)) return RA >= 0;
		if (RA >= 0) return RA < RB;
		return A.Name < B.Name;
	});
	if (Limit > 0 && Out.Num() > Limit) Out.SetNum(Limit);
	return Out;
}

void SOKStudioPanel::RunQuickSearch()
{
	const FOKStudioSettings& S = FOKStudioSettings::Get();
	TArray<FString> Kinds;
	if (!QuickKind.IsEmpty()) Kinds.Add(QuickKind);
	TArray<FHit> Hits;
	if (QuickMode == 2)
	{
		// Recently used, newest first.
		const TArray<FHit> All = SearchCatalog(QuickQuery, Kinds, 0, true);
		for (const FString& K : S.Recent) if (const FHit* H = All.FindByPredicate([&K](const FHit& X) { return X.Key == K; })) Hits.Add(*H);
	}
	else
	{
		Hits = SearchCatalog(QuickQuery, Kinds, QuickQuery.IsEmpty() && QuickMode == 0 ? 200 : 600, QuickMode == 3);
		if (QuickMode == 1) Hits.RemoveAll([&S](const FHit& H) { return !S.Favorites.Contains(H.Key); });
		if (QuickMode == 3) Hits.RemoveAll([&S](const FHit& H) { return !S.Hidden.Contains(H.Key); });
	}
	QuickHits.Reset();
	for (FHit& H : Hits) QuickHits.Add(MakeShared<FHit>(MoveTemp(H)));
	if (QuickList) QuickList->RequestListRefresh();
}

void SOKStudioPanel::ReloadCatalog()
{
	FOKStudioCatalog::Reload();
	const FOKStudioCatalog& C = FOKStudioCatalog::Get();
	// Nothing may keep pointing into the old catalogue; the open lists are rebuilt from the new one.
	AnimSyncedTo = nullptr;
	RebuildSubMenu();
	RebuildProperties();
	TArray<FString> Open;
	for (const TPair<FString, FFloat>& F : Floats) if (F.Value.bOpen) Open.Add(F.Key);
	for (const FString& Id : Open) DeferFloat(Id);
	RunQuickSearch();
	SetStatus(FString::Printf(TEXT("目錄已重新讀取：%d 個道具、%d 個動畫、%d 個特效"), C.Items.Num(), C.Anims.Num(), C.Effects.Num()));
}

bool SOKStudioPanel::UseHit(const FHit& Hit)
{
	if (!Game.IsValid()) return false;
	TArray<FString> P;
	Hit.Key.Mid(Hit.Key.Find(TEXT(":")) + 1).ParseIntoArray(P, TEXT("/"));
	bool bOk = false;
	if (Hit.Kind == TEXT("item") && P.Num() == 3) bOk = AddItemTo(FCString::Atoi(*P[0]), FCString::Atoi(*P[1]), FCString::Atoi(*P[2]), false);
	else if (Hit.Kind == TEXT("effect")) bOk = RunCommand(TEXT("add.effect"), Hit.Key.Mid(7));
	else if (Hit.Kind == TEXT("map") && P.Num() == 1) { Game->Document->Map = FCString::Atoi(*P[0]); Changed(); bOk = true; }
	else if (Hit.Kind == TEXT("anim") && P.Num() == 4)
	{
		const FOKStudioCatalog& C = FOKStudioCatalog::Get();
		const int32 Index = C.Anims.IndexOfByPredicate([&P](const FOKStudioAnim& A) { return A.Kind == FCString::Atoi(*P[0]) && A.Group == FCString::Atoi(*P[1]) && A.Category == FCString::Atoi(*P[2]) && A.No == FCString::Atoi(*P[3]); });
		bOk = Index != INDEX_NONE && PickAnim(Index);
		if (!bOk) SetStatus(TEXT("先選一個角色再套動畫"));
	}
	else if (Hit.Kind == TEXT("pose")) bOk = LoadPose(Hit.Path);
	else if (Hit.Kind == TEXT("card")) bOk = AddOrReplaceCard(Hit.Path, false);
	if (bOk) FOKStudioSettings::Get().UseRecent(Hit.Key);
	return bOk;
}

TSharedRef<SWidget> SOKStudioPanel::BuildQuickSearch()
{
	static const TCHAR* Modes[] = { TEXT("全部"), TEXT("收藏"), TEXT("最近"), TEXT("已隱藏") };
	TSharedRef<SHorizontalBox> ModeRow = SNew(SHorizontalBox);
	for (int32 M = 0; M < 4; ++M) ModeRow->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(Modes[M]), [this, M] { QuickMode = M; RunQuickSearch(); DeferFloat(TEXT("quick")); }, 0.f, 18.f, QuickMode == M, 10.f)];
	static const TMap<FString, FString> KindNames = { { TEXT("item"), TEXT("道具") }, { TEXT("effect"), TEXT("特效") }, { TEXT("map"), TEXT("地圖") }, { TEXT("anim"), TEXT("動畫") }, { TEXT("pose"), TEXT("姿勢") }, { TEXT("card"), TEXT("角色卡") } };
	// HS2Studio6 筛选列表: one kind only; and 刷新目录.
	static const TCHAR* Kinds[] = { TEXT(""), TEXT("item"), TEXT("effect"), TEXT("map"), TEXT("anim"), TEXT("pose"), TEXT("card") };
	TSharedRef<SHorizontalBox> KindRow = SNew(SHorizontalBox);
	for (const TCHAR* K : Kinds)
	{
		const FString Kind = K;
		const FString* Name = KindNames.Find(Kind);
		KindRow->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 1.f, 0.f)[Button(FText::FromString(Name ? *Name : TEXT("全種類")), [this, Kind] { QuickKind = Kind; RunQuickSearch(); DeferFloat(TEXT("quick")); }, 0.f, 18.f, QuickKind == Kind, 9.f)];
	}
	KindRow->AddSlot().AutoWidth()[Button(FText::FromString(Tip(TEXT("刷新目錄"), TEXT("catalog.reload"))), Cmd(TEXT("catalog.reload")), 0.f, 18.f, false, 9.f)];
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride(22.f)
			[
				SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("道具、特效、地圖、動畫、姿勢、角色卡（空格＝且，-詞＝排除）"))).Text(FText::FromString(QuickQuery))
				.OnTextChanged_Lambda([this](const FText& T) { QuickQuery = T.ToString(); RunQuickSearch(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)[ModeRow]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f, 0.f, 3.f)[KindRow]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SBorder).BorderImage(Brush(ListArea())).Padding(1.f)
			[
				SAssignNew(QuickList, SListView<TSharedPtr<FHit>>).ListItemsSource(&QuickHits).SelectionMode(ESelectionMode::None)
				.OnGenerateRow_Lambda([this](TSharedPtr<FHit> H, const TSharedRef<STableViewBase>& Owner)
				{
					const FHit Hit = *H;
					const FString* Kind = KindNames.Find(Hit.Kind);
					return SNew(STableRow<TSharedPtr<FHit>>, Owner).Style(&RowStyle())
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f)[SNew(SBox).WidthOverride(40.f)[OKStudioStyle::Label(FText::FromString(Kind ? *Kind : Hit.Kind), 9.f, TextHint())]]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(SButton).ButtonStyle(&FlatButton()).ContentPadding(FMargin(2.f, 1.f)).ToolTipText(FText::FromString(Hit.Key))
							.OnClicked_Lambda([this, Hit] { UseHit(Hit); return FReply::Handled(); })
							[SNew(STextBlock).Font(Font(10.f)).ColorAndOpacity(OKStudioStyle::Text()).Text(FText::FromString(Hit.Name))]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[IconButton(TEXT("star"), TEXT("收藏"), [this, Hit] { FOKStudioSettings& S = FOKStudioSettings::Get(); if (!S.Favorites.Remove(Hit.Key)) S.Favorites.Add(Hit.Key); S.SaveFavorites(); RunQuickSearch(); }, 20.f, 18.f,
							FOKStudioSettings::Get().Favorites.Contains(Hit.Key), ListArea(), FLinearColor(0.75f, 0.55f, 0.05f))]
						+ SHorizontalBox::Slot().AutoWidth()
						[IconButton(TEXT("eye"), FOKStudioSettings::Get().Hidden.Contains(Hit.Key) ? TEXT("取消隱藏") : TEXT("隱藏"), [this, Hit] { FOKStudioSettings& S = FOKStudioSettings::Get(); if (!S.Hidden.Remove(Hit.Key)) S.Hidden.Add(Hit.Key); S.SaveFavorites(); RunQuickSearch(); }, 20.f, 18.f,
							FOKStudioSettings::Get().Hidden.Contains(Hit.Key), ListArea(), FLinearColor(0.5f, 0.1f, 0.1f))]
					];
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(TAttribute<FText>::CreateLambda([this] { return FText::FromString(FString::Printf(TEXT("%d 筆；點名稱加入或套用"), QuickHits.Num())); }), 9.f, TextHint())];
}

// --- Effects and maps ---

TSharedRef<SWidget> SOKStudioPanel::BuildEffectList()
{
	const FOKStudioCatalog& C = FOKStudioCatalog::Get();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	for (const FString& Category : C.EffectCategories)
	{
		bool bHeader = false;
		for (const FOKStudioEffect& E : C.Effects)
		{
			if (E.Category != Category || (!EffectSearch.IsEmpty() && !E.Name.Contains(EffectSearch) && !E.Id.Contains(EffectSearch))) continue;
			if (!bHeader) { Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 1.f)[OKStudioStyle::Label(FText::FromString(Category), 10.f, TextSoft())]; bHeader = true; }
			const FString Id = E.Id;
			Box->AddSlot().AutoHeight().Padding(8.f, 0.f, 0.f, 1.f)
			[
				Button(FText::FromString(E.Name), [this, Id]
				{
					// The selected effect changes to it; otherwise a new effect object is added.
					FOKSceneObject* X = Obj();
					if (X && X->Kind == EOKSceneObjectKind::Effect) { const FOKStudioEffect* Def = FOKStudioCatalog::Get().FindEffect(Id); X->Effect.Effect = Id; if (Def) X->Name = Def->Name; Changed(); FOKStudioSettings::Get().UseRecent(TEXT("effect:") + Id); }
					else RunCommand(TEXT("add.effect"), Id);
					DeferFloat(TEXT("effects"));
				}, 0.f, 18.f, TAttribute<bool>::CreateLambda([this, Id] { const FOKSceneObject* X = Obj(); return X && X->Kind == EOKSceneObjectKind::Effect && X->Effect.Effect == Id; }), 10.f, RowFill())
			];
		}
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(20.f)[SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("搜尋特效"))).Text(FText::FromString(EffectSearch))
			.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { EffectSearch = T.ToString(); DeferFloat(TEXT("effects")); })]]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 3.f, 0.f, 0.f)[SNew(SBorder).BorderImage(Brush(ListArea())).Padding(2.f)[SNew(SScrollBox) + SScrollBox::Slot()[Box]]]
		+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(FText::FromString(FString::Printf(TEXT("%d 個特效；選著特效物件時是替換"), C.Effects.Num())), 9.f, TextHint())];
}

TSharedRef<SWidget> SOKStudioPanel::BuildMapBrowser()
{
	const FOKStudioCatalog& C = FOKStudioCatalog::Get();
	if (MapPick == -1 && Game.IsValid()) MapPick = Game->Document->Map;
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 1.f)[Button(FText::FromString(TEXT("（無地圖）")), [this] { MapPick = -2; DeferFloat(TEXT("maps")); }, 0.f, 18.f, MapPick == -2, 10.f, RowFill())];
	for (const FOKStudioCatalogGroup& Cat : C.MapCategories)
	{
		bool bHeader = false;
		for (const FOKStudioCatalogMap& M : C.Maps)
		{
			if (M.Category != Cat.Id || (!MapSearch.IsEmpty() && !M.Name.Contains(MapSearch))) continue;
			if (!bHeader) { Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 1.f)[OKStudioStyle::Label(FText::FromString(Cat.Name), 10.f, TextSoft())]; bHeader = true; }
			const int32 No = M.No;
			Box->AddSlot().AutoHeight().Padding(8.f, 0.f, 0.f, 1.f)[Button(FText::FromString(FString::Printf(TEXT("%d %s"), No, *M.Name)), [this, No] { MapPick = No; DeferFloat(TEXT("maps")); }, 0.f, 18.f, MapPick == No, 10.f, RowFill())];
		}
	}
	// Maps of categories the catalogue has no name for.
	for (const FOKStudioCatalogMap& M : C.Maps)
		if (!C.MapCategories.ContainsByPredicate([&M](const FOKStudioCatalogGroup& G) { return G.Id == M.Category; }) && (MapSearch.IsEmpty() || M.Name.Contains(MapSearch)))
		{
			const int32 No = M.No;
			Box->AddSlot().AutoHeight().Padding(8.f, 0.f, 0.f, 1.f)[Button(FText::FromString(FString::Printf(TEXT("%d %s"), No, *M.Name)), [this, No] { MapPick = No; DeferFloat(TEXT("maps")); }, 0.f, 18.f, MapPick == No, 10.f, RowFill())];
		}
	const FOKStudioCatalogMap* Picked = C.FindMap(MapPick);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(20.f)[SNew(SEditableTextBox).Style(&TextBoxStyle()).HintText(FText::FromString(TEXT("搜尋地圖"))).Text(FText::FromString(MapSearch))
			.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { MapSearch = T.ToString(); DeferFloat(TEXT("maps")); })]]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 3.f)[SNew(SBorder).BorderImage(Brush(ListArea())).Padding(2.f)[SNew(SScrollBox) + SScrollBox::Slot()[Box]]]
		+ SVerticalBox::Slot().AutoHeight()[OKStudioStyle::Label(FText::FromString(MapPick == -2 ? FString(TEXT("選擇：無地圖")) : Picked ? TEXT("選擇：") + Picked->Name : FString(TEXT("點一張地圖，再按「讀取」"))), 10.f, TextSoft())]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
		[
			Button(FText::FromString(TEXT("讀取")), [this]
			{
				if (!Game.IsValid() || MapPick == -1) return;
				Game->Document->Map = MapPick == -2 ? -1 : MapPick;
				Changed();
				FOKStudioSettings::Get().UseRecent(FString::Printf(TEXT("map:%d"), Game->Document->Map));
				SetStatus(TEXT("地圖：") + (FOKStudioCatalog::Get().FindMap(Game->Document->Map) ? FOKStudioCatalog::Get().FindMap(Game->Document->Map)->Name : FString(TEXT("無"))));
			}, 0.f, 22.f)
		];
}

// --- Colours ---

void SOKStudioPanel::PickColor(const FString& Key, const FString& Title, const FLinearColor& Start, TFunction<void(const FLinearColor&)> Set)
{
	ColorKey = Key;
	ColorTitle = Title;
	ColorValue = Start.A < 0.f ? FLinearColor(1.f, 1.f, 1.f, 1.f) : Start;
	ColorTarget = MoveTemp(Set);
	if (FFloat* F = Floats.Find(TEXT("color"))) F->Title = TEXT("顏色：") + Title;
	OpenFloat(TEXT("color"));
}

TSharedRef<SWidget> SOKStudioPanel::Swatch(const FString& Key, const FString& Title, TFunction<FLinearColor()> Get, TFunction<void(const FLinearColor&)> Set)
{
	return SNew(SButton).ButtonStyle(&FlatButton()).ContentPadding(FMargin(1.f)).ToolTipText(FText::FromString(TEXT("點一下開色盤；再點一下關閉")))
		.OnClicked_Lambda([this, Key, Title, Get, Set]
		{
			if (IsFloatOpen(TEXT("color")) && ColorKey == Key) CloseFloat(TEXT("color"));
			else PickColor(Key, Title, Get(), Set);
			return FReply::Handled();
		})
		[
			SNew(SBorder).BorderImage_Lambda([Get] { return Brush(Display(Get())); }).Padding(0.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[OKStudioStyle::Label(TAttribute<FText>::CreateLambda([Get] { const FLinearColor C = Get(); return FText::FromString(C.A < 0.f ? FString(TEXT("預設")) : TEXT("#") + C.ToFColor(false).ToHex().Left(6)); }), 9.f)]
		];
}

void SOKStudioPanel::ColorRow(const FString& Name, TFunction<FLinearColor(FOKSceneObject&)> Get, TFunction<void(FOKSceneObject&, const FLinearColor&)> Set)
{
	const int32 Id = Game.IsValid() ? Game->SelectedId : -1;
	Row(Name, SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)[Swatch(FString::Printf(TEXT("obj%d:%s"), Id, *Name), Name, [this, Get] { FOKSceneObject* O = Obj(); return O ? Get(*O) : FLinearColor(1.f, 1.f, 1.f, -1.f); },
			[this, Set](const FLinearColor& C) { if (FOKSceneObject* O = Obj()) { Set(*O, C); Changed(); } })]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f, 0.f, 0.f)[Button(FText::FromString(TEXT("預設")), [this, Set] { if (FOKSceneObject* O = Obj()) { Set(*O, FLinearColor(1.f, 1.f, 1.f, -1.f)); Changed(); } }, 34.f, 18.f, false, 9.f)]);
}

TSharedRef<SWidget> SOKStudioPanel::BuildColorPicker()
{
	auto Apply = [this](const FLinearColor& C) { ColorValue = C; if (ColorTarget) ColorTarget(C); };
	auto Hsv = [this] { return FLinearColor(ColorValue.R, ColorValue.G, ColorValue.B, 1.f).LinearRGBToHSV(); };
	TSharedRef<SWrapBox> Recent = SNew(SWrapBox).UseAllottedSize(true);
	for (const FLinearColor& C : FOKStudioSettings::Get().RecentColors)
		Recent->AddSlot().Padding(1.f)
		[
			SNew(SBox).WidthOverride(18.f).HeightOverride(18.f)
			[SNew(SButton).ButtonStyle(&FlatButton()).ContentPadding(0.f).OnClicked_Lambda([this, Apply, C] { Apply(C); return FReply::Handled(); })[SNew(SBorder).BorderImage(Brush(Display(C)))]]
		];
	auto Channel = [this, Apply](const TCHAR* Name, int32 I)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(16.f)[OKStudioStyle::Label(FText::FromString(Name), 10.f)]]
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SBox).HeightOverride(18.f)[Spin([this, I] { return FMath::RoundToFloat(ColorValue.Component(I) * 255.f); },
				[this, Apply, I](float V) { FLinearColor C = ColorValue; C.Component(I) = FMath::Clamp(V, 0.f, 255.f) / 255.f; Apply(C); }, 0.f, 255.f, 1.f)]];
	};
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(170.f).HeightOverride(160.f)[SNew(SColorField).GetHsv(Hsv).OnPick([this, Apply, Hsv](const FVector2D& At)
				{ FLinearColor H = Hsv(); H.G = At.X; H.B = 1.f - At.Y; FLinearColor C = H.HSVToLinearRGB(); C.A = ColorValue.A; Apply(C); })]]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f)[SNew(SBox).WidthOverride(16.f).HeightOverride(160.f)[SNew(SColorField).bHue(true).GetHsv(Hsv).OnPick([this, Apply, Hsv](const FVector2D& At)
				{ FLinearColor H = Hsv(); H.R = At.Y * 359.9f; if (H.G < 0.01f) H.G = 1.f; if (H.B < 0.01f) H.B = 1.f; FLinearColor C = H.HSVToLinearRGB(); C.A = ColorValue.A; Apply(C); })]]
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SBorder).BorderImage_Lambda([this] { return Brush(Display(ColorValue)); })]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)[Channel(TEXT("R"), 0)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)[Channel(TEXT("G"), 1)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)[Channel(TEXT("B"), 2)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f)[Channel(TEXT("A"), 3)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SBox).HeightOverride(18.f)
			[
				TextBox([this] { return TEXT("#") + ColorValue.ToFColor(false).ToHex(); }, [this, Apply](const FString& V)
				{
					FString Hex = V.TrimStartAndEnd();
					Hex.RemoveFromStart(TEXT("#"));
					if (Hex.Len() != 6 && Hex.Len() != 8) return;
					const FColor C = FColor::FromHex(Hex.Len() == 6 ? Hex + TEXT("FF") : Hex);
					Apply(FLinearColor(C.R / 255.f, C.G / 255.f, C.B / 255.f, C.A / 255.f));
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[Recent]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 2.f, 0.f)[Button(FText::FromString(TEXT("確定")), [this] { FOKStudioSettings::Get().UseColor(ColorValue); CloseFloat(TEXT("color")); }, 0.f, 20.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Button(FText::FromString(TEXT("預設色")), [this] { if (ColorTarget) ColorTarget(FLinearColor(1.f, 1.f, 1.f, -1.f)); CloseFloat(TEXT("color")); }, 0.f, 20.f)]
		];
}

// --- 畫面設定 ---

void SOKStudioPanel::SceneSettingsPage()
{
	if (!Game.IsValid()) return;
	auto Doc = [this]() -> FOKScenePost* { return Game.IsValid() ? &Game->Document->Post : nullptr; };
	auto PostCheck = [this, Doc](const FString& Name, bool FOKScenePost::* Field)
	{
		Props->AddSlot().AutoHeight().Padding(0.f, 1.f)[CheckBox(Name, [Doc, Field] { FOKScenePost* P = Doc(); return P && P->*Field; }, [this, Doc, Field](bool B) { if (FOKScenePost* P = Doc()) { P->*Field = B; Changed(); } })];
	};
	auto PostFloat = [this, Doc](const FString& Name, float FOKScenePost::* Field, float Min, float Max, float Delta)
	{
		Row(Name, Spin([Doc, Field] { FOKScenePost* P = Doc(); return P ? P->*Field : 0.f; }, [this, Doc, Field](float V) { if (FOKScenePost* P = Doc()) { P->*Field = V; Changed(); } }, Min, Max, Delta));
	};
	auto PostInt = [this, Doc](const FString& Name, int32 FOKScenePost::* Field, int32 Min, int32 Max)
	{
		Row(Name, Spin([Doc, Field] { FOKScenePost* P = Doc(); return P ? float(P->*Field) : 0.f; }, [this, Doc, Field, Min, Max](float V) { if (FOKScenePost* P = Doc()) { P->*Field = FMath::Clamp(FMath::RoundToInt(V), Min, Max); Changed(); } }, float(Min), float(Max), 1.f));
	};
	auto PostColor = [this, Doc](const FString& Name, FLinearColor FOKScenePost::* Field, bool bOptional)
	{
		Row(Name, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[Swatch(TEXT("post:") + Name, Name, [Doc, Field] { FOKScenePost* P = Doc(); return P ? P->*Field : FLinearColor::White; },
				[this, Doc, Field, bOptional](const FLinearColor& C) { if (FOKScenePost* P = Doc()) { P->*Field = !bOptional && C.A < 0.f ? FLinearColor(C.R, C.G, C.B, 1.f) : C; Changed(); } })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 0.f, 0.f, 0.f)[bOptional ? Button(FText::FromString(TEXT("無")), [this, Doc, Field] { if (FOKScenePost* P = Doc()) { P->*Field = FLinearColor(0.f, 0.f, 0.f, -1.f); Changed(); } }, 30.f, 18.f, false, 9.f) : SNullWidget::NullWidget]);
	};
	auto PostText = [this, Doc](const FString& Name, FString FOKScenePost::* Field, const FString& Hint)
	{
		Row(Name, TextBox([Doc, Field] { FOKScenePost* P = Doc(); return P ? P->*Field : FString(); }, [this, Doc, Field](const FString& V) { if (FOKScenePost* P = Doc()) { P->*Field = V.TrimStartAndEnd().TrimQuotes(); Changed(); } }, Hint), 0.3f);
	};
	const FOKStudioCatalog& C = FOKStudioCatalog::Get();
	switch (SceneTab)
	{
	case 0:
		Label(TEXT("相機與畫面"));
		PostFloat(TEXT("曝光補償 EV"), &FOKScenePost::Exposure, -5.f, 5.f, 0.1f);
		ChoiceF(TEXT("著色"), { TEXT("照卡片"), TEXT("Koikatsu"), TEXT("Koikano") }, [this] { return Game->Document->Post.ShaderType + 1; }, [this](int32 V) { Game->Document->Post.ShaderType = V - 1; Changed(); });
		PostInt(TEXT("陰影 ramp（-1 照卡片）"), &FOKScenePost::RampG, -1, 999);
		PostColor(TEXT("環境陰影色"), &FOKScenePost::AmbientShadow, false);
		PostFloat(TEXT("環境陰影強度（-1 照材質）"), &FOKScenePost::AmbientShadowG, -1.f, 1.f, 0.01f);
		PostFloat(TEXT("描邊深淺（-1 照材質）"), &FOKScenePost::LineColorG, -1.f, 1.f, 0.01f);
		PostFloat(TEXT("描邊寬度（-1 照材質）"), &FOKScenePost::LineWidthG, -1.f, 4.f, 0.01f);
		Props->AddSlot().AutoHeight().Padding(0.f, 4.f)[Button(FText::FromString(TEXT("相機與視角書籤…")), Cmd(TEXT("page.camera")), 0.f, 20.f)];
		break;
	case 1:
	{
		Label(TEXT("地圖"));
		const FOKStudioCatalogMap* M = C.FindMap(Game->Document->Map);
		Row(TEXT("地圖"), Button(FText::FromString(M ? FString::Printf(TEXT("%d %s"), M->No, *M->Name) : FString(TEXT("（無地圖）"))), [this] { MapPick = -1; OpenFloat(TEXT("maps")); }, 0.f, 18.f, false, 10.f, Input()), 0.3f);
		if (!C.bLoaded) Label(C.Error);
		else if (Game->SceneActor && !Game->SceneActor->MapError.IsEmpty()) Label(Game->SceneActor->MapError);
		// Where the map stands (Studio's map position and rotation).
		for (int32 Axis = 0; Axis < 3; ++Axis)
			Row(FString::Printf(TEXT("地圖位置 %s"), Axis == 0 ? TEXT("X") : Axis == 1 ? TEXT("Y") : TEXT("Z")), Spin([this, Axis] { return float(Game->Document->MapTransform.GetLocation()[Axis]); },
				[this, Axis](float V) { FVector L = Game->Document->MapTransform.GetLocation(); L[Axis] = V; Game->Document->MapTransform.SetLocation(L); Changed(); }, -100000.f, 100000.f, 1.f));
		Row(TEXT("地圖旋轉"), Spin([this] { return float(Game->Document->MapTransform.Rotator().Yaw); },
			[this](float V) { FRotator R = Game->Document->MapTransform.Rotator(); R.Yaw = V; Game->Document->MapTransform.SetRotation(R.Quaternion()); Changed(); }, -360.f, 360.f, 1.f));
		ChoiceF(TEXT("時段"), { TEXT("朝"), TEXT("昼"), TEXT("夕"), TEXT("夜") }, [this] { return Game->Document->Post.SunType; }, [this](int32 V) { Game->Document->Post.SunType = V; Changed(); });
		Label(TEXT("天空"));
		PostCheck(TEXT("天空"), &FOKScenePost::bSky);
		SkyOptions.Reset();
		for (const FOKStudioSkyPattern& P : OKStudioSkyPatterns()) SkyOptions.Add(MakeShared<int32>(P.No));
		Row(TEXT("天空樣式"), Combo(&SkyOptions, [](int32 No) { for (const FOKStudioSkyPattern& P : OKStudioSkyPatterns()) if (P.No == No) return FString::Printf(TEXT("%d %s"), No, *P.Name); return FString::FromInt(No); },
			[this] { return Game->Document->Post.SkyPattern; }, [this](int32 V) { Game->Document->Post.SkyPattern = V; Changed(); }));
		break;
	}
	case 2:
		Label(TEXT("霧"));
		PostCheck(TEXT("霧"), &FOKScenePost::bFog);
		PostColor(TEXT("霧的顏色"), &FOKScenePost::FogColor, false);
		Label(TEXT("環境遮蔽"));
		PostCheck(TEXT("環境遮蔽"), &FOKScenePost::bAmbientOcclusion);
		PostColor(TEXT("遮蔽色調"), &FOKScenePost::AmbientOcclusionTint, false);
		PostFloat(TEXT("遮蔽半徑 m"), &FOKScenePost::AmbientOcclusionRadius, 0.01f, 2.f, 0.01f);
		Label(TEXT("太陽光束"));
		PostCheck(TEXT("太陽光束"), &FOKScenePost::bSunShafts);
		PostColor(TEXT("光束顏色"), &FOKScenePost::SunShaftColor, false);
		PostColor(TEXT("光束門檻"), &FOKScenePost::SunShaftThreshold, false);
		break;
	case 3:
		Label(TEXT("泛光"));
		PostCheck(TEXT("泛光"), &FOKScenePost::bBloom);
		PostFloat(TEXT("強度"), &FOKScenePost::BloomIntensity, 0.f, 8.f, 0.05f);
		PostFloat(TEXT("門檻（-1 全部）"), &FOKScenePost::BloomThreshold, -1.f, 1.f, 0.02f);
		PostFloat(TEXT("大小"), &FOKScenePost::BloomSize, 0.1f, 8.f, 0.05f);
		break;
	case 4:
		Label(TEXT("景深"));
		PostCheck(TEXT("景深"), &FOKScenePost::bDepthOfField);
		PostFloat(TEXT("對焦距離 cm"), &FOKScenePost::FocalDistance, 10.f, 5000.f, 5.f);
		PostFloat(TEXT("光圈 f"), &FOKScenePost::Aperture, 0.3f, 32.f, 0.1f);
		break;
	case 5:
	{
		Label(TEXT("色調（Studio 濾鏡 LUT）"));
		FilterOptions.Reset();
		FilterOptions.Add(MakeShared<int32>(-1));
		for (const FOKStudioFilter& F : C.Filters) FilterOptions.Add(MakeShared<int32>(F.No));
		Row(TEXT("濾鏡"), Combo(&FilterOptions, [](int32 No) { const FOKStudioFilter* F = FOKStudioCatalog::Get().FindFilter(No); return F ? FString::Printf(TEXT("%d %s"), No, *F->Name) : FString(TEXT("（無）")); },
			[this] { return Game->Document->Post.ColorFilter; }, [this](int32 V) { Game->Document->Post.ColorFilter = V; Changed(); }));
		PostFloat(TEXT("濾鏡強度"), &FOKScenePost::ColorFilterIntensity, 0.f, 1.f, 0.01f);
		break;
	}
	case 6:
		Label(TEXT("暗角"));
		PostCheck(TEXT("暗角"), &FOKScenePost::bVignette);
		PostFloat(TEXT("Studio 暗角強度"), &FOKScenePost::SourceVignetteIntensity, 0.f, 0.9f, 0.01f);
		PostFloat(TEXT("備用暗角強度"), &FOKScenePost::VignetteIntensity, 0.f, 2.f, 0.01f);
		break;
	case 7:
		Label(TEXT("背景與前景框"));
		PostText(TEXT("背景圖"), &FOKScenePost::Background, TEXT("PNG 路徑（基礎庫相對或檔案）"));
		PostText(TEXT("前景框"), &FOKScenePost::Frame, TEXT("PNG 路徑（含透明）"));
		PostColor(TEXT("背景色"), &FOKScenePost::BackgroundColor, true);
		// HS2Studio6's background and frame lists: the base library's pictures (studio/bg, studio/frame) and the user's
		// (UserData/Studio/bg, frame); a click shows it at once, 無 takes it off.
		for (int32 Kind = 0; Kind < 2; ++Kind)
		{
			const TCHAR* Folder = Kind == 0 ? TEXT("bg") : TEXT("frame");
			FString FOKScenePost::* Field = Kind == 0 ? &FOKScenePost::Background : &FOKScenePost::Frame;
			Label(Kind == 0 ? TEXT("背景圖一覽") : TEXT("前景框一覽"));
			TArray<TPair<FString, FString>> Pictures; // shown file, value stored in the scene
			for (const FString& Root : { AOKSceneActor::LibraryDirectory() / TEXT("studio") / Folder, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UserData/Studio")) / Folder })
			{
				TArray<FString> Names;
				IFileManager::Get().FindFiles(Names, *(Root / TEXT("*.png")), true, false);
				Names.Sort();
				const bool bLibrary = Root.StartsWith(AOKSceneActor::LibraryDirectory());
				for (const FString& N : Names) Pictures.Add({ Root / N, bLibrary ? FString(TEXT("studio/")) + Folder + TEXT("/") + N : Root / N });
			}
			TSharedRef<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true);
			Grid->AddSlot().Padding(1.f)[Tagged(Kind == 0 ? TEXT("bg:none") : TEXT("frame:none"), Button(FText::FromString(TEXT("無")), [this, Field] { (Game->Document->Post.*Field).Reset(); Changed(); DeferRebuild(); }, 48.f, 27.f,
				TAttribute<bool>::CreateLambda([this, Field] { return Game.IsValid() && (Game->Document->Post.*Field).IsEmpty(); }), 10.f))];
			for (const TPair<FString, FString>& P : Pictures)
			{
				const FString File = P.Key, Value = P.Value;
				Grid->AddSlot().Padding(1.f)
				[
					SNew(SBox).WidthOverride(48.f).HeightOverride(27.f).ToolTipText(FText::FromString(FPaths::GetCleanFilename(File))).AddMetaData<FTagMetaData>(FName(FPaths::GetCleanFilename(File)))
					[
						OKStudioStyle::Fill(TAttribute<bool>::CreateLambda([this, Field, Value] { return Game.IsValid() && Game->Document->Post.*Field == Value; }), Input(),
							// The 2 px rim shows green round the picture shown now.
							SNew(SBox).Padding(2.f)
							[
								SNew(SButton).ButtonStyle(&OKStudioStyle::FlatButton()).ContentPadding(0.f)
								.OnClicked_Lambda([this, Field, Value] { Game->Document->Post.*Field = Value; Changed(); return FReply::Handled(); })
								[SNew(SImage).Image_Lambda([File] { const FSlateBrush* B = FOKStudioThumbnails::Get().File(File, 64); return B ? B : Brush(Input()); })]
							])
					]
				];
			}
			Props->AddSlot().AutoHeight().Padding(0.f, 1.f)[Grid];
		}
		break;
	default:
	{
		// Presets of the whole look: saved as JSON with a picture of the view.
		Label(TEXT("畫面預設"));
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(PresetDirectory() / TEXT("*.json")), true, false);
		Names.Sort();
		for (const FString& N : Names)
		{
			const FString File = PresetDirectory() / N;
			const FString Picture = FPaths::ChangeExtension(File, TEXT("png"));
			Props->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(64.f).HeightOverride(36.f)[SNew(SImage).Image_Lambda([Picture] { const FSlateBrush* B = FOKStudioThumbnails::Get().File(Picture, 96); return B ? B : Brush(Input()); })]]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(3.f, 0.f).VAlign(VAlign_Center)[Button(FText::FromString(FPaths::GetBaseFilename(N)), [this, File]
				{
					FString Text;
					TSharedPtr<FJsonObject> J;
					if (FFileHelper::LoadFileToString(Text, *File) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) && J)
					{
						FOKScenePost Post;
						FJsonObjectConverter::JsonObjectToUStruct(J.ToSharedRef(), &Post);
						Game->Document->Post = Post;
						Changed();
						SetStatus(TEXT("套用畫面預設 ") + FPaths::GetBaseFilename(File));
					}
				}, 0.f, 20.f, false, 10.f)]
				+ SHorizontalBox::Slot().AutoWidth()[Button(FText::FromString(TEXT("刪除")), [this, File, Picture] { IFileManager::Get().Delete(*File); IFileManager::Get().Delete(*Picture); FOKStudioThumbnails::Get().Forget(Picture); DeferRebuild(); }, 34.f, 20.f, false, 9.f)]
			];
		}
		Row(TEXT("名稱"), TextBox([this] { return PresetName; }, [this](const FString& V) { PresetName = V; }, TEXT("預設名稱")), 0.3f);
		Props->AddSlot().AutoHeight().Padding(0.f, 3.f)[Button(FText::FromString(TEXT("把目前畫面存成預設")), [this]
		{
			const FString Name = FPaths::MakeValidFileName(PresetName.TrimStartAndEnd().IsEmpty() ? FDateTime::Now().ToString(TEXT("preset_%Y%m%d_%H%M%S")) : PresetName.TrimStartAndEnd());
			FString Out;
			if (TSharedPtr<FJsonObject> J = FJsonObjectConverter::UStructToJsonObject(Game->Document->Post)) FJsonSerializer::Serialize(J.ToSharedRef(), TJsonWriterFactory<>::Create(&Out));
			IFileManager::Get().MakeDirectory(*PresetDirectory(), true);
			FFileHelper::SaveStringToFile(Out, *(PresetDirectory() / Name + TEXT(".json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			FString Error;
			Game->CaptureToPng(PresetDirectory() / Name + TEXT(".png"), 256, 144, Error);
			FOKStudioThumbnails::Get().Forget(PresetDirectory() / Name + TEXT(".png"));
			SetStatus(TEXT("已存畫面預設 ") + Name);
			DeferRebuild();
		}, 0.f, 22.f)];
		break;
	}
	}
}

TArray<FString> SOKStudioPanel::MaterialProperties(int32 Id) const
{
	// Every parameter of the item's materials, with its kind.
	TSet<FString> Seen;
	TArray<FString> Out;
	const AKKPreviewActor* Shown = Game.IsValid() && Game->SceneActor ? Game->SceneActor->PreviewOf(Id) : nullptr;
	if (!Shown) return Out;
	TInlineComponentArray<UMeshComponent*> Meshes(Shown);
	for (UMeshComponent* C : Meshes)
	{
		const UMeshComponent* Mesh = Cast<UMeshComponent>(C);
		if (!Mesh) continue;
		for (int32 M = 0; M < Mesh->GetNumMaterials(); ++M)
		{
			UMaterialInterface* Material = Mesh->GetMaterial(M);
			if (!Material) continue;
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			auto Add = [&](const TCHAR* Kind) { for (const FMaterialParameterInfo& I : Infos) { const FString S = I.Name.ToString() + TEXT(" ") + Kind; if (!Seen.Contains(S)) { Seen.Add(S); Out.Add(S); } } };
			Material->GetAllScalarParameterInfo(Infos, Ids); Add(TEXT("(float)"));
			Infos.Reset(); Ids.Reset();
			Material->GetAllVectorParameterInfo(Infos, Ids); Add(TEXT("(color)"));
			Infos.Reset(); Ids.Reset();
			Material->GetAllTextureParameterInfo(Infos, Ids); Add(TEXT("(texture)"));
		}
	}
	Out.Sort();
	return Out;
}
