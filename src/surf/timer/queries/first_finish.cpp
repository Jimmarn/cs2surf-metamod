/*
 * !first [map]: who finished a map first. The order of each player's earliest recorded finish (any mode, no styles),
 * for the "Main" course of the current map or of the map named as the argument.
 */
#include "surf/surf.h"
#include "surf/db/surf_db.h"
#include "surf/language/surf_language.h"
#include "utils/simplecmds.h"
#include "utils/utils.h"
#include "vendor/sql_mm/src/public/sql_mm.h"

#include "tier0/memdbgon.h"

static_global constexpr char sql_first_finishes[] = R"(
    SELECT p.Alias, MIN(t.Created) AS FirstFinish
        FROM Times t
        INNER JOIN MapCourses mc ON mc.ID = t.MapCourseID
        INNER JOIN Maps ON Maps.ID = mc.MapID
        INNER JOIN Players p ON p.SteamID64 = t.SteamID64
        WHERE p.Cheater=0 AND Maps.Name='%s' AND mc.Name='%s' AND t.StyleIDFlags=0
        GROUP BY t.SteamID64, p.Alias
        ORDER BY FirstFinish ASC
        LIMIT %d
)";

#define SURF_FIRST_FINISH_COUNT 10

SCMD(surf_first, SCFL_RECORD)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	if (!SurfDatabaseService::IsReady())
	{
		player->languageService->PrintChat(true, false, "First Finish - No Database");
		return true;
	}
	std::string mapName = args->ArgC() >= 2 ? args->Arg(1) : g_pSurfUtils->GetCurrentMapName().Get();
	std::string cleanMap = SurfDatabaseService::GetDatabaseConnection()->Escape(mapName.c_str());
	char query[1024];
	V_snprintf(query, sizeof(query), sql_first_finishes, cleanMap.c_str(), "Main", SURF_FIRST_FINISH_COUNT);

	CPlayerSlot slot = player->GetPlayerSlot();
	auto onSuccess = [slot, mapName](std::vector<ISQLQuery *> queries)
	{
		SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(slot);
		if (!player)
		{
			return;
		}
		ISQLResult *result = queries[0]->GetResultSet();
		if (!result || result->GetRowCount() == 0)
		{
			player->languageService->PrintChat(true, false, "First Finish - None", mapName.c_str());
			return;
		}
		player->languageService->PrintChat(true, false, "First Finish - Header", mapName.c_str());
		i32 place = 1;
		while (result->FetchRow())
		{
			const char *alias = result->GetString(0);
			const char *created = result->GetString(1);
			// "YYYY-MM-DD HH:MM:SS" (or the epoch of an imported run): keep the date only
			char date[16] = "";
			if (created)
			{
				V_strncpy(date, created, sizeof(date));
				date[10] = '\0';
			}
			player->languageService->PrintChat(false, false, "First Finish - Entry", place++, alias ? alias : "?", date);
		}
	};
	Transaction txn;
	txn.queries.push_back(query);
	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, SurfDatabaseService::OnGenericTxnFailure);
	return true;
}
