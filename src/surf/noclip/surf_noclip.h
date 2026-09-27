#pragma once
#include "surf/surf.h"

#define SURF_JUST_NOCLIP_TIME 0.05f;

class SurfNoclipService : public SurfBaseService
{
	using SurfBaseService::SurfBaseService;

private:
	f32 lastNoclipTime {};
	bool inNoclip {};

public:
	void DisableNoclip()
	{
		this->inNoclip = false;
	}

	void ToggleNoclip()
	{
		this->inNoclip = !this->inNoclip;
	}

	bool IsNoclipping()
	{
		return this->inNoclip;
	}

	bool JustNoclipped()
	{
		return g_pSurfUtils->GetServerGlobals()->curtime - lastNoclipTime < SURF_JUST_NOCLIP_TIME;
	}

	virtual void Reset() override;
	void HandleMoveCollision();
	void HandleNoclip();

	void OnTeleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity);
};
