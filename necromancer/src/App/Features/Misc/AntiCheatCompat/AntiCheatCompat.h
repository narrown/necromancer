#pragma once

#include "../../../../SDK/SDK.h"
#include <deque>

// Command history structure for pattern analysis (matching Amalgam's CmdHistory_t)
struct CmdHistory_t
{
	Vec3 m_vAngle;
	bool m_bAttack1;
	bool m_bAttack2;
	bool m_bSendingPacket;
};

class CAntiCheatCompat
{
public:
	// Main processing function called from CreateMove (matches Amalgam's AntiCheatCompatibility)
	void ProcessCommand(CUserCmd* pCmd, bool* pSendPacket);

	// Network cvar validation
	void ValidateNetworkCvars(void* pMsg);
	void SpoofCvarResponse(void* pMsg);

	// Feature limiting
	int GetMaxTickShift(int iServerMax);
	bool ShouldLimitBhop(int& iJumpCount, bool bGrounded, bool bLastGrounded, bool bJumping);
	float ClampBacktrackInterp(float flInterp);

	// Tracked sent values (matching Amalgam's F::Backtrack.m_flSentInterp pattern)
	float m_flSentInterp = -1.f;
	int m_iSentCmdrate = -1;
	int m_iSentUpdaterate = -1;

private:
	std::deque<CmdHistory_t> m_vHistory;
	int m_iBhopCount = 0;

	// Detection thresholds (from Amalgam - exact values)
	static constexpr float MATH_EPSILON = 1.f / 16.f;
	static constexpr float PSILENT_EPSILON = 1.f - MATH_EPSILON;
	static constexpr float REAL_EPSILON = 0.1f + MATH_EPSILON;
	static constexpr float SNAP_SIZE_EPSILON = 10.f - MATH_EPSILON;
	static constexpr float SNAP_NOISE_EPSILON = 0.5f + MATH_EPSILON;
};

MAKE_SINGLETON_SCOPED(CAntiCheatCompat, AntiCheatCompat, F);
