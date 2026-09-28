#include "surf/surf.h"
#include "cs2surf.h"
#include "surf_hud.h"
#include "surf/vote/surf_vote.h"
#include "sdk/datatypes.h"
#include "utils/utils.h"
#include "utils/simplecmds.h"

#include "surf/option/surf_option.h"
#include "surf/timer/surf_timer.h"
#include "surf/language/surf_language.h"
#include "surf/replays/surf_replaysystem.h"

#include "tier0/memdbgon.h"

#define HUD_ON_GROUND_THRESHOLD 0.07f
#define HUD_FLASH_DURATION      4.0f

// Compact panel colours. Speed is coloured by the same thresholds SharpTimer used; diffs use blue/red for time and green/orange for speed.
static_global const i32 hudSpeedThresholds[] = {349, 699, 1049, 1399, 1749, 2099, 2449, 2799, 3149, 3499};
static_global const char *hudSpeedColors[] = {"LimeGreen",  "Lime",   "GreenYellow", "Yellow", "Gold",   "Orange",
											  "DarkOrange", "Tomato", "OrangeRed",   "Red",    "Crimson"};
#define HUD_COLOR_TIME_FASTER  "#5a97fa"
#define HUD_COLOR_TIME_SLOWER  "#fa5a5a"
#define HUD_COLOR_SPEED_FASTER "#3b992c"
#define HUD_COLOR_SPEED_SLOWER "#DA6E1B"

static_global class SurfTimerServiceEventListener_HUD : public SurfTimerServiceEventListener
{
	virtual void OnTimerStopped(SurfPlayer *player, u32 courseGUID) override;
	virtual void OnTimerEndPost(SurfPlayer *player, u32 courseGUID, f32 time) override;

	virtual void OnTimerStartPost(SurfPlayer *player, u32 courseGUID) override
	{
		player->hudService->ResetSync();
	}
} timerEventListener;

static_global class SurfOptionServiceEventListener_HUD : public SurfOptionServiceEventListener
{
	virtual void OnPlayerPreferencesLoaded(SurfPlayer *player)
	{
		player->hudService->ResetShowPanel();
	}
} optionEventListener;

void SurfHUDService::Init()
{
	SurfTimerService::RegisterEventListener(&timerEventListener);
	SurfOptionService::RegisterEventListener(&optionEventListener);
}

void SurfHUDService::Reset()
{
	this->ResetShowPanel();
	this->timerStoppedTime = {};
	this->currentTimeWhenTimerStopped = {};
	this->flash = {};
	this->ResetSync();
}

std::string SurfHUDService::GetSpeedText(const char *language)
{
	Vector velocity, baseVelocity;
	this->player->GetVelocity(&velocity);
	this->player->GetBaseVelocity(&baseVelocity);
	velocity += baseVelocity;
	// Keep the takeoff velocity on for a while after landing so the speed values flicker less.
	if ((this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND
		 && g_pSurfUtils->GetServerGlobals()->curtime - this->player->landingTime > HUD_ON_GROUND_THRESHOLD)
		|| (this->player->GetPlayerPawn()->m_MoveType == MOVETYPE_LADDER && !player->IsButtonPressed(IN_JUMP)))
	{
		return SurfLanguageService::PrepareMessageWithLang(language, "HUD - Speed Text", velocity.Length2D());
	}
	return SurfLanguageService::PrepareMessageWithLang(language, "HUD - Speed Text (Takeoff)", velocity.Length2D(),
													   this->player->takeoffVelocity.Length2D());
}

std::string SurfHUDService::GetKeyText(const char *language)
{
	// clang-format off

	return SurfLanguageService::PrepareMessageWithLang(language, "HUD - Key Text",
		this->player->IsButtonPressed(IN_MOVELEFT) ? 'A' : '_',
		this->player->IsButtonPressed(IN_FORWARD) ? 'W' : '_',
		this->player->IsButtonPressed(IN_BACK) ? 'S' : '_',
		this->player->IsButtonPressed(IN_MOVERIGHT) ? 'D' : '_',
		this->player->IsButtonPressed(IN_DUCK) ? 'C' : '_',
		this->jumpedThisTick ? 'J' : '_'
	);

	// clang-format on
}

