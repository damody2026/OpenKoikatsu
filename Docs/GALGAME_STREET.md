# 街頭對話的 galgame 演出

StreetSocial（`StreetSocial.cmd`）的交談改用 ok-story 的即時演出，和 OK Studio 的劇本播放（[使用說明](GALGAME_STORY_USAGE.md)）是同一套：

- **底部對話框**：顯示說話者名字（角色色）與台詞。文字隨語音進度逐字打出；沒有語音時依固定速度打字。
- **左下即時立繪**：拍的是說話者本人的 3D 模型（正交 SceneCapture，只拍她自己的元件），表情與嘴型與場上同步。
- **逐句鏡頭**：她說話時在「說話者特寫」與「雙人」之間交替；主角說話時改用「越過主角肩膀看她」（`over`）。每個鏡頭先產生多個候選位置，再用深度探針避開牆壁；構圖留出底部對話框的高度。
- **路人不擋鏡頭**：站在鏡頭與兩人之間的其他角色，只在主畫面暫時隱藏，走開後恢復。

舊的街頭對話框在演出開啟時收起，只在餐桌上有菜、或麥克風剛聽到還沒送出的話時，縮小留在新對話框上方；右側面板（話題、按鈕、麥克風音量）不變，新對話框讓出右側 432px。越肩鏡頭會依兩人距離加大側角，主角只露出肩膀。

## 分工

| 部分 | 位置 | 做什麼 |
|---|---|---|
| 演出邏輯 | `Tools/ok-story/src/live.rs`（`ok_story.dll` 的 `oks_live_*`） | 收到「誰說了什麼」，決定打字進度、立繪、鏡頭與探針；C ABI 以 JSON 溝通 |
| 街頭接線 | `Source/OpenKoikatsu/OKStreetSocialGalgame.inl` | 把街頭狀態（劇本台詞、即時對話字幕、語音進度、骨架位置、相機）轉成輸入與 facts，套用回傳的鏡頭，拍立繪、跑探針 |
| 畫面 | `Plugins/OKStudio/.../SOKStoryView`、`OKStoryCapture`、`OKStoryLive` | 與 OK Studio 共用的對話框 widget、立繪擷取、探針與 DLL 載入 |

C++ 只負責呈現；鏡頭選擇、打字速度、構圖等規則都在 Rust，調整見 `Tools/ok-story/src/tuning.rs`（`live`、`camera` 區段）。

## 開關與檔案

- `ok.Street.Galgame 1`（預設）開啟；`0` 恢復舊對話框。
- `ok_story.dll`：打包時由 `Tools/stage_street_social.lua` 複製到執行檔旁；`run_street_social_development.lua` 會建置並以 `OK_STORY_DLL` 指定。找不到 DLL 時保留舊對話框，log 會寫 `Street galgame presenter unavailable`。
- 每送出一句台詞，log 會記一行 `Street galgame line c<slot>: ...`，可用來檢查哪一句換了鏡頭。

## 什麼時候會演出

- 站著交談（按 E 開始、話題、告白等劇本台詞）：對話框、立繪與鏡頭都會用。
- 即時對話（ok-mind／語音）：主角與她的字幕各自成為一句；她想回答時的「……」不算一句，主角那句會留到她真正開口。劇本台詞裡主角那句至少顯示 1.2 秒。
- 一起散步、約會途中的對話：只換對話框與立繪，鏡頭維持街頭本來的跟隨鏡頭。
- 拍照時不搶鏡頭。

## 開發測試

不用打包，直接用 Editor 開街頭：

```powershell
.\Tools\lua\lua.exe Tools\run_street_social_development.lua -Editor -Control -Window 1600x900
```

`-Editor` 以 `UnrealEditor.exe <專案> <地圖> -game` 執行，並在 manifest 加 `editor_common_materials`（Editor 沒有 cook 過的 shader library）。接著用 `Tools/street_play.lua`（`state`、`key E`、`key One`、`shot`）操作與截圖。Editor 模式下 `nav girl` 自動導航不會走，要用 `look` 加 `key W` 走到她旁邊。

## 已知限制

- 立繪與鏡頭以骨架位置計算，坐著、躺著等大幅姿勢的構圖還沒專門調過。
- 路人只在擋住鏡頭時隱藏，從畫面邊緣走過的人仍會入鏡。
- 多人對話（群聊）：每句台詞的立繪與鏡頭跟著說話的人（2026-10-26 Play2E 截圖確認，貞德與胡桃輪流說話時各自換立繪、鏡頭轉向她）；兩人以上同框的構圖還沒專門調過。

