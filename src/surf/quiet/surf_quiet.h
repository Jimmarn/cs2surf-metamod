#pragma once
#include "surf/surf.h"
#include "iserver.h"
#include "networksystem/inetworkserializer.h"

namespace Surf::quiet
{
	void OnCheckTransmit(CCheckTransmitInfo **pInfo, int infoCount);

	void OnPostEvent(INetworkMessageInternal *pEvent, const CNetMessage *pData, const uint64 *clients);
} // namespace Surf::quiet

class SurfQuietService : public SurfBaseService
{
	using SurfBaseService::SurfBaseService;
	u8 lastObserverMode;
	CHandle<CBaseEntity> lastObserverTarget;
	bool hideWeapon {};

public:
	bool hideOtherPlayers {};
	static void Init();
	virtual void Reset() override;

	void OnPhysicsSimulatePost();
	void ApplyPreferences();
	void ToggleHide();
	void UpdateHideState();
	void SendFullUpdate();
	bool ShouldHide();
	bool ShouldHideIndex(u32 targetIndex);

	bool ShouldHideWeapon()
	{
		return this->hideWeapon;
	}

	void ToggleHideWeapon();
};
