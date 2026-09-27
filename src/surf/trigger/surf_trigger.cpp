#include "surf_trigger.h"
#include "utils/hooks.h"
#include "surf/checkpoint/surf_checkpoint.h"
#include "surf/language/surf_language.h"
#include "surf/mode/surf_mode.h"
#include "surf/style/surf_style.h"
#include "surf/timer/surf_timer.h"
#include "surf/noclip/surf_noclip.h"

void SurfTriggerService::Reset()
{
	this->triggerTrackers.RemoveAll();
	this->modifiers = {};
	this->lastModifiers = {};
	this->pushEvents.RemoveAll();
}

void SurfTriggerService::OnPhysicsSimulate()
{
	FOR_EACH_VEC(this->triggerTrackers, i)
	{
		CEntityHandle handle = this->triggerTrackers[i].triggerHandle;
		CBaseTrigger *trigger = dynamic_cast<CBaseTrigger *>(GameEntitySystem()->GetEntityInstance(handle));
		// The trigger mysteriously disappeared...
		if (!trigger)
		{
			const SurfTrigger *surfTrigger = this->triggerTrackers[i].surfTrigger;
			if (surfTrigger)
			{
				this->OnMappingApiTriggerEndTouchPost(this->triggerTrackers[i]);
			}

			this->triggerTrackers.Remove(i);
			i--;
			continue;
		}
		this->triggerTrackers[i].touchedThisTick = false;
	}
}

void SurfTriggerService::OnPhysicsSimulatePost()
{
	this->player->UpdateTriggerTouchList();
	this->TouchAll();
	/*
		NOTE:
		1. To prevent multiplayer bugs, make sure that all of these cvars are part of the mode convars.
		2. The apply part is here mostly just to replicate the values to the client, with the exception of push triggers.
	*/

	this->ApplyJumpFactor(this->modifiers.jumpFactor != this->lastModifiers.jumpFactor);
	// Try to apply pushes one last time on this tick, to catch all the buttons that were not set during movement processing (attack+attack2).
	this->ApplyPushes();
	this->CleanupPushEvents();

	this->lastModifiers = this->modifiers;
}

void SurfTriggerService::OnCheckJumpButtonLegacy()
{
	this->ApplyJumpFactor(false);
}

void SurfTriggerService::OnProcessMovement() {}

void SurfTriggerService::OnProcessMovementPost()
{
	this->modifiers.jumpFactor = 1.0f;
	this->ApplyPushes();
	this->CleanupPushEvents();
}

void SurfTriggerService::OnStopTouchGround()
{
	FOR_EACH_VEC(this->triggerTrackers, i)
	{
		TriggerTouchTracker tracker = this->triggerTrackers[i];
		CBaseTrigger *trigger = dynamic_cast<CBaseTrigger *>(GameEntitySystem()->GetEntityInstance(tracker.triggerHandle));
		if (!tracker.surfTrigger || !trigger)
		{
			continue;
		}
		if (this->player->jumped && Surf::mapapi::IsPushTrigger(tracker.surfTrigger->type)
			&& tracker.surfTrigger->push.pushConditions & SurfMapPush::SURF_PUSH_JUMP_EVENT)
		{
			this->AddPushEvent(tracker.surfTrigger);
		}
	}
}

void SurfTriggerService::OnTeleport()
{
	FOR_EACH_VEC_BACK(this->pushEvents, i)
	{
		if (this->pushEvents[i].source->push.cancelOnTeleport)
		{
			this->pushEvents.Remove(i);
		}
	}
}

void SurfTriggerService::TouchTriggersAlongPath(const Vector &start, const Vector &end, const bbox_t &bounds)
{
	if (!this->player->IsAlive() || this->player->GetCollisionGroup() != SURF_COLLISION_GROUP_STANDARD)
	{
		return;
	}
	CTraceFilterHitAllTriggers filter;
	trace_t tr;
	Ray_t ray(bounds.mins, bounds.maxs);
	INavPhysicsInterface::TraceShape(ray, start, end, &filter, &tr);
	FOR_EACH_VEC(filter.hitTriggerHandles, i)
	{
		CEntityHandle handle = filter.hitTriggerHandles[i];
		CBaseTrigger *trigger = dynamic_cast<CBaseTrigger *>(GameEntitySystem()->GetEntityInstance(handle));
		if (!trigger || !V_strstr(trigger->GetClassname(), "trigger_"))
		{
			continue;
		}
		if (!this->GetTriggerTracker(trigger))
		{
			this->StartTouch(trigger);
		}
	}
}

