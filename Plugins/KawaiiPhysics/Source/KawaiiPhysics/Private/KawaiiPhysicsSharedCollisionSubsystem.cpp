// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "AnimNode_KawaiiPhysics.h"

#include "GameFramework/Actor.h"

// SharedCollision CVars（AnimNode_KawaiiPhysics.cpp で定義）
extern TAutoConsoleVariable<int32> CVarSharedCollisionReadMaxAge;
extern TAutoConsoleVariable<int32> CVarSharedCollisionCleanupMaxAge;
extern TAutoConsoleVariable<float> CVarSharedCollisionCleanupInterval;

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_Publish"), STAT_KawaiiPhysics_SharedCollision_Publish, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_GetOrCreateSlot"), STAT_KawaiiPhysics_SharedCollision_GetOrCreateSlot, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_ReadMerged"), STAT_KawaiiPhysics_SharedCollision_ReadMerged, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_FindOrCreateEntry"), STAT_KawaiiPhysics_SharedCollision_FindOrCreateEntry, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_FindEntry"), STAT_KawaiiPhysics_SharedCollision_FindEntry, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SharedCollision_Tick"), STAT_KawaiiPhysics_SharedCollision_Tick, STATGROUP_Anim);
DECLARE_DWORD_COUNTER_STAT(TEXT("KawaiiPhysics_SharedCollision_NumEntries"), STAT_KawaiiPhysics_SharedCollision_NumEntries, STATGROUP_Anim);
DECLARE_DWORD_COUNTER_STAT(TEXT("KawaiiPhysics_SharedCollision_NumSlots"), STAT_KawaiiPhysics_SharedCollision_NumSlots, STATGROUP_Anim);

AActor* UKawaiiPhysicsSharedCollisionSubsystem::GetFamilyRoot(AActor* Actor)
{
	// アタッチポインタを辿るだけのread-only処理（UObject変更なし）。任意スレッドから呼べる
	AActor* Root = Actor;
	while (Root)
	{
		AActor* Parent = Root->GetAttachParentActor();
		if (!Parent)
		{
			Parent = Root->GetParentActor();
		}

		if (!Parent || Parent == Root)
		{
			break;
		}

		Root = Parent;
	}
	return Root;
}

// -------------------------------------------------------------------
// FKawaiiPhysicsSharedCollisionSourceSlot
// -------------------------------------------------------------------

void FKawaiiPhysicsSharedCollisionSourceSlot::Publish(FKawaiiPhysicsSharedCollisionData& InOutData)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_Publish);

	FWriteScopeLock WriteLock(BufferLock);
	// Swapで旧BufferをInOutDataへ返し、呼び出し側が確保済みメモリを再利用できるようにする（ロック区間はSwapのみで最小）
	Swap(Buffer, InOutData);

	// フレーム番号を記録（鮮度チェック用）
	LastPublishFrame.store(GFrameCounter, std::memory_order_release);
}

bool FKawaiiPhysicsSharedCollisionSourceSlot::IsExpired(uint64 CurrentFrame, uint64 MaxAge) const
{
	const uint64 LastFrame = LastPublishFrame.load(std::memory_order_acquire);
	return (LastFrame == 0) || (CurrentFrame - LastFrame > MaxAge);
}

void FKawaiiPhysicsSharedCollisionSourceSlot::MarkExpired()
{
	LastPublishFrame.store(0, std::memory_order_release);
}

void FKawaiiPhysicsSharedCollisionSourceSlot::AppendTo(FKawaiiPhysicsSharedCollisionData& OutData) const
{
	FReadScopeLock ReadLock(BufferLock);
	OutData.SphericalLimits.Append(Buffer.SphericalLimits);
	OutData.CapsuleLimits.Append(Buffer.CapsuleLimits);
	OutData.BoxLimits.Append(Buffer.BoxLimits);
	OutData.PlanarLimits.Append(Buffer.PlanarLimits);
}

// -------------------------------------------------------------------
// FKawaiiPhysicsSharedCollisionEntry
// -------------------------------------------------------------------

TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot> FKawaiiPhysicsSharedCollisionEntry::GetOrCreateSlot(uint64 SourceID)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_GetOrCreateSlot);

	// 既存Slotの検索は読み取りロック
	{
		FReadScopeLock ReadLock(SlotsLock);
		if (TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>* Existing = Slots.Find(SourceID))
		{
			return *Existing;
		}
	}

	// 構造変更は書き込みロック。ロック取得待ちの間に他スレッドが作成済みの可能性があるため再確認
	FWriteScopeLock WriteLock(SlotsLock);
	if (TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>* Existing = Slots.Find(SourceID))
	{
		return *Existing;
	}
	TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot> NewSlot = MakeShared<FKawaiiPhysicsSharedCollisionSourceSlot>();
	Slots.Add(SourceID, NewSlot);
	return NewSlot;
}

void FKawaiiPhysicsSharedCollisionEntry::ReadMerged(FKawaiiPhysicsSharedCollisionData& OutData) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_ReadMerged);
	OutData.Reset();

	const uint64 CurrentFrame = GFrameCounter;

	FReadScopeLock ReadLock(SlotsLock);
	for (const auto& Pair : Slots)
	{
		// 期限切れスロットをスキップ（Publishが停止したSourceのデータを除外）
		if (Pair.Value->IsExpired(CurrentFrame, CVarSharedCollisionReadMaxAge.GetValueOnAnyThread()))
		{
			continue;
		}

		Pair.Value->AppendTo(OutData);
	}
}

