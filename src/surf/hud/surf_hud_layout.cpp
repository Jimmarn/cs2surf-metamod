/*
 * Layout HUD: a custom_hud_layout entity per player, driven through dialog variables and CSS classes.
 *
 * The layout itself (panorama/layout/custom_game/surfhud/hud.xml + styles) lives in a workshop addon that
 * MultiAddonManager makes every client download, see the "hud" block in cfg/cs2surf-server-config.txt and
 * workshop/README.md. Each player gets their own entity that is only ever transmitted to them, so the
 * entity's global layout state can be used for per-player values (the per-player state does not propagate
 * class changes to child panels).
 */
#include "surf/surf.h"
#include "cs2surf.h"
#include "surf_hud.h"
#include "sdk/entity/ccscustomhudlayout.h"
#include "sdk/datatypes.h"
#include "entitykeyvalues.h"
#include "utils/utils.h"
#include "surf/option/surf_option.h"
#include "surf/timer/surf_timer.h"
#include "surf/mode/surf_mode.h"
#include "surf/language/surf_language.h"
#include "surf/replays/surf_replaysystem.h"
#include "surf/spec/surf_spec.h"

#include <vendor/MultiAddonManager/public/imultiaddonmanager.h>
#include "tier0/memdbgon.h"

extern IMultiAddonManager *g_pMultiAddonManager;

static_global bool layoutAssetMounted = false;

// Panel ids and dialog variables, see workshop/panorama/layout/custom_game/surfhud/hud.xml
#define SURFHUD_PANEL_SPLIT_LABEL     "hud_split_label"
#define SURFHUD_PANEL_SPLIT_TIME      "hud_split_time"
#define SURFHUD_PANEL_SPLIT_DIFF      "hud_split_diff"
#define SURFHUD_PANEL_SPLIT_SPEED     "hud_split_speed"
#define SURFHUD_PANEL_SPLIT_SPEEDDIFF "hud_split_speeddiff"
#define SURFHUD_PANEL_SPEED           "hud_speed"
#define SURFHUD_PANEL_SYNC            "hud_sync"
#define SURFHUD_PANEL_TIMER           "hud_timer"
#define SURFHUD_PANEL_STAGE           "hud_stage"
// top list rows: hud_top1_rank / hud_top1_name / hud_top1_time ... row 6 is the viewer's own PB when outside the top 5
static_global const char *const SURFHUD_TOP_RANK[SURF_HUD_TOP_COUNT + 1] = {"hud_top1_rank", "hud_top2_rank", "hud_top3_rank",
																			"hud_top4_rank", "hud_top5_rank", "hud_top6_rank"};
static_global const char *const SURFHUD_TOP_NAME[SURF_HUD_TOP_COUNT + 1] = {"hud_top1_name", "hud_top2_name", "hud_top3_name",
																			"hud_top4_name", "hud_top5_name", "hud_top6_name"};
static_global const char *const SURFHUD_TOP_TIME[SURF_HUD_TOP_COUNT + 1] = {"hud_top1_time", "hud_top2_time", "hud_top3_time",
																			"hud_top4_time", "hud_top5_time", "hud_top6_time"};
#define SURFHUD_PANEL_KEYS          "hud_keys"
#define SURFHUD_PANEL_SPLIT_OFFSET  "hud_split_offset"
#define SURFHUD_PANEL_BOTTOM_OFFSET "hud_bottom_offset"
#define SURFHUD_PANEL_SPEED_OFFSET  "hud_speed_offset"
#define SURFHUD_PANEL_TOP_HEADER    "hud_top_header"

// Order matches SurfHUDService::LayoutState::keys
static_global const char *const SURFHUD_KEY_PANELS[6] = {"hud_key_w", "hud_key_a", "hud_key_s", "hud_key_d", "hud_key_c", "hud_key_j"};

// classes from styles/custom_game/surfhud/hud.css
static_global const char *const SURFHUD_SPEED_CLASSES[] = {"speed-0", "speed-1", "speed-2", "speed-3", "speed-4", "speed-5",
														   "speed-6", "speed-7", "speed-8", "speed-9", "speed-10"};
