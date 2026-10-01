/*
 * Top list for the layout HUD: the five best times of the current course and mode, plus the viewer's own PB and rank
 * when they are not in the top five. Cached per (mode, course), refreshed whenever the server record cache refreshes
 * (map start and after every submitted run); the viewer's rank is cached per player on top of that.
 */
#include "surf/surf.h"
#include "surf_hud.h"
#include "utils/utils.h"
#include "surf/db/surf_db.h"
#include "surf/timer/surf_timer.h"
#include "surf/mode/surf_mode.h"
#include "vendor/sql_mm/src/public/sql_mm.h"
#include "tier1/keyvalues3.h"

#include "tier0/memdbgon.h"

// Same as the !top query, plus the run metadata (checkpoint / stage splits) so splits can be ranked against the top runs.
static_global constexpr char sql_hud_top[] = R"(
    SELECT t.ID, t.SteamID64, p.Alias, t.RunTime AS PBTime, t.Metadata
        FROM Times t
        INNER JOIN MapCourses mc ON mc.ID = t.MapCourseID
        INNER JOIN Maps ON Maps.ID = mc.MapID
        INNER JOIN Players p ON p.SteamID64=t.SteamID64
        LEFT OUTER JOIN Times t2 ON t2.SteamID64=t.SteamID64
        AND t2.MapCourseID=t.MapCourseID AND t2.ModeID=t.ModeID
        AND t2.StyleIDFlags=t.StyleIDFlags
        AND (t2.RunTime < t.RunTime OR (t2.RunTime = t.RunTime AND t2.ID < t.ID))
        WHERE t2.ID IS NULL AND p.Cheater=0 AND Maps.Name='%s' AND mc.Name='%s' AND t.ModeID=%d AND t.StyleIDFlags=0
        ORDER BY PBTime ASC
        LIMIT %d
)";

static_function void ReadSplits(KeyValues3 &kv, const char *name, std::vector<f64> &out)
{
	KeyValues3 *data = kv.FindMember(name);
	if (!data || data->GetType() != KV3_TYPE_ARRAY)
	{
		return;
	}
	for (i32 i = 0; i < data->GetArrayElementCount(); i++)
	{
		KeyValues3 *element = data->GetArrayElement(i);
		out.push_back(element ? element->GetDouble(-1.0) : -1.0);
	}
}

struct TopList
{
	bool pending = false;
	bool valid = false;
	std::vector<SurfHUDService::TopEntry> entries;
};

static_global std::unordered_map<PBDataKey, TopList> topCache;
static_global u32 topCacheGeneration = 1;

// standalone stage records, keyed by (mode, course) and stage
struct StageTopList
{
	bool pending = false;
	bool valid = false;
	std::vector<SurfHUDService::StageEntry> entries;
};

static_function u64 StageKey(PBDataKey key, i32 stage)
{
	return ((u64)key << 8) | (u64)(stage & 0xff);
}

static_global std::unordered_map<u64, StageTopList> stageTopCache;
static_global u32 stageCacheGeneration = 1;

void SurfHUDService::InvalidateTopCache()
{
	topCache.clear();
	topCacheGeneration++;
	stageTopCache.clear();
	stageCacheGeneration++;
}

// A stage record was saved: drop that stage's list for every mode (cheap, the lists are tiny). Lists of other stages,
// including ones still loading, are left alone; a query in flight for the dropped stage finds no entry and is discarded.
void SurfHUDService::InvalidateStageCache(u32 courseGUID, i32 stage)
{
	for (auto it = stageTopCache.begin(); it != stageTopCache.end();)
	{
		if ((i32)(it->first & 0xff) == stage)
		{
			it = stageTopCache.erase(it);
		}
		else
		{
			++it;
		}
	}
}