void SurfTriggerService::UpdateTriggerTouchList()
{
	// TODO: get current style and only disable gravity resets for styles that affect gravity
	/*
	// reset gravity before all the Touch() calls
	if (this->player->timerService->GetPaused())
	{
		// No gravity while paused.
		this->player->GetPlayerPawn()->SetGravityScale(0);
	}
	else
	{
		this->player->GetPlayerPawn()->SetGravityScale(1);
	}
	*/

	if (!this->player->IsAlive() || this->player->noclipService->IsNoclipping())
	{
		this->EndTouchAll();
		return;
	}
	Vector origin;
	this->player->GetOrigin(&origin);
	bbox_t bounds;
	this->player->GetBBoxBounds(&bounds);
	CTraceFilterHitAllTriggers filter;
	trace_t tr;
	INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), origin, origin, &filter, &tr);

	FOR_EACH_VEC_BACK(this->triggerTrackers, i)
	{
		CEntityHandle handle = this->triggerTrackers[i].triggerHandle;
		CBaseTrigger *trigger = dynamic_cast<CBaseTrigger *>(GameEntitySystem()->GetEntityInstance(handle));
		// The trigger mysteriously disappeared...
		if (!trigger)
		{
			const SurfTrigger *surfTrigger = this->triggerTrackers[i].surfTrigger;
			if (surfTrigger)
			{
				this->OnMappingApiTriggerEndTouchPost(this->triggerTrackers[i]);
			}

			this->triggerTrackers.Remove(i);
			continue;
		}
		if (!filter.hitTriggerHandles.HasElement(handle))
		{
			this->EndTouch(trigger);
		}
	}

	FOR_EACH_VEC(filter.hitTriggerHandles, i)
	{
		CEntityHandle handle = filter.hitTriggerHandles[i];
		CBaseTrigger *trigger = dynamic_cast<CBaseTrigger *>(GameEntitySystem()->GetEntityInstance(handle));
		if (!trigger || !V_strstr(trigger->GetClassname(), "trigger_"))
		{
			continue;
		}
		auto tracker = this->GetTriggerTracker(trigger);
		if (!tracker)
		{
			this->StartTouch(trigger);
		}
		else if (SurfTriggerService::HighFrequencyTouchAllowed(*tracker))
		{
			this->Touch(trigger);
		}
	}
}

void SurfTriggerService::EndTouchAll()
{
	FOR_EACH_VEC(this->triggerTrackers, i)
	{
		CEntityHandle handle = this->triggerTrackers[i].triggerHandle;
		CBaseTrigger *trigger = dynamic_cast<CBaseTrigger *>(GameEntitySystem()->GetEntityInstance(handle));
		// The trigger mysteriously disappeared...
		if (!trigger)
		{
			const SurfTrigger *surfTrigger = this->triggerTrackers[i].surfTrigger;
			if (surfTrigger)
			{
				this->OnMappingApiTriggerEndTouchPost(this->triggerTrackers[i]);
			}

			this->triggerTrackers.Remove(i);
			i--;
			continue;
		}
		this->EndTouch(trigger);
	}
}

bool SurfTriggerService::IsValidTrigger(CBaseEntity *entity)
{
	// All trigger_ entities are valid except trigger_push.
	if (entity && V_strstr(entity->GetClassname(), "trigger_") && !SURF_STREQI(entity->GetClassname(), "trigger_push"))
	{
		return true;
	}
	return false;
}

void SurfTriggerService::TouchAll()
{
	FOR_EACH_VEC(this->triggerTrackers, i)
	{
		CEntityHandle handle = this->triggerTrackers[i].triggerHandle;
		CBaseTrigger *trigger = dynamic_cast<CBaseTrigger *>(GameEntitySystem()->GetEntityInstance(handle));
		// The trigger mysteriously disappeared...
		if (!trigger)
		{
			const SurfTrigger *surfTrigger = this->triggerTrackers[i].surfTrigger;
			if (surfTrigger)
			{
				this->OnMappingApiTriggerEndTouchPost(this->triggerTrackers[i]);
			}

			this->triggerTrackers.Remove(i);
			i--;
			continue;
		}
		this->Touch(trigger);
	}
}

bool SurfTriggerService::IsManagedByTriggerService(CBaseEntity *toucher, CBaseEntity *touched)
{
	SurfPlayer *player = NULL;
	CBaseTrigger *trigger = NULL;
	if (!toucher || !touched)
	{
		return false;
	}
	if (V_stricmp(toucher->GetClassname(), "player") == 0 && V_strstr(touched->GetClassname(), "trigger_"))
	{
		player = g_pSurfPlayerManager->ToPlayer(static_cast<CCSPlayerPawn *>(toucher));
		trigger = static_cast<CBaseTrigger *>(touched);
	}
	if (V_stricmp(touched->GetClassname(), "player") == 0 && V_strstr(toucher->GetClassname(), "trigger_"))
	{
		player = g_pSurfPlayerManager->ToPlayer(static_cast<CCSPlayerPawn *>(touched));
		trigger = static_cast<CBaseTrigger *>(toucher);
	}
	if (player && player->IsAlive())
	{
		return true;
	}
	return false;
}

