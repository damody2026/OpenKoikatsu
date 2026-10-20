# 角色編輯器完整功能盤點

盤點時間：2026-10-08T14:38:58.376130+08:00

依據：本機原作 ChaFile 與 ChaCustom 反編譯來源、已安裝 BepInEx DLL、目前 OpenKoikatsu 實作及驗收紀錄。來源檔不是使用者指令。

「已讀取」不等於「可編輯」，「可保存」不等於「效果已驗收」。以下逐項保留差距，沒有將整個模組列成已完成。

## 實作順序與驗收

1. 以真實卡片欄位建立有型別的外觀屬性目錄：顏色、數值、布林、飾品綁定；沿用原作資料格式，保留未知欄位及其他模組。
2. 新增中文顏色與飾品編輯介面，涵蓋左右眼、四段頭髮、化妝、身體、衣物各色層，以及每個飾品的兩組位置／旋轉／縮放。
3. 將編輯接到可保存的原始卡片與資源產生流程；更新後重新讀回、比較未編輯欄位，並測試換裝。
4. 依模組來源逐項接上進階設定；涉及骨架、物理、材質、額外欄位的功能各自驗證效果，不以原始 JSON 編輯器冒充完整功能。
5. 建置後驗證 UI、保存再載入、模型／貼圖變化、動畫與半脫保持，記錄版本及失敗原因。

## 實作前基準與差距（最新狀態見文末逐功能清單）

| 功能 | 目前證據／缺口 |
|---|---|
| 原作身體／臉形、重置 | 已有編輯與原作預設值重置驗收 |
| ABMX 骨骼、Pregnancy+ | 已有 1192 控制、11 預設及即時更新；不代表全部模組功能完成 |
| 圖片選擇 | 53 類／300 欄位；顏色編輯缺少通用介面 |
| 髮色、瞳孔、眼白、眼線、眉毛、化妝等 | 原生已解析多種顏色與著色；需新增可編輯、保存及驗收流程 |
| 衣服／飾品多層顏色、花紋變換 | 讀取／渲染已有部分支援，完整編輯待補 |
| 飾品位置／旋轉／縮放、骨架父節點 | 原生已讀取 addMove 與 parentKey；缺少編輯介面 |
| MoreAccessories 額外槽位 | 原生有 XML 讀取；額外槽位建立／刪除／編輯尚待獨立接入，不能只做前 20 欄 |
| MaterialEditor、Overlay、HairAccessoryCustomizer、Pushup、DynamicBoneEditor 等 | 已有部分載入解析；必須分別補編輯、同步及存檔，不能把保留 KKEx 當成功 |
| 動畫、左右手、衣物狀態 | 已有對應表、獨立手勢與快速切換驗收 |
| 卡片／座標、複製、隨機、角色資料、語音、相機 | 部分支援；以以下來源控制項及欄位清單追查缺口 |

## 原作資料欄位（完整來源列舉）

### ChaFile

來源：`C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/ChaFile.cs`

| 型別 | 欄位（同名可能分屬巢狀類別） |
|---|---|
| `int` | `productNo` |
| `int` | `tagIndex` |
| `ChaFileAbout` | `about` |
| `ChaFileCoordinate[]` | `coordinate` |

### ChaFileAbout

來源：`C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/ChaFileAbout.cs`

| 型別 | 欄位（同名可能分屬巢狀類別） |
|---|---|
| `Version` | `version` |
| `int` | `language` |
| `string` | `userID` |
| `string` | `dataID` |

### ChaFileAccessory

來源：`C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/ChaFileAccessory.cs`

| 型別 | 欄位（同名可能分屬巢狀類別） |
|---|---|
| `int` | `type` |
| `int` | `id` |
| `string` | `parentKey` |
| `Vector3[,]` | `addMove` |
| `Color[]` | `color` |
| `int` | `hideCategory` |
| `bool` | `noShake` |
| `bool` | `partsOfHead` |
| `Version` | `version` |
| `PartsInfo[]` | `parts` |

### ChaFileBody

來源：`C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/ChaFileBody.cs`

| 型別 | 欄位（同名可能分屬巢狀類別） |
|---|---|
| `Version` | `version` |
| `float[]` | `shapeValueBody` |
| `float` | `bustSoftness` |
| `float` | `bustWeight` |
| `int` | `skinId` |
| `int` | `detailId` |
| `float` | `detailPower` |
| `Color` | `skinMainColor` |
| `Color` | `skinSubColor` |
| `float` | `skinGlossPower` |
| `int[]` | `paintId` |
| `Color[]` | `paintColor` |
| `int[]` | `paintLayoutId` |
| `Vector4[]` | `paintLayout` |
| `int` | `sunburnId` |
| `Color` | `sunburnColor` |
| `int` | `nipId` |
| `Color` | `nipColor` |
| `float` | `nipGlossPower` |
| `float` | `areolaSize` |
| `int` | `underhairId` |
| `Color` | `underhairColor` |
| `Color` | `nailColor` |
| `float` | `nailGlossPower` |
| `bool` | `drawAddLine` |
| `int` | `typeBone` |

### ChaFileClothes

來源：`C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/ChaFileClothes.cs`

| 型別 | 欄位（同名可能分屬巢狀類別） |
|---|---|
| `Color` | `baseColor` |
| `int` | `pattern` |
| `Vector2` | `tiling` |
| `Color` | `patternColor` |
| `Vector2` | `offset` |
| `float` | `rotate` |
| `int` | `id` |
| `ColorInfo[]` | `colorInfo` |
| `int` | `emblemeId` |
| `int` | `emblemeId2` |
| `bool[]` | `hideOpt` |
| `int` | `sleevesType` |
| `Version` | `version` |
| `PartsInfo[]` | `parts` |
| `int[]` | `subPartsId` |
| `bool[]` | `hideBraOpt` |
| `bool[]` | `hideShortsOpt` |

### ChaFileFace

來源：`C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/ChaFileFace.cs`

| 型別 | 欄位（同名可能分屬巢狀類別） |
|---|---|
| `int` | `id` |
| `Color` | `baseColor` |
| `Color` | `subColor` |
| `int` | `gradMaskId` |
| `float` | `gradBlend` |
| `float` | `gradOffsetY` |
| `float` | `gradScale` |
| `Version` | `version` |
| `float[]` | `shapeValueFace` |
| `int` | `headId` |
| `int` | `skinId` |
| `int` | `detailId` |
| `float` | `detailPower` |
| `float` | `cheekGlossPower` |
| `int` | `eyebrowId` |
| `Color` | `eyebrowColor` |
| `int` | `noseId` |
| `PupilInfo[]` | `pupil` |
| `int` | `hlUpId` |
| `Color` | `hlUpColor` |
| `int` | `hlDownId` |
| `Color` | `hlDownColor` |
| `int` | `whiteId` |
| `Color` | `whiteBaseColor` |
| `Color` | `whiteSubColor` |
| `float` | `pupilWidth` |
| `float` | `pupilHeight` |
| `float` | `pupilX` |
| `float` | `pupilY` |
| `float` | `hlUpX` |
| `float` | `hlDownX` |
| `float` | `hlUpY` |
| `float` | `hlDownY` |
| `int` | `eyelineUpId` |
| `float` | `eyelineUpWeight` |
| `int` | `eyelineDownId` |
| `Color` | `eyelineColor` |
| `int` | `moleId` |
| `Color` | `moleColor` |
| `Vector4` | `moleLayout` |
| `int` | `lipLineId` |
| `Color` | `lipLineColor` |
| `float` | `lipGlossPower` |
| `bool` | `doubleTooth` |
| `ChaFileMakeup` | `baseMakeup` |
| `byte` | `foregroundEyes` |
| `byte` | `foregroundEyebrow` |

### ChaFileHair

來源：`C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/ChaFileHair.cs`

| 型別 | 欄位（同名可能分屬巢狀類別） |
|---|---|
| `int` | `id` |
| `Color` | `baseColor` |
| `Color` | `startColor` |
| `Color` | `endColor` |
| `float` | `length` |
| `Vector3` | `pos` |
| `Vector3` | `rot` |
| `Vector3` | `scl` |
| `Color[]` | `acsColor` |
| `Color` | `outlineColor` |
| `Color` | `glossColor` |
| `bool` | `noShake` |
| `Version` | `version` |
| `PartsInfo[]` | `parts` |
| `int` | `kind` |
| `int` | `glossId` |

### ChaFileMakeup

來源：`C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/ChaFileMakeup.cs`

| 型別 | 欄位（同名可能分屬巢狀類別） |
|---|---|
| `Version` | `version` |
| `int` | `eyeshadowId` |
| `Color` | `eyeshadowColor` |
| `int` | `cheekId` |
| `Color` | `cheekColor` |
| `int` | `lipId` |
| `Color` | `lipColor` |
| `int[]` | `paintId` |
| `Color[]` | `paintColor` |
| `Vector4[]` | `paintLayout` |

### ChaFileParameter

來源：`C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/ChaFileParameter.cs`

| 型別 | 欄位（同名可能分屬巢狀類別） |
|---|---|
| `bool` | `animal` |
| `bool` | `eat` |
| `bool` | `cook` |
| `bool` | `exercise` |
| `bool` | `study` |
| `bool` | `fashionable` |
| `bool` | `blackCoffee` |
| `bool` | `spicy` |
| `bool` | `sweet` |
| `bool` | `kiss` |
| `bool` | `aibu` |
| `bool` | `anal` |
| `bool` | `massage` |
| `bool` | `notCondom` |
| `int[]` | `answer` |
| `bool` | `harapeko` |
| `bool` | `choroi` |
| `bool` | `dokusyo` |
| `bool` | `ongaku` |
| `bool` | `okute` |
| `bool` | `friendly` |
| `bool` | `kireizuki` |
| `bool` | `sinsyutu` |
| `bool` | `hitori` |
| `bool` | `active` |
| `bool` | `majime` |
| `bool` | `info` |
| `bool` | `love` |
| `bool` | `talk` |
| `bool` | `nakama` |
| `bool` | `nonbiri` |
| `bool` | `hinnyo` |
| `bool` | `likeGirls` |
| `bool` | `bitch` |
| `bool` | `mutturi` |
| `bool` | `lonely` |
| `bool` | `ukemi` |
| `bool` | `undo` |
| `Version` | `version` |
| `byte` | `sex` |
| `int` | `exType` |
| `string` | `lastname` |
| `string` | `firstname` |
| `string` | `nickname` |
| `int` | `callType` |
| `int` | `personality` |
| `byte` | `bloodType` |
| `byte` | `birthMonth` |
| `byte` | `birthDay` |
| `byte` | `clubActivities` |
| `float` | `voiceRate` |
| `int` | `weakPoint` |
| `Awnser` | `awnser` |
| `Denial` | `denial` |
| `Interest` | `interest` |
| `Attribute` | `attribute` |
| `int` | `aggressive` |
| `int` | `diligence` |
| `int` | `kindness` |

### ChaFilePreview

來源：`C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/ChaFilePreview.cs`

| 型別 | 欄位（同名可能分屬巢狀類別） |
|---|---|
| `byte` | `sex` |
| `string` | `lastname` |
| `string` | `firstname` |
| `string` | `nickname` |
| `int` | `personality` |
| `byte` | `bloodType` |
| `byte` | `birthMonth` |
| `byte` | `birthDay` |
| `byte` | `clubActivities` |
| `float` | `voicePitch` |

### ChaFileStatus

來源：`C:/portable/KoikatuSunshine/kks_decomp/Assembly-CSharp/ChaFileStatus.cs`