const std::vector<SurfHUDService::TopEntry> *SurfHUDService::GetTopList(const SurfCourseDescriptor *course,
																		const SurfModeManager::ModePluginInfo &modeInfo)
{
	PBDataKey key = ToPBDataKey(modeInfo.id, course->guid);
	TopList &list = topCache[key];
	if (list.valid)
	{
		return &list.entries;
	}
	if (list.pending)
	{
		return NULL;
	}
	if (modeInfo.databaseID < 0 || !SurfDatabaseService::IsReady())
	{
		return NULL;
	}
	list.pending = true;
	u32 generation = topCacheGeneration;
	auto onSuccess = [key, generation](std::vector<ISQLQuery *> queries)
	{
		if (generation != topCacheGeneration)
		{
			return; // the cache was cleared while this was in flight
		}
		TopList &l = topCache[key];
		l.entries.clear();
		ISQLResult *result = queries[0]->GetResultSet();
		if (result && result->GetRowCount() > 0)
		{
			while (result->FetchRow())
			{
				SurfHUDService::TopEntry e;
				e.steamID = (u64)result->GetInt64(1);
				e.alias = result->GetString(2);
				e.time = result->GetFloat(3);
				const char *metadata = result->GetString(4);
				if (metadata && metadata[0])
				{
					KeyValues3 kv(KV3_TYPEEX_TABLE, KV3_SUBTYPE_UNSPECIFIED);
					CUtlString error;
					LoadKV3FromJSON(&kv, &error, metadata, "");
					if (error.IsEmpty())
					{
						ReadSplits(kv, "cpZoneTimes", e.cpTimes);
						ReadSplits(kv, "stageZoneTimes", e.stageTimes);
						ReadSplits(kv, "stageTouchTimes", e.stageTouchTimes);
					}
				}
				l.entries.push_back(e);
			}
		}
		l.pending = false;
		l.valid = true;
	};
	auto onFailure = [key, generation](std::string, int)
	{
		if (generation == topCacheGeneration)
		{
			topCache[key].pending = false;
		}
	};
	std::string mapName = SurfDatabaseService::GetDatabaseConnection()->Escape(g_pSurfUtils->GetCurrentMapName().Get());
	std::string courseName = SurfDatabaseService::GetDatabaseConnection()->Escape(course->name);
	char query[2048];
	V_snprintf(query, sizeof(query), sql_hud_top, mapName.c_str(), courseName.c_str(), modeInfo.databaseID, SURF_HUD_TOP_RANKED);
	Transaction txn;
	txn.queries.push_back(query);
	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, onFailure);
	return NULL;
}

// Where a split would place the run among the cached top runs (own PB included): 1 = faster than every top run at that
// zone, 0 = nothing to rank against. Runs slower than all cached top runs rank SURF_HUD_TOP_RANKED + 1.
// stage = true ranks the cumulative time at a stage clear (stageTouchTimes), which older runs do not have.
i32 SurfHUDService::GetSplitRank(const SurfCourseDescriptor *course, const SurfModeManager::ModePluginInfo &modeInfo, bool stage, i32 index, f64 time)
{
	const std::vector<TopEntry> *top = SurfHUDService::GetTopList(course, modeInfo);
	if (!top || top->empty() || index < 0)
	{
		return 0;
	}
	i32 rank = 1;
	bool any = false;
	for (const TopEntry &e : *top)
	{
		const std::vector<f64> &splits = stage ? e.stageTouchTimes : e.cpTimes;
		if ((size_t)index >= splits.size() || splits[index] <= 0.0)
		{
			continue;
		}
		any = true;
		if (splits[index] < time)
		{
			rank++;
		}
	}
	return any ? rank : 0;
}

// Where a finished run would place among the cached top runs (own PB included). 0 = nothing to rank against.
i32 SurfHUDService::GetFinishRank(const SurfCourseDescriptor *course, const SurfModeManager::ModePluginInfo &modeInfo, f64 time)
{
	const std::vector<TopEntry> *top = SurfHUDService::GetTopList(course, modeInfo);
	if (!top || top->empty())
	{
		return 0;
	}
	i32 rank = 1;
	for (const TopEntry &e : *top)
	{
		if (e.time < time)
		{
			rank++;
		}
	}
	return rank;
}

