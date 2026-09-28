#include "surf_vote.h"
#include "utils/simplecmds.h"
#include "utils/utils.h"
#include "utils/ctimer.h"
#include "surf/language/surf_language.h"
#include "surf/option/surf_option.h"
#include "KeyValues.h"

#include <string>
#include <vector>
#include <deque>
#include <fstream>
#include <algorithm>
#include <cmath>

#include "tier0/memdbgon.h"

// Defined in surf/misc/time_limit.cpp
extern CConVarRef<float> mp_timelimit;
extern CConVarRef<CUtlString> nextlevel;

#define VOTE_MAX_OPTIONS 6
#define VOTE_PLAYER_SLOTS (MAXPLAYERS + 2)

namespace
{
	struct VoteConfig
	{
		bool enabled = true;
		f32 rtvPercent = 0.6f;        // fraction of players needed for !vote to trigger a vote
		f32 rtvDelay = 120.0f;        // seconds after map start before !vote counts
		f32 rtvChangeDelay = 6.0f;    // seconds between the vote result and the map change
		f32 endVoteMinutes = 3.0f;    // start the end-of-map vote this many minutes before mp_timelimit
		f32 voteDuration = 25.0f;     // seconds a vote stays open
		i32 mapsInVote = 5;           // number of maps offered (nominations first, then random)
		i32 excludeRecent = 3;        // last N played maps are not offered / nominatable
		i32 extendMinutes = 15;       // "Extend map" adds this many minutes (0 disables the option)
		i32 maxExtends = 2;           // how many times a map can be extended
		f32 revoteDelay = 0.0f;       // unused for now
	};

	enum VoteKind
	{
		VOTE_NONE,
		VOTE_RTV,
		VOTE_ENDOFMAP,
		VOTE_FORCED
	};

	VoteConfig cfg;
	std::vector<std::string> mapList;
	std::deque<std::string> recentMaps;   // most recent first
	std::string currentMap;
	std::string nextMap;                  // decided by a finished vote (end-of-map)

	bool rtvVoted[VOTE_PLAYER_SLOTS];
	std::string nominations[VOTE_PLAYER_SLOTS];
	i32 ballots[VOTE_PLAYER_SLOTS];       // -1 = no vote

	VoteKind voteKind = VOTE_NONE;
	std::vector<std::string> options;     // "" entry = extend
	i32 extendOption = -1;
	f32 voteEndTime = 0.0f;
	bool endVoteDone = false;
	i32 extendsUsed = 0;
	f32 pendingChangeTime = 0.0f;         // > 0: change to nextMap at this curtime
	CTimer<> *tickTimer = nullptr;

	f32 Now()
	{
		return g_pSurfUtils->GetGlobals() ? g_pSurfUtils->GetGlobals()->curtime : 0.0f;
	}

	i32 SlotIndex(SurfPlayer *player)
	{
		i32 i = player->GetPlayerSlot().Get() + 1;
		return (i >= 0 && i < VOTE_PLAYER_SLOTS) ? i : 0;
	}

	bool Eligible(SurfPlayer *player)
	{
		return player && player->IsInGame() && !player->IsFakeClient();
	}

	void ClearPlayer(i32 i)
	{
		rtvVoted[i] = false;
		nominations[i].clear();
		ballots[i] = -1;
	}

	void ClearAllPlayers()
	{
		for (i32 i = 0; i < VOTE_PLAYER_SLOTS; i++)
		{
			ClearPlayer(i);
		}
	}

	i32 CountPlayers()
	{
		i32 n = 0;
		for (i32 i = 0; i <= MAXPLAYERS; i++)
		{
			if (Eligible(g_pSurfPlayerManager->ToPlayer((u32)i)))
			{
				n++;
			}
		}
		return n;
	}

	i32 CountRTV()
	{
		i32 n = 0;
		for (i32 i = 0; i <= MAXPLAYERS; i++)
		{
			SurfPlayer *p = g_pSurfPlayerManager->ToPlayer((u32)i);
			if (Eligible(p) && rtvVoted[SlotIndex(p)])
			{
				n++;
			}
		}
		return n;
	}

	i32 NeededRTV()
	{
		i32 players = CountPlayers();
		i32 needed = (i32)ceilf(players * cfg.rtvPercent);
		return std::max(1, needed);
	}

	f32 TimeLeft()
	{
		f32 limit = mp_timelimit.IsValidRef() ? mp_timelimit.Get() * 60.0f : 0.0f;
		if (limit <= 0.0f)
		{
			return -1.0f;
		}
		return limit - Now();
	}

