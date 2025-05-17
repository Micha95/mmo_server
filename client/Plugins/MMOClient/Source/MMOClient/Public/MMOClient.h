#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MMOClient.generated.h"

UCLASS(Blueprintable)
class MMOCLIENT_API UMMOClient : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "MMOClient")
    void ConnectAuth(const FString& Host, int32 Port);

    UFUNCTION(BlueprintCallable, Category = "MMOClient")
    void ConnectGame(const FString& Host, int32 Port);

    UFUNCTION(BlueprintCallable, Category = "MMOClient")
    void ConnectChat(const FString& Host, int32 Port);

    UFUNCTION(BlueprintCallable, Category = "MMOClient")
    void SendToAuth(const TArray<uint8>& Data);

    UFUNCTION(BlueprintCallable, Category = "MMOClient")
    void SendToGame(const TArray<uint8>& Data);

    UFUNCTION(BlueprintCallable, Category = "MMOClient")
    void SendToChat(const TArray<uint8>& Data);

    UFUNCTION(BlueprintCallable, Category = "MMOClient")
    void DisconnectAll();

    UFUNCTION(BlueprintCallable, Category = "MMOClient")
    void Shutdown();

    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoginResult, bool, bSuccess);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient")
    FOnLoginResult OnLoginResult;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCharSelectSuccess);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient")
    FOnCharSelectSuccess OnCharSelectSuccess;

private:
    TSharedPtr<class FSocket> AuthSocket;
    TSharedPtr<class FSocket> GameSocket;
    TSharedPtr<class FSocket> ChatSocket;

    FTimerHandle AuthRecvHandle, GameRecvHandle, ChatRecvHandle;
    void OnReceive(TSharedPtr<FSocket> Socket, FString ServerType);

    void HandleAuthPacket(const TArray<uint8>& Data);
    void HandleGamePacket(const TArray<uint8>& Data);
    void HandleChatPacket(const TArray<uint8>& Data);

    bool EncryptAndCompress(const TArray<uint8>& In, TArray<uint8>& Out);
    bool DecryptAndDecompress(const TArray<uint8>& In, TArray<uint8>& Out);
};
