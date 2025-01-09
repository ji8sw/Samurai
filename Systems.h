#pragma once
#include <vector>
#include <iostream>
#include <thread>

#include "enet/enet.h"
#include "MatchmakingData.h"
#include "PacketHelper.h"

namespace Samurai
{
    using namespace Matchmaking;

    class networkSystem
    {
    public:
        mutable bool isClient = true;
        mutable int port = 1000;
        mutable bool portCustomized = false;

        networkSystem(bool IsClient = true) : isClient(IsClient)
        {

        }

        virtual void start() = 0;
    };

    /*

        ░██████╗███████╗██████╗░██╗░░░██╗███████╗██████╗░
        ██╔════╝██╔════╝██╔══██╗██║░░░██║██╔════╝██╔══██╗
        ╚█████╗░█████╗░░██████╔╝╚██╗░██╔╝█████╗░░██████╔╝
        ░╚═══██╗██╔══╝░░██╔══██╗░╚████╔╝░██╔══╝░░██╔══██╗
        ██████╔╝███████╗██║░░██║░░╚██╔╝░░███████╗██║░░██║
        ╚═════╝░╚══════╝╚═╝░░╚═╝░░░╚═╝░░░╚══════╝╚═╝░░╚═╝

    */

    class serverSystem : public networkSystem
    {
        std::vector<Matchmaking::sessionData> sessionList;
        std::vector<ENetPeer*> allConnections;

    public:
        serverSystem()
        {
            isClient = false;
        }

        void tick()
        {
            for (int SessInd = 0; SessInd < sessionList.size(); SessInd++)
            {
                if (sessionList[SessInd].shouldBeDestroyed()) // delete empty sessions
                {
                    std::cout << "Deleting empty session (" << sessionList[SessInd].id << ")\n\n";
                    sessionList.erase(sessionList.begin() + SessInd);
                }
            }
        }

        int findSessionIndexById(int id)
        {
            for (int index = 0; index < sessionList.size(); index++)
                if (sessionList[index].id == id) return index;

            return INVALID_INT;
        }

        int findSessionIndexByMemberAddress(ENetAddress addr)
        {
            for (int index = 0; index < sessionList.size(); index++)
            {
                for (playerConnectionInfo info : sessionList[index].playerList)
                    if (info.matches(addr)) return index;
            }

            return INVALID_INT;
        }

        // returns an array index for allConnections, useful for determining if a player is connected to matchmaking at all
        int findPeerIndexByAddress(ENetAddress addr)
        {
            for (int index = 0; index < allConnections.size(); index++)
            {
                if (areAdderessesMatching(allConnections[index]->address, addr)) return index;
            }

            return INVALID_INT;
        }

