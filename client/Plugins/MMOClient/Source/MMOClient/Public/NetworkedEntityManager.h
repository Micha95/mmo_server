// NetworkedEntityManager.h
// (c) 2025 MMOClient
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/NoExportTypes.h"
#include "GameFramework/Actor.h"
#include "NetworkedEntityManager.generated.h"

UENUM(BlueprintType)
enum class ENetworkedEntityType : uint8
{
    Player,
    Mob,
    NPC,
    Item
};

USTRUCT(BlueprintType)
struct FNetworkedEntityInfo
{
    GENERATED_BODY()
    
    UPROPERTY(BlueprintReadOnly)
    int64 EntityId = 0;
    UPROPERTY(BlueprintReadOnly)
    ENetworkedEntityType EntityType = ENetworkedEntityType::Player;
    UPROPERTY(BlueprintReadOnly)
    int32 ShardId = 0;
    UPROPERTY(BlueprintReadOnly)
    FVector Location = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly)
    FRotator Rotation = FRotator::ZeroRotator;
    UPROPERTY(BlueprintReadOnly)
    TWeakObjectPtr<AActor> Actor;
};

UCLASS(Blueprintable)
class MMOCLIENT_API UNetworkedEntityManager : public UObject
{
    GENERATED_BODY()
public:
    UNetworkedEntityManager();
    // Singleton accessor
    static UNetworkedEntityManager* Get(UGameInstance* GameInstance);
    // Auto-register/deregister
    virtual void BeginDestroy() override;
    // Entity management
    UFUNCTION(BlueprintCallable)
    void SpawnEntity(int64 EntityId, ENetworkedEntityType Type, int32 ShardId, const FVector& Location, const FRotator& Rotation);
    UFUNCTION(BlueprintCallable)
    void DespawnEntity(int64 EntityId);
    UFUNCTION(BlueprintCallable)
    void MoveEntity(int64 EntityId, const FVector& NewLocation, const FRotator& NewRotation);
    UFUNCTION(BlueprintCallable)
    void SetEntityShard(int64 EntityId, int32 NewShardId);
    UFUNCTION(BlueprintCallable)
    void DeloadEntity(int64 EntityId);
    // Query
    UFUNCTION(BlueprintCallable)
    FNetworkedEntityInfo GetEntityInfo(int64 EntityId) const;
    UFUNCTION(BlueprintCallable)
    TArray<FNetworkedEntityInfo> GetEntitiesInShard(int32 ShardId) const;
    // Internal: Called by MMOClient on relevant packets
    void HandleEntityMovePacket(const struct S_Move& Packet);
    // Configurable GameInstance class (can be set in BP)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Network")
    TSoftClassPtr<UGameInstance> GameInstanceClass = TSoftClassPtr<UGameInstance>(FSoftObjectPath(TEXT("/Script/MMOClient.MMOGameInstance")));
    // Configurable actor classes for each entity type
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Network|Entities")
    TSoftClassPtr<AActor> PlayerActorClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Network|Entities")
    TSoftClassPtr<AActor> MobActorClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Network|Entities")
    TSoftClassPtr<AActor> NPCActorClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Network|Entities")
    TSoftClassPtr<AActor> ItemActorClass;

    // Type-specific MMOClient integration points
    void HandlePlayerSpawnPacket(const struct S_PlayerSpawn& Packet);
    void HandlePlayerDespawnPacket(const struct S_PlayerDespawn& Packet);
    void HandleMobSpawnPacket(const struct S_MobSpawn& Packet);
    void HandleMobDespawnPacket(const struct S_MobDespawn& Packet);
    void HandleNPCSpawnPacket(const struct S_NPCSpawn& Packet);
    void HandleNPCDespawnPacket(const struct S_NPCDespawn& Packet);
    void HandleItemSpawnPacket(const struct S_ItemSpawn& Packet);
    void HandleItemDespawnPacket(const struct S_ItemDespawn& Packet);
    // Type-specific spawn helpers (not BlueprintCallable)
    UFUNCTION(BlueprintCallable)
    void SpawnPlayerEntity(int64 EntityId, int32 ShardId, const FVector& Location, const FRotator& Rotation, const FString& Name, int32 ClassId, int32 Level);
    UFUNCTION(BlueprintCallable)
    void SpawnMobEntity(int64 EntityId, int32 ShardId, const FVector& Location, const FRotator& Rotation, int32 MobTypeId, int32 Level);
    UFUNCTION(BlueprintCallable)
    void SpawnNPCEntity(int64 EntityId, int32 ShardId, const FVector& Location, const FRotator& Rotation, int32 NPCTypeId);
    UFUNCTION(BlueprintCallable)
    void SpawnItemEntity(int64 EntityId, int32 ShardId, const FVector& Location, int32 ItemId, int32 Count);

    // Singleton instance
    static TWeakObjectPtr<UNetworkedEntityManager> Singleton;
    // Register/deregister with game instance
    void RegisterWithGameInstance(UGameInstance* GameInstance);
    void DeregisterFromGameInstance(UGameInstance* GameInstance);
private:
    // EntityId -> Info (all entities, for fast lookup)
    UPROPERTY()
    TMap<int64, FNetworkedEntityInfo> Entities;
    // ShardId -> Array of EntityIds (all entities)
    TMap<int32, TArray<int64>> ShardEntities;
    // Type-specific entity lists
    UPROPERTY()
    TMap<int64, FNetworkedEntityInfo> PlayerEntities;
    UPROPERTY()
    TMap<int64, FNetworkedEntityInfo> MobEntities;
    UPROPERTY()
    TMap<int64, FNetworkedEntityInfo> NPCEntities;
    UPROPERTY()
    TMap<int64, FNetworkedEntityInfo> ItemEntities;
    // Hold strong references to spawned actors to prevent GC
    UPROPERTY()
    TArray<AActor*> SpawnedActors;
    // Helper: Spawns the correct Actor type for the entity
    AActor* SpawnActorForEntity(UWorld* World, ENetworkedEntityType Type, const FVector& Location, const FRotator& Rotation);
};