std::string SurfHUDService::FormatSplitRank(i32 rank)
{
	if (rank <= 0)
	{
		return SurfHUDService::FormatOrdinal(1); // about to set the first time
	}
	if (rank > SURF_HUD_TOP_RANKED)
	{
		return SurfHUDService::FormatOrdinal(SURF_HUD_TOP_RANKED) + "+";
	}
	return SurfHUDService::FormatOrdinal(rank);
}

std::string SurfHUDService::FormatOrdinal(i32 rank)
{
	const char *suffix = "th";
	i32 mod100 = rank % 100;
	if (mod100 < 11 || mod100 > 13)
	{
		switch (rank % 10)
		{
			case 1:
				suffix = "st";
				break;
			case 2:
				suffix = "nd";
				break;
			case 3:
				suffix = "rd";
				break;
		}
	}
	char buf[16];
	V_snprintf(buf, sizeof(buf), "%d%s", rank, suffix);
	return buf;
}

const SurfHUDService::RankInfo *SurfHUDService::GetOwnRank(const SurfCourseDescriptor *course, const SurfModeManager::ModePluginInfo &modeInfo)
{
	if (this->rankCacheGeneration != topCacheGeneration)
	{
		this->rankCache.clear();
		this->rankCacheGeneration = topCacheGeneration;
	}
	PBDataKey key = ToPBDataKey(modeInfo.id, course->guid);
	RankInfo &info = this->rankCache[key];
	if (info.valid)
	{
		return &info;
	}
	if (info.pending)
	{
		return NULL;
	}
	u64 steamID = this->player->GetSteamId64();
	if (modeInfo.databaseID < 0 || !SurfDatabaseService::IsReady() || steamID == 0)
	{
		return NULL;
	}
	info.pending = true;
	u32 generation = topCacheGeneration;
	CPlayerSlot slot = this->player->GetPlayerSlot();
	auto onSuccess = [key, generation, slot, steamID](std::vector<ISQLQuery *> queries)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(slot);
		if (!player || !player->hudService || player->GetSteamId64() != steamID || generation != topCacheGeneration)
		{
			return;
		}
		RankInfo &r = player->hudService->rankCache[key];
		r.pending = false;
		r.valid = true;
		r.time = 0.0;
		r.rank = 0;
		ISQLResult *pb = queries[0]->GetResultSet();
		if (pb && pb->GetRowCount() > 0 && pb->FetchRow())
		{
			r.time = pb->GetFloat(0);
			ISQLResult *rank = queries[1]->GetResultSet();
			if (rank && rank->GetRowCount() > 0 && rank->FetchRow())
			{
				r.rank = rank->GetInt(0);
			}
		}
	};
	auto onFailure = [key, generation, slot](std::string, int)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(slot);
		if (player && player->hudService && generation == topCacheGeneration)
		{
			player->hudService->rankCache[key].pending = false;
		}
	};
	SurfDatabaseService::QueryPB(steamID, g_pSurfUtils->GetCurrentMapName(), course->name, modeInfo.databaseID, onSuccess, onFailure);
	return NULL;
}