        void start() override
        {
            ENetHost* server;
            ENetAddress address;

            // Initialize ENet
            if (enet_initialize() != 0)
            {
                std::cerr << "An error occurred while initializing ENet." << std::endl;
                return;
            }
            atexit(enet_deinitialize);

            // Set up the address for the server (host IP and port)
            enet_address_set_host(&address, "0.0.0.0");  // Listen on all available interfaces
            address.port = 1000;

            // Create the server (maximum 32 clients, 2 channels)
            server = enet_host_create(&address, 30, 2, 0, 0);
            if (server == nullptr)
            {
                std::cerr << "An error occurred while trying to create an ENet server, trying alternative port" << std::endl;
                address.port = port + 100;
                server = enet_host_create(&address, 5, 2, 0, 0);
                if (server == nullptr)
                {
                    std::cerr << "Alternative port failed, quitting" << std::endl;
                    return;
                }
            }

            std::cout << "Matchmaking server is now running on port " << address.port << "\n\n";

            // Main server loop
            while (true)
            {
                ENetEvent event;
                while (enet_host_service(server, &event, 1000) > 0)
                {
                    switch (event.type)
                    {
                    case ENET_EVENT_TYPE_CONNECT:
                        std::cout << "A new peer connected from " << ipToString(event.peer->address.host) << ":" << event.peer->address.port << std::endl;
                        allConnections.push_back(event.peer);
                        break;

                    case ENET_EVENT_TYPE_RECEIVE:
                    {
                        try
                        {
                            Packet incoming = Packet::deserialize((char*)event.packet->data, event.packet->dataLength);
                            size_t offset = 0;

                            switch (incoming.type)
                            {
                            case REQUEST_CREATE_SESSION:
                            {
                                int maxPlayers = extractInt(incoming.data, offset);
                                joinabilityType joinability = (joinabilityType)extractInt(incoming.data, offset);
                                bool advertise = joinability == allowAny; // no point advertising the session if its private
                                std::cout << "Received REQUEST_CREATE_SESSION: Max Players = " << maxPlayers << ", Joinability = " << joinability << std::endl;

                                playerConnectionInfo ConnectionInfo = playerConnectionInfo(event.peer->address);
                                ConnectionInfo.connection = event.peer;
                                sessionData NewSession = sessionData(ConnectionInfo, maxPlayers);
                                NewSession.waitForHost = true; // host hasnt joined yet so dont destroy it cuz its empty
                                NewSession.advertise = advertise;
                                NewSession.joinability = joinability;
                                sessionList.push_back(NewSession);

                                std::cout << "Successfully created session, ID: " << NewSession.id << "\n\n";

                                // send the session id back to the host so they can request to join the id
                                Packet packet;
                                packet.type = PROVIDE_SESSION_DETAILS;

                                appendInt(packet.data, NewSession.id); // session id
                                appendInt(packet.data, true); // is the person we're sending this to, the host?
                                appendInt(packet.data, 0); // player count, we know theres no players to connect to
                                appendInt(packet.data, 0); // host pid
                                // no need to provide player connection info if theres no players
                                sendNow(packet, event.peer);

                                break;
                            }
                            case REQUEST_JOIN_SESSION:
                            {
                                int sessionId = extractInt(incoming.data, offset);
                                std::cout << "Received REQUEST_JOIN_SESSION: Session ID = " << sessionId << std::endl;
                                bool success = false;

                                int requestedSessionIndex = findSessionIndexById(sessionId);
                                
                                if (requestedSessionIndex == INVALID_INT)
                                {
                                    sendQuickResponseNow(event.peer, INVALID_SESSION_ID);
                                    std::cerr << "Failed to join session.\n\n";
                                    continue;
                                    break;
                                }

                                sessionData* Data = &sessionList[requestedSessionIndex];

                                if (Data->host.matches(event.peer->address)) // this player created this session, join no matter
                                {
                                    Data->host.sessionId = Data->id;
                                    Data->playerList.push_back(Data->host);
                                    Data->waitForHost = false;
                                    success = true;
                                    sendQuickResponseNow(event.peer, SESSION_JOINED_SUCCESS);
                                }
                                else // this is someone elses session, check if they can join
                                {
                                    bool joinAllowed = false;

                                    if (!Data->shouldBeDestroyed() && !Data->isFull())
                                    {
                                        if (Data->joinability == allowAny) joinAllowed = true;
                                        else if (Data->joinability == inviteOnly) // does an invite exist for this players address
                                        {
                                            for (int inviteIndex = 0; inviteIndex < Data->inviteList.size(); inviteIndex++)
                                            {
                                                if (areAdderessesMatching(Data->inviteList[inviteIndex], event.peer->address))
                                                {
                                                    // allow join and delete invite
                                                    joinAllowed = true;
                                                    Data->inviteList.erase(Data->inviteList.begin() + inviteIndex);
                                                }
                                            }
                                        }

                                        if (joinAllowed)
                                        {
                                            playerConnectionInfo connectionInfo = playerConnectionInfo(event.peer->address);
                                            connectionInfo.connection = event.peer;
                                            connectionInfo.sessionId = Data->id;

                                            Packet packet;
                                            packet.type = PROVIDE_JOINER_INFO;
                                            appendAddress(packet.data, connectionInfo.address);
                                            sendBroadcastNow(*Data, packet); // inform all existing players about the new player

                                            Data->playerList.push_back(connectionInfo);
                                            sendQuickResponseNow(event.peer, SESSION_JOINED_SUCCESS);
                                            success = true;
                                        }
                                    }
                                    
                                    if (!joinAllowed)
                                    {
                                        sendQuickResponseNow(event.peer, JOIN_NOT_ALLOWED);
                                    }
                                }

                                if (!success) 
                                {
                                    std::cerr << "Failed to join session.\n\n";
                                    sendQuickResponseNow(event.peer, SESSION_JOINED_FAILURE);
                                }
                                break;
                            }
                            case REQUEST_FIND_SESSION:
                            {
                                std::cout << "Received REQUEST_FIND_SESSION, finding session\n";
                                for (int sessionIndex = 0; sessionIndex < sessionList.size(); sessionIndex++)
                                {
                                    sessionData* Data = &sessionList[sessionIndex];
                                    if (Data->joinability == allowAny && Data->advertise == true && !Data->isFull() && !Data->shouldBeDestroyed())
                                    {
                                        std::cout << "Successfully found session, ID: " << Data->id << "\n\n";

                                        // send the session id back to the requester so they can request to join the id
                                        Packet packet;
                                        packet.type = PROVIDE_SESSION_DETAILS;

                                        appendInt(packet.data, Data->id); // session id
                                        appendInt(packet.data, Data->host.matches(event.peer->address)); // is the person we're sending this to, the host?
                                        appendInt(packet.data, Data->playerList.size()); // player count, we know theres no players to connect to
                                        appendInt(packet.data, Data->getHostPid()); // host pid

                                        for (playerConnectionInfo& connectionInfo : Data->playerList)
                                        {
                                            appendAddress(packet.data, connectionInfo.address);
                                        }

                                        sendNow(packet, event.peer);
                                        break;
                                    }
                                }

                                // didnt find any session
                                sendQuickResponseNow(event.peer, SESSION_FIND_FAILURE);
                                break;
                            }
                            case REQUEST_FIND_SESSION_BY_ID:
                            {
                                std::cout << "Received REQUEST_FIND_SESSION_BY_ID, finding session\n";
                                int tryingToJoin = extractInt(incoming.data, offset);
                                int sessionIndex = findSessionIndexById(tryingToJoin);
                                if (sessionIndex != INVALID_INT)
                                {
                                    sessionData* Data = &sessionList[sessionIndex];

                                    if (!Data->isFull() && !Data->shouldBeDestroyed() && !Data->inviteList.empty())
                                    {
                                        int index = Data->inviteList.size();
                                        while (index--)
                                        {
                                            if (areAdderessesMatching(event.peer->address, Data->inviteList[index]))
                                            {
                                                Data->inviteList.erase(Data->inviteList.begin() + index);

                                                std::cout << "Successfully found session, ID: " << Data->id << "\n\n";

                                                // send the session id back to the requester so they can request to join the id
                                                Packet packet;
                                                packet.type = PROVIDE_SESSION_DETAILS;

                                                appendInt(packet.data, Data->id); // session id
                                                appendInt(packet.data, Data->host.matches(event.peer->address)); // is the person we're sending this to, the host?
                                                appendInt(packet.data, Data->playerList.size()); // player count, we know theres no players to connect to
                                                appendInt(packet.data, Data->getHostPid()); // host pid

                                                for (playerConnectionInfo& connectionInfo : Data->playerList)
                                                {
                                                    appendAddress(packet.data, connectionInfo.address);
                                                }

                                                sendNow(packet, event.peer);
                                                break;
                                            }
                                        }
                                    }
                                }

                                // didnt find any session
                                sendQuickResponseNow(event.peer, SESSION_FIND_FAILURE);
                                break;
                            }
                            case REQUEST_SEND_INVITE:
                            {
                                std::cout << "Received REQUEST_SEND_INVITE, sending...\n";
                                ENetAddress target = extractAddress(incoming.data, offset);
                                std::cout << ipToString(target.host) << ":" << target.port << std::endl;
                                int sessionIndex = findSessionIndexByMemberAddress(event.peer->address); // session as an array element
                                if (sessionIndex != INVALID_INT)
                                {
                                    int sessionId = sessionList[sessionIndex].id; // session as an identifier
                                    int targetConnectionIndex = findPeerIndexByAddress(target); // connection info of the target as an array element

                                    if (targetConnectionIndex != INVALID_INT) // is the player connected to matchmaking at all?
                                    {
                                        ENetPeer* targetConnection = allConnections[targetConnectionIndex];

                                        int targetSessionIndex = findSessionIndexByMemberAddress(target); // session target is in as an array element
                                        int targetSessionId = INVALID_INT; // session target is in as an identifier
                                        std::cout << "index: " << targetSessionIndex << "\nlength: " << sessionList.size() - 1 << "\n";
                                        if (targetSessionIndex != INVALID_INT)
                                        {
                                            std::cout << "target is in a session getting it now\n";
                                            targetSessionId = sessionList[targetSessionIndex].id;
                                        }

                                        // ensure we arent inviting the target to the session theyre already in
                                        if (targetSessionId == INVALID_INT || targetSessionId != sessionId) // not in a session or not in the destination session
                                        {
                                            // send invite and the session id that they are invited to
                                            Packet packet;
                                            packet.type = PROVIDE_INVITE;
                                            appendInt(packet.data, sessionId); // session id
                                            sendNow(packet, targetConnection);
                                            sessionList[sessionIndex].inviteList.push_back(targetConnection->address); // add them to invite list
                                            std::cout << "Sent invite.\n";
                                        }
                                        else std::cout << "Reciever is already in the destination session...\n";
                                    }
                                    else std::cout << "Reciever is not connected to matchmaking...\n";
                                }
                                else std::cout << "Sender is not in a session...\n";
                                break;
                            }
                            case PROVIDE_QUICK_RESPONSE:
                            {
                                QuickResponseType type = (QuickResponseType)extractInt(incoming.data, offset);
                                switch (type)
                                {
                                case NOTIFY_LEAVE_SESSION:
                                {
                                    Packet packet;
                                    packet.type = PLAYER_LEFT;
                                    appendAddress(packet.data, event.peer->address);
                                    int sessionIndex = findSessionIndexByMemberAddress(event.peer->address);
                                    sessionData& data = sessionList[sessionIndex];
                                    
                                    int pid = data.getPidFromAddress(event.peer->address);
                                    data.playerList.erase(data.playerList.begin() + pid);

                                    sendBroadcastNow(sessionList[sessionIndex], packet); // inform all existing players about the leave
                                    break;
                                }
                                default:
                                {
                                    std::cout << "Got quick response: " << type << "\n\n";
                                    break;
                                }
                                }
                                break;
                            }
                            default:
                                std::cerr << "Unknown packet type received: " << incoming.type << "\n\n";
                            }
                        }
                        catch (const std::exception& exception)
                        {
                            std::cerr << "Failed to handle packet: " << exception.what() << "\n\n";
                        }
                        break;
                    }
                    case ENET_EVENT_TYPE_DISCONNECT:
                    {
                        int connectionIndex = findPeerIndexByAddress(event.peer->address);

                        if (connectionIndex != INVALID_INT)
                        {
                            allConnections.erase(allConnections.begin() + connectionIndex);
                            std::cout << "peer disconnected: " << ipToString(event.peer->address.host) << ":" << event.peer->address.port << "\n\n";
                        }

                        break;
                    }
                    }
                }

                tick();
            }

            enet_host_destroy(server);
        }
    };

