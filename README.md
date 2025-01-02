# Samurai
is a basic networking backend in C++, it uses ENet, for a better example check the [matchmaking branch](https://github.com/ji8sw/Samurai/tree/matchmaking)

## How it works in more detail
Samurai uses ENet, a library that allows you to connect via IP addresses and send data, however the data is sent in raw bytes, but Samurai allows you to create a list of bytes based on types. Samurai has a packet system, a packet contains an `int` for its type, which could be any of:

    enum PacketType
    {
        // Core
        PROVIDE_QUICK_RESPONSE,
        PROVIDE_QUICK_RESPONSE_MESSAGE,

        // Example
        PROVIDE_WELCOME_MESSAGE
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

It might seem limited, this is because it only contains what is used in the provided example, much more can be added and fondled with.

If you're sending a packet to a peer this is how it would go:

    Packet packet;
    packet.type = PROVIDE_WELCOME_MESSAGE;
    appendString(packet.data, "Hey there!");
    sendNow(packet, peer);
that's it, very easy, here's how you would receive it:

    Packet packet = Packet::deserialize((char*)event.packet->data, event.packet->dataLength); // event is received from ENet
    size_t offset = 0;
    
    if (packet.type == PROVIDE_WELCOME_MESSAGE)
    {
    	std::cout << "Welcome message received: " << extractString(packet.data, offset) << std::endl;
    }