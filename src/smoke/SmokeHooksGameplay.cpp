#include "SmokeTestHooks.hpp"
#include "SmokeHooksCommon.hpp"

#include "../game.hpp"
#include "../items.hpp"
#include "../mod_tools.hpp"
#include "../net.hpp"
#include "../player.hpp"
#include "../interface/interface.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace
{
	using namespace SmokeHooksCommon;
	struct SmokeAutoEnterDungeonState
	{
		bool initialized = false;
		bool enabled = false;
		bool allowSingleplayer = false;
		bool reloadSameLevel = false;
		bool preventDeath = false;
		bool preventDeathLogged = false;
		int expectedPlayers = 2;
		Uint32 delayTicks = 0;
		Uint32 readySinceTick = 0;
		bool readyWindowStarted = false;
		int maxTransitions = 1;
		int transitionsIssued = 0;
		Uint32 reloadSeedBase = 0;
		int hpFloor = 0;
	};

	static SmokeAutoEnterDungeonState g_smokeAutoEnterDungeon;

	SmokeAutoEnterDungeonState& smokeAutoEnterDungeonState()
	{
		SmokeAutoEnterDungeonState& state = g_smokeAutoEnterDungeon;
		if ( state.initialized )
		{
			return state;
		}
		state.initialized = true;

		const bool smokeEnabled = parseEnvBool("BARONY_SMOKE_AUTOPILOT", false);
		const bool autoEnterDungeon = parseEnvBool("BARONY_SMOKE_AUTO_ENTER_DUNGEON", false);
		const std::string smokeRole = toLowerCopy(std::getenv("BARONY_SMOKE_ROLE"));
		const bool smokeHost = smokeRole == "host";
		const bool smokeLocal = smokeRole == "local";

		if ( !smokeEnabled || !autoEnterDungeon || (!smokeHost && !smokeLocal) )
		{
			return state;
		}

		state.enabled = true;
		state.allowSingleplayer = smokeLocal;
		state.reloadSameLevel = parseEnvBool("BARONY_SMOKE_MAPGEN_RELOAD_SAME_LEVEL", false);
		state.preventDeath = parseEnvBool("BARONY_SMOKE_MAPGEN_PREVENT_DEATH", state.reloadSameLevel);
		state.expectedPlayers = parseEnvInt("BARONY_SMOKE_EXPECTED_PLAYERS", 2, 1, MAXPLAYERS);
		const int delaySecs = parseEnvInt("BARONY_SMOKE_AUTO_ENTER_DUNGEON_DELAY_SECS", 3, 0, 120);
		state.delayTicks = static_cast<Uint32>(delaySecs * TICKS_PER_SECOND);
		state.maxTransitions = parseEnvInt("BARONY_SMOKE_AUTO_ENTER_DUNGEON_REPEATS", 1, 1, 256);
		state.reloadSeedBase = static_cast<Uint32>(
			parseEnvInt("BARONY_SMOKE_MAPGEN_RELOAD_SEED_BASE", 0, 0, std::numeric_limits<int>::max()));
		state.hpFloor = state.preventDeath
			? parseEnvInt("BARONY_SMOKE_MAPGEN_HP_FLOOR", 500, 1, std::numeric_limits<int>::max())
			: 0;
		printlog("[SMOKE]: gameplay auto-enter enabled expected=%d delay=%d sec repeats=%d reload_same_level=%d seed_base=%u prevent_death=%d hp_floor=%d",
			state.expectedPlayers, delaySecs, state.maxTransitions,
			state.reloadSameLevel ? 1 : 0, static_cast<unsigned>(state.reloadSeedBase),
			state.preventDeath ? 1 : 0, state.hpFloor);
		return state;
	}

	int smokeConnectedPlayers()
	{
		int connected = 0;
		for ( int i = 0; i < MAXPLAYERS; ++i )
		{
			if ( !client_disconnected[i] )
			{
				++connected;
			}
		}
		return connected;
	}

	bool smokeConnectedPlayersLoaded()
	{
		for ( int i = 0; i < MAXPLAYERS; ++i )
		{
			if ( client_disconnected[i] )
			{
				continue;
			}
			if ( !players[i] || !players[i]->entity )
			{
				return false;
			}
		}
		return true;
	}

	struct SmokeRemoteCombatState
	{
		bool initialized = false;
		bool traceEnabled = false;
		bool hostAutopilotEnabled = false;
		int expectedPlayers = 2;
		int autoPausePulses = 0;
		Uint32 autoPauseDelayTicks = 0;
		Uint32 autoPauseHoldTicks = 0;
		int autoCombatPulses = 0;
		Uint32 autoCombatDelayTicks = 0;
		bool readyArmed = false;
		Uint32 nextPauseTick = 0;
		Uint32 nextCombatTick = 0;
		bool pauseActive = false;
		int pauseActionsIssued = 0;
		int combatPulsesIssued = 0;
		bool pauseCompleteLogged = false;
		bool combatCompleteLogged = false;
	};

	static SmokeRemoteCombatState g_smokeRemoteCombat;

	SmokeRemoteCombatState& smokeRemoteCombatState()
	{
		SmokeRemoteCombatState& state = g_smokeRemoteCombat;
		if ( state.initialized )
		{
			return state;
		}
		state.initialized = true;

		const bool smokeEnabled = parseEnvBool("BARONY_SMOKE_AUTOPILOT", false);
		const std::string smokeRole = toLowerCopy(std::getenv("BARONY_SMOKE_ROLE"));
		const bool smokeHost = smokeRole == "host";
		state.traceEnabled = smokeEnabled && parseEnvBool("BARONY_SMOKE_TRACE_REMOTE_COMBAT_SLOT_BOUNDS", false);
		state.expectedPlayers = parseEnvInt("BARONY_SMOKE_EXPECTED_PLAYERS", 2, 1, MAXPLAYERS);

		if ( smokeEnabled && smokeHost )
		{
			state.autoPausePulses = parseEnvInt("BARONY_SMOKE_AUTO_PAUSE_PULSES", 0, 0, 64);
			state.autoPauseDelayTicks = static_cast<Uint32>(
				parseEnvInt("BARONY_SMOKE_AUTO_PAUSE_DELAY_SECS", 2, 0, 120) * TICKS_PER_SECOND);
			state.autoPauseHoldTicks = static_cast<Uint32>(
				parseEnvInt("BARONY_SMOKE_AUTO_PAUSE_HOLD_SECS", 1, 0, 120) * TICKS_PER_SECOND);
			state.autoCombatPulses = parseEnvInt("BARONY_SMOKE_AUTO_REMOTE_COMBAT_PULSES", 0, 0, 64);
			state.autoCombatDelayTicks = static_cast<Uint32>(
				parseEnvInt("BARONY_SMOKE_AUTO_REMOTE_COMBAT_DELAY_SECS", 2, 0, 120) * TICKS_PER_SECOND);
			state.hostAutopilotEnabled = (state.autoPausePulses > 0 || state.autoCombatPulses > 0);
		}

		if ( state.traceEnabled )
		{
			printlog("[SMOKE]: remote-combat trace enabled expected=%d", state.expectedPlayers);
		}
		if ( state.hostAutopilotEnabled )
		{
			printlog("[SMOKE]: remote-combat autopilot enabled pause_pulses=%d pause_delay=%u hold=%u combat_pulses=%d combat_delay=%u",
				state.autoPausePulses,
				static_cast<unsigned>(state.autoPauseDelayTicks / TICKS_PER_SECOND),
				static_cast<unsigned>(state.autoPauseHoldTicks / TICKS_PER_SECOND),
				state.autoCombatPulses,
				static_cast<unsigned>(state.autoCombatDelayTicks / TICKS_PER_SECOND));
		}
		return state;
		}

		enum class SmokeInventoryPacketOp : Uint8
		{
			USEI = 0,
			EQUI,
			EQUS,
			EQUM_VALID,
			COOK,
			EQUM_INVALID,
			COOK_ZERO
		};

		constexpr int kSmokeInventoryBaseActionCount = 5;
		constexpr int kSmokeInventoryEdgeActionCount = 7;

		struct SmokeInventoryPacketState
		{
			bool initialized = false;
			bool enabled = false;
			bool includeEdgeCases = false;
			bool completionLogged = false;
			int expectedPlayers = 2;
			int driverClientSlot = 1;
			int pulses = 0;
			int actionIndex = 0;
			int pulseIndex = 0;
			Uint32 delayTicks = 0;
			Uint32 nextSendTick = 0;
			bool readyArmed = false;
		};

		static SmokeInventoryPacketState g_smokeInventoryPackets;

		SmokeInventoryPacketState& smokeInventoryPacketState()
		{
			SmokeInventoryPacketState& state = g_smokeInventoryPackets;
			if ( state.initialized )
			{
				return state;
			}
			state.initialized = true;

			const bool smokeEnabled = parseEnvBool("BARONY_SMOKE_AUTOPILOT", false);
			const std::string smokeRole = toLowerCopy(std::getenv("BARONY_SMOKE_ROLE"));
			const bool smokeClient = smokeRole == "client";
			state.pulses = parseEnvInt("BARONY_SMOKE_AUTO_INVENTORY_PULSES", 0, 0, 512);
			state.includeEdgeCases = parseEnvBool("BARONY_SMOKE_AUTO_INVENTORY_INCLUDE_EDGE_CASES", false);
			state.expectedPlayers = parseEnvInt("BARONY_SMOKE_EXPECTED_PLAYERS", 2, 1, MAXPLAYERS);
			state.driverClientSlot = parseEnvInt("BARONY_SMOKE_AUTO_INVENTORY_CLIENT_SLOT", 0, 0, MAXPLAYERS - 1);
			const int delaySeconds = parseEnvInt("BARONY_SMOKE_AUTO_INVENTORY_DELAY_SECS", 0, 0, 120);
			state.delayTicks = static_cast<Uint32>(delaySeconds * TICKS_PER_SECOND);
			const bool configRequested =
				envHasValue("BARONY_SMOKE_AUTO_INVENTORY_PULSES")
				|| envHasValue("BARONY_SMOKE_AUTO_INVENTORY_INCLUDE_EDGE_CASES")
				|| envHasValue("BARONY_SMOKE_AUTO_INVENTORY_CLIENT_SLOT")
				|| envHasValue("BARONY_SMOKE_AUTO_INVENTORY_DELAY_SECS");
			state.enabled = smokeEnabled && smokeClient && state.pulses > 0;
			if ( configRequested )
			{
				printlog("[SMOKE]: inventory autopilot config smoke=%d role=%s pulses=%d edge_cases=%d expected=%d client_slot=%d delay=%u enabled=%d",
					smokeEnabled ? 1 : 0, smokeRole.c_str(), state.pulses, state.includeEdgeCases ? 1 : 0,
					state.expectedPlayers, state.driverClientSlot,
					static_cast<unsigned>(state.delayTicks / TICKS_PER_SECOND),
					state.enabled ? 1 : 0);
			}
			if ( state.enabled )
			{
				printlog("[SMOKE]: inventory autopilot enabled expected=%d client_slot=%d pulses=%d edge_cases=%d delay=%u",
					state.expectedPlayers, state.driverClientSlot, state.pulses, state.includeEdgeCases ? 1 : 0,
					static_cast<unsigned>(state.delayTicks / TICKS_PER_SECOND));
			}
			return state;
		}

		int smokeInventoryActionCount(const SmokeInventoryPacketState& state)
		{
			return state.includeEdgeCases ? kSmokeInventoryEdgeActionCount : kSmokeInventoryBaseActionCount;
		}

		SmokeInventoryPacketOp smokeInventoryActionAt(const SmokeInventoryPacketState& state, const int actionIndex)
		{
			if ( actionIndex < 0 )
			{
				return SmokeInventoryPacketOp::USEI;
			}
			switch ( actionIndex )
			{
				case 0:
					return SmokeInventoryPacketOp::USEI;
				case 1:
					return SmokeInventoryPacketOp::EQUI;
				case 2:
					return SmokeInventoryPacketOp::EQUS;
				case 3:
					return SmokeInventoryPacketOp::EQUM_VALID;
				case 4:
					return SmokeInventoryPacketOp::COOK;
				case 5:
					return state.includeEdgeCases ? SmokeInventoryPacketOp::EQUM_INVALID : SmokeInventoryPacketOp::USEI;
				case 6:
					return state.includeEdgeCases ? SmokeInventoryPacketOp::COOK_ZERO : SmokeInventoryPacketOp::USEI;
				default:
					return SmokeInventoryPacketOp::USEI;
			}
		}

		const char* smokeInventoryOpName(const SmokeInventoryPacketOp op)
		{
			switch ( op )
			{
				case SmokeInventoryPacketOp::USEI:
					return "USEI";
				case SmokeInventoryPacketOp::EQUI:
					return "EQUI";
				case SmokeInventoryPacketOp::EQUS:
					return "EQUS";
				case SmokeInventoryPacketOp::EQUM_VALID:
				case SmokeInventoryPacketOp::EQUM_INVALID:
					return "EQUM";
				case SmokeInventoryPacketOp::COOK:
				case SmokeInventoryPacketOp::COOK_ZERO:
					return "COOK";
				default:
					return "UNKN";
			}
		}

		bool sendSmokeInventoryPacket(const SmokeInventoryPacketOp op, const Uint8 clientSlot,
			int& outCount, int& outSlot, const char*& outEdge)
		{
			std::array<Uint8, NET_PACKET_SIZE> packetBytes = {};
			UDPpacket packet = {};
			packet.data = packetBytes.data();
			packet.maxlen = static_cast<int>(packetBytes.size());
			packet.address.host = net_server.host;
			packet.address.port = net_server.port;
			outCount = 1;
			outSlot = -1;
			outEdge = "none";

			auto writeBase = [&](const char* opCode, const ItemType type, const Status status,
				const Sint16 beatitude, const int count, const Uint32 appearance, const bool identified) {
				memcpy(packet.data, opCode, 4);
				SDLNet_Write32(static_cast<Uint32>(type), &packet.data[4]);
				SDLNet_Write32(static_cast<Uint32>(status), &packet.data[8]);
				SDLNet_Write32(static_cast<Uint32>(beatitude), &packet.data[12]);
				SDLNet_Write32(static_cast<Uint32>(count), &packet.data[16]);
				SDLNet_Write32(static_cast<Uint32>(appearance), &packet.data[20]);
				packet.data[24] = identified ? 1 : 0;
				packet.data[25] = clientSlot;
				packet.len = 26;
				outCount = count;
			};

			switch ( op )
			{
				case SmokeInventoryPacketOp::USEI:
					writeBase("USEI", FOOD_BREAD, SERVICABLE, 0, 1, 0, true);
					break;
				case SmokeInventoryPacketOp::EQUI:
					writeBase("EQUI", BRONZE_SWORD, SERVICABLE, 0, 1, 0, true);
					packet.data[26] = static_cast<Uint8>(EQUIP_ITEM_SUCCESS_UPDATE_QTY);
					packet.data[27] = static_cast<Uint8>(EQUIP_ITEM_SLOT_WEAPON);
					packet.len = 28;
					outSlot = static_cast<int>(EQUIP_ITEM_SLOT_WEAPON);
					break;
				case SmokeInventoryPacketOp::EQUS:
					writeBase("EQUS", WOODEN_SHIELD, SERVICABLE, 0, 1, 0, true);
					packet.data[26] = static_cast<Uint8>(EQUIP_ITEM_SUCCESS_UPDATE_QTY);
					packet.data[27] = static_cast<Uint8>(EQUIP_ITEM_SLOT_SHIELD);
					packet.len = 28;
					outSlot = static_cast<int>(EQUIP_ITEM_SLOT_SHIELD);
					break;
				case SmokeInventoryPacketOp::EQUM_VALID:
					writeBase("EQUM", WOODEN_SHIELD, SERVICABLE, 0, 1, 0, true);
					packet.data[26] = static_cast<Uint8>(EQUIP_ITEM_SUCCESS_UPDATE_QTY);
					packet.data[27] = static_cast<Uint8>(EQUIP_ITEM_SLOT_SHIELD);
					packet.len = 28;
					outSlot = static_cast<int>(EQUIP_ITEM_SLOT_SHIELD);
					break;
				case SmokeInventoryPacketOp::COOK:
					writeBase("COOK", TOOL_TORCH, SERVICABLE, 0, 1, 0, true);
					break;
				case SmokeInventoryPacketOp::EQUM_INVALID:
					writeBase("EQUM", LEATHER_HELM, SERVICABLE, 0, 1, 0, true);
					packet.data[26] = static_cast<Uint8>(EQUIP_ITEM_SUCCESS_UPDATE_QTY);
					packet.data[27] = static_cast<Uint8>(255);
					packet.len = 28;
					outSlot = 255;
					outEdge = "invalid-slot";
					break;
				case SmokeInventoryPacketOp::COOK_ZERO:
					writeBase("COOK", TOOL_TORCH, SERVICABLE, 0, 0, 0, true);
					outEdge = "count-zero";
					break;
				default:
					return false;
			}

			return sendPacketSafe(net_sock, -1, &packet, 0);
		}

		int firstConnectedRemoteSlot(const int expectedPlayers)
		{
			for ( int slot = 1; slot < MAXPLAYERS; ++slot )
		{
			if ( slot >= expectedPlayers )
			{
				break;
			}
			if ( client_disconnected[slot] )
			{
				continue;
			}
			if ( !players[slot] || !players[slot]->entity )
			{
				continue;
			}
			return slot;
		}
		return -1;
	}

	constexpr int kSmokeLocalMaxPlayers = 4;

	struct SmokeLocalSplitscreenState
	{
		bool initialized = false;
		bool enabled = false;
		bool traceEnabled = false;
		int expectedPlayers = kSmokeLocalMaxPlayers;
		int autoPausePulses = 0;
		Uint32 autoPauseDelayTicks = 0;
		Uint32 autoPauseHoldTicks = 0;
		bool readyArmed = false;
		Uint32 readySinceTick = 0;
		Uint32 nextPauseTick = 0;
		bool pauseActive = false;
		int pauseActionsIssued = 0;
		bool baselineLoggedOk = false;
		bool baselineLoggedFail = false;
		bool pauseCompleteLogged = false;
		bool transitionLogged = false;
	};

	static SmokeLocalSplitscreenState g_smokeLocalSplitscreen;

	SmokeLocalSplitscreenState& smokeLocalSplitscreenState()
	{
		SmokeLocalSplitscreenState& state = g_smokeLocalSplitscreen;
		if ( state.initialized )
		{
			return state;
		}
		state.initialized = true;

		const bool smokeEnabled = parseEnvBool("BARONY_SMOKE_AUTOPILOT", false);
		const std::string smokeRole = toLowerCopy(std::getenv("BARONY_SMOKE_ROLE"));
		const bool smokeLocal = smokeRole == "local";
		state.traceEnabled = smokeEnabled && parseEnvBool("BARONY_SMOKE_TRACE_LOCAL_SPLITSCREEN", false);
		state.expectedPlayers = parseEnvInt("BARONY_SMOKE_EXPECTED_PLAYERS",
			kSmokeLocalMaxPlayers, 1, kSmokeLocalMaxPlayers);
		state.autoPausePulses = parseEnvInt("BARONY_SMOKE_LOCAL_PAUSE_PULSES", 0, 0, 64);
		state.autoPauseDelayTicks = static_cast<Uint32>(
			parseEnvInt("BARONY_SMOKE_LOCAL_PAUSE_DELAY_SECS", 2, 0, 120) * TICKS_PER_SECOND);
		state.autoPauseHoldTicks = static_cast<Uint32>(
			parseEnvInt("BARONY_SMOKE_LOCAL_PAUSE_HOLD_SECS", 1, 0, 120) * TICKS_PER_SECOND);
		state.enabled = smokeEnabled && smokeLocal &&
			(state.traceEnabled || state.autoPausePulses > 0);

		if ( !state.enabled )
		{
			return state;
		}

		printlog("[SMOKE]: local-splitscreen baseline enabled expected=%d trace=%d pause_pulses=%d pause_delay=%u hold=%u",
			state.expectedPlayers,
			state.traceEnabled ? 1 : 0,
			state.autoPausePulses,
			static_cast<unsigned>(state.autoPauseDelayTicks / TICKS_PER_SECOND),
			static_cast<unsigned>(state.autoPauseHoldTicks / TICKS_PER_SECOND));
		return state;
	}

	struct SmokeLocalSplitscreenCapState
	{
		bool initialized = false;
		bool enabled = false;
		bool traceEnabled = false;
		int targetPlayers = 0;
		int cappedPlayers = 0;
		Uint32 commandDelayTicks = 0;
		Uint32 verifyDelayTicks = 0;
		bool armed = false;
		Uint32 armTick = 0;
		bool commandIssued = false;
		Uint32 verifyTick = 0;
		bool loggedOk = false;
		bool loggedFail = false;
	};

	static SmokeLocalSplitscreenCapState g_smokeLocalSplitscreenCap;

	SmokeLocalSplitscreenCapState& smokeLocalSplitscreenCapState()
	{
		SmokeLocalSplitscreenCapState& state = g_smokeLocalSplitscreenCap;
		if ( state.initialized )
		{
			return state;
		}
		state.initialized = true;

		const bool smokeEnabled = parseEnvBool("BARONY_SMOKE_AUTOPILOT", false);
		const std::string smokeRole = toLowerCopy(std::getenv("BARONY_SMOKE_ROLE"));
		const bool smokeLocal = smokeRole == "local";
		state.traceEnabled = parseEnvBool("BARONY_SMOKE_TRACE_LOCAL_SPLITSCREEN_CAP", false);
		state.targetPlayers = parseEnvInt("BARONY_SMOKE_AUTO_SPLITSCREEN_CAP_TARGET", 0, 0, MAXPLAYERS);
		state.cappedPlayers = std::max(2, std::min(state.targetPlayers, kSmokeLocalMaxPlayers));
		state.commandDelayTicks = static_cast<Uint32>(
			parseEnvInt("BARONY_SMOKE_SPLITSCREEN_CAP_DELAY_SECS", 2, 0, 120) * TICKS_PER_SECOND);
		state.verifyDelayTicks = static_cast<Uint32>(
			parseEnvInt("BARONY_SMOKE_SPLITSCREEN_CAP_VERIFY_DELAY_SECS", 1, 0, 120) * TICKS_PER_SECOND);
		state.enabled = smokeEnabled && smokeLocal && state.targetPlayers >= 2;

		if ( !state.enabled )
		{
			return state;
		}

		printlog("[SMOKE]: local-splitscreen cap enabled target=%d cap=%d trace=%d delay=%u verify_delay=%u",
			state.targetPlayers,
			state.cappedPlayers,
			state.traceEnabled ? 1 : 0,
			static_cast<unsigned>(state.commandDelayTicks / TICKS_PER_SECOND),
			static_cast<unsigned>(state.verifyDelayTicks / TICKS_PER_SECOND));
		return state;
	}

	int smokeLocalConnectedPlayers(const int expectedPlayers)
	{
		const int limit = std::max(1, std::min(expectedPlayers, kSmokeLocalMaxPlayers));
		int connected = 0;
		for ( int slot = 0; slot < limit; ++slot )
		{
			if ( !client_disconnected[slot] )
			{
				++connected;
			}
		}
		return connected;
	}

	int smokeLocalLoadedPlayers(const int expectedPlayers)
	{
		const int limit = std::max(1, std::min(expectedPlayers, kSmokeLocalMaxPlayers));
		int loaded = 0;
		for ( int slot = 0; slot < limit; ++slot )
		{
			if ( client_disconnected[slot] )
			{
				continue;
			}
			if ( players[slot] && players[slot]->entity )
			{
				++loaded;
			}
		}
		return loaded;
	}

	bool smokeLocalCameraLayoutOk(const int expectedPlayers, int& localSlotsOk,
		int& vmouseFailures, int& hudMissing)
	{
		localSlotsOk = 1;
		vmouseFailures = 0;
		hudMissing = 0;

		const int limit = std::max(1, std::min(expectedPlayers, kSmokeLocalMaxPlayers));
		int connected = 0;
		for ( int slot = 0; slot < limit; ++slot )
		{
			if ( !client_disconnected[slot] )
			{
				++connected;
			}
		}
		if ( connected <= 0 )
		{
			return false;
		}

		bool cameraOk = true;
		int playerIndex = 0;
		for ( int slot = 0; slot < limit; ++slot )
		{
			if ( client_disconnected[slot] )
			{
				continue;
			}
			if ( !players[slot] )
			{
				localSlotsOk = 0;
				cameraOk = false;
				++playerIndex;
				continue;
			}
			if ( !players[slot]->isLocalPlayer() )
			{
				localSlotsOk = 0;
			}
			if ( !players[slot]->hud.hudFrame )
			{
				++hudMissing;
			}

			int expectedX = 0;
			int expectedY = 0;
			int expectedW = xres;
			int expectedH = yres;
			if ( connected >= 3 )
			{
				expectedX = (playerIndex % 2) * xres / 2;
				expectedY = (playerIndex / 2) * yres / 2;
				expectedW = xres / 2;
				expectedH = yres / 2;
			}

			if ( connected >= 3 )
			{
				if ( players[slot]->camera_x1() != expectedX
					|| players[slot]->camera_y1() != expectedY
					|| players[slot]->camera_width() != expectedW
					|| players[slot]->camera_height() != expectedH )
				{
					cameraOk = false;
				}
			}

				if ( decltype(inputs.getVirtualMouse(slot)) vmouse = inputs.getVirtualMouse(slot) )
				{
					const int minX = players[slot]->camera_x1();
				const int minY = players[slot]->camera_y1();
				const int maxX = players[slot]->camera_x2();
				const int maxY = players[slot]->camera_y2();
				if ( vmouse->x < minX || vmouse->x >= maxX
					|| vmouse->y < minY || vmouse->y >= maxY )
				{
					++vmouseFailures;
				}
			}
			else
			{
				++vmouseFailures;
			}
			++playerIndex;
		}

		return cameraOk;
	}
}