    /*

        ░█████╗░██╗░░░░░██╗███████╗███╗░░██╗████████╗
        ██╔══██╗██║░░░░░██║██╔════╝████╗░██║╚══██╔══╝
        ██║░░╚═╝██║░░░░░██║█████╗░░██╔██╗██║░░░██║░░░
        ██║░░██╗██║░░░░░██║██╔══╝░░██║╚████║░░░██║░░░
        ╚█████╔╝███████╗██║███████╗██║░╚███║░░░██║░░░
        ░╚════╝░╚══════╝╚═╝╚══════╝╚═╝░░╚══╝░░░╚═╝░░░
    
    */

    class clientSystem : public networkSystem
    {
    public:
        ENetHost* self = nullptr;
        ENetPeer* matchmakingHost = nullptr;
        clientJoinState state = noSession;
        int sessionId = 0;
        bool isHost = true;
        std::vector<Matchmaking::playerConnectionInfo> knownPlayerInfos;
        std::atomic<bool> running = true;

        std::vector<int> inviteIds; // list of session ids we have been invited to

        clientSystem()
        {
            isClient = true;
        }

        int playerIndexByAddress(ENetAddress addr)
        {
            for (int pid = 0; pid < knownPlayerInfos.size(); pid++)
            {
                if (knownPlayerInfos[pid].matches(addr))
                    return pid;
            }

            return INVALID_INT;
        }

