#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <any>

#include "enet/enet.h"
#include "Systems.h"

int main(int ArgumentCount, char* Arguments[])
{
	Samurai::networkSystem* System = nullptr;
    int Port = 1000;
    bool PortCustomized = false;

	if (ArgumentCount >= 1)
	{
        for (int ArgumentIndex = 0; ArgumentIndex < ArgumentCount; ArgumentIndex++)
        {
            if (!System && std::string(Arguments[ArgumentIndex]) == "-Server")
            {
                System = new Samurai::serverSystem;
                System->isClient = false;
            }
            else if (std::string(Arguments[ArgumentIndex]) == "-Port" && ArgumentCount > (ArgumentIndex + 1))
            {
                try
                {
                    Port = std::stoi(Arguments[ArgumentIndex + 1]);
                    if (Port >= USHRT_MAX || Port <= 0) Port = 1000;
                    PortCustomized = true;
                } catch (...) {}
            }
        }
	}

    if (!System)
    {
        System = new Samurai::clientSystem;
        System->isClient = true;
        System->portCustomized = PortCustomized; // determines whether the client console should prompt asking for a custom port
    }

    if (System)
    {
        System->port = Port;
        System->start();
    }
}