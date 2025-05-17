#include "MMOGameInstance.h"
#include "MMOClient.h"

void UMMOGameInstance::Init()
{
    Super::Init();
    // Create MMOClient UObject
    MMOClient = NewObject<UMMOClient>(this, UMMOClient::StaticClass());
    if (MMOClient)
    {
         MMOClient->AddToRoot(); // Prevent GC (optional, for debugging)
        // Use Blueprint-configurable IP/port
        MMOClient->ConnectAuth(AuthServerIP, AuthServerPort);
    }
}

void UMMOGameInstance::Shutdown()
{
    if (MMOClient)
    {
        MMOClient->Shutdown();
        MMOClient->RemoveFromRoot(); // Allow GC if desired
        MMOClient = nullptr;
    }
    Super::Shutdown();
}

void UMMOGameInstance::Login(const FString& Username, const FString& Password)
{
    if (MMOClient)
    {
        // Construct login packet (assuming C_LoginRequest struct exists and matches server)
        C_LoginRequest LoginPacket;
        FMemory::Memzero(&LoginPacket, sizeof(LoginPacket));
        LoginPacket.header.packetId = PACKET_C_LOGIN_REQUEST;
        FCStringAnsi::Strncpy(LoginPacket.username, TCHAR_TO_UTF8(*Username), sizeof(LoginPacket.username) - 1);
        FCStringAnsi::Strncpy(LoginPacket.password, TCHAR_TO_UTF8(*Password), sizeof(LoginPacket.password) - 1);
        LoginPacket.usernameLength = FCStringAnsi::Strlen(LoginPacket.username);
        LoginPacket.passwordLength = FCStringAnsi::Strlen(LoginPacket.password);
        TArray<uint8> Data;
        SerializeStruct(LoginPacket, Data);
        MMOClient->SendToAuth(Data);
    }
}
