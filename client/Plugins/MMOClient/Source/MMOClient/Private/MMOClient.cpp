#include "MMOClient.h"
#include "MMOGameInstance.h" // Fix for UMMOGameInstance undefined
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
#include "MMOPlayerMovementComponent.h"

// --- UDP address storage ---
TSharedPtr<FInternetAddr> AuthServerAddr;
TSharedPtr<FInternetAddr> GameServerAddr;
TSharedPtr<FInternetAddr> ChatServerAddr;

// Define a log category for MMOClient
DEFINE_LOG_CATEGORY_STATIC(LogMMOClient, Log, All);

// Fix Windows macro collision with SetPort
#ifdef SetPort
#undef SetPort
#endif

// --- Packet structs (must match server) ---
#include "Packets.h" // You should port or copy the struct definitions to a shared header for client and server




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

static bool DecompressLZ4(const TArray<uint8>& In, TArray<uint8>& Out, int32 UncompressedSize)
{
    if (UncompressedSize <= 0) return false;
    Out.SetNumUninitialized(UncompressedSize);
    if (!FCompression::UncompressMemory(NAME_LZ4, Out.GetData(), UncompressedSize, In.GetData(), In.Num()))
        return false;
    return true;
}

// --- MMOClient implementation ---
// Protocol selection for socket connections
enum class EMMOClientIPVersion {
    Auto,
    IPv4,
    IPv6
};

EMMOClientIPVersion ClientIPVersion = EMMOClientIPVersion::Auto; // Set via config or code

