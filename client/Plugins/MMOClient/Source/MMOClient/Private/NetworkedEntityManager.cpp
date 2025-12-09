// NetworkedEntityManager.cpp
// (c) 2025 MMOClient
#include "NetworkedEntityManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "UObject/UObjectGlobals.h"
#include "MMOClient.h" // For MMOClient integration, if needed
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/SoftObjectPtr.h"
#include "Packets.h"

// --- AActor implementation for placable manager ---
UNetworkedEntityManager::UNetworkedEntityManager() {}

TWeakObjectPtr<UNetworkedEntityManager> UNetworkedEntityManager::Singleton;

UNetworkedEntityManager* UNetworkedEntityManager::Get(UGameInstance* GameInstance)
{
    if (!Singleton.IsValid()) {
        if (GameInstance) {
            UNetworkedEntityManager* NewMgr = NewObject<UNetworkedEntityManager>(GameInstance, GameInstance->GetClass()->GetDefaultObject<UMMOGameInstance>()->NetworkedEntityManagerClass);
            if (NewMgr) {
                NewMgr->RegisterWithGameInstance(GameInstance);
                Singleton = NewMgr;
            }
        }
    }
    return Singleton.Get();
}

void UNetworkedEntityManager::BeginDestroy()
{
    // Cleanup: Destroy all spawned actors and clear entity maps
    for (auto& Elem : Entities)
    {
        FNetworkedEntityInfo& Info = Elem.Value;
        if (Info.Actor.IsValid())
        {
            Info.Actor->Destroy();
        }
    }
    Entities.Empty();
    PlayerEntities.Empty();
    MobEntities.Empty();
    NPCEntities.Empty();
    ItemEntities.Empty();
    ShardEntities.Empty();
    SpawnedActors.Empty();
    DeregisterFromGameInstance(Cast<UGameInstance>(GetOuter()));
    Super::BeginDestroy();
}

void UNetworkedEntityManager::RegisterWithGameInstance(UGameInstance* GameInstance)
{
    if (!GameInstance) return;
    UMMOGameInstance* MMOInst = Cast<UMMOGameInstance>(GameInstance);
    if (MMOInst) {
        MMOInst->SetNetworkedEntityManager(this);
    }
}

void UNetworkedEntityManager::DeregisterFromGameInstance(UGameInstance* GameInstance)
{
    if (!GameInstance) return;
    UMMOGameInstance* MMOInst = Cast<UMMOGameInstance>(GameInstance);
    if (MMOInst) {
        MMOInst->SetNetworkedEntityManager(nullptr);
    }
}

AActor* UNetworkedEntityManager::SpawnActorForEntity(UWorld* World, ENetworkedEntityType Type, const FVector& Location, const FRotator& Rotation)
{
    UClass* ActorClass = nullptr;
    switch (Type) {
        case ENetworkedEntityType::Player:
            ActorClass = PlayerActorClass.IsValid() ? PlayerActorClass.Get() : AActor::StaticClass();
            break;
        case ENetworkedEntityType::Mob:
            ActorClass = MobActorClass.IsValid() ? MobActorClass.Get() : AActor::StaticClass();
            break;
        case ENetworkedEntityType::NPC:
            ActorClass = NPCActorClass.IsValid() ? NPCActorClass.Get() : AActor::StaticClass();
            break;
        case ENetworkedEntityType::Item:
            ActorClass = ItemActorClass.IsValid() ? ItemActorClass.Get() : AActor::StaticClass();
            break;
        default:
            ActorClass = AActor::StaticClass();
            break;
    }
    if (!ActorClass || !World) return nullptr;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return World->SpawnActor<AActor>(ActorClass, Location, Rotation, Params);
}

