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
                std::cerr << "An error occurred while trying to create an ENet server." << std::endl;
                return;
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
                        std::cout << "A new self connected from " << ipToString(event.peer->address.host) << std::endl;
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
                                bool advertise = extractInt(incoming.data, offset) <= 1;
                                std::cout << "Received REQUEST_CREATE_SESSION: Max Players = " << maxPlayers << ", Advertise = " << (advertise ? "Yes" : "No") << std::endl;

                                playerConnectionInfo ConnectionInfo = playerConnectionInfo(event.peer->address);
                                ConnectionInfo.connection = event.peer;
                                sessionData NewSession = sessionData(ConnectionInfo, maxPlayers);
                                NewSession.waitForHost = true; // host hasnt joined yet so dont destroy it cuz its empty
                                NewSession.advertise = advertise;
                                sessionList.push_back(NewSession);

                                std::cout << "Successfully created session, ID: " << NewSession.id << "\n\n";

                                // send the session id back to the host so they can request to join the id
                                Packet packet;
                                packet.type = PROVIDE_SESSION_DETAILS;

                                appendInt(packet.data, NewSession.id); // session id
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
                                    sendRequestDeniedNow(event.peer, INVALID_SESSION_ID);
                                    std::cerr << "Failed to join session.\n\n";
                                    continue;
                                    break;
                                }

                                sessionData* Data = &sessionList[requestedSessionIndex];

                                if (Data->host.matches(event.peer->address)) // this player created this session, join no matter
                                {
                                    Data->playerList.push_back(Data->host);
                                    success = true;
                                    sendQuickResponseNow(event.peer, SESSION_JOINED_SUCCESS);
                                }
                                else // this is someone elses session, check if they can join
                                {
                                    if (Data->joinability == allowAny)
                                    {
                                        playerConnectionInfo connectionInfo = playerConnectionInfo(event.peer->address);
                                        connectionInfo.connection = event.peer;

                                        Packet packet;
                                        packet.type = PROVIDE_JOINER_INFO;
                                        appendAddress(packet.data, connectionInfo.address);
                                        sendBroadcastNow(*Data, packet); // inform all existing players about the new player

                                        Data->playerList.push_back(connectionInfo);
                                        sendQuickResponseNow(event.peer, SESSION_JOINED_SUCCESS);
                                        success = true;
                                    }
                                    else
                                    {
                                        sendRequestDeniedNow(event.peer, JOIN_NOT_ALLOWED);
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
                        std::cout << "self disconnected." << "\n\n";
                        break;
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

        clientSystem()
        {
            isClient = true;
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

                    std::cout << "Requesting session creation with details:\nAdvertise: Yes\nMax Players: 30\n\n";
                    Packet packet;
                    packet.type = REQUEST_CREATE_SESSION;
                    appendInt(packet.data, 30); // max players
                    appendInt(packet.data, true); // advertise
                    sendNow(packet, matchmakingHost);
                    state = waitingToHostNewSession;
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
                else 
                    std::cout << "Unknown command.\n";
            }
        }

        void checkConnections()
        {
            for (int infoIndex = 0; infoIndex < knownPlayerInfos.size(); infoIndex++)
            {
                Matchmaking::playerConnectionInfo& info = knownPlayerInfos[infoIndex];

                if (info.isPlayer)
                {
                    if (info.connected && info.shouldDisconnect)
                    {
                        std::cout << "Disconnecting from player:" << ipToString(info.address.host) << ":" << info.address.port << "\n";
                        knownPlayerInfos.erase(knownPlayerInfos.begin() + infoIndex);
                        info.disconnect();
                        continue;
                    }
                    else if (!info.connected && !info.shouldDisconnect)
                    {
                        std::cout << "Connecting to player:" << ipToString(info.address.host) << ":" << info.address.port << "\n";
                        info.connection = enet_host_connect(self, &info.address, 2, 0);
                        continue;
                    }
                }
            }
        }

        void networkLoop()
        {
            // Main self loop
            while (running)
            {
                // Handle events (incoming messages, disconnects, etc.)
                ENetEvent event;
                while (enet_host_service(self, &event, 1000) > 0)
                {
                    switch (event.type)
                    {
                    case ENET_EVENT_TYPE_CONNECT:
                    {
                        bool isPlayer = false;

                        for (Matchmaking::playerConnectionInfo& info : knownPlayerInfos)
                        {
                            if (info.matches(event.peer->address))
                            {
                                info.isPlayer = true; isPlayer = true;
                                info.connected = true;
                                info.connection = event.peer;
                                std::cout << "Successfully connected to player at " << ipToString(info.address.host) << ":" << info.address.port << "!\n";
                                break;
                            }
                        }

                        if (!isPlayer)
                            std::cout << "Successfully connected to matchmaking server!" << std::endl;
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
                            case waitingToHostNewSession:
                            {
                                sessionId = extractInt(packet.data, offset); // session id
                                int count = extractInt(packet.data, offset); // player count

                                std::cout << "Joining session with details:\nID: " << sessionId << "\nPlayer Count: " << count << "\n\n";
                                Packet packet;
                                packet.type = REQUEST_JOIN_SESSION;
                                appendInt(packet.data, sessionId);
                                sendNow(packet, matchmakingHost);
                                state = joiningSession;
                                break;
                            }
                            case waitingForSessionInfo:
                            {
                                sessionId = extractInt(packet.data, offset); // session id
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
                            Packet packet = Packet::deserialize((char*)event.packet->data, event.packet->dataLength);
                            size_t offset = 0;
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
                            case PLAYER_JOINED:
                            {
                                std::cout << "Got quick response: Failed to join session!\n\n";
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
                            Packet packet = Packet::deserialize((char*)event.packet->data, event.packet->dataLength);
                            size_t offset = 0;
                            ENetAddress addr = extractAddress(packet.data, offset);

                            Matchmaking::playerConnectionInfo newInfo(addr);
                            newInfo.isPlayer = true;

                            ENetPeer* newPeer = enet_host_connect(self, &newInfo.address, 2, 0);
                            std::cout << "Connecting to player at " << ipToString(newInfo.address.host) << ":" << newInfo.address.port << std::endl;

                            if (!newPeer)
                            {
                                std::cerr << "Failed to initiate connection to player at " << ipToString(newInfo.address.host) << ":" << newInfo.address.port << std::endl;
                            }

                            knownPlayerInfos.push_back(newInfo);

                            break;
                        }
                        case PROVIDE_QUICK_RESPONSE_MESSAGE:
                        {
                            Packet packet = Packet::deserialize((char*)event.packet->data, event.packet->dataLength);
                            size_t offset = 0;
                            std::string message = extractString(packet.data, offset);
                            std::cout << "Got quick response message: " << message << "\n\n";
                            break;
                        }
                        case REQUEST_DENIED:
                        {
                            Packet packet = Packet::deserialize((char*)event.packet->data, event.packet->dataLength);
                            size_t offset = 0;
                            PacketDeniedReason Type = (PacketDeniedReason)extractInt(packet.data, offset);

                            switch (Type)
                            {
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
                                std::cout << "A request was denied, reason type: " << Type << "\n\n";
                                break;
                            }
                            }
                            break;
                        }
                        case P2P_CHAT_MESSAGE:
                        {
                            std::string message = extractString(packet.data, offset);
                            std::cout << ipToString(event.peer->address.host) << ":" << event.peer->address.port << " says: " << message << "\n\n";
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
                    case REQUEST_DENIED_MESSAGE:
                    {
                        Packet packet = Packet::deserialize((char*)event.packet->data, event.packet->dataLength);
                        size_t offset = 0;
                        std::string message = extractString(packet.data, offset);
                        std::cout << "A response was denied, reason: " << message << "\n\n";
                        break;
                    }
                    case ENET_EVENT_TYPE_DISCONNECT:
                        std::cout << "Matchmaking server disconnected." << std::endl;
                        return;
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
                std::cerr << "An error occurred while trying to create the ENet self." << std::endl;
                return;
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
                std::cout << "Successfully connected to server!" << "\n\n";
            }
            else
            {
                std::cerr << "Failed to connect to server." << std::endl;
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