#pragma once
#include <vector>
#include <iostream>
#include <sstream>

#define INVALID_INT -1
#define INVALID_STR "INVALID_STR"

namespace Samurai
{
    enum PacketType// used in cases when you want to send data, on the receiving side you can use the PacketType to identify what data should be expected in the receieved packet
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

    enum QuickResponseType  // used in cases when you dont want to send extra data, helper functions exist for sending these easily
    {
        // Matchmaking
        SESSION_CREATED_SUCCESS,
        SESSION_JOINED_SUCCESS,
        SESSION_JOINED_FAILURE,
        SESSION_FIND_FAILURE,
        NOTIFY_LEAVE_SESSION,
        INVALID_SESSION_ID,
        JOIN_NOT_ALLOWED
    };

    struct Packet
    {
        int type; // Packet identifier
        std::vector<char> data; // Flexible payload
        bool valid = true; // if the packet was (de)serialized properly

        // turn outgoing data into a byte array which is compatible with enet
        std::vector<char> serialize() const
        {
            std::vector<char> buffer(sizeof(int) + data.size());
            memcpy(buffer.data(), &type, sizeof(int));
            memcpy(buffer.data() + sizeof(int), data.data(), data.size());
            return buffer;
        }

        // turn recieved byte array back into data
        static Packet deserialize(const char* buffer, size_t length)
        {
            Packet packet;
            if (length < sizeof(int)) { std::cerr << "Invalid packet size." << std::endl; packet.valid = false; return packet; }

            memcpy(&packet.type, buffer, sizeof(int));
            packet.data.assign(buffer + sizeof(int), buffer + length);

            return packet;
        }
    };