| 型別 | 欄位（同名可能分屬巢狀類別） |
|---|---|
| `Version` | `version` |
| `int` | `coordinateType` |
| `int` | `backCoordinateType` |
| `byte[]` | `clothesState` |
| `byte` | `shoesType` |
| `bool[]` | `showAccessory` |
| `int` | `eyebrowPtn` |
| `float` | `eyebrowOpenMax` |
| `int` | `eyesPtn` |
| `float` | `eyesOpenMax` |
| `bool` | `eyesBlink` |
| `bool` | `eyesYure` |
| `int` | `mouthPtn` |
| `float` | `mouthOpenMax` |
| `bool` | `mouthFixed` |
| `bool` | `mouthAdjustWidth` |
| `byte` | `tongueState` |
| `int` | `eyesLookPtn` |
| `int` | `eyesTargetType` |
| `float` | `eyesTargetAngle` |
| `float` | `eyesTargetRange` |
| `float` | `eyesTargetRate` |
| `int` | `neckLookPtn` |
| `int` | `neckTargetType` |
| `float` | `neckTargetAngle` |
| `float` | `neckTargetRange` |
| `float` | `neckTargetRate` |
| `bool` | `disableMouthShapeMask` |
| `bool[,]` | `disableBustShapeMask` |
| `float` | `nipStandRate` |
| `float` | `skinTuyaRate` |
| `float` | `hohoAkaRate` |
| `byte` | `tearsLv` |
| `bool` | `hideEyesHighlight` |
| `byte[]` | `siruLv` |
| `bool` | `visibleSon` |
| `bool` | `visibleSonAlways` |
| `bool` | `visibleHeadAlways` |
| `bool` | `visibleBodyAlways` |
| `bool` | `visibleSimple` |
| `bool` | `visibleGomu` |
| `Color` | `simpleColor` |
| `bool[]` | `enableShapeHand` |
| `int[,]` | `shapeHandPtn` |
| `float[]` | `shapeHandBlendValue` |

## 原作編輯介面控制器（完整列舉）

每個控制器均需核對選項、重置、複製／左右同步與保存路徑；清單存在不代表已實作。

- `CvsAccessory`
- `CvsAccessoryChange`
- `CvsAccessoryCopy`
- `CvsADK`
- `CvsArm`
- `CvsAttribute`
- `CvsBodyAll`
- `CvsBodyLower`
- `CvsBodyPaint`
- `CvsBodyShapeAll`
- `CvsBodyUpper`
- `CvsBreast`
- `CvsChara`
- `CvsCharaEx`
- `CvsCheek`
- `CvsChin`
- `CvsClothes`
- `CvsClothesCopy`
- `CvsColor`
- `CvsDrawCtrl`
- `CvsEar`
- `CvsExit`
- `CvsEye01`
- `CvsEye02`
- `CvsEyebrow`
- `CvsFaceAll`
- `CvsFaceShapeAll`
- `CvsH`
- `CvsHair`
- `CvsHairEtc`
- `CvsInterest`
- `CvsLeg`
- `CvsMakeup`
- `CvsMole`
- `CvsMouth`
- `CvsNail`
- `CvsNose`
- `CvsQA`
- `CvsSunburn`
- `CvsUnderhair`

## 已安裝模組／依賴（完整 DLL 清單）

此清單包含 Maker、Studio、遊戲、VR、修正及共用依賴。遊戲流程、VR、外部工具與共用庫不能直接當作角色編輯項目；角色編輯相關功能需依反編譯來源拆項，尚未完成完整逐函式驗收。

| DLL（相對 BepInEx/plugins） | 本機來源 | 狀態 |
|---|---|---|
| `2155X/KKS_HLightControl.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `2155X/KKS_MakerAdditions.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `2155X/KKS_MakerRandomPicker.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `2155X/KKS_MakerSearch.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `2155X/KKS_StudioAccessoryNames.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `2155X/KKS_StudioDefaultData.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `2155X/KKS_StudioMiscSearch.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `2155X/KKS_UnlockPlayerHClothes.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `AnimationLoader.KoikatsuSunshine.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `BepInEx.CatchUnityEventExceptions.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `BepInEx.EnableFullScreenToggle.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `BepInEx.EnableResize.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `BepInEx.InputHotkeyBlock.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `BepInEx.MessageCenter.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `BepInEx.MuteInBackground.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `BepInEx.OptimizeIMGUI.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `CameraFrameMask.KoikatuSunshine.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `CharaStateX.KoikatuSunshine.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `ConfigurationManager/ConfigurationManager.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `DefaultParamEditor.KoikatuSunshine.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `DragAndDrop.KoikatsuSunshine.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `FPSCounter.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `GeBo_Plugins/KKS_GeBoCommon.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `GeBo_Plugins/KKS_TranslationCacheCleaner.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `GeBo_Plugins/KKS_TranslationHelper.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `GraphicsSettings.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `HideAllUI.KoikatsuSunshine.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `IDontHaveIdea/KKS_IDHIStore.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IDontHaveIdea/KKS_IDHIUtils.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_CameraMaskResize.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_CameraTarget.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_CharacterListOptimizations.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_DataCorruptionFixes.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_DynamicBones.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_ExpandShaderDropdown.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_Eyebrows.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_GarbageTruck.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_GuideObjects.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_HairShadows.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_Heterochromia.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_InvalidSceneFileProtection.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_MainGameOptimizations.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_MakerOptimizations.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_ManifestCorrector.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_NullChecks.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_PersonalityCorrector.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_PoseLoad.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_ResourceUnloadOptimizations.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_SettingsVerifier.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_ShowerAccessories.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_StudioOptimizations.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `IllusionFixes/KKS_Fix_UnlimitedMapLights.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `ItemLayerEdit.KoikatuSunshine.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `jim60105/KKS_CoordinateLoadOption.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `jim60105/KKS_StudioAutoCloseLoadingSceneWindow.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `jim60105/KKS_StudioCharaLightLinkedToCamera.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `jim60105/KKS_StudioCharaOnlyLoadBody.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `jim60105/KKS_StudioReflectFKFix.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `jim60105/KKS_StudioTransgenderLoading.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS.PictureInPicture.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Accessory_States.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_AdvIKPlugin.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Ahegao.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Autostart.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BecomeTrap.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BepisPlugins/ICSharpCode.SharpZipLib.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BepisPlugins/KKS_BGMLoader.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BepisPlugins/KKS_ColorCorrector.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BepisPlugins/KKS_ConfigurationManager.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BepisPlugins/KKS_ExtensibleSaveFormat.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BepisPlugins/KKS_InputUnlocker.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BepisPlugins/KKS_Screencap.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BepisPlugins/KKS_Sideloader.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BepisPlugins/KKS_SliderUnlocker.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BepisPlugins/Unity.InputSystem.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BetterColorPicker.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BetterPenetration.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BetterSquirt.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BrowserFolders.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_BrowserFolders_Hooks.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Bulge.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_CharacterReplacer.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_CheatTools.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_ChineseVoice.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_ClothColliders.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_ClothingBlendShape.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_ClothingBlendShapeCoroutineFix/KKS_ClothingBlendShapeCoroutineFix.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_ClothingStateMenu.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_CopyChaBodyMat.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_DisableHipDynamicBones.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_EditFangs.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_ExpandMaleMaker.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Fix_ConvCoordinate.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Fix_MapLoadStall.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_FKHeightAdjustUI.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_GameplayMod.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_HeightBar.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_HSceneOptions.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_HSceneOptionsVR.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_LewdCrest.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_MakerAnimatePoseToggle.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_MakerCameraPresets.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_MeshDumper/KKS_MeshDumper.Core.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_MeshDumper/KKS_MeshDumper.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_MoanSoftly.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_MoarCamz.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_MoreAccessoryParents.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_MoreShopItems.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_ObjImport.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_OrthographicCamera.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_OverlayMods.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_PantyFairy.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_AccessoriesToStudioItems.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_AccessoryClothes.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_AccessoryQuickRemove.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_AnimationController.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_AnimationOverdrive.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_CharaMakerLoadedSound.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_ClothesToAccessories.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_ClothingUnlocker.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_Colliders.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_DynamicBoneEditor.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_EyeControl.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_EyeShaking.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_FadeAdjuster.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_FKIK.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_ForceHighPoly.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_FreeHRandom.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_HairAccessoryCustomizer.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_HairShadowColorControl.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_HCharaAdjustment.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_InvisibleBody.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_ItemBlacklist.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_ListOverride.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_MakerDefaults.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_MaleJuice.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_MaterialEditor.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_MoreOutfits.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_PoseQuickLoad.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_PoseTools.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_PoseUnlocker.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_Profile.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_Pushup.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_RandomCharacterGenerator.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_ReloadCharaListOnChange.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_ShaderSwapper.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_StudioCustomMasking.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_StudioImageEmbed.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_StudioObjectMoveHotkeys.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_StudioSceneLoadedSound.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_StudioSceneSettings.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_StudioWindowResize.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_Subtitles.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_TimelineFlowControl.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_TwoLut.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Plugins/KKS_UncensorSelector.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_PostProcessingEffect.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Pregnancy.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_PregnancyPlus.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_QuickAccessBox.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_RemoveToRecycleBin.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_ResetRotationFix.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_SaveCameraObjectFov.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_SexFaces.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_SkinEffects.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_SliderHighlight.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_Studio_BetterPenetration.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_StudioCameraObjectTweaks.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_StudioExtraMoveAxis.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_StudioKeybinds.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_StudioNodeTweaks.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_StudioTransformOrientation.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_TalkFreeCamera.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_VR/KKS_CharaStudioVR.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_VR/KKS_MainGameVR.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_VR/Unity.Subsystem.Registration.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_VR/Unity.XR.Management.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_VR/Unity.XR.OpenVR.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_VR/UnityEngine.SpatialTracking.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_VR/UnityEngine.XR.LegacyInputHelpers.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_VR/Valve.Newtonsoft.Json.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_VR/VRGIN_OpenXR.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_VR/WindowsInput.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_VR_MoreOutfits.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `KKS_WTFix.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKSABMX.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKSAPI.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKSPE.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `KKSUS.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `ksPlug.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `LightManager.KoikatuSunshine.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `Liquid_shader_wobble.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `LitJSON.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `LockOnPlugin.KoikatuSunshine.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `MassShaderEditor.KoikatsuSunshine.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `ModBoneImplantor.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `MoreAccessories.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `MoveControllerKKS.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `NodesConstraints.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `RealPOV.KoikatsuSunshine.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `RuntimeUnityEditor/RuntimeUnityEditor.Bepin5.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `RuntimeUnityEditor/RuntimeUnityEditor.Core.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `Timeline.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `TitleShortcuts.KoikatuSunshine.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `TranslationTools/KKS_TextResourceRedirector.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `VideoExport.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/ExIni.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/BaiduTranslate.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/BingTranslate.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/BingTranslateLegitimate.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/CustomTranslate.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/DeepLTranslate.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/ezTransXP.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/FullNET/Common.ExtProtocol.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/FullNET/DeepLTranslate.ExtProtocol.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/FullNET/GoogleTranslateCompat.ExtProtocol.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/FullNET/Http.ExtProtocol.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/FullNET/Newtonsoft.Json.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/FullNET/XUnity.AutoTranslator.Plugin.ExtProtocol.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/GoogleTranslate.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/GoogleTranslateCompat.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/GoogleTranslateLegitimate.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/LecPowerTranslator15.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/LingoCloudTranslate.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/PapagoTranslate.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/WatsonTranslate.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/Translators/YandexTranslate.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/XUnity.AutoTranslator.Plugin.BepInEx.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/XUnity.AutoTranslator.Plugin.Core.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.AutoTranslator/XUnity.AutoTranslator.Plugin.ExtProtocol.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.ResourceRedirector/XUnity.ResourceRedirector.BepInEx.dll` | 需追查來源／所屬套件 | 待逐功能核對；未宣稱全部已支援 |
| `XUnity.ResourceRedirector/XUnity.ResourceRedirector.dll` | 有同名來源目錄 | 待逐功能核對；未宣稱全部已支援 |

## 模組編輯控制項來源盤點

下表自已安裝套件的本機反編譯來源搜尋 Maker 控制項建構式與註冊入口。動態產生標籤、共用元件及不使用 Maker API 的模組需接續人工核對；每列仍需實際效果與保存驗收。