        void leaveSession()
        {
            if (state == inSession)
            {
                sessionId = 0;
                state = noSession;

                for (Matchmaking::playerConnectionInfo& info : knownPlayerInfos)
                {
                    info.shouldDisconnect = true;
                }

                knownPlayerInfos.clear();

                Packet leaveMessage;
                leaveMessage.type = NOTIFY_LEAVE_SESSION;
                sendQuickResponseNow(matchmakingHost, NOTIFY_LEAVE_SESSION);
            }
        }

        void userInputLoop()
        {
            std::string input;
            while (running) 
            {
                std::cout << "> ";
                std::getline(std::cin, input);

                if (input == "quit")
                {
                    if (state == inSession)
                    {
                        leaveSession();
                        std::cout << "Leaving session...\n\n";

                        while (state == inSession) { }

                        enet_peer_disconnect_now(matchmakingHost, 0);
                        enet_host_destroy(self);
                    }

                    running = false;
                    break;
                }
                else if (input == "create_session") 
                {
                    if (state == inSession)
                    {
                        std::cout << "You must leave your current session first!\n\n";
                        continue;
                    }

                    joinabilityType joinability = allowAny;
                    int maxPlayers = 30;

                    std::cout << "Joinability (0: Public, 1: Friends Only, 2: Private) > ";
                    std::getline(std::cin, input);

                    try
                    {
                        joinability = (joinabilityType)std::stoi(input);
                    }
                    catch (...) { joinability = allowAny; }

                    std::cout << "Max Players > ";
                    std::getline(std::cin, input);

                    try
                    {
                        maxPlayers = std::stoi(input);
                    }
                    catch (...) { maxPlayers = 30; }

                    std::cout << "Requesting session creation.\n\n";
                    Packet packet;
                    packet.type = REQUEST_CREATE_SESSION;
                    appendInt(packet.data, maxPlayers);
                    appendInt(packet.data, joinability);
                    sendNow(packet, matchmakingHost);
                    state = waitingForSessionInfo;
                }
                else if (input == "join_session")
                {
                    if (state == inSession)
                    {
                        std::cout << "You must leave your current session first!\n\n";
                        continue;
                    }

                    std::cout << "Requesting session join info\n\n";
                    Packet packet;
                    packet.type = REQUEST_FIND_SESSION;
                    sendNow(packet, matchmakingHost);
                    state = waitingForSessionInfo;
                }
                else if (input == "send_message")
                {
                    if (state != inSession)
                    {
                        std::cout << "You must join a session first!\n\n";
                        continue;
                    }

                    std::cout << "> ";
                    std::getline(std::cin, input);

                    Packet packet;
                    packet.type = P2P_CHAT_MESSAGE;
                    appendString(packet.data, input);
                    sendBroadcastNow(knownPlayerInfos, packet);
                }
                else if (input == "leave_session")
                {
                    if (state != inSession)
                    {
                        std::cout << "You must join a session first!\n\n";
                        continue;
                    }

                    leaveSession();
                    std::cout << "Leaving session...\n\n";
                }
                else 
                    std::cout << "Unknown command.\n";
            }
        }