namespace SmokeTestHooks
{
namespace Gameplay
{
	void tickMapgenSurvivalGuard(SmokeAutoEnterDungeonState& smoke, const bool allowSingle)
	{
		if ( !smoke.preventDeath || smoke.hpFloor <= 0 )
		{
			return;
		}
		if ( client_disconnected[0] || !players[0] || !players[0]->entity || !stats[0] )
		{
			return;
		}

		if ( !(svFlags & SV_FLAG_CHEATS) )
		{
			consoleCommand("/enablecheats");
		}

		if ( allowSingle )
		{
			if ( !godmode )
			{
				consoleCommand("/god");
			}
		}
		else
		{
			godmode = true;
		}
		buddhamode = true;

		Stat* hostStats = stats[0];
		hostStats->MAXHP = std::max<Sint32>(hostStats->MAXHP, static_cast<Sint32>(smoke.hpFloor));
		hostStats->HP = std::max<Sint32>(hostStats->HP, static_cast<Sint32>(smoke.hpFloor));
		hostStats->OLDHP = hostStats->HP;

		if ( !smoke.preventDeathLogged )
		{
			smoke.preventDeathLogged = true;
			printlog("[SMOKE]: mapgen survival guard active mode=%s hp_floor=%d",
				allowSingle ? "console-god" : "forced-god", smoke.hpFloor);
		}
	}

