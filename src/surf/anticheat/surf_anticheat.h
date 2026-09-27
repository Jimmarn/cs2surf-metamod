#pragma once
#include "surf/surf.h"
class SurfBaseService;

class SurfAnticheatService : public SurfBaseService
{
public:
	using SurfBaseService::SurfBaseService;

private:
	bool hasValidCvars = true;

public:
	bool ShouldCheckClientCvars()
	{
		return hasValidCvars;
	}

	void MarkHasInvalidCvars()
	{
		hasValidCvars = false;
	}

	void OnPlayerFullyConnect();
};
