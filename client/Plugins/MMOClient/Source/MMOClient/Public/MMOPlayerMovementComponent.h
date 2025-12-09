#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "MMOPlayerMovementComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MMOCLIENT_API UMMOPlayerMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    UMMOPlayerMovementComponent(const FObjectInitializer& ObjectInitializer);

protected:
    virtual void BeginPlay() override;

    FTimerHandle MoveTickHandle;
    void SendCurrentPositionToServer();
    void SendMoveRequestToServer(const FVector& Position);
    void OnMoveResponse(FVector ConfirmedLocation);
    void OnServerMoveConfirmed(const FVector& ConfirmedLocation);

    FVector LastSentPos = FVector::ZeroVector;
    bool bHasSentValidPosition = false;
};