	void tickAutoEnterDungeon()
	{
		SmokeAutoEnterDungeonState& smoke = smokeAutoEnterDungeonState();
		if ( !smoke.enabled )
		{
			return;
		}
		const bool allowServer = multiplayer == SERVER;
		const bool allowSingle = smoke.allowSingleplayer && multiplayer == SINGLE;
		if ( !allowServer && !allowSingle )
		{
			return;
		}
		tickMapgenSurvivalGuard(smoke, allowSingle);
		if ( smoke.transitionsIssued >= smoke.maxTransitions )
		{
			return;
		}
		if ( loadnextlevel )
		{
			return;
		}

		const int connected = smokeConnectedPlayers();
		if ( connected < smoke.expectedPlayers || !smokeConnectedPlayersLoaded() )
		{
			smoke.readySinceTick = 0;
			smoke.readyWindowStarted = false;
			return;
		}

		if ( !smoke.readyWindowStarted )
		{
			smoke.readyWindowStarted = true;
			smoke.readySinceTick = ticks;
			printlog("[SMOKE]: expected players loaded in starting area (%d/%d), entering dungeon in %u sec",
				connected, smoke.expectedPlayers, static_cast<unsigned>(smoke.delayTicks / TICKS_PER_SECOND));
		}
		if ( ticks - smoke.readySinceTick < smoke.delayTicks )
		{
			return;
		}

			smoke.readySinceTick = 0;
			smoke.readyWindowStarted = false;
			++smoke.transitionsIssued;
			const bool reloadSameDungeonLevel = smoke.reloadSameLevel && currentlevel > 0;
			if ( reloadSameDungeonLevel )
			{
				skipLevelsOnLoad = -1;
				loadingSameLevelAsCurrent = true;
				if ( smoke.reloadSeedBase > 0 )
				{
					forceMapSeed = smoke.reloadSeedBase + static_cast<Uint32>(smoke.transitionsIssued - 1);
				}
			}
			loadnextlevel = true;
			Compendium_t::Events_t::previousCurrentLevel = currentlevel;
			if ( reloadSameDungeonLevel )
			{
				printlog("[SMOKE]: auto-reloading dungeon level transition %d/%d from level %d seed=%u",
					smoke.transitionsIssued, smoke.maxTransitions, currentlevel, static_cast<unsigned>(forceMapSeed));
			}
			else
			{
				printlog("[SMOKE]: auto-entering dungeon transition %d/%d from level %d",
					smoke.transitionsIssued, smoke.maxTransitions, currentlevel);
			}
		}

