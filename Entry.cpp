#include <iostream>
#include "string"
#include "thread"

#include "enet/enet.h"
#include "PacketHelper.h"

namespace Samurai
{
	class networkSystem
	{
	public:
		mutable bool isClient = true;
        mutable int port = 1000;
        mutable int targetPort = 1000;

		networkSystem(bool IsClient = true) : isClient(IsClient)
		{
		
		}

        virtual void start() = 0;
	};

	class clientSystem : public networkSystem
	{
	public:
        clientSystem()
        {
            isClient = false;
        }

        void start() override
        {		
            ENetHost* self;
            ENetAddress address;

            // Initialize ENet
            if (enet_initialize() != 0) 
            {
                std::cerr << "An error occurred while initializing ENet." << std::endl;
                return;
            }
            atexit(enet_deinitialize);

            // Set up the address for ourselves (host IP and port)
            enet_address_set_host(&address, "0.0.0.0");  // Listen on all available interfaces
            address.port = port;

            // Create the server (maximum 5 connections, 2 channels)
            self = enet_host_create(&address, 5, 2, 0, 0);
            if (self == nullptr)
            {
                std::cerr << "An error occurred while trying to create an ENet server, trying alternative port" << std::endl;
                address.port = port + 100;
                self = enet_host_create(&address, 5, 2, 0, 0);
                if (self == nullptr)
                {
                    std::cerr << "Alternative port failed, quitting" << std::endl;
                    return;
                }
            }

            std::cout << "We are now listening at " << ipToString(address.host) << ":" << address.port << std::endl;

            std::string input;
            std::cout << "Target Port > ";
            std::getline(std::cin, input);

            try
            {
                targetPort = std::stoi(input);
            }
            catch (...) { targetPort = 1000; }

            // connect to target
            ENetAddress targetAddr;
            enet_address_set_host(&targetAddr, "127.0.0.1");
            targetAddr.port = targetPort;
            enet_host_connect(self, &targetAddr, 2, 0);

            // Main server loop
            while (true) 
            {
                ENetEvent event;
                while (enet_host_service(self, &event, 1000) > 0)
                {
                    switch (event.type) 
                    {
                    case ENET_EVENT_TYPE_CONNECT:
                    {
                        if (event.peer->incomingPeerID != -1) // it is an incoming connection
                        {
                            std::cout << "A new client connected from " << ipToString(address.host) << ":" << address.port << std::endl;

                            // send them a welcome message when we connect
                            // create the packet and assign its type
                            Packet packet;
                            packet.type = PROVIDE_WELCOME_MESSAGE;

                            // give it the welcome message
                            appendString(packet.data, "hey partner!");

                            // send it to the new connection
                            sendNow(packet, event.peer);
                        }
                        break;
                    }
                    case ENET_EVENT_TYPE_RECEIVE:
                    {
                        Packet packet = Packet::deserialize((char*)event.packet->data, event.packet->dataLength);
                        size_t offset = 0;

                        switch (packet.type)
                        {
                        case PROVIDE_WELCOME_MESSAGE:
                        {
                            std::string welcomeMessage = extractString(packet.data, offset); // we know its a welcome message so we can expect to find a string
                            std::cout << ipToString(event.peer->address.host) << ":" << event.peer->address.port << " has welcomed us: " << welcomeMessage << "\n\n";

                            sendQuickResponseNow(event.peer, ACK_WELCOME_MESSAGE); // send a quick response (no data) acknoledging that we recieved it
                            break;
                        }
                        case PROVIDE_QUICK_RESPONSE:
                        {
                            QuickResponseType type = (QuickResponseType)extractInt(packet.data, offset); // quick responses should always have their type
                            switch (type)
                            {
                            case ACK_WELCOME_MESSAGE:
                            {
                                std::cout << ipToString(event.peer->address.host) << ":" << event.peer->address.port << " has recieved our welcome message!" << "\n\n";
                            }
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
                        std::cout << "A peer has disconnected." << std::endl;
                        break;
                    }
                }

                // Sleep for 1 second
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            enet_host_destroy(self);
		}
	};
}

int main(int ArgumentCount, char* Arguments[])
{
    Samurai::networkSystem* System = nullptr;

    System = new Samurai::clientSystem;
    System->isClient = true;
    System->start();
}