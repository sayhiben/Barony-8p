/*-------------------------------------------------------------------------------

	BARONY
	File: level_load_sync.cpp
	Desc: level-load authority and recovery helpers

	Copyright 2013-2016 (c) Turning Wheel LLC, all rights reserved.
	See LICENSE for details.

-------------------------------------------------------------------------------*/

#include "level_load_sync.hpp"

#include "draw.hpp"
#ifdef EDITOR
#include "editor.hpp"
#endif
#include "files.hpp"
#include "game.hpp"
#include "net.hpp"
#include "paths.hpp"
#include "player.hpp"

#include <vector>

namespace LevelLoadSync
{
namespace
{
	static constexpr int LEVEL_CHANGE_PACKET_BASE_LEN = 15;
	static constexpr Uint8 LEVEL_CHANGE_PACKET_EXTRA_VERSION_1 = 1;
	static constexpr Uint8 LEVEL_CHANGE_PACKET_EXTRA_VERSION_2 = 2;
	static constexpr int LEVEL_CHANGE_PACKET_EXTRA_SIZE_V1 = 1 + sizeof(Uint16) + sizeof(Uint32);
	static constexpr int LEVEL_CHANGE_PACKET_EXTRA_SIZE_V2 = LEVEL_CHANGE_PACKET_EXTRA_SIZE_V1 + sizeof(Uint32);

	struct AuthorityState
	{
		Uint16 playerMask = 0;
		bool tileChecksumValid = false;
		Uint32 tileChecksum = 0;
		bool entityChecksumValid = false;
		Uint32 entityChecksum = 0;
	};

	constexpr Uint8 kMapSnapshotPacketVersion = 1;
	constexpr int kMapSnapshotChunkHeaderSize = 28;
	constexpr int kMapSnapshotChunkPayloadMax = 1800;

	struct PendingMapSnapshotReceive
	{
		bool active = false;
		Uint8 level = 0;
		bool secret = false;
		Uint32 seed = 0;
		Uint16 transferId = 0;
		Uint16 chunkCount = 0;
		Uint16 receivedChunkCount = 0;
		Uint32 totalBytes = 0;
		Uint32 checksum = 0;
		std::vector<Uint8> bytes;
		std::vector<Uint8> receivedChunks;
	};

	AuthorityState g_authorityState;
	PendingMapSnapshotReceive g_pendingMapSnapshotReceive;
	bool g_mapSnapshotRecoveryRequested = false;
	Uint16 g_mapSnapshotTransferId[MAXPLAYERS] = { 0 };

	struct SnapshotRuntimeBuffers
	{
		bool replaceBuffers = false;
		size_t tileCount = 0;
#ifdef EDITOR
		bool* editorVismap = nullptr;
#endif
		bool* menuVismap = nullptr;
		bool* playerVismaps[MAXPLAYERS] = { nullptr };
		bool* shoparea = nullptr;
	};

	bool allocateMapBoolBuffer(bool*& dest, const size_t tileCount)
	{
		dest = static_cast<bool*>(malloc(sizeof(bool) * tileCount));
		if ( !dest )
		{
			return false;
		}
		memset(dest, 0, sizeof(bool) * tileCount);
		return true;
	}

	void freeSnapshotRuntimeBuffers(SnapshotRuntimeBuffers& buffers)
	{
#ifdef EDITOR
		if ( buffers.editorVismap )
		{
			free(buffers.editorVismap);
			buffers.editorVismap = nullptr;
		}
#endif
		if ( buffers.menuVismap )
		{
			free(buffers.menuVismap);
			buffers.menuVismap = nullptr;
		}
		for ( int i = 0; i < MAXPLAYERS; ++i )
		{
			if ( buffers.playerVismaps[i] )
			{
				free(buffers.playerVismaps[i]);
				buffers.playerVismaps[i] = nullptr;
			}
		}
		if ( buffers.shoparea )
		{
			free(buffers.shoparea);
			buffers.shoparea = nullptr;
		}
	}

	bool needsSnapshotRuntimeBufferReplace(const map_t& destmap, const MapGeometrySnapshot& snapshot)
	{
		if ( destmap.width != snapshot.width || destmap.height != snapshot.height )
		{
			return true;
		}
#ifdef EDITOR
		if ( !camera.vismap )
		{
			return true;
		}
#endif
		if ( !menucam.vismap || !shoparea )
		{
			return true;
		}
		for ( int i = 0; i < MAXPLAYERS; ++i )
		{
			if ( !cameras[i].vismap )
			{
				return true;
			}
		}
		return false;
	}

