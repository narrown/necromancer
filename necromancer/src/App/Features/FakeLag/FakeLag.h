#pragma once
#include "../../../SDK/SDK.h"

class CFakeLag
{
private:
	bool IsAllowed(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool IsSniperThreat(C_TFPlayer* pLocal, int& outMinTicks, int& outMaxTicks);

	Vec3 m_vLastPosition = {};
	int m_iNewEntityUnchokeTicks = 0;
	int m_iCurrentChokeTicks = 0;
	int m_iTargetChokeTicks = 0;

public:
	void Run(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd, bool* pSendPacket);

	int m_iGoal = 0;
	bool m_bEnabled = false;
};

MAKE_SINGLETON_SCOPED(CFakeLag, FakeLag, F);
