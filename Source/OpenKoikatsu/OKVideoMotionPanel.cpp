#include "OKVideoMotionPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "KKGameRoot.h"
#include "KKRustExtract.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "OKCharacterController.h"
#include "OKMotionComboBox.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SWindow.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <commdlg.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	const TCHAR* const Placeholder = TEXT("選擇影片動作（可輸入文字搜尋）");

	void StyleButton(UButton* Button)
	{
		FButtonStyle Style = Button->GetStyle();
		Style.Normal = FSlateColorBrush(FLinearColor::FromSRGBColor(FColor(24, 82, 132)));
		Style.Hovered = FSlateColorBrush(FLinearColor::FromSRGBColor(FColor(30, 100, 158)));
		Style.Pressed = FSlateColorBrush(FLinearColor::FromSRGBColor(FColor(18, 60, 100)));
		Button->SetStyle(Style);
	}

	UTextBlock* Label(UWidgetTree* Tree, const FString& Text, int32 Size = 14, FLinearColor Color = FLinearColor::White)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
		Block->SetText(FText::FromString(Text));
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetAutoWrapText(true);
		FSlateFontInfo Font = Block->GetFont();
		Font.Size = Size;
		Block->SetFont(Font);
		return Block;
	}

	UButton* Button(UWidgetTree* Tree, const FString& Text, UTextBlock** OutLabel = nullptr)
	{
		UButton* B = Tree->ConstructWidget<UButton>();
		UTextBlock* T = Label(Tree, Text, 15);
		B->SetContent(T);
		StyleButton(B);
		if (OutLabel) *OutLabel = T;
		return B;
	}

	// Text box with its recommended default prefilled and explained.
	UEditableTextBox* Field(UWidgetTree* Tree, UPanelWidget* Column, const FString& Title, const FString& Default, const FString& Hint, const FString& Help)
	{
		Column->AddChild(Label(Tree, Title, 13, FLinearColor(.85f, .9f, 1.f)));
		UEditableTextBox* Box = Tree->ConstructWidget<UEditableTextBox>();
		Box->SetText(FText::FromString(Default));
		Box->SetHintText(FText::FromString(Hint));
		Box->SetToolTipText(FText::FromString(Help));
		Column->AddChild(Box);
		return Box;
	}

	FString VideoMotionFile(const FString& Name, const TCHAR* Extension)
	{
		return FOkSession::VideoMotionDirectory() / (Name + Extension);
	}
}