## 群聊台詞被整句丟掉（2026-10-26）

繁中遊戲裡，角色卡名字是簡體的女主（「贞德」）在群聊裡一句都沒出現：模型回覆的 `who` 寫成繁體「貞德」，`FinishPartyTalk` 找不到這個名字的人就把那句丟掉，另一位還在回應她「說過」的話。名字比對（`OKStreetScriptedName`）原本只在 zh-CN 轉成簡體，現在 zh-TW 也轉成繁體再比；找不到人的台詞會在 log 記 `Street party talk: line of "<who>" dropped`。

群聊提示裡的名字（`SpokenName`）也改用遊戲的字體：原本 persona 寫簡體的「贞德」在繁中遊戲裡以【贞德】給模型，模型卻以「貞德」回答，同一個人在記憶裡有兩種寫法。

語音伺服器忙時，正在播的串流語音中途斷掉（ran dry 再逾時），原本要先拿這一句的檔案（只是給記錄用，又等一次逾時）才去要下一句，群聊裡下一位女主的台詞因此晚了半分鐘以上；現在這種情況下一句立刻送出。Play3E／Play4E 量測時 GPU 上同時有別的遊戲與語音伺服器，測試用 ok-tts 每句要 15–30 秒，貞德·Alter 的台詞在測試的等待時間內沒輪到，沒能在畫面上確認；台詞本身模型有寫（`Street party talk: 2–4 lines`），比對也對得上。演出等語音的上限仍是 120 秒（刻意的設計：忙碌時寧可等聲音）。測試用 ok-tts 的 log 顯示 6 秒的語音生成了 88 秒、排隊最久 79 秒：GPU 上同時跑著 C 的遊戲、C 的 ok-tts、ok-song 與 StreetSocialCity，是共用 GPU 的負載，不是遊戲的問題。改用關掉語音（`V`，測完切回 TTS）的群聊測試（Play5E／Play6E），貞德與貞德·Alter 輪流說話，立繪與鏡頭都跟著說話的人，兩人名字互相包含也沒認錯。

對話框的名字標籤也改用遊戲的字體（原本是卡片的「贞德」）；模型偶爾寫的「⋯」（U+22EF）對話框字型沒有，顯示成方塊，改成「…」（`OKStreetGalgameWords`）。Play7E 讓主角說出約 60 個常見全形標點與符號逐一截圖確認：其餘（『』《》〈〉【】‥︙―﹏♪☆★♡❤※→ 等）都在字型裡；「〜」（U+301C）畫成一道弧線，也改成全形「～」。

## 打包版

立繪材質是 `/Game/OKStudio/Story/M_StoryPortrait`（DirectoriesToAlwaysCook）。改了 `OKStoryBuildPortraitMaterial` 之後，要重新存檔：

```powershell
& 'E:\PhysX\UE5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' OpenKoikatsu.uproject -run=OKStorySavePortrait -unattended -nullrhi
```

找不到這份資產時，log 會寫 `Story portrait material ... missing`，打包版就沒有立繪；Editor 會改用在記憶體裡建的材質。

## 歷史記錄回放也有立繪（2026-10-24）

「回放最近 20 句」和歷史視窗每一行的「播放」都當成 galgame 的台詞演出：對話框、說話者的左下角立繪，她有語音的句子打字跟語音同步；沒有語音的句子（包括男主的話）依字數停留 1.5～6 秒。沒在對話時從歷史視窗播放，會以該句所屬的女主暫時開一段演出（不動鏡頭），播完、語音停了就收起。關掉 galgame（`ok.Street.Galgame 0`）時維持舊行為：只重播她的語音，播放鈕只出現在有語音的行。

回放中途按「停止回放」或回放播完而對話還在進行：對話框回到這段對話她最後說的那句。回放中按 Esc 結束對話，演出立即收起（無語音句的停留時間會一起清掉）。

## 第一人稱對話鏡頭（2026-10-24）

對話時的鏡頭從男主的眼睛看向女主（ok-story `pov` 鏡頭，`tuning.json` 的 `live.pov` 預設開）：她說話時看她，他說話時看對話對象；群聊時另一位女主說話，鏡頭轉向她。眼睛位置在男主頭骨往後 12 cm、往上 6 cm（`pov_forward`／`pov_up`，往後是為了她靠近或彎腰時仍框得住），水平視角 60°（`pov_fov`），畫面中心在她臉到胸口之間 35% 處（`pov_aim`）。鏡頭在男主頭部 45 cm 內時，男主自己的模型從主畫面隱藏（立繪照拍）。`live.pov: false` 回到舊的特寫／雙人／過肩輪替。

