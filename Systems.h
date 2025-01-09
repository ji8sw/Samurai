#pragma once
#include <vector>
#include <iostream>
#include <thread>
#include <deque>

#include "enet/enet.h"
#include "MatchmakingData.h"
#include "PacketHelper.h"
#include "GUIHelper.h"

#define MAX_CONNECTION_ATTEMPTS 5

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
                if (areAdderessesMatching(allConnections[index]->address, addr)) return index;

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
                                        appendData<size_t>(packet.data, Data->playerList.size()); // player count, we know theres no players to connect to
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
                                        size_t index = Data->inviteList.size();
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
                                                appendData<size_t>(packet.data, Data->playerList.size()); // player count, we know theres no players to connect to
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
                                        if (targetSessionIndex != INVALID_INT)
                                            targetSessionId = sessionList[targetSessionIndex].id;

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
            Samurai::GUI::cleanup();
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
        std::atomic<bool> connectedToMatchmaking = false;
        std::atomic<bool> localServerCreated = false;
        clientJoinState state = noSession;
        std::atomic<int> sessionId = 0;
        std::atomic<bool> isHost = true;
        std::vector<Matchmaking::playerConnectionInfo> knownPlayerInfos;
        std::atomic<bool> running = true;

        std::atomic<bool> guiInitialized = false;
        std::deque<std::string> chatHistory;
        std::string chatInput = "";
        std::string localName = "Guest";

        std::vector<int> inviteIds; // list of session ids we have been invited to

        clientSystem()
        {
            isClient = true;
        }

        void tryCreateLocalServer()
        {
            // self address (IP and port)
            ENetAddress localAddress;
            localAddress.host = ENET_HOST_ANY;
            localAddress.port = port;

            // Create a local server which will send and recieve packets
            int tries = MAX_CONNECTION_ATTEMPTS - 1;
            while (tries--) // try to create an ENet server 5 times
            {
                self = enet_host_create(&localAddress, 5, 2, 0, 0);
                if (self == nullptr)
                    localAddress.port++; // try again on another port
                else
                {
                    localServerCreated = true;
                    break; // server is created, no need to keep trying
                }
            }
        }

        void tryConnectToMatchmaking()
        {
            // servers address (IP and port)
            ENetAddress serverAddress;
            enet_address_set_host(&serverAddress, "127.0.0.1");
            serverAddress.port = 1000;

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
                connectedToMatchmaking = true;
            }
            else
            {
                std::cerr << "Failed to connect to matchmaking server." << std::endl;
                enet_host_destroy(self);
                return;
            }
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

                while (!knownPlayerInfos.empty()); // wait for all connections to be discontinued

                Packet leaveMessage;
                leaveMessage.type = NOTIFY_LEAVE_SESSION;
                sendQuickResponseNow(matchmakingHost, NOTIFY_LEAVE_SESSION);
            }
        }

        // calls leaveSession(), disconnects from the matchmaker, then handles cleaning up the local server and imgui
        void disconnectAndCleanup()
        {
            leaveSession();
            enet_peer_disconnect_now(matchmakingHost, 0);
            enet_host_destroy(self);
            Samurai::GUI::cleanup();
        }

        std::string stateToString()
        {
            switch (state)
            {
                case inSession:
                    return "In Session";
                    break;
                default:
                case noSession:
                    return "No Session";
                    break;
                case waitingForSessionInfo:
                    return "Waiting for session details";
                    break;
                case joiningSession:
                    return "Joining session";
                    break;
            }
        }

        void guiLoop()
        {
            while (running)
            {
                // Create a window and initialize glfw ready for the system to render to
                if (!guiInitialized && !Samurai::GUI::initialize())
                {
                    std::cout << "Failed to initialize GUI..." << std::endl;
                    return;
                }
                guiInitialized = true;
                if (!Samurai::GUI::standardFrameStart()) { running = false; return; }

                if (!connectedToMatchmaking)
                {
                    ImGui::Begin("Loading...");
                    ImGui::Text("Connecting to matchmaking...");
                    ImGui::End();
                    tryConnectToMatchmaking();
                    Samurai::GUI::standardFrameEnd();
                    continue;
                }

                ImGui::Begin("Connection Details");

                ImGui::Text(std::string("Current State: " + stateToString()).c_str());

                if (state == inSession)
                {
                    ImGui::Separator();
                    ImGui::Text("Current Session Details");

                    std::string sessionIdStr = std::to_string(sessionId);
                    ImGui::Text(std::string("Session ID: " + sessionIdStr).c_str());

                    std::string isHostStr = isHost == true ? "Yes" : "No";
                    ImGui::Text(std::string("Is Host: " + isHostStr).c_str());

                    std::string playerCountStr = std::to_string(knownPlayerInfos.size() + 1); // add one to include ourselves
                    ImGui::Text(std::string("Players: " + playerCountStr).c_str());

                    if (state == inSession && ImGui::Button("Leave Session"))
                    {
                        leaveSession();
                        std::cout << "Leaving session...\n\n";
                    }
                }
                else if (state == noSession)
                {
                    ImGui::Separator();

                    ImGui::Text("Name");
                    ImGui::SameLine();
                    ImGui::InputText("##nameinp", &localName);

                    if (ImGui::Button("Find A Session"))
                    {
                        std::cout << "Requesting session join info\n\n";
                        Packet packet;
                        packet.type = REQUEST_FIND_SESSION;
                        sendNow(packet, matchmakingHost);
                        state = waitingForSessionInfo;
                    }

                    ImGui::Separator();

                    static int maxPlayers = 30;
                    ImGui::Text("Max Players"); ImGui::SameLine();
                    ImGui::InputInt("##maxplys", &maxPlayers);

                    static int joinability = (int)allowAny;
                    ImGui::Text("Joinability"); ImGui::SameLine();
                    ImGui::Combo("##joinability", &joinability, joinabilityStrings, IM_ARRAYSIZE(joinabilityStrings));

                    if (ImGui::Button("Create A Session"))
                    {
                        std::cout << "Requesting session creation.\n\n";
                        Packet packet;
                        packet.type = REQUEST_CREATE_SESSION;
                        appendInt(packet.data, maxPlayers);
                        appendInt(packet.data, joinability);
                        sendNow(packet, matchmakingHost);
                        state = waitingForSessionInfo;
                    }
                }

                ImGui::Separator();
                if (ImGui::Button("Quit"))
                {
                    if (state == inSession)
                    {
                        leaveSession();
                        std::cout << "Leaving session...\n\n";

                        while (state == inSession) {}

                        disconnectAndCleanup();
                    }

                    running = false;
                }

                ImGui::End();

                ImGui::Begin("Chat");

                if (state == inSession)
                {
                    ImGui::InputText("##chatinput", &chatInput); ImGui::SameLine(); if (ImGui::Button("Send") && !chatInput.empty())
                    {
                        Packet packet;
                        packet.type = P2P_CHAT_MESSAGE;
                        appendString(packet.data, chatInput);
                        sendBroadcastNow(knownPlayerInfos, packet);

                        chatHistory.push_front("Me: " + chatInput);
                        if (chatHistory.size() >= 15)
                        {
                            chatHistory.erase(chatHistory.end());
                        }
                    }

                    for (std::string chat : chatHistory)
                    {
                        ImGui::Text(chat.c_str());
                    }
                }
                else ImGui::Text("Join a session to send messages!");

                ImGui::End();

                ImGui::Begin("Invites");

                static std::string inviteIPInput;
                static int invitePortInput;

                ImGui::Text("IP Address");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.0f);
                ImGui::InputText("##ip", &inviteIPInput);
                ImGui::SameLine();
                ImGui::Text("Port");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.0f);
                ImGui::InputInt("##port", &invitePortInput);
                ImGui::SameLine();
                if (ImGui::Button("Send"))
                {
                    ENetAddress target;
                    if (enet_address_set_host_ip(&target, inviteIPInput.c_str()) == 0)
                    {
                        target.port = (enet_uint16)invitePortInput;
                        if (target.port >= 0 && target.port <= USHRT_MAX)
                        {
                            Packet packet;
                            packet.type = REQUEST_SEND_INVITE;
                            appendAddress(packet.data, target);
                            sendNow(packet, matchmakingHost);
                        }
                    }
                }

                if (!inviteIds.empty())
                {
                    size_t index = inviteIds.size();
                    while (index--)
                    {
                        std::string inviteIdStr = std::to_string(inviteIds[index]);
                        ImGui::Text(std::string("You have been invited to #" + inviteIdStr).c_str());
                        ImGui::SameLine();
                        if (ImGui::Button(std::string("Join ##" + inviteIdStr).c_str()))
                        {
                            leaveSession();
                            std::cout << "Leaving session...\n\n";

                            std::cout << "Requesting session join info\n\n";
                            Packet packet;
                            packet.type = REQUEST_FIND_SESSION_BY_ID;
                            appendInt(packet.data, inviteIds[index]);
                            sendNow(packet, matchmakingHost);
                            state = waitingForSessionInfo;

                            // remove invite from list
                            inviteIds.erase(inviteIds.begin() + index);
                        }
                    }
                }
                else ImGui::Text("You haven't received any invites.");

                ImGui::End();

                Samurai::GUI::standardFrameEnd();
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
                if (!connectedToMatchmaking) continue; // wait for gui to connect us...

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
                                        
                                        // provide them with my name
                                        Packet packet;
                                        packet.type = P2P_PROVIDE_NAME;
                                        appendString(packet.data, localName);
                                        sendNow(packet, event.peer);
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
                                std::cout << "1\n";
                                sessionId = extractInt(packet.data, offset); // session id
                                std::cout << "2\n";
                                isHost = extractInt(packet.data, offset); // am i the host of this session, todo: add extractBool
                                std::cout << "3\n";
                                size_t count = extractData<size_t>(packet.data, offset); // player count
                                std::cout << "4\n";
                                int hostId = extractInt(packet.data, offset); // host id
                                std::cout << "5\n";

                                for (int playerIndex = 0; playerIndex < count; playerIndex++)
                                {
                                    Matchmaking::playerConnectionInfo Info(extractAddress(packet.data, offset));
                                    Info.isPlayer = true;
                                    Info.connected = false;

                                    knownPlayerInfos.push_back(Info);
                                }

                                std::cout << "Found connection info for " << knownPlayerInfos.size() << "/" << count << " players.\n";

                                std::cout << "Joining session with details:\nID: " << sessionId << "\nPlayer Count: " << count << "\n\n";
                                Packet packet;
                                packet.type = REQUEST_JOIN_SESSION;
                                std::cout << "joining: " << sessionId << "\n";
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
                            // sent by the server to inform players that a new player is joining and how to connect to them.
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
                            
                            // get name of the sender
                            std::string name = ipToString(event.peer->address.host);
                            int index = playerIndexByAddress(event.peer->address);
                            if (index != INVALID_INT)
                            {
                                name = knownPlayerInfos[index].name;
                            }

                            // add message to list of messages
                            std::string messageFormatted = name + ": " + message;
                            chatHistory.push_front(messageFormatted);
                            if (chatHistory.size() >= 15)
                            {
                                chatHistory.erase(chatHistory.end());
                            }
                            break;
                        }
                        case PROVIDE_INVITE:
                        {
                            // add invite to list of invites
                            int invitedToSessionId = extractInt(packet.data, offset);
                            inviteIds.push_back(invitedToSessionId);
                            std::cout << "Someone has sent you an invite, session ID: " << invitedToSessionId << "\n\n";
                            break;
                        }
                        case P2P_PROVIDE_NAME:
                        {
                            std::string name = extractString(packet.data, offset);
                            int index = playerIndexByAddress(event.peer->address);
                            if (index != INVALID_INT)
                            {
                                knownPlayerInfos[index].name = name;
                            }
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

            // the local server which will communicate with the matchmaking server
            tryCreateLocalServer();

            std::thread networkThread(&clientSystem::networkLoop, this);
            std::thread inputThread(&clientSystem::guiLoop, this);

            networkThread.join(); // reached once networkThread stops looping
            inputThread.join(); // reached once inputThread stops looping

            disconnectAndCleanup();
        }
    };
}