void UOKVideoMotionPanel::Build(UWidgetTree* Tree, UPanelWidget* Column, UOKCharacterController* InOwner)
{
	Owner = InOwner;
	Column->AddChild(Label(Tree, TEXT("影片動作（自行匯入）"), 18));
	Combo = Tree->ConstructWidget<UOKMotionComboBox>(UOKMotionComboBox::StaticClass(), TEXT("VideoMotionCombo"));
	Combo->SetToolTipText(FText::FromString(TEXT("從影片轉換出的動作，與上方的內建動作分開列出。展開後可輸入文字搜尋。")));
	Combo->OnSelectionChanged.AddDynamic(this, &UOKVideoMotionPanel::OnSelected);
	Column->AddChild(Combo);

	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
	UButton* Browse = Button(Tree, TEXT("選擇影片…"));
	Browse->SetToolTipText(FText::FromString(TEXT("選擇要轉換的影片檔（mp4、mov、mkv、avi、webm）。")));
	Browse->OnClicked.AddDynamic(this, &UOKVideoMotionPanel::OnBrowse);
	Row->AddChildToHorizontalBox(Browse)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UTextBlock* DeleteText = nullptr;
	UButton* Delete = Button(Tree, TEXT("刪除選取的動作"), &DeleteText);
	DeleteLabel = DeleteText;
	Delete->SetToolTipText(FText::FromString(TEXT("刪除下拉選單中選取的影片動作（只限自行匯入的動作，按兩次確認）。")));
	Delete->OnClicked.AddDynamic(this, &UOKVideoMotionPanel::OnDelete);
	Row->AddChildToHorizontalBox(Delete)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Column->AddChild(Row);

	PathBox = Field(Tree, Column, TEXT("影片檔"), FString(), TEXT("按「選擇影片…」或貼上影片完整路徑"),
		TEXT("影片中只拍一個人、全身入鏡、光線充足時效果最好。"));
	NameBox = Field(Tree, Column, TEXT("動作名稱"), FString(), TEXT("預設為影片檔名"),
		TEXT("顯示在影片動作清單中的名稱。不可包含 \\ / : * ? \" < > |。同名會覆蓋。"));
	StaticCamera = Tree->ConstructWidget<UCheckBox>();
	StaticCamera->SetIsChecked(true);
	StaticCamera->SetContent(Label(Tree, TEXT("鏡頭固定不動（建議：腳架或固定機位的影片勾選）"), 13));
	StaticCamera->SetToolTipText(FText::FromString(TEXT("勾選時直接假設鏡頭不動，結果最穩定。手持或跟拍的影片請取消，會另外估算鏡頭移動。")));
	Column->AddChild(StaticCamera);
	PairVideo = Tree->ConstructWidget<UCheckBox>();
	PairVideo->SetIsChecked(false);
	PairVideo->SetContent(Label(Tree, TEXT("雙人影片（一男一女，各自產生動作並貼合身體）"), 13));
	PairVideo->SetToolTipText(FText::FromString(TEXT("兩人貼身互動的影片。會分別估計兩人的動作，對齊到同一個空間並讓身體保持影片中的接觸；需要遊戲目錄中至少一張男性角色卡。播放時女角播女方、已載入的配對男角播男方。")));
	Column->AddChild(PairVideo);
	MaleLeft = Tree->ConstructWidget<UCheckBox>();
	MaleLeft->SetIsChecked(true);
	MaleLeft->SetContent(Label(Tree, TEXT("　男性在畫面左邊（取消＝女性在左）"), 13));
	MaleLeft->SetToolTipText(FText::FromString(TEXT("用來分辨兩人：依整段影片中兩人的平均左右位置判斷。")));
	Column->AddChild(MaleLeft);
	Column->AddChild(Label(Tree, TEXT("進階參數（已預填建議值，一般不需修改）"), 13, FLinearColor(1.f, .92f, .45f)));
	ArmReachBox = Field(Tree, Column, TEXT("手臂比例修正　建議 1.0（範圍 0–1）"), TEXT("1.0"), TEXT("1.0"),
		TEXT("戀活角色手臂比真人長約 20%。1.0：垂下的手落在臀側，手肘自然微彎；0：照抄影片角度，手會垂到大腿中段。"));
	CollarBox = Field(Tree, Column, TEXT("鎖骨幅度　建議 0.5（範圍 0–1）"), TEXT("0.5"), TEXT("0.5"),
		TEXT("影片中鎖骨的轉動通常比原作大，0.5 可避免肩膀被拉伸；聳肩動作很明顯時可提高到 0.8。"));
	SmoothBox = Field(Tree, Column, TEXT("平滑程度　建議 1.0（0 為關閉，最大 2.5）"), TEXT("1.0"), TEXT("1.0"),
		TEXT("去除影片辨識的抖動。數值越大越平順但會吃掉快速動作的細節；轉換時若仍偵測到抖動會自動加強。"));
	FocalBox = Field(Tree, Column, TEXT("鏡頭焦距 mm（全片幅等效）　建議留空＝自動"), FString(), TEXT("留空＝自動估計"),
		TEXT("只有知道拍攝焦距時才填，例如手機主鏡頭約 24、2 倍鏡頭約 48。"));

	UTextBlock* StartText = nullptr;
	UButton* Start = Button(Tree, TEXT("開始轉換"), &StartText);
	StartLabel = StartText;
	Start->SetToolTipText(FText::FromString(TEXT("在背景轉換，期間可繼續操作。轉換會自動修復關節、腳步與比例，並逐幀驗證，完成後自動加入清單並播放。")));
	Start->OnClicked.AddDynamic(this, &UOKVideoMotionPanel::OnStart);
	Column->AddChild(Start);
	Status = Label(Tree, FString(), 13, FLinearColor(1.f, .92f, .45f));
	Column->AddChild(Status);
	Refresh();
	const FString Exe = ImporterExe();
	SetStatus(!Exe.IsEmpty() ? TEXT("選擇影片後按「開始轉換」。")
		: TEXT("找不到影片轉換程式 ok-video.exe，請先建置 Tools/ok-video（cargo-cuda.cmd build --release），見 Docs/VIDEO_MOTION.md。"), Exe.IsEmpty());
}