const std::vector<SurfHUDService::StageEntry> *SurfHUDService::GetStageTopList(const SurfCourseDescriptor *course,
																			   const SurfModeManager::ModePluginInfo &modeInfo, i32 stage)
{
	if (!course || stage < 1)
	{
		return NULL;
	}
	u64 key = StageKey(ToPBDataKey(modeInfo.id, course->guid), stage);
	StageTopList &list = stageTopCache[key];
	if (list.valid)
	{
		return &list.entries;
	}
	if (list.pending)
	{
		return NULL;
	}
	if (modeInfo.databaseID < 0 || !SurfDatabaseService::IsReady())
	{
		return NULL;
	}
	list.pending = true;
	u32 generation = stageCacheGeneration;
	auto onSuccess = [key, generation](std::vector<ISQLQuery *> queries)
	{
		auto it = stageTopCache.find(key);
		if (generation != stageCacheGeneration || it == stageTopCache.end() || !it->second.pending)
		{
			return; // the whole cache was cleared, or this stage was invalidated while loading: the next lookup refetches
		}
		StageTopList &l = it->second;
		l.entries.clear();
		ISQLResult *result = queries[0]->GetResultSet();
		if (result && result->GetRowCount() > 0)
		{
			while (result->FetchRow())
			{
				SurfHUDService::StageEntry e;
				e.steamID = (u64)result->GetInt64(0);
				e.alias = result->GetString(1);
				e.time = result->GetFloat(2);
				e.speed = result->GetFloat(3);
				l.entries.push_back(e);
			}
		}
		l.pending = false;
		l.valid = true;
	};
	auto onFailure = [key, generation](std::string, int)
	{
		if (generation == stageCacheGeneration)
		{
			stageTopCache.erase(key);
		}
	};
	SurfDatabaseService::QueryStageTop(g_pSurfUtils->GetCurrentMapName(), course->name, modeInfo.databaseID, stage, SURF_HUD_TOP_RANKED, onSuccess,
									   onFailure);
	return NULL;
}

// Where a stage segment would place among the cached stage records (own best included). 0 = nothing to rank against.
i32 SurfHUDService::GetStageRank(const SurfCourseDescriptor *course, const SurfModeManager::ModePluginInfo &modeInfo, i32 stage, f64 time)
{
	const std::vector<StageEntry> *top = SurfHUDService::GetStageTopList(course, modeInfo, stage);
	if (!top || top->empty())
	{
		return 0;
	}
	i32 rank = 1;
	for (const StageEntry &e : *top)
	{
		if (e.time < time)
		{
			rank++;
		}
	}
	return rank;
}

void SurfHUDService::InvalidateOwnStageCache()
{
	this->ownStageCache.clear();
	this->ownStageCacheGeneration++;
}

const SurfHUDService::StageBest *SurfHUDService::GetOwnStageBest(const SurfCourseDescriptor *course, const SurfModeManager::ModePluginInfo &modeInfo,
																 i32 stage)
{
	if (!course || stage < 1)
	{
		return NULL;
	}
	u64 key = StageKey(ToPBDataKey(modeInfo.id, course->guid), stage);
	StageBest &best = this->ownStageCache[key];
	if (best.valid)
	{
		return &best;
	}
	if (best.pending)
	{
		return NULL;
	}
	u64 steamID = this->player->GetSteamId64();
	if (modeInfo.databaseID < 0 || !SurfDatabaseService::IsReady() || steamID == 0)
	{
		return NULL;
	}
	best.pending = true;
	u32 generation = this->ownStageCacheGeneration;
	CPlayerSlot slot = this->player->GetPlayerSlot();
	auto onSuccess = [key, generation, slot, steamID](std::vector<ISQLQuery *> queries)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(slot);
		if (!player || !player->hudService || player->GetSteamId64() != steamID || generation != player->hudService->ownStageCacheGeneration)
		{
			return;
		}
		StageBest &b = player->hudService->ownStageCache[key];
		b.pending = false;
		b.valid = true;
		b.time = 0.0;
		b.speed = -1.0;
		b.rank = 0;
		ISQLResult *result = queries[0]->GetResultSet();
		if (result && result->GetRowCount() > 0 && result->FetchRow())
		{
			b.time = result->GetFloat(0);
			b.speed = result->GetFloat(1);
			b.rank = result->GetInt(2);
		}
	};
	auto onFailure = [key, generation, slot](std::string, int)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(slot);
		if (player && player->hudService && generation == player->hudService->ownStageCacheGeneration)
		{
			player->hudService->ownStageCache[key].pending = false;
		}
	};
	SurfDatabaseService::QueryStagePB(steamID, g_pSurfUtils->GetCurrentMapName(), course->name, modeInfo.databaseID, stage, onSuccess, onFailure);
	return NULL;
}
