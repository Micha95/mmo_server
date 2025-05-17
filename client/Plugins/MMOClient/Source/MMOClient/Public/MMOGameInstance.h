#pragma once
#include "Engine/GameInstance.h"
#include "MMOGameInstance.generated.h"

UCLASS()
class MMOCLIENT_API UMMOGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly)
    FString SessionKey;

    UPROPERTY(BlueprintReadOnly)
    UMMOClient* MMOClient;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MMO|Auth")
    FString AuthServerIP = TEXT("127.0.0.1");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MMO|Auth")
    int32 AuthServerPort = 5000;

    UFUNCTION(BlueprintCallable)
    void SetSessionKey(const FString& InKey) { SessionKey = InKey; }
    UFUNCTION(BlueprintCallable)
    FString GetSessionKey() const { return SessionKey; }
    UFUNCTION(BlueprintCallable)
    UMMOClient* GetMMOClient() const { return MMOClient; }

    UFUNCTION(BlueprintCallable, Category = "MMO|Auth")
    void Login(const FString& Username, const FString& Password);

    virtual void Init() override;
    virtual void Shutdown() override;
};