TArray<UOKVideoMotionPanel::FPair> UOKVideoMotionPanel::ReadPairs(TArray<FString>* Problems)
{
	TArray<FPair> Out;
	const FString Dir = FOkSession::VideoMotionDirectory();
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.json")), true, false);
	Files.Sort();
	for (const FString& File : Files)
	{
		if (!File.EndsWith(TEXT(".pair.json"))) continue;
		FString Json;
		TSharedPtr<FJsonObject> Info;
		if (!FFileHelper::LoadFileToString(Json, *(Dir / File))) { if (Problems) Problems->Add(File + TEXT(": cannot read")); continue; }
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Info) || !Info.IsValid()) { if (Problems) Problems->Add(File + TEXT(": invalid JSON")); continue; }
		FPair Pair;
		Info->TryGetStringField(TEXT("name"), Pair.Name);
		Info->TryGetStringField(TEXT("female"), Pair.Female);
		Info->TryGetStringField(TEXT("male"), Pair.Male);
		Info->TryGetStringField(TEXT("work"), Pair.Work);
		if (Pair.Name.IsEmpty() || Pair.Female.IsEmpty() || Pair.Male.IsEmpty()) { if (Problems) Problems->Add(File + TEXT(": missing name/female/male")); continue; }
		Out.Add(Pair);
	}
	UE_LOG(LogTemp, Display, TEXT("OK video motion pairs=%d dir=%s"), Out.Num(), *Dir);
	return Out;
}

void UOKVideoMotionPanel::Refresh(const FString& Select)
{
	if (!Combo) return;
	TGuardValue<bool> Guard(bRefreshing, true);
	OptionKeys.Reset();
	Pairs.Reset();
	Combo->ClearOptions();
	Combo->AddOption(Placeholder);
	// Pair motions (<name>.pair.json) are one entry; their two clips are not listed alone.
	TSet<FString> Members;
	TArray<FString> Problems;
	for (const FPair& Pair : ReadPairs(&Problems))
	{
		const FString Option = TEXT("【雙人】") + Pair.Name;
		OptionKeys.Add(Option, TEXT("pair:|") + Pair.Name);
		Pairs.Add(Pair.Name, Pair);
		Members.Add(Pair.Female);
		Members.Add(Pair.Male);
		Combo->AddOption(Option);
	}
	for (const FString& Problem : Problems) UE_LOG(LogTemp, Warning, TEXT("OK video motion pair skipped: %s"), *Problem);
	for (const FString& Key : FOkSession::VideoClipNames())
	{
		const FString Name = Key.Mid(7);
		if (Members.Contains(Name)) continue;
		OptionKeys.Add(Name, Key);
		Combo->AddOption(Name);
	}
	int32 Index = Select.IsEmpty() ? INDEX_NONE : Combo->FindOptionIndex(Select);
	if (Index < 0 && !Select.IsEmpty()) Index = Combo->FindOptionIndex(TEXT("【雙人】") + Select);
	Combo->SetSelectedIndex(Index > 0 ? Index : 0);
}

bool UOKVideoMotionPanel::SelectItem(const FString& Item)
{
	if (!Combo || !OptionKeys.Contains(Item)) return false;
	{
		TGuardValue<bool> Guard(bRefreshing, true);
		Combo->SetSelectedOption(Item);
	}
	OnSelected(Item, ESelectInfo::OnMouseClick);
	return true;
}