// --- Update Connect/Disconnect logic ---
void UMMOClient::ConnectAuth(const FString& Host, int32 Port)
{
    LastAuthHost = Host;
    LastAuthPort = Port;
    UE_LOG(LogMMOClient, Log, TEXT("Connecting to Auth server (UDP): %s:%d"), *Host, Port);
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    bool bIsIPv6 = false;
    // Detect IPv6 by presence of ':' and absence of '.'
    if (ClientIPVersion == EMMOClientIPVersion::IPv6 || (ClientIPVersion == EMMOClientIPVersion::Auto && Host.Contains(":") && !Host.Contains("."))) {
        bIsIPv6 = true;
    }
    FName ProtocolType = bIsIPv6 ? FNetworkProtocolTypes::IPv6 : FNetworkProtocolTypes::IPv4;
    // Only create socket if not already valid
    if (!AuthSocket.IsValid()) {
        AuthSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_DGram, TEXT("AuthSocket"), ProtocolType));
        TSharedRef<FInternetAddr> LocalAddr = SocketSubsystem->CreateInternetAddr();
        LocalAddr->SetAnyAddress();
        LocalAddr->SetPort(0);
        bool bDidBind = AuthSocket->Bind(*LocalAddr);
        if (!bDidBind) {
            UE_LOG(LogMMOClient, Error, TEXT("Failed to bind AuthSocket to local address!"));
        }
        AuthSocket->SetNonBlocking(true);
        TSharedRef<FInternetAddr> BoundAddr = SocketSubsystem->CreateInternetAddr();
        AuthSocket->GetAddress(*BoundAddr);
        UE_LOG(LogMMOClient, Log, TEXT("AuthSocket bound to: %s"), *BoundAddr->ToString(true));
    }
    AuthServerAddr = SocketSubsystem->CreateInternetAddr();
    if (bIsIPv6) {
        bool bIsValid = false;
        AuthServerAddr->SetIp(*Host, bIsValid); // false = allow IPv6
        if (!bIsValid) {
            UE_LOG(LogMMOClient, Error, TEXT("Failed to parse IPv6 address: %s"), *Host);
            return;
        }
    } else {
        FIPv4Address OutIp;
        if (!FIPv4Address::Parse(Host, OutIp)) {
            UE_LOG(LogMMOClient, Error, TEXT("Failed to parse IPv4 address: %s"), *Host);
            return;
        }
        AuthServerAddr->SetIp(OutIp.Value);
    }
    AuthServerAddr->SetPort(Port);
    SetClientState(EMMOClientState::CONNECTING_AUTH);
    DisconnectGame();
    UWorld* World = GetWorld();
    if (!World) {
        UE_LOG(LogMMOClient, Error, TEXT("GetWorld() returned nullptr!"));
        return;
    }
    // No connect for UDP, just start receive timer
    World->GetTimerManager().SetTimer(AuthRecvHandle, [this]() { OnReceive(AuthSocket, TEXT("Auth")); }, 0.01f, true);
    UE_LOG(LogMMOClient, Log, TEXT("Auth receive timer set! (UDP)"));
    // Send C_ConnectRequest to Auth server immediately (UDP handshake)
    C_ConnectRequest connectReq;
    FMemory::Memzero(&connectReq, sizeof(connectReq));
    connectReq.header.packetId = PACKET_C_CONNECT_REQUEST;
    FTCHARToUTF8 sessionKeyUtf8(*GetSessionKey());
    FCStringAnsi::Strncpy(connectReq.sessionKey, sessionKeyUtf8.Get(), sizeof(connectReq.sessionKey) - 1);
    TArray<uint8> connectReqData;
    SerializeStruct(connectReq, connectReqData);
    SendToAuth(connectReqData);
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
    UE_LOG(LogMMOClient, Log, TEXT("Connecting to Game server (UDP): %s:%d"), *Host, Port);
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    bool bIsIPv6 = false;
    if (ClientIPVersion == EMMOClientIPVersion::IPv6 || (ClientIPVersion == EMMOClientIPVersion::Auto && Host.Contains(":") && !Host.Contains("."))) {
        bIsIPv6 = true;
    }
    FName ProtocolType = bIsIPv6 ? FNetworkProtocolTypes::IPv6 : FNetworkProtocolTypes::IPv4;
    TSharedRef<FInternetAddr> GameBoundAddr = SocketSubsystem->CreateInternetAddr();
    if (!GameSocket.IsValid()) {
        GameSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_DGram, TEXT("GameSocket"), ProtocolType));
        GameSocket->SetNonBlocking(true);
        GameSocket->GetAddress(*GameBoundAddr);
        UE_LOG(LogMMOClient, Log, TEXT("GameSocket bound to: %s"), *GameBoundAddr->ToString(true));
    }
    GameServerAddr = SocketSubsystem->CreateInternetAddr();
    if (bIsIPv6) {
        bool bIsValid = false;
        GameServerAddr->SetIp(*Host, bIsValid);
        if (!bIsValid) {
            UE_LOG(LogMMOClient, Error, TEXT("Failed to parse IPv6 address: %s"), *Host);
            return;
        }
    } else {
        FIPv4Address OutIp;
        if (!FIPv4Address::Parse(Host, OutIp)) {
            UE_LOG(LogMMOClient, Error, TEXT("Failed to parse IPv4 address: %s"), *Host);
            return;
        }
        GameServerAddr->SetIp(OutIp.Value);
    }
    GameServerAddr->SetPort(Port);
    SetClientState(EMMOClientState::CONNECTING_GAME);
    DisconnectAuth();
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
        UE_LOG(LogMMOClient, Log, TEXT("Game connection established (UDP)."));
        OnGameConnected.Broadcast();
    } else {
        UE_LOG(LogMMOClient, Error, TEXT("GetWorld() returned nullptr, timer not set!"));
    }
}

void UMMOClient::ConnectChat(const FString& Host, int32 Port)
{
    UE_LOG(LogMMOClient, Log, TEXT("Connecting to Chat server (UDP): %s:%d"), *Host, Port);
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    // Only create socket if not already valid
    if (!ChatSocket.IsValid()) {
        ChatSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_DGram, TEXT("ChatSocket"), FNetworkProtocolTypes::IPv4));
        ChatSocket->SetNonBlocking(true);
        // Log the local address and port after binding
        TSharedRef<FInternetAddr> ChatBoundAddr = SocketSubsystem->CreateInternetAddr();
        ChatSocket->GetAddress(*ChatBoundAddr);
        UE_LOG(LogMMOClient, Log, TEXT("ChatSocket bound to: %s"), *ChatBoundAddr->ToString(true));
    }
    if (UWorld* World = GetWorld()) {
        World->GetTimerManager().SetTimer(ChatRecvHandle, [this]() { OnReceive(ChatSocket, TEXT("Chat")); }, 0.01f, true);
        UE_LOG(LogMMOClient, Log, TEXT("Chat receive timer set! (UDP)"));
        OnChatConnected.Broadcast();
    } else {
        UE_LOG(LogMMOClient, Error, TEXT("GetWorld() returned nullptr, timer not set!"));
    }
}

