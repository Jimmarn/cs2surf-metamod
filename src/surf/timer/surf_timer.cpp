#include "surf_timer.h"
#include "surf/db/surf_db.h"
#include "surf/global/surf_global.h"
#include "surf/language/surf_language.h"
#include "surf/mode/surf_mode.h"
#include "surf/style/surf_style.h"
#include "surf/noclip/surf_noclip.h"
#include "surf/option/surf_option.h"
#include "surf/language/surf_language.h"
#include "surf/trigger/surf_trigger.h"
#include "surf/spec/surf_spec.h"
#include "surf/recording/surf_recording.h"
#include "surf/hud/surf_hud.h"
#include "submission.h"

#include "utils/utils.h"
#include "utils/simplecmds.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

// clang-format off
constexpr const char *diffTextKeys[SurfTimerService::CompareType::COMPARETYPE_COUNT] = {
	"",
	"Server PB Diff (Overall)",
	"Global PB Diff (Overall)",
	"SR Diff (Overall)",
	"WR Diff (Overall)"
};

constexpr const char *missedTimeKeys[SurfTimerService::CompareType::COMPARETYPE_COUNT] = {
	"",
	"Missed Server PB (Overall)",
	"Missed Global PB (Overall)",
	"Missed SR (Overall)",
	"Missed WR (Overall)"
};

// clang-format on

static_global class SurfDatabaseServiceEventListener_Timer : public SurfDatabaseServiceEventListener
{
public:
	virtual void OnMapSetup() override;
	virtual void OnClientSetup(Player *player, u64 steamID64, bool isCheater) override;
} databaseEventListener;

static_global class SurfOptionServiceEventListener_Timer : public SurfOptionServiceEventListener
{
	virtual void OnPlayerPreferencesLoaded(SurfPlayer *player)
	{
		player->timerService->ApplyPreferences();
	}

	virtual void OnPlayerPreferenceChanged(SurfPlayer *player, const char *optionName)
	{
		if (SURF_STREQI(optionName, "preferredCompareType") || SURF_STREQI(optionName, "timerStopSound"))
		{
			player->timerService->ApplyPreferences();
		}
	}
} optionEventListener;

std::unordered_map<PBDataKey, PBData> SurfTimerService::srCache;
std::unordered_map<PBDataKey, PBData> SurfTimerService::wrCache;

static_global CUtlVector<SurfTimerServiceEventListener *> eventListeners;

bool SurfTimerService::RegisterEventListener(SurfTimerServiceEventListener *eventListener)
{
	if (eventListeners.Find(eventListener) >= 0)
	{
		return false;
	}
	eventListeners.AddToTail(eventListener);
	return true;
}

bool SurfTimerService::UnregisterEventListener(SurfTimerServiceEventListener *eventListener)
{
	return eventListeners.FindAndRemove(eventListener);
}

void SurfTimerService::StartZoneStartTouch(const SurfCourseDescriptor *course)
{
	this->touchedGroundSinceTouchingStartZone = !!(this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND);
	this->inStartzone = true;
}

void SurfTimerService::StartZoneEndTouch(const SurfCourseDescriptor *course)
{
	if (this->touchedGroundSinceTouchingStartZone && !this->timerRunning)
	{
		this->TimerStart(course);
	}
	this->inStartzone = false;
}

void SurfTimerService::CheckpointZoneStartTouch(const SurfCourseDescriptor *course, i32 cpNumber)
{
	if (!this->timerRunning || course->guid != this->currentCourseGUID)
	{
		return;
	}

	assert(cpNumber > INVALID_CHECKPOINT_NUMBER && cpNumber < SURF_MAX_CHECKPOINT_ZONES);

	if (this->cpZoneTimes[cpNumber - 1] < 0)
	{
		this->PlayReachedCheckpointSound();
		this->cpZoneTimes[cpNumber - 1] = this->GetTime();
		this->cpZoneSpeeds[cpNumber - 1] = this->GetSplitSpeed();
		this->ShowCheckpointText(cpNumber);
		this->lastCheckpoint = cpNumber;
		this->reachedCheckpoints++;
		CALL_FORWARD(eventListeners, OnCheckpointZoneTouchPost, this->player, cpNumber);
	}
}

void SurfTimerService::StageZoneStartTouch(const SurfCourseDescriptor *course, i32 stageNumber)
{
	if (!this->timerRunning || course->guid != this->currentCourseGUID)
	{
		return;
	}

	assert(stageNumber > INVALID_STAGE_NUMBER && stageNumber < SURF_MAX_STAGE_ZONES);

	// skipped stage
	if (stageNumber > this->currentStage + 1)
	{
		this->PlayMissedZoneSound();
		this->player->languageService->PrintChat(true, false, "Touched too high stage number (Missed stage)", this->currentStage + 1);
		return;
	}

	// same stage (failed)
	if (stageNumber == this->currentStage)
	{
		return;
	}

	// next stage
	if (stageNumber == this->currentStage + 1)
	{
		i32 cleared = this->currentStage; // 1-based stage that was just finished
		this->stageZoneTimes[cleared - 1] = this->GetTime() - this->stageEndTouchTimes[cleared - 1];
		this->stageZoneSpeeds[cleared - 1] = this->GetSplitSpeed();
		this->stageTouchTimes[cleared - 1] = this->GetTime();

		this->PlayReachedStageSound();
		this->ShowStageText(cleared);
		this->SubmitStageTime(course, cleared);
		this->currentStage++;
		CALL_FORWARD(eventListeners, OnStageZoneTouchPost, this->player, stageNumber);
	}
}

void SurfTimerService::StageZoneEndTouch(const SurfCourseDescriptor *course, i32 stageNumber)
{
	if (!this->timerRunning || course->guid != this->currentCourseGUID)
	{
		return;
	}

	assert(stageNumber > INVALID_STAGE_NUMBER && stageNumber < SURF_MAX_STAGE_ZONES);

	this->stageEndTouchTimes[this->currentStage - 1] = this->GetTime();
}