void FKawaiiPhysicsSharedCollisionEntry::RemoveExpiredSlots(uint64 CurrentFrame, uint64 MaxAge)
{
	FWriteScopeLock WriteLock(SlotsLock);
	for (auto SlotIt = Slots.CreateIterator(); SlotIt; ++SlotIt)
	{
		if (SlotIt->Value->IsExpired(CurrentFrame, MaxAge))
		{
			SlotIt.RemoveCurrent();
		}
	}
}

int32 FKawaiiPhysicsSharedCollisionEntry::GetSlotCount() const
{
	FReadScopeLock ReadLock(SlotsLock);
	return Slots.Num();
}

bool FKawaiiPhysicsSharedCollisionEntry::IsEmpty() const
{
	FReadScopeLock ReadLock(SlotsLock);
	return Slots.IsEmpty();
}

// -------------------------------------------------------------------
// UKawaiiPhysicsSharedCollisionSubsystem
// -------------------------------------------------------------------

bool UKawaiiPhysicsSharedCollisionSubsystem::TryResolveRegistryKey(
	AActor* Actor, const FGameplayTag& Tag, FRegistryKey& OutKey)
{
	if (!Actor || !Tag.IsValid())
	{
		return false;
	}

	// アタッチ階層を毎回辿り直すことで、ランタイムのアタッチ変更にも追従する（read-only）
	AActor* FamilyRoot = GetFamilyRoot(Actor);
	if (!FamilyRoot)
	{
		return false;
	}

	OutKey = FRegistryKey(FamilyRoot, Tag);
	return true;
}

TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> UKawaiiPhysicsSharedCollisionSubsystem::FindEntryByKey(
	const FRegistryKey& Key) const
{
	FReadScopeLock ReadLock(RegistryLock);
	if (const TSharedPtr<FKawaiiPhysicsSharedCollisionEntry>* Found = Registry.Find(Key))
	{
		// Actorが無効ならスキップ（Tick()で定期的にクリーンアップ）
		if (Key.Key.IsValid())
		{
			return *Found;
		}
	}
	return nullptr;
}

TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> UKawaiiPhysicsSharedCollisionSubsystem::FindOrCreateEntry(
	AActor* Actor, const FGameplayTag& Tag)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_FindOrCreateEntry);

	FRegistryKey Key;
	if (!TryResolveRegistryKey(Actor, Tag, Key))
	{
		return nullptr;
	}

	// 既存Entryの検索（読み取りロック）
	if (TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> Existing = FindEntryByKey(Key))
	{
		return Existing;
	}

	// 構造変更は書き込みロック。ロック取得待ちの間に他スレッドが作成済みの可能性があるため再確認
	FWriteScopeLock WriteLock(RegistryLock);
	if (TSharedPtr<FKawaiiPhysicsSharedCollisionEntry>* Existing = Registry.Find(Key))
	{
		return *Existing;
	}
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> NewEntry = MakeShared<FKawaiiPhysicsSharedCollisionEntry>();
	Registry.Add(Key, NewEntry);
	return NewEntry;
}

TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> UKawaiiPhysicsSharedCollisionSubsystem::FindEntry(
	AActor* Actor, const FGameplayTag& Tag) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_FindEntry);

	FRegistryKey Key;
	if (!TryResolveRegistryKey(Actor, Tag, Key))
	{
		return nullptr;
	}
	return FindEntryByKey(Key);
}

void UKawaiiPhysicsSharedCollisionSubsystem::Deinitialize()
{
	{
		FWriteScopeLock WriteLock(RegistryLock);
		Registry.Empty();
	}
	Super::Deinitialize();
}

void UKawaiiPhysicsSharedCollisionSubsystem::Tick(float DeltaTime)
{
	CleanupAccumulator += DeltaTime;
	if (CleanupAccumulator < CVarSharedCollisionCleanupInterval.GetValueOnGameThread())
	{
		return;
	}
	CleanupAccumulator = 0.0f;
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SharedCollision_Tick);

	const uint64 CurrentFrame = GFrameCounter;

	// Registryの構造変更とWorkerスレッドのFind/FindOrCreateの競合を防ぐため書き込みロックで保護。
	// ロック順序は Registry → Slots（Entryメソッドが内部でSlotsLockを取る）。
	FWriteScopeLock WriteLock(RegistryLock);

	for (auto It = Registry.CreateIterator(); It; ++It)
	{
		// Actorが無効 → エントリ除去
		if (!It->Key.Key.IsValid())
		{
			It.RemoveCurrent();
			continue;
		}

		// 期限切れスロットを除去
		FKawaiiPhysicsSharedCollisionEntry& Entry = *It->Value;
		Entry.RemoveExpiredSlots(CurrentFrame, CVarSharedCollisionCleanupMaxAge.GetValueOnGameThread());

		// スロットが空になったエントリも除去
		if (Entry.IsEmpty())
		{
			It.RemoveCurrent();
		}
	}

	// 整数カウンタ更新
	int32 TotalSlots = 0;
	for (const auto& Pair : Registry)
	{
		TotalSlots += Pair.Value->GetSlotCount();
	}
	SET_DWORD_STAT(STAT_KawaiiPhysics_SharedCollision_NumEntries, Registry.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_SharedCollision_NumSlots, TotalSlots);
}

bool UKawaiiPhysicsSharedCollisionSubsystem::IsTickable() const
{
	// FindOrCreateEntryがWorkerスレッドからRegistryを変更しうるため、空判定も読み取りロックで保護する
	FReadScopeLock ReadLock(RegistryLock);
	return !Registry.IsEmpty();
}

TStatId UKawaiiPhysicsSharedCollisionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UKawaiiPhysicsSharedCollisionSubsystem, STATGROUP_Tickables);
}