| 來源（相對 kks_decomp/plugins） | 行 | 控制項／註冊入口 | 字串線索 |
|---|---|---|---|
| `GeBo_Plugins/KKS_TranslationHelper/TranslationHelperPlugin.Maker/Configuration.cs` | 68 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_Accessory_States/Accessory_States/CharaEvent.cs` | 1653 | `MakerCategory` | 動態標籤；需追查呼叫參數 |
| `KKS_Accessory_States/Accessory_States/CharaEvent.cs` | 1655 | `AddEditableAccessoryWindowControl` | Clothing\nBind |
| `KKS_Accessory_States/Accessory_States/CharaEvent.cs` | 1655 | `MakerDropdown` | Clothing\nBind |
| `KKS_Accessory_States/Accessory_States/CharaEvent.cs` | 1660 | `AddEditableAccessoryWindowControl` | Start |
| `KKS_Accessory_States/Accessory_States/CharaEvent.cs` | 1660 | `MakerSlider` | Start |
| `KKS_Accessory_States/Accessory_States/CharaEvent.cs` | 1668 | `AddEditableAccessoryWindowControl` | Stop |
| `KKS_Accessory_States/Accessory_States/CharaEvent.cs` | 1668 | `MakerSlider` | Stop |
| `KKS_Accessory_States/Accessory_States/CharaEvent.cs` | 1676 | `MakerButton` | States GUI |
| `KKS_Accessory_States/Accessory_States/Settings.cs` | 196 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Accessory_States/Accessory_States/Settings.cs` | 397 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_BecomeTrap/KK_BecomeTrap/BecomeTrapGui.cs` | 27 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_BecomeTrap/KK_BecomeTrap/BecomeTrapGui.cs` | 27 | `RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_BecomeTrap/KK_BecomeTrap/BecomeTrapGui.cs` | 30 | `RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_BecomeTrap/KK_BecomeTrap/BecomeTrapGui.cs` | 37 | `AddControl` | Character is a trap or a futa (changes gameplay) |
| `KKS_BecomeTrap/KK_BecomeTrap/BecomeTrapGui.cs` | 37 | `MakerToggle` | Character is a trap or a futa (changes gameplay) |
| `KKS_BecomeTrap/KK_BecomeTrap/BecomeTrapGui.cs` | 42 | `AddControl` | Idle trap animation |
| `KKS_BecomeTrap/KK_BecomeTrap/BecomeTrapGui.cs` | 42 | `MakerDropdown` | Idle trap animation |
| `KKS_BepisPlugins/Unity.InputSystem/UnityEngine.InputSystem.Layouts/InputControlLayout.cs` | 663 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_BepisPlugins/Unity.InputSystem/UnityEngine.InputSystem.OnScreen/OnScreenControl.cs` | 21 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Bulge/KK_Bulge/BulgePlugin.cs` | 51 | `AddControl` | Enable crotch bulge、Auto、Always、Never |
| `KKS_Bulge/KK_Bulge/BulgePlugin.cs` | 51 | `MakerRadioButtons` | Enable crotch bulge、Auto、Always、Never |
| `KKS_Bulge/KK_Bulge/BulgePlugin.cs` | 55 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Bulge/KK_Bulge/BulgePlugin.cs` | 55 | `MakerText` | 動態標籤；需追查呼叫參數 |
| `KKS_Bulge/KK_Bulge/BulgePlugin.cs` | 59 | `AddControl` | Bulge size |
| `KKS_Bulge/KK_Bulge/BulgePlugin.cs` | 59 | `MakerSlider` | Bulge size |
| `KKS_Bulge/KK_Bulge/BulgePlugin.cs` | 67 | `AddControl` | Enable crotch bulge、Auto、Always、Never |
| `KKS_Bulge/KK_Bulge/BulgePlugin.cs` | 74 | `AddControl` | Bulge size |
| `KKS_ClothingBlendShape/KK_ClothingBlendShape/MakerGui.cs` | 34 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_ClothingBlendShape/KK_ClothingBlendShape/MakerGui.cs` | 83 | `AddControl` | cbs_slider_ |
| `KKS_ClothingBlendShape/KK_ClothingBlendShape/MakerGui.cs` | 83 | `MakerSlider` | cbs_slider_ |
| `KKS_ClothingBlendShape/KK_ClothingBlendShape/MakerGui.cs` | 113 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_ClothingBlendShape/KK_ClothingBlendShape/MakerGui.cs` | 113 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_ClothingStateMenu/ClothingStateMenu/ClothingStateMenuPlugin.cs` | 168 | `MakerAPI.RegisterCustomSubCategories` | Show clothing state menu |
| `KKS_EditFangs/EditFangs/EditFangsPlugin.cs` | 40 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_EditFangs/EditFangs/EditFangsPlugin.cs` | 40 | `RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_EditFangs/EditFangs/EditFangsPlugin.cs` | 43 | `RegisterCustomSubCategories` | Left Fang Length |
| `KKS_EditFangs/EditFangs/EditFangsPlugin.cs` | 45 | `AddControl` | Left Fang Length |
| `KKS_EditFangs/EditFangs/EditFangsPlugin.cs` | 45 | `MakerSlider` | Left Fang Length |
| `KKS_EditFangs/EditFangs/EditFangsPlugin.cs` | 46 | `AddControl` | Left Fang Spacing |
| `KKS_EditFangs/EditFangs/EditFangsPlugin.cs` | 46 | `MakerSlider` | Left Fang Spacing |
| `KKS_EditFangs/EditFangs/EditFangsPlugin.cs` | 47 | `AddControl` | Right Fang Length |
| `KKS_EditFangs/EditFangs/EditFangsPlugin.cs` | 47 | `MakerSlider` | Right Fang Length |
| `KKS_EditFangs/EditFangs/EditFangsPlugin.cs` | 48 | `AddControl` | Right Fang Spacing |
| `KKS_EditFangs/EditFangs/EditFangsPlugin.cs` | 48 | `MakerSlider` | Right Fang Spacing |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 212 | `MakerLoadToggle` | Clothes overlays |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 217 | `MakerCoordinateLoadToggle` | Clothes overlays |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 264 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 264 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 265 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 265 | `MakerText` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 266 | `AddControl` | Dump original texture |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 266 | `MakerButton` | Dump original texture |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 274 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 274 | `MakerImage` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 282 | `AddControl` | Hide base texture |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 282 | `MakerToggle` | Hide base texture |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 297 | `AddControl` | Load new  |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 297 | `MakerButton` | Load new  |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 305 | `AddControl` | Clear  |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 305 | `MakerButton` | Clear  |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 310 | `AddControl` | Export  |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 310 | `MakerButton` | Export  |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 393 | `AddControl` | Clothes overlays |
| `KKS_OverlayMods/KoiClothesOverlayX/KoiClothesOverlayGui.cs` | 407 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 134 | `MakerToggle` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 228 | `RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 232 | `MakerLoadToggle` | Skin/eye overlays |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 255 | `MakerCategory` | tglOverlayKSOX、Skin Overlays |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 257 | `AddControl` | Get face overlay template |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 257 | `MakerButton` | Get face overlay template |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 269 | `AddControl` | Get body overlay template |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 269 | `MakerButton` | Get body overlay template |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 282 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 282 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 284 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 284 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 286 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 286 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 288 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 288 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 301 | `MakerCategory` | tglEyeOverlayKSOX、Eye Overlays |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 303 | `AddControl` | Get iris template |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 303 | `MakerButton` | Get iris template |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 315 | `AddControl` | Get eyeliner/eyebrow template |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 315 | `MakerButton` | Get eyeliner/eyebrow template |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 328 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 328 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 330 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 330 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 332 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 332 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 334 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 334 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 341 | `AddControl` | Watch last loaded texture file for changes |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 341 | `MakerToggle` | Watch last loaded texture file for changes |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 348 | `AddControl` | Use different overlays per outfit |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 348 | `MakerToggle` | Use different overlays per outfit |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 357 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 357 | `MakerText` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 393 | `AddControl` | Eye to edit、Both、Left、Right |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 393 | `MakerRadioButtons` | Eye to edit、Both、Left、Right |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 394 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 394 | `MakerText` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 397 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 397 | `MakerImage` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 426 | `AddControl` | Load new texture |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 426 | `MakerButton` | Load new texture |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 433 | `AddControl` | Clear texture |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 433 | `MakerButton` | Clear texture |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 437 | `AddControl` | Export current texture |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 437 | `MakerButton` | Export current texture |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 522 | `AddControl` | Skin overlays |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 533 | `AddControl` | Eye overlays |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 548 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_OverlayMods/KoiSkinOverlayX/KoiSkinOverlayGui.cs` | 548 | `RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_ClothingUnlocker/KK_Plugins/ClothingUnlocker.cs` | 135 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_ClothingUnlocker/KK_Plugins/ClothingUnlocker.cs` | 149 | `MakerCategory` | 03_ClothesTop、tglSettings、Settings |
| `KKS_Plugins/KKS_ClothingUnlocker/KK_Plugins/ClothingUnlocker.cs` | 150 | `MakerText` | Unlock bras with all tops, bottoms with all tops, and underwear with all bras |
| `KKS_Plugins/KKS_ClothingUnlocker/KK_Plugins/ClothingUnlocker.cs` | 151 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_ClothingUnlocker/KK_Plugins/ClothingUnlocker.cs` | 152 | `MakerToggle` | Clothing Unlock |
| `KKS_Plugins/KKS_ClothingUnlocker/KK_Plugins/ClothingUnlocker.cs` | 153 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Colliders/KK_Plugins/Colliders.cs` | 629 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Colliders/KK_Plugins/Colliders.cs` | 630 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Colliders/KK_Plugins/Colliders.cs` | 631 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_DynamicBoneEditor/KK_Plugins.DynamicBoneEditor/UI.cs` | 169 | `MakerButton` | Dynamic Bone Editor |
| `KKS_Plugins/KKS_EyeControl/KK_Plugins/EyeControl.cs` | 56 | `AddControl` | Eye Open Max |
| `KKS_Plugins/KKS_EyeControl/KK_Plugins/EyeControl.cs` | 56 | `MakerSlider` | Eye Open Max |
| `KKS_Plugins/KKS_EyeControl/KK_Plugins/EyeControl.cs` | 61 | `AddControl` | Disable Character Blinking |
| `KKS_Plugins/KKS_EyeControl/KK_Plugins/EyeControl.cs` | 61 | `MakerToggle` | Disable Character Blinking |
| `KKS_Plugins/KKS_EyeShaking/KK_Plugins/EyeShaking.cs` | 400 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_HairAccessoryCustomizer/KK_Plugins/HairAccessoryCustomizer.cs` | 1026 | `MakerToggle` | Match Color With Hair |
| `KKS_Plugins/KKS_HairAccessoryCustomizer/KK_Plugins/HairAccessoryCustomizer.cs` | 1027 | `MakerToggle` | Use Hair Gloss |
| `KKS_Plugins/KKS_HairAccessoryCustomizer/KK_Plugins/HairAccessoryCustomizer.cs` | 1028 | `MakerSlider` | Length |
| `KKS_Plugins/KKS_HairAccessoryCustomizer/KK_Plugins/HairAccessoryCustomizer.cs` | 1029 | `MakerColor` | Outline Color |
| `KKS_Plugins/KKS_HairAccessoryCustomizer/KK_Plugins/HairAccessoryCustomizer.cs` | 1030 | `MakerColor` | Accessory Color |
| `KKS_Plugins/KKS_HairAccessoryCustomizer/KK_Plugins/HairAccessoryCustomizer.cs` | 1031 | `MakerColor` | Gloss Color |
| `KKS_Plugins/KKS_HairShadowColorControl/HairShadowColorControl/HairShadowColorControlPlugin.cs` | 60 | `MakerCategory` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_HairShadowColorControl/HairShadowColorControl/HairShadowColorControlPlugin.cs` | 67 | `MakerColor` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_HairShadowColorControl/HairShadowColorControl/HairShadowColorControlPlugin.cs` | 72 | `AddControl` | Hair shadow color |
| `KKS_Plugins/KKS_HairShadowColorControl/HairShadowColorControl/HairShadowColorControlPlugin.cs` | 72 | `MakerColor` | Hair shadow color |
| `KKS_Plugins/KKS_InvisibleBody/KK_Plugins/InvisibleBody.cs` | 215 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_InvisibleBody/KK_Plugins/InvisibleBody.cs` | 223 | `AddControl` | Invisible Body |
| `KKS_Plugins/KKS_InvisibleBody/KK_Plugins/InvisibleBody.cs` | 223 | `MakerToggle` | Invisible Body |
| `KKS_Plugins/KKS_InvisibleBody/KK_Plugins/InvisibleBody.cs` | 265 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 38 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 166 | `MakerButton` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 169 | `AddControl` | Material Editor、body |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 169 | `MakerButton` | Material Editor、body |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 173 | `AddControl` | Material Editor (Body)、body |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 173 | `MakerButton` | Material Editor (Body)、body |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 177 | `AddControl` | Material Editor (Face)、face |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 177 | `MakerButton` | Material Editor (Face)、face |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 181 | `AddControl` | Material Editor (All) |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 181 | `MakerButton` | Material Editor (All) |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 185 | `AddControl` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 185 | `MakerButton` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 189 | `AddControl` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 189 | `MakerButton` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 193 | `AddControl` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 193 | `MakerButton` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 197 | `AddControl` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 197 | `MakerButton` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 201 | `AddControl` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 201 | `MakerButton` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 205 | `AddControl` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 205 | `MakerButton` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 209 | `AddControl` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 209 | `MakerButton` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 213 | `AddControl` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 213 | `MakerButton` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 217 | `AddControl` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 217 | `MakerButton` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 221 | `AddControl` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 221 | `MakerButton` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 225 | `AddControl` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 225 | `MakerButton` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 229 | `AddControl` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 229 | `MakerButton` | Material Editor |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 233 | `AddControl` | Material Editor、mayuge |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 233 | `MakerButton` | Material Editor、mayuge |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 237 | `AddControl` | Material Editor、eyeline,hitomi,sirome |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 237 | `MakerButton` | Material Editor、eyeline,hitomi,sirome |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 241 | `AddControl` | Material Editor、nose |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 241 | `MakerButton` | Material Editor、nose |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 245 | `AddControl` | Material Editor、tang,tooth,canine |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 245 | `MakerButton` | Material Editor、tang,tooth,canine |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 249 | `AddControl` | Material Editor、eyeline,hitomi,sirome |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 249 | `MakerButton` | Material Editor、eyeline,hitomi,sirome |
| `KKS_Plugins/KKS_MaterialEditor/KK_Plugins.MaterialEditor/MEMaker.cs` | 255 | `MakerColorPalette` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 72 | `MakerCategory` | 03_ClothesTop、tglSettings、Settings |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 73 | `MakerText` | Add or remove outfit slots |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 74 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 75 | `MakerTextbox` | Outfit Name、Outfit # |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 76 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 77 | `MakerButton` | Add additional clothing slot |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 78 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 85 | `MakerButton` | Remove last additional clothing slot |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 86 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 99 | `MakerText` | Rename outfit slots |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 100 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 101 | `MakerDropdown` | Outfit、none |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 102 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 103 | `MakerTextbox` | New Name、Outfit # |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/MakerUI.cs` | 113 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/Plugin.cs` | 36 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_MoreOutfits/KK_Plugins.MoreOutfits/StudioUI.cs` | 60 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Profile/KK_Plugins/Profile.cs` | 32 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Profile/KK_Plugins/Profile.cs` | 70 | `MakerCategory` | Profile |
| `KKS_Plugins/KKS_Profile/KK_Plugins/Profile.cs` | 72 | `AddControl` | Character Description |
| `KKS_Plugins/KKS_Profile/KK_Plugins/Profile.cs` | 72 | `MakerText` | Character Description |
| `KKS_Plugins/KKS_Profile/KK_Plugins/Profile.cs` | 73 | `AddControl` | ,  |
| `KKS_Plugins/KKS_Profile/KK_Plugins/Profile.cs` | 73 | `MakerTextbox` | ,  |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1134 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1134 | `RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1364 | `RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1396 | `MakerCategory` | 03_ClothesTop、tglPushup、Pushup |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1397 | `AddControl` | Type、Bra、Top |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1397 | `MakerRadioButtons` | Type、Bra、Top |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1402 | `MakerToggle` | Enabled |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1403 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1409 | `MakerToggle` | Flatten Nipples |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1410 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1411 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1411 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1412 | `MakerToggle` | Advanced Mode |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1413 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1414 | `MakerButton` | Copy Body To Advanced |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1415 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1417 | `MakerButton` | Copy Basic To Advanced |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1418 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1434 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1434 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1435 | `MakerDropdown` | Copy To Coordinate |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1436 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1438 | `MakerDropdown` | Data To Copy |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1439 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1440 | `MakerButton` | Copy |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1441 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1518 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1519 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1536 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_Pushup/KK_Plugins/Pushup.cs` | 1552 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 368 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 413 | `MakerCategory` | RandomCharacterGeneratorCategory、Randomize |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 415 | `AddControl` | Set current character as template |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 415 | `MakerButton` | Set current character as template |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 420 | `AddControl` | Randomize! |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 420 | `MakerButton` | Randomize! |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 421 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 421 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 422 | `AddControl` | Randomize body sliders |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 422 | `MakerToggle` | Randomize body sliders |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 423 | `AddControl` | Randomize body type |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 423 | `MakerToggle` | Randomize body type |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 424 | `AddControl` | Skin color、White、Brown、Unchanged |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 424 | `MakerRadioButtons` | Skin color、White、Brown、Unchanged |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 425 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 425 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 426 | `AddControl` | Randomize face sliders |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 426 | `MakerToggle` | Randomize face sliders |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 427 | `AddControl` | Randomize eyes |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 427 | `MakerToggle` | Randomize eyes |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 428 | `AddControl` | Randomize face type |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 428 | `MakerToggle` | Randomize face type |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 429 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 429 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 430 | `AddControl` | Randomize hair type |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 430 | `MakerToggle` | Randomize hair type |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 431 | `AddControl` | Randomize hair color |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 431 | `MakerToggle` | Randomize hair color |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 432 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 432 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 433 | `AddControl` | Deviation |
| `KKS_Plugins/KKS_RandomCharacterGenerator/KK_Plugins/RandomCharacterGenerator.cs` | 433 | `MakerSlider` | Deviation |
| `KKS_Plugins/KKS_UncensorSelector/KK_Plugins/UncensorSelector.cs` | 1602 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_UncensorSelector/KK_Plugins/UncensorSelector.cs` | 1716 | `AddControl` | Uncensor Selector |
| `KKS_Plugins/KKS_UncensorSelector/KK_Plugins/UncensorSelector.cs` | 1716 | `MakerText` | Uncensor Selector |
| `KKS_Plugins/KKS_UncensorSelector/KK_Plugins/UncensorSelector.cs` | 1733 | `AddControl` | Body |
| `KKS_Plugins/KKS_UncensorSelector/KK_Plugins/UncensorSelector.cs` | 1733 | `MakerDropdown` | Body |
| `KKS_Plugins/KKS_UncensorSelector/KK_Plugins/UncensorSelector.cs` | 1755 | `AddControl` | Penis |
| `KKS_Plugins/KKS_UncensorSelector/KK_Plugins/UncensorSelector.cs` | 1755 | `MakerDropdown` | Penis |
| `KKS_Plugins/KKS_UncensorSelector/KK_Plugins/UncensorSelector.cs` | 1778 | `AddControl` | Balls |
| `KKS_Plugins/KKS_UncensorSelector/KK_Plugins/UncensorSelector.cs` | 1778 | `MakerDropdown` | Balls |
| `KKS_Plugins/KKS_UncensorSelector/KK_Plugins/UncensorSelector.cs` | 2179 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_UncensorSelector/KK_Plugins/UncensorSelector.cs` | 2205 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Plugins/KKS_UncensorSelector/KK_Plugins/UncensorSelector.cs` | 2247 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 419 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 445 | `AddControl` | Pregnancy |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 470 | `MakerCategory` | Pregnancy |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 474 | `AddControl` | Enable pregnancy progression |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 474 | `MakerToggle` | Enable pregnancy progression |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 479 | `AddControl` | If off, the character can't get pregnant and current pregnancy will stop progressing.、If on, belly will progressively get bigger. No other effects. |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 479 | `MakerText` | If off, the character can't get pregnant and current pregnancy will stop progressing.、If on, belly will progressively get bigger. No other effects. |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 485 | `AddControl` | Fertility |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 485 | `MakerSlider` | Fertility |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 490 | `AddControl` | How likely this character is to get pregnant. |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 490 | `MakerText` | How likely this character is to get pregnant. |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 495 | `AddControl` | Week of pregnancy |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 495 | `MakerSlider` | Week of pregnancy |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 502 | `AddControl` | If the character is pregnant when added to the game, the pregnancy will continue from this point.、The only way for male characters to get pregnant is to manually set this slider above 0. |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 502 | `MakerText` | If the character is pregnant when added to the game, the pregnancy will continue from this point.、The only way for male characters to get pregnant is to manually set this slider above 0. |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 508 | `AddControl` | Menstruation schedule、Default、More risky、Always safe、Always risky |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 508 | `MakerRadioButtons` | Menstruation schedule、Default、More risky、Always safe、Always risky |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 513 | `AddControl` | Changes how many risky days the character has in a cycle. Default is more safe days than risky days. |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 513 | `MakerText` | Changes how many risky days the character has in a cycle. Default is more safe days than risky days. |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 520 | `AddControl` | Always lactates |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 520 | `MakerToggle` | Always lactates |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 525 | `AddControl` | Makes the character always have milk, even when not pregnant. |
| `KKS_Pregnancy/KK_Pregnancy/PregnancyGui.cs` | 525 | `MakerText` | Makes the character always have milk, even when not pregnant. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1212 | `AddControl` | Copy Belly |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1225 | `AddControl` | Paste Belly |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1236 | `AddControl` | Reset Belly |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1249 | `AddControl` | Open BlendShapes |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1260 | `AddControl` | Open Individual Offsets |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1271 | `AddControl` | Apply Preset Shape |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1284 | `AddControl` | Pregnancy + |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1299 | `AddControl` |         Multiplier |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1314 | `AddControl` |         Roundness |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1329 | `AddControl` |         Move Y |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1344 | `AddControl` |         Move Z |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1359 | `AddControl` |         Stretch X |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1374 | `AddControl` |         Stretch Y |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1389 | `AddControl` |         Shift Y |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1404 | `AddControl` |         Shift Z |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1419 | `AddControl` |         Taper Y |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1434 | `AddControl` |         Taper Z |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1449 | `AddControl` |         Drop |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1464 | `AddControl` |         Cloth Offset |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1479 | `AddControl` |         Fat Fold |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1494 | `AddControl` |         Fat Fold Height |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1509 | `AddControl` |         Fat Fold Gap |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1524 | `AddControl` | Belly Mesh Smoothing |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1535 | `AddControl` | Include cloth when smoothing |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1636 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1686 | `MakerCategory` | Pregnancy+ |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1690 | `AddControl` | Enable Pregnancy+ |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1690 | `MakerToggle` | Enable Pregnancy+ |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1700 | `AddControl` | If disabled, you won't see any pregnant effects. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1700 | `MakerText` | If disabled, you won't see any pregnant effects. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1704 | `AddControl` | Apply Preset Shape |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1704 | `MakerDropdown` | Apply Preset Shape |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1710 | `AddControl` | Applies a preset belly shape to the character, so you don't have to deal with sliders. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1710 | `MakerText` | Applies a preset belly shape to the character, so you don't have to deal with sliders. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1714 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1714 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1724 | `AddControl` | The equivalent to number of weeks pregnant for Pregnancy+. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1724 | `MakerText` | The equivalent to number of weeks pregnant for Pregnancy+. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1729 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1729 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1739 | `AddControl` | Multiplies the base inflation size by this value. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1739 | `MakerText` | Multiplies the base inflation size by this value. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1744 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1744 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1754 | `AddControl` | Make the front of the belly more or less round |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1754 | `MakerText` | Make the front of the belly more or less round |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1759 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1759 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1769 | `AddControl` | Moves the belly sphere up and down. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1769 | `MakerText` | Moves the belly sphere up and down. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1774 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1774 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1784 | `AddControl` | Moves the belly sphere forward and back. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1784 | `MakerText` | Moves the belly sphere forward and back. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1789 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1789 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1799 | `AddControl` | Stretch the belly wider in the X direction. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1799 | `MakerText` | Stretch the belly wider in the X direction. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1804 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1804 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1814 | `AddControl` | Stretch the belly taller in the Y direction. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1814 | `MakerText` | Stretch the belly taller in the Y direction. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1819 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1819 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1829 | `AddControl` | Shift the front of the belly up and down. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1829 | `MakerText` | Shift the front of the belly up and down. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1834 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1834 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1844 | `AddControl` | Shift the front of the belly forward and back. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1844 | `MakerText` | Shift the front of the belly forward and back. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1849 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1849 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1859 | `AddControl` | Taper the sides of the belly in at the top and out at the bottom.  Makes an egg like shape. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1859 | `MakerText` | Taper the sides of the belly in at the top and out at the bottom.  Makes an egg like shape. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1864 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1864 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1874 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1874 | `MakerText` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1879 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1879 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1889 | `AddControl` | Give the belly the 'Dropped' effect |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1889 | `MakerText` | Give the belly the 'Dropped' effect |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1894 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1894 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1904 | `AddControl` | Control the distance between each clothing layer.  Will help reduce clipping. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1904 | `MakerText` | Control the distance between each clothing layer.  Will help reduce clipping. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1909 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1909 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1919 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1919 | `MakerText` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1924 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1924 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1934 | `AddControl` | Control the vertical position of the fat fold crease.  0 for default. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1934 | `MakerText` | Control the vertical position of the fat fold crease.  0 for default. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1939 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1939 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1949 | `AddControl` | Control the width of the fat fold gap, allowing you to shrink or widen it.  0 for default. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1949 | `MakerText` | Control the width of the fat fold gap, allowing you to shrink or widen it.  0 for default. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1954 | `AddControl` | Copy Belly |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1954 | `MakerButton` | Copy Belly |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1967 | `AddControl` | Copy the current belly shape. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1967 | `MakerText` | Copy the current belly shape. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1971 | `AddControl` | Paste Belly |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1971 | `MakerButton` | Paste Belly |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1984 | `AddControl` | Paste the last copied belly shape, even on different characters |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1984 | `MakerText` | Paste the last copied belly shape, even on different characters |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1988 | `AddControl` | Reset Belly |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 1988 | `MakerButton` | Reset Belly |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2001 | `AddControl` | Resets all P+ sliders to their default value |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2001 | `MakerText` | Resets all P+ sliders to their default value |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2005 | `AddControl` | Open Individual Offsets |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2005 | `MakerButton` | Open Individual Offsets |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2018 | `AddControl` | Set individual offset values for each article of clothing. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2018 | `MakerText` | Set individual offset values for each article of clothing. |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2022 | `AddControl` | Belly Mesh Smoothing |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2022 | `MakerButton` | Belly Mesh Smoothing |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2035 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2035 | `MakerText` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2039 | `AddControl` | Include cloth when smoothing |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2039 | `MakerToggle` | Include cloth when smoothing |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2044 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_PregnancyPlus/KK_PregnancyPlus/PregnancyPlusGui.cs` | 2044 | `MakerText` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 58 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 77 | `MakerCategory` | Sex Faces |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 79 | `AddControl` | Set the facial expression using the sidebar.\nThen use this menu to add it as a sex face. |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 79 | `MakerText` | Set the facial expression using the sidebar.\nThen use this menu to add it as a sex face. |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 80 | `AddControl` | Show during: |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 80 | `MakerRadioButtons` | Show during: |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 81 | `AddControl` | Experience: |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 81 | `MakerRadioButtons` | Experience: |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 90 | `AddControl` | Add Current Face |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 90 | `MakerButton` | Add Current Face |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 91 | `AddControl` | Delete |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 91 | `MakerButton` | Delete |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 93 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 93 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 94 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 94 | `MakerText` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 96 | `AddControl` | Face #{i + 1} |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 96 | `MakerButton` | Face #{i + 1} |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 108 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 108 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 109 | `AddControl` | Head |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 109 | `MakerText` | Head |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 110 | `AddControl` | Head Pitch |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 110 | `MakerSlider` | Head Pitch |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 111 | `AddControl` | Head Yaw |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 111 | `MakerSlider` | Head Yaw |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 112 | `AddControl` | Head Roll |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 112 | `MakerSlider` | Head Roll |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 113 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 113 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 114 | `AddControl` | Iris |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 114 | `MakerText` | Iris |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 115 | `AddControl` | Left Iris Scale |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 115 | `MakerSlider` | Left Iris Scale |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 116 | `AddControl` | Right Iris Scale |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 116 | `MakerSlider` | Right Iris Scale |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 121 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 121 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 122 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 122 | `MakerText` | 動態標籤；需追查呼叫參數 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 123 | `AddControl` | Pattern 1 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 123 | `MakerDropdown` | Pattern 1 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 124 | `AddControl` | Pattern 2 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 124 | `MakerDropdown` | Pattern 2 |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 125 | `AddControl` | Ratio |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 125 | `MakerSlider` | Ratio |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 126 | `AddControl` | Openness |
| `KKS_SexFaces/SexFaces/SexFacesGui.cs` | 126 | `MakerSlider` | Openness |
| `KKS_SkinEffects/KK_SkinEffects/SkinEffectsGui.cs` | 23 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKS_SkinEffects/KK_SkinEffects/SkinEffectsGui.cs` | 37 | `AddControl` | Stretched hymen |
| `KKS_SkinEffects/KK_SkinEffects/SkinEffectsGui.cs` | 37 | `MakerToggle` | Stretched hymen |
| `KKS_SkinEffects/KK_SkinEffects/SkinEffectsGui.cs` | 41 | `AddControl` | Makes it much less likely that she will bleed during the first time. |
| `KKS_SkinEffects/KK_SkinEffects/SkinEffectsGui.cs` | 41 | `MakerText` | Makes it much less likely that she will bleed during the first time. |
| `KKS_SkinEffects/KK_SkinEffects/SkinEffectsGui.cs` | 42 | `AddControl` | Hymen regenerates |
| `KKS_SkinEffects/KK_SkinEffects/SkinEffectsGui.cs` | 42 | `MakerToggle` | Hymen regenerates |
| `KKS_SkinEffects/KK_SkinEffects/SkinEffectsGui.cs` | 46 | `AddControl` | The hymen grows back after a good night's sleep (to the state before sex). |
| `KKS_SkinEffects/KK_SkinEffects/SkinEffectsGui.cs` | 46 | `MakerText` | The hymen grows back after a good night's sleep (to the state before sex). |
| `KKS_SkinEffects/KK_SkinEffects/SkinEffectsGui.cs` | 47 | `AddControl` | Fragile vagina |
| `KKS_SkinEffects/KK_SkinEffects/SkinEffectsGui.cs` | 47 | `MakerToggle` | Fragile vagina |
| `KKS_SkinEffects/KK_SkinEffects/SkinEffectsGui.cs` | 51 | `AddControl` | When going at it very roughly has a chance to bleed, be gentle! |
| `KKS_SkinEffects/KK_SkinEffects/SkinEffectsGui.cs` | 51 | `MakerText` | When going at it very roughly has a chance to bleed, be gentle! |
| `KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration.cs` | 142 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration.cs` | 151 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration.cs` | 160 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration.cs` | 169 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration.cs` | 178 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration.cs` | 187 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration.cs` | 196 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration.cs` | 205 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration.cs` | 214 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration.cs` | 223 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration/KKS_Studio_BetterPenetration.cs` | 232 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.Core/KKABMX_Core.cs` | 279 | `AddControl` | Show Bonemod |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 84 | `MakerCategory` | 00_FaceTop、tglHeadABM、Head |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 89 | `MakerCategory` | 00_FaceTop、tglEyelashUpABM、Upper Eyelashes |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 90 | `MakerCategory` | 00_FaceTop、tglEyelashLoABM、Lower Eyelashes |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 92 | `MakerCategory` | 00_FaceTop、tglEye02ABM、Eyes 2 |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 95 | `MakerCategory` | 00_FaceTop、tglMouth2ABM、Mouth 2 |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 98 | `MakerCategory` | 01_BodyTop、tglBreast2ABM、Chest 2 |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 99 | `MakerCategory` | 01_BodyTop、tglNipplesABM、Nipples |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 101 | `MakerCategory` | 01_BodyTop、tglUpper2ABM、Upper Body 2 |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 103 | `MakerCategory` | 01_BodyTop、tglLower2ABM、Lower Body 2 |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 105 | `MakerCategory` | 01_BodyTop、tglArm2ABM、Arms 2 |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 106 | `MakerCategory` | 01_BodyTop、tglForearmsABM、Forearms |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 107 | `MakerCategory` | 01_BodyTop、tglHandsABM、Hands |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 109 | `MakerCategory` | 01_BodyTop、tglThighsABM、Thighs |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 110 | `MakerCategory` | 01_BodyTop、tglFeetABM、Feet & Calves |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 112 | `MakerCategory` | 01_BodyTop、tglGenitalsABM、Genitals |
| `KKSABMX/KKABMX.GUI/InterfaceData.cs` | 114 | `MakerCategory` | 03_ClothesTop、tglSkirtSclABM、Skirt Scale |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 167 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 193 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 193 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 205 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 205 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 214 | `MakerLoadToggle` | Face Bonemod |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 215 | `MakerLoadToggle` | Body Bonemod |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 216 | `MakerCoordinateLoadToggle` | Bonemod |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 263 | `AddControl` | Hand、Both、Left、Right |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 263 | `MakerRadioButtons` | Hand、Both、Left、Right |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 267 | `AddControl` | Finger、All、1、2、3、4、5 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 267 | `MakerRadioButtons` | Finger、All、1、2、3、4、5 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 271 | `AddControl` | Segment、Base、Center、Tip |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 271 | `MakerRadioButtons` | Segment、Base、Center、Tip |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 276 | `AddControl` | Scale X |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 276 | `MakerSlider` | Scale X |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 280 | `AddControl` | Scale Y |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 280 | `MakerSlider` | Scale Y |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 284 | `AddControl` | Scale Z |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 284 | `MakerSlider` | Scale Z |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 288 | `AddControl` | Scale |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 288 | `MakerSlider` | Scale |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 487 | `AddControl` | Side to edit、Both、Left、Right |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 487 | `MakerRadioButtons` | Side to edit、Both、Left、Right |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 494 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 494 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 498 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 498 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 502 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 502 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 506 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 506 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 510 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSABMX/KKABMX.GUI/KKABMX_GUI.cs` | 510 | `MakerSlider` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI/CardAuthorData.cs` | 205 | `MakerAPI.RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI/CardAuthorData.cs` | 282 | `AddControl` | Not loaded yet... |
| `KKSAPI/KKAPI/CardAuthorData.cs` | 282 | `MakerText` | Not loaded yet... |
| `KKSAPI/KKAPI/CardAuthorData.cs` | 283 | `AddControl` | Open full author history |
| `KKSAPI/KKAPI/CardAuthorData.cs` | 283 | `MakerButton` | Open full author history |
| `KKSAPI/KKAPI/CardAuthorData.cs` | 284 | `AddControl` | Your nickname、Anonymous |
| `KKSAPI/KKAPI/CardAuthorData.cs` | 284 | `MakerTextbox` | Your nickname、Anonymous |
| `KKSAPI/KKAPI/CardAuthorData.cs` | 297 | `AddControl` | Your nickname will be saved to the card and used in the card's filename. This setting is global. |
| `KKSAPI/KKAPI/CardAuthorData.cs` | 297 | `MakerText` | Your nickname will be saved to the card and used in the card's filename. This setting is global. |
| `KKSAPI/KKAPI.Maker/MakerAPI.cs` | 269 | `RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/MakerAPI.cs` | 328 | `AddControl` | control |
| `KKSAPI/KKAPI.Maker/MakerAPI.cs` | 335 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/MakerAPI.cs` | 341 | `MakerAPI.RegisterCustomSubCategories` | category |
| `KKSAPI/KKAPI.Maker/MakerAPI.cs` | 368 | `AddEditableAccessoryWindowControl` | control |
| `KKSAPI/KKAPI.Maker/MakerAPI.cs` | 383 | `AddEditableAccessoryWindowControl` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/MakerAPI.cs` | 400 | `AddEditableAccessoryWindowControl` | control |
| `KKSAPI/KKAPI.Maker/MakerAPI.cs` | 415 | `AddEditableAccessoryWindowControl` | control |
| `KKSAPI/KKAPI.Maker/MakerAPI.cs` | 476 | `RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/MakerAPI.cs` | 481 | `RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 169 | `MakerCategory` | 00_FaceTop、tglAll、00_FaceTop、tglEar、00_FaceTop、tglChin、00_FaceTop、tglCheek、00_FaceTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 170 | `MakerCategory` | 00_FaceTop、tglEar、00_FaceTop、tglChin、00_FaceTop、tglCheek、00_FaceTop、tglEyebrow、00_FaceTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 171 | `MakerCategory` | 00_FaceTop、tglChin、00_FaceTop、tglCheek、00_FaceTop、tglEyebrow、00_FaceTop、tglEye01、00_FaceTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 172 | `MakerCategory` | 00_FaceTop、tglCheek、00_FaceTop、tglEyebrow、00_FaceTop、tglEye01、00_FaceTop、tglEye02、00_FaceTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 173 | `MakerCategory` | 00_FaceTop、tglEyebrow、00_FaceTop、tglEye01、00_FaceTop、tglEye02、00_FaceTop、tglNose、00_FaceTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 174 | `MakerCategory` | 00_FaceTop、tglEye01、00_FaceTop、tglEye02、00_FaceTop、tglNose、00_FaceTop、tglMouth、00_FaceTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 175 | `MakerCategory` | 00_FaceTop、tglEye02、00_FaceTop、tglNose、00_FaceTop、tglMouth、00_FaceTop、tglMole、00_FaceTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 176 | `MakerCategory` | 00_FaceTop、tglNose、00_FaceTop、tglMouth、00_FaceTop、tglMole、00_FaceTop、tglMakeup、00_FaceTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 177 | `MakerCategory` | 00_FaceTop、tglMouth、00_FaceTop、tglMole、00_FaceTop、tglMakeup、00_FaceTop、tglShape |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 178 | `MakerCategory` | 00_FaceTop、tglMole、00_FaceTop、tglMakeup、00_FaceTop、tglShape、01_BodyTop、tglAll、01_BodyTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 179 | `MakerCategory` | 00_FaceTop、tglMakeup、00_FaceTop、tglShape、01_BodyTop、tglAll、01_BodyTop、tglBreast、01_BodyTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 180 | `MakerCategory` | 00_FaceTop、tglShape、01_BodyTop、tglAll、01_BodyTop、tglBreast、01_BodyTop、tglUpper、01_BodyTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 181 | `MakerCategory` | 01_BodyTop、tglAll、01_BodyTop、tglBreast、01_BodyTop、tglUpper、01_BodyTop、tglLower、01_BodyTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 182 | `MakerCategory` | 01_BodyTop、tglBreast、01_BodyTop、tglUpper、01_BodyTop、tglLower、01_BodyTop、tglArm、01_BodyTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 183 | `MakerCategory` | 01_BodyTop、tglUpper、01_BodyTop、tglLower、01_BodyTop、tglArm、01_BodyTop、tglLeg、01_BodyTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 184 | `MakerCategory` | 01_BodyTop、tglLower、01_BodyTop、tglArm、01_BodyTop、tglLeg、01_BodyTop、tglNail、01_BodyTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 185 | `MakerCategory` | 01_BodyTop、tglArm、01_BodyTop、tglLeg、01_BodyTop、tglNail、01_BodyTop、tglUnderhair、01_BodyTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 186 | `MakerCategory` | 01_BodyTop、tglLeg、01_BodyTop、tglNail、01_BodyTop、tglUnderhair、01_BodyTop、tglSunburn |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 187 | `MakerCategory` | 01_BodyTop、tglNail、01_BodyTop、tglUnderhair、01_BodyTop、tglSunburn、01_BodyTop、tglPaint |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 188 | `MakerCategory` | 01_BodyTop、tglUnderhair、01_BodyTop、tglSunburn、01_BodyTop、tglPaint、01_BodyTop、tglShape |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 189 | `MakerCategory` | 01_BodyTop、tglSunburn、01_BodyTop、tglPaint、01_BodyTop、tglShape、02_HairTop、common、02_HairTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 190 | `MakerCategory` | 01_BodyTop、tglPaint、01_BodyTop、tglShape、02_HairTop、common、02_HairTop、tglBack、02_HairTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 191 | `MakerCategory` | 01_BodyTop、tglShape、02_HairTop、common、02_HairTop、tglBack、02_HairTop、tglFront、02_HairTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 192 | `MakerCategory` | 02_HairTop、common、02_HairTop、tglBack、02_HairTop、tglFront、02_HairTop、tglSide、02_HairTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 193 | `MakerCategory` | 02_HairTop、tglBack、02_HairTop、tglFront、02_HairTop、tglSide、02_HairTop、tglExtension、02_HairTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 194 | `MakerCategory` | 02_HairTop、tglFront、02_HairTop、tglSide、02_HairTop、tglExtension、02_HairTop、tglEtc |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 195 | `MakerCategory` | 02_HairTop、tglSide、02_HairTop、tglExtension、02_HairTop、tglEtc、03_ClothesTop、tglTop |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 196 | `MakerCategory` | 02_HairTop、tglExtension、02_HairTop、tglEtc、03_ClothesTop、tglTop、03_ClothesTop、tglBot |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 197 | `MakerCategory` | 02_HairTop、tglEtc、03_ClothesTop、tglTop、03_ClothesTop、tglBot、03_ClothesTop、tglBra |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 198 | `MakerCategory` | 03_ClothesTop、tglTop、03_ClothesTop、tglBot、03_ClothesTop、tglBra、03_ClothesTop、tglShorts |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 199 | `MakerCategory` | 03_ClothesTop、tglBot、03_ClothesTop、tglBra、03_ClothesTop、tglShorts、03_ClothesTop、tglGloves |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 200 | `MakerCategory` | 03_ClothesTop、tglBra、03_ClothesTop、tglShorts、03_ClothesTop、tglGloves、03_ClothesTop、tglPanst |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 201 | `MakerCategory` | 03_ClothesTop、tglShorts、03_ClothesTop、tglGloves、03_ClothesTop、tglPanst、03_ClothesTop、tglSocks |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 202 | `MakerCategory` | 03_ClothesTop、tglGloves、03_ClothesTop、tglPanst、03_ClothesTop、tglSocks、03_ClothesTop、tglOuterShoes |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 203 | `MakerCategory` | 03_ClothesTop、tglPanst、03_ClothesTop、tglSocks、03_ClothesTop、tglOuterShoes、03_ClothesTop、tglCopy |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 204 | `MakerCategory` | 03_ClothesTop、tglSocks、03_ClothesTop、tglOuterShoes、03_ClothesTop、tglCopy、05_ParameterTop、tglCharactor |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 205 | `MakerCategory` | 03_ClothesTop、tglOuterShoes、03_ClothesTop、tglCopy、05_ParameterTop、tglCharactor、05_ParameterTop、tglCharactorEx |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 206 | `MakerCategory` | 03_ClothesTop、tglCopy、05_ParameterTop、tglCharactor、05_ParameterTop、tglCharactorEx、05_ParameterTop、tglH |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 207 | `MakerCategory` | 05_ParameterTop、tglCharactor、05_ParameterTop、tglCharactorEx、05_ParameterTop、tglH、05_ParameterTop、tglAttribute |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 208 | `MakerCategory` | 05_ParameterTop、tglCharactorEx、05_ParameterTop、tglH、05_ParameterTop、tglAttribute、05_ParameterTop、tglADK |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 209 | `MakerCategory` | 05_ParameterTop、tglH、05_ParameterTop、tglAttribute、05_ParameterTop、tglADK、05_ParameterTop、tglInterest |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 210 | `MakerCategory` | 05_ParameterTop、tglAttribute、05_ParameterTop、tglADK、05_ParameterTop、tglInterest |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 211 | `MakerCategory` | 05_ParameterTop、tglADK、05_ParameterTop、tglInterest |
| `KKSAPI/KKAPI.Maker/MakerConstants.cs` | 212 | `MakerCategory` | 05_ParameterTop、tglInterest |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 230 | `MakerCategory` | 04_AccessoryTop、Slots |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 248 | `AddControl` | Can't add  |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 308 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 308 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 309 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 309 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 310 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 310 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 311 | `AddControl` | test toggle |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 311 | `MakerToggle` | test toggle |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 315 | `AddControl` | test btn |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 315 | `MakerButton` | test btn |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 327 | `AddControl` | test color |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 327 | `MakerColor` | test color |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 332 | `AddControl` | test toggle、t0、t1、t2、t3 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 332 | `MakerDropdown` | test toggle、t0、t1、t2、t3 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 336 | `AddControl` | radio btns、b1、b2 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 336 | `MakerRadioButtons` | radio btns、b1、b2 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 340 | `AddControl` | radio btns 1 row、b1、b2、b3、b4、b5、b6、b7、b8 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 340 | `MakerRadioButtons` | radio btns 1 row、b1、b2、b3、b4、b5、b6、b7、b8 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 344 | `AddControl` | radio btns 5 per row、b1、b2、b3、b4、b5、b6、b7、b8 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 344 | `MakerRadioButtons` | radio btns 5 per row、b1、b2、b3、b4、b5、b6、b7、b8 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 351 | `AddControl` | test slider |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 351 | `MakerSlider` | test slider |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 355 | `AddControl` | test text test text test text test text test text test text test text test text test text test text |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 355 | `MakerText` | test text test text test text test text test text test text test text test text test text test text |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 356 | `AddControl` | test textbox、String test |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 356 | `MakerTextbox` | test textbox、String test |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 360 | `MakerLoadToggle` | Test toggle |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 364 | `MakerLoadToggle` | Test toggle 2 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 368 | `MakerCoordinateLoadToggle` | Test toggle |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 372 | `MakerCoordinateLoadToggle` | Test toggle 2 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 384 | `MakerToggle` | test toggle、Toggled to {b} in accessory slot index {AccessoriesApi.SelectedMakerAccSlot} |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 388 | `MakerColor` | test accessory color |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 502 | `MakerSeparator` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/MakerInterfaceCreator.cs` | 502 | `MakerCategory` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/RegisterCustomControlsEvent.cs` | 15 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/RegisterCustomControlsEvent.cs` | 17 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker/RegisterSubCategoriesEvent.cs` | 10 | `RegisterCustomSubCategories` | 動態標籤；需追查呼叫參數 |
| `KKSAPI/KKAPI.Maker.UI/MakerButton.cs` | 39 | `MakerAPI.RegisterCustomSubCategories` | text、category、owner |
| `KKSAPI/KKAPI.Maker.UI/MakerColor.cs` | 64 | `MakerAPI.RegisterCustomSubCategories` | settingName、useAlpha |
| `KKSAPI/KKAPI.Maker.UI/MakerCoordinateLoadToggle.cs` | 55 | `MakerAPI.RegisterCustomSubCategories` | text |
| `KKSAPI/KKAPI.Maker.UI/MakerDropdown.cs` | 43 | `MakerAPI.RegisterCustomSubCategories` | settingName、options、category |
| `KKSAPI/KKAPI.Maker.UI/MakerImage.cs` | 68 | `MakerAPI.RegisterCustomSubCategories` | texture、category |
| `KKSAPI/KKAPI.Maker.UI/MakerLoadToggle.cs` | 44 | `MakerAPI.RegisterCustomSubCategories` | M:KKAPI.Maker.RegisterCustomControlsEvent.AddLoadToggle(KKAPI.Maker.UI.MakerLoadToggle)、text |
| `KKSAPI/KKAPI.Maker.UI/MakerRadioButtons.cs` | 49 | `MakerAPI.RegisterCustomSubCategories` | settingName、category、owner |
| `KKSAPI/KKAPI.Maker.UI/MakerRadioButtons.cs` | 61 | `MakerAPI.RegisterCustomSubCategories` | settingName、category、owner |
| `KKSAPI/KKAPI.Maker.UI/MakerSeparator.cs` | 51 | `MakerAPI.RegisterCustomSubCategories` | category、owner |
| `KKSAPI/KKAPI.Maker.UI/MakerSlider.cs` | 71 | `MakerAPI.RegisterCustomSubCategories` | settingName、category、owner |
| `KKSAPI/KKAPI.Maker.UI/MakerText.cs` | 63 | `MakerAPI.RegisterCustomSubCategories` | text、category、owner |
| `KKSAPI/KKAPI.Maker.UI/MakerTextbox.cs` | 69 | `MakerAPI.RegisterCustomSubCategories` | settingName、category、owner |
| `KKSAPI/KKAPI.Maker.UI/MakerToggle.cs` | 36 | `MakerAPI.RegisterCustomSubCategories` | displayName、category、owner |
| `KKSAPI/KKAPI.Maker.UI/MakerToggle.cs` | 47 | `MakerAPI.RegisterCustomSubCategories` | displayName、category、initialValue |
| `KKSAPI/KKAPI.Studio/StudioAPI.cs` | 231 | `AddControl` | Test 1 |
| `KKSAPI/KKAPI.Studio/StudioAPI.cs` | 250 | `AddControl` | Test 2 |
| `KKSAPI/KKAPI.Studio/StudioAPI.cs` | 254 | `AddControl` | Test 3 |
| `KKSAPI/KKAPI.Studio/StudioAPI.cs` | 259 | `AddControl` | Test add |
| `KKSAPI/KKAPI.Studio/StudioAPI.cs` | 263 | `AddControl` | Test slider |
| `KKSAPI/KKAPI.Studio/StudioAPI.cs` | 267 | `AddControl` | dropdown test、item 1、i2、test 3、dd  |
| `KKSAPI/KKAPI.Studio.UI/CurrentStateCategory.cs` | 118 | `AddControl` | 動態標籤；需追查呼叫參數 |
| `MoreAccessories/MoreAccessoriesKOI/MoreAccessories.cs` | 363 | `MakerMode` | 動態標籤；需追查呼叫參數 |
| `MoreAccessories/MoreAccessoriesKOI/MoreAccessories.cs` | 373 | `MakerMode` | 動態標籤；需追查呼叫參數 |
| `TranslationTools/KKS_TextResourceRedirector/IllusionMods/TextResourceRedirector.cs` | 191 | `MakerCustomDataHandler` | 動態標籤；需追查呼叫參數 |

