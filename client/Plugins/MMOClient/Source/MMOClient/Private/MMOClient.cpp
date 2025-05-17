#include "MMOClient.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include <cstring>
#include <vector>
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#define UI UI_ST
THIRD_PARTY_INCLUDES_START
#include "openssl/evp.h"
THIRD_PARTY_INCLUDES_END
#undef UI
#include "Compression/lz4.h"
#include <Logging/LogMacros.h>

// Define a log category for MMOClient
DEFINE_LOG_CATEGORY_STATIC(LogMMOClient, Log, All);

// Fix Windows macro collision with SetPort
#ifdef SetPort
#undef SetPort
#endif

// --- Packet structs (must match server) ---
#include "Packets.h" // You should port or copy the struct definitions to a shared header for client and server

// --- Serialization helpers ---
template<typename T>
static void SerializeStruct(const T& packet, TArray<uint8>& out)
{
    out.SetNumUninitialized(sizeof(T));
    FMemory::Memcpy(out.GetData(), &packet, sizeof(T));
}

template<typename T>
static bool DeserializeStruct(const TArray<uint8>& in, T& out)
{
    if (in.Num() < sizeof(T)) return false;
    FMemory::Memcpy(&out, in.GetData(), sizeof(T));
    return true;
}

// --- AES-256-CBC encryption/decryption (OpenSSL) ---
static bool AesEncrypt(const TArray<uint8>& In, TArray<uint8>& Out, const std::vector<uint8_t>& Key)
{
    if (Key.size() != 32) return false;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    unsigned char iv[32] = {0};
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, Key.data(), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    Out.SetNum(In.Num() + EVP_CIPHER_block_size(EVP_aes_256_cbc()));
    int outlen1 = 0, outlen2 = 0;
    if (EVP_EncryptUpdate(ctx, Out.GetData(), &outlen1, In.GetData(), (int)In.Num()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    if (EVP_EncryptFinal_ex(ctx, Out.GetData() + outlen1, &outlen2) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    Out.SetNum(outlen1 + outlen2);
    EVP_CIPHER_CTX_free(ctx);
    return true;
}

static bool AesDecrypt(const TArray<uint8>& In, TArray<uint8>& Out, const std::vector<uint8_t>& Key)
{
    if (Key.size() != 32) return false;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    unsigned char iv[32] = {0};
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, Key.data(), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    Out.SetNum(In.Num());
    int outlen1 = 0, outlen2 = 0;
    if (EVP_DecryptUpdate(ctx, Out.GetData(), &outlen1, In.GetData(), (int)In.Num()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    if (EVP_DecryptFinal_ex(ctx, Out.GetData() + outlen1, &outlen2) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    Out.SetNum(outlen1 + outlen2);
    EVP_CIPHER_CTX_free(ctx);
    return true;
}

// --- LZ4 compression/decompression using Unreal API ---
static bool CompressLZ4(const TArray<uint8>& In, TArray<uint8>& Out)
{
    if (In.Num() == 0) return false;
    int32 CompressedSize = FCompression::CompressMemoryBound(NAME_LZ4, In.Num());
    TArray<uint8> Compressed;
    Compressed.SetNumUninitialized(CompressedSize);
    if (!FCompression::CompressMemory(NAME_LZ4, Compressed.GetData(), CompressedSize, In.GetData(), In.Num()))
        return false;
    Compressed.SetNum(CompressedSize);
    // Prepend original size in network byte order
    Out.SetNumUninitialized(4 + CompressedSize);
    uint32 OrigSize = htonl((uint32)In.Num());
    FMemory::Memcpy(Out.GetData(), &OrigSize, 4);
    FMemory::Memcpy(Out.GetData() + 4, Compressed.GetData(), CompressedSize);
    return true;
}

static bool DecompressLZ4(const TArray<uint8>& In, TArray<uint8>& Out, int32 /*UncompressedSize*/)
{
    if (In.Num() < 4) return false;
    uint32 OrigSize = 0;
    FMemory::Memcpy(&OrigSize, In.GetData(), 4);
    OrigSize = ntohl(OrigSize);
    Out.SetNumUninitialized(OrigSize);
    if (!FCompression::UncompressMemory(NAME_LZ4, Out.GetData(), OrigSize, In.GetData() + 4, In.Num() - 4))
        return false;
    return true;
}

// --- MMOClient implementation ---
void UMMOClient::ConnectAuth(const FString& Host, int32 Port)
{
    UE_LOG(LogMMOClient, Log, TEXT("Connecting to Auth server: %s:%d"), *Host, Port);
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    AuthSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("AuthSocket"), false));
    TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
    bool bIsValid = false;
    Addr->SetIp(*Host, bIsValid);
    Addr->SetPort(Port);
    if (AuthSocket->Connect(*Addr))
    {
        UE_LOG(LogMMOClient, Log, TEXT("Auth connection established."));
        if (UWorld* World = GetWorld()) {
            World->GetTimerManager().SetTimer(AuthRecvHandle, [this]() { OnReceive(AuthSocket, TEXT("Auth")); }, 0.01f, true);
            UE_LOG(LogMMOClient, Log, TEXT("Auth receive timer set!"));
        } else {
            UE_LOG(LogMMOClient, Error, TEXT("GetWorld() returned nullptr, timer not set!"));
        }
    }
    else {
        UE_LOG(LogMMOClient, Error, TEXT("Failed to connect to Auth server."));
        AuthSocket.Reset();
    }
}

void UMMOClient::ConnectGame(const FString& Host, int32 Port)
{
    UE_LOG(LogMMOClient, Log, TEXT("Connecting to Game server: %s:%d"), *Host, Port);
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    GameSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("GameSocket"), false));
    TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
    bool bIsValid = false;
    Addr->SetIp(*Host, bIsValid);
    Addr->SetPort(Port);
    if (GameSocket->Connect(*Addr))
    {
        UE_LOG(LogMMOClient, Log, TEXT("Game connection established."));
        if (UWorld* World = GetWorld()) {
            World->GetTimerManager().SetTimer(GameRecvHandle, [this]() { OnReceive(GameSocket, TEXT("Game")); }, 0.01f, true);
            UE_LOG(LogMMOClient, Log, TEXT("Game receive timer set!"));
        } else {
            UE_LOG(LogMMOClient, Error, TEXT("GetWorld() returned nullptr, timer not set!"));
        }
    }
    else {
        UE_LOG(LogMMOClient, Error, TEXT("Failed to connect to Game server."));
        GameSocket.Reset();
    }
}

void UMMOClient::ConnectChat(const FString& Host, int32 Port)
{
    UE_LOG(LogMMOClient, Log, TEXT("Connecting to Chat server: %s:%d"), *Host, Port);
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    ChatSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("ChatSocket"), false));
    TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
    bool bIsValid = false;
    Addr->SetIp(*Host, bIsValid);
    Addr->SetPort(Port);
    if (ChatSocket->Connect(*Addr))
    {
        UE_LOG(LogMMOClient, Log, TEXT("Chat connection established."));
        if (UWorld* World = GetWorld()) {
            World->GetTimerManager().SetTimer(ChatRecvHandle, [this]() { OnReceive(ChatSocket, TEXT("Chat")); }, 0.01f, true);
            UE_LOG(LogMMOClient, Log, TEXT("Chat receive timer set!"));
        } else {
            UE_LOG(LogMMOClient, Error, TEXT("GetWorld() returned nullptr, timer not set!"));
        }
    }
    else {
        UE_LOG(LogMMOClient, Error, TEXT("Failed to connect to Chat server."));
        ChatSocket.Reset();
    }
}

