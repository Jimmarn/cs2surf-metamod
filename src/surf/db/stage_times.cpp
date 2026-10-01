/*
 * Stage records: best segment time per player, course, mode and stage, from any attempt.
 */
#include "surf_db.h"
#include "vendor/sql_mm/src/public/sql_mm.h"
#include "queries/stage_times.h"

void SurfDatabaseService::SaveStageTime(u64 steamID, u32 courseID, i32 modeID, u64 styleIDs, i32 stage, f64 time, f32 speed,
										TransactionSuccessCallbackFunc onSuccess, TransactionFailureCallbackFunc onFailure)
{
	if (!SurfDatabaseService::IsReady())
	{
		return;
	}
	char query[2048];
	Transaction txn;
	// improve the existing row when this is faster...
	V_snprintf(query, sizeof(query), sql_stagetimes_update, time, speed, steamID, courseID, modeID, styleIDs, stage, time);
	txn.queries.push_back(query);
	// ...or create it when the player has no time for this stage yet
	V_snprintf(query, sizeof(query), sql_stagetimes_insert, steamID, courseID, modeID, styleIDs, stage, time, speed, steamID, steamID, courseID,
			   modeID, styleIDs, stage);
	txn.queries.push_back(query);
	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, onFailure);
}

void SurfDatabaseService::QueryStageTop(CUtlString mapName, CUtlString courseName, u32 modeID, i32 stage, u32 count,
										TransactionSuccessCallbackFunc onSuccess, TransactionFailureCallbackFunc onFailure)
{
	std::string cleanedMapName = SurfDatabaseService::GetDatabaseConnection()->Escape(mapName.Get());
	std::string cleanedCourseName = SurfDatabaseService::GetDatabaseConnection()->Escape(courseName.Get());
	char query[2048];
	Transaction txn;
	V_snprintf(query, sizeof(query), sql_stagetimes_top, cleanedMapName.c_str(), cleanedCourseName.c_str(), modeID, stage, count);
	txn.queries.push_back(query);
	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, onFailure);
}

void SurfDatabaseService::QueryStagePB(u64 steamID64, CUtlString mapName, CUtlString courseName, u32 modeID, i32 stage,
									   TransactionSuccessCallbackFunc onSuccess, TransactionFailureCallbackFunc onFailure)
{
	std::string cleanedMapName = SurfDatabaseService::GetDatabaseConnection()->Escape(mapName.Get());
	std::string cleanedCourseName = SurfDatabaseService::GetDatabaseConnection()->Escape(courseName.Get());
	char query[2048];
	Transaction txn;
	V_snprintf(query, sizeof(query), sql_stagetimes_own, steamID64, cleanedMapName.c_str(), cleanedCourseName.c_str(), modeID, stage);
	txn.queries.push_back(query);
	SurfDatabaseService::GetDatabaseConnection()->ExecuteTransaction(txn, onSuccess, onFailure);
}
