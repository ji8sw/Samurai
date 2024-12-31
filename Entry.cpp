#include "enet/enet.h"
#include <iostream>
#include "string"
#include "thread"

namespace Samurai
{
	class networkSystem
	{
	public:
		mutable bool isClient = true;

		networkSystem(bool IsClient = true) : isClient(IsClient)
		{
		
		}

        virtual void start() = 0;
	};

	class serverSystem : public networkSystem
	{
	public:
        serverSystem()
        {
            isClient = false;
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
            server = enet_host_create(&address, 32, 2, 0, 0);
            if (server == nullptr) 
            {
                std::cerr << "An error occurred while trying to create an ENet server." << std::endl;
                return;
            }

            std::cout << "Server is now running on port " << address.port << std::endl;

            // Main server loop
            while (true) 
            {
                ENetEvent event;
                while (enet_host_service(server, &event, 1000) > 0) 
                {
                    switch (event.type) 
                    {
                    case ENET_EVENT_TYPE_CONNECT:
                        std::cout << "A new client connected from " << event.peer->address.host << std::endl;
                        break;

                    case ENET_EVENT_TYPE_RECEIVE:
                        std::cout << "Packet received from client: " << event.packet->data << std::endl;
                        // Handle received data
                        enet_packet_destroy(event.packet);
                        break;

                    case ENET_EVENT_TYPE_DISCONNECT:
                        std::cout << "Client disconnected." << std::endl;
                        break;
                    }
                }
            }

            enet_host_destroy(server);
		}
	};

    class clientSystem : public networkSystem
    {
    public:
        clientSystem()
        {
            isClient = true;
        }

        void start() override
        {
            ENetHost* client;
            ENetAddress address;
            ENetPeer* peer;

            // Initialize ENet
            if (enet_initialize() != 0) 
            {
                std::cerr << "An error occurred while initializing ENet." << std::endl;
                return;
            }
            atexit(enet_deinitialize);

            // Set up the server's address (IP and port)
            enet_address_set_host(&address, "127.0.0.1"); // Use your server's IP address here
            address.port = 1000;

            // Create the client (1 channel)
            client = enet_host_create(nullptr, 1, 1, 0, 0);
            if (client == nullptr) 
            {
                std::cerr << "An error occurred while trying to create the ENet client." << std::endl;
                return;
            }

            // Connect to the server
            peer = enet_host_connect(client, &address, 2, 0);
            if (peer == nullptr) 
            {
                std::cerr << "No available peers for initiating an ENet connection." << std::endl;
                return;
            }

            // Wait for the connection to be established
            ENetEvent event;
            if (enet_host_service(client, &event, 5000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) 
            {
                std::cout << "Successfully connected to server!" << std::endl;
            }
            else 
            {
                std::cerr << "Failed to connect to server." << std::endl;
                enet_host_destroy(client);
                return;
            }

            // Main client loop
            while (true) 
            {
                // Handle events (incoming messages, disconnects, etc.)
                while (enet_host_service(client, &event, 1000) > 0) 
                {
                    switch (event.type) 
                    {
                    case ENET_EVENT_TYPE_CONNECT:
                        std::cout << "Successfully connected to server!" << std::endl;
                        break;

                    case ENET_EVENT_TYPE_RECEIVE:
                        std::cout << "Packet received from server: " << event.packet->data << std::endl;
                        // Handle received data
                        enet_packet_destroy(event.packet);
                        break;

                    case ENET_EVENT_TYPE_DISCONNECT:
                        std::cout << "Server disconnected." << std::endl;
                        return;
                    }
                }

                // Send a simple packet to the server every second
                const char* message = "Hello from client!";
                ENetPacket* packet = enet_packet_create(message, strlen(message) + 1, ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(peer, 0, packet);
                enet_host_flush(client);

                // Sleep for 1 second
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            enet_host_destroy(client);
        }
    };
}

int main(int ArgumentCount, char* Arguments[])
{
	Samurai::networkSystem* System = nullptr;

	if (ArgumentCount >= 1)
	{
        for (int ArgumentIndex = 0; ArgumentIndex < ArgumentCount; ArgumentIndex++)
        {
            if (!System && std::string(Arguments[ArgumentIndex]) == "-Server")
            {
                System = new Samurai::serverSystem;
                System->isClient = false;
                System->start();
            }
        }
	}

    if (!System)
    {
        System = new Samurai::clientSystem;
        System->isClient = true;
        System->start();
    }

	return 1;
}