void UMMOClient::SendToAuth(const TArray<uint8>& Data)
{
    UE_LOG(LogMMOClient, Verbose, TEXT("Sending data to Auth server. Size: %d bytes"), Data.Num());
    TArray<uint8> Out;
    if (EncryptAndCompress(Data, Out) && AuthSocket.IsValid()) {
        int32 Sent = 0; AuthSocket->Send(Out.GetData(), Out.Num(), Sent);
    }
}
void UMMOClient::SendToGame(const TArray<uint8>& Data)
{
    UE_LOG(LogMMOClient, Verbose, TEXT("Sending data to Game server. Size: %d bytes"), Data.Num());
    TArray<uint8> Out;
    if (EncryptAndCompress(Data, Out) && GameSocket.IsValid()) {
        int32 Sent = 0; GameSocket->Send(Out.GetData(), Out.Num(), Sent);
    }
}
void UMMOClient::SendToChat(const TArray<uint8>& Data)
{
    UE_LOG(LogMMOClient, Verbose, TEXT("Sending data to Chat server. Size: %d bytes"), Data.Num());
    TArray<uint8> Out;
    if (EncryptAndCompress(Data, Out) && ChatSocket.IsValid()) {
        int32 Sent = 0; ChatSocket->Send(Out.GetData(), Out.Num(), Sent);
    }
}

