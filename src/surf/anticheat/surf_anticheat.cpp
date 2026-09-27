#include "surf/surf.h"
#include "surf_anticheat.h"
#include "surf/language/surf_language.h"
#include "surf/timer/surf_timer.h"
#include "utils/ctimer.h"
#include "utils/cvarquery.h"

#define INTEGRITY_CHECK_MIN_INTERVAL 1.0f
#define INTEGRITY_CHECK_MAX_INTERVAL 5.0f
#define KICK_DELAY                   5.0f
#define MINIMUM_FPS_MAX              64.0f
#define MAXIMUM_M_YAW                0.3f

static_function f64 KickPlayerInvalidSettings(CPlayerUserId userID)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(userID);
	if (player)
	{
		player->Kick("Invalid player settings");
	}
	return 0.0f;
}

static_function void ValidateCvar(CPlayerSlot nSlot, cvarquery::Status eStatus, const char *pszCvarName, const char *pszCvarValue)
{
	if (eStatus != cvarquery::Status::ValueIntact)
	{
		return;
	}
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(nSlot);
	if (SURF_STREQI(pszCvarName, "m_yaw"))
	{
		if (atof(pszCvarValue) > MAXIMUM_M_YAW)
		{
			player->languageService->PrintChat(true, false, "Kick Player m_yaw");
			player->languageService->PrintConsole(false, false, "Kick Player m_yaw (Console)");
			player->anticheatService->MarkHasInvalidCvars();
			player->timerService->TimerStop();
			StartTimer<CPlayerUserId>(KickPlayerInvalidSettings, player->GetClient()->GetUserID(), KICK_DELAY, true, true);
		}
	}
}

static_function f64 CheckClientCvars(CPlayerUserId userID)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(userID);
	if (!player || !player->anticheatService->ShouldCheckClientCvars())
	{
		return 0.0f;
	}
	cvarquery::Query(player->GetPlayerSlot(), "m_yaw", ValidateCvar);
	return RandomFloat(INTEGRITY_CHECK_MIN_INTERVAL, INTEGRITY_CHECK_MAX_INTERVAL);
}

void SurfAnticheatService::OnPlayerFullyConnect()
{
	this->hasValidCvars = true;
	StartTimer<CPlayerUserId>(CheckClientCvars, this->player->GetClient()->GetUserID(),
							  RandomFloat(INTEGRITY_CHECK_MIN_INTERVAL, INTEGRITY_CHECK_MAX_INTERVAL), true, true);
}