void UMMOClient::SendToAuth(const TArray<uint8>& Data)
{
    UE_LOG(LogMMOClient, Verbose, TEXT("Sending data to Auth server. Size: %d bytes"), Data.Num());
    FString HexDump;
    for (int32 i = 0; i < Data.Num(); ++i) HexDump += FString::Printf(TEXT("%02x "), Data[i]);
    UE_LOG(LogMMOClient, Verbose, TEXT("[RAW OUT] Data: %s"), *HexDump);
    TArray<uint8> Out;
    if (EncryptAndCompress(Data, Out) && AuthSocket.IsValid() && AuthServerAddr.IsValid()) {
        uint32 PacketLen = Out.Num();
        uint32 NetPacketLen = htonl(PacketLen);
        TArray<uint8> SendBuf;
        SendBuf.SetNumUninitialized(4 + PacketLen);
        FMemory::Memcpy(SendBuf.GetData(), &NetPacketLen, 4);
        FMemory::Memcpy(SendBuf.GetData() + 4, Out.GetData(), PacketLen);
        FString HexDump2;
        for (int32 i = 0; i < SendBuf.Num(); ++i) HexDump2 += FString::Printf(TEXT("%02x "), SendBuf[i]);
        UE_LOG(LogMMOClient, Verbose, TEXT("[ENC+COMP+LEN OUT] Data: %s"), *HexDump2);
        int32 Sent = 0;
        bool bSendResult = AuthSocket->SendTo(SendBuf.GetData(), SendBuf.Num(), Sent, *AuthServerAddr);
        if (!bSendResult || Sent != SendBuf.Num()) {
            const TCHAR* SocketError = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError(ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode());
            UE_LOG(LogMMOClient, Error, TEXT("SendTo Auth failed! Sent=%d/%d, Error: %s"), Sent, SendBuf.Num(), SocketError ? SocketError : TEXT("Unknown"));
        } else {
            UE_LOG(LogMMOClient, Verbose, TEXT("SendTo Auth succeeded. Sent %d bytes."), Sent);
        }
    }

    // Log the remote address before sending
    UE_LOG(LogMMOClient, Log, TEXT("Sending to AuthServerAddr: %s"), *AuthServerAddr->ToString(true));
}

void UMMOClient::SendToGame(const TArray<uint8>& Data)
{
    UE_LOG(LogMMOClient, Verbose, TEXT("[RAW OUT] Sending data to Game server. Size: %d bytes"), Data.Num());
    FString HexDump;
    for (int32 i = 0; i < Data.Num(); ++i) HexDump += FString::Printf(TEXT("%02x "), Data[i]);
    UE_LOG(LogMMOClient, Verbose, TEXT("[RAW OUT] Data: %s"), *HexDump);
    TArray<uint8> Out;
    if (!GameSocket.IsValid() || !GameServerAddr.IsValid()) {
        UE_LOG(LogMMOClient, Warning, TEXT("SendToGame called but GameSocket or GameServerAddr is invalid! Data not sent."));
        return;
    }
    if (EncryptAndCompress(Data, Out)) {
        uint32 PacketLen = Out.Num();
        uint32 NetPacketLen = htonl(PacketLen);
        TArray<uint8> SendBuf;
        SendBuf.SetNumUninitialized(4 + PacketLen);
        FMemory::Memcpy(SendBuf.GetData(), &NetPacketLen, 4);
        FMemory::Memcpy(SendBuf.GetData() + 4, Out.GetData(), PacketLen);
        FString HexDump2;
        for (int32 i = 0; i < SendBuf.Num(); ++i) HexDump2 += FString::Printf(TEXT("%02x "), SendBuf[i]);
        UE_LOG(LogMMOClient, Verbose, TEXT("[ENC+COMP+LEN OUT] Data: %s"), *HexDump2);
        int32 Sent = 0;
        bool bSendResult = GameSocket->SendTo(SendBuf.GetData(), SendBuf.Num(), Sent, *GameServerAddr);
        if (!bSendResult || Sent != SendBuf.Num()) {
            const TCHAR* SocketError = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError(ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode());
            UE_LOG(LogMMOClient, Error, TEXT("SendTo Game failed! Sent=%d/%d, Error: %s"), Sent, SendBuf.Num(), SocketError ? SocketError : TEXT("Unknown"));
        } else {
            UE_LOG(LogMMOClient, Verbose, TEXT("SendTo Game succeeded. Sent %d bytes."), Sent);
        }
        UE_LOG(LogMMOClient, Verbose, TEXT("Sending to GameServerAddr: %s"), *GameServerAddr->ToString(true));
    }
}

