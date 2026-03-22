#include "SmokeTestHooks.hpp"
#include "SmokeHooksCommon.hpp"

namespace
{
	using namespace SmokeHooksCommon;
}

namespace SmokeTestHooks
{
namespace Net
{
	bool isForceHeloChunkEnabled()
	{
		static bool initialized = false;
		static bool enabled = false;
		if ( !initialized )
		{
			initialized = true;
			enabled = parseEnvBool("BARONY_SMOKE_FORCE_HELO_CHUNK", false);
			if ( enabled )
			{
				printlog("[SMOKE]: BARONY_SMOKE_FORCE_HELO_CHUNK is enabled");
			}
		}
		return enabled;
	}

	int heloChunkPayloadMaxOverride(const int defaultPayloadMax, const int minPayloadMax)
	{
		return MainMenu::heloChunkPayloadMaxOverride(defaultPayloadMax, minPayloadMax);
	}

	bool isJoinRejectTraceEnabled()
	{
		static const bool enabled = parseEnvBool("BARONY_SMOKE_TRACE_JOIN_REJECTS", false);
		return enabled;
	}

	bool isInventoryPacketTraceEnabled()
	{
		static const bool enabled = parseEnvBool("BARONY_SMOKE_TRACE_INVENTORY_PACKETS", false);
		return enabled;
	}

	void traceInventoryPacketUse(const int client, const int count)
	{
		if ( !isInventoryPacketTraceEnabled() )
		{
			return;
		}
		printlog("[SMOKE]: inventory packet op=USEI client=%d count=%d cleanup_required=0 cleanup_cleared=0 slot=-1 edge=none status=ok",
			client, count);
	}

	void traceInventoryPacketEquip(const char* op, const int client, const int count,
		const bool cleanupRequired, const bool cleanupCleared, const int equipResult,
		const int slot, const char* edge)
	{
		if ( !isInventoryPacketTraceEnabled() )
		{
			return;
		}
		const bool ok = !cleanupRequired || cleanupCleared;
		printlog("[SMOKE]: inventory packet op=%s client=%d count=%d cleanup_required=%d cleanup_cleared=%d equip_result=%d slot=%d edge=%s status=%s",
			op ? op : "UNKN",
			client,
			count,
			cleanupRequired ? 1 : 0,
			cleanupCleared ? 1 : 0,
			equipResult,
			slot,
			edge ? edge : "none",
			ok ? "ok" : "fail");
	}

	bool forceLevelLoadMapMismatch(map_t& map)
	{
		static bool initialized = false;
		static bool enabled = false;
		static bool consumed = false;
		if ( !initialized )
		{
			initialized = true;
			enabled = parseEnvBool("BARONY_SMOKE_FORCE_MAP_SNAPSHOT_RECOVERY", false);
			if ( enabled )
			{
				printlog("[SMOKE]: BARONY_SMOKE_FORCE_MAP_SNAPSHOT_RECOVERY is enabled");
			}
		}
		if ( !enabled || consumed || !map.tiles || map.width == 0 || map.height == 0 )
		{
			return false;
		}
		consumed = true;
		const size_t tileCount = static_cast<size_t>(map.width) * map.height * MAPLAYERS;
		const size_t index = tileCount > 1 ? 1 : 0;
		const Sint32 original = map.tiles[index];
		map.tiles[index] = (original == 0) ? 1 : 0;
		printlog("[SMOKE]: forced level-load map mismatch tile_index=%zu original=%d patched=%d",
			index, original, map.tiles[index]);
		return true;
	}

	void traceLobbyJoinReject(const Uint32 result, const Uint8 requestedSlot, const bool lockedSlots[MAXPLAYERS], const bool disconnectedSlots[MAXPLAYERS])
	{
		if ( !isJoinRejectTraceEnabled() )
		{
			return;
		}

		int freeUnlocked = 0;
		int freeLocked = 0;
		int occupied = 0;
		int firstFree = -1;
		char slotStates[MAXPLAYERS + 1] = {};
		int statePos = 0;

		for ( int slot = 1; slot < MAXPLAYERS; ++slot )
		{
			const bool disconnected = disconnectedSlots ? disconnectedSlots[slot] : false;
			const bool locked = lockedSlots ? lockedSlots[slot] : false;
			char state = '?';
			if ( !disconnected )
			{
				state = 'O';
				++occupied;
			}
			else if ( locked )
			{
				state = 'L';
				++freeLocked;
			}
			else
			{
				state = 'F';
				++freeUnlocked;
				if ( firstFree < 0 )
				{
					firstFree = slot;
				}
			}
			if ( statePos < MAXPLAYERS )
			{
				slotStates[statePos++] = state;
			}
		}
		slotStates[statePos] = '\0';

		const bool anySlotRequest = requestedSlot == 0;
		const int requested = anySlotRequest ? -1 : static_cast<int>(requestedSlot);
		printlog("[SMOKE]: lobby join reject code=%u requested_slot=%d any_slot=%d free_unlocked=%d free_locked=%d occupied=%d first_free=%d states=%s",
			static_cast<unsigned>(result),
			requested,
			anySlotRequest ? 1 : 0,
			freeUnlocked,
			freeLocked,
			occupied,
			firstFree,
			slotStates);
	}
}

}
