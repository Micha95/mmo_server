#pragma once
#include "Engine/GameInstance.h"
#include "NetworkedEntityManager.h" // Added for UNetworkedEntityManager
#include "Packets.h" // Added for packet structs and constants
#include "MMOGameInstance.generated.h"


// If GPlayInEditorID is not defined elsewhere, add a comment or define as needed
// extern int32 GPlayInEditorID; // Uncomment if needed

UCLASS()
class MMOCLIENT_API UMMOGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly)
    UMMOClient* MMOClient;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MMO|Auth")
    FString AuthServerIP = TEXT("127.0.0.1");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MMO|Auth")
    int32 AuthServerPort = 5000;

    UFUNCTION(BlueprintCallable)
    UMMOClient* GetMMOClient() const { return MMOClient; }

    UFUNCTION(BlueprintCallable, Category = "MMO|Auth")
    void Login(const FString& Username, const FString& Password);

    UFUNCTION(BlueprintCallable, Category = "MMO|Char")
    void CreateCharacter(const FString& Name, int32 ClassId);

    UFUNCTION(BlueprintCallable, Category = "MMO|Char")
    void DeleteCharacter(int32 CharId);

    UFUNCTION(BlueprintCallable, Category = "MMO|Char")
    void ListCharacters();

    UFUNCTION(BlueprintCallable, Category = "MMO|Char")
    void SelectCharacter(int32 CharId);

    UFUNCTION(BlueprintCallable, Category = "MMO|Movement")
    void SendMoveRequest(const FVector& NewLocation);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MMO|Network")
    TSubclassOf<UNetworkedEntityManager> NetworkedEntityManagerClass;

    UPROPERTY(BlueprintReadOnly)
    UNetworkedEntityManager* NetworkedEntityManager = nullptr;

    UFUNCTION(BlueprintCallable, Category="MMO|Network")
    void SetNetworkedEntityManager(UNetworkedEntityManager* Manager) { NetworkedEntityManager = Manager; }

    // Returns the PIE (Play-In-Editor) client index, or -1 if not in PIE
    UFUNCTION(BlueprintCallable, Category="MMO|Debug")
    int32 GetPIEClientIndex() const;


    // Character ID
    UPROPERTY(BlueprintReadOnly, Category="MMO|Char")
    int32 SelectedCharacterId = -1;
    virtual void Init() override;
    virtual void Shutdown() override;

    UFUNCTION(BlueprintCallable, Category="MMO|Char")
    int32 GetSelectedCharacterId() const { return SelectedCharacterId; }

    void ReinitializeNetworkedEntityManager();
};