static_global const i32 SURFHUD_SPEED_THRESHOLDS[] = {349, 699, 1049, 1399, 1749, 2099, 2449, 2799, 3149, 3499};
static_assert(SURF_ARRAYSIZE(SURFHUD_SPEED_CLASSES) == SURF_ARRAYSIZE(SURFHUD_SPEED_THRESHOLDS) + 1, "one speed class per threshold band");

// === availability ==========================================================

void SurfHUDService::RefreshLayoutAvailability()
{
	layoutAssetMounted = g_pFullFileSystem && g_pFullFileSystem->FileExists(SURF_HUD_LAYOUT, NULL);
}

bool SurfHUDService::IsLayoutHudAvailable()
{
	// With MultiAddonManager the addon is mounted for clients even when the server side lookup fails.
	return (g_pMultiAddonManager != nullptr && SurfHUDService::GetLayoutAddon()[0] != '\0') || layoutAssetMounted;
}

const char *SurfHUDService::GetLayoutAddon()
{
	static_persist char addon[32];
	KeyValues *kv = SurfOptionService::GetOptionKV("hud");
	V_strncpy(addon, kv ? kv->GetString("layoutAddon", "") : "", sizeof(addon));
	return addon;
}

bool SurfHUDService::IsUsingLayoutStyle()
{
	return SurfHUDService::IsLayoutHudAvailable() && this->layoutStyle;
}

// === entity lifecycle ======================================================

CCSCustomHudLayout *SurfHUDService::EnsureOwnedLayout(bool &created)
{
	created = false;
	if (g_SurfPlugin.unloading || !SurfHUDService::IsLayoutHudAvailable())
	{
		return NULL;
	}
	if (CBaseEntity *cached = this->ownedLayout.Get())
	{
		return (CCSCustomHudLayout *)cached;
	}
	CCSCustomHudLayout *layout = utils::CreateEntityByName<CCSCustomHudLayout>("custom_hud_layout");
	if (!layout)
	{
		return NULL;
	}
	CEntityKeyValues *pKeyValues = new CEntityKeyValues();
	pKeyValues->SetString("layout", SURF_HUD_LAYOUT);
	char name[32];
	V_snprintf(name, sizeof(name), "surfhud%i", this->player->GetPlayerSlot().Get());
	pKeyValues->SetString("targetname", name);
	layout->DispatchSpawn(pKeyValues);
	this->ownedLayout = layout->GetRefEHandle();
	this->layoutState = LayoutState();
	created = true;
	return layout;
}

void SurfHUDService::DestroyOwnedLayout()
{
	// Null on server exit.
	if (CBaseEntity *ent = GameEntitySystem() ? this->ownedLayout.Get() : nullptr)
	{
		g_pSurfUtils->RemoveEntity(ent);
	}
	this->ownedLayout = nullptr;
	this->layoutState = LayoutState();
}

void SurfHUDService::Cleanup()
{
	for (i32 i = 0; i < MAXPLAYERS; i++)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(CPlayerSlot(i));
		if (player && player->hudService)
		{
			player->hudService->DestroyOwnedLayout();
		}
	}
}

void SurfHUDService::OnCheckTransmit(CCheckTransmitInfo **pInfo, int infoCount)
{
	static_persist const i32 offset = g_pGameConfig->GetOffset("QuietPlayerSlot");
	for (i32 i = 0; i < infoCount; i++)
	{
		TransmitInfo *info = reinterpret_cast<TransmitInfo *>(pInfo[i]);
		const i32 recipient = *reinterpret_cast<int *>(reinterpret_cast<uintptr_t>(info) + offset);
		for (i32 owner = 0; owner < MAXPLAYERS; owner++)
		{
			if (owner == recipient)
			{
				continue;
			}
			SurfPlayer *ownerPlayer = g_pSurfPlayerManager->ToPlayer(CPlayerSlot(owner));
			if (!ownerPlayer || !ownerPlayer->hudService)
			{
				continue;
			}
			CBaseEntity *ent = ownerPlayer->hudService->ownedLayout.Get();
			if (ent)
			{
				info->m_pTransmitEdict->Clear(ent->entindex());
			}
		}
	}
}

// === writing to the layout =================================================