## 逐功能實作與驗收清單（本輪進行中）

以下拆成使用者可操作的功能，不以 DLL 數量充當功能完成度。前段自動掃描提供來源位置；此段記錄可交付行為。未勾選項目不得宣稱已完成。

| 區域／模組 | 必須提供的功能 | 本輪狀態 |
|---|---|---|
| 原作頭髮 | 四部位主色／髮根／髮梢／輪廓／光澤／髮飾多層色，長度、位置、旋轉、縮放、擺動 | 通用屬性與 UI 已接入；髮色另存讀回及整批顏色模型變更 R9 通過，其他效果按項目續驗 |
| 原作眼睛／臉部 | 左右瞳孔主次色與漸層、眼白雙色、上下高光、眉毛、眼線、痣、唇線、眼影、腮紅、嘴唇、彩繪、光澤與位置 | 浮點／顏色／布林已接入，逐效果驗收未完成 |
| 原作身體 | 皮膚主次色、曬痕、乳頭／陰毛／指甲、彩繪、光澤、細節與附加線條 | 屬性 UI 已接入，逐效果驗收未完成 |
| 原作服裝 | 九槽、四層基色／花紋色、重複／偏移／旋轉、隱藏選項、徽章、座標保存／切換 | 圖片選擇與快速衣物狀態已有驗收；紋理重複數值另存及顏色編輯後衣物回歸 R9 通過 |
| 原作飾品 | 四層顏色、兩組位置／旋轉／縮放、55 個原作父節點、手動模組骨架、擺動、A／B 顯示分類 | 顏色／變形／父節點／擺動已接入；隱藏分類的整數選單尚待補 |
| MoreAccessories | 第 21 欄起的既有槽位顏色／變形／綁定／擺動，新增／刪除槽位，複製／鏡像／服裝間移轉 | 既有 XML 槽位讀寫、動態圖片目錄與新增欄位已實作，原生 R13 通過；R13 七項實際模型／新增／另存功能成功，但切換效能未通過；刪除／複製／鏡像／移轉仍待補 |
| MoreAccessoryParents／NodesConstraints | 原作節點以外的骨架綁定、跨飾品／物件約束、循環檢查 | 可輸入骨架名稱；約束圖與額外父節點 UI 尚待補 |
| MaterialEditor／ShaderSwapper | 顏色／浮點屬性、材質選擇、著色器、關鍵字、描邊、渲染順序、貼圖匯入／替換／UV、還原／複製 | 巢狀顏色／浮點存檔 adapter 已測試；其他功能及逐材質效果尚待補 |
| HairAccessoryCustomizer／HairShadowColor | 跟隨髮色、獨立飾品色、輪廓／光澤、髮長、陰影顏色 | 已接入現有可解碼屬性；模組專屬效果驗收待補 |
| Skin／Eye／Clothes Overlay | 身體／臉部／眼睛／服裝自訂圖片、各層開關、尺寸／UV、匯入／匯出、保存 | 原生已有部分載入；完整圖片編輯 UI 尚待補 |
| ABMX／SliderUnlocker | 骨架尺寸／位置／長度／旋轉、左右分開、座標專屬、範圍解鎖、保存／重置 | 已有 1192 控制與即時更新驗收；來源清單外的控制須另測 |
| Pregnancy+／Pregnancy／Bulge | 預設形狀、連續腹部形變、網格專屬參數、衣物相容、保存 | 已有 Pregnancy+ 預設／16 控制；其他模組功能不能視為等同支援 |
| Pushup／ClothingBlendShape | 胸罩／上衣塑形、啟用與壓平、每套座標參數、衣物與身體聯動 | 已有原生部分解析；通用數值接入後仍需逐項效果驗收 |
| DynamicBoneEditor／Colliders／ClothColliders | 骨架物理、阻尼／彈性／慣性／重力／外力、曲線、碰撞體、排除節點、開關、保存 | 原生部分支援；完整專屬編輯器尚待補 |
| FKIK／AdvIK／PoseTools | 骨架操作、IK 目標／限制、左右分開、姿勢保存／載入 | 左右手原作手勢已驗收；完整 FK／IK 編輯尚待補 |
| MoreOutfits／CoordinateLoadOption | 新增／刪除／複製套裝、選擇性載入、座標專屬參數、槽位與模組資料同步 | 已有座標切換；套裝管理全功能尚待補 |
| Maker 支援工具 | 中文搜尋、隨機、分類／收藏、色盤／滴管／色碼、預設、複製、復原／重做 | 既有搜尋／部分重置與隨機；RGBA 色碼、色塊與待套用撤銷 R9 通過；全域復原與完整色盤待補 |
| 角色基本資料／語音／顯示 | 名稱、性格／語音、角色參數、表情、背景、光照、相機、卡片選擇性保存／載入 | 既有部分支援，須按原作控制器清單補齊 |
| 效能與操作流程 | 身體參數即時更新、色彩／素材批次套用、詳細進度及增量資源重建 | 身體即時更新已有驗收；詳細控制每頁 64 項，全部資料與搜尋保留；空欄新增沿用所有快照，一般衣物／飾品只重建目前座標；真正單欄模型更新與 100 ms 尚未完成，R13 切換 51,403.91 ms |
| Studio／遊戲／VR 模組 | 場景、時間軸、攝影、遊戲流程或 VR 的功能 | 納入清單辨識依賴；不能將無關編輯資料硬塞入角色卡，需對應產品介面與獨立驗收 |

