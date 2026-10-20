// PregnancyPlus 7.8 BellyTemplate.cs preset values.
#pragma once
struct FOKPregnancyPreset { const TCHAR* Name; const TCHAR* Label; const TCHAR* Json; };
inline const FOKPregnancyPreset OKPregnancyPresets[] = {
 {TEXT("None"), TEXT("無"), TEXT("{\"GameplayEnabled\":true,\"inflationSize\":40}")},
 {TEXT("Small"), TEXT("小型"), TEXT("{\"GameplayEnabled\":true,\"inflationSize\":40,\"inflationMultiplier\":-0.1,\"inflationStretchY\":-0.05,\"inflationRoundness\":0.005,\"inflationStretchX\":-0.05,\"inflationDrop\":0.02}")},
 {TEXT("Standard"), TEXT("標準"), TEXT("{\"GameplayEnabled\":true,\"inflationSize\":40,\"inflationMultiplier\":0.25,\"inflationStretchX\":-0.12,\"inflationStretchY\":-0.07,\"inflationTaperY\":-0.005,\"inflationTaperZ\":-0.003,\"inflationRoundness\":0.005,\"inflationDrop\":0.05}")},
 {TEXT("Standard Flat"), TEXT("標準扁平"), TEXT("{\"GameplayEnabled\":true,\"inflationSize\":40,\"inflationStretchX\":-0.02,\"inflationStretchY\":-0.018,\"inflationShiftZ\":-0.005,\"inflationMultiplier\":0.2,\"inflationRoundness\":0.007,\"inflationTaperY\":-0.01,\"inflationTaperZ\":-0.005,\"inflationDrop\":0.05}")},
 {TEXT("Main Game"), TEXT("原始遊戲"), TEXT("{\"GameplayEnabled\":true,\"inflationSize\":40,\"inflationMultiplier\":0.4,\"inflationStretchX\":-0.2,\"inflationStretchY\":-0.1,\"inflationTaperZ\":-0.005,\"inflationTaperY\":-0.02,\"inflationDrop\":0.05}")},
 {TEXT("Torpedo"), TEXT("向前突出"), TEXT("{\"GameplayEnabled\":true,\"inflationSize\":40,\"inflationRoundness\":0.05,\"inflationShiftZ\":0.02,\"inflationMoveZ\":0.01,\"inflationMultiplier\":0.4,\"inflationStretchX\":-0.35,\"inflationStretchY\":-0.35,\"inflationTaperY\":-0.01,\"inflationTaperZ\":-0.005,\"inflationDrop\":0.07}")},
 {TEXT("Multiples"), TEXT("多胞胎"), TEXT("{\"GameplayEnabled\":true,\"inflationSize\":40,\"inflationMultiplier\":0.48,\"inflationRoundness\":0.1,\"inflationStretchX\":-0.22,\"inflationStretchY\":-0.2,\"inflationDrop\":0.07,\"inflationShiftZ\":0.007,\"inflationTaperY\":-0.01,\"inflationTaperZ\":-0.002}")},
 {TEXT("Hyper"), TEXT("超大型"), TEXT("{\"GameplayEnabled\":true,\"inflationSize\":40,\"inflationMultiplier\":0.7,\"inflationRoundness\":0.14,\"inflationStretchX\":-0.25,\"inflationStretchY\":-0.25,\"inflationShiftZ\":0.015,\"inflationTaperZ\":-0.002,\"inflationDrop\":0.15}")},
 {TEXT("Chub Small"), TEXT("微胖小型"), TEXT("{\"GameplayEnabled\":true,\"inflationSize\":40,\"inflationMultiplier\":-0.17,\"inflationTaperZ\":-0.004,\"inflationFatFold\":0.9}")},
 {TEXT("Chub Standard"), TEXT("微胖標準"), TEXT("{\"GameplayEnabled\":true,\"inflationSize\":40,\"inflationRoundness\":0.008,\"inflationStretchX\":-0.1,\"inflationFatFold\":0.9,\"inflationFatFoldGap\":-0.02}")},
 {TEXT("Fat"), TEXT("肥胖"), TEXT("{\"GameplayEnabled\":true,\"inflationSize\":40,\"inflationMultiplier\":0.1,\"inflationRoundness\":0.018,\"inflationStretchX\":-0.1,\"inflationTaperZ\":0.007,\"inflationFatFold\":1.0,\"inflationFatFoldGap\":-0.03}")},
};