static_function void SetLayoutHidden(CCSCustomHudLayout *layout, const char *panelId, i8 &cache, bool hidden)
{
	if (cache == (i8)hidden)
	{
		return;
	}
	cache = (i8)hidden;
	layout->SetHasClass(panelId, "hidden", hidden ? k_eHudPanelClassStatus_HasClass : k_eHudPanelClassStatus_DoesNotHaveClass);
}

static_function void SetLayoutText(CCSCustomHudLayout *layout, const char *panelId, const char *varName, std::string &cache, const char *text)
{
	if (cache == text)
	{
		return;
	}
	cache = text;
	layout->SetDialogVariableString(panelId, varName, text);
}

// One mutually exclusive class per panel (colour bands etc): removes the previous one, adds the new one.
static_function void SetLayoutClass(CCSCustomHudLayout *layout, const char *panelId, const char *&cache, const char *className)
{
	if (cache == className)
	{
		return;
	}
	if (cache)
	{
		layout->SetHasClass(panelId, cache, k_eHudPanelClassStatus_DoesNotHaveClass);
	}
	if (className)
	{
		layout->SetHasClass(panelId, className, k_eHudPanelClassStatus_HasClass);
	}
	cache = className;
}

// Position classes from positions.css: "x--neg12pct" / "y--30pct", whole percents from -50 to 50.
static_function void SetLayoutPosition(CCSCustomHudLayout *layout, const char *panelId, i32 &cache, i32 value, const char *prefix)
{
	value = (std::max)(-50, (std::min)(50, value));
	if (cache == value)
	{
		return;
	}
	char className[32];
	if (cache != INT_MIN)
	{
		V_snprintf(className, sizeof(className), "%s--%s%ipct", prefix, cache < 0 ? "neg" : "", abs(cache));
		layout->SetHasClass(panelId, className, k_eHudPanelClassStatus_DoesNotHaveClass);
	}
	V_snprintf(className, sizeof(className), "%s--%s%ipct", prefix, value < 0 ? "neg" : "", abs(value));
	layout->SetHasClass(panelId, className, k_eHudPanelClassStatus_HasClass);
	cache = value;
}

// A text element that is hidden when empty.
static_function void UpdateTextElement(CCSCustomHudLayout *layout, const char *panelId, const char *varName, SurfHUDService::LayoutElement &state,
									   const std::string &text, const char *className = NULL)
{
	SetLayoutHidden(layout, panelId, state.hidden, text.empty());
	if (text.empty())
	{
		return;
	}
	SetLayoutText(layout, panelId, varName, state.text, text.c_str());
	SetLayoutClass(layout, panelId, state.className, className);
}