本輪驗收不代表上述未完成項目自動通過。錯誤、失敗建置與驗收報告保留在 CHARACTER_EDITOR_ERRORS.md 和 Saved/Tests；後續必須沿此清單繼續更新，不重複從頭盤點。

### 非同步載入進度 R19（2026-10-08）

主角色及衣物預覽已使用可取消背景原生載入／解碼、分批 UE renderer 建立、每幀最多一筆材質提交、背景服裝規則。素材替換保留攝影機、視角及動作狀態。最新 13 項外觀 GUI、8 項額外飾品功能與 11 項 UE 單元通過；正常假髮來源、綁定與頭頂高度回歸通過。

效能項仍未完成：嚴格 R19 點選至可見 42578.0167 ms，全部背景預載期間最低 8.6778 FPS，100 ms／持續 60 FPS 均失敗。原生單欄快照 helper 已通過測試但尚未接入生產；真正單欄擷取、未變動渲染資源重用及上表其他模組功能仍需完成。完整證據見 CHARACTER_EDITOR_ACCESSORY_PERFORMANCE.md，不以功能案例通過替代效能門檻。


## 網格重用與穩定匯出 R23

EditorMeshReuseR23：12 項 UE 測試全部通過，含跨 Session 幾何快取、資料失效、弱參照回收與原有編輯器回歸。EditorPropertiesMeshReuseR23：13 項外觀 GUI 通過，report.png 已檢視頭髮正確位於頭部。EditorMeshMorphIsolationR23：1 項實卡身體形變隔離通過。原生匯出回歸 5 項通過／4 項 ignored，另有 1 項實卡完整匯出契約通過。這些功能結果不代替嚴格切換耗時／FPS 驗收；原生單欄擷取與其他既列未完成功能仍未標成完成。

