#ifndef SURF_REPLAYEVENTS_H
#define SURF_REPLAYEVENTS_H

#include "sdk/datatypes.h"
#include "surf_replay.h"
#include "data.h"

class SurfPlayer;

namespace Surf::replaysystem::events
{
	// Event processing functions
	void CheckEvents(SurfPlayer &player);

	// Event reprocessing for navigation
	void ReprocessEventsUpToTick(data::ReplayPlayback *replay, u32 targetTick);

	// Specific event handlers
	void HandleTimerEvent(SurfPlayer &player, const RpEvent *event, data::ReplayPlayback *replay);
	void HandleModeChangeEvent(SurfPlayer &player, const RpEvent *event);
	void HandleStyleChangeEvent(SurfPlayer &player, const RpEvent *event);
	void HandleTeleportEvent(SurfPlayer &player, const RpEvent *event);
} // namespace Surf::replaysystem::events

#endif // SURF_REPLAYEVENTS_H