	bool prepareSnapshotRuntimeBuffers(const map_t& destmap, const MapGeometrySnapshot& snapshot, SnapshotRuntimeBuffers& buffers)
	{
		if ( snapshot.width == 0 || snapshot.height == 0 )
		{
			return false;
		}
		buffers.tileCount = static_cast<size_t>(snapshot.width) * snapshot.height;
		buffers.replaceBuffers = needsSnapshotRuntimeBufferReplace(destmap, snapshot);
		if ( !buffers.replaceBuffers )
		{
			return true;
		}

#ifdef EDITOR
		if ( !allocateMapBoolBuffer(buffers.editorVismap, buffers.tileCount) )
		{
			return false;
		}
#endif
		if ( !allocateMapBoolBuffer(buffers.menuVismap, buffers.tileCount) )
		{
			freeSnapshotRuntimeBuffers(buffers);
			return false;
		}
		for ( int i = 0; i < MAXPLAYERS; ++i )
		{
			if ( !allocateMapBoolBuffer(buffers.playerVismaps[i], buffers.tileCount) )
			{
				freeSnapshotRuntimeBuffers(buffers);
				return false;
			}
		}
		if ( !allocateMapBoolBuffer(buffers.shoparea, buffers.tileCount) )
		{
			freeSnapshotRuntimeBuffers(buffers);
			return false;
		}
		return true;
	}

	void commitSnapshotRuntimeBuffers(SnapshotRuntimeBuffers& buffers)
	{
#ifdef EDITOR
		if ( camera.vismap )
		{
			free(camera.vismap);
		}
		camera.vismap = buffers.editorVismap;
		buffers.editorVismap = nullptr;
#endif
		if ( menucam.vismap )
		{
			free(menucam.vismap);
		}
		menucam.vismap = buffers.menuVismap;
		buffers.menuVismap = nullptr;
		for ( int i = 0; i < MAXPLAYERS; ++i )
		{
			if ( cameras[i].vismap )
			{
				free(cameras[i].vismap);
			}
			cameras[i].vismap = buffers.playerVismaps[i];
			buffers.playerVismaps[i] = nullptr;
		}
		if ( shoparea )
		{
			free(shoparea);
		}
		shoparea = buffers.shoparea;
		buffers.shoparea = nullptr;
		buffers.replaceBuffers = false;
	}

	void resetSnapshotRuntimeBuffers(const size_t tileCount)
	{
#ifdef EDITOR
		memset(camera.vismap, 0, sizeof(bool) * tileCount);
#endif
		memset(menucam.vismap, 0, sizeof(bool) * tileCount);
		for ( int i = 0; i < MAXPLAYERS; ++i )
		{
			memset(cameras[i].vismap, 0, sizeof(bool) * tileCount);
		}
		memset(shoparea, 0, sizeof(bool) * tileCount);
	}

	Uint16 buildConnectedPlayerMaskForLevelLoad()
	{
		Uint16 mask = 0;
		for ( int i = 0; i < MAXPLAYERS; ++i )
		{
			if ( !client_disconnected[i] )
			{
				mask |= static_cast<Uint16>(1u << i);
			}
		}
		return mask;
	}

	int countConnectedPlayersInMask(const Uint16 mask)
	{
		int connectedPlayers = 0;
		for ( int i = 0; i < MAXPLAYERS; ++i )
		{
			if ( mask & static_cast<Uint16>(1u << i) )
			{
				++connectedPlayers;
			}
		}
		return connectedPlayers;
	}

	Uint16 nextMapSnapshotTransferIdForPlayer(const int player)
	{
		if ( player < 0 || player >= MAXPLAYERS )
		{
			return 1;
		}
		++g_mapSnapshotTransferId[player];
		if ( g_mapSnapshotTransferId[player] == 0 )
		{
			++g_mapSnapshotTransferId[player];
		}
		return g_mapSnapshotTransferId[player];
	}

	void resetPendingMapSnapshotReceive()
	{
		g_pendingMapSnapshotReceive = PendingMapSnapshotReceive{};
	}

	void appendSnapshotByte(std::vector<Uint8>& buffer, const Uint8 value)
	{
		buffer.push_back(value);
	}

	void appendSnapshotUint32(std::vector<Uint8>& buffer, const Uint32 value)
	{
		const size_t offset = buffer.size();
		buffer.resize(offset + sizeof(Uint32));
		SDLNet_Write32(value, buffer.data() + offset);
	}

	void appendSnapshotSint32(std::vector<Uint8>& buffer, const Sint32 value)
	{
		appendSnapshotUint32(buffer, static_cast<Uint32>(value));
	}