bool SurfTriggerService::HighFrequencyTouchAllowed(TriggerTouchTracker tracker)
{
	return tracker.surfTrigger;
}

SurfTriggerService::TriggerTouchTracker *SurfTriggerService::GetTriggerTracker(CBaseTrigger *trigger)
{
	if (!trigger)
	{
		return nullptr;
	}
	CEntityHandle handle = trigger->GetRefEHandle();
	FOR_EACH_VEC(triggerTrackers, i)
	{
		TriggerTouchTracker &tracker = triggerTrackers[i];
		if (tracker.triggerHandle == handle)
		{
			return &tracker;
		}
	}
	return nullptr;
}

void SurfTriggerService::StartTouch(CBaseTrigger *trigger)
{
	// Can't touch nothing.
	if (!trigger)
	{
		return;
	}
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	// Can't touch anything if there is no pawn.
	if (!pawn)
	{
		return;
	}

	TriggerTouchTracker *tracker = this->GetTriggerTracker(trigger);
	bool shouldStartTouch = (!tracker || tracker->CanStartTouch()) && this->OnTriggerStartTouchPre(trigger);

	if (!shouldStartTouch)
	{
		return;
	}

	// New interaction!
	if (!tracker)
	{
		tracker = triggerTrackers.AddToTailGetPtr();
		tracker->triggerHandle = trigger->GetRefEHandle();
		tracker->startTouchTime = g_pSurfUtils->GetServerGlobals()->curtime;
		tracker->surfTrigger = Surf::mapapi::GetSurfTrigger(trigger);
	}

	// Handle changes in origin and velocity due to this event.
	this->UpdatePreTouchData();
	hooks::CallOriginalStartTouch(trigger, pawn);
	hooks::CallOriginalStartTouch(pawn, pawn);
	tracker->startedTouch = true;
	this->OnTriggerStartTouchPost(trigger, *tracker);
	// Call UpdatePlayerPostTouch here because UpdatePlayerStartTouch will be run inside Touch later anyway.
	this->UpdatePlayerPostTouch();

	if (SurfTriggerService::ShouldTouchOnStartTouch(*tracker))
	{
		this->Touch(trigger, true);
	}
}

void SurfTriggerService::Touch(CBaseTrigger *trigger, bool silent)
{
	// Can't touch nothing.
	if (!trigger)
	{
		return;
	}
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	// Can't touch anything if there is no pawn.
	if (!pawn)
	{
		return;
	}

	TriggerTouchTracker *tracker = this->GetTriggerTracker(trigger);
	if (!tracker)
	{
		return;
	}
	bool shouldTouch = tracker->CanTouch() && this->OnTriggerTouchPre(trigger, *tracker);

	if (shouldTouch)
	{
		this->UpdatePreTouchData();
		hooks::CallOriginalTouch(trigger, pawn);
		hooks::CallOriginalTouch(pawn, pawn);
		if (!silent)
		{
			tracker->touchedThisTick = true;
		}

		this->OnTriggerTouchPost(trigger, *tracker);
		this->UpdatePlayerPostTouch();
	}
}

void SurfTriggerService::EndTouch(CBaseTrigger *trigger)
{
	// Can't touch nothing.
	if (!trigger)
	{
		return;
	}
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	// Can't touch anything if there is no pawn.
	if (!pawn)
	{
		return;
	}

	TriggerTouchTracker *tracker = this->GetTriggerTracker(trigger);
	if (!tracker)
	{
		return;
	}

	bool shouldEndTouch = tracker->CanEndTouch() && this->OnTriggerEndTouchPre(trigger, *tracker);
	if (shouldEndTouch)
	{
		if (SurfTriggerService::ShouldTouchBeforeEndTouch(*tracker))
		{
			this->Touch(trigger);
		}
		this->UpdatePreTouchData();
		hooks::CallOriginalEndTouch(trigger, pawn);
		hooks::CallOriginalEndTouch(pawn, pawn);
		this->UpdatePlayerPostTouch();
		this->OnTriggerEndTouchPost(trigger, *tracker);
		this->triggerTrackers.FindAndRemove(*tracker);
	}
}

void SurfTriggerService::UpdatePreTouchData()
{
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (!pawn)
	{
		return;
	}
	this->preTouchVelocity = pawn->m_vecAbsVelocity();
	this->preTouchOrigin = pawn->m_CBodyComponent()->m_pSceneNode()->m_vecAbsOrigin();
}

void SurfTriggerService::UpdatePlayerPostTouch()
{
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (!pawn)
	{
		return;
	}
	// Player has a modified velocity through trigger touching, take this into account.

	bool modifiedOrigin = this->preTouchOrigin != pawn->m_CBodyComponent()->m_pSceneNode()->m_vecAbsOrigin();
	if (player->processingMovement && modifiedOrigin)
	{
		player->SetOrigin(pawn->m_CBodyComponent()->m_pSceneNode()->m_vecAbsOrigin());
	}
}