	bool IsRecent(const std::string &map)
	{
		for (const auto &m : recentMaps)
		{
			if (!V_stricmp(m.c_str(), map.c_str()))
			{
				return true;
			}
		}
		return false;
	}

	bool IsCurrent(const std::string &map)
	{
		return !V_stricmp(map.c_str(), currentMap.c_str());
	}

	std::string FormatTime(f32 seconds)
	{
		if (seconds < 0)
		{
			seconds = 0;
		}
		i32 s = (i32)seconds;
		char buf[32];
		if (s >= 3600)
		{
			V_snprintf(buf, sizeof(buf), "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
		}
		else
		{
			V_snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
		}
		return buf;
	}

	void ChangeLevel(const std::string &map)
	{
		char cmd[MAX_PATH + 32];
		V_snprintf(cmd, sizeof(cmd), "changelevel %s", map.c_str());
		META_CONPRINTF("[Surf::Vote] Changing map to %s\n", map.c_str());
		interfaces::pEngine->ServerCommand(cmd);
	}

	// Build the option list: nominations first, then random maps that are neither current nor recent.
	void BuildOptions(bool allowExtend)
	{
		options.clear();
		extendOption = -1;

		for (i32 i = 0; i < VOTE_PLAYER_SLOTS && (i32)options.size() < cfg.mapsInVote; i++)
		{
			if (nominations[i].empty())
			{
				continue;
			}
			bool dup = false;
			for (const auto &o : options)
			{
				if (!V_stricmp(o.c_str(), nominations[i].c_str()))
				{
					dup = true;
					break;
				}
			}
			if (!dup)
			{
				options.push_back(nominations[i]);
			}
		}

		std::vector<std::string> pool;
		for (const auto &m : mapList)
		{
			if (IsCurrent(m) || IsRecent(m))
			{
				continue;
			}
			bool dup = false;
			for (const auto &o : options)
			{
				if (!V_stricmp(o.c_str(), m.c_str()))
				{
					dup = true;
					break;
				}
			}
			if (!dup)
			{
				pool.push_back(m);
			}
		}
		// Not enough fresh maps: allow recent ones again (still never the current map).
		if ((i32)(pool.size() + options.size()) < cfg.mapsInVote)
		{
			for (const auto &m : mapList)
			{
				if (IsCurrent(m) || !IsRecent(m))
				{
					continue;
				}
				pool.push_back(m);
			}
		}
		while ((i32)options.size() < cfg.mapsInVote && !pool.empty())
		{
			i32 k = RandomInt(0, (i32)pool.size() - 1);
			options.push_back(pool[k]);
			pool.erase(pool.begin() + k);
		}
		if (allowExtend && cfg.extendMinutes > 0 && extendsUsed < cfg.maxExtends && (i32)options.size() < VOTE_MAX_OPTIONS)
		{
			extendOption = (i32)options.size();
			options.push_back("");
		}
	}

	i32 voteCounts[VOTE_MAX_OPTIONS] = {};

	void RecountVotes()
	{
		for (i32 o = 0; o < VOTE_MAX_OPTIONS; o++)
		{
			voteCounts[o] = 0;
		}
		for (i32 i = 0; i <= MAXPLAYERS; i++)
		{
			SurfPlayer *p = g_pSurfPlayerManager->ToPlayer((u32)i);
			if (!Eligible(p))
			{
				continue;
			}
			i32 b = ballots[SlotIndex(p)];
			if (b >= 0 && b < (i32)options.size())
			{
				voteCounts[b]++;
			}
		}
	}

	void FinishVote()
	{
		RecountVotes();
		i32 *counts = voteCounts;
		i32 total = 0;
		for (i32 o = 0; o < (i32)options.size(); o++)
		{
			total += counts[o];
		}
		// Winner: most votes, ties broken randomly. No votes at all: random map (never "extend").
		i32 winner = -1;
		i32 best = 0;
		std::vector<i32> tied;
		for (i32 o = 0; o < (i32)options.size(); o++)
		{
			if (counts[o] > best)
			{
				best = counts[o];
				tied.clear();
				tied.push_back(o);
			}
			else if (counts[o] == best && best > 0)
			{
				tied.push_back(o);
			}
		}
		if (!tied.empty())
		{
			winner = tied[RandomInt(0, (i32)tied.size() - 1)];
		}
		if (winner < 0)
		{
			std::vector<i32> maps;
			for (i32 o = 0; o < (i32)options.size(); o++)
			{
				if (!options[o].empty())
				{
					maps.push_back(o);
				}
			}
			if (maps.empty())
			{
				voteKind = VOTE_NONE;
				options.clear();
				return;
			}
			winner = maps[RandomInt(0, (i32)maps.size() - 1)];
		}

		VoteKind kind = voteKind;
		voteKind = VOTE_NONE;
		for (i32 i = 0; i < VOTE_PLAYER_SLOTS; i++)
		{
			ballots[i] = -1;
			rtvVoted[i] = false;
			nominations[i].clear();
		}

		if (winner == extendOption)
		{
			extendsUsed++;
			if (mp_timelimit.IsValidRef())
			{
				mp_timelimit.Set(mp_timelimit.Get() + (f32)cfg.extendMinutes);
			}
			endVoteDone = false;   // a new end-of-map vote will run before the new limit
			nextMap.clear();
			SurfLanguageService::PrintChatAll(true, "Vote - Result Extend", cfg.extendMinutes, counts[winner], total);
			options.clear();
			return;
		}

		std::string map = options[winner];
		options.clear();
		nextMap = map;
		if (nextlevel.IsValidRef())
		{
			nextlevel.Set(map.c_str());
		}
		if (kind == VOTE_ENDOFMAP)
		{
			SurfLanguageService::PrintChatAll(true, "Vote - Result Next Map", map.c_str(), counts[winner], total);
		}
		else
		{
			SurfLanguageService::PrintChatAll(true, "Vote - Result Change Map", map.c_str(), counts[winner], total, (i32)cfg.rtvChangeDelay);
			pendingChangeTime = Now() + cfg.rtvChangeDelay;
		}
	}

	f64 Tick()
	{
		if (!cfg.enabled)
		{
			return 1.0;
		}
		f32 now = Now();

		if (pendingChangeTime > 0.0f && now >= pendingChangeTime)
		{
			pendingChangeTime = 0.0f;
			ChangeLevel(nextMap);
			return 1.0;
		}

		if (voteKind != VOTE_NONE)
		{
			if (now >= voteEndTime)
			{
				FinishVote();
			}
			else
			{
				RecountVotes();
			}
			return 1.0;
		}

		// Automatic end-of-map vote.
		f32 left = TimeLeft();
		if (!endVoteDone && pendingChangeTime <= 0.0f && left > 0.0f && left <= cfg.endVoteMinutes * 60.0f)
		{
			endVoteDone = true;
			if (nextMap.empty() && CountPlayers() > 0)
			{
				Surf::vote::StartVote(true, false);
			}
		}
		return 1.0;
	}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------

void Surf::vote::ReloadMapList()
{
	mapList.clear();
	char path[1024];
	V_snprintf(path, sizeof(path), "%s/cfg/cs2surf-maplist.txt", g_SMAPI->GetBaseDir());
	std::ifstream file(path);
	if (!file.is_open())
	{
		META_CONPRINTF("[Surf::Vote] %s not found, map voting only has the current map to offer.\n", path);
		return;
	}
	std::string line;
	while (std::getline(file, line))
	{
		// strip comments, whitespace, CR and an optional ":workshopid"
		size_t hash = line.find('#');
		if (hash != std::string::npos)
		{
			line = line.substr(0, hash);
		}
		size_t colon = line.find(':');
		if (colon != std::string::npos)
		{
			line = line.substr(0, colon);
		}
		line.erase(std::remove_if(line.begin(), line.end(), [](unsigned char c) { return c == '\r' || c == '\n' || c == '\t' || c == ' ' || c == '"'; }),
				   line.end());
		if (line.empty())
		{
			continue;
		}
		bool dup = false;
		for (const auto &m : mapList)
		{
			if (!V_stricmp(m.c_str(), line.c_str()))
			{
				dup = true;
				break;
			}
		}
		if (!dup)
		{
			mapList.push_back(line);
		}
	}
	META_CONPRINTF("[Surf::Vote] Loaded %d maps from cfg/cs2surf-maplist.txt\n", (i32)mapList.size());
}

static_function void LoadConfig()
{
	cfg = VoteConfig();
	KeyValues *kv = SurfOptionService::GetOptionKV("vote");
	if (!kv)
	{
		return;
	}
	cfg.enabled = kv->GetBool("enabled", cfg.enabled);
	cfg.rtvPercent = kv->GetFloat("rtvPercent", cfg.rtvPercent);
	cfg.rtvDelay = kv->GetFloat("rtvDelay", cfg.rtvDelay);
	cfg.rtvChangeDelay = kv->GetFloat("rtvChangeDelay", cfg.rtvChangeDelay);
	cfg.endVoteMinutes = kv->GetFloat("endVoteMinutes", cfg.endVoteMinutes);
	cfg.voteDuration = kv->GetFloat("voteDuration", cfg.voteDuration);
	cfg.mapsInVote = kv->GetInt("mapsInVote", cfg.mapsInVote);
	cfg.excludeRecent = kv->GetInt("excludeRecent", cfg.excludeRecent);
	cfg.extendMinutes = kv->GetInt("extendMinutes", cfg.extendMinutes);
	cfg.maxExtends = kv->GetInt("maxExtends", cfg.maxExtends);
	cfg.rtvPercent = std::min(std::max(cfg.rtvPercent, 0.05f), 1.0f);
	cfg.mapsInVote = std::min(std::max(cfg.mapsInVote, 2), VOTE_MAX_OPTIONS - 1);
	cfg.voteDuration = std::max(cfg.voteDuration, 5.0f);
	cfg.excludeRecent = std::max(cfg.excludeRecent, 0);
}

void Surf::vote::Init()
{
	ClearAllPlayers();
	LoadConfig();
	ReloadMapList();
	// One persistent, real-time timer for the whole plugin lifetime (curtime restarts on every map).
	tickTimer = StartTimer(Tick, 1.0, true, true);
}

void Surf::vote::OnActivateServer()
{
	LoadConfig();
	if (mapList.empty())
	{
		ReloadMapList();
	}
	const CGlobalVars *globals = g_pSurfUtils->GetGlobals();
	currentMap = (globals && globals->mapname.ToCStr()) ? globals->mapname.ToCStr() : "";

	// remember what was played so it is not offered again right away
	if (!currentMap.empty())
	{
		recentMaps.erase(std::remove_if(recentMaps.begin(), recentMaps.end(), [](const std::string &m) { return IsCurrent(m); }), recentMaps.end());
		recentMaps.push_front(currentMap);
		while ((i32)recentMaps.size() > cfg.excludeRecent)
		{
			recentMaps.pop_back();
		}
	}

	ClearAllPlayers();
	voteKind = VOTE_NONE;
	options.clear();
	extendOption = -1;
	nextMap.clear();
	endVoteDone = false;
	extendsUsed = 0;
	pendingChangeTime = 0.0f;

	if (tickTimer)
	{
		tickTimer->lastExecute = -1;
	}
}

void Surf::vote::OnClientDisconnect(CPlayerSlot slot)
{
	i32 i = slot.Get() + 1;
	if (i >= 0 && i < VOTE_PLAYER_SLOTS)
	{
		ClearPlayer(i);
	}
}

bool Surf::vote::IsVoteRunning()
{
	return voteKind != VOTE_NONE;
}

// Drawn by SurfHUDService::DrawPanels (the HUD owns the HTML centre panel and redraws it every tick).
std::string Surf::vote::GetPanelHTML(SurfPlayer *target)
{
	if (voteKind == VOTE_NONE || !target)
	{
		return "";
	}
	f32 left = voteEndTime - Now();
	std::string title = target->languageService->PrepareMessage(voteKind == VOTE_ENDOFMAP ? "Vote - Panel Title (End Of Map)" : "Vote - Panel Title");
	std::string extendLabel = target->languageService->PrepareMessage("Vote - Extend Option", cfg.extendMinutes);
	std::string html = "<font color='#7fff00'><b>" + title + "</b></font> <font color='#aaaaaa'>" + FormatTime(left) + "</font><br>";
	i32 mine = ballots[SlotIndex(target)];
	for (i32 o = 0; o < (i32)options.size(); o++)
	{
		const char *label = options[o].empty() ? extendLabel.c_str() : options[o].c_str();
		char line[320];
		V_snprintf(line, sizeof(line), "%s!%d  %s <font color='#aaaaaa'>[%d]</font>%s<br>", (mine == o) ? "<font color='#7fff00'>" : "", o + 1,
				   label, voteCounts[o], (mine == o) ? "</font>" : "");
		html += line;
	}
	// the HUD prints this through a printf-style call
	std::string safe;
	for (char c : html)
	{
		if (c == '%') safe += "%%";
		else safe += c;
	}
	return safe;
}

bool Surf::vote::StartVote(bool endOfMap, bool forced)
{
	if (voteKind != VOTE_NONE || pendingChangeTime > 0.0f)
	{
		return false;
	}
	BuildOptions(endOfMap);
	if (options.empty() || (options.size() == 1 && extendOption == 0))
	{
		META_CONPRINTF("[Surf::Vote] No maps available for a vote (check cfg/cs2surf-maplist.txt)\n");
		if (!endOfMap)
		{
			SurfLanguageService::PrintChatAll(true, "Vote - No Maps");
		}
		return false;
	}
	for (i32 i = 0; i < VOTE_PLAYER_SLOTS; i++)
	{
		ballots[i] = -1;
	}
	voteKind = endOfMap ? VOTE_ENDOFMAP : (forced ? VOTE_FORCED : VOTE_RTV);
	voteEndTime = Now() + cfg.voteDuration;
	RecountVotes();
	SurfLanguageService::PrintChatAll(true, endOfMap ? "Vote - Started (End Of Map)" : "Vote - Started", (i32)cfg.voteDuration);
	return true;
}

void Surf::vote::RequestRTV(SurfPlayer *player)
{
	if (!cfg.enabled)
	{
		player->languageService->PrintChat(true, false, "Vote - Disabled");
		return;
	}
	if (voteKind != VOTE_NONE)
	{
		player->languageService->PrintChat(true, false, "Vote - Already Running");
		return;
	}
	if (pendingChangeTime > 0.0f)
	{
		player->languageService->PrintChat(true, false, "Vote - Change Pending", nextMap.c_str());
		return;
	}
	f32 now = Now();
	if (now < cfg.rtvDelay)
	{
		player->languageService->PrintChat(true, false, "Vote - Too Early", FormatTime(cfg.rtvDelay - now).c_str());
		return;
	}
	i32 idx = SlotIndex(player);
	if (rtvVoted[idx])
	{
		player->languageService->PrintChat(true, false, "Vote - Already Voted", CountRTV(), NeededRTV());
		return;
	}
	rtvVoted[idx] = true;
	i32 have = CountRTV();
	i32 need = NeededRTV();
	SurfLanguageService::PrintChatAll(true, "Vote - Player Wants Vote", player->GetName(), have, need);
	if (have >= need)
	{
		StartVote(false, false);
	}
}

void Surf::vote::UnRTV(SurfPlayer *player)
{
	i32 idx = SlotIndex(player);
	if (!rtvVoted[idx])
	{
		return;
	}
	rtvVoted[idx] = false;
	SurfLanguageService::PrintChatAll(true, "Vote - Player Withdrew", player->GetName(), CountRTV(), NeededRTV());
}

void Surf::vote::Nominate(SurfPlayer *player, const char *mapNamePart)
{
	if (!cfg.enabled)
	{
		player->languageService->PrintChat(true, false, "Vote - Disabled");
		return;
	}
	if (!mapNamePart || !mapNamePart[0])
	{
		player->languageService->PrintChat(true, false, "Vote - Nominate Usage");
		PrintMapList(player);
		return;
	}
	// exact match first, then unique substring match
	std::string chosen;
	std::vector<std::string> matches;
	for (const auto &m : mapList)
	{
		if (!V_stricmp(m.c_str(), mapNamePart))
		{
			chosen = m;
			break;
		}
		if (V_stristr(m.c_str(), mapNamePart))
		{
			matches.push_back(m);
		}
	}
	if (chosen.empty())
	{
		if (matches.size() == 1)
		{
			chosen = matches[0];
		}
		else if (matches.empty())
		{
			player->languageService->PrintChat(true, false, "Vote - Nominate Not Found", mapNamePart);
			return;
		}
		else
		{
			std::string list;
			for (size_t i = 0; i < matches.size() && i < 8; i++)
			{
				list += (i ? ", " : "") + matches[i];
			}
			player->languageService->PrintChat(true, false, "Vote - Nominate Ambiguous", list.c_str());
			return;
		}
	}
	if (IsCurrent(chosen))
	{
		player->languageService->PrintChat(true, false, "Vote - Nominate Current Map");
		return;
	}
	if (IsRecent(chosen))
	{
		player->languageService->PrintChat(true, false, "Vote - Nominate Recent Map", chosen.c_str());
		return;
	}
	i32 idx = SlotIndex(player);
	if (!V_stricmp(nominations[idx].c_str(), chosen.c_str()))
	{
		player->languageService->PrintChat(true, false, "Vote - Nominate Already", chosen.c_str());
		return;
	}
	nominations[idx] = chosen;
	SurfLanguageService::PrintChatAll(true, "Vote - Player Nominated", player->GetName(), chosen.c_str());
}

void Surf::vote::CastVote(SurfPlayer *player, i32 option)
{
	if (voteKind == VOTE_NONE)
	{
		player->languageService->PrintChat(true, false, "Vote - Not Running");
		return;
	}
	if (option < 0 || option >= (i32)options.size())
	{
		player->languageService->PrintChat(true, false, "Vote - Invalid Option", (i32)options.size());
		return;
	}
	ballots[SlotIndex(player)] = option;
	if (options[option].empty())
	{
		player->languageService->PrintChat(true, false, "Vote - You Voted Extend");
	}
	else
	{
		player->languageService->PrintChat(true, false, "Vote - You Voted", options[option].c_str());
	}
	RecountVotes();
}

void Surf::vote::PrintNextMap(SurfPlayer *player)
{
	if (!nextMap.empty())
	{
		player->languageService->PrintChat(true, false, "Vote - Next Map", nextMap.c_str());
		return;
	}
	f32 left = TimeLeft();
	if (left > 0.0f && !endVoteDone)
	{
		f32 untilVote = left - cfg.endVoteMinutes * 60.0f;
		player->languageService->PrintChat(true, false, "Vote - Next Map Undecided", FormatTime(std::max(untilVote, 0.0f)).c_str());
	}
	else
	{
		player->languageService->PrintChat(true, false, "Vote - Next Map Unknown");
	}
}

void Surf::vote::PrintTimeLeft(SurfPlayer *player)
{
	f32 left = TimeLeft();
	if (left < 0.0f)
	{
		player->languageService->PrintChat(true, false, "Vote - No Time Limit");
		return;
	}
	player->languageService->PrintChat(true, false, "Vote - Time Left", FormatTime(left).c_str());
}

void Surf::vote::PrintMapList(SurfPlayer *player)
{
	player->languageService->PrintChat(true, false, "Vote - Map List Hint", (i32)mapList.size());
	player->PrintConsole(false, false, "---- Map list (%d) ----", (i32)mapList.size());
	for (const auto &m : mapList)
	{
		player->PrintConsole(false, false, "%s%s", m.c_str(), IsCurrent(m) ? "  (current)" : (IsRecent(m) ? "  (recently played)" : ""));
	}
}

// ------------------------------------------------------------------------------------------------------- commands

SCMD(surf_vote, SCFL_MAP)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	Surf::vote::RequestRTV(player);
	return true;
}

SCMD_LINK(surf_rtv, surf_vote);
SCMD_LINK(surf_rockthevote, surf_vote);

SCMD(surf_unvote, SCFL_MAP)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	Surf::vote::UnRTV(player);
	return true;
}

