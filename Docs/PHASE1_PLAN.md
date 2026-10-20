# OpenKoikatsu 第一階段：PNG 人物建模工具與 Viewer

> **給實作代理：** 必用子技能 superpowers:subagent-driven-development（建議）或 superpowers:executing-plans，依任務逐步實作。步驟用 checkbox（`- [ ]`）追蹤。

**目標：** 用 Unreal Engine 5.8 開一個全新的開源專案，能讀 Koikatsu / Koikatsu Sunshine 人物 PNG 卡、顯示 2D 卡片牆、用使用者自己的遊戲＋模組安裝重建 3D 人物，並把卡片寫回去。

**架構：** 原作留在 `C:\portable\KoikatuSunshine`，只當唯讀相容資料來源。在隔壁建 UE 5.8 專案，git 裡永遠不放 Illusion 的模型／貼圖。卡片讀寫是 C++ plugin（格式參考 KKAFIO / kkloader）。3D 組裝在執行期從 `abdata` 與 `mods/*.zipmod` 載入 Unity 2019.4 AssetBundle，再套用 ChaFile 的臉／身體／頭髮／衣服 ID 與塑形數值。

**技術棧：** Unreal Engine 5.8（`E:\PhysX\UE5.8`）、C++ plugin、MessagePack、UnityPy（只用於離線目錄／探測，不進正式 runtime）、使用者的 KKS 安裝當可選資源來源。

**規格：** 這份計畫只涵蓋重製的第一階段。Studio、學園／ADV、原創開源身體等後續階段，要等這個 viewer／捏人工具能真正載入並儲存一張卡之後才開始。

## 全域約束

- 引擎用 `Tools\ok_engine.ps1` 指定的 Unreal Engine 5.8（2026-10-15 起預設 `E:\PhysX\UE5.8`，見 `Docs\PHYSX_ENGINE_PORT_PLAN.md`），不准換主版本。
- 新專案路徑：`C:\portable\OpenKoikatsu`（跟遊戲目錄同級，絕對不要建在 `C:\portable\KoikatuSunshine` 裡面）。
- 開源 repo 不准提交、複製、散布 `abdata/`、`KoikatsuSunshine_Data/`、`mods/`、`UserData/` 裡的檔案。
- 遊戲安裝路徑是使用者設定的 `GameRoot`。本機預設：`C:\portable\KoikatuSunshine`。
- PNG 卡維持交換格式。沒改過的卡片 round-trip 必須位元組完全一致。
- 同時支援 `【KoiKatuChara】`（KK）與 `【KoiKatuCharaSun】`（KKS）標記。
- 原作有成人部位；第一階段顯示卡片實際引用的部件，之後再加 SFW 隱藏開關。不准把原作成人貼圖打進發行包。
- 新 repo 授權 MIT 或 Apache-2.0。解析器自己重寫；除非授權相容並註明出處，否則不要直接搬 KKAFIO / KKTools 原始碼。
- Python（`kkloader`、UnityPy）只准用在測試與目錄產生，不能當正式 viewer runtime。

---

## 為什麼要開新專案，而不是改原作

Illusion 已經倒了。目前這個資料夾是 Unity 2019.4.9 遊戲，加上 BepInEx 與約 5600 個 zipmod。真正的重製不能繼續把那個封閉 runtime 當引擎。社群真正每天在用的東西是 **人物卡**，所以開源的第一個產品從這裡做起。

原作捏人管線：

```
PNG 卡  -->  ChaFile（IEND 後面的 MessagePack）
        -->  UniversalAutoResolver（KKEx 的 GUID）
        -->  list/characustom 槽位 -> 資源路徑
        -->  Unity AssetBundle 模型／貼圖
        -->  morph、顏色、overlay
        -->  3D 人物
```

第一階段在 UE5 重做這條管線，指向使用者自己擁有的檔案。

---

## 現有開源工具調查

**沒有**任何獨立 3D Koikatsu 人物 viewer，能在不啟動原作的情況下從 PNG 重建模型。現有工具分成兩類。

### 2D 卡片讀寫與管理（借概念，不是 3D）