		void tickRemoteCombatAutopilot()
		{
			SmokeRemoteCombatState& smoke = smokeRemoteCombatState();
			if ( !smoke.hostAutopilotEnabled )
		{
			return;
		}
		if ( multiplayer != SERVER )
		{
			return;
		}
		if ( loadnextlevel )
		{
			return;
		}

		const int connected = smokeConnectedPlayers();
		if ( connected < smoke.expectedPlayers || !smokeConnectedPlayersLoaded() )
		{
			smoke.readyArmed = false;
			return;
		}

		if ( !smoke.readyArmed )
		{
			smoke.readyArmed = true;
			smoke.nextPauseTick = ticks + smoke.autoPauseDelayTicks;
			smoke.nextCombatTick = ticks + smoke.autoCombatDelayTicks;
			printlog("[SMOKE]: remote-combat autopilot armed connected=%d expected=%d",
				connected, smoke.expectedPlayers);
		}

		if ( smoke.autoCombatPulses > 0 && smoke.combatPulsesIssued < smoke.autoCombatPulses
			&& ticks >= smoke.nextCombatTick )
		{
			int sourceSlot = -1;
			if ( players[0] && players[0]->entity && !client_disconnected[0] )
			{
				sourceSlot = 0;
			}
			const int targetSlot = firstConnectedRemoteSlot(smoke.expectedPlayers);
			bool enemyBarOk = false;
			bool damageGibOk = false;
			if ( sourceSlot >= 0 && targetSlot >= 1 && targetSlot < MAXPLAYERS
				&& players[targetSlot] && players[targetSlot]->entity )
			{
				Entity* source = players[sourceSlot]->entity;
				Entity* target = players[targetSlot]->entity;
				Stat* targetStats = target->getStats();
				const char* targetName = "Remote Player";
				Sint32 targetHp = 1;
				Sint32 targetMaxHp = 1;
				if ( targetStats )
				{
					if ( targetStats->name[0] != '\0' )
					{
						targetName = targetStats->name;
					}
					targetHp = std::max(1, targetStats->HP);
					targetMaxHp = std::max(targetHp, std::max(1, targetStats->MAXHP));
				}
				updateEnemyBar(source, target, targetName, targetHp, targetMaxHp, false, DMG_DEFAULT);
				enemyBarOk = true;
				if ( spawnDamageGib(target, 4, DamageGib::DMG_DEFAULT,
					DamageGibDisplayType::DMG_GIB_NUMBER, true) )
				{
					damageGibOk = true;
					Combat::traceRemoteCombatEvent("auto-dmgg-pulse", targetSlot);
				}
				Combat::traceRemoteCombatEvent("auto-enemy-bar-pulse", targetSlot);
			}

			++smoke.combatPulsesIssued;
			smoke.nextCombatTick = ticks + smoke.autoCombatDelayTicks;
			const bool ok = enemyBarOk && damageGibOk;
			printlog("[SMOKE]: remote-combat auto-event action=enemy-bar pulse=%d/%d source_slot=%d target_slot=%d enemy_bar=%d damage_gib=%d status=%s",
				smoke.combatPulsesIssued, smoke.autoCombatPulses, sourceSlot, targetSlot,
				enemyBarOk ? 1 : 0, damageGibOk ? 1 : 0, ok ? "ok" : "fail");
			if ( smoke.combatPulsesIssued >= smoke.autoCombatPulses && !smoke.combatCompleteLogged )
			{
				smoke.combatCompleteLogged = true;
				printlog("[SMOKE]: remote-combat auto-event complete pulses=%d", smoke.autoCombatPulses);
			}
		}

		const int completedPausePulses = smoke.pauseActionsIssued / 2;
		if ( smoke.autoPausePulses > 0 && (completedPausePulses < smoke.autoPausePulses)
			&& ticks >= smoke.nextPauseTick )
		{
			if ( !smoke.pauseActive )
			{
				pauseGame(2, 0);
				smoke.pauseActive = true;
				++smoke.pauseActionsIssued;
				smoke.nextPauseTick = ticks + smoke.autoPauseHoldTicks;
				Combat::traceRemoteCombatEvent("auto-pause-issued", 0);
				printlog("[SMOKE]: remote-combat auto-pause action=pause pulse=%d/%d",
					completedPausePulses + 1, smoke.autoPausePulses);
			}
			else
			{
				pauseGame(1, 0);
				smoke.pauseActive = false;
				++smoke.pauseActionsIssued;
				smoke.nextPauseTick = ticks + smoke.autoPauseDelayTicks;
				Combat::traceRemoteCombatEvent("auto-unpause-issued", 0);
				printlog("[SMOKE]: remote-combat auto-pause action=unpause pulse=%d/%d",
					completedPausePulses + 1, smoke.autoPausePulses);
			}
		}

		if ( !smoke.pauseCompleteLogged && (smoke.pauseActionsIssued / 2) >= smoke.autoPausePulses
			&& smoke.autoPausePulses > 0 )
		{
			smoke.pauseCompleteLogged = true;
				printlog("[SMOKE]: remote-combat auto-pause complete pulses=%d", smoke.autoPausePulses);
			}
		}