FString UOKVideoMotionPanel::StatusText() const
{
	return Status ? Status->GetText().ToString() : FString();
}

FString UOKVideoMotionPanel::SelectedKey() const
{
	return Combo ? OptionKeys.FindRef(Combo->GetSelectedOption()) : FString();
}

void UOKVideoMotionPanel::SetStatus(const FString& Text, bool bError)
{
	if (!Status) return;
	Status->SetText(FText::FromString(Text));
	Status->SetColorAndOpacity(FSlateColor(bError ? FLinearColor(1.f, .35f, .25f) : FLinearColor(1.f, .92f, .45f)));
}

void UOKVideoMotionPanel::OnSelected(FString Item, ESelectInfo::Type Type)
{
	// UOKMotionComboBox commits clicks through SetSelectedOption (ESelectInfo::Direct),
	// so only this panel's own list rebuilds are ignored.
	if (bRefreshing || !Owner) return;
	const FString Key = OptionKeys.FindRef(Item);
	if (Key.IsEmpty()) return;
	DeleteArmedUntil = 0.;
	if (DeleteLabel) DeleteLabel->SetText(FText::FromString(TEXT("刪除選取的動作")));
	if (Key.StartsWith(TEXT("pair:|")))
	{
		if (const FPair* Pair = Pairs.Find(Key.Mid(6)); Pair && PlayOverride)
			PlayOverride(TEXT("video:|") + Pair->Female, TEXT("video:|") + Pair->Male, Pair->Name);
		else if (Pair)
			SetStatus(Owner->PlayVideoPair(TEXT("video:|") + Pair->Female, TEXT("video:|") + Pair->Male, Pair->Name));
		return;
	}
	if (PlayOverride) { PlayOverride(Key, FString(), Item); return; }
	if (!Owner->PlayVideoMotion(Key, Item)) SetStatus(TEXT("無法播放：") + Item, true);
}

void UOKVideoMotionPanel::OnBrowse()
{
#if PLATFORM_WINDOWS
	TCHAR File[4096] = {0};
	OPENFILENAMEW Dialog{};
	Dialog.lStructSize = sizeof(Dialog);
	if (GEngine && GEngine->GameViewport)
		if (TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
			if (Window->GetNativeWindow().IsValid()) Dialog.hwndOwner = (HWND)Window->GetNativeWindow()->GetOSWindowHandle();
	Dialog.lpstrFilter = TEXT("影片檔\0*.mp4;*.mov;*.mkv;*.avi;*.webm;*.m4v\0所有檔案\0*.*\0");
	Dialog.lpstrFile = File;
	Dialog.nMaxFile = UE_ARRAY_COUNT(File);
	Dialog.lpstrTitle = TEXT("選擇要轉換成動作的影片");
	Dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&Dialog)) return;
	const FString Path(File);
	PathBox->SetText(FText::FromString(Path));
	NameBox->SetText(FText::FromString(FPaths::GetBaseFilename(Path)));
	SetStatus(TEXT("確認名稱與鏡頭設定後按「開始轉換」。"));
#else
	SetStatus(TEXT("此平台請直接貼上影片路徑。"), true);
#endif
}

FString UOKVideoMotionPanel::ImporterExe()
{
	// Rust converter (Docs/VIDEO_MOTION_NATIVE_PLAN.md): no Python. Development build first, then a staged copy.
	for (const FString& Candidate : {
		FPaths::ProjectDir() / TEXT("Tools/ok-video/target/release/ok-video.exe"),
		FPaths::ProjectDir() / TEXT("Tools/ok-video/ok-video.exe"),
		FPaths::LaunchDir() / TEXT("ok-video/ok-video.exe") })
	{
		const FString Full = FPaths::ConvertRelativePathToFull(Candidate);
		if (FPaths::FileExists(Full)) return Full;
	}
	return FString();
}

