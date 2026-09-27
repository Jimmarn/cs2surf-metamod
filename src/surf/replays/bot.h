#ifndef SURF_REPLAYBOT_H
#define SURF_REPLAYBOT_H

#include "sdk/datatypes.h"
#include "surf_replay.h"

class CCSPlayerController;
class SurfPlayer;

namespace Surf::replaysystem::bot
{
	// Bot lifecycle management
	void SpawnBot();
	void KickBot();
	void MakeBotAlive();

	// Bot state management
	CCSPlayerController *GetBot();
	bool IsValidBot(CCSPlayerController *controller);
	SurfPlayer *GetBotPlayer();

	// Bot setup and configuration
	void InitializeBotForReplay(const ReplayHeader &header);

	// Bot spectator handling
	void SpectateBot(SurfPlayer *spectator);
} // namespace Surf::replaysystem::bot

#endif // SURF_REPLAYBOT_H