std::string SurfHUDService::GetStageText(const char *language)
{
	// clang-format off

	int stage = this->player->timerService->GetStage();
	return stage > 0 
		? SurfLanguageService::PrepareMessageWithLang(language, "HUD - Stage Text", stage)
		: "";

	// clang-format on
}

std::string SurfHUDService::GetTimerText(const char *language)
{
	if (Surf::replaysystem::IsReplayBot(this->player))
	{
		char timeText[128];

		f64 time = Surf::replaysystem::GetTime();
		bool paused = Surf::replaysystem::GetPaused();
		bool timerRunning = Surf::replaysystem::GetEndTime() == 0.0f;
		// Show timer if time is not 0 or end time is not 0.
		if (time == 0.0f && Surf::replaysystem::GetEndTime() == 0.0f)
		{
			return std::string("");
		}
		if (!timerRunning)
		{
			time = Surf::replaysystem::GetEndTime();
		}
		utils::FormatTime(time, timeText, sizeof(timeText));
		// clang-format off
		return SurfLanguageService::PrepareMessageWithLang(language, "HUD - Timer Text",
			timeText,
			timerRunning ? "" : SurfLanguageService::PrepareMessageWithLang(language, "HUD - Stopped Text").c_str(),
			paused ? SurfLanguageService::PrepareMessageWithLang(language, "HUD - Paused Text").c_str() : ""
		);
		// clang-format on
	}
	if (this->player->timerService->GetTimerRunning() || this->ShouldShowTimerAfterStop())
	{
		char timeText[128];

		// clang-format off

		f64 time = this->player->timerService->GetTimerRunning()
			? player->timerService->GetTime()
			: this->currentTimeWhenTimerStopped;

		bool timerRunning = this->player->timerService->GetTimerRunning();
		bool paused = this->player->timerService->GetPaused();

		utils::FormatTime(time, timeText, sizeof(timeText));
		return SurfLanguageService::PrepareMessageWithLang(language, "HUD - Timer Text",
			timeText,
			timerRunning ? "" : SurfLanguageService::PrepareMessageWithLang(language, "HUD - Stopped Text").c_str(),
			paused ? SurfLanguageService::PrepareMessageWithLang(language, "HUD - Paused Text").c_str() : ""
		);
		// clang-format on
	}
	return std::string("");
}

void SurfHUDService::DrawPanels(SurfPlayer *player, SurfPlayer *target)
{
	std::string voteHtml = Surf::vote::GetPanelHTML(target);
	if (!target->hudService->IsShowingPanel())
	{
		if (!voteHtml.empty())
		{
			target->PrintHTMLCentre(false, false, voteHtml.c_str());
		}
		return;
	}
	const char *language = target->languageService->GetLanguage();

	if (target->hudService->IsCompactStyle())
	{
		std::string html = player->hudService->GetCompactHtml(language, target);
		if (!voteHtml.empty())
		{
			html = voteHtml + html;
		}
		if (!html.empty())
		{
			target->PrintHTMLCentre(false, false, html.c_str());
		}
		return;
	}

	std::string keyText = player->hudService->GetKeyText(language);
	std::string timerText = player->hudService->GetTimerText(language);
	std::string speedText = player->hudService->GetSpeedText(language);
	std::string stageText = player->hudService->GetStageText(language);

	// clang-format off
	std::string centerText = SurfLanguageService::PrepareMessageWithLang(language, "HUD - Center Text", 
		keyText.c_str(), stageText.c_str(), timerText.c_str(), speedText.c_str());
	std::string alertText = SurfLanguageService::PrepareMessageWithLang(language, "HUD - Alert Text", 
		keyText.c_str(), stageText.c_str(), timerText.c_str(), speedText.c_str());
	std::string htmlText = SurfLanguageService::PrepareMessageWithLang(language, "HUD - Html Center Text",
		keyText.c_str(), stageText.c_str(), timerText.c_str(), speedText.c_str());

	// clang-format on

	auto trimNewlines = [](std::string &str)
	{
		// Remove leading newlines
		size_t start = str.find_first_not_of('\n');
		if (start == std::string::npos)
		{
			str.clear();
			return;
		}
		// Remove trailing newlines
		size_t end = str.find_last_not_of('\n');
		str = str.substr(start, end - start + 1);
	};

	trimNewlines(centerText);
	trimNewlines(alertText);
	trimNewlines(htmlText);
	if (!voteHtml.empty())
	{
		// map vote in progress: show it above the speed/keys panel
		htmlText = htmlText.empty() ? voteHtml : voteHtml + htmlText;
	}

	// Remove leading & trailing newlines just in case a line is empty.
	if (!centerText.empty())
	{
		target->PrintCentre(false, false, centerText.c_str());
	}
	if (!alertText.empty())
	{
		target->PrintAlert(false, false, alertText.c_str());
	}
	if (!htmlText.empty())
	{
		target->PrintHTMLCentre(false, false, htmlText.c_str());
	}
}

