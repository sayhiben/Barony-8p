#pragma once

#include <string>

#include "main.hpp"

namespace LevelLoadSync
{
void reset();

void beginHostLevelLoad();
void finalizeHostLevelLoad(const map_t& map);

bool prepareLevelChangePacket(UDPpacket* packet,
	const char* packetType,
	bool secretLevelValue,
	Uint32 mapSeedValue,
	Uint32 entityUidValue,
	int currentLevelValue,
	const std::string& customMapName);

void beginClientLevelChangeFromPacket(const UDPpacket* packet);
void validateClientLoadedMap(const map_t& map, bool entityChecksumValid, Uint32 entityChecksum);

void handleMapSnapshotChunkPacket();
void handleMapSnapshotRequestPacket();

bool hasAuthoritativePlayerMask();
bool isPlayerConnectedForMapgen(int player);
} // namespace LevelLoadSync