bool SurfHUDService::UpdateHudLayout(SurfPlayer *source)
{
	bool created = false;
	CCSCustomHudLayout *layout = this->EnsureOwnedLayout(created);
	if (!layout)
	{
		return false;
	}
	LayoutState &st = this->layoutState;
	const bool show = this->IsShowingPanel() && this->IsUsingLayoutStyle();
	if (!show)
	{
		// hide everything, keep the cached values so switching back is cheap
		SetLayoutHidden(layout, SURFHUD_PANEL_SPLIT_LABEL, st.splitLabel.hidden, true);
		SetLayoutHidden(layout, SURFHUD_PANEL_SPLIT_TIME, st.splitTime.hidden, true);
		SetLayoutHidden(layout, SURFHUD_PANEL_SPLIT_DIFF, st.splitDiff.hidden, true);
		SetLayoutHidden(layout, SURFHUD_PANEL_SPLIT_SPEED, st.splitSpeed.hidden, true);
		SetLayoutHidden(layout, SURFHUD_PANEL_SPLIT_SPEEDDIFF, st.splitSpeedDiff.hidden, true);
		SetLayoutHidden(layout, SURFHUD_PANEL_SPEED, st.speed.hidden, true);
		SetLayoutHidden(layout, SURFHUD_PANEL_SYNC, st.sync.hidden, true);
		SetLayoutHidden(layout, SURFHUD_PANEL_TIMER, st.timer.hidden, true);
		SetLayoutHidden(layout, SURFHUD_PANEL_STAGE, st.stage.hidden, true);
		for (i32 i = 0; i <= SURF_HUD_TOP_COUNT; i++)
		{
			SetLayoutHidden(layout, SURFHUD_TOP_RANK[i], st.topRank[i].hidden, true);
			SetLayoutHidden(layout, SURFHUD_TOP_NAME[i], st.topName[i].hidden, true);
			SetLayoutHidden(layout, SURFHUD_TOP_TIME[i], st.topTime[i].hidden, true);
		}
		SetLayoutHidden(layout, SURFHUD_PANEL_TOP_HEADER, st.topHeader.hidden, true);
		SetLayoutHidden(layout, SURFHUD_PANEL_KEYS, st.keysHidden, true);
		return true;
	}

	SurfHUDService *hud = source->hudService; // the player whose run is shown (self, or the spectated player)
	const char *language = this->player->languageService->GetLanguage();
	char buf[64];

	// --- split flash (top centre) ---
	std::string splitLabel, splitTime, splitDiff, splitSpeed, splitSpeedDiff;
	const char *diffClass = NULL, *speedDiffClass = NULL;
	if (hud->flash.expiry > g_pSurfUtils->GetServerGlobals()->curtime && !hud->flash.time.empty())
	{
		splitLabel = hud->flash.label; // rank ordinal
		splitTime = hud->flash.time;
		if (!hud->flash.diff.empty())
		{
			splitDiff = hud->flash.diff;
			diffClass = hud->flash.faster ? "diff-faster" : "diff-slower";
			// the speed row only makes sense next to a comparison
			splitSpeed = hud->flash.speed;
			if (!hud->flash.speedDiff.empty())
			{
				splitSpeedDiff = hud->flash.speedDiff;
				speedDiffClass = hud->flash.speedFaster ? "sd-faster" : "sd-slower";
			}
		}
	}
	UpdateTextElement(layout, SURFHUD_PANEL_SPLIT_LABEL, "split_label", st.splitLabel, splitLabel);
	UpdateTextElement(layout, SURFHUD_PANEL_SPLIT_TIME, "split_time", st.splitTime, splitTime);
	UpdateTextElement(layout, SURFHUD_PANEL_SPLIT_DIFF, "split_diff", st.splitDiff, splitDiff, diffClass);
	UpdateTextElement(layout, SURFHUD_PANEL_SPLIT_SPEED, "split_speed", st.splitSpeed, splitSpeed);
	UpdateTextElement(layout, SURFHUD_PANEL_SPLIT_SPEEDDIFF, "split_speeddiff", st.splitSpeedDiff, splitSpeedDiff, speedDiffClass);

	// --- speed / sync / timer / stage (bottom centre) ---
	Vector velocity, baseVelocity;
	source->GetVelocity(&velocity);
	source->GetBaseVelocity(&baseVelocity);
	velocity += baseVelocity;
	f32 speed = velocity.Length2D();
	const char *speedClass = SURFHUD_SPEED_CLASSES[SURF_ARRAYSIZE(SURFHUD_SPEED_THRESHOLDS)];
	for (u32 i = 0; i < SURF_ARRAYSIZE(SURFHUD_SPEED_THRESHOLDS); i++)
	{
		if (speed < SURFHUD_SPEED_THRESHOLDS[i])
		{
			speedClass = SURFHUD_SPEED_CLASSES[i];
			break;
		}
	}
	V_snprintf(buf, sizeof(buf), "%04.0f", speed);
	UpdateTextElement(layout, SURFHUD_PANEL_SPEED, "speed", st.speed, buf, this->speedColor ? speedClass : NULL);

	std::string syncText;
	if (this->showSync && !Surf::replaysystem::IsReplayBot(source))
	{
		V_snprintf(buf, sizeof(buf), "%.1f%%", hud->GetSync());
		syncText = buf;
	}
	UpdateTextElement(layout, SURFHUD_PANEL_SYNC, "sync", st.sync, syncText, this->syncAltFont ? "sync-alt" : NULL);

	std::string timeText;
	if (Surf::replaysystem::IsReplayBot(source) || source->timerService->GetTimerRunning() || hud->ShouldShowTimerAfterStop())
	{
		timeText = hud->GetTimerText(language);
	}
	UpdateTextElement(layout, SURFHUD_PANEL_TIMER, "timer", st.timer, timeText);
	std::string stageText = hud->GetStageText(language);
	if (stageText != st.stage.text)
	{
		// maps tend to print their own "Stage X" hint in the plain centre text; wipe it for a few ticks
		this->clearCentreTicks = 16;
	}
	UpdateTextElement(layout, SURFHUD_PANEL_STAGE, "stage", st.stage, stageText);
	SetLayoutPosition(layout, SURFHUD_PANEL_SPLIT_OFFSET, st.splitX, this->splitX, "x");
	SetLayoutPosition(layout, SURFHUD_PANEL_SPLIT_OFFSET, st.splitY, this->splitY, "y");
	SetLayoutPosition(layout, SURFHUD_PANEL_BOTTOM_OFFSET, st.timerX, this->timerX, "x");
	SetLayoutPosition(layout, SURFHUD_PANEL_BOTTOM_OFFSET, st.timerY, this->timerY, "y");
	SetLayoutPosition(layout, SURFHUD_PANEL_SPEED_OFFSET, st.speedX, this->timerX + this->speedX, "x");
	SetLayoutPosition(layout, SURFHUD_PANEL_SPEED_OFFSET, st.speedY, this->timerY + this->speedY, "y");
	if (this->clearCentreTicks > 0)
	{
		this->clearCentreTicks--;
		utils::PrintCentre(this->player->GetController(), "#SFUI_EmptyString");
	}

	// --- top list (top left): best 5 of the course/mode, own PB as a sixth row when outside the top 5.
	// In stage mode on a staged map: the records of the stage the player is in, with a header saying which. ---
	std::string rankText[SURF_HUD_TOP_COUNT + 1], nameText[SURF_HUD_TOP_COUNT + 1], timeText2[SURF_HUD_TOP_COUNT + 1];
	bool selfRow[SURF_HUD_TOP_COUNT + 1] = {};
	std::string headerText;
	const SurfCourseDescriptor *course = source->timerService->GetCourse();
	if (course)
	{
		auto modeInfo = Surf::mode::GetModeInfo(source->modeService->GetModeName());
		u64 ownSteamID = this->player->GetSteamId64();
		bool inTop = false;
		i32 totalStages = SurfTimerService::GetTotalStages(course);
		if (this->stageMode && totalStages > 0)
		{
			i32 stage = (std::max)(1, (std::min)(totalStages, source->timerService->GetStage()));
			headerText = this->player->languageService->PrepareMessage("HUD Top List - Stage Header", stage);
			const std::vector<StageEntry> *top = SurfHUDService::GetStageTopList(course, modeInfo, stage);
			if (top)
			{
				for (size_t i = 0; i < top->size() && i < SURF_HUD_TOP_COUNT; i++)
				{
					const StageEntry &e = (*top)[i];
					rankText[i] = std::to_string(i + 1);
					nameText[i] = e.alias;
					timeText2[i] = utils::FormatTime(e.time).Get();
					selfRow[i] = e.steamID == ownSteamID;
					inTop |= selfRow[i];
				}
			}
			if (!inTop)
			{
				const StageBest *own = this->GetOwnStageBest(course, modeInfo, stage);
				if (own && own->rank > 0)
				{
					rankText[SURF_HUD_TOP_COUNT] = std::to_string(own->rank);
					nameText[SURF_HUD_TOP_COUNT] = this->player->GetName();
					timeText2[SURF_HUD_TOP_COUNT] = utils::FormatTime(own->time).Get();
					selfRow[SURF_HUD_TOP_COUNT] = true;
				}
			}
		}
		else
		{
			if (totalStages > 0)
			{
				headerText = this->player->languageService->PrepareMessage("HUD Top List - Map Header");
			}
			const std::vector<TopEntry> *top = SurfHUDService::GetTopList(course, modeInfo);
			if (top)
			{
				for (size_t i = 0; i < top->size() && i < SURF_HUD_TOP_COUNT; i++)
				{
					const TopEntry &e = (*top)[i];
					rankText[i] = std::to_string(i + 1);
					nameText[i] = e.alias;
					timeText2[i] = utils::FormatTime(e.time).Get();
					selfRow[i] = e.steamID == ownSteamID;
					inTop |= selfRow[i];
				}
			}
			if (!inTop)
			{
				const RankInfo *own = this->GetOwnRank(course, modeInfo);
				if (own && own->rank > 0)
				{
					rankText[SURF_HUD_TOP_COUNT] = std::to_string(own->rank);
					nameText[SURF_HUD_TOP_COUNT] = this->player->GetName();
					timeText2[SURF_HUD_TOP_COUNT] = utils::FormatTime(own->time).Get();
					selfRow[SURF_HUD_TOP_COUNT] = true;
				}
			}
		}
	}
	UpdateTextElement(layout, SURFHUD_PANEL_TOP_HEADER, "header", st.topHeader, headerText);
	for (i32 i = 0; i <= SURF_HUD_TOP_COUNT; i++)
	{
		const char *cls = selfRow[i] ? "self" : NULL;
		UpdateTextElement(layout, SURFHUD_TOP_RANK[i], "rank", st.topRank[i], rankText[i], cls);
		UpdateTextElement(layout, SURFHUD_TOP_NAME[i], "name", st.topName[i], nameText[i], cls);
		UpdateTextElement(layout, SURFHUD_TOP_TIME[i], "time", st.topTime[i], timeText2[i], cls);
	}

	// --- keys (left, moveable with !keys) ---
	SetLayoutHidden(layout, SURFHUD_PANEL_KEYS, st.keysHidden, !this->showKeys);
	if (this->showKeys)
	{
		const bool keys[6] = {source->IsButtonPressed(IN_FORWARD),   source->IsButtonPressed(IN_MOVELEFT), source->IsButtonPressed(IN_BACK),
							  source->IsButtonPressed(IN_MOVERIGHT), source->IsButtonPressed(IN_DUCK),     hud->jumpedThisTick};
		for (i32 i = 0; i < 6; i++)
		{
			if (st.keys[i] != (i8)keys[i])
			{
				st.keys[i] = (i8)keys[i];
				layout->SetHasClass(SURFHUD_KEY_PANELS[i], "pressed",
									keys[i] ? k_eHudPanelClassStatus_HasClass : k_eHudPanelClassStatus_DoesNotHaveClass);
			}
		}
		SetLayoutPosition(layout, SURFHUD_PANEL_KEYS, st.keysX, this->keysX, "x");
		SetLayoutPosition(layout, SURFHUD_PANEL_KEYS, st.keysY, this->keysY, "y");
	}
	return true;
}