## 多段語音的打字同步（2026-10-24）

自由對話她的一則回覆會切成好幾段 TTS（一兩句一段），畫面上是同一句台詞；回放的一行也可能有好幾個語音檔（`a.ogg|b.ogg`）。打字進度原本只看正在播的那一段，第一段播完整句就打完了。現在進度是「這位說話者已開始的段數－1＋目前這段的進度」除以她的總段數，段與段之間等下一段時也算在說（`playing`），回放的多檔語音同理。回放結束要把對話框還原成她最後一句時，若畫面上已經是同一句就不重送（不會重打一次）。

## 對話時她抬頭看著他（2026-10-24）

面對面對話時（`TickFaceTalkPartner` 會把她轉向他的那些情況），她的頸與頭轉向男主的眼睛（他的 `cm_j_head` 上方 6 cm），不再跟著待機動作看前方或低頭看他的胸口。做法是人群動畫本身的頭部注視（`OKSetCrowdRuntimeHeadLook`）：每幀用 CPU 取樣她目前動作的頭部方向，算出轉向目標所需的旋轉（相對身體左右 60°、下 25°／上 30° 內），頸部分 40%、頭部分其餘，換成兩根骨在各自父骨座標下的前乘旋轉，交給 GPU 姿態 shader（`IndependentPose.usf` 的 `LookRotation`）與 CPU 姿態；保留動作原本頭部動態的 20%，開始與結束各約半秒淡入淡出。她被自己的活動占住時（同行配對、設施、睡覺、親密姿勢等）不轉頭，對話結束就放開。

這改了 KKCrowdGpuPacking 的全域 shader，所以只重編執行檔的 stage 還要新的全域 shader：`Tools/rebuild_street_social_patch.lua` 現在多一步，用專案本身 cook 引擎的 Entry 地圖產生 `Engine/GlobalShaderCache-*.bin`，打成 `RuntimeGlobalShaderPatch-*_P.pak` 放進 patch 資料夾（`-pakdir` 掛載，優先於 base 裡的舊快取）。執行前 Editor 要先用目前的程式碼編過。

## 立繪不畫最外層輪廓線（2026-10-24）

左下角立繪的外框是角色描邊（outline 殼）在剪影外圍的那一圈。立繪的顏色照拍（臉、頭髮之間的內部線條保留），但決定透明範圍的 coverage 那一張拍攝時暫時隱藏她的 outline 殼（`OKSetCrowdRuntimeOutlinesShown`，材質的 `__OK_RuntimeRevealOverride` 設 0，拍完設回 -1），於是剪影外圍那圈描邊落在透明區、不顯示。`FOKStoryPortraitCapture::Update` 新增選用的 `CoverageScope` 回呼做這件事。coverage 原本當成主畫面的額外 pass 在幀尾才畫（`bRenderInMainRenderer`），那時 outline 早已設回顯示，所以有 scope 時 coverage 改成在 `CaptureScene` 當下自己畫一次（無光照、無後製，每兩幀一次）；`ok.Street.GalgamePortraitOutline 1` 回到保留外框、coverage 跟主畫面一起畫的舊做法。coverage 原本只有顏色的一半解析度，雙線性放大後邊緣會多透出一兩個像素，正好是那圈描邊，所以改成與顏色同解析度。對話時頭部注視可用 `ok.HeadLook 0` 關掉（A/B 比較用）。

走路時（她跟著他走、邊走邊說）立繪外框會閃：顏色在第 N 幀拍、coverage 在第 N+1 幀用第 N 幀的鏡頭拍，她在這一幀間已經移動，裁切邊緣就偏離剪影，一側露出描邊、另一側被切掉，每次擷取來回跳。現在隱藏描邊時，coverage 在同一幀緊接著顏色拍（反正它已經是當下自己畫），同一個姿勢，邊緣對齊。

## 第一人稱被家具擋住時改用一般鏡頭（2026-10-25）

兩人隔著桌子、檯燈或書架說話時，第一人稱鏡頭只拍到家具（她就在 1.2 m 外，被桌上的檯燈整個擋住）。現在每一句取景時，第一人稱的候選（她的臉 → 他的眼睛）排第一，後面接這一句一般鏡頭（特寫／雙人／過肩）的候選，一起送 probe 給遊戲量視線。第一人稱看得到她就照用；看不到就換成第一個視線暢通的一般鏡頭，該句之後的重新取景也跟著那個鏡頭（`LiveState.pov_fallback`，`Tools/ok-story/src/live.rs`）。下一句再從第一人稱開始檢查。

