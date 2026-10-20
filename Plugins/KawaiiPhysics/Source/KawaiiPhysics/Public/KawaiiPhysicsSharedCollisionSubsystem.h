// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"

#include <atomic>

#include "KawaiiPhysicsSharedCollisionTypes.h"

#include "KawaiiPhysicsSharedCollisionSubsystem.generated.h"

/**
 * Source1つ分の共有コリジョンスロット
 * Shared collision slot for a single source
 *
 * BufferはBufferLockで保護する。 / All access to Buffer must hold BufferLock.
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedCollisionSourceSlot
{
	/**
	 * ワーカースレッドから呼び出し可能 / Can be called from any thread.
	 * InOutDataとBufferをSwapする。呼び出し側は受け取った旧Buffer(=InOutData)を次フレームの一時バッファとして再利用でき、
	 * 書き込みロック区間内のディープコピーと毎フレームのメモリ確保を避けられる。
	 * Swaps InOutData with Buffer. The caller can reuse the returned old buffer as next frame's scratch,
	 * avoiding a deep copy inside the write-lock critical section and per-frame allocation.
	 */
	void Publish(FKawaiiPhysicsSharedCollisionData& InOutData);

	/** ワーカースレッドから呼び出し可能 / Can be called from any thread */
	void AppendTo(FKawaiiPhysicsSharedCollisionData& OutData) const;

	/** スロットが古くなっているか判定 / Check if this slot has not been published to recently */
	bool IsExpired(uint64 CurrentFrame, uint64 MaxAge) const;

	/** スロットを即座に期限切れ化 / Mark this slot as immediately expired */
	void MarkExpired();

private:
	FKawaiiPhysicsSharedCollisionData Buffer;

	/** 最終Publishフレーム番号（鮮度チェック用） / Last published frame number for expiration detection */
	std::atomic<uint64> LastPublishFrame{0};

	/** バッファ内容の読み書きを保護。 / Protects buffer contents. */
	mutable FRWLock BufferLock;
};

/**
 * (ActorFamilyRoot, Tag) 単位のエントリ。複数Sourceのスロットを保持
 * Entry per (ActorFamilyRoot, Tag) pair. Holds slots for multiple sources.
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedCollisionEntry
{
	/**
	 * Source用: 自分専用スロットを取得/作成（SlotsLockでスレッドセーフ。任意スレッドから呼べる）
	 * For sources: Get or create a dedicated slot (thread-safe via SlotsLock; callable from any thread)
	 */
	TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot> GetOrCreateSlot(uint64 SourceID);

	/**
	 * Target用: 全スロットのコリジョンをマージして読み取り
	 * For targets: Read merged collision data from all source slots
	 */
	void ReadMerged(FKawaiiPhysicsSharedCollisionData& OutData) const;

	/**
	 * 期限切れスロットを除去（書き込みロック内で実行）
	 * Remove expired slots under write lock
	 */
	void RemoveExpiredSlots(uint64 CurrentFrame, uint64 MaxAge);

	/** スロット数を取得（読み取りロック内） / Get slot count under read lock */
	int32 GetSlotCount() const;

	/** スロットが空か判定（読み取りロック内） / Check if empty under read lock */
	bool IsEmpty() const;

private:
	/** SourceID（AnimNodeアドレス等）→ 専用スロット / Source ID -> dedicated slot */
	TMap<uint64, TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>> Slots;

	/** TMap構造変更とイテレーションの競合を防ぐロック / Lock to protect TMap structural changes vs iteration */
	mutable FRWLock SlotsLock;
};

/**
 * KawaiiPhysics AnimNode間でコリジョンデータを共有するためのWorldSubsystem
 * WorldSubsystem for sharing collision data between KawaiiPhysics AnimNodes in an attached actor family
 */
UCLASS()
class KAWAIIPHYSICS_API UKawaiiPhysicsSharedCollisionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Actorのアタッチ階層を遡ってファミリーrootを求める。アタッチポインタを辿るだけのread-only処理で、
	 * UObjectの変更やGCに触れないため任意スレッドから呼べる（並列eval中はアタッチが不変である前提）。
	 * Resolve the actor-family root by walking the attach hierarchy. Read-only pointer chase (no UObject mutation/GC),
	 * callable from any thread (assumes attachment is stable during parallel evaluation).
	 */
	static AActor* GetFamilyRoot(AActor* Actor);

	/**
	 * Source用: Actorのファミリーrootのエントリを検索、なければ作成（RegistryLockでスレッドセーフ。任意スレッドから呼べる）
	 * For sources: Find or create an entry for the actor family root. Thread-safe via RegistryLock; callable from any thread.
	 */
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> FindOrCreateEntry(AActor* Actor, const FGameplayTag& Tag);

	/**
	 * Target用: Actorのファミリーrootのエントリを検索（RegistryLockでスレッドセーフ。任意スレッドから呼べる）
	 * For targets: Find an entry for the actor family root. Thread-safe via RegistryLock; callable from any thread.
	 */
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> FindEntry(AActor* Actor, const FGameplayTag& Tag) const;

	// USubsystem interface
	virtual void Deinitialize() override;

	// FTickableGameObject interface (via UTickableWorldSubsystem)
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	// RegistryはWorkerスレッド(FindOrCreateEntry)からも変更されるため、空判定もRegistryLockで保護する（.cppで定義）
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return true; }

private:
	/** レジストリのキー型: (ActorFamilyRoot, Tag) / Registry key type */
	using FRegistryKey = TPair<TWeakObjectPtr<AActor>, FGameplayTag>;

	/**
	 * Actor/Tagからレジストリキーを構築する（GetFamilyRootでファミリーroot解決込み）。
	 * Actor/Tagが無効、またはファミリーrootが取れない場合は false。FindOrCreateEntry/FindEntryの共通前処理。
	 * Build the registry key from Actor/Tag (resolving the family root). Returns false if invalid. Shared by FindOrCreateEntry/FindEntry.
	 */
	static bool TryResolveRegistryKey(AActor* Actor, const FGameplayTag& Tag, FRegistryKey& OutKey);

	/**
	 * 構築済みキーで Entry を読み取りロック検索する（死んだActorのEntryはスキップ）。
	 * Read-locked lookup of an entry by its already-resolved key (skips entries whose family-root actor has died).
	 */
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> FindEntryByKey(const FRegistryKey& Key) const;

	/** レジストリ: (ActorFamilyRoot, Tag) → Entry / Registry: (ActorFamilyRoot, Tag) -> Entry */
	TMap<FRegistryKey, TSharedPtr<FKawaiiPhysicsSharedCollisionEntry>> Registry;

	/** Registryの構造変更とイテレーションの競合を防ぐロック（Worker初期化とGameThread Tickの両方が触る）
	 *  Lock protecting Registry structural changes vs iteration (touched by both worker-thread init and GameThread Tick).
	 *  ロック順序は Registry → Slots に統一する（Tickは本ロック保持中にEntryのSlotsLockを取る）。デッドロック回避のため逆順は禁止。
	 *  Lock order is always Registry -> Slots (Tick holds this while taking an Entry's SlotsLock); never the reverse. */
	mutable FRWLock RegistryLock;

	/** クリーンアップ間隔制御 / Cleanup interval control */
	float CleanupAccumulator = 0.0f;
};