void SurfHUDService::ResetShowPanel()
{
	this->showPanel = this->player->optionService->GetPreferenceBool("showPanel", true);
	this->compactStyle = this->player->optionService->GetPreferenceBool("hudCompact", true);
	this->showSync = this->player->optionService->GetPreferenceBool("hudSync", true);
}

void SurfHUDService::ToggleStyle()
{
	this->compactStyle = !this->compactStyle;
	this->player->optionService->SetPreferenceBool("hudCompact", this->compactStyle);
	// the compact style does not use the plain centre text, clear whatever the classic one left there
	utils::PrintCentre(this->player->GetController(), "#SFUI_EmptyString");
}

void SurfHUDService::ToggleSync()
{
	this->showSync = !this->showSync;
	this->player->optionService->SetPreferenceBool("hudSync", this->showSync);
}

void SurfHUDService::OnProcessMovementPost()
{
	// Strafe sync: while airborne and holding exactly one strafe key, count the ticks where the view turned the same way.
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (!pawn || (pawn->m_fFlags() & FL_ONGROUND) || this->player->GetMoveType() != MOVETYPE_WALK)
	{
		return;
	}
	bool left = this->player->IsButtonPressed(IN_MOVELEFT);
	bool right = this->player->IsButtonPressed(IN_MOVERIGHT);
	if (left == right)
	{
		return;
	}
	TurnState turn = this->player->GetTurning();
	if (turn == TURN_NONE)
	{
		return;
	}
	this->syncTotal++;
	if ((left && turn == TURN_LEFT) || (right && turn == TURN_RIGHT))
	{
		this->syncGood++;
	}
}

void SurfHUDService::SetSplitFlash(const char *label, const char *time, f64 diff, bool hasDiff, f32 speed, f32 pbSpeed)
{
	this->flash = {};
	this->flash.label = label;
	this->flash.time = time;
	if (hasDiff)
	{
		this->flash.diff = SurfTimerService::FormatDiffTime(diff).Get();
		this->flash.faster = diff < 0;
	}
	char buf[32];
	V_snprintf(buf, sizeof(buf), "%.0f", speed);
	this->flash.speed = buf;
	if (pbSpeed >= 0.0f)
	{
		f32 sd = speed - pbSpeed;
		V_snprintf(buf, sizeof(buf), "%+.0f", sd);
		this->flash.speedDiff = buf;
		this->flash.speedFaster = sd > 0.0f;
	}
	this->flash.expiry = g_pSurfUtils->GetServerGlobals()->curtime + HUD_FLASH_DURATION;
}

