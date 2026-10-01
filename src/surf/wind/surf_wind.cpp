#include "surf_wind.h"
#include "surf/option/surf_option.h"
#include "surf/language/surf_language.h"
#include "utils/simplecmds.h"
#include "utils/utils.h"
#include "utils/interfaces.h"

#include "tier0/memdbgon.h"

// Same numbers as the CounterStrikeSharp plugin: 2 s clips, a new one every second so they crossfade.
#define SURF_WIND_COOLDOWN 1.0f
#define SURF_WIND_CLIPS    8
#define SURF_WIND_LEVELS   4

static_global const f32 SURF_WIND_DEFAULT_THRESHOLDS[SURF_WIND_LEVELS] = {320.0f, 1000.0f, 2000.0f, 3000.0f};

// "wind" block of cfg/cs2surf-server-config.txt
struct WindConfig
{
	bool enabled = false;
	f32 thresholds[SURF_WIND_LEVELS] = {320.0f, 1000.0f, 2000.0f, 3000.0f};
	char path[128] = "sounds/windsound";
	bool loaded = false;
};

static_global WindConfig windConfig;

static_function void LoadWindConfig()
{
	if (windConfig.loaded)
	{
		return;
	}
	windConfig.loaded = true;
	KeyValues *kv = SurfOptionService::GetOptionKV("wind");
	if (!kv)
	{
		return;
	}
	windConfig.enabled = kv->GetBool("enabled", false);
	V_strncpy(windConfig.path, kv->GetString("path", "sounds/windsound"), sizeof(windConfig.path));
	const char *keys[SURF_WIND_LEVELS] = {"minSpeed", "level1", "level2", "level3"};
	for (i32 i = 0; i < SURF_WIND_LEVELS; i++)
	{
		windConfig.thresholds[i] = kv->GetFloat(keys[i], SURF_WIND_DEFAULT_THRESHOLDS[i]);
	}
}

static_global class SurfOptionServiceEventListener_Wind : public SurfOptionServiceEventListener
{
	virtual void OnPlayerPreferencesLoaded(SurfPlayer *player)
	{
		player->windService->Reset();
	}
} optionEventListener;

void SurfWindService::Init()
{
	SurfOptionService::RegisterEventListener(&optionEventListener);
}

bool SurfWindService::IsAvailable()
{
	LoadWindConfig();
	return windConfig.enabled;
}

void SurfWindService::Reset()
{
	this->enabled = this->player->optionService->GetPreferenceBool("wind", true);
	this->ordered = this->player->optionService->GetPreferenceBool("windOrdered", false);
	this->nextIndex = 0;
	this->lastSoundTime = 0.0f;
}

void SurfWindService::Toggle()
{
	this->enabled = !this->enabled;
	this->player->optionService->SetPreferenceBool("wind", this->enabled);
}

void SurfWindService::ToggleMode()
{
	this->ordered = !this->ordered;
	this->nextIndex = 0;
	this->player->optionService->SetPreferenceBool("windOrdered", this->ordered);
}

void SurfWindService::OnPhysicsSimulatePost()
{
	if (!this->enabled || !SurfWindService::IsAvailable())
	{
		return;
	}
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (!pawn || !this->player->IsAlive() || pawn->IsBot())
	{
		return;
	}
	f32 now = g_pSurfUtils->GetServerGlobals()->curtime;
	if (now - this->lastSoundTime < SURF_WIND_COOLDOWN)
	{
		return;
	}
	Vector velocity;
	this->player->GetVelocity(&velocity);
	f32 speed = velocity.Length2D();
	if (speed < windConfig.thresholds[0])
	{
		return;
	}
	i32 level = 0;
	for (i32 i = 1; i < SURF_WIND_LEVELS; i++)
	{
		if (speed >= windConfig.thresholds[i])
		{
			level = i;
		}
	}
	i32 clip;
	if (this->ordered)
	{
		clip = this->nextIndex;
		this->nextIndex = (this->nextIndex + 1) % SURF_WIND_CLIPS;
	}
	else
	{
		clip = RandomInt(0, SURF_WIND_CLIPS - 1);
	}
	this->lastSoundTime = now;
	// sounds/windsound/WindL2/wind_Intensity_2_005.vsnd - played client side, only this player hears it
	interfaces::pEngine->ClientCommand(this->player->GetPlayerSlot(), "play %s/WindL%i/wind_Intensity_%i_%03i.vsnd", windConfig.path, level, level,
									   clip + 1);
}

SCMD(surf_wind, SCFL_PREFERENCE)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!SurfWindService::IsAvailable())
	{
		player->languageService->PrintChat(true, false, "Wind - Not Available");
		return true;
	}
	player->windService->Toggle();
	player->languageService->PrintChat(true, false, player->windService->IsEnabled() ? "Wind - Enabled" : "Wind - Disabled");
	return true;
}

SCMD(surf_windmode, SCFL_PREFERENCE)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!SurfWindService::IsAvailable())
	{
		player->languageService->PrintChat(true, false, "Wind - Not Available");
		return true;
	}
	player->windService->ToggleMode();
	player->languageService->PrintChat(true, false, player->windService->IsOrdered() ? "Wind - Mode Sequential" : "Wind - Mode Random");
	return true;
}
