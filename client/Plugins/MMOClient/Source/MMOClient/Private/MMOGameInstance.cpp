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
    // Create NetworkedEntityManager UObject automatically
    if (NetworkedEntityManagerClass)
    {
        NetworkedEntityManager = NewObject<UNetworkedEntityManager>(this, NetworkedEntityManagerClass);
        if (NetworkedEntityManager)
        {
            NetworkedEntityManager->RegisterWithGameInstance(this);
        }
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

void UMMOGameInstance::CreateCharacter(const FString& Name, int32 ClassId)
{
    if (MMOClient)
    {
        // Construct and send C_CharCreate packet
        C_CharCreate Packet;
        FMemory::Memzero(&Packet, sizeof(Packet));
        Packet.header.packetId = PACKET_C_CHAR_CREATE;
        FCStringAnsi::Strncpy(Packet.name, TCHAR_TO_UTF8(*Name), sizeof(Packet.name) - 1);
        Packet.nameLength = FCStringAnsi::Strlen(Packet.name);
        Packet.classId = ClassId;
        MMOClient->SendToAuth(TArray<uint8>((uint8*)&Packet, sizeof(Packet)));
    }
}

void UMMOGameInstance::DeleteCharacter(int32 CharId)
{
    if (MMOClient)
    {
        // Construct and send C_CharDelete packet
        C_CharDelete Packet;
        FMemory::Memzero(&Packet, sizeof(Packet));
        Packet.header.packetId = PACKET_C_CHAR_DELETE;
        Packet.charId = CharId;
        MMOClient->SendToAuth(TArray<uint8>((uint8*)&Packet, sizeof(Packet)));
    }
}

void UMMOGameInstance::ListCharacters()
{
    if (MMOClient)
    {
        // Construct and send C_CharListRequest packet
        C_CharListRequest Packet;
        FMemory::Memzero(&Packet, sizeof(Packet));
        Packet.header.packetId = PACKET_C_CHAR_LIST_REQUEST;
        MMOClient->SendToAuth(TArray<uint8>((uint8*)&Packet, sizeof(Packet)));
    }
}

void UMMOGameInstance::SelectCharacter(int32 CharId)
{
    if (MMOClient)
    {
        // Construct and send C_CharSelect packet
        C_CharSelect Packet;
        FMemory::Memzero(&Packet, sizeof(Packet));
        Packet.header.packetId = PACKET_C_CHAR_SELECT;
        Packet.charId = CharId;
        // Store selected character ID for later use
        SelectedCharacterId = CharId;
        // Send the packet to the auth server
        MMOClient->SendToAuth(TArray<uint8>((uint8*)&Packet, sizeof(Packet)));
    }
}

void UMMOGameInstance::SendMoveRequest(const FVector& NewLocation)
{
    if (MMOClient)
    {
        // Serialize and send move request

        C_Move MovePacket;
        memset(&MovePacket, 0, sizeof(MovePacket)); // Always zero-initialize!
        MovePacket.header.packetId = PACKET_C_MOVE;
        MovePacket.x = NewLocation.X;
        MovePacket.y = NewLocation.Y;
        MovePacket.z = NewLocation.Z;
        // Send as a single buffer
        MMOClient->SendToGame(TArray<uint8>((uint8*)&MovePacket, sizeof(MovePacket)));
    }
}

int32 UMMOGameInstance::GetPIEClientIndex() const
{
#if WITH_EDITOR
    // GPlayInEditorID is set by Unreal for PIE clients (0 for first, 1 for second, ...)
    return GPlayInEditorID;
#else
    return -1;
#endif
}

// No additional implementation needed for SetNetworkedEntityManager