void UMMOClient::DisconnectAll()
{
    UE_LOG(LogMMOClient, Log, TEXT("Disconnecting all sockets."));
    if (AuthSocket.IsValid()) { AuthSocket->Close(); AuthSocket.Reset(); }
    if (GameSocket.IsValid()) { GameSocket->Close(); GameSocket.Reset(); }
    if (ChatSocket.IsValid()) { ChatSocket->Close(); ChatSocket.Reset(); }
    if (GWorld)
    {
        GWorld->GetTimerManager().ClearTimer(AuthRecvHandle);
        GWorld->GetTimerManager().ClearTimer(GameRecvHandle);
        GWorld->GetTimerManager().ClearTimer(ChatRecvHandle);
    }
}

void UMMOClient::OnReceive(TSharedPtr<FSocket> Socket, FString ServerType)
{
     UE_LOG(LogMMOClient, Verbose, TEXT("OnReceive called for %s"), *ServerType);
    if (!Socket.IsValid()) return;
     UE_LOG(LogMMOClient, Verbose, TEXT("OnReceive Socket VALID"));
    uint32 PendingDataSize = 0;
    while (Socket->HasPendingData(PendingDataSize))
    {
        TArray<uint8> Data; Data.SetNumUninitialized(PendingDataSize);
        int32 BytesRead = 0;
        Socket->Recv(Data.GetData(), Data.Num(), BytesRead);
        UE_LOG(LogMMOClient, Verbose, TEXT("Received %d bytes from %s server."), BytesRead, *ServerType);
        if (BytesRead > 0)
        {
            TArray<uint8> Plain;
            if (DecryptAndDecompress(Data, Plain))
            {
                UE_LOG(LogMMOClient, Verbose, TEXT("Decrypted and decompressed packet from %s server. Size: %d bytes"), *ServerType, Plain.Num());
                if (ServerType == TEXT("Auth")) HandleAuthPacket(Plain);
                else if (ServerType == TEXT("Game")) HandleGamePacket(Plain);
                else if (ServerType == TEXT("Chat")) HandleChatPacket(Plain);
            }
            else {
                UE_LOG(LogMMOClient, Warning, TEXT("Failed to decrypt/decompress packet from %s server."), *ServerType);
            }
        }
    }
}

void UMMOClient::HandleAuthPacket(const TArray<uint8>& Data)
{
    if (Data.Num() < sizeof(PacketHeader)) return;
    PacketHeader header;
    FMemory::Memcpy(&header, Data.GetData(), sizeof(PacketHeader));
    UE_LOG(LogMMOClient, Verbose, TEXT("Handling Auth packet. PacketId: %d"), header.packetId);
    switch (header.packetId) {
        case PACKET_S_LOGIN_RESPONSE: {
            S_LoginResponse resp;
            if (DeserializeStruct(Data, resp)) {
                bool bSuccess = resp.resultCode == 0;
                UE_LOG(LogMMOClient, Log, TEXT("Login response received. Success: %s"), bSuccess ? TEXT("true") : TEXT("false"));
                // Store sessionKey in GameInstance
                if (GWorld) {
                    UGameInstance* GameInstance = GWorld->GetGameInstance();
                    if (GameInstance) {
                        FString SessionKeyStr = UTF8_TO_TCHAR(resp.sessionKey);
                        class UMMOGameInstance* MMOGameInstance = Cast<UMMOGameInstance>(GameInstance);
                        if (MMOGameInstance) {
                            MMOGameInstance->SetSessionKey(SessionKeyStr);
                        }
                    }
                }
                OnLoginResult.Broadcast(bSuccess); // <--- Notify listeners
            }
            break;
        }
        case PACKET_S_CHAR_SELECT_RESULT: {
            S_CharSelectResult resp;
            if (DeserializeStruct(Data, resp) && resp.resultCode == 0) {
                UE_LOG(LogMMOClient, Log, TEXT("Character select result: Success. Connecting to game server %s:%d"), UTF8_TO_TCHAR(resp.gameServerAddress), resp.gameServerPort);
                // Connect to game server using resp.gameServerAddress and resp.gameServerPort
                ConnectGame(UTF8_TO_TCHAR(resp.gameServerAddress), resp.gameServerPort);
                OnCharSelectSuccess.Broadcast();
            }
            break;
        }
        // ...handle other auth packets as needed...
        default:
            UE_LOG(LogMMOClient, Warning, TEXT("Unknown Auth packetId: %d"), header.packetId);
            break;
    }
}

