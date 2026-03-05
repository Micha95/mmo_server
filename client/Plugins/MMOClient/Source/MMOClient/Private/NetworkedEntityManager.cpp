// NetworkedEntityManager.cpp
// (c) 2025 MMOClient
#include "NetworkedEntityManager.h"
#include "MMOGameInstance.h" // Fix for UMMOGameInstance undefined
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
// For AMMOCharacter reference
#include "MMOCharacter.h"
// For movement component
#include "GameFramework/CharacterMovementComponent.h"
// For AI movement
#include "AIController.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Navigation/PathFollowingComponent.h"
// Define a log category for MMOClient
DEFINE_LOG_CATEGORY_STATIC(LogMMOClient, Log, All);
// --- AActor implementation for placable manager ---
UNetworkedEntityManager::UNetworkedEntityManager()
{
}

void UNetworkedEntityManager::PostInitProperties()
{
    Super::PostInitProperties();
    UE_LOG(LogMMOClient, Log, TEXT("[NetworkedEntityManager] (PostInitProperties) Instance class: %s"), *GetClass()->GetName());
    if (PlayerActorClass.IsValid()) {
        UE_LOG(LogMMOClient, Log, TEXT("[NetworkedEntityManager] (PostInitProperties) PlayerActorClass set to: %s"), *PlayerActorClass->GetName());
    } else {
        UE_LOG(LogMMOClient, Warning, TEXT("[NetworkedEntityManager] (PostInitProperties) PlayerActorClass is NOT set."));
    }
}

TWeakObjectPtr<UNetworkedEntityManager> UNetworkedEntityManager::Singleton;

UNetworkedEntityManager* UNetworkedEntityManager::Get(UGameInstance* GameInstance)
{
    if (!Singleton.IsValid()) {
        if (GameInstance) {
            UMMOGameInstance* MMOInst = Cast<UMMOGameInstance>(GameInstance);
            if (MMOInst && MMOInst->NetworkedEntityManagerClass) {
                UNetworkedEntityManager* NewMgr = NewObject<UNetworkedEntityManager>(GameInstance, MMOInst->NetworkedEntityManagerClass);
                if (NewMgr) {
                    // Copy Blueprint default property values from CDO
                    UNetworkedEntityManager* CDO = Cast<UNetworkedEntityManager>(MMOInst->NetworkedEntityManagerClass->GetDefaultObject());
                    if (CDO) {
                        NewMgr->PlayerActorClass = CDO->PlayerActorClass;
                        if (!NewMgr->PlayerActorClass.IsNull()) {
                            NewMgr->PlayerActorClass.LoadSynchronous();
                        }
                        NewMgr->MobActorClass = CDO->MobActorClass;
                        if (!NewMgr->MobActorClass.IsNull()) {
                            NewMgr->MobActorClass.LoadSynchronous();
                        }
                        NewMgr->NPCActorClass = CDO->NPCActorClass;
                        if (!NewMgr->NPCActorClass.IsNull()) {
                            NewMgr->NPCActorClass.LoadSynchronous();
                        }
                        NewMgr->ItemActorClass = CDO->ItemActorClass;
                        if (!NewMgr->ItemActorClass.IsNull()) {
                            NewMgr->ItemActorClass.LoadSynchronous();
                        }
                    }
                    NewMgr->RegisterWithGameInstance(GameInstance);
                    Singleton = NewMgr;
                }
            }
        }
    }
    return Singleton.Get();
}