bool SurfTimerService::TimerStart(const SurfCourseDescriptor *courseDesc, bool playSound)
{
	// clang-format off
	if (!this->player->GetPlayerPawn()->IsAlive()
		|| this->JustStartedTimer()
		|| this->player->JustTeleported()
		|| this->player->noclipService->JustNoclipped()
		|| !this->HasValidMoveType()
		|| (this->GetTimerRunning() && courseDesc->guid == this->currentCourseGUID)
		|| (!(this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND) && !this->GetValidJump()))
	// clang-format on
	{
		return false;
	}
	if (this->player->inPerf || this->JustLanded())
	{
		// Have a .5s landing time cooldown to ensure no speed from recent perfs can be used to start
		this->player->languageService->PrintChat(true, false, "Can't Bhop Start");
		return false;
	}
	if (V_strlen(this->player->modeService->GetModeName()) > SURF_MAX_MODE_NAME_LENGTH)
	{
		Warning("[Surf] Timer start failed: Mode name is too long!");
		return false;
	}

	bool allowStart = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowStart &= eventListeners[i]->OnTimerStart(this->player, courseDesc->guid);
	}
	if (!allowStart)
	{
		return false;
	}

	// In CS2Surf you can touch trigger in half tick intervals, but here we are incrementing by full tick intervals only.
	// Since the player was still in the trigger for half a tick, we need to offset by half a tick if we started in a half tick.
	// So the current time should be subtracted by the difference between server curtime and client curtime at the moment of starting the timer,
	// That way when we increment by full tick intervals in OnPhysicsSimulatePost, the time will be correct.
	this->currentTime = g_pSurfUtils->GetGlobals()->curtime - g_pSurfUtils->GetServerGlobals()->curtime;
	assert(this->currentTime <= 0 && this->currentTime > -ENGINE_FIXED_TICK_INTERVAL);
	this->timerRunning = true;

	this->reachedCheckpoints = 0;
	this->lastCheckpoint = 0;

	f64 invalidTime = -1;
	i32 totalStages = SurfTimerService::GetTotalStages(courseDesc);
	this->cpZoneTimes.SetSize(courseDesc->checkpointCount);
	this->stageZoneTimes.SetSize(totalStages);
	this->stageEndTouchTimes.SetSize(totalStages);
	this->stageTouchTimes.SetSize(totalStages);
	this->cpZoneSpeeds.SetSize(courseDesc->checkpointCount);
	this->stageZoneSpeeds.SetSize(totalStages);

	this->cpZoneTimes.FillWithValue(invalidTime);
	this->stageZoneTimes.FillWithValue(invalidTime);
	this->stageEndTouchTimes.FillWithValue(invalidTime);
	this->stageTouchTimes.FillWithValue(invalidTime);
	this->cpZoneSpeeds.FillWithValue(invalidTime);
	this->stageZoneSpeeds.FillWithValue(invalidTime);

	if (courseDesc->stageCount > 0)
	{
		this->currentStage = 1;
		// initialize stage 1 end touch time
		this->stageEndTouchTimes[0] = this->GetTime();
	}
	else
	{
		this->currentStage = 0;
	}

	this->player->checkpointService->ResetCheckpoints();

	SetCourse(courseDesc->guid);
	this->validTime = true;
	this->shouldAnnounceMissedTime = true;

	this->UpdateCurrentCompareType(ToPBDataKey(Surf::mode::GetModeInfo(this->player->modeService).id, courseDesc->guid));

	if (playSound)
	{
		for (SurfPlayer *spec = player->specService->GetNextSpectator(NULL); spec != NULL; spec = player->specService->GetNextSpectator(spec))
		{
			player->timerService->PlayTimerStartSound();
		}
		this->PlayTimerStartSound();
	}

	if (!this->player->IsAuthenticated())
	{
		this->player->languageService->PrintChat(true, false, "No Steam Authentication Warning");
	}
	if (SurfGlobalService::IsAvailable() && !this->player->hasPrime)
	{
		this->player->languageService->PrintChat(true, false, "No Prime Warning");
	}

	const char *language = this->player->languageService->GetLanguage();
	std::string startSpeedText = this->player->timerService->GetStartSpeedText(language);

	this->player->languageService->PrintChat(true, true, startSpeedText.c_str());

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerStartPost(this->player, courseDesc->guid);
	}
	return true;
}

bool SurfTimerService::TimerEnd(const SurfCourseDescriptor *courseDesc)
{
	if (!this->player->IsAlive())
	{
		return false;
	}

	if (!this->timerRunning || courseDesc->guid != this->currentCourseGUID)
	{
		for (SurfPlayer *spec = player->specService->GetNextSpectator(NULL); spec != NULL; spec = player->specService->GetNextSpectator(spec))
		{
			player->timerService->PlayTimerFalseEndSound();
		}
		this->PlayTimerFalseEndSound();
		this->lastFalseEndTime = g_pSurfUtils->GetServerGlobals()->curtime;
		return false;
	}

	if (courseDesc->stageCount > 0 && (this->currentStage - 1 != courseDesc->stageCount))
	{
		this->PlayMissedZoneSound();
		this->player->languageService->PrintChat(true, false, "Can't Finish Run (Missed Stage)", this->currentStage + 1);
		return false;
	}

	if (this->reachedCheckpoints != courseDesc->checkpointCount)
	{
		this->PlayMissedZoneSound();
		i32 missCount = courseDesc->checkpointCount - this->reachedCheckpoints;
		if (missCount == 1)
		{
			this->player->languageService->PrintChat(true, false, "Can't Finish Run (Missed a Checkpoint Zone)");
		}
		else
		{
			this->player->languageService->PrintChat(true, false, "Can't Finish Run (Missed Checkpoint Zones)", missCount);
		}
		return false;
	}

	f32 time = this->GetTime() + g_pSurfUtils->GetServerGlobals()->frametime;
	u32 teleportsUsed = this->player->checkpointService->GetTeleportCount();

	bool allowEnd = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowEnd &= eventListeners[i]->OnTimerEnd(this->player, this->currentCourseGUID, time);
	}
	if (!allowEnd)
	{
		return false;
	}
	// Update current time for one last time.
	this->currentTime = time;

	// the final stage ends at the finish line: record its segment like any other stage
	if (courseDesc->stageCount > 0)
	{
		i32 last = SurfTimerService::GetTotalStages(courseDesc);
		if (last <= this->stageZoneTimes.Count() && this->stageEndTouchTimes[last - 1] >= 0)
		{
			this->stageZoneTimes[last - 1] = (f64)time - this->stageEndTouchTimes[last - 1];
			this->stageZoneSpeeds[last - 1] = this->GetSplitSpeed();
			this->stageTouchTimes[last - 1] = time;
			this->SubmitStageTime(courseDesc, last);
		}
	}

	this->timerRunning = false;
	this->lastEndTime = g_pSurfUtils->GetServerGlobals()->curtime;

	for (SurfPlayer *spec = player->specService->GetNextSpectator(NULL); spec != NULL; spec = player->specService->GetNextSpectator(spec))
	{
		player->timerService->PlayTimerEndSound();
	}
	this->PlayTimerEndSound();

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerEndPost(this->player, this->currentCourseGUID, time);
	}

	// HUD split flash for the finish, ranked against the top runs before this one is submitted. In stage mode the last
	// stage's own result is what matters, the finish message in chat covers the total.
	i32 lastStage = SurfTimerService::GetTotalStages(courseDesc);
	if (lastStage > 0 && this->player->hudService->IsStageMode())
	{
		this->ShowStageText(lastStage);
	}
	else
	{
		this->ShowFinishText(courseDesc, time);
	}

	// This must be called after OnTimerEndPost so that the run UUID is set correctly.
	if (!this->player->GetPlayerPawn()->IsBot())
	{
		RunSubmission::Create(this->player);
	}

	// Reset current stage immediately to remove HUD element
	this->currentStage = 0;

	return true;
}