void UNetworkedEntityManager::SpawnEntity(int64 EntityId, ENetworkedEntityType Type, int32 ShardId, const FVector& Location, const FRotator& Rotation)
{
    if (Entities.Contains(EntityId)) return; // Already exists
    UWorld* World = nullptr;
    if (UGameInstance* GameInstance = Cast<UGameInstance>(GetOuter())) {
        World = GameInstance->GetWorld();
    }
    if (!World) {
        UE_LOG(LogTemp, Error, TEXT("[NetworkedEntityManager] Could not get valid world from GameInstance!"));
        return;
    }
    AActor* Actor = SpawnActorForEntity(World, Type, Location, Rotation);
    if (Actor) {
        UE_LOG(LogTemp, Warning, TEXT("[NetworkedEntityManager] Spawned actor: %s at %s in world: %s (IsValid: %d, IsPendingKillPending: %d)"),
            *Actor->GetName(), *Location.ToString(), *World->GetName(), Actor->IsValidLowLevel(), Actor->IsPendingKillPending());
        SpawnedActors.Add(Actor); // Hold strong reference to prevent GC
    } else {
        UE_LOG(LogTemp, Error, TEXT("[NetworkedEntityManager] Failed to spawn actor for entity %lld of type %d at %s in world: %s"),
            EntityId, (int32)Type, *Location.ToString(), World ? *World->GetName() : TEXT("NULL"));
    }
    FNetworkedEntityInfo Info;
    Info.EntityId = EntityId;
    Info.EntityType = Type;
    Info.ShardId = ShardId;
    Info.Location = Location;
    Info.Rotation = Rotation;
    Info.Actor = Actor;
    Entities.Add(EntityId, Info);
    ShardEntities.FindOrAdd(ShardId).Add(EntityId);
    // Add to type-specific map
    switch (Type) {
        case ENetworkedEntityType::Player:
            PlayerEntities.Add(EntityId, Info);
            break;
        case ENetworkedEntityType::Mob:
            MobEntities.Add(EntityId, Info);
            break;
        case ENetworkedEntityType::NPC:
            NPCEntities.Add(EntityId, Info);
            break;
        case ENetworkedEntityType::Item:
            ItemEntities.Add(EntityId, Info);
            break;
        default:
            break;
    }
}

void UNetworkedEntityManager::DespawnEntity(int64 EntityId)
{
    FNetworkedEntityInfo* Info = Entities.Find(EntityId);
    if (!Info) return;
    if (Info->Actor.IsValid()) {
        Info->Actor->Destroy();
        SpawnedActors.Remove(Info->Actor.Get()); // Remove strong reference
    }
    ShardEntities.FindOrAdd(Info->ShardId).Remove(EntityId);
    // Remove from type-specific map
    switch (Info->EntityType) {
        case ENetworkedEntityType::Player:
            PlayerEntities.Remove(EntityId);
            break;
        case ENetworkedEntityType::Mob:
            MobEntities.Remove(EntityId);
            break;
        case ENetworkedEntityType::NPC:
            NPCEntities.Remove(EntityId);
            break;
        case ENetworkedEntityType::Item:
            ItemEntities.Remove(EntityId);
            break;
        default:
            break;
    }
    Entities.Remove(EntityId);
}

void UNetworkedEntityManager::MoveEntity(int64 EntityId, const FVector& NewLocation, const FRotator& NewRotation)
{
    FNetworkedEntityInfo* Info = Entities.Find(EntityId);
    if (!Info) return;
    Info->Location = NewLocation;
    Info->Rotation = NewRotation;
    if (Info->Actor.IsValid()) {
        Info->Actor->SetActorLocationAndRotation(NewLocation, NewRotation);
    }
}

void UNetworkedEntityManager::SetEntityShard(int64 EntityId, int32 NewShardId)
{
    FNetworkedEntityInfo* Info = Entities.Find(EntityId);
    if (!Info) return;
    if (Info->ShardId != NewShardId) {
        ShardEntities.FindOrAdd(Info->ShardId).Remove(EntityId);
        Info->ShardId = NewShardId;
        ShardEntities.FindOrAdd(NewShardId).Add(EntityId);
    }
}

void UNetworkedEntityManager::DeloadEntity(int64 EntityId)
{
    FNetworkedEntityInfo* Info = Entities.Find(EntityId);
    if (!Info) return;
    if (Info->Actor.IsValid()) {
        Info->Actor->Destroy();
    }
    ShardEntities.FindOrAdd(Info->ShardId).Remove(EntityId);
    Entities.Remove(EntityId);
}

FNetworkedEntityInfo UNetworkedEntityManager::GetEntityInfo(int64 EntityId) const
{
    const FNetworkedEntityInfo* Info = Entities.Find(EntityId);
    return Info ? *Info : FNetworkedEntityInfo();
}

TArray<FNetworkedEntityInfo> UNetworkedEntityManager::GetEntitiesInShard(int32 ShardId) const
{
    TArray<FNetworkedEntityInfo> Result;
    const TArray<int64>* Ids = ShardEntities.Find(ShardId);
    if (!Ids) return Result;
    for (int64 Id : *Ids) {
        const FNetworkedEntityInfo* Info = Entities.Find(Id);
        if (Info) Result.Add(*Info);
    }
    return Result;
}


void UNetworkedEntityManager::HandleEntityMovePacket(const S_Move& Packet)
{
    MoveEntity(Packet.entityId, FVector(Packet.x, Packet.y, Packet.z), FRotator(0, Packet.yaw, 0));
}

