#pragma once
#include "surf/surf.h"
#include "../timer/surf_timer.h"

#define SURF_HUD_TIMER_STOPPED_GRACE_TIME 3.0f

class SurfHUDService : public SurfBaseService
{
	using SurfBaseService::SurfBaseService;

private:
	bool jumpedThisTick {};
	bool showPanel {};
	f64 timerStoppedTime {};
	f64 currentTimeWhenTimerStopped {};

public:
	virtual void Reset() override;
	static void Init();

	// Draw the panel from a player to a specific target.
	static void DrawPanels(SurfPlayer *player, SurfPlayer *target);

	void ResetShowPanel();
	void TogglePanel();

	void OnPhysicsSimulate()
	{
		jumpedThisTick = false;
	}

	void OnJump()
	{
		jumpedThisTick = true;
	}

	bool IsShowingPanel()
	{
		return this->showPanel;
	}

	void OnTimerStopped(f64 currentTimeWhenTimerStopped);

	bool ShouldShowTimerAfterStop()
	{
		return g_pSurfUtils->GetServerGlobals()->curtime > SURF_HUD_TIMER_STOPPED_GRACE_TIME
			   && g_pSurfUtils->GetServerGlobals()->curtime - timerStoppedTime < SURF_HUD_TIMER_STOPPED_GRACE_TIME;
	}

private:
	std::string GetSpeedText(const char *language = SURF_DEFAULT_LANGUAGE);
	std::string GetKeyText(const char *language = SURF_DEFAULT_LANGUAGE);
	std::string GetTimerText(const char *language = SURF_DEFAULT_LANGUAGE);
	std::string GetStageText(const char *language = SURF_DEFAULT_LANGUAGE);
};
