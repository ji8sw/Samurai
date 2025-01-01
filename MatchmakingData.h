#pragma once
#include <random>

namespace Samurai
{
    namespace Matchmaking
    {
        enum natType
        {
            Open,
            Moderate,
            Strict
        };

        enum clientJoinState
        {
            inSession,
            noSession,
            waitingToHostNewSession, // similair to waitingForSessionInfo but for our own sessions
            waitingForSessionInfo,
            joiningSession
        };

        enum joinabilityType
        {
            allowAny,
            friendsOnly,
            inviteOnly
        };

        struct playerConnectionInfo
        {
            ENetPeer* connection = nullptr;
            ENetAddress address;
            bool isHost = false;

            // used in client settings when connecting to other players
            bool isPlayer = false;
            bool connected = false;
            bool shouldDisconnect = false;

            playerConnectionInfo(const char* Address, enet_uint16 Port)
            {
                enet_address_set_host(&address, "0.0.0.0");
                address.port = Port;
            }

            playerConnectionInfo(ENetAddress addr)
            {
                address = addr;
            }

            playerConnectionInfo() {}

            bool matches(playerConnectionInfo other)
            {
                return address.host == other.address.host && address.port == other.address.port;
            }

            bool matches(ENetAddress other)
            {
                return address.host == other.host && address.port == other.port;
            }

            void disconnect()
            {
                enet_peer_disconnect_now(connection, 0);
            }
        };

        struct sessionData
        {
            short maxPlayers = 30;
            std::vector<playerConnectionInfo> playerList;
            playerConnectionInfo host;
            bool advertise = true; // is this session public? should it be advertised to players looking to join?
            bool waitForHost = true; // session is newly created, has no players because the host hasnt joined yet, dont destroy it
            joinabilityType joinability = allowAny;
            int id = 0;

            bool isFull()
            {
                return playerList.size() == maxPlayers;
            }

            bool shouldBeDestroyed()
            {
                return playerList.size() == 0 && !waitForHost;
            }

            int getHostPid()
            {
                for (int pid = 0; pid < playerList.size(); pid++)
                {
                    if (playerList[pid].address.host == host.address.host)
                        return pid;
                }
            }

            sessionData(playerConnectionInfo Host, short MaxPlayers = 30) : host(Host), maxPlayers(MaxPlayers)
            {
                std::random_device randomDevice;
                std::mt19937 generator(randomDevice());
                std::uniform_int_distribution<int> Distrib(100000, 999999);

                id = Distrib(generator);

                host.isHost = true;
            }
        };
    }
}