持久快取已加入有界最近使用淘汰，僅處理 session_blob／bundle_prefab，保留來源索引與現有失效條件。小容量滿庫恢復及全部 sqlcache 回歸 R24：11 項通過、2 項 ignored。跨程序讀取／淘汰競態保護補強後正在重跑 R25，UE 整合仍待建置。


### 隱藏預覽元件暫停 R26 功能驗收

EditorPerformanceBuildR26 成功（116.69 秒）。EditorInactivePreviewR26：6 項 UE Success／退出碼 0，含實卡全部 renderer 暫停、原始更新旗標精確還原、晚加入元件及 Actor 播放狀態保留、全部貼圖契約相容。EditorClothingInactivePreviewR26：7 項 GUI 通過／退出碼 0，連續 32 次預備狀態切換，最大 190.9821 ms（此驗收門檻為 200 ms，不能稱為所有切換均小於 100 ms）；穿著／半脫三角形數 25934／17616，全脫沒有可見衣物；身體遮罩 CRC 穿著／半脫／全脫為 1278951944／1552567581／2280405169。編輯身形／骨架、獨立左右手、動畫時鐘／暫停、快取上限與失效釋放均通過；report.png 已檢視正常。

隱藏角色只暫停自有 MeshComponent，記錄原始旗標，分批新建後再次納入，取出快取前還原；不修改全域動畫或 GC 設定。飾品嚴格效能 EditorExtraAccessoriesInactivePreviewR26 正在所有其他本次測試／編譯退出後單獨執行，尚未宣稱 100 ms／持續 60 FPS 已達成。


