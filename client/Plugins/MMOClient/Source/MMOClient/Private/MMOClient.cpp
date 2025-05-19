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
#include "MMOClientDelegates.h"

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

// --- Update Connect/Disconnect logic ---
void UMMOClient::ConnectAuth(const FString& Host, int32 Port)
{
    LastAuthHost = Host;
    LastAuthPort = Port;
    UE_LOG(LogMMOClient, Log, TEXT("Connecting to Auth server: %s:%d"), *Host, Port);
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    AuthSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("AuthSocket"), false));
    AuthSocket->SetNonBlocking(true); // Ensure non-blocking mode for safe disconnect detection
    TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
    bool bIsValid = false;
    Addr->SetIp(*Host, bIsValid);
    Addr->SetPort(Port);
    SetClientState(EMMOClientState::CONNECTING_AUTH);
    DisconnectGame(); // Disconnect Game socket if connected
    UWorld* World = GetWorld();
    if (!World) {
        UE_LOG(LogMMOClient, Error, TEXT("GetWorld() returned nullptr!"));
        return;
    }
    if (AuthSocket->Connect(*Addr))
    {
        if (World) {
            World->GetTimerManager().SetTimer(AuthRecvHandle, [this]() { OnReceive(AuthSocket, TEXT("Auth")); }, 0.01f, true);
            UE_LOG(LogMMOClient, Log, TEXT("Auth receive timer set!"));
        } else {
            UE_LOG(LogMMOClient, Error, TEXT("GetWorld() returned nullptr, timer not set!"));
        }
    }
    else {
        UE_LOG(LogMMOClient, Error, TEXT("Failed to connect to Auth server."));
        AuthSocket.Reset();
        OnAuthDisconnected.Broadcast();
        //SetClientState(EMMOClientState::DISCONNECTED);
    }
    if (!World->GetTimerManager().IsTimerActive(TickTimerHandle))
    {
        World->GetTimerManager().SetTimer(TickTimerHandle, this, &UMMOClient::TickSockets, 1.0f, true);
        UE_LOG(LogMMOClient, Log, TEXT("TickTimer started successfully."));
    }
}

void UMMOClient::ConnectGame(const FString& Host, int32 Port)
{
    LastGameHost = Host;
    LastGamePort = Port;
    UE_LOG(LogMMOClient, Log, TEXT("Connecting to Game server: %s:%d"), *Host, Port);
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    GameSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("GameSocket"), false));
    GameSocket->SetNonBlocking(true); // Ensure non-blocking mode for safe disconnect detection
    TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
    bool bIsValid = false;
    Addr->SetIp(*Host, bIsValid);
    Addr->SetPort(Port);
    SetClientState(EMMOClientState::CONNECTING_GAME);
    DisconnectAuth(); // Disconnect Auth socket if connected
    if (GameSocket->Connect(*Addr))
    {
       
        if (UWorld* World = GetWorld()) {
            SetClientState(EMMOClientState::GAME);
            World->GetTimerManager().SetTimer(GameRecvHandle, [this]() { OnReceive(GameSocket, TEXT("Game")); }, 0.01f, true);
            // Send C_ConnectRequest with session key
            C_ConnectRequest connectReq;
            FMemory::Memzero(&connectReq, sizeof(connectReq));
            connectReq.header.packetId = PACKET_C_CONNECT_REQUEST;
            FTCHARToUTF8 sessionKeyUtf8(*GetSessionKey());
            FCStringAnsi::Strncpy(connectReq.sessionKey, sessionKeyUtf8.Get(), sizeof(connectReq.sessionKey) - 1);
            TArray<uint8> connectReqData;
            SerializeStruct(connectReq, connectReqData);
            SendToGame(connectReqData);
            StartHeartbeatForSocket(TEXT("Game"));
            UE_LOG(LogMMOClient, Log, TEXT("Game connection established."));
            OnGameConnected.Broadcast();
        } else {
            UE_LOG(LogMMOClient, Error, TEXT("GetWorld() returned nullptr, timer not set!"));
        }
    }
    else {
        // If the connection fails, reset the socket and notify the disconnection
        // This is important to avoid dangling pointers and ensure proper cleanup
        // before attempting to reconnect or handle the error
        UE_LOG(LogMMOClient, Error, TEXT("Failed to connect to Game server."));
        GameSocket.Reset();
        OnGameDisconnected.Broadcast();
        SetClientState(EMMOClientState::CONNECTING_AUTH);
    }
}

