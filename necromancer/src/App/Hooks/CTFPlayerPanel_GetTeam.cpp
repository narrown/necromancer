#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

MAKE_SIGNATURE(CTFPlayerPanel_GetTeam, "client.dll", "8B 91 ? ? ? ? 83 FA ? 74 ? 48 8B 05", 0x0);
MAKE_SIGNATURE(CTFTeamStatusPlayerPanel_Update, "client.dll", "40 56 57 48 83 EC ? 48 83 3D", 0x0);
MAKE_SIGNATURE(CTFTeamStatusPlayerPanel_Update_GetTeam_Call, "client.dll", "8B 9F ? ? ? ? 40 32 F6", 0x0);

static int s_iPlayerIndex = 0;

MAKE_HOOK(CTFPlayerPanel_GetTeam, Signatures::CTFPlayerPanel_GetTeam.Get(), int, __fastcall,
	void* rcx)
{
	static const auto dwDesired = Signatures::CTFTeamStatusPlayerPanel_Update_GetTeam_Call.Get();
	const auto dwRetAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());

	// If reveal scoreboard is enabled and called from the team status panel
	if (CFG::Visuals_Reveal_Scoreboard && dwRetAddr == dwDesired)
	{
		// Return local player's team so enemies show up in HUD
		if (auto pLocal = H::Entities->GetLocal())
			return pLocal->m_iTeamNum();
	}

	return CALL_ORIGINAL(rcx);
}

MAKE_HOOK(CTFTeamStatusPlayerPanel_Update, Signatures::CTFTeamStatusPlayerPanel_Update.Get(), bool, __fastcall,
	void* rcx)
{
	// Store player index for potential use by other hooks
	s_iPlayerIndex = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(rcx) + 580);
	return CALL_ORIGINAL(rcx);
}