        void checkConnections()
        {
            for (int infoIndex = 0; infoIndex < knownPlayerInfos.size(); infoIndex++)
            {
                Matchmaking::playerConnectionInfo& info = knownPlayerInfos[infoIndex];

                for (int infoIndex2 = 0; infoIndex2 < knownPlayerInfos.size(); infoIndex2++) // check for duplicate connections
                    if (infoIndex != infoIndex2 && info.matches(knownPlayerInfos[infoIndex2]))
                        knownPlayerInfos.erase(knownPlayerInfos.begin() + infoIndex2);

                if (info.isPlayer)
                {
                    if (info.connected && info.shouldDisconnect)
                    {
                        std::cout << "Disconnecting from player: " << ipToString(info.address.host) << ":" << info.address.port << "\n";
                        info.disconnect();
                        knownPlayerInfos.erase(knownPlayerInfos.begin() + infoIndex);
                        continue;
                    }
                    else if (!info.connecting && !info.connected && !info.shouldDisconnect)
                    {
                        std::cout << "Connecting to player: " << ipToString(info.address.host) << ":" << info.address.port << "\n";
                        info.connection = enet_host_connect(self, &info.address, 2, 0);
                        info.connecting = true;
                        continue;
                    }
                }
            }
        }

        void networkLoop()
        {
            while (running)
            {
                // Handle events (incoming packets, disconnects, etc.)
                ENetEvent event;
                while (enet_host_service(self, &event, 1000) > 0)
                {
                    switch (event.type)
                    {
                    case ENET_EVENT_TYPE_CONNECT:
                    {
                        if (event.peer->incomingPeerID != -1) // it is an incoming connection
                        {
                            for (Matchmaking::playerConnectionInfo& info : knownPlayerInfos)
                            {
                                if (info.matches(event.peer->address))
                                {
                                    if (!info.connected)
                                    {
                                        info.isPlayer = true;
                                        info.connected = true;
                                        info.connection = event.peer;
                                        info.connecting = false;
                                        std::cout << "Successfully connected to player at " << ipToString(info.address.host) << ":" << info.address.port << "!\n";
                                    }
                                    break;
                                }
                            }
                        }
                        break;
                    }
                    case ENET_EVENT_TYPE_RECEIVE:
                    {
                        Packet packet = Packet::deserialize((char*)event.packet->data, event.packet->dataLength);
                        size_t offset = 0;

                        switch (packet.type)
                        {
                        case PROVIDE_SESSION_DETAILS: // used when awaiting join session
                        {
                            switch (state)
                            {
                            case waitingForSessionInfo:
                            {
                                sessionId = extractInt(packet.data, offset); // session id
                                isHost = extractInt(packet.data, offset); // am i the host of this session, todo: add extractBool
                                int count = extractInt(packet.data, offset); // player count
                                int hostId = extractInt(packet.data, offset); // host id

                                for (int playerIndex = 0; playerIndex < count; playerIndex++)
                                {
                                    Matchmaking::playerConnectionInfo Info(extractAddress(packet.data, offset));
                                    Info.isPlayer = true;
                                    Info.connected = false;
                                    if (playerIndex == hostId) Info.isHost = true;

                                    knownPlayerInfos.push_back(Info);
                                }

                                std::cout << "Found connection info for " << knownPlayerInfos.size() << " players.\n";

                                std::cout << "Joining session with details:\nID: " << sessionId << "\nPlayer Count: " << count << "\n\n";
                                Packet packet;
                                packet.type = REQUEST_JOIN_SESSION;
                                appendInt(packet.data, sessionId);
                                sendNow(packet, matchmakingHost);
                                state = joiningSession;
                                break;
                            }
                            }
                            break;
                        }
                        case PROVIDE_QUICK_RESPONSE:
                        {
                            QuickResponseType Type = (QuickResponseType)extractInt(packet.data, offset);

                            switch (Type)
                            {
                            case SESSION_CREATED_SUCCESS:
                            {
                                state = joiningSession;
                                std::cout << "Got quick response: Session created successfully!\n\n";
                                break;
                            }
                            case SESSION_JOINED_SUCCESS:
                            {
                                std::cout << "Got quick response: Session joined successfully!\n\n";
                                state = inSession;
                                break;
                            }
                            case SESSION_JOINED_FAILURE:
                            {
                                state = noSession;
                                std::cout << "Got quick response: Failed to join session!\n\n";
                                break;
                            }
                            case SESSION_FIND_FAILURE:
                            {
                                std::cout << "Got quick response: Failed to find a joinable session!\n\n";
                                if (state == waitingForSessionInfo)
                                {
                                    if (sessionId != 0) state = inSession;
                                    else state = noSession;
                                }
                                break;
                            }
                            case INVALID_SESSION_ID:
                            {
                                std::cout << "The session you tried to join no longer exists or is invalid\n\n";
                                break;
                            }
                            case JOIN_NOT_ALLOWED:
                            {
                                std::cout << "The session you tried to join is private\n\n";
                                break;
                            }
                            default:
                            {
                                std::cout << "Got quick response: " << Type << "\n\n";
                                break;
                            }
                            }
                            break;
                        }
                        case PROVIDE_JOINER_INFO:
                        {
                            ENetAddress addr = extractAddress(packet.data, offset);

                            Matchmaking::playerConnectionInfo newInfo(addr);
                            newInfo.isPlayer = true;

                            knownPlayerInfos.push_back(newInfo);

                            break;
                        }
                        case PLAYER_LEFT:
                        {
                            ENetAddress addr = extractAddress(packet.data, offset); // addr of person who left

                            if (!areAdderessesMatching(addr, self->address))
                            {
                                int pid = playerIndexByAddress(addr);

                                if (pid != INVALID_INT)
                                {
                                    std::cout << ipToString(addr.host) << ":" << addr.port << " has left." << std::endl;
                                    knownPlayerInfos[pid].shouldDisconnect = true;
                                }
                            }

                            break;
                        }
                        case PROVIDE_QUICK_RESPONSE_MESSAGE:
                        {
                            std::string message = extractString(packet.data, offset);
                            std::cout << "Got quick response message: " << message << "\n\n";
                            break;
                        }
                        case P2P_CHAT_MESSAGE:
                        {
                            std::string message = extractString(packet.data, offset);
                            std::cout << ipToString(event.peer->address.host) << ":" << event.peer->address.port << " says: " << message << "\n\n";
                            break;
                        }
                        case PROVIDE_INVITE:
                        {
                            int invitedToSessionId = extractInt(packet.data, offset);
                            inviteIds.push_back(invitedToSessionId);
                            std::cout << "Someone has sent you an invite, session ID: " << invitedToSessionId << "\n\n";
                            break;
                        }
                        default:
                        {
                            std::cout << "A request was recieved but not handled, type: " << packet.type << "\n\n";
                            break;
                        }
                        }

                        enet_packet_destroy(event.packet);
                        break;
                    }
                    case ENET_EVENT_TYPE_DISCONNECT:
                        if (areAdderessesMatching(event.peer->address, matchmakingHost->address))
                        {
                            std::cout << "Matchmaking server disconnected." << std::endl;
                            return;
                        }

                        int pid = playerIndexByAddress(event.peer->address);

                        if (pid != INVALID_INT)
                        {
                            std::cout << ipToString(event.peer->address.host) << ":" << event.peer->address.port << " has left." << std::endl;
                            knownPlayerInfos[pid].shouldDisconnect = true;
                        }
                    }
                }

                checkConnections();

                // Sleep for 1 second
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        void start() override
        {
            // Initialize ENet
            if (enet_initialize() != 0)
            {
                std::cerr << "An error occurred while initializing ENet." << std::endl;
                return;
            }
            atexit(enet_deinitialize);

            // servers address (IP and port)
            ENetAddress serverAddress;
            enet_address_set_host(&serverAddress, "127.0.0.1");
            serverAddress.port = 1000;

            // self address (IP and port)
            ENetAddress localAddress;
            localAddress.host = ENET_HOST_ANY;
            localAddress.port = port;

            if (!portCustomized)
            {
                std::string input;
                std::cout << "Enter local port: ";
                std::getline(std::cin, input);

                try { localAddress.port = std::stoi(input); }
                catch (...) { localAddress.port = port; }
            }

            // Create the self (1 channel)
            self = enet_host_create(&localAddress, 30, 1, 0, 0);
            if (self == nullptr)
            {
                std::cerr << "An error occurred while trying to create an ENet server, trying alternative port" << std::endl;
                localAddress.port = port + 100;
                self = enet_host_create(&localAddress, 5, 2, 0, 0);
                if (self == nullptr)
                {
                    std::cerr << "Alternative port failed, quitting" << std::endl;
                    return;
                }
            }

            // Connect to the server
            matchmakingHost = enet_host_connect(self, &serverAddress, 2, 0);
            if (matchmakingHost == nullptr)
            {
                std::cerr << "No available peers for initiating an ENet connection." << std::endl;
                return;
            }

            // Wait for the connection to be established
            ENetEvent event;
            if (enet_host_service(self, &event, 5000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
            {
                std::cout << "Successfully connected to matchmaking server!" << "\n\n";
            }
            else
            {
                std::cerr << "Failed to connect to matchmaking server." << std::endl;
                enet_host_destroy(self);
                return;
            }

            std::thread networkThread(&clientSystem::networkLoop, this);
            std::thread inputThread(&clientSystem::userInputLoop, this);

            networkThread.join();
            inputThread.join();

            enet_host_destroy(self);
        }
    };
}