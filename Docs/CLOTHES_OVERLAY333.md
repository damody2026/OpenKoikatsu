# Stage333：KCOX 衣服圖層與 Forge main_alpha

龍卷白色衣服缺少原生圖案，根因不是卡片選错衣服：fresh native `Tatsumaki333Load/Card0.json.clothes-load.tsv` 和 source 選到相同 `Nam Taisoufuku` entry、MainTex、ColorMask、top colors。既有 KCOX 解析只處理 BodyMask/BraMask/InnerMask，漏掉元件名稱所對應的衣服 albedo 圖層。

依據安裝反編譯 `KoiClothesOverlayController.ApplyOverlays`、`GetApplicableRenderers`、`ClothesTexData`：

- `Overlays` 是 coordinate → component name → `[TextureBytes, Override]`；不可跨 coordinate fallback。
- 主衣服九種、top subparts A/B/C 分別對應原生元件名。Override 先清空現有 RT；即使 PNG 為空，清空仍有意義。
- 僅選原生 renderer group 的第一個 renderer 與其第二組配對；同一 create RT 的其他 renderer 共享修改。不可將圖層套給所有 submesh、Normal03 或非 runtime MainTex。
- 保留 MaterialEditor 後續覆蓋順序；圖層合成使用既有 KSOX 同一原生 blend 算式。

`SESSION214 / CLI325 / CACHE105`：原始 extracted mesh 新增 Texture2/Texture3/liquidmask，必須同時更新 bundle cache 版本，單改 SESSION 版本會重讀舊 mesh、仍缺 liquid 貼圖。

`Shader Forge/main_alpha` 原本落到 kind0，現在分為 kind11，原始 installed EC DXBC 0017/0006 對 native metadata12/14。保留 MainTex、NormalMap、DetailMask、AlphaMask、AnotherRamp、liquid 三張貼圖、各自 ST / sampler / material uniforms。Forward 使用原生 clip 與 SrcAlpha/OneMinusSrcAlpha；不得沿用 MainItem 的 One/Zero 常數 opacity mask。

驗證紀錄：

- 兩片段程式各 44 GPU controls、704 pixels，原 DXBC 與 generated HLSL最大誤差0，錯 ShadowColor 負例拒絕。這只證明相同輸入的 fragment math，不代表 Unity 外部 screen shadow 和完整 outline parity。
- 龍卷1024² MainTex對fresh native：最大 RGBA byte error1、差異295 pixels；橘色領口／袖口／底邊與 NIKU 圖案已恢復。不得稱全像素 max0。
- 新 KCOX parser 單測：保留無PNG清空、支援 array/map序列化、coordinate隔離 PASS。
- 實際 UE kind11 龍卷四視角已驗證；完整回歸另行稽核，尚未發布正式 Screenshots。

CACHE105補三張liquid貼圖各自filter/wrap/sRGB/mip provenance。原生normal RT對source packed-normal PNG最大byte差5、mean0.2231，來自現存CPU decode/normal-convert鏈，尚不作normal texture max0聲明。

UE build ForgeAlpha333UEBuild成功，Tatsumaki333Fixed四視角exit0，PV1/1、附件9筆誤差0、材質/GPU/view/shadow readiness完整PASS。目視橘色領口、袖口、底邊、NIKU與cloth folds/specular恢復；未加入角色調色。

最終 `ForgeAlpha333All37` owned PID81616 已正常exit0，37卡148張900×1080圖；PV37/37、附件869筆最大誤差0、material/GPU/view/shadow readiness全部PASS。Contact01–10全部目視，包含三男角格紋/配件、守岸人藍紫星點、Siesta背髮、Android18眉毛/眼眶線、KK226176/KK388205/江之岛盾子2，未發現此次修正的新可見退化。證據：`Saved/Logs/ForgeAlpha333All37Audit.json`、`ForgeAlpha333All37VisualReview.json`。

37卡 source prewarm 全部exit0，但七海千秋3仍有兩筆 Sideloader PNG 花紋 CPU fallback，因此 `ForgeAlpha333PrewarmAudit.json.full_clothes_gpu_passed=false`。完整全GPU衣服門檻未通過，不能把畫面/匯入PASS當全GPU通解已完成；正式 Screenshots 保留原圖，本輪新版可在 `Saved/DebugShots/ForgeAlpha333All37/Tpose/` 查看。owned UE/預熱CLI均已結束。

Authored Texture2D 的完整mip payload尚未由PNG API傳輸；例如Texture3來源10mips，但UE只載base mip，不能以metadata存在當完整sampler parity。現有runtime NormalMap完整自動mips11。fractional-alpha ZWrite與完整原生outline亦未作全部parity聲明。

Stage334後續：上述七海 CPU fallback 已確認是漏 Sideloader StripAll而誤選PNG，不是原生PNG缺GPU解碼。通用migration修正後 SESSION215／CLI326／CACHE105的37卡來源全部GPU、fallback0；Resolve334All37Final統一open／凍結四視角148圖與全部數值／目視門檻PASS，已更新正式Saved/Screenshots並先備份136舊圖。詳Docs/SIDELOADER_MIGRATION334.md及Resolve334All37FinalPublish.json。本節Stage333未發布敘述保留為歷史；normal max5、完整mips／透明深度等界線未因發布而消失。