void UMMOClient::HandleGamePacket(const TArray<uint8>& Data)
{
    if (Data.Num() < sizeof(PacketHeader)) return;
    PacketHeader header;
    FMemory::Memcpy(&header, Data.GetData(), sizeof(PacketHeader));
    UE_LOG(LogMMOClient, Verbose, TEXT("Handling Game packet. PacketId: %d"), header.packetId);
    switch (header.packetId) {
        case PACKET_S_MOVE: {
            S_Move move;
            if (DeserializeStruct(Data, move)) {
                UE_LOG(LogMMOClient, Verbose, TEXT("Move packet received."));
                // Handle move packet
            }
            break;
        }
        // ...handle other game packets as needed...
        default:
            UE_LOG(LogMMOClient, Warning, TEXT("Unknown Game packetId: %d"), header.packetId);
            break;
    }
}

void UMMOClient::HandleChatPacket(const TArray<uint8>& Data)
{
    if (Data.Num() < sizeof(PacketHeader)) return;
    PacketHeader header;
    FMemory::Memcpy(&header, Data.GetData(), sizeof(PacketHeader));
    UE_LOG(LogMMOClient, Verbose, TEXT("Handling Chat packet. PacketId: %d"), header.packetId);
    switch (header.packetId) {
        case PACKET_S_CHAT_MESSAGE: {
            S_ChatMessage msg;
            if (DeserializeStruct(Data, msg)) {
                UE_LOG(LogMMOClient, Log, TEXT("Chat message received."));
                // Handle chat message
            }
            break;
        }
        // ...handle other chat packets as needed...
        default:
            UE_LOG(LogMMOClient, Warning, TEXT("Unknown Chat packetId: %d"), header.packetId);
            break;
    }
}

bool UMMOClient::EncryptAndCompress(const TArray<uint8>& In, TArray<uint8>& Out)
{
    TArray<uint8> Compressed;
    if (!CompressLZ4(In, Compressed)) {
        UE_LOG(LogMMOClient, Error, TEXT("Compression failed."));
        return false;
    }
    if (!AesEncrypt(Compressed, Out, PACKET_CRYPTO_KEY)) {
        UE_LOG(LogMMOClient, Error, TEXT("Encryption failed."));
        return false;
    }
    return true;
}

bool UMMOClient::DecryptAndDecompress(const TArray<uint8>& In, TArray<uint8>& Out)
{
    TArray<uint8> Decrypted;
    if (!AesDecrypt(In, Decrypted, PACKET_CRYPTO_KEY)) {
        UE_LOG(LogMMOClient, Error, TEXT("Decryption failed."));
        return false;
    }
    if (!DecompressLZ4(Decrypted, Out, In.Num())) {
        UE_LOG(LogMMOClient, Error, TEXT("Decompression failed."));
        return false;
    }
    return true;
}

void UMMOClient::Shutdown()
{
    UE_LOG(LogMMOClient, Log, TEXT("MMOClient shutting down. Disconnecting all sockets and cleaning up resources."));
    DisconnectAll();
    // Nullify socket handles and clear timers for safety
    AuthSocket.Reset();
    GameSocket.Reset();
    ChatSocket.Reset();
    if (GWorld)
    {
        GWorld->GetTimerManager().ClearTimer(AuthRecvHandle);
        GWorld->GetTimerManager().ClearTimer(GameRecvHandle);
        GWorld->GetTimerManager().ClearTimer(ChatRecvHandle);
    }
    // If you have any additional resources (threads, buffers, etc.), clean them up here
}
