
## Card 079
Quality conversion failed; inspect C:\portable\OpenKoikatsu\Saved\Crowd\GeometryBatchR1_75_C079.log

```text
LogWindows: Failed to load 'aqProf.dll' (GetLastError=126)
LogWindows: Failed to load 'VtuneApi.dll' (GetLastError=126)
LogWindows: Failed to load 'VtuneApi32e.dll' (GetLastError=126)
LogWindows: Failed to load 'WinPixGpuCapturer.dll' (GetLastError=126)
PixWinPlugin: PIX capture plugin failed to initialize! Check that the process is launched from PIX.
[2026.10.03-14.48.01:022][  0]LogTargetPlatformManager: Display: Failed to SetupSDK for platform 'Android'
[2026.10.03-14.48.01:045][  0]LogTargetPlatformManager: Failed to load module 'IOSTargetPlatform' for platform IOS (Reason=FileNotFound).
[2026.10.03-14.48.01:070][  0]LogTargetPlatformManager: Display: Failed to SetupSDK for platform 'Linux'
[2026.10.03-14.48.01:095][  0]LogTargetPlatformManager: Display: Failed to SetupSDK for platform 'LinuxArm64'
[2026.10.03-14.48.01:117][  0]LogTargetPlatformManager: Failed to load module 'MacTargetPlatform' for platform Mac (Reason=FileNotFound).
[2026.10.03-14.48.01:118][  0]LogTargetPlatformManager: Failed to load module 'TVOSTargetPlatform' for platform TVOS (Reason=FileNotFound).
[2026.10.03-14.48.37:075][  0]LogWindows: Failed to get driver data for device '' using Setup API. Switching to fallback method.
```
Continue other independent cards; this card is excluded from acceptance until repaired.


### Card 079 修正與重測
不是骨骼權重錯誤。LOD 2 回復受損材質群後增加到 24526 面，高於 LOD 1 的 24414 面，原有單調性檢查因而退出且未印原因。改為在該級回用上一級已重建的完整網格，保留完整綁定與覆蓋，不讓更遠 LOD 變得更昂貴。GameLowMonotoneC079R1：45626/24414/24414 主網格面數，3 LOD × 7 時間點 deformation audit good=1，commandlet exit 0。原失敗 batch receipt 保留，重試 log/exit 是修復證據。