void UMMOClient::ConnectChat(const FString& Host, int32 Port)
{
    UE_LOG(LogMMOClient, Log, TEXT("Connecting to Chat server: %s:%d"), *Host, Port);
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    ChatSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("ChatSocket"), false));
    ChatSocket->SetNonBlocking(true); // Ensure non-blocking mode for safe disconnect detection
    TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
    bool bIsValid = false;
    Addr->SetIp(*Host, bIsValid);
    Addr->SetPort(Port);

    if (ChatSocket->Connect(*Addr))
    {
        UE_LOG(LogMMOClient, Log, TEXT("Chat connection established."));
        OnChatConnected.Broadcast();
        if (UWorld* World = GetWorld()) {
            World->GetTimerManager().SetTimer(ChatRecvHandle, [this]() { OnReceive(ChatSocket, TEXT("Chat")); }, 0.01f, true);
            StartHeartbeatForSocket(TEXT("Chat"));
            UE_LOG(LogMMOClient, Log, TEXT("Chat receive timer set!"));
        } else {
            UE_LOG(LogMMOClient, Error, TEXT("GetWorld() returned nullptr, timer not set!"));
        }
    }
    else {
        UE_LOG(LogMMOClient, Error, TEXT("Failed to connect to Chat server."));
        ChatSocket.Reset();
        OnChatDisconnected.Broadcast();
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
    StopHeartbeatForSocket(TEXT("Auth"));
    StopHeartbeatForSocket(TEXT("Game"));
    StopHeartbeatForSocket(TEXT("Chat"));
    UE_LOG(LogMMOClient, Log, TEXT("Disconnecting all sockets."));
    StopHeartbeat();
    if (AuthSocket.IsValid()) { AuthSocket->Close(); AuthSocket.Reset(); OnAuthDisconnected.Broadcast(); }
    if (GameSocket.IsValid()) { GameSocket->Close(); GameSocket.Reset(); OnGameDisconnected.Broadcast(); }
    if (ChatSocket.IsValid()) { ChatSocket->Close(); ChatSocket.Reset(); OnChatDisconnected.Broadcast(); }
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(AuthRecvHandle);
        World->GetTimerManager().ClearTimer(GameRecvHandle);
        World->GetTimerManager().ClearTimer(ChatRecvHandle);
    }
}

void UMMOClient::DisconnectAuth()
{
    if (AuthSocket.IsValid()) {
        AuthSocket->Close();
        AuthSocket.Reset();
        OnAuthDisconnected.Broadcast();
        StopHeartbeatForSocket(TEXT("Auth"));
    }
}

void UMMOClient::DisconnectGame()
{
    if (GameSocket.IsValid()) {
        GameSocket->Close();
        GameSocket.Reset();
        OnGameDisconnected.Broadcast();
        StopHeartbeatForSocket(TEXT("Game"));
    }
}