std::string SurfHUDService::GetCompactHtml(const char *language, SurfPlayer *target)
{
	// --- timer (big) ---
	std::string timeText;
	if (Surf::replaysystem::IsReplayBot(this->player) || this->player->timerService->GetTimerRunning() || this->ShouldShowTimerAfterStop())
	{
		timeText = this->GetTimerText(language);
	}
	// --- speed, coloured by value ---
	Vector velocity, baseVelocity;
	this->player->GetVelocity(&velocity);
	this->player->GetBaseVelocity(&baseVelocity);
	velocity += baseVelocity;
	f32 speed = velocity.Length2D();
	const char *speedColor = hudSpeedColors[SURF_ARRAYSIZE(hudSpeedThresholds)];
	for (u32 i = 0; i < SURF_ARRAYSIZE(hudSpeedThresholds); i++)
	{
		if (speed < hudSpeedThresholds[i])
		{
			speedColor = hudSpeedColors[i];
			break;
		}
	}
	char speedText[64];
	V_snprintf(speedText, sizeof(speedText), "<font color='%s'>%.0f</font>", speedColor, speed);
	// --- sync ---
	std::string syncText;
	if (target->hudService->showSync && !Surf::replaysystem::IsReplayBot(this->player))
	{
		char buf[64];
		V_snprintf(buf, sizeof(buf), "%.1f", this->GetSync());
		syncText = SurfLanguageService::PrepareMessageWithLang(language, "HUD - Compact Sync", buf);
	}
	// --- checkpoint / stage flash ---
	std::string flashText;
	if (this->flash.expiry > g_pSurfUtils->GetServerGlobals()->curtime && !this->flash.label.empty())
	{
		std::string diffLine, speedLine;
		if (!this->flash.diff.empty())
		{
			char buf[160];
			V_snprintf(buf, sizeof(buf), "<font color='%s'>%s %s</font>", this->flash.faster ? HUD_COLOR_TIME_FASTER : HUD_COLOR_TIME_SLOWER,
					   this->flash.faster ? "&#9650;" : "&#9660;", this->flash.diff.c_str());
			diffLine = buf;
		}
		if (!this->flash.speedDiff.empty())
		{
			char buf[160];
			V_snprintf(buf, sizeof(buf), "<font color='white'>%s</font> <font color='%s'>%s %s</font>", this->flash.speed.c_str(),
					   this->flash.speedFaster ? HUD_COLOR_SPEED_FASTER : HUD_COLOR_SPEED_SLOWER, this->flash.speedFaster ? "&#9650;" : "&#9660;",
					   this->flash.speedDiff.c_str());
			speedLine = buf;
		}
		else
		{
			speedLine = "<font color='white'>" + this->flash.speed + "</font>";
		}
		flashText = SurfLanguageService::PrepareMessageWithLang(language, "HUD - Compact Split", this->flash.label.c_str(), this->flash.time.c_str(),
																diffLine.c_str(), speedLine.c_str());
	}
	std::string stageText = this->GetStageText(language);
	std::string keyText = this->GetKeyText(language);
	// clang-format off
	return SurfLanguageService::PrepareMessageWithLang(language, "HUD - Compact Panel",
		flashText.c_str(), timeText.c_str(), speedText, syncText.c_str(), stageText.c_str(), keyText.c_str());
	// clang-format on
}

void SurfHUDService::TogglePanel()
{
	this->showPanel = !this->showPanel;
	this->player->optionService->SetPreferenceBool("showPanel", this->showPanel);
	if (!this->showPanel)
	{
		utils::PrintAlert(this->player->GetController(), "#SFUI_EmptyString");
		utils::PrintCentre(this->player->GetController(), "#SFUI_EmptyString");
		this->player->languageService->PrintHTMLCentre(false, false, "HUD - HTML Panel Disabled");
	}
}

void SurfHUDService::OnTimerStopped(f64 currentTimeWhenTimerStopped)
{
	// g_pSurfUtils->GetServerGlobals() becomes invalid when the plugin is unloading.
	if (g_SurfPlugin.unloading)
	{
		return;
	}
	this->timerStoppedTime = g_pSurfUtils->GetServerGlobals()->curtime;
	this->currentTimeWhenTimerStopped = currentTimeWhenTimerStopped;
}

void SurfTimerServiceEventListener_HUD::OnTimerStopped(SurfPlayer *player, u32 courseGUID)
{
	player->hudService->OnTimerStopped(player->timerService->GetTime());
}

void SurfTimerServiceEventListener_HUD::OnTimerEndPost(SurfPlayer *player, u32 courseGUID, f32 time)
{
	player->hudService->OnTimerStopped(time);
}

SCMD(surf_panel, SCFL_HUD)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->hudService->TogglePanel();
	if (player->hudService->IsShowingPanel())
	{
		player->languageService->PrintChat(true, false, "HUD Option - Info Panel - Enable");
	}
	else
	{
		player->languageService->PrintChat(true, false, "HUD Option - Info Panel - Disable");
	}
	return true;
}

SCMD(surf_hudstyle, SCFL_HUD)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->hudService->ToggleStyle();
	player->languageService->PrintChat(true, false,
									   player->hudService->IsCompactStyle() ? "HUD Option - Style - Compact" : "HUD Option - Style - Classic");
	return true;
}

SCMD_LINK(surf_hud, surf_hudstyle);

SCMD(surf_sync, SCFL_HUD)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->hudService->ToggleSync();
	player->languageService->PrintChat(true, false, "HUD Option - Sync - Toggled");
	return true;
}