bool SurfTimerService::TimerStop(bool playSound)
{
	if (!this->timerRunning)
	{
		return false;
	}
	this->timerRunning = false;
	if (playSound)
	{
		for (SurfPlayer *spec = player->specService->GetNextSpectator(NULL); spec != NULL; spec = player->specService->GetNextSpectator(spec))
		{
			spec->timerService->PlayTimerStopSound();
		}
		this->PlayTimerStopSound();
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerStopped(this->player, this->currentCourseGUID);
	}

	// Reset current stage immediately to remove HUD element
	this->currentStage = 0;

	return true;
}

void SurfTimerService::TimerStopAll(bool playSound)
{
	for (int i = 0; i < MAXPLAYERS + 1; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(i);
		if (!player || !player->timerService)
		{
			continue;
		}
		player->timerService->TimerStop(playSound);
	}
}

void SurfTimerService::InvalidateJump()
{
	this->validJump = false;
	this->lastInvalidateTime = g_pSurfUtils->GetServerGlobals()->curtime;
}

void SurfTimerService::PlayTimerStartSound()
{
	if (g_pSurfUtils->GetServerGlobals()->curtime - this->lastStartSoundTime > SURF_TIMER_SOUND_COOLDOWN && this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_START);
		this->lastStartSoundTime = g_pSurfUtils->GetServerGlobals()->curtime;
	}
}

void SurfTimerService::InvalidateRun()
{
	if (!this->validTime)
	{
		return;
	}
	this->validTime = false;

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnTimerInvalidated(this->player);
	}
}

bool SurfTimerService::HasValidMoveType()
{
	return SurfTimerService::IsValidMoveType(this->player->GetMoveType());
}

void SurfTimerService::PlayTimerEndSound()
{
	if (this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_END);
	}
}

void SurfTimerService::PlayTimerFalseEndSound()
{
	if (this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_FALSE_END);
	}
}

void SurfTimerService::PlayMissedZoneSound()
{
	if (this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_MISSED_ZONE);
	}
}

void SurfTimerService::PlayReachedCheckpointSound()
{
	if (this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_REACH_CHECKPOINT);
	}
}

void SurfTimerService::PlayReachedStageSound()
{
	if (this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_REACH_STAGE);
	}
}

void SurfTimerService::PlayTimerStopSound()
{
	if (this->shouldPlayTimerSound)
	{
		utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_STOP);
	}
}

void SurfTimerService::PlayMissedTimeSound()
{
	if (this->shouldPlayTimerSound)
	{
		if (g_pSurfUtils->GetServerGlobals()->curtime - this->lastMissedTimeSoundTime > SURF_TIMER_SOUND_COOLDOWN)
		{
			utils::PlaySoundToClient(this->player->GetPlayerSlot(), SURF_TIMER_SND_MISSED_TIME);
			this->lastMissedTimeSoundTime = g_pSurfUtils->GetServerGlobals()->curtime;
		}
	}
}

static_function std::string GetTeleportCountText(int tpCount, const char *language)
{
	return tpCount == 1 ? SurfLanguageService::PrepareMessageWithLang(language, "1 Teleport Text")
						: SurfLanguageService::PrepareMessageWithLang(language, "2+ Teleports Text", tpCount);
}

void SurfTimerService::Pause()
{
	if (!this->CanPause(true))
	{
		return;
	}

	bool allowPause = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowPause &= eventListeners[i]->OnPause(this->player);
	}
	if (!allowPause)
	{
		this->player->languageService->PrintChat(true, false, "Can't Pause (Generic)");
		this->player->PlayErrorSound();
		return;
	}

	this->paused = true;
	this->pausedOnLadder = this->player->GetMoveType() == MOVETYPE_LADDER;
	this->lastDuckValue = this->player->GetMoveServices()->m_flDuckAmount;
	this->lastStaminaValue = this->player->GetMoveServices()->m_flStamina;
	this->player->SetVelocity(vec3_origin);
	this->player->SetMoveType(MOVETYPE_NONE);
	this->player->GetPlayerPawn()->SetGravityScale(0);

	if (this->GetTimerRunning())
	{
		this->hasPausedInThisRun = true;
		this->lastPauseTime = g_pSurfUtils->GetServerGlobals()->curtime;
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnPausePost(this->player);
	}
}

bool SurfTimerService::CanPause(bool showError)
{
	if (this->paused)
	{
		return false;
	}

	Vector velocity;
	this->player->GetVelocity(&velocity);

	if (this->GetTimerRunning())
	{
		if (this->hasResumedInThisRun && g_pSurfUtils->GetServerGlobals()->curtime - this->lastResumeTime < SURF_PAUSE_COOLDOWN)
		{
			if (showError)
			{
				this->player->languageService->PrintChat(true, false, "Can't Pause (Just Resumed)");
				this->player->PlayErrorSound();
			}
			return false;
		}
		else if (!(this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND) && !(velocity.Length2D() == 0.0f && velocity.z == 0.0f))
		{
			if (showError)
			{
				this->player->languageService->PrintChat(true, false, "Can't Pause (Midair)");
				this->player->PlayErrorSound();
			}
			return false;
		}
	}
	return true;
}

void SurfTimerService::Resume(bool force)
{
	if (!this->paused)
	{
		return;
	}
	if (!force && !this->CanResume(true))
	{
		return;
	}

	bool allowResume = true;
	FOR_EACH_VEC(eventListeners, i)
	{
		allowResume &= eventListeners[i]->OnResume(this->player);
	}
	if (!allowResume)
	{
		this->player->languageService->PrintChat(true, false, "Can't Resume (Generic)");
		this->player->PlayErrorSound();
		return;
	}

	if (this->pausedOnLadder)
	{
		this->player->SetMoveType(MOVETYPE_LADDER);
	}
	else
	{
		this->player->SetMoveType(MOVETYPE_WALK);
	}

	// GOKZ: prevent noclip exploit
	this->player->GetPlayerPawn()->m_Collision().m_CollisionGroup() = SURF_COLLISION_GROUP_STANDARD;
	this->player->GetPlayerPawn()->CollisionRulesChanged();

	this->paused = false;
	if (this->GetTimerRunning())
	{
		this->hasResumedInThisRun = true;
		this->lastResumeTime = g_pSurfUtils->GetServerGlobals()->curtime;
	}
	this->player->GetMoveServices()->m_flDuckAmount = this->lastDuckValue;
	this->player->GetMoveServices()->m_flStamina = this->lastStaminaValue;
	this->player->GetPlayerPawn()->SetGravityScale(1);

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnResumePost(this->player);
	}
}

