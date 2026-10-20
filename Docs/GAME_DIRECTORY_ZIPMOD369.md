# Stage369：遊戲目錄內卡片自動使用遊戲 zipmod

Unreal 專用匯入介面不用另外指定遊戲路徑。原始卡位於遊戲安裝目錄內時，沿正規化後的卡片父目錄向上，尋找同時有 `abdata`、`UserData` 與 `KoikatsuSunshine.exe` 或 `Koikatu.exe` 的最近祖先；找到後遞迴讀取其 `mods/**/*.zipmod`。卡片同層 `.zipmod` 仍會納入。外部卡片不搜尋全機遊戲，也不因相似路徑字首或僅有資料夾就誤認安裝。

模型基礎資源仍用專案內 native_v1；不啟動遊戲，不讀遊戲 abdata 作替代，也不載入外部 plugin/config。獨立 PNG/APNG 仍走原有資源包。只自動加入識別到的遊戲 `mods`，不支援 Sideloader 設定中的外部 AdditionalModsDirectory 或普通 `.zip`。

## 模組處理

遊戲 mod 遞迴搜尋與多版本選擇依反編譯 `KKS_BepisPlugins/KKS_Sideloader/Sideloader/Sideloader.cs` / `ManifestVersionComparer.cs`：同 GUID 全部有版本號時，以 token 比較版本降序，再以完整檔名字串長度降序；任一版本號缺少時用修改時間降序。數字 token 會按數值比較，`v/r` 前綴與分隔符處理一致。相同優先級採正規化路徑的穩定搜尋順序，沒有原遊戲 Directory.GetFiles 的相同排序保證。

遊戲來源的損壞／不支援 manifest 記錄為 `Skipped invalid game zipmod` 並略過，避免無關的損壞模組阻止所有卡。卡片相鄰損壞 zipmod 仍拒絕；所需依賴不足仍由原有 strict native guard 拒絕，不替換部件。舊缺件訊息的 `missing adjacent zipmod GUIDs` 字樣維持 API 相容，現在指整個匯入來源缺件。不同內容的相鄰／選中遊戲模組同 GUID 仍明確衝突，沒有靜默相鄰覆蓋。

來源視圖使用帶序號的獨立 archive 名稱，防止不同 mods 子目錄同名檔互相覆寫。模組按 manifest GUID 解析，原始 zipmod 不修改。完整選中模組放進私有來源，以涵蓋 body descriptor 等非普通 UAR 的依賴。

## 驗證與錯誤

`Saved/Tests/AssetImport369` 使用新目的地與 log，歷史 Audit 不重封。

- BuildR1/R2 成功，但 RawGameR1/R2 都是 actual0 / AutomationFail，不能作通過。R1 被無關損壞 manifest 阻止；R2 略過損壞後，被遊戲已安裝的多版本 GUID `com3d2.hair` 阻止。根因分別是誤套相鄰嚴格拒絕規則與缺少 Sideloader 選版邏輯，已通用修正。
- BuildR3 actual6：猜錯 LexFromString header 的位置，實際 UE5.8 是 `String/LexFromString.h`。BuildR4 完整 Editor Succeeded / actual0、5.18 秒。
- SourcesR1 actual0 / LocalZipmods AutomationSuccess：涵蓋原 CRC/BOM/同 GUID 去重與衝突，以及遊戲祖先辨識、正規化路徑、遞迴 mod、同名 archive、外部字首隔離、非安裝目錄排除、無關壞 mod 略過、版本1.10優於1.9與無版本修改時間選擇。
- 調查曾猜錯 KKSideloader 模組位置；之後改用 rg --files 定位。一次 PowerShell 的 Python inline SQL 引號轉義錯誤 SyntaxError，未執行查詢；改單引號 here-string stdin，只讀既有 sqlite。

最終 RawGameR3／RawExternalR1 均 actual0 / AutomationSuccess：遊戲 UserData 內的原始 KK_398530 自動使用7283個選中 zipmod；外部副本 detected_game=none，只使用33個相鄰 zipmod。兩者皆313assets。VerifyGameR3／VerifyExternalR1 新程序實際 RHI actual0：57 Static／11 Skin／158材質／85貼圖、Static195914／Skin19914三角形、18mipchains259levels、300morph91970deltas／tiny0，UV與頂點色保留。最終 SHA 證據見 Audit369；原始卡與副本 bytes相同、舊資產與Audit保留。

遊戲來源發現5個不支援／損壞 manifest，均保留診斷；不能宣稱它們可載入。沒有全部卡片、全部第三方模組、GPU最終像素、controller 或完整應用 Shipping 的零 bug 驗收。