		void tickInventoryPacketAutopilot()
		{
			if ( multiplayer != CLIENT )
			{
				return;
			}
			SmokeInventoryPacketState& smoke = smokeInventoryPacketState();
			if ( !smoke.enabled )
			{
				return;
			}
			if ( smoke.driverClientSlot > 0 && clientnum != smoke.driverClientSlot )
			{
				return;
			}
			if ( net_server.host == 0 || net_server.port == 0 )
			{
				return;
			}
			if ( loadnextlevel )
			{
				smoke.readyArmed = false;
				return;
			}
			if ( currentlevel <= 0 )
			{
				smoke.readyArmed = false;
				return;
			}

			const int connected = smokeConnectedPlayers();
			if ( connected < smoke.expectedPlayers || !smokeConnectedPlayersLoaded() )
			{
				smoke.readyArmed = false;
				return;
			}

			if ( smoke.pulseIndex >= smoke.pulses )
			{
				if ( !smoke.completionLogged )
				{
					smoke.completionLogged = true;
					printlog("[SMOKE]: inventory autopilot complete pulses=%d actions_per_pulse=%d edge_cases=%d",
						smoke.pulses, smokeInventoryActionCount(smoke), smoke.includeEdgeCases ? 1 : 0);
				}
				return;
			}

			if ( !smoke.readyArmed )
			{
				smoke.readyArmed = true;
				smoke.nextSendTick = ticks + smoke.delayTicks;
				printlog("[SMOKE]: inventory autopilot armed level=%d connected=%d expected=%d",
					currentlevel, connected, smoke.expectedPlayers);
				return;
			}

			if ( ticks < smoke.nextSendTick )
			{
				return;
			}

			const int actionCount = smokeInventoryActionCount(smoke);
			if ( actionCount <= 0 )
			{
				return;
			}

			const SmokeInventoryPacketOp op = smokeInventoryActionAt(smoke, smoke.actionIndex);
			int sentCount = 0;
			int sentSlot = -1;
			const char* edge = "none";
			const bool sent = sendSmokeInventoryPacket(op, static_cast<Uint8>(clientnum), sentCount, sentSlot, edge);
			printlog("[SMOKE]: inventory autopilot send op=%s pulse=%d/%d action=%d/%d count=%d slot=%d edge=%s status=%s",
				smokeInventoryOpName(op),
				smoke.pulseIndex + 1, smoke.pulses,
				smoke.actionIndex + 1, actionCount,
				sentCount, sentSlot, edge,
				sent ? "ok" : "fail");

			++smoke.actionIndex;
			if ( smoke.actionIndex >= actionCount )
			{
				smoke.actionIndex = 0;
				++smoke.pulseIndex;
			}
			smoke.nextSendTick = ticks + smoke.delayTicks;
		}