| 專案 | 實際在做什麼 | 我們怎麼用 |
|---|---|---|
| [KKAFIO](https://github.com/RedDeadDepresso/KKAFIO) | 下載／備份／過濾／KK↔KKS 轉換、用 IEND 後 payload 抓重複、從 KKEx UAR 抽 GUID、zipmod 索引、服裝卡顏色指紋 | **主要 I/O 參考。** 把 `classifier.py` 與 `chara_ops.py` 的邏輯 port 成 C++。 |
| [kkloader / KoikatuCharaLoader](https://github.com/great-majority/KoikatuCharaLoader) | 完整 ChaFile 反序列化／序列化（`Custom`、`Coordinate`、`Parameter`、`Status`、`KKEx`） | **主要 schema 參考。** `temp/dump_card*.py` 已經在用。 |
| [KKTools](https://github.com/galact-byte/KKTools) | PyQt6 的 2D 卡片牆＋欄位編輯＋缺 mod 掃描 | 卡片牆／檢查器的 UX 參考。 |
| [kk-snippets chara-data-viewer](https://kk-snippets.streamlit.app/chara-data-viewer) | 瀏覽器裡把卡片 dump 成 JSON | 檢查器版面參考。 |
| [PluginDataReader](https://github.com/jim60105/PluginDataReader) | Blazor PWA，dump KKEx plugin 資料 | KKEx 涵蓋範圍清單。 |
| [KoiCatalog](https://github.com/im-mi/KoiCatalog) | 舊的 .NET 卡片目錄 | 較弱，略過。 |
| [KK Library](https://kklibrary.dev/) | 本機場景／卡片管理器 | 桌面工具，不是 3D 重製。 |
| 本資料夾裡的 KKManager | 原作的 mod／plugin／卡片管理器 | 繼續用在遊戲安裝上，不要重做一份。 |

### 3D 重建（全部仍要跑原作 Unity）

| 專案 | 實際在做什麼 | 我們怎麼用 |
|---|---|---|
| [KKBP Exporter + Importer](https://gitgoon.dev/kkbp-dev/KKBP_Importer) | 遊戲內 BepInEx 把模型／貼圖／骨架 dump 出來 → Blender → 再轉 FBX 給 UE／Unity | **可選的精度對照**，不是重製的 runtime。證明 3D 匯出做得到。 |
| IllusionMods KoikatsuModdingTools | 給原作做 zipmod 的 Unity 5.6／2019.4 編輯器 | 舊遊戲的資源製作，不是 viewer。 |

**結論：** 卡片格式跟 KKAFIO + kkloader 學；3D 組裝跟 KKBP／原作 maker 的槽位解析學。但 UE5 viewer 必須自己載入 bundle，否則重製會永遠被死掉的 exe 卡住。

---

## 建議做法（選這個）

考慮過三種：

1. **只做 KKBP 橋接** — 原作匯出 FBX，UE5 只負責顯示。  
   3D 來得快，但永遠依賴 `KoikatsuSunshine.exe`。這不是重製。
2. **只做 2D 的 Python／Qt，複製 KKAFIO／KKTools。**  
   很快就能用，對 UE5 重製零進展。
3. **UE5 原生管線（建議）** — 先做 C++ 卡片 I/O 與 2D 卡片牆，再在 runtime 把 Unity bundle 轉成 UE mesh，從 `GameRoot` 組人。  
   比較難，但每週產出的程式就是重製真正需要的東西。

第一階段用切片交貨，每一片都能打開來玩：

```
切片 A：空的 UE5 專案 + 讀卡 + 2D 卡片牆
切片 B：GUID／zipmod／缺 mod 報告
切片 C：一顆原作身體＋頭在 UE5 裡出現
切片 D：把卡片的臉／身體塑形套上去
切片 E：頭髮＋目前服裝，並能存 PNG
```

KKBP 只當 **除錯對照**（同一張卡匯出後在 UE5 疊著看），不當產品路徑。

---

## 法律／開源規則（不可談）

- 為了互通而逆向 **PNG 卡片格式** 與 **AssetBundle 配置** 在範圍內。
- 把 Illusion 的 `abdata` 模型、貼圖、音訊、地圖、UI 美術打進發行包 **不在範圍內，而且禁止**。
- 開源程式可以在執行期 *讀取* 使用者合法擁有的安裝，就像音樂播放器讀使用者自己的 MP3。
- 後續階段再做原創替代身體／頭，讓沒有 Illusion 安裝也能看 3D。這不是第一階段。
- `UserData/chara` 裡的 PNG 是使用者資料。測試用合成小卡，加上一張本機忽略的真實卡路徑。

---

## 檔案配置（新 repo）

在 `C:\portable\OpenKoikatsu/` 建立下列結構。下面這些東西都不准放進遊戲目錄。

```
C:\portable\OpenKoikatsu\
  OpenKoikatsu.uproject
  README.md
  LICENSE
  .gitignore                 # 忽略 Saved/、Intermediate/、DerivedDataCache/、本機 GameRoot.ini
  Config\
    DefaultEngine.ini
    DefaultGame.ini
    DefaultInput.ini
  Content\
    UI\                      # UMG：卡片牆、檢查器、捏人側欄
    Viewer\                  # 預設燈光、地板、相機 pawn
    Placeholder\             # 開源灰色身體（不是 Illusion 美術）
  Source\
    OpenKoikatsu\
      OpenKoikatsu.Target.cs
      OpenKoikatsuEditor.Target.cs
      OpenKoikatsu\
        OpenKoikatsu.Build.cs
        OpenKoikatsuGameMode.cpp/.h
  Plugins\
    KKCard\
      Source/KKCard\
        Public/KKCardTypes.h
        Public/KKCardReader.h
        Public/KKCardWriter.h
        Private/KKCardReader.cpp
        Private/KKPng.cpp          # IEND 走訪、7-bit 字串
        Private/KKMsgpack.cpp
        Tests/KKCard.spec.cpp
    KKSideloader\
      Source/KKSideloader\
        Public/KKGameRoot.h
        Public/KKModIndex.h        # zipmod GUID -> 路徑
        Public/KKResolver.h        # UAR ResolveInfo
        Private/...
    KKUnityBundle\
      Source/KKUnityBundle\
        Public/KKBundleCatalog.h   # list/characustom 槽位 -> bundle 路徑
        Public/KKBundleLoader.h    # UnityFS -> 原始 mesh/tex
        Public/KKMeshConvert.h     # Unity mesh -> UProceduralMesh / RuntimeMesh
        Private/...
    KKCharacter\
      Source/KKCharacter\
        Public/KKCharacterActor.h  # 組好的人物
        Public/KKShapeApplier.h    # shapeValueFace / shapeValueBody
        Public/KKMakerState.h
        Private/...
  Tools\
    python\
      test_cards\                  # 提交用的合成 PNG
      dump_golden.py               # 用 kkloader 寫出預期 JSON
      catalog_characustom.py       # UnityPy 探測：把槽位表 dump 成 JSON
  Docs\
    CARD_FORMAT.md
    ASSET_PIPELINE.md
```

---

## 資料流

```
UserData/chara/*.png
        |
        v
 +--------------+     +------------------+
 | KKCardReader | --> | FKKCharacterFile |
 | PNG + msgpack|     | Custom/Coord/    |
 +--------------+     | Param/Status/KKEx|
        |             +--------+---------+
        |                      |
        |                      v
        |             +------------------+
        |             | KKResolver       |
        |             | KKEx UAR info[]  |
        |             | ModID + Slot +   |
        |             | Property         |
        |             +--------+---------+
        |                      |
        v                      v
 +--------------+     +------------------+     GameRoot/abdata
 | 2D 卡片牆    |     | KKBundleCatalog  | <-- GameRoot/mods/*.zipmod
 | 預覽 PNG     |     | 分類+槽位 ->     |     list/characustom/*.unity3d
 | 檢查器       |     | bundle + 資源名  |
 +--------------+     +--------+---------+
                               |
                               v
                      +------------------+
                      | KKBundleLoader   |
                      | Unity 2019.4 FS  |
                      | mesh / tex / mb  |
                      +--------+---------+
                               |
                               v
                      +------------------+
                      | KKCharacterActor |
                      | 掛上部件         |
                      | 套用 shapekey    |
                      | 顏色／overlay    |
                      +--------+---------+
                               |
                               v
                      UE5 Viewer + 捏人滑桿
                               |
                               v
                      KKCardWriter --> PNG
```

磁碟上的卡片配置（來自 KKAFIO `chara_ops.py` 與本機 `temp/parse_kks_card.py`）：

1. 標準 PNG，直到 IEND chunk（預覽圖）。
2. `int32 product_no`（100）。
3. 7-bit 長度字首的標記：`【KoiKatuChara】`／`【KoiKatuCharaSun】`／`【KoiKatuCharaSP】`。
4. 版本字串。
5. 臉部 PNG blob（`int32` 長度 + bytes）— 捏人臉部裁切圖。
6. MessagePack block header（`lstInfo`：name、pos、size）。
7. payload：`Custom`、`Coordinate`、`Parameter`、`Status`、可選 `About`、可選 `KKEx`。
8. `KKEx` 是 plugin map。鍵 `com.bepis.sideloader.universalautoresolver` 裡有 `info[]`，每筆是 MessagePack `ResolveInfo`（`ModID`、`Slot`、`LocalSlot`、`Property`、`CategoryNo`）。

本機已驗證：`temp/card_dump/resolve_info.json` 就是 `UserData/chara/female/KK_398374.png` 的 dump。

---

### 任務 1：建立 UE 5.8 專案與 git repo

**檔案：**
- 新增：`C:\portable\OpenKoikatsu\OpenKoikatsu.uproject`
- 新增：`C:\portable\OpenKoikatsu\README.md`
- 新增：`C:\portable\OpenKoikatsu\LICENSE`
- 新增：`C:\portable\OpenKoikatsu\.gitignore`
- 新增：`C:\portable\OpenKoikatsu\Source\OpenKoikatsu\OpenKoikatsu.Target.cs`
- 新增：`C:\portable\OpenKoikatsu\Source\OpenKoikatsuEditor.Target.cs`
- 新增：`C:\portable\OpenKoikatsu\Source\OpenKoikatsu\OpenKoikatsu\OpenKoikatsu.Build.cs`
- 新增：`C:\portable\OpenKoikatsu\Config\DefaultEngine.ini`
- 新增：`C:\portable\OpenKoikatsu\Config\DefaultGame.ini`

**介面：**
- 使用：`E:\PhysX\UE5.8` 引擎
- 產出：`E:\PhysX\UE5.8\Engine\Binaries\Win64\UnrealEditor.exe` 開得起來的專案

- [ ] **步驟 1：寫 uproject**

`OpenKoikatsu.uproject`：

```json
{
  "FileVersion": 3,
  "EngineAssociation": "",
  "Category": "",
  "Description": "Open-source Koikatsu Sunshine character maker and viewer",
  "Modules": [
    {
      "Name": "OpenKoikatsu",
      "Type": "Runtime",
      "LoadingPhase": "Default"
    }
  ],
  "Plugins": [
    { "Name": "KKCard", "Enabled": true }
  ]
}
```

引擎關聯不要寫死 marketplace GUID。用這個指令開：

```
E:\PhysX\UE5.8\Engine\Binaries\Win64\UnrealEditor.exe C:\portable\OpenKoikatsu\OpenKoikatsu.uproject
```

- [ ] **步驟 2：寫 Target 與空白 GameMode**

`OpenKoikatsu.Target.cs` 類型 `Game`，額外模組 `OpenKoikatsu`。
`OpenKoikatsuEditor.Target.cs` 類型 `Editor`。
Game 模組依賴 `Core`、`CoreUObject`、`Engine`、`InputCore`、`UMG`、`Slate`、`SlateCore`。

- [ ] **步驟 3：只在新資料夾 git init**

```
cd C:\portable\OpenKoikatsu
git init
```

`.gitignore` 必須包含：`Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/`、`Build/`、`.vs/`、`*.sln`、`*.suo`、本機 `Saved/Config/WindowsEditor/GameRoot.ini`。

**不要** 在 `C:\portable\KoikatuSunshine` 裡 `git init`。

- [ ] **步驟 4：用編輯器打開，確認空白 PIE 能跑**

```
E:\PhysX\UE5.8\Engine\Binaries\Win64\UnrealEditor.exe C:\portable\OpenKoikatsu\OpenKoikatsu.uproject
```

預期：編輯器載入、預設地圖能 Play、沒有 plugin 錯誤。

- [ ] **步驟 5：提交**

```
git add OpenKoikatsu.uproject Source Config README.md LICENSE .gitignore
git commit -m "chore: scaffold UE 5.8 OpenKoikatsu project"
```

---

### 任務 2：KKCard plugin — PNG IEND + 標記 + block header

**檔案：**
- 新增：`Plugins/KKCard/KKCard.uplugin`
- 新增：`Plugins/KKCard/Source/KKCard/KKCard.Build.cs`
- 新增：`Plugins/KKCard/Source/KKCard/Public/KKCardTypes.h`
- 新增：`Plugins/KKCard/Source/KKCard/Public/KKPng.h`
- 新增：`Plugins/KKCard/Source/KKCard/Private/KKPng.cpp`
- 新增：`Plugins/KKCard/Source/KKCard/Public/KKCardReader.h`
- 新增：`Plugins/KKCard/Source/KKCard/Private/KKCardReader.cpp`
- 測試：`Plugins/KKCard/Source/KKCard/Private/Tests/KKPng.spec.cpp`
- 測試資料：`Tools/python/test_cards/minimal_kks.png`（產生的，不是從 UserData 拿）

**介面：**
- 使用：`.png` 路徑或 byte 陣列
- 產出：

```cpp
enum class EKKCardKind : uint8 { Unknown, KK, KKSP, KKS, Coordinate, Scene };

struct FKKCardHeader {
  int32 ProductNo = 0;
  FString Marker;     // 【KoiKatuCharaSun】
  FString Version;
  EKKCardKind Kind = EKKCardKind::Unknown;
  int32 PngEndOffset = 0;
  TArray<uint8> PreviewPng;
  TArray<uint8> FacePng;
};

struct FKKBlockInfo {
  FString Name;  // Custom, Coordinate, Parameter, Status, About, KKEx
  int32 Pos = 0;
  int32 Size = 0;
};

bool FindPngIendEnd(const TArray<uint8>& Bytes, int32& OutEnd);
EKKCardKind ClassifyCard(const TArray<uint8>& Bytes);
bool Read7BitString(FMemoryReader& Reader, FString& Out);
bool ParseCardHeader(const TArray<uint8>& Bytes, FKKCardHeader& OutHeader, TArray<FKKBlockInfo>& OutBlocks, FString& OutError);
```

分類規則（來自 KKAFIO `classifier.py`）：

- `KoiKatuClothes` → Coordinate
- `KoiKatuCharaSun` → KKS
- `KoiKatuCharaSP` → KKSP
- `KoiKatuChara` → KK
- `sceneInfo` + 人物標記 → Scene（卡片牆會列，第一階段不載 3D）

- [ ] **步驟 1：寫會失敗的 header 測試**

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKKPngIendTest, "OpenKoikatsu.KKCard.Iend",
  EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKKPngIendTest::RunTest(const FString& Parameters)
{
  TArray<uint8> Bytes;
  FFileHelper::LoadFileToArray(Bytes, TEXT("Tools/python/test_cards/minimal_kks.png"));
  int32 End = -1;
  TestTrue(TEXT("IEND found"), FindPngIendEnd(Bytes, End));
  TestTrue(TEXT("payload after IEND"), End > 0 && End < Bytes.Num());

  FKKCardHeader Header;
  TArray<FKKBlockInfo> Blocks;
  FString Err;
  TestTrue(TEXT("header parses"), ParseCardHeader(Bytes, Header, Blocks, Err));
  TestEqual(TEXT("kind"), Header.Kind, EKKCardKind::KKS);
  TestTrue(TEXT("has Custom"), Blocks.ContainsByPredicate([](const FKKBlockInfo& B){ return B.Name == TEXT("Custom"); }));
  return true;
}
```

- [ ] **步驟 2：用 kkloader 產生合成測試卡**

`Tools/python/dump_golden.py` 載入一張很小的構造卡（或在測試時把 *本機忽略* 的真實卡複製到 `Saved/TestCards/`）。提交的 fixture 必須是合成的：合法 PNG + `【KoiKatuCharaSun】` + 幾乎空的 msgpack block。

- [ ] **步驟 3：實作 PNG IEND 走訪**

從 offset 8 走 PNG chunk：`length(uint32 BE) + type(4) + data + crc(4)`。遇到 `IEND` 停。回傳 CRC 後面的 offset。這跟 KKAFIO `_find_iend_end` 一樣。

- [ ] **步驟 4：實作 .NET 7-bit 字串 + header 解析**

IEND 之後：

```
product_no : int32 LE
marker     : 7-bit 長度 + UTF-8
version    : 7-bit 長度 + UTF-8
face_len   : int32 LE
face_png   : face_len bytes
bh_len     : int32 LE
block_header : 帶 lstInfo[] 的 msgpack map
total_size : int64 LE
payload    : 原始 bytes，用 lstInfo 的 pos/size 切片
```

- [ ] **步驟 5：跑 automation 測試**

編輯器：Session Frontend → Automation → `OpenKoikatsu.KKCard`。預期：PASS。

另外對真實卡路徑做手動確認（不提交）：

`C:\portable\KoikatuSunshine\UserData\chara\female\` 第一張 — 預期 Kind=KKS 或 KK，blocks 含 Custom／Coordinate／Parameter。

- [ ] **步驟 6：提交**

```
git commit -m "feat(KKCard): parse PNG IEND, card marker, and block header"
```

---

### 任務 3：反序列化 Custom／Parameter／Coordinate／KKEx

**檔案：**
- 新增：`Plugins/KKCard/Source/KKCard/Public/KKCharacterFile.h`
- 新增：`Plugins/KKCard/Source/KKCard/Private/KKCharacterFile.cpp`
- 新增：`Plugins/KKCard/Source/KKCard/Private/KKMsgpack.cpp`
- 測試：`Plugins/KKCard/Source/KKCard/Private/Tests/KKCharacterFile.spec.cpp`

**介面：**
- 使用：`ParseCardHeader` 輸出 + payload bytes
- 產出：

```cpp
struct FKKColor { FLinearColor Value; };

struct FKKFace {
  TArray<float> ShapeValueFace; // KKS 是 52 個 float
  int32 HeadId = 0;
  int32 SkinId = 0;
  int32 DetailId = 0;
  int32 EyebrowId = 0;
  int32 NoseId = 0;
  int32 HlUpId = 0;
  int32 HlDownId = 0;
  int32 EyelineUpId = 0;
  int32 EyelineDownId = 0;
  int32 WhiteId = 0;
  int32 MoleId = 0;
  int32 LipLineId = 0;
  // pupil[2]、makeup、顏色 — 見 temp/card_dump/face.json
};

struct FKKBody {
  TArray<float> ShapeValueBody;
  int32 SkinId = 0;
  int32 DetailId = 0;
  int32 SunburnId = 0;
  int32 NipId = 0;
  int32 UnderhairId = 0;
};

struct FKKHairPart { int32 Id = 0; FLinearColor BaseColor; FLinearColor StartColor; FLinearColor EndColor; };
struct FKKHair { FKKHairPart Front, Back, Side, Extension; };

struct FKKClothesPart { int32 Id = 0; /* colorInfo[] 之後再做 */ };
struct FKKAccessoryPart { int32 Type = 0; int32 Id = 0; FName ParentKey; bool bUsed = false; };

struct FKKCoordinateSlot {
  TArray<FKKClothesPart> Clothes;     // top, bot, bra, shorts, gloves, panst, socks, shoes
  TArray<FKKAccessoryPart> Accessories;
};

struct FKKResolveInfo {
  FString ModID;
  int32 Slot = 0;
  int32 LocalSlot = 0;
  FString Property;   // ChaFileFace.headId
  int32 CategoryNo = 0;
  FString Author;
  FString Name;
};

struct FKKCharacterFile {
  FKKCardHeader Header;
  FKKFace Face;
  FKKBody Body;
  FKKHair Hair;
  TArray<FKKCoordinateSlot> Outfits; // KKS 有 7 個學園日槽
  FString LastName, FirstName, Nickname;
  int32 Sex = 1;
  int32 Personality = 0;
  TArray<FKKResolveInfo> Resolves;
  TMap<FString, TArray<uint8>> RawBlocks; // 用來做位元組一致的重寫
};

UCLASS()
class UKKCardReader : public UObject {
  GENERATED_BODY()
public:
  UFUNCTION(BlueprintCallable)
  static bool LoadFromFile(const FString& Path, FKKCharacterFile& OutFile, FString& OutError);
};
```

`temp/card_dump/face.json` 對 `KK_398374.png` 的黃金值（只在本機測，路徑走 `GameRoot`）：

- `shapeValueFace` 長度 52
- `headId` 1（UAR：ModID `HeadMod`，Slot 1，CategoryNo 100）
- `hlUpId` 1919，`eyelineUpId` 1880

- [ ] **步驟 1：寫會失敗的測試，確認 LoadFromFile 填得出 LastName 與 Face.HeadId**
- [ ] **步驟 2：引入小型 C++ MessagePack reader（或把 MIT 的 `msgpack-c` 合併進 ThirdParty）。不要拉巨大的 Boost 依賴。**
- [ ] **步驟 3：解析 Custom map：`face`、`body`、`hair`。解析 Parameter：`lastname`、`firstname`、`nickname`、`sex`、`personality`。**
- [ ] **步驟 4：把 KKEx UAR `info[]` 解析成 `Resolves`，走法比照 KKAFIO `_extract_guids_from_kkex`。**
- [ ] **步驟 5：跑 automation 測試，可選再跑本機真實卡。提交。**

```
git commit -m "feat(KKCard): deserialize Custom, Parameter, Coordinate, KKEx UAR"
```

---

### 任務 4：位元組一致的寫回 + 2D 卡片牆

**檔案：**
- 新增：`Plugins/KKCard/Source/KKCard/Public/KKCardWriter.h`
- 新增：`Plugins/KKCard/Source/KKCard/Private/KKCardWriter.cpp`
- 新增：`Content/UI/WBP_CardGallery.uasset`（UMG）
- 新增：`Content/UI/WBP_CardInspector.uasset`
- 新增：`Source/OpenKoikatsu/OpenKoikatsu/CardGalleryPlayerController.cpp/.h`
- 修改：預設地圖 Play 時打開卡片牆

**介面：**
- 使用：`FKKCharacterFile` 加上 `RawBlocks`
- 產出：磁碟上的 PNG；`UserData/chara/**/*.png` 的卡片牆

```cpp
bool SaveUnchanged(const FKKCharacterFile& File, const FString& Path); // 沒改就 memcpy 原檔
bool SaveWithEdits(FKKCharacterFile& File, const FString& Path, FString& OutError);

UCLASS()
class UKKCardLibrary : public UObject {
  GENERATED_BODY()
public:
  UFUNCTION(BlueprintCallable)
  static TArray<FKKCardListEntry> ScanFolder(const FString& Folder);
};
```

`FKKCardListEntry`：路徑、種類、預覽貼圖（只解 PNG IHDR..IDAT）、姓／名／暱稱、性別、性格。

- [ ] **步驟 1：會失敗的測試 — 載入合成卡、SaveUnchanged、hash 相同。**
- [ ] **步驟 2：實作 writer。欄位沒改就寫回原 bytes。只改 Parameter 名字時，只重建 Parameter msgpack，其他 block 原樣保留。**
- [ ] **步驟 3：UMG 卡片牆：資料夾選擇預設 `GameRoot/UserData/chara/female`。虛擬化 tile view（這份安裝約 5000 張女卡；不准一次生 5000 個 widget）。分頁或 Slate tile view，縮圖非同步解碼。**
- [ ] **步驟 4：檢查器：預覽圖、臉部裁切、名字、性格、服裝槽清單、UAR GUID 清單（缺 mod 之後標紅）。**
- [ ] **步驟 5：PIE：掃描本機 female 資料夾，點一張卡，名字與 kkloader dump 一致。提交。**

```
git commit -m "feat: 2D card gallery, inspector, and byte-identical save"
```

這一切片本身就是能用的開源 2D viewer，能力接近 KKTools，但跑在 UE5 裡。

---

### 任務 5：GameRoot + zipmod GUID 索引（port KKAFIO）

**檔案：**
- 新增：`Plugins/KKSideloader/KKSideloader.uplugin`
- 新增：`Plugins/KKSideloader/Source/KKSideloader/Public/KKGameRoot.h`
- 新增：`Plugins/KKSideloader/Source/KKSideloader/Public/KKModIndex.h`
- 新增：`Plugins/KKSideloader/Source/KKSideloader/Private/KKModIndex.cpp`

**介面：**
- 使用：`GameRoot` 路徑、`FKKCharacterFile.Resolves`
- 產出：

```cpp
UCLASS(Config=Game)
class UKKGameRoot : public UObject {
  GENERATED_BODY()
public:
  UPROPERTY(Config, EditAnywhere)
  FDirectoryPath Root; // C:\portable\KoikatuSunshine

  FString Abdata() const;   // Root/abdata
  FString Mods() const;     // Root/mods
  FString CharaFemale() const;
  FString Coordinate() const;
};

struct FKKZipmod {
  FString Guid;
  FString Path;
  FString Name;
  bool bInSideloaderModpack = false;
};

class FKKModIndex {
public:
  void Build(const FString& ModsDir);          // 掃 *.zipmod，讀 manifest.xml <guid>
  const FKKZipmod* Find(const FString& Guid) const;
  TArray<FKKResolveInfo> Missing(const TArray<FKKResolveInfo>& Resolves) const;
};
```

Zipmod GUID：zip 裡檔名以 `manifest.xml` 結尾的項目，XML 標籤 `<guid>`。不要每次把 Sideloader Modpack 裡每個檔都掃一遍；快取 `Saved/mod_index.json`，用 mods 目錄 mtime 當 key，概念同 KKAFIO `kkafio_mods_cache.json`。

- [ ] **步驟 1：用 `Tools/python/test_cards/fake.guid.zipmod` 寫會失敗的測試**
- [ ] **步驟 2：實作 zip + XML guid 解析**
- [ ] **步驟 3：檢查器把缺少的 GUID 塗紅、找到的塗綠**
- [ ] **步驟 4：對本機 `mods/`（約 5600 個 zipmod）建一次索引並記錄時間。第二次建置必須 < 1 秒。**
- [ ] **步驟 5：提交**

```
git commit -m "feat(KKSideloader): GameRoot config and zipmod GUID index"
```

---

### 任務 6：characustom 目錄（槽位 → bundle 路徑）

**檔案：**
- 新增：`Plugins/KKUnityBundle/KKUnityBundle.uplugin`
- 新增：`Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKBundleCatalog.h`
- 新增：`Tools/python/catalog_characustom.py`
- 新增：`Docs/ASSET_PIPELINE.md`

原作列表在 `GameRoot/abdata/list/characustom/*.unity3d`。Zipmod 會再加列表。本機 dump 看到的分類編號：

| CategoryNo | Property | Bundle 前綴 |
|---|---|---|
| 100 | ChaFileFace.headId | `chara/bo_head_*.unity3d`／頭模組 |
| 101 | ChaFileHair.HairBack | `chara/bo_hair_b_*.unity3d` |
| 105 | ClothesTop | `chara/co_top_*.unity3d` |
| 106 | ClothesBot | `chara/co_bot_*.unity3d` |
| 401 | eyeshadow | `chara/mt_eyeshadow_*.unity3d` |
| 410/411 | hlUp/hlDown | `chara/mt_eye_hi_*.unity3d` |

- [ ] **步驟 1：用 UnityPy 探測（不進發行包）：把一份 `00.unity3d` 列表 dump 成 JSON。在 ASSET_PIPELINE.md 寫下列資料 schema。**
- [ ] **步驟 2：C++ 目錄載入該 JSON，*或* 直接從 bundle 讀 list TextAsset。優先執行期讀取，這樣就不用提交 dump 出來的 Illusion 列表。**
- [ ] **步驟 3：給定 `headId` + ResolveInfo，回傳 `{BundlePath, AssetName}`。原作 ID 在 `abdata/chara` 下解析。模組 ID 經 zipmod 內部的 `abdata/` 解析。**
- [ ] **步驟 4：測試：KK_398374 的 HeadMod slot 1 能解析到 zipmod 或 abdata 裡的真實路徑。提交。**

```
git commit -m "feat(KKUnityBundle): characustom slot catalog from GameRoot"
```

---

### 任務 7：Unity 2019.4 AssetBundle → UE mesh／貼圖（一顆頭 + 一具身體）

**檔案：**
- 新增：`Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKBundleLoader.h`
- 新增：`Plugins/KKUnityBundle/Source/KKUnityBundle/Private/UnityFS.cpp`（UnityFS header、blocks、nodes）
- 新增：`Plugins/KKUnityBundle/Source/KKUnityBundle/Private/SerializedFile.cpp`
- 新增：`Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKMeshConvert.h`
- 新增：`Content/Placeholder/SM_GrayBody.uasset`（簡單開源膠囊／身體，MIT）

**介面：**
- 使用：bundle 路徑（散裝 `.unity3d` 或 zipmod 裡的 bytes）+ 資源名
- 產出：執行期的 `UTexture2D*` 與程序化 `UStaticMesh`／`USkeletalMesh`

KKS 的 Unity 版本是 **2019.4.9**。Bundle 是 UnityFS。TypeTree 常常被剝掉；UnityPy 內建的 2019.4 class database 是參考。最少 port UnityPy／AssetStudio 的：Texture2D + Mesh + SkinnedMeshRenderer + Transform。Shader 跳過，改用 UE 的 toon／lit material。

- [ ] **步驟 1：探測（Python，用完可丟）：UnityPy 載入 `abdata/chara/oo_base.unity3d` 與 `bo_head_00.unity3d`，匯出一顆 mesh 成 OBJ、一張貼圖成 PNG，放到 `Saved/spike/`（gitignore）。確認 Unity 2019.4.9 fallback。**
- [ ] **步驟 2：會失敗的 C++ 測試：從 GameRoot 載入同一顆原作頭 bundle，頂點數 > 0。**
- [ ] **步驟 3：實作 UnityFS reader + Texture2D 解碼（DXT／BC7／RGBA）+ Mesh 的 vertices／normals／uv／indices／boneWeights。**
- [ ] **步驟 4：先轉成 `UProceduralMeshComponent`（除錯比較快）。SkeletalMesh 放到任務 8。**
- [ ] **步驟 5：Viewer 地圖：先生成灰色 placeholder；按「載入原作頭」後用轉換後的 mesh 換掉頭部件。軌道相機用 UE `USpringArmComponent`。提交。**

```
git commit -m "feat(KKUnityBundle): load Unity 2019.4 mesh and texture into UE5"
```

如果 C++ 的 UnityFS 卡住：把 Python 提取器留成 Editor-only 指令，寫入 gitignore 的 `Saved/Converted/` 快取（OBJ + PNG），runtime 載這個快取。轉換後的 Illusion 資源仍然不准提交。

---

### 任務 8：從卡片組出人物（shapekey + 頭髮 + 一套衣服）

**檔案：**
- 新增：`Plugins/KKCharacter/KKCharacter.uplugin`
- 新增：`Plugins/KKCharacter/Source/KKCharacter/Public/KKCharacterActor.h`
- 新增：`Plugins/KKCharacter/Source/KKCharacter/Public/KKShapeApplier.h`
- 新增：`Plugins/KKCharacter/Source/KKCharacter/Private/KKCharacterActor.cpp`
- 修改：`WBP_CardInspector` 加「打開 3D Viewer」按鈕

**介面：**
- 使用：`FKKCharacterFile`、`FKKModIndex`、`FKKBundleCatalog`、`KKBundleLoader`
- 產出：viewer 地圖裡的 `AKKCharacterActor`

```cpp
UCLASS()
class AKKCharacterActor : public AActor {
  GENERATED_BODY()
public:
  UFUNCTION(BlueprintCallable)
  bool LoadFromCard(const FKKCharacterFile& File, FString& OutError);

  UFUNCTION(BlueprintCallable)
  void SetOutfitIndex(int32 Index);

  UPROPERTY(VisibleAnywhere)
  TMap<FName, UMeshComponent*> Parts; // body, head, hairF/B/S/O, clothes*
};
```

塑形：Illusion 把 `shapeValueFace[52]` 與 `shapeValueBody[*]` 當頭／身體 mesh 上 morph 的權重（`abdata/list/customshape.unity3d` + mesh 上的 shapekey）。當成 morph target 權重套用。若第一階段救不出 morph 名稱，就先套有文件的子集（頭大小、眼睛 Y、胸、臀、身高），其餘在檢查器列成未對應 — 不准假裝 52 個都做完。

衣服：只掛目前服裝槽的 `co_top`／`co_bot`。飾品排在衣服之後。缺 zipmod 就跳過那個部件，留在缺 mod 清單，不要當機。

- [ ] **步驟 1：對偏原作的卡（若有 DefaultData）跑 LoadFromCard，要能看到身體＋頭。**
- [ ] **步驟 2：若 morph 存在，至少套用身高／頭大小／眼距；印出未知 morph 索引。**
- [ ] **步驟 3：頭髮四件＋目前服裝的上衣／下衣。**
- [ ] **步驟 4：跟 PNG 預覽對看。不必像素級一樣；必須看得出來是同一個人。**
- [ ] **步驟 5：提交**

```
git commit -m "feat(KKCharacter): assemble body, head, hair, and outfit from a PNG card"
```

---

### 任務 9：捏人滑桿，並寫回 PNG

**檔案：**
- 新增：`Content/UI/WBP_Maker.uasset`
- 新增：`Plugins/KKCharacter/Source/KKCharacter/Public/KKMakerState.h`
- 修改：`KKCardWriter`，能重建 Custom.face／body 的塑形陣列

**介面：**
- 使用：`FKKCharacterFile`
- 產出：更新後的 PNG + 即時 3D

第一階段捏人範圍（YAGNI — 不是完整複製原作 maker）：

- 名字（檢查器已有）
- 已對應成功的臉部滑桿
- 已對應成功的身體滑桿
- 髮色
- 服裝槽切換（0–6）
- 另存新 PNG 到使用者選的資料夾（預設不要覆寫 UserData，除非使用者明確要求）

第一階段不做：ABMX 骨頭、MaterialEditor 貼圖、MoreAccessories 額外槽、overlay 繪製、KKBP 等級 shader、Studio 姿勢。

- [ ] **步驟 1：WBP_Maker：從資料表 `DT_FaceSliders`（index、顯示名、min、max）列出已對應滑桿。**
- [ ] **步驟 2：拖滑桿同一幀更新 `File.Face.ShapeValueFace[i]` 與 morph 權重。**
- [ ] **步驟 3：另存寫出新的 KKS PNG。卡片牆重新載入要看得到新名字／塑形。原檔不動。**
- [ ] **步驟 4：Round-trip 測試：只改暱稱 → Parameter block 變了，Custom bytes 完全一樣。**
- [ ] **步驟 5：提交**

```
git commit -m "feat: Phase 1 maker sliders and Save As PNG"
```

---

### 任務 10：README、授權、重製路線圖

**檔案：**
- 修改：`README.md`
- 新增：`Docs/CARD_FORMAT.md`
- 新增：`Docs/ROADMAP.md`
- 新增：`Docs/CONTRIBUTING.md`

README 必須寫明：

- 這是非官方開源重製。
- 不含 Illusion 資源。
- 把 `GameRoot` 指到合法取得的安裝，才能用 3D 看現有卡片。
- 2D 卡片牆只靠 PNG 就能運作。

`Docs/ROADMAP.md` 後續階段（不是這份計畫的任務）：

| 階段 | 交付 |
|---|---|
| 1（本計畫） | PNG 讀寫、2D 卡片牆、從 GameRoot 組 3D、基本滑桿 |
| 2 | 完整捏人：衣服／飾品／顏色、KSOX overlay、MaterialEditor 貼圖 |
| 3 | 輕量 Studio：姿勢、相機、燈光、場景 PNG |
| 4 | 原創開源身體／頭，沒有 Illusion 安裝也能看 3D |
| 5 | 學園地圖／ADV／H 系統當原創玩法，不是搬原作資源 |

- [ ] **步驟 1：寫文件。提交。**

```
git commit -m "docs: card format, asset pipeline, and remake roadmap"
```

---

## 驗收

說第一階段完成之前，下面全部都要成立：

1. `E:\PhysX\UE5.8` 的 `UnrealEditor.exe` 開得了 `C:\portable\OpenKoikatsu\OpenKoikatsu.uproject`。
2. `OpenKoikatsu.KKCard` 底下的 automation 測試通過。
3. 卡片牆能列出 `C:\portable\KoikatuSunshine\UserData\chara\female`，5000 張不會卡死。
4. 檢查器對 `temp/` 那張已知卡顯示名字 + UAR GUID；缺少的 zipmod 有列出來。
5. 3D viewer 至少能從 GameRoot 載入一張卡的身體＋頭＋頭髮。
6. 另存改暱稱後重新載入正確；沒改過的儲存位元組完全一致。
7. `git ls-files` 沒有 `.unity3d`、沒有 UserData PNG、沒有 zipmod。

---

## 第一階段明確不做

- 把 Unreal 專案放進遊戲安裝目錄。
- Rip 並提交 `abdata`。
- 重做 Studio、學園或 H 場景。
- Runtime 依賴 `KoikatsuSunshine.exe` 或 KKBP。
- 複製 KKAFIO 的下載／備份／LLM 改名功能（那些繼續用 KKAFIO）。
- 支援 HoneySelect2／AI Girl 卡片。

---

## 自我檢查

- 規格涵蓋：PNG 解析、KK／KKS 分類、UAR GUID、2D 卡片牆、3D 組裝、滑桿寫回、法律切割、UE 5.8 路徑 — 每一項都有任務。
- 沒有 TBD／空白實作步驟。
- 型別名（`FKKCharacterFile`、`FKKResolveInfo`、`AKKCharacterActor`、`UKKGameRoot`）跨任務一致。