void SurfHUDService::SetKeysPosition(i32 x, i32 y)
{
	this->keysX = (std::max)(-50, (std::min)(50, x));
	this->keysY = (std::max)(-50, (std::min)(50, y));
	this->player->optionService->SetPreferenceInt("hudKeysX", this->keysX);
	this->player->optionService->SetPreferenceInt("hudKeysY", this->keysY);
}

void SurfHUDService::SetSplitPosition(i32 x, i32 y)
{
	this->splitX = (std::max)(-50, (std::min)(50, x));
	this->splitY = (std::max)(-50, (std::min)(50, y));
	this->player->optionService->SetPreferenceInt("hudSplitX", this->splitX);
	this->player->optionService->SetPreferenceInt("hudSplitY", this->splitY);
}

void SurfHUDService::SetSpeedPosition(i32 x, i32 y)
{
	this->speedX = (std::max)(-50, (std::min)(50, x));
	this->speedY = (std::max)(-50, (std::min)(50, y));
	this->player->optionService->SetPreferenceInt("hudSpeedX", this->speedX);
	this->player->optionService->SetPreferenceInt("hudSpeedY", this->speedY);
}

void SurfHUDService::SetTimerPosition(i32 x, i32 y)
{
	this->timerX = (std::max)(-50, (std::min)(50, x));
	this->timerY = (std::max)(-50, (std::min)(50, y));
	this->player->optionService->SetPreferenceInt("hudTimerX", this->timerX);
	this->player->optionService->SetPreferenceInt("hudTimerY", this->timerY);
}