bool SurfTimerService::CanResume(bool showError)
{
	if (this->GetTimerRunning() && this->hasPausedInThisRun && g_pSurfUtils->GetServerGlobals()->curtime - this->lastPauseTime < SURF_PAUSE_COOLDOWN)
	{
		if (showError)
		{
			this->player->languageService->PrintChat(true, false, "Can't Resume (Just Paused)");
			this->player->PlayErrorSound();
		}
		return false;
	}
	return true;
}

void SurfTimerService::TogglePause()
{
	if (!this->player->IsAlive())
	{
		Surf::misc::JoinTeam(player, CS_TEAM_CT);
	}
	else
	{
		paused ? Resume() : Pause();
	}
}

SCMD(surf_timerstopsound, SCFL_TIMER | SCFL_PREFERENCE)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->timerService->ToggleTimerStopSound();
	return true;
}

SCMD_LINK(surf_tss, surf_timerstopsound);

void SurfTimerService::ToggleTimerStopSound()
{
	this->shouldPlayTimerSound = !this->shouldPlayTimerSound;
	this->player->optionService->SetPreferenceBool("timerStopSound", this->shouldPlayTimerSound);
	this->player->languageService->PrintChat(true, false, this->shouldPlayTimerSound ? "Timer Stop Sound Enabled" : "Timer Stop Sound Disabled");
}

void SurfTimerService::Reset()
{
	this->timerRunning = {};
	this->currentTime = {};
	this->currentCourseGUID = 0;
	this->lastEndTime = {};
	this->lastFalseEndTime = {};
	this->lastStartSoundTime = {};
	this->lastMissedTimeSoundTime = {};
	this->validTime = {};
	this->paused = {};
	this->pausedOnLadder = {};
	this->lastPauseTime = {};
	this->hasPausedInThisRun = {};
	this->lastResumeTime = {};
	this->hasResumedInThisRun = {};
	this->lastDuckValue = {};
	this->lastStaminaValue = {};
	this->validJump = {};
	this->lastInvalidateTime = {};
	this->touchedGroundSinceTouchingStartZone = {};
	this->shouldPlayTimerSound = true;
	this->ClearPBCache();
}

void SurfTimerService::OnPhysicsSimulatePost()
{
	if (this->player->IsAlive() && this->GetTimerRunning() && !this->GetPaused())
	{
		this->currentTime += ENGINE_FIXED_TICK_INTERVAL;
		this->CheckMissedTime();
		this->lastTickSpeed = this->GetCurrentSpeed();
	}
}

void SurfTimerService::OnStartTouchGround()
{
	this->touchedGroundSinceTouchingStartZone = true;
}

void SurfTimerService::OnStopTouchGround()
{
	if (this->HasValidMoveType() && this->lastInvalidateTime != g_pSurfUtils->GetServerGlobals()->curtime)
	{
		this->validJump = true;
	}
	else
	{
		this->InvalidateJump();
	}
}

void SurfTimerService::OnChangeMoveType(MoveType_t oldMoveType)
{
	if (oldMoveType == MOVETYPE_LADDER && this->player->GetMoveType() == MOVETYPE_WALK
		&& this->lastInvalidateTime != g_pSurfUtils->GetServerGlobals()->curtime)
	{
		this->validJump = true;
	}
	else
	{
		this->InvalidateJump();
	}
	// Check if player has escaped MOVETYPE_NONE
	if (!this->paused || this->player->GetMoveType() == MOVETYPE_NONE)
	{
		return;
	}

	this->paused = false;
	if (this->GetTimerRunning())
	{
		this->hasResumedInThisRun = true;
		this->lastResumeTime = g_pSurfUtils->GetServerGlobals()->curtime;
	}

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnResumePost(this->player);
	}
}

void SurfTimerService::OnTeleportToStart()
{
	this->TimerStop();
}

void SurfTimerService::OnClientDisconnect()
{
	this->TimerStop();
}

void SurfTimerService::OnPlayerSpawn()
{
	if (!this->player->GetPlayerPawn() || !this->paused)
	{
		return;
	}

	// Player has left paused state by spawning in, so resume
	this->paused = false;
	if (this->GetTimerRunning())
	{
		this->hasResumedInThisRun = true;
		this->lastResumeTime = g_pSurfUtils->GetServerGlobals()->curtime;
	}
	this->player->GetMoveServices()->m_flDuckAmount = this->lastDuckValue;
	this->player->GetMoveServices()->m_flStamina = this->lastStaminaValue;

	FOR_EACH_VEC(eventListeners, i)
	{
		eventListeners[i]->OnResumePost(this->player);
	}
}

void SurfTimerService::OnPlayerJoinTeam(i32 team)
{
	if (team == CS_TEAM_SPECTATOR)
	{
		this->paused = true;
		if (this->GetTimerRunning())
		{
			this->hasPausedInThisRun = true;
			this->lastPauseTime = g_pSurfUtils->GetServerGlobals()->curtime;
		}

		FOR_EACH_VEC(eventListeners, i)
		{
			eventListeners[i]->OnPausePost(this->player);
		}
	}
}

void SurfTimerService::OnPlayerDeath()
{
	this->TimerStop();
}

void SurfTimerService::OnRoundStart()
{
	SurfTimerService::TimerStopAll();
}

void SurfTimerService::OnTeleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity)
{
	if (newPosition || newVelocity)
	{
		this->InvalidateJump();
	}
}

SCMD(surf_stop, SCFL_TIMER)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (player->timerService->GetTimerRunning())
	{
		player->timerService->TimerStop();
	}
	return true;
}

SCMD(surf_pause, SCFL_TIMER)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->timerService->TogglePause();
	return true;
}

SCMD(surf_comparelevel, SCFL_RECORD | SCFL_TIMER | SCFL_PREFERENCE)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->timerService->SetCompareTarget(args->Arg(1));
	return true;
}

static_function SurfTimerService::CompareType GetCompareTypeFromString(const char *typeString)
{
	if (V_stricmp("off", typeString) == 0 || V_stricmp("none", typeString) == 0)
	{
		return SurfTimerService::CompareType::COMPARE_NONE;
	}
	if (V_stricmp("spb", typeString) == 0)
	{
		return SurfTimerService::CompareType::COMPARE_SPB;
	}
	if (V_stricmp("gpb", typeString) == 0 || V_stricmp("pb", typeString) == 0)
	{
		return SurfTimerService::CompareType::COMPARE_GPB;
	}
	if (V_stricmp("sr", typeString) == 0)
	{
		return SurfTimerService::CompareType::COMPARE_SR;
	}
	if (V_stricmp("wr", typeString) == 0)
	{
		return SurfTimerService::CompareType::COMPARE_WR;
	}
	return SurfTimerService::CompareType::COMPARETYPE_COUNT;
}