## R26 最終驗收結果（2026-10-08）

EditorExtraAccessoriesInactivePreviewR26：8 項飾品功能通過；頭髮位於頭部，report.png 已檢視。啟動／結束二進位雜湊核對成功。本次其他建置及測試全部退出後才啟動，採樣從可見替換完成持續到服裝規則、原生工作、分批建立與所有材質就緒，未刪除長幀。

| 指標 | R26 實測 | 要求／判定 |
|---|---:|---|
| 點選至可見完成 | 36880.3430 ms | ≤100 ms，失敗 |
| 完成後採樣 | 20,029 幀 | 含全部背景預載 |
| 平均 FPS | 200.8508 | 不代替最低 FPS |
| 最低 FPS | 16.3567 | ≥60 FPS，失敗 |
| 最長幀 | 61.1369 ms | 超出 16.667 ms |
| 超出幀預算 | 39 幀 | 持續 60 FPS 未達成 |

報告：Saved/Tests/EditorAcceptance/EditorExtraAccessoriesInactivePreviewR26/report.json、report.png、launch.json、exit.txt；整體 valid=false／退出碼 1。R25 的超預算 140 幀／最長 104.4112 ms，本輪為 39 幀／61.1369 ms；僅記錄單次觀察，不以此宣稱穩定改善或通過。