bool UOKVideoMotionPanel::StartImport(const FString& Path, const FString& Name, bool bStatic, bool bPair, bool bMaleLeft)
{
	if (Proc.IsValid() || !PathBox || !NameBox) return false;
	PathBox->SetText(FText::FromString(Path));
	NameBox->SetText(FText::FromString(Name));
	if (StaticCamera) StaticCamera->SetIsChecked(bStatic);
	if (PairVideo) PairVideo->SetIsChecked(bPair);
	if (MaleLeft) MaleLeft->SetIsChecked(bMaleLeft);
	OnStart();
	return Proc.IsValid();
}

void UOKVideoMotionPanel::OnStart()
{
	if (Proc.IsValid())
	{
		// Second press while running cancels.
		FPlatformProcess::TerminateProc(Proc, true);
		FinishImport();
		SetStatus(TEXT("已取消轉換。"), true);
		return;
	}
	FString Path = PathBox->GetText().ToString().TrimStartAndEnd().TrimQuotes();
	if (!FPaths::FileExists(Path)) { SetStatus(TEXT("找不到影片檔，請先按「選擇影片…」。"), true); return; }
	FString Name = NameBox->GetText().ToString().TrimStartAndEnd();
	if (Name.IsEmpty()) Name = FPaths::GetBaseFilename(Path);
	for (const TCHAR* Bad : {TEXT("\\"), TEXT("/"), TEXT(":"), TEXT("*"), TEXT("?"), TEXT("\""), TEXT("<"), TEXT(">"), TEXT("|"), TEXT("..")})
		if (Name.Contains(Bad)) { SetStatus(TEXT("動作名稱不可包含 \\ / : * ? \" < > | 或 ..。"), true); return; }
	const FString Exe = ImporterExe();
	if (Exe.IsEmpty())
	{ SetStatus(TEXT("找不到影片轉換程式 ok-video.exe，請先建置 Tools/ok-video（cargo-cuda.cmd build --release），見 Docs/VIDEO_MOTION.md。"), true); return; }
	auto Number = [](UEditableTextBox* Box, float Default, float Lo, float Hi)
	{
		const FString Text = Box->GetText().ToString().TrimStartAndEnd();
		return Text.IsNumeric() ? FMath::Clamp(FCString::Atof(*Text), Lo, Hi) : Default;
	};
	FString Args = FString::Printf(TEXT("import \"%s\" --name \"%s\" --arm-reach %.3f --collar-scale %.3f --smooth %.3f"),
		*Path, *Name, Number(ArmReachBox, 1.f, 0.f, 1.f), Number(CollarBox, .5f, 0.f, 1.f), Number(SmoothBox, 1.f, 0.f, 2.5f));
	if (StaticCamera->IsChecked()) Args += TEXT(" -s");
	const UKKGameRoot* Game = UKKGameRoot::Get();
	const FString Root = Game ? Game->Root : FString();
	if (PairVideo && PairVideo->IsChecked())
	{
		if (Root.IsEmpty() || !FPaths::DirectoryExists(Root / TEXT("UserData/chara/male")))
		{ SetStatus(TEXT("雙人影片需要遊戲目錄中的男性角色卡（UserData/chara/male），請先設定遊戲目錄。"), true); return; }
		Args += FString::Printf(TEXT(" --pair --left %s"), MaleLeft->IsChecked() ? TEXT("male") : TEXT("female"));
	}
	// The game root also supplies a validation card when the bundled one has no portable snapshot.
	if (!Root.IsEmpty()) Args += FString::Printf(TEXT(" --game \"%s\""), *Root);
	const FString Focal = FocalBox->GetText().ToString().TrimStartAndEnd();
	if (Focal.IsNumeric() && FCString::Atoi(*Focal) > 0) Args += FString::Printf(TEXT(" --f_mm %d"), FCString::Atoi(*Focal));
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{ SetStatus(FString::Printf(TEXT("無法建立轉換程序的輸出管道（Win32 錯誤 %u）。"), FPlatformMisc::GetLastError()), true); return; }
	Proc = FPlatformProcess::CreateProc(*Exe, *Args, false, true, true, nullptr, 0, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), WritePipe);
	if (!Proc.IsValid())
	{
		const uint32 Error = FPlatformMisc::GetLastError();
		FinishImport();
		SetStatus(FString::Printf(TEXT("無法啟動轉換程序（CreateProc 失敗，Win32 錯誤 %u）：%s"), Error, *Exe), true);
		return;
	}
	RunningName = Name;
	Pending.Reset();
	LastLine.Reset();
	ErrorLines.Reset();
	if (StartLabel) StartLabel->SetText(FText::FromString(TEXT("取消轉換")));
	SetStatus(FString::Printf(TEXT("轉換中：%s（第一次執行要轉換模型權重並編譯 GPU 程式，約多 1 分鐘）"), *Name));
}

