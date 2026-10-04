// SPDX-License-Identifier: MIT
// All SDK calls run on the Python transport's single owner thread.
#include <eos_sdk.h>
#include <eos_connect.h>
#include <eos_lobby.h>
#include <eos_p2p.h>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>

#if defined(_WIN32)
#define RIIS_EXPORT __declspec(dllexport)
#else
#define RIIS_EXPORT
#endif

struct Bridge {
    EOS_HPlatform platform = nullptr;
    EOS_HConnect connect = nullptr;
    EOS_HLobby lobby = nullptr;
    EOS_HP2P p2p = nullptr;
    EOS_ProductUserId user = nullptr, peer = nullptr;
    EOS_HLobbySearch search = nullptr;
    EOS_HLobbyDetails detail = nullptr;
    EOS_NotificationId expiry = 0, request = 0, established = 0, closed = 0, members = 0;
    EOS_P2P_SocketId socket{};
    std::string name, room, expectedOwner, error;
    int login = 0, roomReady = 0, network = 0;
    bool host = false, ending = false, left = false;
    void fail(const char* operation, EOS_EResult result) {
        error = std::string(operation) + ": " + EOS_EResult_ToString(result);
    }
};
static bool active = false;
static void login(Bridge*);
static void EOS_CALL createdUser(const EOS_Connect_CreateUserCallbackInfo* d) {
    auto b = static_cast<Bridge*>(d->ClientData);
    if (d->ResultCode != EOS_EResult::EOS_Success) { b->fail("Create device user", d->ResultCode); return; }
    b->user = d->LocalUserId; b->login = 1;
}
static void EOS_CALL loggedIn(const EOS_Connect_LoginCallbackInfo* d) {
    auto b = static_cast<Bridge*>(d->ClientData);
    if (d->ResultCode == EOS_EResult::EOS_InvalidUser && !b->user) {
        EOS_Connect_CreateUserOptions o{}; o.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
        o.ContinuanceToken = d->ContinuanceToken;
        EOS_Connect_CreateUser(b->connect, &o, b, createdUser);
    } else if (d->ResultCode == EOS_EResult::EOS_Success) {
        if (b->user && b->user != d->LocalUserId) {
            b->error = "Device identity changed during the session."; return;
        }
        b->user = d->LocalUserId; b->login = 1;
    } else b->fail("Device login", d->ResultCode);
}
static void login(Bridge* b) {
    EOS_Connect_Credentials c{}; c.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    c.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    EOS_Connect_UserLoginInfo u{}; u.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
    u.DisplayName = b->name.c_str();
    EOS_Connect_LoginOptions o{}; o.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    o.Credentials = &c; o.UserLoginInfo = &u;
    EOS_Connect_Login(b->connect, &o, b, loggedIn);
}
static void EOS_CALL deviceCreated(const EOS_Connect_CreateDeviceIdCallbackInfo* d) {
    auto b = static_cast<Bridge*>(d->ClientData);
    if (d->ResultCode == EOS_EResult::EOS_Success || d->ResultCode == EOS_EResult::EOS_DuplicateNotAllowed) login(b);
    else b->fail("Create device credential", d->ResultCode);
}
static void EOS_CALL expired(const EOS_Connect_AuthExpirationCallbackInfo* d) {
    auto b = static_cast<Bridge*>(d->ClientData); if (!b->ending) login(b);
}
static void EOS_CALL incoming(const EOS_P2P_OnIncomingConnectionRequestInfo* d) {
    auto b = static_cast<Bridge*>(d->ClientData);
    if (b->peer != d->RemoteUserId || std::strcmp(d->SocketId->SocketName, b->socket.SocketName)) return;
    EOS_P2P_AcceptConnectionOptions o{}; o.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    o.LocalUserId = b->user; o.RemoteUserId = b->peer; o.SocketId = &b->socket;
    auto r = EOS_P2P_AcceptConnection(b->p2p, &o);
    if (r != EOS_EResult::EOS_Success) b->fail("Accept P2P", r);
}
static void EOS_CALL connected(const EOS_P2P_OnPeerConnectionEstablishedInfo* d) {
    auto b = static_cast<Bridge*>(d->ClientData);
    if (b->peer == d->RemoteUserId) b->network = int(d->NetworkType);
}
static void EOS_CALL disconnected(const EOS_P2P_OnRemoteConnectionClosedInfo* d) {
    auto b = static_cast<Bridge*>(d->ClientData);
    if (!b->ending && b->peer == d->RemoteUserId) b->error = "EOS peer connection closed.";
}
static void EOS_CALL memberChanged(const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo* d) {
    auto b = static_cast<Bridge*>(d->ClientData);
    if (b->ending || b->room != d->LobbyId) return;
    if ((d->TargetUserId == b->peer || d->TargetUserId == b->user) &&
        (d->CurrentStatus == EOS_ELobbyMemberStatus::EOS_LMS_LEFT || d->CurrentStatus == EOS_ELobbyMemberStatus::EOS_LMS_DISCONNECTED ||
         d->CurrentStatus == EOS_ELobbyMemberStatus::EOS_LMS_KICKED || d->CurrentStatus == EOS_ELobbyMemberStatus::EOS_LMS_CLOSED))
        b->error = "A player left the EOS lobby.";
}
extern "C" {
RIIS_EXPORT Bridge* riis_eos_create(const char* product, const char* sandbox, const char* deployment,
    const char* client, const char* secret, const char* cache, const char* display, int forceRelay) {
    if (active) return nullptr;
    EOS_InitializeOptions init{}; init.ApiVersion = EOS_INITIALIZE_API_LATEST;
    init.ProductName = "Riisorted"; init.ProductVersion = "0.3.0-eos-preview";
    if (EOS_Initialize(&init) != EOS_EResult::EOS_Success) return nullptr;
    active = true;
    auto b = new Bridge; b->name = display;
    EOS_Platform_Options o{}; o.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    o.ProductId = product; o.SandboxId = sandbox; o.DeploymentId = deployment;
    o.ClientCredentials.ClientId = client; o.ClientCredentials.ClientSecret = secret;
    o.CacheDirectory = cache; o.Flags = EOS_PF_DISABLE_OVERLAY;
    b->platform = EOS_Platform_Create(&o);
    if (!b->platform) { delete b; EOS_Shutdown(); active = false; return nullptr; }
    b->connect = EOS_Platform_GetConnectInterface(b->platform);
    b->lobby = EOS_Platform_GetLobbyInterface(b->platform);
    b->p2p = EOS_Platform_GetP2PInterface(b->platform);
    b->socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    std::strcpy(b->socket.SocketName, "RiisortedV1");
    EOS_P2P_SetRelayControlOptions relay{}; relay.ApiVersion = EOS_P2P_SETRELAYCONTROL_API_LATEST;
    relay.RelayControl = forceRelay ? EOS_ERelayControl::EOS_RC_ForceRelays : EOS_ERelayControl::EOS_RC_AllowRelays;
    auto r = EOS_P2P_SetRelayControl(b->p2p, &relay);
    if (r != EOS_EResult::EOS_Success) b->fail("Set relay mode", r);
    EOS_P2P_SetPortRangeOptions ports{}; ports.ApiVersion = EOS_P2P_SETPORTRANGE_API_LATEST;
    r = EOS_P2P_SetPortRange(b->p2p, &ports); // OS assigned UDP port, no fixed port collision.
    if (r != EOS_EResult::EOS_Success) b->fail("Set P2P port", r);
    EOS_P2P_SetPacketQueueSizeOptions q{}; q.ApiVersion = EOS_P2P_SETPACKETQUEUESIZE_API_LATEST;
    q.IncomingPacketQueueMaxSizeBytes = q.OutgoingPacketQueueMaxSizeBytes = 8 * 1024 * 1024;
    r = EOS_P2P_SetPacketQueueSize(b->p2p, &q);
    if (r != EOS_EResult::EOS_Success) b->fail("Set packet queues", r);
    EOS_Connect_AddNotifyAuthExpirationOptions expiry{}; expiry.ApiVersion = EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST;
    b->expiry = EOS_Connect_AddNotifyAuthExpiration(b->connect, &expiry, b, expired);
    EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions members{}; members.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYMEMBERSTATUSRECEIVED_API_LATEST;
    b->members = EOS_Lobby_AddNotifyLobbyMemberStatusReceived(b->lobby, &members, b, memberChanged);
    if (!b->expiry || !b->members) b->error = "EOS identity/lobby notifications could not be registered.";
    EOS_Connect_CreateDeviceIdOptions device{}; device.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
    device.DeviceModel = "Riisorted Linux PC";
    EOS_Connect_CreateDeviceId(b->connect, &device, b, deviceCreated);
    return b;
}
RIIS_EXPORT void riis_eos_tick(Bridge* b) { EOS_Platform_Tick(b->platform); }
RIIS_EXPORT const char* riis_eos_error(Bridge* b) { return b->error.c_str(); }
RIIS_EXPORT int riis_eos_login_ready(Bridge* b) { return b->login; }
RIIS_EXPORT int riis_eos_room_ready(Bridge* b) { return b->roomReady; }
RIIS_EXPORT const char* riis_eos_room_id(Bridge* b) { return b->room.c_str(); }
RIIS_EXPORT int riis_eos_network(Bridge* b) { return b->network; }
}
static void EOS_CALL roomCreated(const EOS_Lobby_CreateLobbyCallbackInfo* d) {
    auto b = static_cast<Bridge*>(d->ClientData);
    if (d->ResultCode != EOS_EResult::EOS_Success) { b->fail("Create lobby", d->ResultCode); return; }
    b->room = d->LobbyId; b->roomReady = 1;
}
static void EOS_CALL joined(const EOS_Lobby_JoinLobbyCallbackInfo* d) {
    auto b = static_cast<Bridge*>(d->ClientData);
    if (d->ResultCode != EOS_EResult::EOS_Success) { b->fail("Join lobby", d->ResultCode); return; }
    b->room = d->LobbyId; b->roomReady = 1;
}
static void EOS_CALL found(const EOS_LobbySearch_FindCallbackInfo* d) {
    auto b = static_cast<Bridge*>(d->ClientData);
    if (d->ResultCode != EOS_EResult::EOS_Success) { b->fail("Find lobby", d->ResultCode); return; }
    EOS_LobbySearch_CopySearchResultByIndexOptions copy{}; copy.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
    auto r = EOS_LobbySearch_CopySearchResultByIndex(b->search, &copy, &b->detail);
    if (r != EOS_EResult::EOS_Success) { b->fail("Lobby unavailable", r); return; }
    EOS_LobbyDetails_GetLobbyOwnerOptions owner{}; owner.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
    auto id = EOS_LobbyDetails_GetLobbyOwner(b->detail, &owner);
    if (id != EOS_ProductUserId_FromString(b->expectedOwner.c_str()) || id == b->user) {
        b->error = "Lobby owner differs from invitation, or both windows use the same device identity."; return;
    }
    EOS_Lobby_JoinLobbyOptions join{}; join.ApiVersion = EOS_LOBBY_JOINLOBBY_API_LATEST;
    join.LocalUserId = b->user; join.LobbyDetailsHandle = b->detail;
    EOS_Lobby_JoinLobby(b->lobby, &join, b, joined);
}
extern "C" {
RIIS_EXPORT void riis_eos_host(Bridge* b) {
    b->host = true;
    EOS_Lobby_CreateLobbyOptions o{}; o.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
    o.LocalUserId = b->user; o.MaxLobbyMembers = 2;
    // Room-code discovery uses an exact ID search; no public lobby list in our UI.
    // EOS admission is public; the separate invitation secret gates all save/data exchange.
    o.PermissionLevel = EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED; o.BucketId = "RiisortedV1";
    o.bDisableHostMigration = EOS_TRUE; o.bRejoinAfterKickRequiresInvite = EOS_TRUE;
    EOS_Lobby_CreateLobby(b->lobby, &o, b, roomCreated);
}
RIIS_EXPORT void riis_eos_join(Bridge* b, const char* room, const char* owner) {
    b->expectedOwner = owner;
    EOS_Lobby_CreateLobbySearchOptions create{}; create.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
    create.MaxResults = 1;
    auto r = EOS_Lobby_CreateLobbySearch(b->lobby, &create, &b->search);
    if (r != EOS_EResult::EOS_Success) { b->fail("Create lobby search", r); return; }
    EOS_LobbySearch_SetLobbyIdOptions id{}; id.ApiVersion = EOS_LOBBYSEARCH_SETLOBBYID_API_LATEST; id.LobbyId = room;
    r = EOS_LobbySearch_SetLobbyId(b->search, &id);
    if (r != EOS_EResult::EOS_Success) { b->fail("Set lobby ID", r); return; }
    EOS_LobbySearch_FindOptions find{}; find.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST; find.LocalUserId = b->user;
    EOS_LobbySearch_Find(b->search, &find, b, found);
}
RIIS_EXPORT const char* riis_eos_user_id(Bridge* b) {
    static thread_local char id[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
    int32_t length = sizeof(id);
    if (!b->user || EOS_ProductUserId_ToString(b->user, id, &length) != EOS_EResult::EOS_Success) return "";
    return id;
}
RIIS_EXPORT int riis_eos_select_peer(Bridge* b) {
    if (b->peer) return 1;
    EOS_Lobby_CopyLobbyDetailsHandleOptions o{}; o.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
    o.LobbyId = b->room.c_str(); o.LocalUserId = b->user;
    EOS_HLobbyDetails detail = nullptr;
    auto r = EOS_Lobby_CopyLobbyDetailsHandle(b->lobby, &o, &detail);
    if (r != EOS_EResult::EOS_Success) { b->fail("Read lobby members", r); return 0; }
    EOS_LobbyDetails_GetMemberCountOptions count{}; count.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
    auto n = EOS_LobbyDetails_GetMemberCount(detail, &count);
    for (uint32_t i = 0; i < n; ++i) {
        EOS_LobbyDetails_GetMemberByIndexOptions member{}; member.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERBYINDEX_API_LATEST;
        member.MemberIndex = i;
        auto id = EOS_LobbyDetails_GetMemberByIndex(detail, &member);
        if (id && id != b->user) b->peer = id;
    }
    EOS_LobbyDetails_Release(detail);
    if (!b->peer) return 0;
    if (!b->host && b->peer != EOS_ProductUserId_FromString(b->expectedOwner.c_str())) {
        b->error = "Unexpected peer in lobby."; return 0;
    }
    EOS_P2P_AddNotifyPeerConnectionRequestOptions req{}; req.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
    req.LocalUserId = b->user; req.SocketId = &b->socket;
    b->request = EOS_P2P_AddNotifyPeerConnectionRequest(b->p2p, &req, b, incoming);
    EOS_P2P_AddNotifyPeerConnectionEstablishedOptions est{}; est.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST;
    est.LocalUserId = b->user; est.SocketId = &b->socket;
    b->established = EOS_P2P_AddNotifyPeerConnectionEstablished(b->p2p, &est, b, connected);
    EOS_P2P_AddNotifyPeerConnectionClosedOptions close{}; close.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST;
    close.LocalUserId = b->user; close.SocketId = &b->socket;
    b->closed = EOS_P2P_AddNotifyPeerConnectionClosed(b->p2p, &close, b, disconnected);
    if (!b->request || !b->established || !b->closed) {
        b->error = "EOS P2P notifications could not be registered."; return 0;
    }
    EOS_P2P_AcceptConnectionOptions accept{}; accept.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    accept.LocalUserId = b->user; accept.RemoteUserId = b->peer; accept.SocketId = &b->socket;
    r = EOS_P2P_AcceptConnection(b->p2p, &accept);
    if (r != EOS_EResult::EOS_Success) b->fail("Accept peer", r);
    return 1;
}
RIIS_EXPORT int riis_eos_send(Bridge* b, const void* data, uint32_t size) {
    if (!b->peer || !size || size > EOS_P2P_MAX_PACKET_SIZE) return -1;
    EOS_P2P_SendPacketOptions o{}; o.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
    o.LocalUserId = b->user; o.RemoteUserId = b->peer; o.SocketId = &b->socket;
    o.Data = data; o.DataLengthBytes = size; o.bAllowDelayedDelivery = EOS_TRUE;
    o.Reliability = EOS_EPacketReliability::EOS_PR_ReliableOrdered;
    auto r = EOS_P2P_SendPacket(b->p2p, &o);
    if (r == EOS_EResult::EOS_LimitExceeded) return 0;
    if (r != EOS_EResult::EOS_Success) { b->fail("Send P2P packet", r); return -1; }
    return 1;
}
RIIS_EXPORT int riis_eos_receive(Bridge* b, void* data, uint32_t capacity) {
    EOS_P2P_ReceivePacketOptions o{}; o.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
    o.LocalUserId = b->user; o.MaxDataSizeBytes = capacity;
    EOS_ProductUserId remote = nullptr; EOS_P2P_SocketId socket{}; uint8_t channel = 0; uint32_t size = 0;
    auto r = EOS_P2P_ReceivePacket(b->p2p, &o, &remote, &socket, &channel, data, &size);
    if (r == EOS_EResult::EOS_NotFound) return 0;
    if (r != EOS_EResult::EOS_Success) { b->fail("Receive P2P packet", r); return -1; }
    if (remote != b->peer || channel != 0 || std::strcmp(socket.SocketName, b->socket.SocketName)) return 0;
    return int(size);
}
}
static void EOS_CALL left(const EOS_Lobby_LeaveLobbyCallbackInfo* d) { static_cast<Bridge*>(d->ClientData)->left = true; }
static void EOS_CALL destroyed(const EOS_Lobby_DestroyLobbyCallbackInfo* d) { static_cast<Bridge*>(d->ClientData)->left = true; }
extern "C" RIIS_EXPORT void riis_eos_destroy(Bridge* b) {
    if (!b) return;
    b->ending = true;
    if (b->request) EOS_P2P_RemoveNotifyPeerConnectionRequest(b->p2p, b->request);
    if (b->established) EOS_P2P_RemoveNotifyPeerConnectionEstablished(b->p2p, b->established);
    if (b->closed) EOS_P2P_RemoveNotifyPeerConnectionClosed(b->p2p, b->closed);
    if (b->expiry) EOS_Connect_RemoveNotifyAuthExpiration(b->connect, b->expiry);
    if (b->members) EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived(b->lobby, b->members);
    if (!b->room.empty()) {
        if (b->host) {
            EOS_Lobby_DestroyLobbyOptions o{}; o.ApiVersion = EOS_LOBBY_DESTROYLOBBY_API_LATEST;
            o.LocalUserId = b->user; o.LobbyId = b->room.c_str();
            EOS_Lobby_DestroyLobby(b->lobby, &o, b, destroyed);
        } else {
            EOS_Lobby_LeaveLobbyOptions o{}; o.ApiVersion = EOS_LOBBY_LEAVELOBBY_API_LATEST;
            o.LocalUserId = b->user; o.LobbyId = b->room.c_str();
            EOS_Lobby_LeaveLobby(b->lobby, &o, b, left);
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!b->left && std::chrono::steady_clock::now() < deadline) {
            EOS_Platform_Tick(b->platform); std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    if (b->detail) EOS_LobbyDetails_Release(b->detail);
    if (b->search) EOS_LobbySearch_Release(b->search);
    EOS_Platform_Release(b->platform); EOS_Shutdown(); active = false; delete b;
}