		void tickLocalSplitscreenBaseline()
		{
			SmokeLocalSplitscreenState& smoke = smokeLocalSplitscreenState();
		if ( !smoke.enabled )
		{
			return;
		}
		if ( multiplayer != SINGLE )
		{
			return;
		}

		const int expected = std::max(1, std::min(smoke.expectedPlayers, kSmokeLocalMaxPlayers));
		const int connected = smokeLocalConnectedPlayers(expected);
		const int loaded = smokeLocalLoadedPlayers(expected);
		const bool splitscreenOk = expected >= 2 ? splitscreen : !splitscreen;

		int localSlotsOk = 0;
		int vmouseFailures = 0;
		int hudMissing = 0;
		const bool cameraOk = smokeLocalCameraLayoutOk(expected, localSlotsOk, vmouseFailures, hudMissing);
		const bool baselineOk = connected >= expected
			&& loaded >= expected
			&& splitscreenOk
			&& localSlotsOk == 1
			&& cameraOk
			&& vmouseFailures == 0
			&& hudMissing == 0;

		if ( smoke.traceEnabled && baselineOk && !smoke.baselineLoggedOk )
		{
			smoke.baselineLoggedOk = true;
			printlog("[SMOKE]: local-splitscreen baseline status=ok expected=%d connected=%d loaded=%d splitscreen=%d local_slots_ok=%d camera_ok=%d vmouse_failures=%d hud_missing=%d",
				expected, connected, loaded, splitscreen ? 1 : 0, localSlotsOk,
				cameraOk ? 1 : 0, vmouseFailures, hudMissing);
		}
		else if ( smoke.traceEnabled && !baselineOk && !smoke.baselineLoggedFail )
		{
			smoke.baselineLoggedFail = true;
			printlog("[SMOKE]: local-splitscreen baseline status=wait expected=%d connected=%d loaded=%d splitscreen=%d local_slots_ok=%d camera_ok=%d vmouse_failures=%d hud_missing=%d",
				expected, connected, loaded, splitscreen ? 1 : 0, localSlotsOk,
				cameraOk ? 1 : 0, vmouseFailures, hudMissing);
		}

		if ( !baselineOk )
		{
			smoke.readyArmed = false;
			return;
		}

		if ( !smoke.readyArmed )
		{
			smoke.readyArmed = true;
			smoke.readySinceTick = ticks;
			smoke.nextPauseTick = ticks + smoke.autoPauseDelayTicks;
			printlog("[SMOKE]: local-splitscreen baseline armed expected=%d", expected);
		}

		const int completedPausePulses = smoke.pauseActionsIssued / 2;
		if ( smoke.autoPausePulses > 0
			&& completedPausePulses < smoke.autoPausePulses
			&& ticks >= smoke.nextPauseTick )
		{
			if ( !smoke.pauseActive )
			{
				pauseGame(2, 0);
				smoke.pauseActive = true;
				++smoke.pauseActionsIssued;
				smoke.nextPauseTick = ticks + smoke.autoPauseHoldTicks;
				printlog("[SMOKE]: local-splitscreen auto-pause action=pause pulse=%d/%d gamePaused=%d",
					completedPausePulses + 1, smoke.autoPausePulses, gamePaused ? 1 : 0);
			}
			else
			{
				pauseGame(1, 0);
				smoke.pauseActive = false;
				++smoke.pauseActionsIssued;
				smoke.nextPauseTick = ticks + smoke.autoPauseDelayTicks;
				printlog("[SMOKE]: local-splitscreen auto-pause action=unpause pulse=%d/%d gamePaused=%d",
					completedPausePulses + 1, smoke.autoPausePulses, gamePaused ? 1 : 0);
			}
		}

		if ( !smoke.pauseCompleteLogged
			&& smoke.autoPausePulses > 0
			&& (smoke.pauseActionsIssued / 2) >= smoke.autoPausePulses )
		{
			smoke.pauseCompleteLogged = true;
			printlog("[SMOKE]: local-splitscreen auto-pause complete pulses=%d", smoke.autoPausePulses);
		}

		if ( !smoke.transitionLogged && currentlevel > 0 )
		{
			smoke.transitionLogged = true;
			printlog("[SMOKE]: local-splitscreen transition level=%d status=ok", currentlevel);
		}
	}