void UOKVideoMotionPanel::ParseOutput(bool bFinal)
{
	Pending += FPlatformProcess::ReadPipe(ReadPipe);
	// tqdm redraws with \r; treat it as a line break.
	Pending.ReplaceInline(TEXT("\r"), TEXT("\n"));
	TArray<FString> Lines;
	Pending.ParseIntoArray(Lines, TEXT("\n"), true);
	if (!bFinal && !Pending.EndsWith(TEXT("\n")) && Lines.Num() > 0) Pending = Lines.Pop(); else Pending.Reset();
	for (const FString& Line : Lines)
	{
		const FString Clean = Line.TrimStartAndEnd();
		// Importer protocol: [步驟]/[資訊]/[1/2]/[2/2] progress, [錯誤] causes
		// (API exception type and message, likely reason, log path), "round N"
		// repair loop. Library logs, warnings and tqdm bars are ignored.
		if (Clean.StartsWith(TEXT("[錯誤]"))) { ErrorLines.Add(Clean.Mid(4).TrimStart()); continue; }
		const bool Stage = Clean.Len() > 4 && Clean[0] == TCHAR('[') && FChar::IsDigit(Clean[1]) && Clean[2] == TCHAR('/');  // [1/3]
		const bool Progress = Stage || Clean.StartsWith(TEXT("[步驟]")) || Clean.StartsWith(TEXT("[資訊]"))
			|| Clean.StartsWith(TEXT("round ")) || Clean.StartsWith(TEXT("wrote ")) || Clean.StartsWith(TEXT("pair fit"));
		if (!Progress) continue;
		LastLine = Clean.Left(200);
		if (!bFinal) SetStatus(FString::Printf(TEXT("轉換中：%s\n%s"), *RunningName, *LastLine));
	}
}

void UOKVideoMotionPanel::Tick()
{
	if (!Proc.IsValid()) return;
	ParseOutput(false);
	if (FPlatformProcess::IsProcRunning(Proc)) return;
	int32 Code = -1;
	FPlatformProcess::GetProcReturnCode(Proc, &Code);
	ParseOutput(true);  // the causes are usually the last lines before exit
	const FString Name = RunningName;
	FinishImport();
	if (Code == 0 && (FPaths::FileExists(VideoMotionFile(Name, TEXT(".okanim"))) || FPaths::FileExists(VideoMotionFile(Name, TEXT(".pair.json")))))
	{
		Refresh(Name);
		SetStatus(FString::Printf(TEXT("完成：%s\n%s"), *Name, *LastLine));
		if (PlayOverride)
		{
			if (const FPair* Pair = Pairs.Find(Name)) PlayOverride(TEXT("video:|") + Pair->Female, TEXT("video:|") + Pair->Male, Name);
			else PlayOverride(TEXT("video:|") + Name, FString(), Name);
		}
		else if (Owner)
		{
			if (const FPair* Pair = Pairs.Find(Name)) Owner->PlayVideoPair(TEXT("video:|") + Pair->Female, TEXT("video:|") + Pair->Male, Name);
			else Owner->PlayVideoMotion(TEXT("video:|") + Name, Name);
		}
		return;
	}
	const FString Detail = ErrorLines.IsEmpty()
		? FString::Printf(TEXT("轉換程序沒有回報原因（可能被中止，或 Python 環境損毀）。最後的進度：%s"), *LastLine)
		: FString::Join(ErrorLines, TEXT("\n"));
	SetStatus(FString::Printf(TEXT("轉換失敗（結束代碼 %d）：%s\n%s"), Code, *Name, *Detail), true);
}