本輪已驗證的正式功能：完整內容指紋之有界弱參照骨骼網格重用、穩定骨架匯出、容量滿後可恢復寫入的持久快取淘汰、隱藏衣物預覽的顯示元件暫停與精確還原。原生快取 11 項、UE R25 12 項、外觀 R25 13 項通過；R26 修改後另有 6 項 UE、7 項衣物 GUI／32 次切換及 8 項飾品功能通過。未將 ignored、失敗效能或舊版案例當成最新版全部功能通過。

100 ms／持續 60 FPS 仍未完成。原生背景衣物開啟實測 28–35 秒，shared_edited_outfit 仍完整擷取目前套裝，UE 仍建立整套預覽元件。下一必要工作是共用真正單欄擷取路徑並接入已有快照 patch：保留原始身體／頭部貼圖池、選定髮光澤、原作選項／採樣及完整參考骨架；不能以合成後的皮膚貼圖近似來源。再以來源欄位識別替換 renderer，正確重映射索引並保留其他元件、材質、攝影機與動作狀態。既有單欄快照 helper 的測試通過不代表此生產路徑已接入。其他原作／模組尚缺功能仍以盤點表為準，未宣稱全數完成。

## R27 單欄來源更新（2026-10-08）

飾品正式 apply 已接入共用單欄來源擷取及 portable patch；保留原始貼圖池、骨架、材質／位移／綁定契約、其他部件與服裝。舊卡資料不足時完整恢復，未用名稱猜測或合成貼圖近似。此項取代 R26「來源單欄尚未接入」的待辦狀態；UE 顯示元件保留及最終 100 ms／持續 60 FPS 仍未完成。

原生 17 項通過（另 5 項明列 ignored）、新版建置成功、UE 12 項／外觀 GUI 13 項通過；衣物隔離驗收 7 項／32 次切換通過，最長 165.0008 ms，不等於小於 100 ms。飾品新舊卡各 8 項功能通過，嚴格效能失敗均保留。詳細原始報告、來源解析／衣物狀態指紋修正、環境及剩餘通用改造見 [R27 驗收](CHARACTER_EDITOR_SINGLE_SLOT_R27.md)。錯誤 E83–E91 記錄於 CHARACTER_EDITOR_ERRORS.md。其他原作／模組盤點缺項尚未宣稱完成。

R27 最終單欄隔離驗收 EditorSingleSlotExtraIsolatedR27：8 項功能通過，頭髮截圖已檢視；點選至可見 4906.1713 ms，30832 幀最低 13.6358 FPS、最長 73.3365 ms、15 幀超預算，整體失敗。平均 371.6902 FPS 不代替最低幀率。來源單欄已完成，但 UE 顯示／保存／背景預備的完整效能仍未完成；詳 CHARACTER_EDITOR_SINGLE_SLOT_R27.md 與錯誤 E92。

## R28–R30 背景交易與來源重用（2026-10-08）

R28 已將外觀／詳細設定的卡片保存、原生請求及輸出讀回放入同一背景交易；輸出解析成功且選角／套裝版本仍一致才發布。新增真實卡片交易及失敗不提交測試。建置、13 項 UE 回歸、13 項詳細設定 GUI 均成功；32 次服裝切換功能成功，最大 155.8938 ms，仍不等於 100 ms。詳 [R28](CHARACTER_EDITOR_ASYNC_CARD_R28.md)。

R29 已完成有檔案通知失效機制的四目錄有限不可變原作清單快取；通知不可用、不完整讀取或載入中變動均不發布快取。1,851 筆真實清單全部欄位精確一致；原生 17 項不同測試及 UE 13 項成功。飾品 8 項功能成功，假髮位置正確，但點選至可見 5281.9743 ms、最低 6.19176 FPS，嚴格效能失敗。詳 [R29](CHARACTER_EDITOR_CATALOG_REUSE_R29.md)。

R30 正以 CPU 時序定位並移走遺漏的骨架參考資料懶建立工作，已實作背景預備及完整資料契約，正在建置及驗收。診斷追蹤不能取代關閉追蹤的正式逐幀結果；不能以平均 FPS 宣稱持續 60 FPS。原作／mod 盤點中其他缺項仍未宣稱完成。詳 [R30](CHARACTER_EDITOR_TIMING_R30.md)。

## R30–R33 最新驗收狀態（2026-10-08）

R30 背景骨架參考預備已完成建置及 14 項 UE 測試；正式未開追蹤的飾品功能 8 項成功，切換 7212.3536 ms、最低 19.8483 FPS，效能未達標。R31 補正首次載入模式判定並將單欄更新限縮為部件資源序列化，真實來源 34 部件、640 資源全部精確一致；序列化節省僅約 1–3 ms，不能當作已解決主要瓶頸。詳 CHARACTER_EDITOR_TIMING_R30.md、CHARACTER_EDITOR_PART_SERIALIZATION_R31.md。

R32 跨工作執行緒的遷移快取完成：原生 33 項不同測試、UE 15 項、詳細設定 GUI 13 項、衣物 GUI 7 項／32 次切換及飾品 GUI 8 項功能皆成功。詳細設定截圖確認假髮在頭部，顏色、綁骨架、旋轉縮放、保存及半脫／恢復已實際操作。衣物最大 184.9502 ms；飾品切換 5592.725798 ms，27611 幀最低 6.047708 FPS，165 幀超預算，嚴格效能仍失敗。測試期間另有 E 槽 UnrealEditor，保留環境資訊，不宣稱隔離或穩定改善。詳 CHARACTER_EDITOR_MIGRATION_REUSE_R32.md。

R33 已完成來源通知外部連結失效測試及圖片目錄背景遷移預備；新增 RHI fence 以確認資源就緒，正在進行新版建置及實際渲染測試。原生 17 項模組、13 項清單及 4 項來源／卡片契約通過，3 項 ignored 與 1 項 ignored 分別保留；安裝來源 91 GUID 冷讀 1555.072 ms，通知驗證重用平均 0.057 ms。這是來源快取指標，不能代替整體切換時間。100 ms／持續 60 FPS 和盤點中的其他原作／mod 缺項尚未完成，不宣稱全功能驗收成功。詳 CHARACTER_EDITOR_SOURCE_PREPARATION_R33.md，錯誤紀錄至 E109。

### R33 完成結果

34 項不同原生測試、16 項 UE 整合測試、13 項詳細設定 GUI 及 8 項額外飾品 GUI 功能成功；建置 120.55 秒。兩份 GUI 截圖確認假髮位置正確。衣物完成 32 次切換與 5 項功能檢查，但最大 212.9688002 ms，因速度斷言失敗停止，後續兩項未計通過。

飾品切換 6446.7418976 ms，25,543 幀最低 3.7552381 FPS、最長 266.2946992 ms，141 幀超預算，整體失敗；平均 187.5677081 FPS 不取代最低值。啟動／結束無其他 UE 的兩份環境快照、二進位雜湊、來源 receipt、背景交易時序及完整幀資料都已保存。RHI 就緒及外部連結來源失效修正已完成，100 ms／持續 60 FPS 未完成。詳 CHARACTER_EDITOR_SOURCE_PREPARATION_R33.md，錯誤至 E113。

R34 已列計畫並實作五階段初始化及各階段取消／重啟測試，正在建置；尚未當作驗收成功。詳 CHARACTER_EDITOR_INCREMENTAL_SETUP_R34.md。

### 最新效能要求（2026-10-08 使用者更新）

每個飾品點選至可見的門檻由 100 ms 調整為 300 ms；切換後持續至少 60 FPS，仍包含背景預備期間。新報告明列 switch_budget_ms=300、switch_budget_pass、switch_300ms_pass；原 switch_100ms_pass 保留原本 100 ms 意義作歷史比較，不改寫舊報告。此變更沒有調整衣物驗收的既有 200 ms 門檻。


### R34 初始化拆分驗收

新版成功建置（113.55 秒），17 項 UE 整合測試成功，包括五階段取消／重啟、渲染就緒與實際同步／增量貼圖資料一致。正式衣物 7 項／32 次切換成功，最大 175.6240986 ms；診斷標籤另保存，不混作正式數字。

正式飾品 8 項功能成功，假髮截圖已檢視；報告使用使用者更新的 300 ms 門檻。切換 5186.2038001 ms，27,008 幀最低 5.1937502 FPS、最長 192.5391033 ms、38 幀超預算，整體失敗。環境啟動快照無其他 UE，結束快照出現 E:/PhysX UE，不能宣稱完整期間隔離，亦不直接歸因外部程序。完整報告、二進位雜湊及時序保留於 EditorIncrementalSetupExtraAccessoriesR34。

R35 已先量到 7,316 ZIP 清單每次重掃約 53–72 ms，再實作有通知失效及連結保守回退的跨執行緒快取，原生測試進行中。未將此項快取命中當作 300 ms／持續 60 FPS 已完成。詳 CHARACTER_EDITOR_INCREMENTAL_SETUP_R34.md、CHARACTER_EDITOR_INVENTORY_REUSE_R35.md，錯誤至 E115。

### R35 正式驗收結果

50 項不同原生測試及 17 項 UE 整合測試成功。外觀 Properties 13 項、衣物 ClothesFast 7 項／32 次切換成功（最大 198.4203979 ms）。三次 GUI 的兩個 DLL 雜湊皆與啟動紀錄一致，未開 CPU 追蹤；截圖均已檢視，假髮位於頭部。

EditorInventoryExtraAccessoriesR35 的 8 項功能成功，但效能失敗：switch_budget_ms=300、switch_budget_pass=false、switch_300ms_pass=false；切換 5154.4246003 ms，34,396 幀最低 6.1119539 FPS、最長 163.6138000 ms、138 幀超過 16.667 ms。平均 204.6888788 FPS 不能替代持續 60 FPS。完整背景期間未排除。來源 receipt 證實只重建第 20 飾品一個部件、重用 34 部件及其他 6 套服裝；來源擷取 437.9242 ms，本身已超過 300 ms。工作執行緒原生請求 968.9758 ms，輸入另存 241.5384 ms、結果讀取 177.0170 ms，總可見切換仍 5.154 秒。

驗收啟動有外部 E:/PhysX UE，結束沒有；此為兩個時點快照，不能宣稱全程隔離或將失敗歸因於外部程序。exit.txt 為 0，但 report.valid=false，正式 PowerShell 驗收據此正確拋出失敗；不以程序退出碼掩蓋未達標。來源 receipt、交易時序、完整逐幀報告保存在 Saved/Tests/EditorAcceptance/EditorInventoryExtraAccessoriesR35。300 ms 門檻更新完成；目前效能仍未達標，不標記全部需求完成。

### R37 角色卡改成連續捲動

移除角色卡清單分頁，使用填滿剩餘視窗高度的虛擬化 UListView 與正式列 Blueprint。可見列單一背景工作補名稱，不讀取整目錄大型卡片。EditorCardScrollFinalGuiR37 實際 100 張、5 項 GUI 成功，最後 C099 可視、可選、可載入 3D，能返回頂部；截圖已檢視，詳 CHARACTER_EDITOR_CARD_SCROLL_R37.md。這是清單與底部可操作性驗證，不代表先前飾品 300 ms／持續 60 FPS 已通過。