void SurfTimerService::SetCompareTarget(const char *typeString)
{
	if (!typeString || !V_stricmp("", typeString))
	{
		this->player->languageService->PrintChat(true, false, "Compare Command Usage");
		return;
	}

	CompareType type = GetCompareTypeFromString(typeString);
	if (type == COMPARETYPE_COUNT)
	{
		this->player->languageService->PrintChat(true, false, "Compare Command Usage");
		return;
	}

	assert(type < COMPARETYPE_COUNT && type >= COMPARE_NONE);
	switch (type)
	{
		case COMPARE_NONE:
		{
			this->player->languageService->PrintChat(true, false, "Compare Disabled");
			break;
		}
		case COMPARE_SPB:
		{
			this->player->languageService->PrintChat(true, false, "Compare Server PB");
			break;
		}
		case COMPARE_GPB:
		{
			this->player->languageService->PrintChat(true, false, "Compare Global PB");
			break;
		}
		case COMPARE_SR:
		{
			this->player->languageService->PrintChat(true, false, "Compare Server Record");
			break;
		}
		case COMPARE_WR:
		{
			this->player->languageService->PrintChat(true, false, "Compare World Record");
			break;
		}
	}
	this->preferredCompareType = type;
	this->player->optionService->SetPreferenceInt("preferredCompareType", this->preferredCompareType);
	if (this->GetCourse())
	{
		this->UpdateCurrentCompareType(ToPBDataKey(Surf::mode::GetModeInfo(this->player->modeService).id, this->GetCourse()->guid));
	}
}

void SurfTimerService::UpdateCurrentCompareType(PBDataKey key)
{
	for (u8 type = this->preferredCompareType; type > COMPARE_NONE; type--)
	{
		if (this->GetCompareTargetForType((CompareType)type, key))
		{
			this->currentCompareType = (CompareType)type;
			return;
		}
	}
	this->currentCompareType = COMPARE_NONE;
}

const PBData *SurfTimerService::GetCompareTargetForType(CompareType type, PBDataKey key)
{
	switch (type)
	{
		case COMPARE_WR:
		{
			if (SurfTimerService::wrCache.find(key) != SurfTimerService::wrCache.end())
			{
				return &SurfTimerService::wrCache[key];
			}
			break;
		}
		case COMPARE_SR:
		{
			if (SurfTimerService::srCache.find(key) != SurfTimerService::srCache.end())
			{
				return &SurfTimerService::srCache[key];
			}
			break;
		}
		case COMPARE_GPB:
		{
			if (SurfTimerService::globalPBCache.find(key) != SurfTimerService::globalPBCache.end())
			{
				return &this->globalPBCache[key];
			}
			break;
		}
		case COMPARE_SPB:
		{
			if (SurfTimerService::localPBCache.find(key) != SurfTimerService::localPBCache.end())
			{
				return &this->localPBCache[key];
			}
			break;
		}
	}
	return nullptr;
}

const PBData *SurfTimerService::GetCompareTarget(PBDataKey key)
{
	switch (this->currentCompareType)
	{
		case COMPARE_WR:
		{
			if (SurfTimerService::wrCache.find(key) != SurfTimerService::wrCache.end())
			{
				return &SurfTimerService::wrCache[key];
			}
			break;
		}
		case COMPARE_SR:
		{
			if (SurfTimerService::srCache.find(key) != SurfTimerService::srCache.end())
			{
				return &SurfTimerService::srCache[key];
			}
			break;
		}
		case COMPARE_GPB:
		{
			if (SurfTimerService::globalPBCache.find(key) != SurfTimerService::globalPBCache.end())
			{
				return &this->globalPBCache[key];
			}
			break;
		}
		case COMPARE_SPB:
		{
			if (SurfTimerService::localPBCache.find(key) != SurfTimerService::localPBCache.end())
			{
				return &this->localPBCache[key];
			}
			break;
		}
	}
	return nullptr;
}

void SurfTimerService::ClearRecordCache()
{
	SurfTimerService::srCache.clear();
	SurfTimerService::wrCache.clear();
	for (i32 i = 0; i < MAXPLAYERS + 1; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(i);
		if (player && player->timerService)
		{
			player->timerService->ClearPBCache();
		}
	}
}

void SurfTimerService::UpdateLocalRecordCache()
{
	SurfHUDService::InvalidateTopCache();
	auto onQuerySuccess = [](std::vector<ISQLQuery *> queries)
	{
		ISQLResult *result = queries[0]->GetResultSet();
		if (result && result->GetRowCount() > 0)
		{
			while (result->FetchRow())
			{
				auto modeInfo = Surf::mode::GetModeInfoFromDatabaseID(result->GetInt(2));
				if (modeInfo.databaseID < 0)
				{
					continue;
				}
				const SurfCourseDescriptor *course = Surf::course::GetCourseByLocalCourseID(result->GetInt(1));
				if (!course)
				{
					continue;
				}
				SurfTimerService::InsertRecordToCache(result->GetFloat(0), course, modeInfo.id, false, result->GetString(3));
			}
		}
	};
	SurfDatabaseService::QueryAllRecords(g_pSurfUtils->GetCurrentMapName(), onQuerySuccess, SurfDatabaseService::OnGenericTxnFailure);
}

template<size_t N>
static_function void ReadZoneArray(KeyValues3 &kv, const char *name, i32 count, CUtlVectorFixed<f64, N> &out)
{
	KeyValues3 *data = kv.FindMember(name);
	if (data && data->GetType() == KV3_TYPE_ARRAY)
	{
		for (i32 i = 0; i < count; i++)
		{
			KeyValues3 *element = data->GetArrayElement(i);
			out[i] = element ? element->GetDouble(-1.0) : -1.0;
		}
	}
}

void SurfTimerService::InsertRecordToCache(f64 time, const SurfCourseDescriptor *course, PluginId modeID, bool global, CUtlString metadata)
{
	PBData &pb = global ? SurfTimerService::wrCache[ToPBDataKey(modeID, course->guid)] : SurfTimerService::srCache[ToPBDataKey(modeID, course->guid)];

	pb.overall.pbTime = time;
	KeyValues3 kv(KV3_TYPEEX_TABLE, KV3_SUBTYPE_UNSPECIFIED);
	CUtlString error = "";
	if (metadata.IsEmpty())
	{
		return;
	}
	LoadKV3FromJSON(&kv, &error, metadata.Get(), "");
	if (!error.IsEmpty())
	{
		META_CONPRINTF("[Surf::Timer] Failed to insert PB to cache due to metadata error: %s\n", error.Get());
		return;
	}

	KeyValues3 *data = kv.FindMember("cpZoneTimes");
	if (data && data->GetType() == KV3_TYPE_ARRAY)
	{
		for (i32 i = 0; i < course->checkpointCount; i++)
		{
			f64 time = -1.0f;
			KeyValues3 *element = data->GetArrayElement(i);
			if (element)
			{
				time = element->GetDouble(-1.0);
			}
			pb.overall.pbCpZoneTimes[i] = time;
		}
	}

	i32 totalStages = SurfTimerService::GetTotalStages(course);
	ReadZoneArray(kv, "stageZoneTimes", totalStages, pb.overall.pbStageZoneTimes);
	ReadZoneArray(kv, "cpZoneSpeeds", course->checkpointCount, pb.overall.pbCpZoneSpeeds);
	ReadZoneArray(kv, "stageZoneSpeeds", totalStages, pb.overall.pbStageZoneSpeeds);
	ReadZoneArray(kv, "stageTouchTimes", totalStages, pb.overall.pbStageTouchTimes);
}

