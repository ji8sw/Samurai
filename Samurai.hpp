#pragma once
#include "enet/enet.h"

#include <iostream>
#include <sstream>
#include <vector>
#include <string>

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
		SESSION_CREATED_SUCCESS,
		REQUEST_FIND_SESSION_BY_ID,
		SESSION_JOINED_SUCCESS,
		SESSION_JOINED_FAILURE,
		SESSION_FIND_FAILURE,
		NOTIFY_LEAVE_SESSION,
		INVALID_SESSION_ID,
		JOIN_NOT_ALLOWED,

		// P2P
		P2P_CHAT_MESSAGE
	};

	struct Packet
	{
		int type;               // Packet identifier
		std::vector<char> data; // Flexible payload
		bool valid = true;      // if the packet was (de)serialized properly

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
			if (length < sizeof(int))
			{
				std::cerr << "Invalid packet size." << std::endl;
				packet.valid = false;
				return packet;
			}

			memcpy(&packet.type, buffer, sizeof(int));
			packet.data.assign(buffer + sizeof(int), buffer + length);

			return packet;
		}

		Packet(int inType = 0, std::vector<char> inData = std::vector<char>()) :
			type(inType),
			data(inData)
		{
		}
	};

	template<typename T>
	static void appendData(std::vector<char>& buffer, T value)
	{
		buffer.insert(buffer.end(), reinterpret_cast<const char*>(&value), reinterpret_cast<const char*>(&value) + sizeof(T));
	}

	template<typename T>
	static T extractData(const std::vector<char>& buffer, size_t& offset)
	{
		if (offset + sizeof(T) > buffer.size())
		{
			std::cerr << "Buffer underflow while extracting data." << std::endl;
			return T();
		}
		T value;
		memcpy(&value, buffer.data() + offset, sizeof(T));
		offset += sizeof(T);
		return value;
	}

	// Append an ENetAddress (enet_uint32) and port (enet_uint16) to a vector<char> used in a Packet
	static void appendAddress(std::vector<char>& buffer, ENetAddress addr)
	{
		appendData<uint32_t>(buffer, addr.host);
		appendData<uint16_t>(buffer, addr.port);
	}

	// Append an ENetAddress (enet_uint32) and port (enet_uint16) to a vector<char> used in a Packet
	static ENetAddress extractAddress(std::vector<char>& buffer, size_t& offset)
	{
		ENetAddress addr;
		addr.host = extractData<uint32_t>(buffer, offset);
		if (addr.host == INVALID_INT)
		{
			std::cerr << "Buffer underflow while extracting uint32 for address." << std::endl;
			return addr;
		}
		addr.port = extractData<uint16_t>(buffer, offset);
		if (addr.port == INVALID_INT)
		{
			std::cerr << "Buffer underflow while extracting uint16 for address." << std::endl;
			return addr;
		}
		return addr;
	}

	// Append a string to a vector<char> used in a Packet
	static void appendString(std::vector<char>& buffer, const std::string& value)
	{
		int length = value.size();
		appendData<int>(buffer, length);
		buffer.insert(buffer.end(), value.begin(), value.end());
	}

	// Extract a string from a vector<char> used in a Packet
	static std::string extractString(const std::vector<char>& buffer, size_t& offset)
	{
		int length = extractData<int>(buffer, offset);
		if (offset + length > buffer.size())
		{
			std::cerr << "Buffer underflow while extracting string." << std::endl;
			return INVALID_STR;
		}
		std::string value(buffer.begin() + offset, buffer.begin() + offset + length);
		offset += length;
		return value;
	}

	static void sendNow(Packet& packet, ENetPeer* Client, ENetPacketFlag Flag = ENET_PACKET_FLAG_RELIABLE)
	{
		if (!Client)
		{
			return;
		}

		auto serializedData = packet.serialize();
		ENetPacket* enetPacket = enet_packet_create(serializedData.data(), serializedData.size(), Flag);
		enet_peer_send(Client, 0, enetPacket);
		enet_host_flush(Client->host);
	}

	static void sendBroadcastNow(std::vector<ENetPeer*> connections, Packet& packet, ENetPacketFlag Flag = ENET_PACKET_FLAG_RELIABLE)
	{
		if (connections.empty())
		{
			return;
		}
		for (ENetPeer* connection : connections)
		{
			if (!connection)
			{
				continue;
			}
			sendNow(packet, connection, Flag);
		}
	}

	static void sendBroadcastQuickResponseMessageNow(std::vector<ENetPeer*> connections, std::string Reason, ENetPacketFlag Flag = ENET_PACKET_FLAG_RELIABLE)
	{
		if (connections.empty())
		{
			return;
		}

		for (ENetPeer* connection : connections)
		{
			if (!connection)
			{
				continue;
			}

			Packet packet;
			packet.type = PROVIDE_QUICK_RESPONSE_MESSAGE;

			appendString(packet.data, Reason);
			sendNow(packet, connection, Flag);
		}
	}

	static std::string IpToString(enet_uint32 IP)
	{
		unsigned char Byte1 = IP & 0xFF;
		unsigned char Byte2 = (IP >> 8) & 0xFF;
		unsigned char Byte3 = (IP >> 16) & 0xFF;
		unsigned char Byte4 = (IP >> 24) & 0xFF;

		std::ostringstream IpString;
		IpString << static_cast<int>(Byte1) << "." << static_cast<int>(Byte2) << "." << static_cast<int>(Byte3) << "." << static_cast<int>(Byte4);

		return IpString.str();
	}

	static bool areAdderessesMatching(ENetAddress addrA, ENetAddress addrB)
	{
		return addrA.host == addrB.host && addrA.port == addrB.port;
	}
}