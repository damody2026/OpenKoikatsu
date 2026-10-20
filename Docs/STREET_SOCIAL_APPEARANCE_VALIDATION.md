# StreetSocial common appearance correction — 2026-10-09

The street renderer now converts authored Unity Color properties to linear RGB
before passing them to Unreal, including Color properties without a Gamma flag.
Alpha and explicitly supplied scene uniforms remain unchanged. Common lighting
defaults match the CardReader native material helper. Texture preparation and
upload share a 1024-pixel maximum (previously 256), with complete mip chains.
Exact shader texture streams take precedence over legacy preview streams.

Rigid front hair and accessories use their neutral world-baked vertices as the
reference, then apply the attachment delta once. Previously the initial animated
attachment delta was inverted out of vertices which were already neutral. This
introduced a persistent offset relative to the animated head. No head-scale or
card-specific position adjustment was added.

The source front-cull outline pass is now represented by common precompiled
materials for ten shader families (22 recipes). The outline shell shares the
character's vertices, UVs, skin weights, bone map and animation; its expansion,
coverage masks, color and skin width use source shader properties. Main and
outline passes share the reveal lifecycle. Outline shells are excluded from
the source shadow geometry. The main shader's global line width also receives
the authored line width.

Procedural speech follows the actual instance transform rather than the
instanced component's origin. PlayWhenSilent keeps speech progression alive
when the listener is far away. This fixes the reproducible private-scene
greeting timeout without replacing completion with a guessed timer.

## Evidence

- C019 original versus portable diagnostic:
  `Saved/Tests/StreetAppearance01/native.json`. Original and portable hair gloss,
  detail image sizes and UV channel counts agree. Reapplying card shape does not
  resolve the attachment offset; preserving unreduced source geometry does not
  resolve it either.
- Before: `Saved/Crowd/StreetAppearance01Source/visual.side.gpu.png`.
- After: `Saved/Crowd/StreetOutline12C019/visual.side.gpu.png`.
- New executable: `Saved/StreetSocial/Build/StreetOutline12.stage/Windows/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe`.
- Three source-geometry frozen views per card:
  `Saved/Crowd/StreetOutline12C019/visual.comparison.json`,
  `Saved/Crowd/StreetOutline12C000Source/visual.comparison.json`,
  `Saved/Crowd/StreetOutline12C044Source/visual.comparison.json`.
  CPU and GPU images agree byte for byte; palette checks have zero mismatches.
  This verifies the two runtime paths, **not equality with the original game**.
- Additional C019 head close-up:
  `Saved/Crowd/StreetOutline12Head/visual.side.gpu.png`.
- Normal reduced-geometry C000/C044 captures also passed under
  `Saved/Crowd/StreetOutline12C000` and `Saved/Crowd/StreetOutline12C044`.
- Shared materials: `Saved/Crowd/RuntimeSharedMaterial-StreetOutline10.json`:
  83 main/outline parents and six default textures, no per-card input or runtime
  shader compilation. Extracted original programs and reflection are retained in
  `Saved/Tests/RuntimeOutlines01/Source/sources.json`.
- Presentation: `Saved/StreetSocial/Runtime-StreetOutline12Presentation/social.validation.json`:
  five standing actors, six running actors, 15 relationship controls and live sync.
- Dialogue: `Saved/StreetSocial/Runtime-StreetOutline12Context/social.validation.json`:
  20 original dialogue context cases, five accepted confessions and five refusals.
- Full regression: `Saved/StreetSocial/Runtime-StreetOutline12Full/social.validation.json`.
  Passed with five female voices, 15 completed dialogue lines, all 15 private
  girl/relationship cases, paired activities, cancellation/restore, expressions,
  persistence and independent source verification. No VSM marking overflow.
  Prior full runs are not relabeled as this executable's evidence.
- Invitations: `Saved/StreetSocial/Runtime-StreetOutline12Invitations/social.validation.json`:
  20 source acceptance/refusal/ledger/cancel cases passed.
- Python regression: 57 StreetSocial tests and 11 shader translator tests passed.
- Current executable SHA-256:
  `890766d43a48e4915ad07d9ad5a1da0bad70e0198b331e487c5a0365ca68c6bc`.

The washed-out tint and attachment offset are corrected. The resulting runtime
image has different camera, lighting and post-processing conditions from the
supplied original-game image; pixel-perfect appearance parity has not been
established. Original cards and existing relationship saves
were not replaced. Higher-resolution textures increase texture memory use.

`Tools/publish_street_social_appearance.py` requires current executable evidence,
the same common material root in all functional tests, and three distinct source
cards. It retains a recoverable previous receipt and explicitly identifies any
optional older regression evidence. The launcher validates executable, material
manifest and patch hashes before opening the game.

## 最終功能驗收

StreetOutline12 已發布至 `Saved/StreetSocial/Build/receipt.json`，
`full_functional_acceptance=true`；啟動器的 `-ValidateOnly` 核對通過。
此次所有功能證據均來自上述同一執行檔，無沿用舊版自主活動或邀約結果。

| 功能 | 結果 |
| --- | --- |
| 個性站立、話題收合、半透明右側選項與下方對話、六個即時數值 | 通過 presentation |
| 陌生／朋友／戀人互斥勾選、保存及外部變動同步 | 15 組通過 |
| 好感 100 告白、原作前後文、成功與拒絕台詞／語音 | 20 組前後文、5 接受、5 拒絕通過 |
| 任意女角、三種關係進入一對一場景 | 15 組通過 |
| 男主跑步、較慢的女角短跑、停止與站立恢復 | 6 名跑步及 movement 通過 |
| 對話、表情、共同活動、手機、觸碰、取消與保存 | 完整回歸及獨立核對通過 |
| 邀約接受／拒絕／取消／每日紀錄 | 20 組通過 |
| 自主手機、喝飲料、自拍、整理儀容 | 每項 5 名女角及獨立來源核對通過 |
| C019 掛點、共用來源描邊與材質 | 3 張來源卡三視角、C019 近拍通過 |

原始 C019 卡與 APNG 雜湊保持不變；測試使用獨立關係檔。
本輪不以 120 FPS 或原作逐像素一致作為已通過項目。

## CardReader 高模往返後的回歸

StreetHigh05 已取代 StreetOutline12 成為啟動器目前版本。外觀共用材質與補丁不變，
C019、C000、C044 的來源幾何三視角以 StreetHigh05 重新驗證通過。
五名女角三種關係實際進入 CardReader 高模互動場景並返回原街區，共十五組通過；
完整功能回歸與獨立來源核對通過，沒有保留舊版功能證據代替新版驗收。
完整證據與計時修正記於 `Docs/STREET_SOCIAL_HIGH_INTERACTION_VALIDATION.md`。


## StreetBodyClick07 外觀回歸

C019、C000、C044 的來源幾何三視角，以 StreetBodyClick07 當前執行檔及 StreetOutline10 共用材質測試通過；證據分別位於 `Saved/Crowd/StreetBodyClick07C019Source`、`StreetBodyClick07C000Source`、`StreetBodyClick07C044Source`。C019 APNG 與 KK_384914.png 雜湊未變動。短反應共用來源曲線、各自骨架，不新增卡片專屬幾何。完整功能重測及發布檢查通過。
