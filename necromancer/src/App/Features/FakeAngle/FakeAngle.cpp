#include "FakeAngle.h"
#include "../CFG.h"
#include "../amalgam_port/AmalgamCompat.h"

// Jitter system (hash-based like Amalgam)
static inline int GetJitter(uint32_t uHash)
{
	static std::unordered_map<uint32_t, bool> mJitter = {};

	if (!I::ClientState->chokedcommands)
		mJitter[uHash] = !mJitter[uHash];
	return mJitter[uHash] ? 1 : -1;
}

// Simple hash for jitter keys
static constexpr uint32_t HashJitter(const char* str)
{
	uint32_t hash = 2166136261u;
	while (*str)
	{
		hash ^= static_cast<uint32_t>(*str++);
		hash *= 16777619u;
	}
	return hash;
}

bool CFakeAngle::AntiAimOn()
{
	return CFG::Exploits_AntiAim_Enabled
		&& (CFG::Exploits_AntiAim_PitchReal
		|| CFG::Exploits_AntiAim_PitchFake
		|| CFG::Exploits_AntiAim_YawReal
		|| CFG::Exploits_AntiAim_YawFake
		|| CFG::Exploits_AntiAim_RealYawBase
		|| CFG::Exploits_AntiAim_FakeYawBase
		|| CFG::Exploits_AntiAim_RealYawOffset
		|| CFG::Exploits_AntiAim_FakeYawOffset);
}

bool CFakeAngle::YawOn()
{
	return CFG::Exploits_AntiAim_Enabled
		&& (CFG::Exploits_AntiAim_YawReal
		|| CFG::Exploits_AntiAim_YawFake
		|| CFG::Exploits_AntiAim_RealYawBase
		|| CFG::Exploits_AntiAim_FakeYawBase
		|| CFG::Exploits_AntiAim_RealYawOffset
		|| CFG::Exploits_AntiAim_FakeYawOffset);
}