	void appendSnapshotBytes(std::vector<Uint8>& buffer, const void* data, const size_t size)
	{
		const auto* bytes = static_cast<const Uint8*>(data);
		buffer.insert(buffer.end(), bytes, bytes + size);
	}

	bool readSnapshotUint8(const std::vector<Uint8>& buffer, size_t& offset, Uint8& value)
	{
		if ( offset + sizeof(Uint8) > buffer.size() )
		{
			return false;
		}
		value = buffer[offset];
		offset += sizeof(Uint8);
		return true;
	}

	bool readSnapshotUint32(const std::vector<Uint8>& buffer, size_t& offset, Uint32& value)
	{
		if ( offset + sizeof(Uint32) > buffer.size() )
		{
			return false;
		}
		value = SDLNet_Read32(buffer.data() + offset);
		offset += sizeof(Uint32);
		return true;
	}

	bool readSnapshotSint32(const std::vector<Uint8>& buffer, size_t& offset, Sint32& value)
	{
		Uint32 raw = 0;
		if ( !readSnapshotUint32(buffer, offset, raw) )
		{
			return false;
		}
		value = static_cast<Sint32>(raw);
		return true;
	}

	bool readSnapshotBytes(const std::vector<Uint8>& buffer, size_t& offset, void* dest, const size_t size)
	{
		if ( offset + size > buffer.size() )
		{
			return false;
		}
		memcpy(dest, buffer.data() + offset, size);
		offset += size;
		return true;
	}

	std::vector<Uint8> serializeMapGeometrySnapshot(const MapGeometrySnapshot& snapshot)
	{
		std::vector<Uint8> bytes;
		const size_t tileBytes = snapshot.tiles.size() * sizeof(Sint32);
		const size_t attrBytes = snapshot.tileAttributes.size() * (sizeof(Uint32) + sizeof(Uint32));
		bytes.reserve(1 + sizeof(snapshot.name) + sizeof(snapshot.author) + sizeof(snapshot.filename)
			+ sizeof(Uint32) * 4 + sizeof(snapshot.flags) + tileBytes + attrBytes);

		appendSnapshotByte(bytes, kMapSnapshotPacketVersion);
		appendSnapshotBytes(bytes, snapshot.name, sizeof(snapshot.name));
		appendSnapshotBytes(bytes, snapshot.author, sizeof(snapshot.author));
		appendSnapshotBytes(bytes, snapshot.filename, sizeof(snapshot.filename));
		appendSnapshotUint32(bytes, snapshot.width);
		appendSnapshotUint32(bytes, snapshot.height);
		appendSnapshotUint32(bytes, snapshot.skybox);
		for ( const auto flag : snapshot.flags )
		{
			appendSnapshotSint32(bytes, flag);
		}
		appendSnapshotUint32(bytes, static_cast<Uint32>(snapshot.tiles.size()));
		for ( const auto tile : snapshot.tiles )
		{
			appendSnapshotSint32(bytes, tile);
		}
		appendSnapshotUint32(bytes, static_cast<Uint32>(snapshot.tileAttributes.size()));
		for ( const auto& entry : snapshot.tileAttributes )
		{
			appendSnapshotSint32(bytes, static_cast<Sint32>(entry.first));
			appendSnapshotUint32(bytes, entry.second);
		}
		return bytes;
	}

	bool deserializeMapGeometrySnapshot(const std::vector<Uint8>& bytes, MapGeometrySnapshot& snapshot)
	{
		size_t offset = 0;
		Uint8 version = 0;
		if ( !readSnapshotUint8(bytes, offset, version) || version != kMapSnapshotPacketVersion )
		{
			return false;
		}
		if ( !readSnapshotBytes(bytes, offset, snapshot.name, sizeof(snapshot.name))
			|| !readSnapshotBytes(bytes, offset, snapshot.author, sizeof(snapshot.author))
			|| !readSnapshotBytes(bytes, offset, snapshot.filename, sizeof(snapshot.filename))
			|| !readSnapshotUint32(bytes, offset, snapshot.width)
			|| !readSnapshotUint32(bytes, offset, snapshot.height)
			|| !readSnapshotUint32(bytes, offset, snapshot.skybox) )
		{
			return false;
		}
		for ( auto& flag : snapshot.flags )
		{
			if ( !readSnapshotSint32(bytes, offset, flag) )
			{
				return false;
			}
		}

		Uint32 tileCount = 0;
		if ( !readSnapshotUint32(bytes, offset, tileCount) )
		{
			return false;
		}
		const size_t expectedTileCount = static_cast<size_t>(snapshot.width) * snapshot.height * MAPLAYERS;
		if ( tileCount != expectedTileCount )
		{
			return false;
		}
		snapshot.tiles.resize(tileCount);
		for ( Uint32 i = 0; i < tileCount; ++i )
		{
			if ( !readSnapshotSint32(bytes, offset, snapshot.tiles[i]) )
			{
				return false;
			}
		}

		Uint32 attributeCount = 0;
		if ( !readSnapshotUint32(bytes, offset, attributeCount) )
		{
			return false;
		}
		snapshot.tileAttributes.clear();
		for ( Uint32 i = 0; i < attributeCount; ++i )
		{
			Sint32 key = 0;
			Uint32 value = 0;
			if ( !readSnapshotSint32(bytes, offset, key)
				|| !readSnapshotUint32(bytes, offset, value) )
			{
				return false;
			}
			snapshot.tileAttributes[static_cast<int>(key)] = value;
		}

		return offset == bytes.size();
	}