void SurfTimerService::ClearPBCache()
{
	this->localPBCache.clear();
}

const PBData *SurfTimerService::GetGlobalCachedPB(const SurfCourseDescriptor *course, PluginId modeID)
{
	PBDataKey key = ToPBDataKey(modeID, course->guid);

	if (this->globalPBCache.find(key) == this->globalPBCache.end())
	{
		return nullptr;
	}

	return &this->globalPBCache[key];
}

void SurfTimerService::InsertPBToCache(f64 time, const SurfCourseDescriptor *course, PluginId modeID, bool global, CUtlString metadata, f64 points)
{
	PBData &pb = global ? this->globalPBCache[ToPBDataKey(modeID, course->guid)] : this->localPBCache[ToPBDataKey(modeID, course->guid)];

	pb.overall.points = points;
	pb.overall.pbTime = time;
	KeyValues3 kv(KV3_TYPEEX_TABLE, KV3_SUBTYPE_UNSPECIFIED);
	CUtlString error = "";
	if (metadata.IsEmpty())
	{
		return;
	}
	LoadKV3FromJSON(&kv, &error, metadata.Get(), "");
	if (!error.IsEmpty())
	{
		META_CONPRINTF("[Surf::Timer] Failed to insert server record to cache due to metadata error: %s\n", error.Get());
		return;
	}

	KeyValues3 *data = kv.FindMember("cpZoneTimes");
	if (data && data->GetType() == KV3_TYPE_ARRAY)
	{
		for (i32 i = 0; i < course->checkpointCount; i++)
		{
			f64 time = -1.0f;
			KeyValues3 *element = data->GetArrayElement(i);
			if (element)
			{
				time = element->GetDouble(-1.0);
			}
			pb.overall.pbCpZoneTimes[i] = time;
		}
	}

	i32 totalStages = SurfTimerService::GetTotalStages(course);
	ReadZoneArray(kv, "stageZoneTimes", totalStages, pb.overall.pbStageZoneTimes);
	ReadZoneArray(kv, "cpZoneSpeeds", course->checkpointCount, pb.overall.pbCpZoneSpeeds);
	ReadZoneArray(kv, "stageZoneSpeeds", totalStages, pb.overall.pbStageZoneSpeeds);
	ReadZoneArray(kv, "stageTouchTimes", totalStages, pb.overall.pbStageTouchTimes);
}

void SurfTimerService::CheckMissedTime()
{
	const SurfCourseDescriptor *course = this->GetCourse();
	// No active course, the timer is not running or if we already announce late PBs.
	if (!course || !this->GetTimerRunning() || !this->shouldAnnounceMissedTime)
	{
		return;
	}
	// No comparison available for styled runs.
	if (this->player->styleServices.Count() > 0)
	{
		return;
	}
	auto modeInfo = Surf::mode::GetModeInfo(this->player->modeService->GetModeName());

	PBDataKey key = ToPBDataKey(modeInfo.id, course->guid);

	// Check if there is personal best data for this mode and course.
	auto pb = this->GetCompareTarget(key);
	if (!pb)
	{
		return;
	}

	if (this->shouldAnnounceMissedTime && pb->overall.pbTime > 0 && this->GetTime() > pb->overall.pbTime)
	{
		CUtlString timeText = utils::FormatTime(pb->overall.pbTime);
		this->player->languageService->PrintChat(true, false, missedTimeKeys[this->currentCompareType], timeText.Get());
		this->shouldAnnounceMissedTime = false;
		this->PlayMissedTimeSound();
	}
}

// " | 850 u/s (+12)" for chat checkpoint/stage lines, green = faster than PB, gold = slower.
static std::string FormatChatSpeed(f32 speed, f32 pbSpeed)
{
	char buf[96];
	if (pbSpeed >= 0.0f)
	{
		f32 diff = speed - pbSpeed;
		V_snprintf(buf, sizeof(buf), " {grey}| {default}%.0f {grey}u/s %s(%s%.0f){grey}", speed, diff >= 0.0f ? "{green}" : "{gold}",
				   diff >= 0.0f ? "+" : "", diff);
	}
	else
	{
		V_snprintf(buf, sizeof(buf), " {grey}| {default}%.0f {grey}u/s", speed);
	}
	return buf;
}

void SurfTimerService::ShowCheckpointText(u32 currentCheckpoint)
{
	const SurfCourseDescriptor *course = this->GetCourse();
	// No active course so we can't compare anything.
	if (!course)
	{
		return;
	}
	// No comparison available for styled runs.
	if (this->player->styleServices.Count() > 0)
	{
		return;
	}

	CUtlString time;
	std::string pbDiff = "";

	time = utils::FormatTime(this->cpZoneTimes[currentCheckpoint - 1]);
	if (this->lastCheckpoint != 0)
	{
		f64 diff = this->cpZoneTimes[currentCheckpoint - 1] - this->cpZoneTimes[this->lastCheckpoint - 1];
		CUtlString splitTime = SurfTimerService::FormatDiffTime(diff);
		splitTime.Format(" {grey}({default}%s{grey})", splitTime.Get());
		time.Append(splitTime.Get());
	}

	auto modeInfo = Surf::mode::GetModeInfo(this->player->modeService->GetModeName());
	PBDataKey key = ToPBDataKey(modeInfo.id, course->guid);

	// Check if there is personal best data for this mode and course.
	const PBData *pb = this->GetCompareTarget(key);
	if (pb)
	{
		if (pb->overall.pbCpZoneTimes[currentCheckpoint - 1] > 0)
		{
			f64 diff = this->cpZoneTimes[currentCheckpoint - 1] - pb->overall.pbCpZoneTimes[currentCheckpoint - 1];
			CUtlString diffText = SurfTimerService::FormatDiffTime(diff);
			diffText.Format("{grey}%s%s{grey}", diff < 0 ? "{blue}" : "{lightred}", diffText.Get());
			pbDiff = this->player->languageService->PrepareMessage(diffTextKeys[this->currentCompareType], diffText.Get());
		}
	}

	pbDiff += FormatChatSpeed((f32)this->cpZoneSpeeds[currentCheckpoint - 1], pb ? (f32)pb->overall.pbCpZoneSpeeds[currentCheckpoint - 1] : -1.0f);
	this->player->languageService->PrintChat(true, false, "Course Checkpoint Reached", currentCheckpoint, time.Get(), pbDiff.c_str());

	// HUD split flash
	{
		bool hasDiff = pb && pb->overall.pbCpZoneTimes[currentCheckpoint - 1] > 0;
		f64 diff = hasDiff ? this->cpZoneTimes[currentCheckpoint - 1] - pb->overall.pbCpZoneTimes[currentCheckpoint - 1] : 0.0;
		f32 pbSpeed = pb ? (f32)pb->overall.pbCpZoneSpeeds[currentCheckpoint - 1] : -1.0f;
		// Trackmania style: where this split would place the run among the top runs. Nothing to rank against = 1st.
		i32 rank = SurfHUDService::GetSplitRank(course, modeInfo, false, currentCheckpoint - 1, this->cpZoneTimes[currentCheckpoint - 1]);
		std::string ordinal = SurfHUDService::FormatSplitRank(rank);
		this->player->hudService->SetSplitFlash(ordinal.c_str(), utils::FormatTime(this->cpZoneTimes[currentCheckpoint - 1]).Get(), diff, hasDiff,
												(f32)this->cpZoneSpeeds[currentCheckpoint - 1], pbSpeed);
	}
}