void UOKVideoMotionPanel::FinishImport()
{
	if (Proc.IsValid()) FPlatformProcess::CloseProc(Proc);
	Proc.Reset();
	if (ReadPipe || WritePipe) FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
	ReadPipe = WritePipe = nullptr;
	RunningName.Reset();
	if (StartLabel) StartLabel->SetText(FText::FromString(TEXT("開始轉換")));
}

void UOKVideoMotionPanel::OnDelete()
{
	const FString Key = SelectedKey();
	if (Key.IsEmpty()) { SetStatus(TEXT("先在上方清單選擇要刪除的影片動作。"), true); return; }
	const bool bPair = Key.StartsWith(TEXT("pair:|"));
	const FString Name = Key.Mid(bPair ? 6 : 7);
	const double Now = FPlatformTime::Seconds();
	if (Now > DeleteArmedUntil)
	{
		DeleteArmedUntil = Now + 4.;
		if (DeleteLabel) DeleteLabel->SetText(FText::FromString(TEXT("再按一次確認刪除")));
		SetStatus(FString::Printf(TEXT("確定刪除「%s」？4 秒內再按一次刪除。"), *Name), true);
		return;
	}
	DeleteArmedUntil = 0.;
	if (DeleteLabel) DeleteLabel->SetText(FText::FromString(TEXT("刪除選取的動作")));
	IFileManager& Files = IFileManager::Get();
	auto SafeDir = [](const FString& Dir) { return !Dir.IsEmpty() && !Dir.Contains(TEXT("..")) && !Dir.Contains(TEXT("/")) && !Dir.Contains(TEXT("\\")); };
	if (const FPair* Pair = bPair ? Pairs.Find(Name) : nullptr)
	{
		bool Ok = Files.Delete(*VideoMotionFile(Name, TEXT(".pair.json")), false, true, true);
		for (const FString& Clip : {Pair->Female, Pair->Male})
			for (const TCHAR* Extra : {TEXT(".okanim"), TEXT(".json"), TEXT(".validation.json")}) Files.Delete(*VideoMotionFile(Clip, Extra), false, true, true);
		if (SafeDir(Pair->Work)) Files.DeleteDirectory(*(FOkSession::VideoMotionDirectory() / TEXT("work") / Pair->Work), false, true);
		Refresh();
		SetStatus(Ok ? FString::Printf(TEXT("已刪除雙人動作：%s"), *Name) : FString::Printf(TEXT("刪除失敗：%s"), *Name), !Ok);
		return;
	}
	// The importer records its ASCII working directory (GVHMR cannot use
	// non-ASCII paths) in <name>.json.
	FString Work = Name, Json;
	TSharedPtr<FJsonObject> Info;
	if (FFileHelper::LoadFileToString(Json, *VideoMotionFile(Name, TEXT(".json"))) &&
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Info) && Info.IsValid())
		Info->TryGetStringField(TEXT("work"), Work);
	bool Ok = Files.Delete(*VideoMotionFile(Name, TEXT(".okanim")), false, true, true);
	for (const TCHAR* Extra : {TEXT(".json"), TEXT(".validation.json")}) Files.Delete(*VideoMotionFile(Name, Extra), false, true, true);
	if (!Work.IsEmpty() && !Work.Contains(TEXT("..")) && !Work.Contains(TEXT("/")) && !Work.Contains(TEXT("\\")))
		Files.DeleteDirectory(*(FOkSession::VideoMotionDirectory() / TEXT("work") / Work), false, true);
	Refresh();
	SetStatus(Ok ? FString::Printf(TEXT("已刪除：%s"), *Name) : FString::Printf(TEXT("刪除失敗：%s"), *Name), !Ok);
}

void UOKVideoMotionPanel::BeginDestroy()
{
	if (Proc.IsValid()) FPlatformProcess::TerminateProc(Proc, true);
	FinishImport();
	Super::BeginDestroy();
}