// --- Type-specific MMOClient integration points ---
void UNetworkedEntityManager::HandlePlayerSpawnPacket(const S_PlayerSpawn& Packet)
{
    // Convert char[] name to FString
    FString Name = FString(UTF8_TO_TCHAR(Packet.name));
    SpawnPlayerEntity(Packet.entityId, Packet.shardId, FVector(Packet.x, Packet.y, Packet.z), FRotator(0, Packet.yaw, 0), Name, static_cast<int32>(Packet.classId), Packet.level);
}
void UNetworkedEntityManager::HandlePlayerDespawnPacket(const S_PlayerDespawn& Packet)
{
    DespawnEntity(Packet.entityId);
}
void UNetworkedEntityManager::HandleMobSpawnPacket(const S_MobSpawn& Packet)
{
    SpawnMobEntity(Packet.entityId, Packet.shardId, FVector(Packet.x, Packet.y, Packet.z), FRotator(0, Packet.yaw, 0), Packet.mobTypeId, Packet.level);
}
void UNetworkedEntityManager::HandleMobDespawnPacket(const S_MobDespawn& Packet)
{
    DespawnEntity(Packet.entityId);
}
void UNetworkedEntityManager::HandleNPCSpawnPacket(const S_NPCSpawn& Packet)
{
    SpawnNPCEntity(Packet.entityId, Packet.shardId, FVector(Packet.x, Packet.y, Packet.z), FRotator(0, Packet.yaw, 0), Packet.npcTypeId);
}
void UNetworkedEntityManager::HandleNPCDespawnPacket(const S_NPCDespawn& Packet)
{
    DespawnEntity(Packet.entityId);
}
void UNetworkedEntityManager::HandleItemSpawnPacket(const S_ItemSpawn& Packet)
{
    SpawnItemEntity(Packet.entityId, Packet.shardId, FVector(Packet.x, Packet.y, Packet.z), Packet.itemId, Packet.count);
}
void UNetworkedEntityManager::HandleItemDespawnPacket(const S_ItemDespawn& Packet)
{
    DespawnEntity(Packet.entityId);
}

// Type-specific spawn helpers (extend as needed for extra info)
void UNetworkedEntityManager::SpawnPlayerEntity(int64 EntityId, int32 ShardId, const FVector& Location, const FRotator& Rotation, const FString& Name, int32 ClassId, int32 Level)
{
    // TODO: Store player-specific info in FNetworkedEntityInfo or a derived struct if needed
    SpawnEntity(EntityId, ENetworkedEntityType::Player, ShardId, Location, Rotation);
    // You can use Name, ClassId, Level as needed
    FNetworkedEntityInfo* Info = Entities.Find(EntityId);
    if (Info) {
       // Info->Name = Name; // Store name in the info struct
        //Info->ClassId = ClassId; // Store class ID
        //Info->Level = Level; // Store level
    }
    if (Info && Info->Actor.IsValid()) {
        Info->Actor->SetActorLabel(Name);
        // Possess the actor if this is the local player
        UWorld* World = Info->Actor->GetWorld();
        if (World)
        {
            UGameInstance* GameInstance = World->GetGameInstance();
            UMMOGameInstance* MMOGameInstance = Cast<UMMOGameInstance>(GameInstance);
            if (MMOGameInstance && EntityId == MMOGameInstance->GetSelectedCharacterId())
            {
                APlayerController* PC = World->GetFirstPlayerController();
                APawn* Pawn = Cast<APawn>(Info->Actor.Get());
                if (PC && Pawn)
                {
                    PC->Possess(Pawn);
                }
            }
        }
    }


}

void UNetworkedEntityManager::SpawnMobEntity(int64 EntityId, int32 ShardId, const FVector& Location, const FRotator& Rotation, int32 MobTypeId, int32 Level)
{
    SpawnEntity(EntityId, ENetworkedEntityType::Mob, ShardId, Location, Rotation);
    // You can use MobTypeId, Level as needed
}

void UNetworkedEntityManager::SpawnNPCEntity(int64 EntityId, int32 ShardId, const FVector& Location, const FRotator& Rotation, int32 NPCTypeId)
{
    SpawnEntity(EntityId, ENetworkedEntityType::NPC, ShardId, Location, Rotation);
    // You can use NPCTypeId as needed
}

void UNetworkedEntityManager::SpawnItemEntity(int64 EntityId, int32 ShardId, const FVector& Location, int32 ItemId, int32 Count)
{
    SpawnEntity(EntityId, ENetworkedEntityType::Item, ShardId, Location, FRotator::ZeroRotator);
    // You can use ItemId, Count as needed
}