i32 SurfTimerService::GetTotalStages(const SurfCourseDescriptor *course)
{
	if (!course || course->stageCount <= 0)
	{
		return 0;
	}
	// stage zones are numbered 2..N, the start zone is stage 1: one more stage than stage zones (capped to the array size)
	return (std::min)(course->stageCount + 1, (i32)SURF_MAX_STAGE_ZONES);
}

void SurfTimerService::SubmitStageTime(const SurfCourseDescriptor *course, i32 stage)
{
	if (!course || stage < 1 || stage > this->stageZoneTimes.Count() || this->stageZoneTimes[stage - 1] < 0)
	{
		return;
	}
	if (this->player->GetPlayerPawn()->IsBot() || this->player->styleServices.Count() > 0 || !this->validTime)
	{
		return;
	}
	if (course->localDatabaseID == 0 || !SurfDatabaseService::IsReady())
	{
		return;
	}
	auto modeInfo = Surf::mode::GetModeInfo(this->player->modeService->GetModeName());
	if (modeInfo.databaseID < 0)
	{
		return;
	}
	u64 steamID = this->player->GetSteamId64();
	f64 time = this->stageZoneTimes[stage - 1];
	f32 speed = (f32)this->stageZoneSpeeds[stage - 1];
	u32 courseGUID = course->guid;
	CPlayerSlot slot = this->player->GetPlayerSlot();
	auto onSuccess = [slot, courseGUID, stage](std::vector<ISQLQuery *> queries)
	{
		// the stage lists and the player's own stage best may have changed
		SurfHUDService::InvalidateStageCache(courseGUID, stage);
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(slot);
		if (player && player->hudService)
		{
			player->hudService->InvalidateOwnStageCache();
		}
	};
	SurfDatabaseService::SaveStageTime(steamID, course->localDatabaseID, modeInfo.databaseID, 0, stage, time, speed, onSuccess,
									   SurfDatabaseService::OnGenericTxnFailure);
}

// Stage cleared. Run mode (default): the cumulative time, compared and ranked like a checkpoint of the full run.
// Stage mode (!stagemode): the stage segment, compared with the player's stage best (or the stage record) and ranked among the
// standalone stage records.
void SurfTimerService::ShowStageText(i32 stage)
{
	const SurfCourseDescriptor *course = this->GetCourse();
	// No active course so we can't compare anything.
	if (!course || stage < 1 || stage > this->stageZoneTimes.Count())
	{
		return;
	}
	// No comparison available for styled runs.
	if (this->player->styleServices.Count() > 0)
	{
		return;
	}
	i32 idx = stage - 1;
	auto modeInfo = Surf::mode::GetModeInfo(this->player->modeService->GetModeName());
	PBDataKey key = ToPBDataKey(modeInfo.id, course->guid);
	const PBData *pb = this->GetCompareTarget(key);
	const bool stageMode = this->player->hudService->IsStageMode();

	f64 segment = this->stageZoneTimes[idx];
	f64 cumulative = this->stageTouchTimes[idx];
	f32 speed = (f32)this->stageZoneSpeeds[idx];

	// what is shown, what it is compared with and where it ranks
	f64 shown = stageMode ? segment : cumulative;
	f64 other = stageMode ? cumulative : segment;
	f64 compareTime = -1.0;
	f32 compareSpeed = -1.0f;
	i32 rank = 0;
	if (stageMode)
	{
		const SurfHUDService::StageBest *best = this->player->hudService->GetOwnStageBest(course, modeInfo, stage);
		if (this->currentCompareType == COMPARE_SR || this->currentCompareType == COMPARE_WR)
		{
			const std::vector<SurfHUDService::StageEntry> *top = SurfHUDService::GetStageTopList(course, modeInfo, stage);
			if (top && !top->empty())
			{
				compareTime = (*top)[0].time;
				compareSpeed = (f32)(*top)[0].speed;
			}
		}
		else if (best && best->time > 0)
		{
			compareTime = best->time;
			compareSpeed = (f32)best->speed;
		}
		rank = SurfHUDService::GetStageRank(course, modeInfo, stage, segment);
	}
	else
	{
		if (pb && pb->overall.pbStageTouchTimes[idx] > 0)
		{
			compareTime = pb->overall.pbStageTouchTimes[idx];
		}
		if (pb)
		{
			compareSpeed = (f32)pb->overall.pbStageZoneSpeeds[idx];
		}
		rank = SurfHUDService::GetSplitRank(course, modeInfo, true, idx, cumulative);
	}

	// chat: "Stage #3: 00:31.234 (00:07.406) | SPB +00:00.188 | 828 u/s (-109)" - the other measure in brackets
	CUtlString time = utils::FormatTime(shown);
	CUtlString otherText;
	otherText.Format(" {grey}({default}%s{grey})", utils::FormatTime(other).Get());
	time.Append(otherText.Get());
	std::string pbDiff = "";
	if (compareTime > 0)
	{
		f64 diff = shown - compareTime;
		CUtlString diffText = SurfTimerService::FormatDiffTime(diff);
		diffText.Format("{grey}%s%s{grey}", diff < 0 ? "{blue}" : "{lightred}", diffText.Get());
		pbDiff = this->player->languageService->PrepareMessage(diffTextKeys[this->currentCompareType], diffText.Get());
	}
	pbDiff += FormatChatSpeed(speed, compareSpeed);
	this->player->languageService->PrintChat(true, false, stageMode ? "Course Stage Reached (Stage Mode)" : "Course Stage Reached", stage, time.Get(),
											 pbDiff.c_str());

	// HUD split flash
	bool hasDiff = compareTime > 0;
	f64 diff = hasDiff ? shown - compareTime : 0.0;
	std::string ordinal = SurfHUDService::FormatSplitRank(rank);
	this->player->hudService->SetSplitFlash(ordinal.c_str(), utils::FormatTime(shown).Get(), diff, hasDiff, speed, compareSpeed);
}