SCMD_LINK(surf_unrtv, surf_unvote);

SCMD(surf_nominate, SCFL_MAP)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	Surf::vote::Nominate(player, args->ArgC() > 1 ? args->Arg(1) : "");
	return true;
}

SCMD_LINK(surf_nom, surf_nominate);

SCMD(surf_nextmap, SCFL_MAP)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	Surf::vote::PrintNextMap(player);
	return true;
}

SCMD(surf_timeleft, SCFL_MAP)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	Surf::vote::PrintTimeLeft(player);
	return true;
}

SCMD(surf_maps, SCFL_MAP)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	Surf::vote::PrintMapList(player);
	return true;
}

SCMD_LINK(surf_maplist, surf_maps);

// !1 .. !6 while a vote is open
#define VOTE_OPTION_CMD(n) \
	SCMD(surf_##n, SCFL_HIDDEN) \
	{ \
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller); \
		if (!Surf::vote::IsVoteRunning()) \
		{ \
			return false; \
		} \
		Surf::vote::CastVote(player, n - 1); \
		return true; \
	}

VOTE_OPTION_CMD(1)
VOTE_OPTION_CMD(2)
VOTE_OPTION_CMD(3)
VOTE_OPTION_CMD(4)
VOTE_OPTION_CMD(5)
VOTE_OPTION_CMD(6)

// server console / rcon
CON_COMMAND_F(surf_vote_start, "Start a map vote now; the winner is loaded right after the vote", FCVAR_NONE)
{
	if (!Surf::vote::StartVote(false, true))
	{
		META_CONPRINT("[Surf::Vote] Could not start a vote (one is already running, or no maps).\n");
	}
}

CON_COMMAND_F(surf_vote_reload_maplist, "Reload cfg/cs2surf-maplist.txt", FCVAR_NONE)
{
	Surf::vote::ReloadMapList();
}
