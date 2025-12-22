#pragma once
#include "../../../SDK/SDK.h"

class CFakeAngle
{
public:
	void Run(C_TFPlayer* pLocal);
};

MAKE_SINGLETON_SCOPED(CFakeAngle, FakeAngle, F);