void UMMOClient::DisconnectChat()
{
    if (ChatSocket.IsValid()) {
        ChatSocket->Close();
        ChatSocket.Reset();
        OnChatDisconnected.Broadcast();
        StopHeartbeatForSocket(TEXT("Chat"));
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

void UMMOClient::SetSessionKey(const FString& InKey)
{
    SessionKey = InKey;
}

FString UMMOClient::GetSessionKey() const
{
    return SessionKey;
}

void UMMOClient::HandleAuthPacket(const TArray<uint8>& Data)
{
    if (Data.Num() < sizeof(PacketHeader)) return;
    PacketHeader header;
    FMemory::Memcpy(&header, Data.GetData(), sizeof(PacketHeader));
    switch (header.packetId) {
        case PACKET_S_LOGIN_RESPONSE: {
            S_LoginResponse resp;
            if (DeserializeStruct(Data, resp)) {
                bool bSuccess = resp.resultCode == 0;
                UE_LOG(LogMMOClient, Log, TEXT("Login response received. Success: %s | MMOClient instance: %p"), bSuccess ? TEXT("true") : TEXT("false"), this);
                // Store sessionKey in MMOClient (not GameInstance)
                FString SessionKeyStr = UTF8_TO_TCHAR(resp.sessionKey);
                SetSessionKey(SessionKeyStr);
                OnLoginResult.Broadcast(bSuccess);
            }
            break;
        }
        case PACKET_S_CHAR_CREATE_RESULT: {
            S_CharCreateResult resp;
            if (DeserializeStruct(Data, resp)) {
                bool bSuccess = resp.resultCode == 0;
                OnCharCreateSuccess.Broadcast(bSuccess);
                if (bSuccess) {
                    UE_LOG(LogMMOClient, Log, TEXT("Character created successfully. CharId: %d"), resp.charId);
                } else {
                    UE_LOG(LogMMOClient, Warning, TEXT("Character creation failed."));
                }
            }
            break;
        }
        case PACKET_S_CHAR_DELETE_RESULT: {
            S_CharDeleteResult resp;
            if (DeserializeStruct(Data, resp)) {
                bool bSuccess = resp.resultCode == 0;
                OnCharDeleteSuccess.Broadcast(bSuccess);
                if (bSuccess) {
                    UE_LOG(LogMMOClient, Log, TEXT("Character deleted successfully. CharId: %d"), resp.charId);
                } else {
                    UE_LOG(LogMMOClient, Warning, TEXT("Character deletion failed."));
                }
            }
            break;
        }
        case PACKET_S_CHAR_SELECT_RESULT: {
            S_CharSelectResult resp;
            if (DeserializeStruct(Data, resp)) {
                bool bSuccess = resp.resultCode == 0;
                OnCharSelectSuccess.Broadcast(bSuccess);
                if (bSuccess) {
                    UE_LOG(LogMMOClient, Log, TEXT("Character select result: Success. Connecting to game server %s:%d"), UTF8_TO_TCHAR(resp.gameServerAddress), resp.gameServerPort);
                    // Connect to game server using resp.gameServerAddress and resp.gameServerPort
                    ConnectGame(UTF8_TO_TCHAR(resp.gameServerAddress), resp.gameServerPort);
                } else {
                    // Handle failure case
                    UE_LOG(LogMMOClient, Warning, TEXT("Character selection failed."));
                }
            }
            break;
        }
        case PACKET_S_CHAR_LIST_RESULT: {
            S_CharListResult resp;
            if (DeserializeStruct(Data, resp)) {
                UE_LOG(LogMMOClient, Log, TEXT("Character list received. Count: %d"), resp.CharCount);
                TArray<FCharListEntry> CharList;
                for (int32 i = 0; i < resp.CharCount; ++i) {
                    FCharListEntry Entry;
                    Entry.CharId = resp.Entries[i].CharId;
                    Entry.Name = UTF8_TO_TCHAR(resp.Entries[i].Name);
                    Entry.ClassId = resp.Entries[i].ClassId;
                    CharList.Add(Entry);
                }
                OnCharListResult.Broadcast(CharList, resp.CharCount);
            }
            break;
        }
        case PACKET_S_HEARTBEAT: {
            S_Heartbeat resp;
            if (DeserializeStruct(Data, resp)) {
                AuthHeartbeat.LastReceivedTimestamp = FDateTime::UtcNow().ToUnixTimestamp();
                UE_LOG(LogMMOClient, Verbose, TEXT("Received S_Heartbeat from Auth. Timestamp: %d"), resp.timestamp);
                // Only now consider the connection established and start heartbeat
                if (CurrentState == EMMOClientState::CONNECTING_AUTH) {
                    SetClientState(EMMOClientState::AUTH);
                    StartHeartbeatForSocket(TEXT("Auth"));
                    UE_LOG(LogMMOClient, Log, TEXT("Auth connection established."));
                    OnAuthConnected.Broadcast();
                    
                }
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
    switch (header.packetId) {
        case PACKET_S_MOVE: {
            S_Move move;
            if (DeserializeStruct(Data, move)) {
                UE_LOG(LogMMOClient, Verbose, TEXT("Move packet received."));
                // Handle move packet
            }
            break;
        }
        case PACKET_S_CONNECT_RESULT: {
            S_ConnectResult resp;
            if (DeserializeStruct(Data, resp)) {
                bool bSuccess = resp.resultCode == 0;
                // If OnConnectResult is not declared, comment out the next line or implement it in the header
                // OnConnectResult.Broadcast(bSuccess);
                if (bSuccess) {
                    UE_LOG(LogMMOClient, Log, TEXT("Connected to game server successfully."));
                   
                    if(CurrentState == EMMOClientState::GAME) {
                        StartHeartbeatForSocket(TEXT("Game"));
                        UE_LOG(LogMMOClient, Log, TEXT("Game connection established."));
                        OnGameConnected.Broadcast();
                    }
                } else {
                    UE_LOG(LogMMOClient, Warning, TEXT("Game server connection failed."));
                }
            }
            break;
        }
        case PACKET_S_HEARTBEAT: {
            S_Heartbeat resp;
            if (DeserializeStruct(Data, resp)) {
                GameHeartbeat.LastReceivedTimestamp = FDateTime::UtcNow().ToUnixTimestamp();
                UE_LOG(LogMMOClient, Verbose, TEXT("Received S_Heartbeat from Game. Timestamp: %d"), resp.timestamp);
                // Only now consider the connection established and send a connect request
                if (CurrentState == EMMOClientState::CONNECTING_GAME) {
                    SetClientState(EMMOClientState::GAME);
                     // Send C_ConnectRequest with session key
                    C_ConnectRequest connectReq;
                    FMemory::Memzero(&connectReq, sizeof(connectReq));
                    connectReq.header.packetId = PACKET_C_CONNECT_REQUEST;
                    FTCHARToUTF8 sessionKeyUtf8(*GetSessionKey());
                    FCStringAnsi::Strncpy(connectReq.sessionKey, sessionKeyUtf8.Get(), sizeof(connectReq.sessionKey) - 1);
                    TArray<uint8> connectReqData;
                    SerializeStruct(connectReq, connectReqData);
                    SendToGame(connectReqData);
                }
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
    switch (header.packetId) {
        case PACKET_S_CHAT_MESSAGE: {
            S_ChatMessage msg;
            if (DeserializeStruct(Data, msg)) {
                UE_LOG(LogMMOClient, Log, TEXT("Chat message received."));
                // Handle chat message
            }
            break;
        }
        case PACKET_S_HEARTBEAT: {
            S_Heartbeat resp;
            if (DeserializeStruct(Data, resp)) {
                ChatHeartbeat.LastReceivedTimestamp = FDateTime::UtcNow().ToUnixTimestamp();
                UE_LOG(LogMMOClient, Verbose, TEXT("Received S_Heartbeat from Chat. Timestamp: %d"), resp.timestamp);
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
    TArray<uint8> Encrypted;
    if (!AesEncrypt(In, Encrypted, PACKET_CRYPTO_KEY)) {
        UE_LOG(LogMMOClient, Error, TEXT("Encryption failed."));
        return false;
    }
    if (!CompressLZ4(Encrypted, Out)) {
        UE_LOG(LogMMOClient, Error, TEXT("Compression failed."));
        return false;
    }
    return true;
}

bool UMMOClient::DecryptAndDecompress(const TArray<uint8>& In, TArray<uint8>& Out)
{
    TArray<uint8> Decompressed;
    if (!DecompressLZ4(In, Decompressed, In.Num())) {
        UE_LOG(LogMMOClient, Error, TEXT("Decompression failed."));
        return false;
    }
    if (!AesDecrypt(Decompressed, Out, PACKET_CRYPTO_KEY)) {
        UE_LOG(LogMMOClient, Error, TEXT("Decryption failed."));
        return false;
    }
    return true;
}

void UMMOClient::Shutdown()
{
    StopHeartbeatForSocket(TEXT("Auth"));
    StopHeartbeatForSocket(TEXT("Game"));
    StopHeartbeatForSocket(TEXT("Chat"));
    UE_LOG(LogMMOClient, Log, TEXT("MMOClient shutting down. Disconnecting all sockets and cleaning up resources."));
    DisconnectAll();
    // Nullify socket handles and clear timers for safety
    AuthSocket.Reset();
    GameSocket.Reset();
    ChatSocket.Reset();
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TickTimerHandle);
        World->GetTimerManager().ClearTimer(AuthRecvHandle);
        World->GetTimerManager().ClearTimer(GameRecvHandle);
        World->GetTimerManager().ClearTimer(ChatRecvHandle);
    }
    else
    {
        UE_LOG(LogMMOClient, Error, TEXT("GetWorld() returned nullptr, timers not cleared!"));
    }

    // If you have any additional resources (threads, buffers, etc.), clean them up here
}

void UMMOClient::SetClientState(EMMOClientState NewState)
{
    if (CurrentState != NewState)
    {
        CurrentState = NewState;
        OnClientStateChanged.Broadcast(NewState);
        UE_LOG(LogMMOClient, Log, TEXT("MMOClient state changed to: %s"), *UEnum::GetValueAsString(NewState));
    }
}
/*
    In Unreal Engine, there is no built-in event or callback for socket disconnection on the FSocket class itself—Unreal's sockets are low-level and do not provide asynchronous disconnect notifications
*/
void UMMOClient::TickSockets()
{   
    // Check AuthSocket
    switch (CurrentState) 
    {
        case EMMOClientState::CONNECTING_AUTH: // Reconnecting to Auth server
            if (
                (!AuthSocket.IsValid() || AuthSocket->GetConnectionState() != SCS_Connected) 
                && !LastAuthHost.IsEmpty() && LastAuthPort > 0
            ){
                UE_LOG(LogMMOClient, Warning, TEXT("Reconnecting to Auth server at %s:%d"), *LastAuthHost, LastAuthPort);
                ConnectAuth(LastAuthHost, LastAuthPort);
            }
            break;
        case EMMOClientState::AUTH: // Auth server connected
            CheckHeartbeatTimeout(TEXT("Auth"), AuthHeartbeat);
            break;
        case EMMOClientState::CONNECTING_GAME: // Reconnecting to Game server & Chat Server only try for 5 seconds
            if(gameRetryCount < 30)
            {
                gameRetryCount++;
                if (
                    (!GameSocket.IsValid() || GameSocket->GetConnectionState() != SCS_Connected )
                    && !LastGameHost.IsEmpty() && LastGamePort > 0
                ){
                    UE_LOG(LogMMOClient, Warning, TEXT("Reconnecting to Game server at %s:%d Retry: %d"), *LastGameHost, LastGamePort, gameRetryCount);
                    ConnectGame(LastGameHost, LastGamePort);
                }
            }
            else
            {
                gameRetryCount = 0;
                SetClientState(EMMOClientState::CONNECTING_AUTH);
                UE_LOG(LogMMOClient, Warning, TEXT("Game server connection failed after 30 attempts. Switching to AUTH state."));
            }
            break;
        case EMMOClientState::GAME: // Game server connected
            CheckHeartbeatTimeout(TEXT("Game"), GameHeartbeat);
            break;
        default:
            break;
    }

}

void UMMOClient::BeginDestroy()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TickTimerHandle);
    }
    Super::BeginDestroy();
}

void UMMOClient::StartHeartbeatForSocket(const FString& SocketType)
{
    UWorld* World = GetWorld();
    if (!World) return;
    FHeartbeatState* State = nullptr;
    if (SocketType == TEXT("Auth")) State = &AuthHeartbeat;
    else if (SocketType == TEXT("Game")) State = &GameHeartbeat;
    else if (SocketType == TEXT("Chat")) State = &ChatHeartbeat;
    if (!State) return;
    World->GetTimerManager().SetTimer(State->TimerHandle, [this, SocketType]() { SendHeartbeat(SocketType); }, HEARTBEAT_INTERVAL, true);
    State->LastReceivedTimestamp = FDateTime::UtcNow().ToUnixTimestamp();
    UE_LOG(LogMMOClient, Log, TEXT("%s Heartbeat timer started."), *SocketType);
}

void UMMOClient::StopHeartbeatForSocket(const FString& SocketType)
{
    UWorld* World = GetWorld();
    if (!World) return;
    FHeartbeatState* State = nullptr;
    if (SocketType == TEXT("Auth")) State = &AuthHeartbeat;
    else if (SocketType == TEXT("Game")) State = &GameHeartbeat;
    else if (SocketType == TEXT("Chat")) State = &ChatHeartbeat;
    if (!State) return;
    World->GetTimerManager().ClearTimer(State->TimerHandle);
    UE_LOG(LogMMOClient, Log, TEXT("%s Heartbeat timer stopped."), *SocketType);
}

void UMMOClient::StopHeartbeat()
{
    // If you want to stop all heartbeats at once, call StopHeartbeatForSocket for each
    StopHeartbeatForSocket(TEXT("Auth"));
    StopHeartbeatForSocket(TEXT("Game"));
    StopHeartbeatForSocket(TEXT("Chat"));
}

void UMMOClient::SendHeartbeat(const FString& SocketType)
{
    int32 Now = FDateTime::UtcNow().ToUnixTimestamp();
    C_Heartbeat Packet;
    FMemory::Memzero(&Packet, sizeof(Packet));
    Packet.header.packetId = PACKET_C_HEARTBEAT;
    Packet.timestamp = Now;
    TArray<uint8> Data;
    SerializeStruct(Packet, Data);
    if (SocketType == TEXT("Auth") && AuthSocket.IsValid() && AuthSocket->GetConnectionState() == SCS_Connected) {
        SendToAuth(Data);
        CheckHeartbeatTimeout(SocketType, AuthHeartbeat);
    } else if (SocketType == TEXT("Game") && GameSocket.IsValid() && GameSocket->GetConnectionState() == SCS_Connected) {
        SendToGame(Data);
        CheckHeartbeatTimeout(SocketType, GameHeartbeat);
    } else if (SocketType == TEXT("Chat") && ChatSocket.IsValid() && ChatSocket->GetConnectionState() == SCS_Connected) {
        SendToChat(Data);
        CheckHeartbeatTimeout(SocketType, ChatHeartbeat);
    }
    UE_LOG(LogMMOClient, Verbose, TEXT("Heartbeat sent to %s server."), *SocketType);
}

void UMMOClient::CheckHeartbeatTimeout(const FString& SocketType, FHeartbeatState& State)
{
    int32 Now = FDateTime::UtcNow().ToUnixTimestamp();
    if (Now - State.LastReceivedTimestamp > HEARTBEAT_TIMEOUT) {
        UE_LOG(LogMMOClient, Error, TEXT("%s heartbeat timeout. Treating as disconnect."), *SocketType);
        if (SocketType == TEXT("Auth")) {
            if (AuthSocket.IsValid()) { AuthSocket->Close(); AuthSocket.Reset(); OnAuthDisconnected.Broadcast(); }
            StopHeartbeatForSocket(SocketType);
            SetClientState(EMMOClientState::CONNECTING_AUTH);
        } else if (SocketType == TEXT("Game")) {
            if (GameSocket.IsValid()) { GameSocket->Close(); GameSocket.Reset(); OnGameDisconnected.Broadcast(); }
            StopHeartbeatForSocket(SocketType);
            SetClientState(EMMOClientState::CONNECTING_GAME);
        } else if (SocketType == TEXT("Chat")) {
            if (ChatSocket.IsValid()) { ChatSocket->Close(); ChatSocket.Reset(); OnChatDisconnected.Broadcast(); }
            StopHeartbeatForSocket(SocketType);
        }
    }
}

// Static member definitions for linker
FHeartbeatState UMMOClient::AuthHeartbeat;
FHeartbeatState UMMOClient::GameHeartbeat;
FHeartbeatState UMMOClient::ChatHeartbeat;