	void tickLocalSplitscreenCap()
	{
		SmokeLocalSplitscreenCapState& smoke = smokeLocalSplitscreenCapState();
		if ( !smoke.enabled )
		{
			return;
		}
		if ( multiplayer != SINGLE )
		{
			return;
		}
		if ( smoke.targetPlayers < 2 )
		{
			return;
		}
		if ( !players[0] || !players[0]->entity )
		{
			return;
		}

		if ( !smoke.armed )
		{
			smoke.armed = true;
			smoke.armTick = ticks;
			if ( smoke.traceEnabled )
			{
				printlog("[SMOKE]: local-splitscreen cap armed target=%d cap=%d issue_in=%u",
					smoke.targetPlayers,
					smoke.cappedPlayers,
					static_cast<unsigned>(smoke.commandDelayTicks / TICKS_PER_SECOND));
			}
			return;
		}

		if ( !smoke.commandIssued
			&& ticks - smoke.armTick >= smoke.commandDelayTicks )
		{
			consoleCommand("/enablecheats");
			if ( splitscreen )
			{
				consoleCommand("/splitscreen");
			}

			char command[64] = "";
			snprintf(command, sizeof(command), "/splitscreen %d", smoke.targetPlayers);
			consoleCommand(command);
			smoke.commandIssued = true;
			smoke.verifyTick = ticks + smoke.verifyDelayTicks;
			printlog("[SMOKE]: local-splitscreen cap command issued target=%d cap=%d verify_after=%u",
				smoke.targetPlayers,
				smoke.cappedPlayers,
				static_cast<unsigned>(smoke.verifyDelayTicks / TICKS_PER_SECOND));
			return;
		}

		if ( !smoke.commandIssued || ticks < smoke.verifyTick )
		{
			return;
		}

		int connectedPlayers = 0;
		int connectedLocalPlayers = 0;
		int overCapConnected = 0;
		int overCapLocal = 0;
		int overCapSplitscreen = 0;
		int underCapNonLocal = 0;

		for ( int slot = 0; slot < MAXPLAYERS; ++slot )
		{
			const bool connected = !client_disconnected[slot];
			const bool localPlayer = players[slot] && players[slot]->isLocalPlayer();
			if ( connected )
			{
				++connectedPlayers;
				if ( localPlayer )
				{
					++connectedLocalPlayers;
				}
				else if ( slot < smoke.cappedPlayers )
				{
					++underCapNonLocal;
				}
			}

			if ( slot >= smoke.cappedPlayers )
			{
				if ( connected )
				{
					++overCapConnected;
				}
				if ( localPlayer )
				{
					++overCapLocal;
				}
				if ( players[slot] && players[slot]->bSplitscreen )
				{
					++overCapSplitscreen;
				}
			}
		}

		const bool ok = splitscreen
			&& connectedPlayers == smoke.cappedPlayers
			&& connectedLocalPlayers == smoke.cappedPlayers
			&& overCapConnected == 0
			&& overCapLocal == 0
			&& overCapSplitscreen == 0
			&& underCapNonLocal == 0;

		if ( ok )
		{
			if ( !smoke.loggedOk )
			{
				smoke.loggedOk = true;
				printlog("[SMOKE]: local-splitscreen cap status=ok target=%d cap=%d connected=%d connected_local=%d over_cap_connected=%d over_cap_local=%d over_cap_splitscreen=%d under_cap_nonlocal=%d",
					smoke.targetPlayers,
					smoke.cappedPlayers,
					connectedPlayers,
					connectedLocalPlayers,
					overCapConnected,
					overCapLocal,
					overCapSplitscreen,
					underCapNonLocal);
			}
			return;
		}

		if ( !smoke.loggedFail )
		{
			smoke.loggedFail = true;
			printlog("[SMOKE]: local-splitscreen cap status=fail target=%d cap=%d connected=%d connected_local=%d over_cap_connected=%d over_cap_local=%d over_cap_splitscreen=%d under_cap_nonlocal=%d splitscreen=%d",
				smoke.targetPlayers,
				smoke.cappedPlayers,
				connectedPlayers,
				connectedLocalPlayers,
				overCapConnected,
				overCapLocal,
				overCapSplitscreen,
				underCapNonLocal,
				splitscreen ? 1 : 0);
		}
	}
}

}