    // Append an integer to a vector<char> used in a Packet
    void appendInt(std::vector<char>& buffer, int value) 
    {
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&value),
            reinterpret_cast<const char*>(&value) + sizeof(int));
    }

    // Extract an integer from a vector<char> used in a Packet
    int extractInt(const std::vector<char>& buffer, size_t& offset) 
    {
        if (offset + sizeof(int) > buffer.size()) { std::cerr << "Buffer underflow while extracting int." << std::endl; return INVALID_INT; }
        int value;
        memcpy(&value, buffer.data() + offset, sizeof(int));
        offset += sizeof(int);
        return value;
    }

    // Append IP address (as enet_uint32)
    void appendUInt32(std::vector<char>& buffer, enet_uint32 value)
    {
        buffer.insert(buffer.end(),
            reinterpret_cast<const char*>(&value),
            reinterpret_cast<const char*>(&value) + sizeof(enet_uint32));
    }

    // Extract a uint32 from a vector<char> used in a Packet
    unsigned int extractUInt32(const std::vector<char>& buffer, size_t& offset)
    {
        if (offset + sizeof(unsigned int) > buffer.size()) { std::cerr << "Buffer underflow while extracting unsigned int." << std::endl; return INVALID_INT; }
        unsigned int value;
        memcpy(&value, buffer.data() + offset, sizeof(unsigned int));
        offset += sizeof(unsigned int);
        return value;
    }

    // Append a uint16 to a vector<char> used in a Packet
    void appendUInt16(std::vector<char>& buffer, enet_uint16 value)
    {
        buffer.insert(buffer.end(),
            reinterpret_cast<const char*>(&value),
            reinterpret_cast<const char*>(&value) + sizeof(enet_uint16));
    }

    // Extract a uint16_t from a vector<char> used in a Packet
    unsigned short extractUInt16(const std::vector<char>& buffer, size_t& offset)
    {
        if (offset + sizeof(unsigned short) > buffer.size()) { std::cerr << "Buffer underflow while extracting unsigned short." << std::endl; return INVALID_INT; }
        unsigned short value;
        memcpy(&value, buffer.data() + offset, sizeof(unsigned short));
        offset += sizeof(unsigned short);
        return value;
    }

    // Append an ENetAddress (enet_uint32) and port (enet_uint16) to a vector<char> used in a Packet
    void appendAddress(std::vector<char>& buffer, ENetAddress addr)
    {
        appendUInt32(buffer, addr.host);
        appendUInt16(buffer, addr.port);
    }

    // Append an ENetAddress (enet_uint32) and port (enet_uint16) to a vector<char> used in a Packet
    ENetAddress extractAddress(std::vector<char>& buffer, size_t& offset)
    {
        ENetAddress addr;
        addr.host = extractUInt32(buffer, offset);
        if (addr.host == INVALID_INT) { std::cerr << "Buffer underflow while extracting uint32 for address." << std::endl; return addr; }
        addr.port = extractUInt16(buffer, offset);
        if (addr.port == INVALID_INT) { std::cerr << "Buffer underflow while extracting uint16 for address." << std::endl; return addr; }
        return addr;
    }

    // Append a string to a vector<char> used in a Packet
    void appendString(std::vector<char>& buffer, const std::string& value) 
    {
        int length = value.size();
        appendInt(buffer, length);
        buffer.insert(buffer.end(), value.begin(), value.end());
    }

    // Extract a string from a vector<char> used in a Packet
    std::string extractString(const std::vector<char>& buffer, size_t& offset) 
    {
        int length = extractInt(buffer, offset);
        if (offset + length > buffer.size()) { std::cerr << "Buffer underflow while extracting string." << std::endl; return INVALID_STR; }
        std::string value(buffer.begin() + offset, buffer.begin() + offset + length);
        offset += length;
        return value;
    }

    void sendNow(Packet& packet, ENetPeer* Client, ENetPacketFlag Flag = ENET_PACKET_FLAG_RELIABLE)
    {
        if (!Client) return;

        auto serializedData = packet.serialize();
        ENetPacket* enetPacket = enet_packet_create(serializedData.data(), serializedData.size(), Flag);
        enet_peer_send(Client, 0, enetPacket);
        enet_host_flush(Client->host);
    }

    void sendBroadcastNow(Matchmaking::sessionData session, Packet& packet, ENetPacketFlag Flag = ENET_PACKET_FLAG_RELIABLE)
    {
        if (session.playerList.empty()) return;
        for (Matchmaking::playerConnectionInfo connection : session.playerList)
        {
            if (!connection.connection) continue;
            sendNow(packet, connection.connection, Flag);
        }
    }

    void sendBroadcastNow(std::vector<Matchmaking::playerConnectionInfo> infos, Packet& packet, ENetPacketFlag Flag = ENET_PACKET_FLAG_RELIABLE)
    {
        if (infos.empty()) return;
        for (Matchmaking::playerConnectionInfo connection : infos)
        {
            if (!connection.connection) continue;
            sendNow(packet, connection.connection, Flag);
        }
    }

    void sendQuickResponseNow(ENetPeer* Client, QuickResponseType Reason, ENetPacketFlag Flag = ENET_PACKET_FLAG_RELIABLE)
    {
        Packet packet;
        packet.type = PROVIDE_QUICK_RESPONSE;

        appendInt(packet.data, Reason);
        sendNow(packet, Client, Flag);
    }

    void sendBroadcastQuickResponseNow(Matchmaking::sessionData session, QuickResponseType Reason, ENetPacketFlag Flag = ENET_PACKET_FLAG_RELIABLE)
    {
        if (session.playerList.empty()) return;

        for (Matchmaking::playerConnectionInfo connection : session.playerList)
        {
            if (!connection.connection) continue;

            Packet packet;
            packet.type = PROVIDE_QUICK_RESPONSE;

            appendInt(packet.data, Reason);
            sendNow(packet, connection.connection, Flag);
        }
    }

    void sendBroadcastQuickResponseMessageNow(Matchmaking::sessionData session, std::string Reason, ENetPacketFlag Flag = ENET_PACKET_FLAG_RELIABLE)
    {
        if (session.playerList.empty()) return;

        for (Matchmaking::playerConnectionInfo connection : session.playerList)
        {
            if (!connection.connection) continue;

            Packet packet;
            packet.type = PROVIDE_QUICK_RESPONSE_MESSAGE;

            appendString(packet.data, Reason);
            sendNow(packet, connection.connection, Flag);
        }
    }

    void sendBroadcastQuickResponseNow(std::vector<ENetPeer*> connections, QuickResponseType Reason, ENetPacketFlag Flag = ENET_PACKET_FLAG_RELIABLE)
    {
        if (connections.empty()) return;

        for (ENetPeer* connection : connections)
        {
            if (!connection) continue;

            Packet packet;
            packet.type = PROVIDE_QUICK_RESPONSE;

            appendInt(packet.data, Reason);
            sendNow(packet, connection, Flag);
        }
    }

    void sendBroadcastQuickResponseMessageNow(std::vector<ENetPeer*> connections, std::string Reason, ENetPacketFlag Flag = ENET_PACKET_FLAG_RELIABLE)
    {
        if (connections.empty()) return;

        for (ENetPeer* connection : connections)
        {
            if (!connection) continue;

            Packet packet;
            packet.type = PROVIDE_QUICK_RESPONSE_MESSAGE;

            appendString(packet.data, Reason);
            sendNow(packet, connection, Flag);
        }
    }

    std::string ipToString(enet_uint32 ip) 
    {
        // bitshift each byte then format them into a string

        unsigned char byte1 = (ip >> 24) & 0xFF;
        unsigned char byte2 = (ip >> 16) & 0xFF;
        unsigned char byte3 = (ip >> 8) & 0xFF;
        unsigned char byte4 = ip & 0xFF;

        std::ostringstream ipString;
        ipString << static_cast<int>(byte4) << "."
            << static_cast<int>(byte2) << "."
            << static_cast<int>(byte3) << "."
            << static_cast<int>(byte1);

        return ipString.str();
    }

    bool areAdderessesMatching(ENetAddress addrA, ENetAddress addrB)
    {
        return addrA.host == addrB.host && addrA.host == addrB.port;
    }
}