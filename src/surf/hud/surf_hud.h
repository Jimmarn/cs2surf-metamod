#pragma once
#include "surf/surf.h"
#include "../timer/surf_timer.h"

#define SURF_HUD_TIMER_STOPPED_GRACE_TIME 3.0f

class SurfHUDService : public SurfBaseService
{
	using SurfBaseService::SurfBaseService;

public:
	// Checkpoint / stage split shown in the panel for a few seconds (compact style).
	struct SplitFlash
	{
		std::string label;          // "CP 3" / "Stage 2"
		std::string time;           // formatted split/zone time
		std::string diff;           // formatted diff vs the compare target, "" when unknown
		bool faster {};
		std::string speed;          // speed when the zone was touched
		std::string speedDiff;      // "+123" / "-45", "" when unknown
		bool speedFaster {};
		f64 expiry {};
	};

private:
	bool jumpedThisTick {};
	bool showPanel {};
	bool compactStyle {};
	bool showSync {};
	f64 timerStoppedTime {};
	f64 currentTimeWhenTimerStopped {};
	SplitFlash flash {};
	// strafe sync (compact style): ticks strafing in the air where the mouse turned the same way as the strafe key
	u32 syncGood {};
	u32 syncTotal {};

public:
	virtual void Reset() override;
	static void Init();

	// Draw the panel from a player to a specific target.
	static void DrawPanels(SurfPlayer *player, SurfPlayer *target);

	void ResetShowPanel();
	void TogglePanel();
	void ToggleStyle();
	void ToggleSync();

	bool IsCompactStyle()
	{
		return this->compactStyle;
	}

	void OnProcessMovementPost();
	void ResetSync()
	{
		this->syncGood = this->syncTotal = 0;
	}

	f32 GetSync()
	{
		return this->syncTotal ? 100.0f * this->syncGood / this->syncTotal : 100.0f;
	}

	void SetSplitFlash(const char *label, const char *time, f64 diff, bool hasDiff, f32 speed, f32 pbSpeed);

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
	std::string GetCompactHtml(const char *language, SurfPlayer *target);
};