void SurfTimerService::ShowFinishText(const SurfCourseDescriptor *course, f32 time)
{
	// No comparison for styled runs, same as the checkpoint / stage flashes.
	if (!course || this->player->styleServices.Count() > 0)
	{
		return;
	}
	auto modeInfo = Surf::mode::GetModeInfo(this->player->modeService->GetModeName());
	PBDataKey key = ToPBDataKey(modeInfo.id, course->guid);
	const PBData *pb = this->GetCompareTarget(key);
	bool hasDiff = pb && pb->overall.pbTime > 0;
	f64 diff = hasDiff ? (f64)time - pb->overall.pbTime : 0.0;
	i32 rank = SurfHUDService::GetFinishRank(course, modeInfo, time);
	std::string ordinal = SurfHUDService::FormatSplitRank(rank);
	// no end speed is stored with records, so the finish flash shows the speed without a diff
	this->player->hudService->SetSplitFlash(ordinal.c_str(), utils::FormatTime(time).Get(), diff, hasDiff, this->GetSplitSpeed(), -1.0f);
}

CUtlString SurfTimerService::GetCurrentRunMetadata()
{
	KeyValues3 kv(KV3_TYPEEX_TABLE, KV3_SUBTYPE_UNSPECIFIED);

	KeyValues3 *cpZoneTimesKV = kv.FindOrCreateMember("cpZoneTimes");
	cpZoneTimesKV->SetToEmptyArray();
	FOR_EACH_VEC(this->cpZoneTimes, i)
	{
		KeyValues3 *time = cpZoneTimesKV->ArrayAddElementToTail();
		time->SetDouble(this->cpZoneTimes[i]);
	}

	KeyValues3 *stageZoneTimesKV = kv.FindOrCreateMember("stageZoneTimes");
	stageZoneTimesKV->SetToEmptyArray();
	FOR_EACH_VEC(this->stageZoneTimes, i)
	{
		KeyValues3 *time = stageZoneTimesKV->ArrayAddElementToTail();
		time->SetDouble(this->stageZoneTimes[i]);
	}

	KeyValues3 *cpZoneSpeedsKV = kv.FindOrCreateMember("cpZoneSpeeds");
	cpZoneSpeedsKV->SetToEmptyArray();
	FOR_EACH_VEC(this->cpZoneSpeeds, i)
	{
		KeyValues3 *v = cpZoneSpeedsKV->ArrayAddElementToTail();
		v->SetDouble(this->cpZoneSpeeds[i]);
	}

	KeyValues3 *stageZoneSpeedsKV = kv.FindOrCreateMember("stageZoneSpeeds");
	stageZoneSpeedsKV->SetToEmptyArray();
	FOR_EACH_VEC(this->stageZoneSpeeds, i)
	{
		KeyValues3 *v = stageZoneSpeedsKV->ArrayAddElementToTail();
		v->SetDouble(this->stageZoneSpeeds[i]);
	}

	KeyValues3 *stageTouchTimesKV = kv.FindOrCreateMember("stageTouchTimes");
	stageTouchTimesKV->SetToEmptyArray();
	FOR_EACH_VEC(this->stageTouchTimes, i)
	{
		KeyValues3 *v = stageTouchTimesKV->ArrayAddElementToTail();
		v->SetDouble(this->stageTouchTimes[i]);
	}

	CUtlString result, error;
	if (SaveKV3AsJSON(&kv, &error, &result))
	{
		return result;
	}
	META_CONPRINTF("[Surf::Timer] Failed to obtain current run's metadata! (%s)\n", error.Get());
	return "";
}

void SurfTimerService::UpdateLocalPBCache()
{
	CPlayerUserId uid = player->GetClient()->GetUserID();

	auto onQuerySuccess = [uid](std::vector<ISQLQuery *> queries)
	{
		SurfPlayer *pl = g_pSurfPlayerManager->ToPlayer(uid);
		if (!pl)
		{
			return;
		}
		ISQLResult *result = queries[0]->GetResultSet();
		if (result && result->GetRowCount() > 0)
		{
			while (result->FetchRow())
			{
				auto modeInfo = Surf::mode::GetModeInfoFromDatabaseID(result->GetInt(2));
				if (modeInfo.databaseID < 0)
				{
					continue;
				}
				const SurfCourseDescriptor *course = Surf::course::GetCourseByLocalCourseID(result->GetInt(1));
				if (!course)
				{
					continue;
				}
				pl->timerService->InsertPBToCache(result->GetFloat(0), course, modeInfo.id, false, result->GetString(3));
			}
		}
	};
	SurfDatabaseService::QueryAllPBs(player->GetSteamId64(), g_pSurfUtils->GetCurrentMapName(), onQuerySuccess,
									 SurfDatabaseService::OnGenericTxnFailure);
}

std::string SurfTimerService::GetStartSpeedText(const char *language)
{
	Vector velocity, baseVelocity;
	this->player->GetVelocity(&velocity);
	this->player->GetBaseVelocity(&baseVelocity);
	velocity += baseVelocity;

	float startSpeed = velocity.Length2D();
	return SurfLanguageService::PrepareMessageWithLang(language, "Start Speed", startSpeed);
}

void SurfTimerService::Init()
{
	SurfDatabaseService::RegisterEventListener(&databaseEventListener);
	SurfOptionService::RegisterEventListener(&optionEventListener);
}

void SurfTimerService::ApplyPreferences()
{
	if (this->player->optionService->GetPreferenceInt("preferredCompareType", COMPARE_GPB) > COMPARETYPE_COUNT)
	{
		this->preferredCompareType = COMPARE_GPB;
		return;
	}
	this->preferredCompareType = (CompareType)this->player->optionService->GetPreferenceInt("preferredCompareType", COMPARE_GPB);
	this->shouldPlayTimerSound = this->player->optionService->GetPreferenceBool("timerStopSound", true);
}

void SurfDatabaseServiceEventListener_Timer::OnMapSetup()
{
	// TODO: find a better way to do this, we now call SetupLocalCourses after all trigger_multiple spawns
	// Surf::course::SetupLocalCourses();
	SurfTimerService::UpdateLocalRecordCache();
}

void SurfDatabaseServiceEventListener_Timer::OnClientSetup(Player *player, u64 steamID64, bool isCheater)
{
	SurfPlayer *SurfPlayer = g_pSurfPlayerManager->ToSurfPlayer(player);
	SurfPlayer->timerService->UpdateLocalPBCache();
}
