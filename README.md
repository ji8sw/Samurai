# Samurai - Matchmaking
The Samurai matchmaking service allows players to create and join sessions, the matchmaking service serves as a middle-man, first, a player creates a session with a max player count, and if it should be advertised, then another player will join the session by asking the matchmaking service if any sessions are available (have free player slots and are advertised), if it is then the server sends info back, the info contains the session ID, which is used to identify and join a session, and a list of player IP addresses and ports, the joiner can then request to join the session via its ID, connecting to every player in the process.

Included is an example P2P chat system, where messages are broadcasted using the console to every connected peer in the session, but the system is highly flexible and can be used to make games or other services, you could take away the console and implement it into a larger system, you could then implement callback functions so your game can implement player joining and leaving, or make a GUI to manage all the running sessions via the server.

## How it works in more detail
Samurai uses ENet, a library that allows you to connect via IP addresses and send data, however the data is sent in raw bytes, but Samurai allows you to create a list of bytes based on types. Samurai has a packet system, a packet contains an `int` for its type, which could be any of:

    enum PacketType
    {
        // Core
        PROVIDE_QUICK_RESPONSE,
        PROVIDE_QUICK_RESPONSE_MESSAGE,

        // Matchmaking
        REQUEST_CREATE_SESSION,
        PROVIDE_SESSION_DETAILS,
        REQUEST_FIND_SESSION,
        PROVIDE_JOINER_INFO,
        REQUEST_JOIN_SESSION,
        REQUEST_SEND_INVITE,
        PROVIDE_INVITE,
        PLAYER_LEFT,

        // P2P
        P2P_CHAT_MESSAGE
    };
and then a list of bytes, the list of bytes can be appended and extracted with included helper functions, these include:
`appendInt / extractInt`
`appendUInt32 / extractUInt32`
`appendUInt16 / extractUInt16`
`appendAddress / extractAddress` (for ENet addresses (IP and Port))
`appendString / extractString`
you can always add more, even combining these previous functions to support a struct (for example Address functions combine UInt32 (for IP) and UInt16 (for port).

Packets can be sent very easily using these functions:
`sendNow` sends packet to a specific peer
`sendBroadcastNow` sends packet to a list of peers
there are some other helper functions for different kinds of packets like quick responses which should contain no data.

It might seem limited, this is because it only contains what is used in Samurai, much more can be added and fondled with.

If you're sending a packet to a peer this is how it would go:

    Packet packet;
    packet.type = P2P_CHAT_MESSAGE;
    appendString(packet.data, "Hey there!");
    sendNow(packet, peer);
that's it, very easy, here's how you would receive it:

    Packet packet = Packet::deserialize((char*)event.packet->data, event.packet->dataLength); // event is received from ENet
    size_t offset = 0;
    
    if (packet.type == P2P_CHAT_MESSAGE)
    {
    	std::cout << "Message Received: " << extractString(packet.data, offset) << std::endl;
    }

you can see all of this in action inside `System.h`, which is where most important logic takes place.
