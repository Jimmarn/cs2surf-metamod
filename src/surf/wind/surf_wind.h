/*
 * Wind sounds: a per-player, client-side wind loop whose intensity follows the player's speed. Port of Jax's WindSound
 * CounterStrikeSharp plugin. The clips live in a workshop addon (the same "windsounds" addon that holds the layout HUD)
 * and are started with a client-side "play" command, so only that player hears them.
 */
#pragma once
#include "surf/surf.h"

class SurfWindService : public SurfBaseService
{
	using SurfBaseService::SurfBaseService;

	bool enabled {};  // !wind
	bool ordered {};  // !windmode: cycle the clips 1..8 instead of picking at random
	i32 nextIndex {}; // sequential mode: next clip
	f32 lastSoundTime {};

public:
	static void Init();
	virtual void Reset() override;

	void OnPhysicsSimulatePost();
	void Toggle();
	void ToggleMode();

	bool IsEnabled()
	{
		return this->enabled;
	}

	bool IsOrdered()
	{
		return this->ordered;
	}

	// server config "wind" block: whether the feature is on at all (the addon must be mounted for clients)
	static bool IsAvailable();
};
