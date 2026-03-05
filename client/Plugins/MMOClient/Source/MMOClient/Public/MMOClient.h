#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MMOClient.generated.h"

// Character list entry for Blueprint
USTRUCT(BlueprintType)
struct FCharListEntry {
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    int32 CharId;
    UPROPERTY(BlueprintReadOnly)
    FString Name;
    UPROPERTY(BlueprintReadOnly)
    int32 ClassId; // Changed from int16 to int32 for Blueprint compatibility
};
struct FHeartbeatState {
        FTimerHandle TimerHandle;
        int32 LastReceivedTimestamp = 0;
};
UENUM(BlueprintType)
enum class EMMOClientState : uint8 {
    DISCONNECTED     UMETA(DisplayName = "Disconnected"),
    CONNECTING_AUTH       UMETA(DisplayName = "Auth Connecting"),
    AUTH             UMETA(DisplayName = "Auth Connected"),
    CONNECTING_GAME       UMETA(DisplayName = "Game Connecting"),
    GAME             UMETA(DisplayName = "Game Connected")
};

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

    UFUNCTION(BlueprintCallable, Category = "MMOClient")
    void DisconnectAuth();
    UFUNCTION(BlueprintCallable, Category = "MMOClient")
    void DisconnectGame();
    UFUNCTION(BlueprintCallable, Category = "MMOClient")
    void DisconnectChat();

    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLoginResult, bool, bSuccess);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient")
    FOnLoginResult OnLoginResult;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharSelectSuccess, bool, bSuccess);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient")
    FOnCharSelectSuccess OnCharSelectSuccess;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharCreateSuccess, bool, bSuccess);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient")
    FOnCharCreateSuccess OnCharCreateSuccess;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharDeleteSuccess, bool, bSuccess);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient")
    FOnCharDeleteSuccess OnCharDeleteSuccess;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCharListResult, const TArray<FCharListEntry>&, CharList, int32, CharCount);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient")
    FOnCharListResult OnCharListResult;

    // Connection delegates
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAuthConnected);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient|Connection")
    FOnAuthConnected OnAuthConnected;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGameConnected);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient|Connection")
    FOnGameConnected OnGameConnected;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnChatConnected);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient|Connection")
    FOnChatConnected OnChatConnected;

    // Disconnect delegates
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAuthDisconnected);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient|Connection")
    FOnAuthDisconnected OnAuthDisconnected;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGameDisconnected);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient|Connection")
    FOnGameDisconnected OnGameDisconnected;

    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnChatDisconnected);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient|Connection")
    FOnChatDisconnected OnChatDisconnected;

    // State enum and property
    UPROPERTY(BlueprintReadOnly, Category = "MMOClient|State")
    EMMOClientState CurrentState = EMMOClientState::DISCONNECTED;

    // Track last state for reconnection logic
    UPROPERTY(BlueprintReadOnly, Category = "MMOClient|State")
    EMMOClientState LastState = EMMOClientState::DISCONNECTED;

    // Delegate for state change
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnClientStateChanged, EMMOClientState, NewState);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient|State")
    FOnClientStateChanged OnClientStateChanged;

    // Call this to set state and broadcast if changed
    UFUNCTION(BlueprintCallable, Category = "MMOClient|State")
    void SetClientState(EMMOClientState NewState);
    virtual void BeginDestroy() override;

    UFUNCTION(BlueprintCallable, Category = "MMOClient|Session")
    void SetSessionKey(const FString& InKey);
    UFUNCTION(BlueprintCallable, Category = "MMOClient|Session")
    FString GetSessionKey() const;

private:
    TSharedPtr<class FSocket> AuthSocket;
    TSharedPtr<class FSocket> GameSocket;
    TSharedPtr<class FSocket> ChatSocket;
    int32 gameRetryCount = 0;

    FTimerHandle AuthRecvHandle, GameRecvHandle, ChatRecvHandle;
    FTimerHandle HeartbeatTimerHandle;
    void OnReceive(TSharedPtr<FSocket> Socket, FString ServerType);

    void HandleAuthPacket(const TArray<uint8>& Data);
    void HandleGamePacket(const TArray<uint8>& Data);
    void HandleChatPacket(const TArray<uint8>& Data);

    bool EncryptAndCompress(const TArray<uint8>& In, TArray<uint8>& Out);
    bool DecryptAndDecompress(const TArray<uint8>& In, TArray<uint8>& Out);

    void TickSockets();
    void StartHeartbeat();
    void StopHeartbeat();
    void SendHeartbeat();

    constexpr static float HEARTBEAT_INTERVAL = 2.0f;
    constexpr static float HEARTBEAT_TIMEOUT = 6.0f;

    

    // Per-socket heartbeat state (defined as static in .cpp)
    static FHeartbeatState AuthHeartbeat;
    static FHeartbeatState GameHeartbeat;
    static FHeartbeatState ChatHeartbeat;

    void StartHeartbeatForSocket(const FString& SocketType);
    void StopHeartbeatForSocket(const FString& SocketType);
    void SendHeartbeat(const FString& SocketType);
    void CheckHeartbeatTimeout(const FString& SocketType, FHeartbeatState& State);

    // Store last used host/port for Auth and Game
    FString LastAuthHost;
    int32 LastAuthPort = 0;
    FString LastGameHost;
    int32 LastGamePort = 0;

    FTimerHandle TickTimerHandle;
    FString SessionKey;

    public:
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMoveResponse, FVector, ConfirmedLocation);
    UPROPERTY(BlueprintAssignable, Category = "MMOClient|Movement")
    FOnMoveResponse OnMoveResponse;

};

class UMMOPlayerMovementComponent;
class AMMOCharacter;