bool CFakeAngle::ShouldRun(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pLocal || !pLocal->IsAlive() || pLocal->InCond(TF_COND_TAUNTING))
		return false;
	
	// Ghost check
	if (pLocal->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
		return false;
	
	// Use GetMoveType() - the netvar-based one
	if (pLocal->GetMoveType() != MOVETYPE_WALK)
		return false;
	
	if (pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return false;
	
	// Don't anti-aim while charging (demoman shield)
	if (pLocal->InCond(TF_COND_SHIELD_CHARGE))
		return false;
	
	// Don't anti-aim when attacking (check both G::Attacking and current cmd buttons)
	// G::Attacking is set early, but aimbot might add IN_ATTACK later
	if (G::Attacking == 1)
		return false;
	
	// Don't anti-aim during rapid fire shifting
	if (Shifting::bShiftingRapidFire || Shifting::bRapidFireWantShift)
		return false;
	
	// Don't anti-aim during recharging
	if (Shifting::bRecharging)
		return false;
	
	// Beggar's Bazooka check (like Amalgam)
	if (pWeapon && pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka 
		&& pCmd->buttons & IN_ATTACK && !(G::LastUserCmd ? G::LastUserCmd->buttons & IN_ATTACK : false))
		return false;
	
	return true;
}

float CFakeAngle::GetYawOffset(C_TFPlayer* pLocal, bool bFake)
{
	const int iMode = bFake ? CFG::Exploits_AntiAim_YawFake : CFG::Exploits_AntiAim_YawReal;
	int iJitter = GetJitter(HashJitter("Yaw"));
	
	// Yaw modes: 0=Forward, 1=Left, 2=Right, 3=Backwards, 4=Edge, 5=Jitter, 6=Spin
	switch (iMode)
	{
		case 0: return 0.0f; // Forward
		case 1: return 90.0f; // Left
		case 2: return -90.0f; // Right
		case 3: return 180.0f; // Backwards
		case 4: // Edge - just use yaw value (no trace-based edge detection)
			return (bFake ? CFG::Exploits_AntiAim_FakeYawValue : CFG::Exploits_AntiAim_RealYawValue);
		case 5: // Jitter
			return (bFake ? CFG::Exploits_AntiAim_FakeYawValue : CFG::Exploits_AntiAim_RealYawValue) * iJitter;
		case 6: // Spin
			return fmod(I::GlobalVars->tickcount * CFG::Exploits_AntiAim_SpinSpeed + 180.0f, 360.0f) - 180.0f;
	}
	return 0.0f;
}

float CFakeAngle::GetBaseYaw(C_TFPlayer* pLocal, CUserCmd* pCmd, bool bFake)
{
	const int iMode = bFake ? CFG::Exploits_AntiAim_FakeYawBase : CFG::Exploits_AntiAim_RealYawBase;
	const float flOffset = bFake ? CFG::Exploits_AntiAim_FakeYawOffset : CFG::Exploits_AntiAim_RealYawOffset;
	
	// YawBase modes: 0=View, 1=Target
	switch (iMode)
	{
		case 0: // View
			return pCmd->viewangles.y + flOffset;
		case 1: // Target - find closest enemy
		{
			float flSmallestAngleTo = 0.0f;
			float flSmallestFovTo = 360.0f;
			
			for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
			{
				auto pEntity = I::ClientEntityList->GetClientEntity(i);
				if (!pEntity || pEntity == pLocal)
					continue;
				
				auto pPlayer = pEntity->As<C_TFPlayer>();
				if (!pPlayer || !pPlayer->IsAlive())
					continue;
				
				// Skip ghosts
				if (pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
					continue;
				
				if (pPlayer->m_iTeamNum() == pLocal->m_iTeamNum())
					continue;
				
				Vec3 vAngleTo = Math::CalcAngle(pLocal->m_vecOrigin(), pPlayer->m_vecOrigin());
				float flFOVTo = Math::CalcFov(I::EngineClient->GetViewAngles(), vAngleTo);
				
				if (flFOVTo < flSmallestFovTo)
				{
					flSmallestAngleTo = vAngleTo.y;
					flSmallestFovTo = flFOVTo;
				}
			}
			
			return (flSmallestFovTo == 360.0f ? pCmd->viewangles.y + flOffset : flSmallestAngleTo + flOffset);
		}
	}
	return pCmd->viewangles.y;
}

void CFakeAngle::RunOverlapping(C_TFPlayer* pLocal, CUserCmd* pCmd, float& flYaw, bool bFake, float flEpsilon)
{
	if (!CFG::Exploits_AntiAim_AntiOverlap || bFake)
		return;
	
	float flFakeYaw = GetBaseYaw(pLocal, pCmd, true) + GetYawOffset(pLocal, true);
	const float flYawDiff = Math::NormalizeAngle(flYaw - flFakeYaw);
	if (fabsf(flYawDiff) < flEpsilon)
		flYaw += flYawDiff > 0 ? flEpsilon : -flEpsilon;
}

float CFakeAngle::GetYaw(C_TFPlayer* pLocal, CUserCmd* pCmd, bool bFake)
{
	float flYaw = GetBaseYaw(pLocal, pCmd, bFake) + GetYawOffset(pLocal, bFake);
	RunOverlapping(pLocal, pCmd, flYaw, bFake);
	return flYaw;
}

float CFakeAngle::GetPitch(float flCurPitch)
{
	float flRealPitch = 0.0f, flFakePitch = 0.0f;
	int iJitter = GetJitter(HashJitter("Pitch"));
	
	// PitchReal: 0=None, 1=Up, 2=Down, 3=Zero, 4=Jitter, 5=ReverseJitter
	switch (CFG::Exploits_AntiAim_PitchReal)
	{
		case 1: flRealPitch = -89.0f; break; // Up
		case 2: flRealPitch = 89.0f; break; // Down
		case 3: flRealPitch = 0.0f; break; // Zero
		case 4: flRealPitch = -89.0f * iJitter; break; // Jitter
		case 5: flRealPitch = 89.0f * iJitter; break; // ReverseJitter
	}
	
	// PitchFake: 0=None, 1=Up, 2=Down, 3=Jitter, 4=ReverseJitter
	switch (CFG::Exploits_AntiAim_PitchFake)
	{
		case 1: flFakePitch = -89.0f; break; // Up
		case 2: flFakePitch = 89.0f; break; // Down
		case 3: flFakePitch = -89.0f * iJitter; break; // Jitter
		case 4: flFakePitch = 89.0f * iJitter; break; // ReverseJitter
	}
	
	// Amalgam's pitch trick - adds 360 to create invalid pitch that looks different to enemies
	if (CFG::Exploits_AntiAim_PitchReal && CFG::Exploits_AntiAim_PitchFake)
		return flRealPitch + (flFakePitch > 0.0f ? 360.0f : -360.0f);
	else if (CFG::Exploits_AntiAim_PitchReal)
		return flRealPitch;
	else if (CFG::Exploits_AntiAim_PitchFake)
		return flFakePitch;
	else
		return flCurPitch;
}

void CFakeAngle::FakeShotAngles(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!CFG::Exploits_AntiAim_InvalidShootPitch || G::Attacking != 1 || pLocal->GetMoveType() != MOVETYPE_WALK)
		return;
	
	// Don't apply to medigun or laser pointer
	if (pWeapon)
	{
		int iWeaponID = pWeapon->GetWeaponID();
		if (iWeaponID == TF_WEAPON_MEDIGUN || iWeaponID == TF_WEAPON_LASER_POINTER)
			return;
	}
	
	G::bSilentAngles = true;
	pCmd->viewangles.x = 180.0f - pCmd->viewangles.x;
	pCmd->viewangles.y += 180.0f;
}

void CFakeAngle::MinWalk(CUserCmd* pCmd, C_TFPlayer* pLocal)
{
	if (!CFG::Exploits_AntiAim_MinWalk || !YawOn() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return;
	
	if (!pLocal->m_hGroundEntity())
		return;
	
	if (!pCmd->forwardmove && !pCmd->sidemove && pLocal->m_vecVelocity().Length2D() < 2.0f)
	{
		// Amalgam's MinWalk with proper rotation
		static bool bVar = true;
		float flMove = (IsDucking(pLocal) ? 3.0f : 1.0f) * ((bVar = !bVar) ? 1.0f : -1.0f);
		Vec3 vDir = { flMove, flMove, 0.0f };
		
		// Rotate movement to account for view angles
		Vec3 vMove = Math::RotatePoint(vDir, {}, { 0.0f, -pCmd->viewangles.y, 0.0f });
		pCmd->forwardmove = vMove.x * (fmodf(fabsf(pCmd->viewangles.x), 180.0f) > 90.0f ? -1.0f : 1.0f);
		pCmd->sidemove = -vMove.y;
		
		// Prevent standing still detection (Amalgam trick)
		pLocal->m_vecVelocity() = { 1.0f, 1.0f, pLocal->m_vecVelocity().z };
	}
}

void CFakeAngle::Run(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, bool bSendPacket)
{
	// Set global anti-aim flag (like Amalgam's G::AntiAim)
	// ShouldRun checks G::Attacking == 1, which is now updated AFTER aimbot runs
	bool bAntiAimActive = AntiAimOn() && ShouldRun(pLocal, pWeapon, pCmd);
	
	// FakeShotAngles runs regardless of anti-aim state
	FakeShotAngles(pLocal, pWeapon, pCmd);
	
	if (!bAntiAimActive)
	{
		// Store current angles (clamped for visual purposes)
		m_vRealAngles = { std::clamp(pCmd->viewangles.x, -89.0f, 89.0f), pCmd->viewangles.y };
		m_vFakeAngles = { std::clamp(pCmd->viewangles.x, -89.0f, 89.0f), pCmd->viewangles.y };
		return;
	}
	
	// bSendPacket = true means this is the "fake" tick that gets sent (shown to enemies)
	// bSendPacket = false means this is the "real" tick that gets choked
	
	// Get the pitch - this may return unclamped values like -449 for the fake pitch trick
	float flPitch = GetPitch(pCmd->viewangles.x);
	float flYaw = GetYaw(pLocal, pCmd, bSendPacket);
	
	// Store angles - always clamp for visual purposes
	if (bSendPacket)
	{
		m_vFakeAngles.x = std::clamp(flPitch, -89.0f, 89.0f);
		m_vFakeAngles.y = flYaw;
	}
	else
	{
		m_vRealAngles.x = std::clamp(flPitch, -89.0f, 89.0f);
		m_vRealAngles.y = flYaw;
	}
	
	// Apply angles to cmd - use unclamped pitch for server
	Vec3 vCmdAngles = { flPitch, flYaw, 0.0f };
	
	// Only clamp cmd angles when anti-cheat compatibility is enabled (like Amalgam)
	if (CFG::Misc_AntiCheat_Enabled)
		Math::ClampAngles(vCmdAngles);
	
	// Fix movement using clamped angles to avoid issues
	Vec3 vClampedForMovement = { std::clamp(flPitch, -89.0f, 89.0f), flYaw, 0.0f };
	H::AimUtils->FixMovement(pCmd, vClampedForMovement);
	
	// Apply angles
	pCmd->viewangles.x = vCmdAngles.x;
	pCmd->viewangles.y = vCmdAngles.y;
	
	G::bSilentAngles = true;
	
	MinWalk(pCmd, pLocal);
}

void CFakeAngle::SetupFakeModel(C_TFPlayer* pLocal)
{
	if (!pLocal || !pLocal->IsAlive())
	{
		m_bBonesSetup = false;
		return;
	}
	
	// Check if we should draw (like Amalgam's group check)
	if (!AntiAimOn() && !CFG::Exploits_FakeLag_Enabled)
	{
		m_bBonesSetup = false;
		return;
	}
	
	if (!CFG::Exploits_AntiAim_DrawFakeModel)
	{
		m_bBonesSetup = false;
		return;
	}
	
	auto pAnimState = pLocal->GetAnimState();
	if (!pAnimState)
	{
		m_bBonesSetup = false;
		return;
	}
	
	// Save original state
	float flOldFrameTime = I::GlobalVars->frametime;
	int nOldSequence = pLocal->m_nSequence();
	float flOldCycle = pLocal->m_flCycle();
	auto pOldPoseParams = pLocal->m_flPoseParameter();
	
	// Save anim state
	char pOldAnimState[sizeof(CMultiPlayerAnimState)];
	memcpy(pOldAnimState, pAnimState, sizeof(CMultiPlayerAnimState));
	
	// Setup fake angles - clamp pitch for visual (the 271 trick is for server, not visual)
	I::GlobalVars->frametime = 0.0f;
	Vec2 vAngle = { std::clamp(m_vFakeAngles.x, -89.0f, 89.0f), m_vFakeAngles.y };
	
	if (pLocal->InCond(TF_COND_TAUNTING) && pLocal->m_bAllowMoveDuringTaunt())
		pLocal->m_flTauntYaw() = vAngle.y;
	
	// Use m_flCurrentFeetYaw like Amalgam does
	pAnimState->m_flCurrentFeetYaw = vAngle.y;
	pAnimState->Update(vAngle.y, vAngle.x);
	pLocal->InvalidateBoneCache();
	m_bBonesSetup = pLocal->SetupBones(m_aBones, 128, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);
	
	// Restore original state
	I::GlobalVars->frametime = flOldFrameTime;
	pLocal->m_nSequence() = nOldSequence;
	pLocal->m_flCycle() = flOldCycle;
	pLocal->m_flPoseParameter() = pOldPoseParams;
	memcpy(pAnimState, pOldAnimState, sizeof(CMultiPlayerAnimState));
}
