#pragma once
#include "surf/surf.h"
#include "../timer/surf_timer.h"
#include "../mode/surf_mode.h"
#include <unordered_map>
#include <vector>

#define SURF_HUD_TIMER_STOPPED_GRACE_TIME 3.0f

// Compiled panorama layout inside the HUD workshop addon (see workshop/README.md).
#define SURF_HUD_LAYOUT "panorama/layout/custom_game/surfhud/hud.vxml_c"

class CCSCustomHudLayout;
class CCheckTransmitInfo;
struct SurfCourseDescriptor;

#define SURF_HUD_TOP_COUNT  5  // rows shown top left
#define SURF_HUD_TOP_RANKED 10 // runs cached for ranking splits

class SurfHUDService : public SurfBaseService
{
	using SurfBaseService::SurfBaseService;

public:
	// Checkpoint / stage split shown in the panel for a few seconds (compact style).
	struct SplitFlash
	{
		std::string label; // "CP 3" / "Stage 2"
		std::string time;  // formatted split/zone time
		std::string diff;  // formatted diff vs the compare target, "" when unknown
		bool faster {};
		std::string speed;     // speed when the zone was touched
		std::string speedDiff; // "+123" / "-45", "" when unknown
		bool speedFaster {};
		f64 expiry {};
	};

private:
	bool jumpedThisTick {};
	bool showPanel {};
	bool compactStyle {};
	bool showSync {};
	bool showKeys {};
	bool layoutStyle {};
	bool syncAltFont {};      // !syncfont: alternative font for the sync readout
	bool speedColor {};       // !speedcolor: colour the speed readout by speed band
	bool stageMode {};        // !stagemode: stage splits and the top list show standalone stage records instead of the full run
	i32 keysX {}, keysY {};   // !keys position, percent of the screen from the centre
	i32 splitX {}, splitY {}; // !splitpos: offset of the split box from its default place, percent of the screen
	i32 timerX {}, timerY {}; // !timerpos: offset of the bottom stacks (speed / sync and stage / timer)
	i32 speedX {}, speedY {}; // !speedpos: extra offset of the speed / sync stack on top of !timerpos
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
	void ToggleKeys();
	void SetKeysPosition(i32 x, i32 y);
	void SetSplitPosition(i32 x, i32 y);
	void SetTimerPosition(i32 x, i32 y);
	void SetSpeedPosition(i32 x, i32 y);

	void ToggleSyncFont();
	void ToggleSpeedColor();
	void ToggleStageMode();

	bool IsStageMode()
	{
		return this->stageMode;
	}

	// --- layout HUD (custom_hud_layout entity fed from a workshop addon), surf_hud_layout.cpp ---
	static void RefreshLayoutAvailability();
	static bool IsLayoutHudAvailable();
	static const char *GetLayoutAddon(); // workshop id from the "hud" config block, "" when unset
	static void Cleanup();
	static void OnCheckTransmit(CCheckTransmitInfo **pInfo, int infoCount);
	bool IsUsingLayoutStyle();
	bool UpdateHudLayout(SurfPlayer *source);
	void DestroyOwnedLayout();

	// --- top list (surf_hud_top.cpp) ---
	struct TopEntry
	{
		u64 steamID {};
		std::string alias;
		f64 time {};
		std::vector<f64> cpTimes;         // cumulative checkpoint times of that run
		std::vector<f64> stageTimes;      // per stage segment times of that run
		std::vector<f64> stageTouchTimes; // cumulative time at each stage clear (empty for runs saved before this existed)
	};

	struct RankInfo
	{
		bool pending = false;
		bool valid = false;
		f64 time = 0.0;
		u32 rank = 0;
	};

	static void InvalidateTopCache();

	// --- standalone stage records (StageTimes), surf_hud_top.cpp ---
	struct StageEntry
	{
		u64 steamID {};
		std::string alias;
		f64 time {};
		f64 speed {};
	};

	struct StageBest
	{
		bool pending = false;
		bool valid = false;
		f64 time = 0.0;
		f64 speed = -1.0;
		u32 rank = 0;
	};

	static void InvalidateStageCache(u32 courseGUID, i32 stage);
	static const std::vector<StageEntry> *GetStageTopList(const SurfCourseDescriptor *course, const SurfModeManager::ModePluginInfo &modeInfo,
														  i32 stage);
	static i32 GetStageRank(const SurfCourseDescriptor *course, const SurfModeManager::ModePluginInfo &modeInfo, i32 stage, f64 time);
	const StageBest *GetOwnStageBest(const SurfCourseDescriptor *course, const SurfModeManager::ModePluginInfo &modeInfo, i32 stage);
	void InvalidateOwnStageCache();
	std::unordered_map<u64, StageBest> ownStageCache; // key: PBDataKey << 8 | stage
	u32 ownStageCacheGeneration {};
	static const std::vector<TopEntry> *GetTopList(const SurfCourseDescriptor *course, const SurfModeManager::ModePluginInfo &modeInfo);
	const RankInfo *GetOwnRank(const SurfCourseDescriptor *course, const SurfModeManager::ModePluginInfo &modeInfo);
	static i32 GetSplitRank(const SurfCourseDescriptor *course, const SurfModeManager::ModePluginInfo &modeInfo, bool stage, i32 index, f64 time);
	static i32 GetFinishRank(const SurfCourseDescriptor *course, const SurfModeManager::ModePluginInfo &modeInfo, f64 time);
	static std::string FormatOrdinal(i32 rank);
	// Label for the split flash: "1st" when there is nothing to rank against, "10th+" past the cached top runs.
	static std::string FormatSplitRank(i32 rank);
	std::unordered_map<PBDataKey, RankInfo> rankCache;
	u32 rankCacheGeneration {};

	struct LayoutElement
	{
		i8 hidden {-1};
		std::string text {};
		const char *className {};
	};

	struct LayoutState
	{
		LayoutElement splitLabel, splitTime, splitDiff, splitSpeed, splitSpeedDiff, speed, sync, timer, stage, topHeader;
		LayoutElement topRank[SURF_HUD_TOP_COUNT + 1], topName[SURF_HUD_TOP_COUNT + 1], topTime[SURF_HUD_TOP_COUNT + 1];
		i8 topSelf[SURF_HUD_TOP_COUNT + 1] {-1, -1, -1, -1, -1, -1};
		i8 keysHidden {-1};
		i8 keys[6] {-1, -1, -1, -1, -1, -1};
		i32 keysX {INT_MIN};
		i32 keysY {INT_MIN};
		i32 splitX {INT_MIN};
		i32 splitY {INT_MIN};
		i32 timerX {INT_MIN};
		i32 timerY {INT_MIN};
		i32 speedX {INT_MIN};
		i32 speedY {INT_MIN};
	};

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
	CHandle<CBaseEntity> ownedLayout {};
	LayoutState layoutState {};
	i32 clearCentreTicks {};
	CCSCustomHudLayout *EnsureOwnedLayout(bool &created);

	std::string GetSpeedText(const char *language = SURF_DEFAULT_LANGUAGE);
	std::string GetKeyText(const char *language = SURF_DEFAULT_LANGUAGE);
	std::string GetTimerText(const char *language = SURF_DEFAULT_LANGUAGE);
	std::string GetStageText(const char *language = SURF_DEFAULT_LANGUAGE);
	std::string GetCompactHtml(const char *language, SurfPlayer *target);
};