	bool requestAuthoritativeMapSnapshotFromHost(const Uint32 localChecksum)
	{
		if ( multiplayer != CLIENT || !net_packet || !net_packet->data )
		{
			return false;
		}
		strcpy((char*)net_packet->data, "MSRQ");
		net_packet->data[4] = clientnum;
		net_packet->data[5] = kMapSnapshotPacketVersion;
		net_packet->data[6] = static_cast<Uint8>(currentlevel);
		net_packet->data[7] = secretlevel ? 1 : 0;
		SDLNet_Write32(mapseed, &net_packet->data[8]);
		SDLNet_Write32(g_authorityState.tileChecksum, &net_packet->data[12]);
		net_packet->address.host = net_server.host;
		net_packet->address.port = net_server.port;
		net_packet->len = 16;
		const int sent = sendPacketSafe(net_sock, -1, net_packet, 0);
		printlog("[NET]: requested authoritative map snapshot level=%d secret=%d seed=%u host_checksum=%u local_checksum=%u sent=%d",
			currentlevel, secretlevel ? 1 : 0, mapseed,
			g_authorityState.tileChecksum, localChecksum, sent);
		return sent != 0;
	}

	void sendAuthoritativeMapSnapshotToClient(const int player)
	{
		if ( multiplayer != SERVER || player <= 0 || player >= MAXPLAYERS || !net_packet || !net_packet->data )
		{
			return;
		}
		const MapGeometrySnapshot snapshot = captureMapGeometrySnapshot(map);
		const std::vector<Uint8> bytes = serializeMapGeometrySnapshot(snapshot);
		const Uint16 chunkCount = std::max<Uint16>(1, static_cast<Uint16>((bytes.size() + kMapSnapshotChunkPayloadMax - 1) / kMapSnapshotChunkPayloadMax));
		const Uint16 transferId = nextMapSnapshotTransferIdForPlayer(player);
		printlog("[NET]: sending authoritative map snapshot player=%d level=%d secret=%d seed=%u transfer=%u bytes=%zu chunks=%u checksum=%u",
			player, currentlevel, secretlevel ? 1 : 0, mapseed,
			transferId, bytes.size(), static_cast<unsigned>(chunkCount), g_authorityState.tileChecksum);

		for ( Uint16 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
		{
			const size_t offset = static_cast<size_t>(chunkIndex) * kMapSnapshotChunkPayloadMax;
			const size_t remaining = offset < bytes.size() ? (bytes.size() - offset) : 0;
			const Uint16 payloadBytes = static_cast<Uint16>(std::min<size_t>(remaining, kMapSnapshotChunkPayloadMax));

			strcpy((char*)net_packet->data, "MSNP");
			net_packet->data[4] = static_cast<Uint8>(player);
			net_packet->data[5] = kMapSnapshotPacketVersion;
			net_packet->data[6] = static_cast<Uint8>(currentlevel);
			net_packet->data[7] = secretlevel ? 1 : 0;
			SDLNet_Write32(mapseed, &net_packet->data[8]);
			SDLNet_Write16(transferId, &net_packet->data[12]);
			SDLNet_Write16(chunkIndex, &net_packet->data[14]);
			SDLNet_Write16(chunkCount, &net_packet->data[16]);
			SDLNet_Write32(static_cast<Uint32>(bytes.size()), &net_packet->data[18]);
			SDLNet_Write32(g_authorityState.tileChecksum, &net_packet->data[22]);
			SDLNet_Write16(payloadBytes, &net_packet->data[26]);
			if ( payloadBytes > 0 )
			{
				memcpy(net_packet->data + kMapSnapshotChunkHeaderSize, bytes.data() + offset, payloadBytes);
			}
			net_packet->address.host = net_clients[player - 1].host;
			net_packet->address.port = net_clients[player - 1].port;
			net_packet->len = kMapSnapshotChunkHeaderSize + payloadBytes;
			sendPacketSafe(net_sock, -1, net_packet, player - 1);
		}
	}

	bool applyPendingMapSnapshotReceive()
	{
		MapGeometrySnapshot snapshot;
		if ( !deserializeMapGeometrySnapshot(g_pendingMapSnapshotReceive.bytes, snapshot) )
		{
			printlog("[NET]: failed to decode authoritative map snapshot transfer=%u bytes=%u",
				g_pendingMapSnapshotReceive.transferId, g_pendingMapSnapshotReceive.totalBytes);
			messagePlayer(clientnum, MESSAGE_MISC, "Failed to decode host map snapshot. Rejoining is recommended.");
			g_mapSnapshotRecoveryRequested = false;
			resetPendingMapSnapshotReceive();
			return false;
		}
		SnapshotRuntimeBuffers runtimeBuffers;
		if ( !prepareSnapshotRuntimeBuffers(map, snapshot, runtimeBuffers)
			|| !applyMapGeometrySnapshotData(map, snapshot) )
		{
			freeSnapshotRuntimeBuffers(runtimeBuffers);
			printlog("[NET]: failed to apply authoritative map snapshot transfer=%u level=%d secret=%d seed=%u",
				g_pendingMapSnapshotReceive.transferId, currentlevel, secretlevel ? 1 : 0, mapseed);
			messagePlayer(clientnum, MESSAGE_MISC, "Failed to apply host map snapshot. Rejoining is recommended.");
			g_mapSnapshotRecoveryRequested = false;
			resetPendingMapSnapshotReceive();
			return false;
		}
		if ( runtimeBuffers.replaceBuffers )
		{
			commitSnapshotRuntimeBuffers(runtimeBuffers);
		}
		else
		{
			resetSnapshotRuntimeBuffers(runtimeBuffers.tileCount);
		}
		resetMapVisualCachesForGeometry(map);
		generatePathMaps();
		clearChunks();
		createChunks();
		const Uint32 recoveredChecksum = calculateMapTileChecksum(map);
		const bool checksumMatches = !g_authorityState.tileChecksumValid
			|| recoveredChecksum == g_pendingMapSnapshotReceive.checksum;
		if ( checksumMatches )
		{
			g_authorityState.tileChecksum = g_pendingMapSnapshotReceive.checksum;
			g_authorityState.tileChecksumValid = true;
			printlog("[NET]: authoritative map snapshot applied transfer=%u level=%d secret=%d seed=%u checksum=%u",
				g_pendingMapSnapshotReceive.transferId, currentlevel, secretlevel ? 1 : 0, mapseed, recoveredChecksum);
			messagePlayer(clientnum, MESSAGE_MISC, "Recovered map geometry from host.");
		}
		else
		{
			printlog("[NET]: authoritative map snapshot checksum mismatch transfer=%u level=%d secret=%d seed=%u host_checksum=%u recovered_checksum=%u",
				g_pendingMapSnapshotReceive.transferId, currentlevel, secretlevel ? 1 : 0, mapseed,
				g_pendingMapSnapshotReceive.checksum, recoveredChecksum);
			messagePlayer(clientnum, MESSAGE_MISC, "Host map snapshot recovery failed. Rejoining is recommended.");
		}
		g_mapSnapshotRecoveryRequested = false;
		resetPendingMapSnapshotReceive();
		return checksumMatches;
	}
} // namespace

void reset()
{
	g_authorityState = AuthorityState{};
	g_mapSnapshotRecoveryRequested = false;
	resetPendingMapSnapshotReceive();
	memset(g_mapSnapshotTransferId, 0, sizeof(g_mapSnapshotTransferId));
}

void beginHostLevelLoad()
{
	g_authorityState = AuthorityState{};
	g_authorityState.playerMask = buildConnectedPlayerMaskForLevelLoad();
}

void finalizeHostLevelLoad(const map_t& mapToFinalize)
{
	g_authorityState.tileChecksum = calculateMapTileChecksum(mapToFinalize);
	g_authorityState.tileChecksumValid = true;
	g_authorityState.entityChecksum = calculateMapEntityChecksum(mapToFinalize);
	g_authorityState.entityChecksumValid = true;
	printlog("[NET]: level load authoritative mapgen inputs level=%d secret=%d seed=%u players=%d mask=0x%04X tile_checksum=%u entity_checksum=%u",
		currentlevel, secretlevel ? 1 : 0, mapseed,
		countConnectedPlayersInMask(g_authorityState.playerMask),
		static_cast<unsigned>(g_authorityState.playerMask),
		g_authorityState.tileChecksum,
		g_authorityState.entityChecksum);
}

bool prepareLevelChangePacket(UDPpacket* packet,
	const char* packetType,
	bool secretLevelValue,
	Uint32 mapSeedValue,
	Uint32 entityUidValue,
	int currentLevelValue,
	const std::string& customMapName)
{
	if ( !packet || !packet->data )
	{
		return false;
	}

	strcpy((char*)packet->data, packetType);
	packet->data[4] = secretLevelValue ? 1 : 0;
	SDLNet_Write32(mapSeedValue, &packet->data[5]);
	SDLNet_Write32(entityUidValue, &packet->data[9]);
	packet->data[13] = currentLevelValue;

	int offset = LEVEL_CHANGE_PACKET_BASE_LEN;
	if ( !customMapName.empty() )
	{
		strcpy((char*)(&packet->data[14]), customMapName.c_str());
		packet->data[14 + customMapName.length()] = 0;
		offset += static_cast<int>(customMapName.length()) + 1;
	}
	else
	{
		packet->data[14] = 0;
	}

	if ( g_authorityState.tileChecksumValid )
	{
		packet->data[offset] = g_authorityState.entityChecksumValid
			? LEVEL_CHANGE_PACKET_EXTRA_VERSION_2
			: LEVEL_CHANGE_PACKET_EXTRA_VERSION_1;
		SDLNet_Write16(g_authorityState.playerMask, &packet->data[offset + 1]);
		SDLNet_Write32(g_authorityState.tileChecksum, &packet->data[offset + 3]);
		offset += LEVEL_CHANGE_PACKET_EXTRA_SIZE_V1;
		if ( g_authorityState.entityChecksumValid )
		{
			SDLNet_Write32(g_authorityState.entityChecksum, &packet->data[offset]);
			offset += sizeof(Uint32);
		}
	}

	packet->len = offset;
	return true;
}

void beginClientLevelChangeFromPacket(const UDPpacket* packet)
{
	reset();
	if ( !packet || !packet->data || packet->len < LEVEL_CHANGE_PACKET_BASE_LEN )
	{
		return;
	}

	int extraOffset = LEVEL_CHANGE_PACKET_BASE_LEN;
	if ( packet->data[14] != 0 )
	{
		extraOffset += static_cast<int>(strlen((char*)&packet->data[14])) + 1;
	}
	if ( packet->len >= extraOffset + LEVEL_CHANGE_PACKET_EXTRA_SIZE_V1
		&& packet->data[extraOffset] >= LEVEL_CHANGE_PACKET_EXTRA_VERSION_1 )
	{
		const Uint8 extraVersion = packet->data[extraOffset];
		g_authorityState.playerMask = SDLNet_Read16(&packet->data[extraOffset + 1]);
		g_authorityState.tileChecksum = SDLNet_Read32(&packet->data[extraOffset + 3]);
		g_authorityState.tileChecksumValid = true;
		if ( extraVersion >= LEVEL_CHANGE_PACKET_EXTRA_VERSION_2
			&& packet->len >= extraOffset + LEVEL_CHANGE_PACKET_EXTRA_SIZE_V2 )
		{
			g_authorityState.entityChecksum = SDLNet_Read32(&packet->data[extraOffset + LEVEL_CHANGE_PACKET_EXTRA_SIZE_V1]);
			g_authorityState.entityChecksumValid = true;
		}
		printlog("[NET]: received authoritative mapgen inputs level=%d secret=%d seed=%u players=%d mask=0x%04X tile_checksum=%u entity_checksum=%u version=%u",
			packet->data[13], packet->data[4], SDLNet_Read32(&packet->data[5]),
			countConnectedPlayersInMask(g_authorityState.playerMask),
			static_cast<unsigned>(g_authorityState.playerMask),
			g_authorityState.tileChecksum,
			g_authorityState.entityChecksumValid ? g_authorityState.entityChecksum : 0,
			static_cast<unsigned>(extraVersion));
	}
}

void validateClientLoadedMap(const map_t& loadedMap, const bool entityChecksumValid, const Uint32 entityChecksum)
{
	if ( g_authorityState.entityChecksumValid && entityChecksumValid )
	{
		if ( entityChecksum != g_authorityState.entityChecksum )
		{
			printlog("[NET]: entity sync mismatch detected level=%d secret=%d seed=%u host_entity_checksum=%u local_entity_checksum=%u mask=0x%04X map=\"%s\"",
				currentlevel, secretlevel ? 1 : 0, mapseed,
				g_authorityState.entityChecksum, entityChecksum,
				static_cast<unsigned>(g_authorityState.playerMask), loadedMap.name);
		}
	}
	if ( g_authorityState.tileChecksumValid )
	{
		const Uint32 localTileChecksum = calculateMapTileChecksum(loadedMap);
		if ( localTileChecksum != g_authorityState.tileChecksum )
		{
			printlog("[NET]: map sync mismatch detected level=%d secret=%d seed=%u host_checksum=%u local_checksum=%u mask=0x%04X map=\"%s\"",
				currentlevel, secretlevel ? 1 : 0, mapseed,
				g_authorityState.tileChecksum, localTileChecksum,
				static_cast<unsigned>(g_authorityState.playerMask), loadedMap.name);
			if ( !g_mapSnapshotRecoveryRequested )
			{
				g_mapSnapshotRecoveryRequested = true;
				requestAuthoritativeMapSnapshotFromHost(localTileChecksum);
				messagePlayer(clientnum, MESSAGE_MISC, "Map sync mismatch detected. Requesting host snapshot.");
			}
		}
	}
}

void handleMapSnapshotChunkPacket()
{
	if ( !net_packet || !net_packet->data || net_packet->len < kMapSnapshotChunkHeaderSize )
	{
		return;
	}
	const int targetPlayer = net_packet->data[4];
	if ( targetPlayer != clientnum )
	{
		return;
	}
	if ( net_packet->data[5] != kMapSnapshotPacketVersion )
	{
		printlog("[NET]: ignoring map snapshot chunk with unsupported version=%u",
			static_cast<unsigned>(net_packet->data[5]));
		return;
	}
	const int level = static_cast<Sint8>(net_packet->data[6]);
	const bool secret = net_packet->data[7] != 0;
	const Uint32 seed = SDLNet_Read32(&net_packet->data[8]);
	if ( level != currentlevel || secret != static_cast<bool>(secretlevel) || seed != mapseed )
	{
		printlog("[NET]: ignoring stale authoritative map snapshot chunk level=%d secret=%d seed=%u current_level=%d current_secret=%d current_seed=%u",
			level, secret ? 1 : 0, seed, currentlevel, secretlevel ? 1 : 0, mapseed);
		return;
	}
	const Uint16 transferId = SDLNet_Read16(&net_packet->data[12]);
	const Uint16 chunkIndex = SDLNet_Read16(&net_packet->data[14]);
	const Uint16 chunkCount = SDLNet_Read16(&net_packet->data[16]);
	const Uint32 totalBytes = SDLNet_Read32(&net_packet->data[18]);
	const Uint32 checksum = SDLNet_Read32(&net_packet->data[22]);
	const Uint16 payloadBytes = SDLNet_Read16(&net_packet->data[26]);
	if ( chunkCount == 0 || chunkIndex >= chunkCount || net_packet->len != kMapSnapshotChunkHeaderSize + payloadBytes )
	{
		printlog("[NET]: ignoring malformed authoritative map snapshot chunk transfer=%u chunk=%u/%u len=%d payload=%u",
			transferId, static_cast<unsigned>(chunkIndex), static_cast<unsigned>(chunkCount),
			net_packet->len, static_cast<unsigned>(payloadBytes));
		return;
	}

	const bool newTransfer = !g_pendingMapSnapshotReceive.active
		|| g_pendingMapSnapshotReceive.transferId != transferId
		|| g_pendingMapSnapshotReceive.level != static_cast<Uint8>(level)
		|| g_pendingMapSnapshotReceive.secret != secret
		|| g_pendingMapSnapshotReceive.seed != seed;
	if ( newTransfer )
	{
		resetPendingMapSnapshotReceive();
		g_pendingMapSnapshotReceive.active = true;
		g_pendingMapSnapshotReceive.level = static_cast<Uint8>(level);
		g_pendingMapSnapshotReceive.secret = secret;
		g_pendingMapSnapshotReceive.seed = seed;
		g_pendingMapSnapshotReceive.transferId = transferId;
		g_pendingMapSnapshotReceive.chunkCount = chunkCount;
		g_pendingMapSnapshotReceive.totalBytes = totalBytes;
		g_pendingMapSnapshotReceive.checksum = checksum;
		g_pendingMapSnapshotReceive.bytes.assign(totalBytes, 0);
		g_pendingMapSnapshotReceive.receivedChunks.assign(chunkCount, 0);
		printlog("[NET]: receiving authoritative map snapshot transfer=%u level=%d secret=%d seed=%u bytes=%u chunks=%u checksum=%u",
			transferId, level, secret ? 1 : 0, seed, totalBytes,
			static_cast<unsigned>(chunkCount), checksum);
	}
	else if ( g_pendingMapSnapshotReceive.chunkCount != chunkCount
		|| g_pendingMapSnapshotReceive.totalBytes != totalBytes
		|| g_pendingMapSnapshotReceive.checksum != checksum )
	{
		printlog("[NET]: dropping authoritative map snapshot transfer=%u due to metadata mismatch", transferId);
		resetPendingMapSnapshotReceive();
		return;
	}

	const size_t copyOffset = static_cast<size_t>(chunkIndex) * kMapSnapshotChunkPayloadMax;
	if ( copyOffset + payloadBytes > g_pendingMapSnapshotReceive.bytes.size() )
	{
		printlog("[NET]: ignoring out-of-range authoritative map snapshot chunk transfer=%u chunk=%u offset=%zu payload=%u total=%zu",
			transferId, static_cast<unsigned>(chunkIndex), copyOffset,
			static_cast<unsigned>(payloadBytes), g_pendingMapSnapshotReceive.bytes.size());
		resetPendingMapSnapshotReceive();
		return;
	}
	if ( payloadBytes > 0 )
	{
		memcpy(g_pendingMapSnapshotReceive.bytes.data() + copyOffset,
			net_packet->data + kMapSnapshotChunkHeaderSize, payloadBytes);
	}
	if ( !g_pendingMapSnapshotReceive.receivedChunks[chunkIndex] )
	{
		g_pendingMapSnapshotReceive.receivedChunks[chunkIndex] = 1;
		++g_pendingMapSnapshotReceive.receivedChunkCount;
	}
	if ( g_pendingMapSnapshotReceive.receivedChunkCount == g_pendingMapSnapshotReceive.chunkCount )
	{
		applyPendingMapSnapshotReceive();
	}
}

void handleMapSnapshotRequestPacket()
{
	if ( !net_packet || !net_packet->data || net_packet->len < 16 || net_packet->data[5] != kMapSnapshotPacketVersion )
	{
		return;
	}
	const int player = std::min(net_packet->data[4], static_cast<Uint8>(MAXPLAYERS - 1));
	if ( player <= 0 || player >= MAXPLAYERS || client_disconnected[player] || players[player]->isLocalPlayer() )
	{
		return;
	}
	const int requestedLevel = static_cast<Sint8>(net_packet->data[6]);
	const bool requestedSecret = net_packet->data[7] != 0;
	const Uint32 requestedSeed = SDLNet_Read32(&net_packet->data[8]);
	const Uint32 requestedChecksum = SDLNet_Read32(&net_packet->data[12]);
	if ( requestedLevel != currentlevel || requestedSecret != static_cast<bool>(secretlevel) || requestedSeed != mapseed )
	{
		printlog("[NET]: ignoring map snapshot request from player=%d due to stale level=%d/%d secret=%d/%d seed=%u/%u",
			player, requestedLevel, currentlevel,
			requestedSecret ? 1 : 0, secretlevel ? 1 : 0,
			requestedSeed, mapseed);
		return;
	}
	if ( !g_authorityState.tileChecksumValid )
	{
		printlog("[NET]: ignoring map snapshot request from player=%d because authoritative checksum is unavailable",
			player);
		return;
	}
	printlog("[NET]: received map snapshot request from player=%d level=%d secret=%d seed=%u requested_checksum=%u host_checksum=%u",
		player, requestedLevel, requestedSecret ? 1 : 0, requestedSeed,
		requestedChecksum, g_authorityState.tileChecksum);
	sendAuthoritativeMapSnapshotToClient(player);
}

bool hasAuthoritativePlayerMask()
{
	return g_authorityState.playerMask != 0;
}

bool isPlayerConnectedForMapgen(const int player)
{
	if ( player < 0 || player >= MAXPLAYERS )
	{
		return false;
	}
	if ( g_authorityState.playerMask != 0 )
	{
		return (g_authorityState.playerMask & static_cast<Uint16>(1u << player)) != 0;
	}
	return !client_disconnected[player];
}
} // namespace LevelLoadSync