void UMMOClient::SendToChat(const TArray<uint8>& Data)
{
    UE_LOG(LogMMOClient, Verbose, TEXT("Sending data to Chat server. Size: %d bytes"), Data.Num());
    TArray<uint8> Out;
    if (EncryptAndCompress(Data, Out) && ChatSocket.IsValid() && ChatServerAddr.IsValid()) {
        uint32 PacketLen = Out.Num();
        uint32 NetPacketLen = htonl(PacketLen);
        TArray<uint8> SendBuf;
        SendBuf.SetNumUninitialized(4 + PacketLen);
        FMemory::Memcpy(SendBuf.GetData(), &NetPacketLen, 4);
        FMemory::Memcpy(SendBuf.GetData() + 4, Out.GetData(), PacketLen);
        int32 Sent = 0;
        bool bSendResult = ChatSocket->SendTo(SendBuf.GetData(), SendBuf.Num(), Sent, *ChatServerAddr);
        if (!bSendResult || Sent != SendBuf.Num()) {
            const TCHAR* SocketError = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError(ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode());
            UE_LOG(LogMMOClient, Error, TEXT("SendTo Chat failed! Sent=%d/%d, Error: %s"), Sent, SendBuf.Num(), SocketError ? SocketError : TEXT("Unknown"));
        } else {
            UE_LOG(LogMMOClient, Verbose, TEXT("SendTo Chat succeeded. Sent %d bytes."), Sent);
        }
    }

    // Log the remote address before sending
    UE_LOG(LogMMOClient, Log, TEXT("Sending to ChatServerAddr: %s"), *ChatServerAddr->ToString(true));
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
    static TMap<FString, TArray<uint8>> ReceiveBuffers; // One buffer per server type
    //UE_LOG(LogMMOClient, Verbose, TEXT("OnReceive called for %s"), *ServerType);
    if (!Socket.IsValid()) return;
    //UE_LOG(LogMMOClient, Verbose, TEXT("OnReceive Socket VALID"));
    uint32 PendingDataSize = 0;
    TArray<uint8>& Buffer = ReceiveBuffers.FindOrAdd(ServerType);
    TSharedRef<FInternetAddr> Sender = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
    while (Socket->HasPendingData(PendingDataSize))
    {
        TArray<uint8> Data; Data.SetNumUninitialized(PendingDataSize);
        int32 BytesRead = 0;
        Socket->RecvFrom(Data.GetData(), Data.Num(), BytesRead, *Sender);
        UE_LOG(LogMMOClient, Verbose, TEXT("[RAW IN] Received %d bytes from %s server (UDP)."), BytesRead, *ServerType);
        FString HexDump;
        for (int32 i = 0; i < Data.Num(); ++i) HexDump += FString::Printf(TEXT("%02x "), Data[i]);
        UE_LOG(LogMMOClient, Verbose, TEXT("[RAW IN] Data: %s"), *HexDump);
        if (BytesRead > 0)
        {
            int32 OldNum = Buffer.Num();
            Buffer.SetNum(OldNum + BytesRead);
            FMemory::Memcpy(Buffer.GetData() + OldNum, Data.GetData(), BytesRead);
        }
    }
    // Process all complete packets in the buffer
    int32 Offset = 0;
    while (Buffer.Num() - Offset >= 4) {
        uint32 PacketLen = 0;
        FMemory::Memcpy(&PacketLen, Buffer.GetData() + Offset, 4);
        PacketLen = ntohl(PacketLen);
        // Defensive: check for absurdly large or negative PacketLen
        if (PacketLen <= 0 || PacketLen > 65536) {
            UE_LOG(LogMMOClient, Error, TEXT("[FATAL] Invalid UDP packet length: %u from %s server. Dropping buffer."), PacketLen, *ServerType);
            Buffer.SetNum(0);
            break;
        }
        if (Buffer.Num() - Offset < 4 + (int32)PacketLen)
            break; // Not enough data for a full packet
        // Extract one packet (including length prefix)
        // --- FIX: For UDP, do not copy to OnePacket, just use Buffer directly ---
        // Remove length prefix for processing
        TArray<uint8> Payload;
        Payload.SetNumUninitialized(PacketLen);
        FMemory::Memcpy(Payload.GetData(), Buffer.GetData() + Offset + 4, PacketLen);
        // Defensive: check for LZ4 uncompressed size prefix
        if (Payload.Num() < 4) {
            UE_LOG(LogMMOClient, Error, TEXT("[DECOMPRESS FAIL] Packet too short for LZ4 size prefix from %s server."), *ServerType);
            Offset += 4 + PacketLen;
            continue;
        }
        uint32 UncompressedSize = 0;
        FMemory::Memcpy(&UncompressedSize, Payload.GetData(), 4);
        UncompressedSize = ntohl(UncompressedSize);
        // Defensive: check for absurdly large or negative UncompressedSize
        if (UncompressedSize <= 0 || UncompressedSize > 65536) {
            UE_LOG(LogMMOClient, Error, TEXT("[FATAL] Invalid LZ4 uncompressed size: %u from %s server. Dropping buffer."), UncompressedSize, *ServerType);
            Buffer.SetNum(0);
            break;
        }
        TArray<uint8> CompressedData;
        CompressedData.SetNumUninitialized(Payload.Num() - 4);
        FMemory::Memcpy(CompressedData.GetData(), Payload.GetData() + 4, Payload.Num() - 4);
        TArray<uint8> Decompressed;
        if (!DecompressLZ4(CompressedData, Decompressed, UncompressedSize)) {
            UE_LOG(LogMMOClient, Error, TEXT("[DECOMPRESS FAIL] from %s server."), *ServerType);
            Offset += 4 + PacketLen;
            continue;
        }
        FString HexDumpDecomp;
        for (int32 i = 0; i < Decompressed.Num(); ++i) HexDumpDecomp += FString::Printf(TEXT("%02x "), Decompressed[i]);
        UE_LOG(LogMMOClient, Verbose, TEXT("[DECOMP OUT] Data: %s"), *HexDumpDecomp);
        TArray<uint8> Plain;
        if (!AesDecrypt(Decompressed, Plain, PACKET_CRYPTO_KEY)) {
            UE_LOG(LogMMOClient, Error, TEXT("[DECRYPT FAIL] from %s server."), *ServerType);
            Offset += 4 + PacketLen; // Skip this packet
            continue;
        }
        FString HexDumpPlain;
        for (int32 i = 0; i < Plain.Num(); ++i) HexDumpPlain += FString::Printf(TEXT("%02x "), Plain[i]);
        UE_LOG(LogMMOClient, Verbose, TEXT("[DECRYPT OUT] Data: %s"), *HexDumpPlain);
        UE_LOG(LogMMOClient, Verbose, TEXT("Decrypted and decompressed packet from %s server. Size: %d bytes"), *ServerType, Plain.Num());
        // Log the packet type
        if (Plain.Num() < sizeof(PacketHeader)) {
            UE_LOG(LogMMOClient, Error, TEXT("[FATAL] Packet too small after decryption from %s server. Dropping buffer."), *ServerType);
            Buffer.SetNum(0);
            break;
        }
        PacketHeader header;
        FMemory::Memcpy(&header, Plain.GetData(), sizeof(PacketHeader));
        const char* PacketName = PacketTypeToString(header.packetId);
        UE_LOG(LogMMOClient, Verbose, TEXT("Received packet from %s server. packetId: %d (%hs), Size: %d bytes"), *ServerType, header.packetId, PacketName, Plain.Num());

        if (ServerType == TEXT("Auth")) HandleAuthPacket(Plain);
        else if (ServerType == TEXT("Game")) HandleGamePacket(Plain);
        else if (ServerType == TEXT("Chat")) HandleChatPacket(Plain);
        Offset += 4 + PacketLen;
    }
    // Remove processed bytes
    if (Offset > 0)
        Buffer.RemoveAt(0, Offset, EAllowShrinking::No);
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
            if (Data.Num() < sizeof(S_CharListResult)) break;
            const S_CharListResult* resp = reinterpret_cast<const S_CharListResult*>(Data.GetData());
            int32 CharCount = resp->CharCount;
            int32 expectedSize = sizeof(S_CharListResult) + CharCount * sizeof(CharListEntry);
            if (Data.Num() < expectedSize) {
                UE_LOG(LogMMOClient, Error, TEXT("S_CharListResult: Packet too small for %d entries (got %d bytes, expected %d)"), CharCount, Data.Num(), expectedSize);
                break;
            }
            UE_LOG(LogMMOClient, Log, TEXT("Character list received. Count: %d"), CharCount);
            TArray<FCharListEntry> CharList;
            const CharListEntry* entries = reinterpret_cast<const CharListEntry*>(Data.GetData() + sizeof(S_CharListResult));
            for (int32 i = 0; i < CharCount; ++i) {
                FCharListEntry Entry;
                Entry.CharId = entries[i].CharId;
                Entry.Name = UTF8_TO_TCHAR(entries[i].Name);
                Entry.ClassId = entries[i].ClassId;
                CharList.Add(Entry);
            }
            OnCharListResult.Broadcast(CharList, CharCount);
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
    // Exclude move and heartbeat packets from log
    if (header.packetId != PACKET_S_MOVE && header.packetId != PACKET_S_HEARTBEAT && header.packetId != PACKET_C_MOVE && header.packetId != PACKET_C_HEARTBEAT) {
        UE_LOG(LogMMOClient, Warning, TEXT("[MMOClient] HandleGamePacket: Received packetId=%d (%hs), Size=%d bytes"),
            header.packetId, PacketTypeToString(header.packetId), Data.Num());
    }
    switch (header.packetId) {
        case PACKET_S_MOVE: {
            S_Move move;
            if (DeserializeStruct(Data, move)) {
                UE_LOG(LogMMOClient, Log, TEXT("Move packet received. EntityId: %d, Location: (%f, %f, %f)"), move.entityId, move.x, move.y, move.z);
                UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
                UMMOGameInstance* MMOGameInstance = Cast<UMMOGameInstance>(GameInstance);
                if(MMOGameInstance && MMOGameInstance->NetworkedEntityManager) {
                    if(MMOGameInstance && move.entityId != MMOGameInstance->SelectedCharacterId){
                        MMOGameInstance->NetworkedEntityManager->HandleEntityMovePacket(move);
                    }
                } else if (!MMOGameInstance) {
                    UE_LOG(LogMMOClient, Error, TEXT("Failed to cast GameInstance to MMOGameInstance!"));
                } else {
                    UE_LOG(LogMMOClient, Error, TEXT("NetworkedEntityManager is null!"));
                }
                // Only broadcast for local player
                if (MMOGameInstance && move.entityId == MMOGameInstance->SelectedCharacterId) {
                    FVector ConfirmedLocation(move.x, move.y, move.z);
                    OnMoveResponse.Broadcast(ConfirmedLocation);
                }
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
                    UE_LOG(LogMMOClient, Warning, TEXT("Game server connection failed. Switching back to AUTH state."));
                    SetClientState(EMMOClientState::CONNECTING_AUTH);
                }
            }
            break;
        }
        case PACKET_S_PLAYER_SPAWN: {
            S_PlayerSpawn spawn;
            if (DeserializeStruct(Data, spawn)) {
                UE_LOG(LogMMOClient, Log, TEXT("Player spawn packet received. EntityId: %d, Location: (%f, %f, %f)"), spawn.entityId, spawn.x, spawn.y, spawn.z);
                UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
                UMMOGameInstance* MMOGameInstance = Cast<UMMOGameInstance>(GameInstance);
                if(MMOGameInstance && MMOGameInstance->NetworkedEntityManager) {
                    MMOGameInstance->NetworkedEntityManager->HandlePlayerSpawnPacket(spawn);
                } else if (!MMOGameInstance) {
                    UE_LOG(LogMMOClient, Error, TEXT("Failed to cast GameInstance to MMOGameInstance!"));
                } else {
                    UE_LOG(LogMMOClient, Error, TEXT("NetworkedEntityManager is null!"));
                }
            }
            break;
        }
        case PACKET_S_PLAYER_DESPAWN: {
            S_PlayerDespawn despawn;
            if (DeserializeStruct(Data, despawn)) {
                UE_LOG(LogMMOClient, Log, TEXT("Player despawn packet received. EntityId: %d"), despawn.entityId);
                UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
                UMMOGameInstance* MMOGameInstance = Cast<UMMOGameInstance>(GameInstance);
                if(MMOGameInstance && MMOGameInstance->NetworkedEntityManager) {
                    MMOGameInstance->NetworkedEntityManager->HandlePlayerDespawnPacket(despawn);
                } else if (!MMOGameInstance) {
                    UE_LOG(LogMMOClient, Error, TEXT("Failed to cast GameInstance to MMOGameInstance!"));
                } else {
                    UE_LOG(LogMMOClient, Error, TEXT("NetworkedEntityManager is null!"));
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
                    StartHeartbeatForSocket(TEXT("Game"));
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
                // Only now consider the connection established and start heartbeat
                if (!ChatHeartbeat.TimerHandle.IsValid()) {
                    StartHeartbeatForSocket(TEXT("Chat"));
                }
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
                DisconnectGame(); // Ensure Game socket is disconnected when Auth connection is lost
                ConnectAuth(LastAuthHost, LastAuthPort);
            }
            break;
        case EMMOClientState::AUTH: // Auth server connected
            CheckHeartbeatTimeout(TEXT("Auth"), AuthHeartbeat);
            break;
        case EMMOClientState::CONNECTING_GAME: // Reconnecting to Game server & Chat Server only try for 5 seconds
            
            if(gameRetryCount < 5)
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
                UE_LOG(LogMMOClient, Warning, TEXT("Game server connection failed after 5 attempts. Switching to AUTH state."));
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
    if (SocketType == TEXT("Auth")) {
        if (!AuthSocket.IsValid()) {
            UE_LOG(LogMMOClient, Warning, TEXT("AuthSocket invalid when sending heartbeat."));
        } else if (AuthSocket->GetConnectionState() != SCS_Connected) {
            UE_LOG(LogMMOClient, Warning, TEXT("AuthSocket not connected when sending heartbeat. State: %d"), (int32)AuthSocket->GetConnectionState());
        } else {
            SendToAuth(Data);
            CheckHeartbeatTimeout(SocketType, AuthHeartbeat);
            UE_LOG(LogMMOClient, Verbose, TEXT("Heartbeat sent to %s server."), *SocketType);
        }
    } else if (SocketType == TEXT("Game")) {
        if (!GameSocket.IsValid()) {
            UE_LOG(LogMMOClient, Warning, TEXT("GameSocket invalid when sending heartbeat."));
        } else if (GameSocket->GetConnectionState() != SCS_Connected) {
            UE_LOG(LogMMOClient, Warning, TEXT("GameSocket not connected when sending heartbeat. State: %d"), (int32)GameSocket->GetConnectionState());
        } else {
            SendToGame(Data);
            CheckHeartbeatTimeout(SocketType, GameHeartbeat);
            UE_LOG(LogMMOClient, Verbose, TEXT("Heartbeat sent to %s server."), *SocketType);
        }
    } else if (SocketType == TEXT("Chat")) {
        if (!ChatSocket.IsValid()) {
            UE_LOG(LogMMOClient, Warning, TEXT("ChatSocket invalid when sending heartbeat."));
        } else if (ChatSocket->GetConnectionState() != SCS_Connected) {
            UE_LOG(LogMMOClient, Warning, TEXT("ChatSocket not connected when sending heartbeat. State: %d"), (int32)ChatSocket->GetConnectionState());
        } else {
            SendToChat(Data);
            CheckHeartbeatTimeout(SocketType, ChatHeartbeat);
            UE_LOG(LogMMOClient, Verbose, TEXT("Heartbeat sent to %s server."), *SocketType);
        }
    }
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