void UNetworkedEntityManager::ClearAllEntities()
{
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
            if (PlayerActorClass.IsValid()) {
                ActorClass = PlayerActorClass.Get();
            } else if (!PlayerActorClass.IsNull()) {
                ActorClass = PlayerActorClass.LoadSynchronous();
            }
            if (!ActorClass) {
                UE_LOG(LogMMOClient, Error, TEXT("[NetworkedEntityManager] PlayerActorClass is not set or failed to load! Cannot spawn player entity."));
                return nullptr;
            }
            break;
        case ENetworkedEntityType::Mob:
            if (MobActorClass.IsValid()) {
                ActorClass = MobActorClass.Get();
            } else if (!MobActorClass.IsNull()) {
                ActorClass = MobActorClass.LoadSynchronous();
            } else {
                ActorClass = AActor::StaticClass();
            }
            break;
        case ENetworkedEntityType::NPC:
            if (NPCActorClass.IsValid()) {
                ActorClass = NPCActorClass.Get();
            } else if (!NPCActorClass.IsNull()) {
                ActorClass = NPCActorClass.LoadSynchronous();
            } else {
                ActorClass = AActor::StaticClass();
            }
            break;
        case ENetworkedEntityType::Item:
            if (ItemActorClass.IsValid()) {
                ActorClass = ItemActorClass.Get();
            } else if (!ItemActorClass.IsNull()) {
                ActorClass = ItemActorClass.LoadSynchronous();
            } else {
                ActorClass = AActor::StaticClass();
            }
            break;
        default:
            ActorClass = AActor::StaticClass();
            break;
    }
    if (!ActorClass || !World) {
        UE_LOG(LogMMOClient, Error, TEXT("[NetworkedEntityManager] Invalid ActorClass or World for entity spawn (Type=%d)"), (int32)Type);
        return nullptr;
    }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* Spawned = World->SpawnActor<AActor>(ActorClass, Location, Rotation, Params);
    if (!Spawned) {
        UE_LOG(LogMMOClient, Error, TEXT("[NetworkedEntityManager] Failed to spawn actor of class %s at %s (Type=%d)"),
            *GetNameSafe(ActorClass), *Location.ToString(), (int32)Type);
    }
    return Spawned;
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

        // If Actor is a subclass of AMMOCharacter, set its EntityId
        class AMMOCharacter* MMOChar = Cast<AMMOCharacter>(Actor);
        if (MMOChar)
        {
            MMOChar->EntityId = EntityId;
        }
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
    FVector PreviousLocation = Info->Location;
    Info->Location = NewLocation;
    Info->Rotation = NewRotation;
    if (Info->Actor.IsValid()) {
        AMMOCharacter* MMOChar = Cast<AMMOCharacter>(Info->Actor.Get());
        if (MMOChar && MMOChar->GetCharacterMovement()) {
            FVector CurrentLocation = MMOChar->GetActorLocation();
            FVector MoveDelta = NewLocation - CurrentLocation;
            float TeleportThreshold = 1000.0f;
            if (MoveDelta.Size() > TeleportThreshold) {
                MMOChar->SetActorLocation(NewLocation);
                UE_LOG(LogMMOClient, Warning, TEXT("[NetworkedEntityManager] Teleporting EntityId=%lld to %s (Delta=%s)"), EntityId, *NewLocation.ToString(), *MoveDelta.ToString());
            } else {
                // Use AIController MoveToLocation for remote movement and animation
                AAIController* AIController = Cast<AAIController>(MMOChar->GetController());
                if (!AIController) {
                    MMOChar->SpawnDefaultController();
                    AIController = Cast<AAIController>(MMOChar->GetController());
                }
                if (AIController) {
                    FAIMoveRequest MoveRequest;
                    MoveRequest.SetGoalLocation(NewLocation);
                    MoveRequest.SetAcceptanceRadius(5.0f);
                    MoveRequest.SetUsePathfinding(true);
                    MoveRequest.SetAllowPartialPath(true);
                    AIController->MoveTo(MoveRequest);
                    UE_LOG(LogMMOClient, Log, TEXT("[NetworkedEntityManager] AIController MoveTo for EntityId=%lld to %s"), EntityId, *NewLocation.ToString());
                }
            }

            FRotator CurrentRotation = MMOChar->GetActorRotation();
            float AngleDiff = FMath::Abs((CurrentRotation - NewRotation).GetNormalized().Yaw);
            if (AngleDiff > 10.0f) {
               // MMOChar->SetActorRotation(NewRotation); // not needed as we simply use the automatic rotation
            }

            // Improved jump detection and cooldown
            float JumpThreshold = 20.0f; // Minimum Z change to trigger jump
            double JumpCooldown = 0.5; // Minimum time between jumps
            double CurrentTime = MMOChar->GetWorld() ? MMOChar->GetWorld()->GetTimeSeconds() : 0.0;
            bool ShouldJump = (NewLocation.Z - PreviousLocation.Z) > JumpThreshold
                && MMOChar->GetCharacterMovement()->IsMovingOnGround()
                && (CurrentTime - Info->LastJumpTime > JumpCooldown)
                && !MMOChar->GetCharacterMovement()->IsFalling();
            if (ShouldJump) {
                MMOChar->Jump();
                Info->LastJumpTime = CurrentTime;
            }
        } else if (MMOChar) {
            MMOChar->SetActorLocationAndRotation(NewLocation, NewRotation);
        } else {
            // Fallback for non-character actors
            Info->Actor->SetActorLocationAndRotation(NewLocation, NewRotation);
        }
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
    UE_LOG(LogMMOClient, Log, TEXT("[NetworkedEntityManager] Spawning player entity: Id=%lld, Shard=%d, Name=%s, ClassId=%d, Level=%d, Location=%s, Rotation=%s"),
        EntityId, ShardId, *Name, ClassId, Level, *Location.ToString(), *Rotation.ToString());
    if (PlayerActorClass.IsValid()) {
        UE_LOG(LogMMOClient, Log, TEXT("[NetworkedEntityManager] (RUNTIME) PlayerActorClass is: %s"), *PlayerActorClass->GetName());
    } else {
        UE_LOG(LogMMOClient, Warning, TEXT("[NetworkedEntityManager] (RUNTIME) PlayerActorClass is NOT set."));
    }
    FNetworkedEntityInfo* Info = Entities.Find(EntityId);
    if(!Info) {
        UE_LOG(LogMMOClient, Log, TEXT("[NetworkedEntityManager] No existing entity info found for EntityId=%lld. Spawning new entity."), EntityId);
        SpawnEntity(EntityId, ENetworkedEntityType::Player, ShardId, Location, Rotation);
        Info = Entities.Find(EntityId);
        if (!Info) {
            UE_LOG(LogMMOClient, Error, TEXT("[NetworkedEntityManager] Failed to find entity info after spawning for EntityId=%lld"), EntityId);
            return;
        }
        if (Info && Info->Actor.IsValid()) {
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
                        UE_LOG(LogMMOClient, Log, TEXT("[NetworkedEntityManager] Possessing player pawn for local player: %s (EntityId=%lld)"), *Name, EntityId);
                        PC->Possess(Pawn);
                    }
                    else
                    {
                        FString ActorClassName = Info->Actor.IsValid() ? Info->Actor->GetClass()->GetName() : TEXT("NULL");
                        UE_LOG(LogMMOClient, Warning, TEXT("[NetworkedEntityManager] Failed to possess pawn: PC=%p, Pawn=%p (EntityId=%lld, ActorClass=%s)"), PC, Pawn, EntityId, *ActorClassName);
                    }
                }
            }
        }
        else
        {
            UE_LOG(LogMMOClient, Warning, TEXT("[NetworkedEntityManager] Failed to spawn player actor for EntityId=%lld"), EntityId);
        }
        
    } else {
        UE_LOG(LogMMOClient, Log, TEXT("[NetworkedEntityManager] Found existing entity info for EntityId=%lld. Updating location and properties."), EntityId);
        MoveEntity(EntityId, Location, Rotation);
    }
    if (Info && Info->Actor.IsValid()) {
        // Set the Name property if this is an AMMOCharacter
        AMMOCharacter* MMOChar = Cast<AMMOCharacter>(Info->Actor.Get());
        if (MMOChar) {
            MMOChar->Name = Name;
            MMOChar->ClassId = ClassId;
            MMOChar->Level = Level;
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
