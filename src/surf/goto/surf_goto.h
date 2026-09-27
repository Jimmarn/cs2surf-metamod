#pragma once
#include "surf/surf.h"

class SurfGotoService : public SurfBaseService
{
	using SurfBaseService::SurfBaseService;

private:

public:
	virtual void Reset() override;
	static void Init();

	bool GotoPlayer(const char *playerNamePart);
};
