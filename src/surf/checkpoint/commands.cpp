#include "surf_checkpoint.h"
#include "utils/simplecmds.h"

SCMD(surf_checkpoint, SCFL_CHECKPOINT)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->checkpointService->SetCheckpoint();
	return true;
}

SCMD_LINK(surf_cp, surf_checkpoint);
SCMD_LINK(surf_saveloc, surf_checkpoint);

SCMD(surf_teleport, SCFL_CHECKPOINT)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->checkpointService->TpToCheckpoint();
	return true;
}

SCMD_LINK(surf_tp, surf_teleport);
SCMD_LINK(surf_loadloc, surf_teleport);

SCMD(surf_undo, SCFL_CHECKPOINT)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->checkpointService->UndoTeleport();
	return true;
}

SCMD(surf_prevcp, SCFL_CHECKPOINT)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->checkpointService->TpToPrevCp();
	return true;
}

SCMD_LINK(surf_pcp, surf_prevcp);
SCMD_LINK(surf_prevloc, surf_prevcp);

SCMD(surf_nextcp, SCFL_CHECKPOINT)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->checkpointService->TpToNextCp();
	return true;
}

SCMD_LINK(surf_ncp, surf_nextcp);
SCMD_LINK(surf_nextloc, surf_nextcp);

SCMD(surf_setstartpos, SCFL_CHECKPOINT | SCFL_MAP | SCFL_PREFERENCE)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->checkpointService->SetStartPosition();
	return true;
}

SCMD_LINK(surf_ssp, surf_setstartpos);

SCMD(surf_clearstartpos, SCFL_CHECKPOINT)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);
	player->checkpointService->ClearStartPosition();
	return true;
}

SCMD_LINK(surf_csp, surf_clearstartpos);