## 有語音的句子先等語音（2026-10-25）

生成的語音常在文字出現幾秒後才開始播（TTS 忙碌時排隊更久）。打字機原本在語音開始前就用閱讀速度打字，字跑在聲音前面，等語音開始時整句早已打完，同步就沒了。現在標記有語音的句子會先等，最多 `live.voice_wait`（預設 2.5 秒，`tuning.json` 可調），這段時間不打字；語音一開始就跟著它的進度打，逾時才改用閱讀速度。

## 台詞整句顯示、點擊跳過、滾輪回滾（2026-10-25）

- **整句顯示**：台詞一出現就整句顯示，不再逐字打出（`live.instant_text`，預設開；關掉時回到跟著語音打字，上一節的 `voice_wait` 仍有效）。
- **跳過**：場景中按左鍵（沒點到她身上可觸碰的部位時）、Enter 或 Space，畫面上這一句立刻結束（`SkipGalgameLine`）：
  - 回放中：換下一句回放。
  - 她的自由對話回覆：停掉語音，這次回覆中同一位說話者接下來的語音段落也一起略過。
  - 劇本台詞：結束並算作說過。
  - 他自己的語音：停掉。
  - 她唱歌不受影響。
  - 聊天輸入框裡的 Enter 照舊送出訊息（那時遊戲畫面沒有焦點）。
- **回滾**：場景中滾輪往上，對話框退回這段對話的上一句（含立繪），往下前進，回到最新一句就恢復即時；場景中滾輪不再縮放街區鏡頭。
  - 他每說一句話之前，記一個回滾點：已顯示的句數、自由對話記錄序號、她的對話記憶、她的事件歷史、她的關係條目（好感、色氣、階段），以及 ok-mind 的回滾點（對話則數、壓縮次數、下一筆記憶的 id；新的查詢 `checkpoint`）。
  - 停在較早的一句時他再開口（打字或麥克風），就回到那一句之後的回滾點（`GalgameRollBack`）：
    - 截掉之後的記錄和事件歷史（事件歷史檔會重寫）；
    - 還原對話記憶和她的關係條目；
    - ok-mind 截掉之後的對話並刪掉之後新增的記憶（新的查詢 `rollback`；期間若做過對話壓縮，對話維持原樣，只刪記憶）。
  - 他自己那句也會被撤回，等於重新說一次。
  - 只有他自己說的話才會回滾。遊戲替她送的話（好感到了自動變朋友後她說「我們算朋友了」、結帳提示、走路時的情境）不是他的話：2026-10-26 以前，這種話剛好在他用滾輪往回看時送出，就會把之後的台詞當成他重說而撤掉。
  - 金錢、帳本、行程等其他遊戲狀態不回滾。

## 回歸測試（2026-10-25）

每次打包後一鍵驗證街頭 galgame：

```
Tools\lua\lua.exe Tools\galgame_regression.lua --launch [--girl 3] [--keep]
```

不加 `--launch` 時用本 checkout 控制埠上已在跑的遊戲。腳本叫女主過來、按 E 開始對話，逐項回報 PASS／FAIL／WARN，有 FAIL 時結束碼為 1：

| 項目 | 檢查 |
|---|---|
| scene | galgame 呈現接手對話（`/galgame` active、partner 是她） |
| pov | 她回答時是第一人稱鏡頭（presenter shot `pov`；被擋住時會換一般鏡頭，換個地方再跑） |
| portrait | 立繪顯示並已擷取 |
| text | 台詞整句顯示（已顯示字數 = 總字數） |
| skip | 她的語音台詞按 Space 跳過（log「skipped the line」） |
| wheel | 滾輪往上兩格，畫面回到之前的句子 |
| rollback | 回滾後再開口，對話回到那一句（log「rollback: back to line」，記錄與事件歷史縮短） |
| memory | 回滾也帶回 ok-mind（log 的 mind 部分不是 skip／fail） |
| forgot | 問她回滾掉的事實（生日），她提到就記 WARN（AI 回答，不算失敗） |

遊戲端為此新增控制埠 `GET /galgame`（呈現狀態：active、partner、lines、view_line、checkpoints、on_screen、portrait_captured、ok-story 的 state）與 `/state` 的 `all_ready`（每個角色都載好，不依顯示語言）。
