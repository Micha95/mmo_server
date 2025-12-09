#include "MMOPlayerMovementComponent.h"
#include "MMOClient.h"
#include "GameFramework/Actor.h"
#include "MMOGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Misc/OutputDeviceDebug.h"

UMMOPlayerMovementComponent::UMMOPlayerMovementComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = true; // Ensure movement component ticks!
    bAutoActivate = true;
}

void UMMOPlayerMovementComponent::BeginPlay()
{
    Super::BeginPlay();
    if (const UWorld* World = GetWorld())
    {
        if (UGameInstance* GameInstance = World->GetGameInstance())
        {
            if (UMMOGameInstance* MMOGameInstance = Cast<UMMOGameInstance>(GameInstance))
            {
                if (UMMOClient* MMOClient = MMOGameInstance->GetMMOClient())
                {
                    MMOClient->OnMoveResponse.AddDynamic(this, &UMMOPlayerMovementComponent::OnMoveResponse);
                }
            }
        }
        // Start timer to send position every 10ms (100Hz)
        World->GetTimerManager().SetTimer(MoveTickHandle, this, &UMMOPlayerMovementComponent::SendCurrentPositionToServer, 0.01f, true);
    }
}

void UMMOPlayerMovementComponent::SendCurrentPositionToServer()
{
    if (GetNetMode() == NM_Standalone) // Only send in Standalone/Client mode
    {
        if (AActor* Owner = GetOwner())
        {
            FVector CurrentPos = Owner->GetActorLocation();
            if ((!CurrentPos.IsNearlyZero() || bHasSentValidPosition) && !CurrentPos.Equals(LastSentPos, 0.01f))
            {
                UE_LOG(LogTemp, Warning, TEXT("[MMO] Sending position to server: %s"), *CurrentPos.ToString());
                SendMoveRequestToServer(CurrentPos);
                bHasSentValidPosition = true;
                LastSentPos = CurrentPos;
            }
            else if (CurrentPos.IsNearlyZero() && !bHasSentValidPosition)
            {
                UE_LOG(LogTemp, Warning, TEXT("[MMO] Skipping send: Actor at (0,0,0)"));
            }
        }
    }
}

void UMMOPlayerMovementComponent::SendMoveRequestToServer(const FVector& Position)
{
    if (const UWorld* World = GetWorld())
    {
        if (UGameInstance* GameInstance = World->GetGameInstance())
        {
            if (UMMOGameInstance* MMOGameInstance = Cast<UMMOGameInstance>(GameInstance))
            {
                MMOGameInstance->SendMoveRequest(Position);
            }
        }
    }
}

void UMMOPlayerMovementComponent::OnMoveResponse(FVector ConfirmedLocation)
{
    UE_LOG(LogTemp, Warning, TEXT("[MMO] OnMoveResponse: Server confirmed position: %s"), *ConfirmedLocation.ToString());
    OnServerMoveConfirmed(ConfirmedLocation);
}

void UMMOPlayerMovementComponent::OnServerMoveConfirmed(const FVector& ConfirmedLocation)
{
    if (AActor* Owner = GetOwner())
    {
        const float Tolerance = 0.1f;
        if (!Owner->GetActorLocation().Equals(ConfirmedLocation, Tolerance))
        {
            UE_LOG(LogTemp, Warning, TEXT("[MMO] Correcting position from %s to %s"), *Owner->GetActorLocation().ToString(), *ConfirmedLocation.ToString());
            Owner->SetActorLocation(ConfirmedLocation);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[MMO] No correction needed. Position matches server."));
        }
    }
}
