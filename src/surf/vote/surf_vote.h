#pragma once
#include "common.h"
#include "surf/surf.h"
#include <string>

/*
 * Map voting for CS2Surf: !vote (rtv), !nominate, !nextmap, !timeleft, !maps and an automatic
 * end-of-map vote a few minutes before mp_timelimit runs out. Votes are cast with !1 .. !6.
 *
 * Map pool: cfg/cs2surf-maplist.txt (one map per line, "name" or "name:workshopid", # comments).
 * Settings: "vote" block in cfg/cs2surf-server-config.txt (see cfg/cs2surf-server-config.txt).
 */
namespace Surf
{
	namespace vote
	{
		void Init();
		void OnActivateServer();
		void OnClientDisconnect(CPlayerSlot slot);

		// Player commands
		void RequestRTV(SurfPlayer *player, const char *mapNamePart = ""); // !vote [map]: alone on the server = instant change
		void UnRTV(SurfPlayer *player);
		void Nominate(SurfPlayer *player, const char *mapNamePart);
		void CastVote(SurfPlayer *player, i32 option);
		void PrintNextMap(SurfPlayer *player);
		void PrintTimeLeft(SurfPlayer *player);
		void PrintMapList(SurfPlayer *player);

		// Server side
		bool StartVote(bool endOfMap, bool forced);
		void ReloadMapList();
		bool IsVoteRunning();
		std::string GetPanelHTML(SurfPlayer *target); // empty when no vote is running
	} // namespace vote
} // namespace Surf
