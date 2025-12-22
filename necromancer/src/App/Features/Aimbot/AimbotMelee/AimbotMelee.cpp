#include "AimbotMelee.h"

#include "../../CFG.h"
#include "../../amalgam_port/Simulation/MovementSimulation/AmalgamMoveSim.h"

int CAimbotMelee::GetSwingTime(C_TFWeaponBase* pWeapon)
{
	// Knife has instant backstab, no swing delay
	if (pWeapon->GetWeaponID() == TF_WEAPON_KNIFE)
		return 0;
	
	// Get the actual smack delay from weapon data and convert to ticks
	int iSmackTicks = static_cast<int>(ceilf(pWeapon->GetSmackDelay() / I::GlobalVars->interval_per_tick));
	return iSmackTicks;
}

bool CAimbotMelee::ShouldTargetFriendlyBuilding(C_BaseObject* pBuilding, C_TFWeaponBase* pWeapon)
{
	const bool bIsWrench = pWeapon->GetWeaponID() == TF_WEAPON_WRENCH;
	const bool bCanRemoveSapper = SDKUtils::AttribHookValue(0.f, "set_dmg_apply_to_sapper", pWeapon) > 0.f;
	
	// Building has sapper - both wrench and homewrecker can remove
	if (pBuilding->m_bHasSapper())
		return bIsWrench || bCanRemoveSapper;
	
	// Only wrench can repair/upgrade
	if (!bIsWrench)
		return false;
	
	// Building needs repair
	if (pBuilding->m_iHealth() < pBuilding->m_iMaxHealth())
		return true;
	
	// Building can be upgraded (not mini and not max level)
	if (!pBuilding->m_bMiniBuilding() && pBuilding->m_iUpgradeLevel() < 3)
		return true;
	
	// Check sentry ammo
	if (pBuilding->GetClassId() == ETFClassIds::CObjectSentrygun)
	{
		const auto pSentry = pBuilding->As<C_ObjectSentrygun>();
		
		// Level 1 sentry: 150 max shells
		// Level 2 sentry: 200 max shells
		// Level 3 sentry: 200 max shells, 20 max rockets
		int iMaxShells = pSentry->m_iUpgradeLevel() == 1 ? 150 : 200;
		int iMaxRockets = pSentry->m_iUpgradeLevel() == 3 ? 20 : 0;
		
		if (pSentry->m_iAmmoShells() < iMaxShells)
			return true;
		
		if (iMaxRockets > 0 && pSentry->m_iAmmoRockets() < iMaxRockets)
			return true;
	}
	
	return false;
}

bool CAimbotMelee::CanSee(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, MeleeTarget_t& target)
{
	// OPTIMIZATION: Use squared distance to avoid sqrt
	if (pLocal->GetShootPos().DistToSqr(target.Position) > 360000.0f) // 600^2
		return false;

	// Get weapon-specific range and hull size
	float flRange = SDKUtils::AttribHookValue(pWeapon->GetSwingRange(), "melee_range_multiplier", pWeapon);
	float flHull = SDKUtils::AttribHookValue(18.0f, "melee_bounds_multiplier", pWeapon);
	
	// Account for player model scale
	if (pLocal->m_flModelScale() > 1.0f)
	{
		flRange *= pLocal->m_flModelScale();
		flHull *= pLocal->m_flModelScale();
	}
	
	// Wrench has special range for friendly buildings
	if (pWeapon->GetWeaponID() == TF_WEAPON_WRENCH && target.Entity->m_iTeamNum() == pLocal->m_iTeamNum())
	{
		flRange = 70.0f;
		flHull = 18.0f;
	}

	Vec3 vSwingMins = { -flHull, -flHull, -flHull };
	Vec3 vSwingMaxs = { flHull, flHull, flHull };

	// Get swing time for prediction
	const int iSwingTicks = GetSwingTime(pWeapon);
	const bool bIsPlayer = target.Entity->GetClassId() == ETFClassIds::CTFPlayer;

	auto checkPos = [&](const Vec3& vLocalPos, const LagRecord_t* pRecord, const Vec3* pPredictedTargetPos = nullptr) -> bool
	{
		// Set up lag record if provided
		if (pRecord)
			F::LagRecordMatrixHelper->Set(pRecord);

		// Calculate optimal target position
		Vec3 vTargetPos = target.Position;
		
		// Use predicted position if provided (for swing prediction)
		if (pPredictedTargetPos)
		{
			vTargetPos = *pPredictedTargetPos;
		}
		else if (pRecord && bIsPlayer)
		{
			// For backtrack records, use the record position directly
			// The server will lag compensate to this position - don't extrapolate!
			Vec3 vOrigin = pRecord->AbsOrigin;
			Vec3 vMins = target.Entity->m_vecMins() + 0.125f;
			Vec3 vMaxs = target.Entity->m_vecMaxs() - 0.125f;
			
			// Calculate optimal Z position (aim at same height as local player when possible)
			float flZDiff = std::clamp(vLocalPos.z - vOrigin.z, vMins.z, vMaxs.z);
			vTargetPos = vOrigin + Vec3(0, 0, flZDiff);
		}
		else if (pRecord)
		{
			vTargetPos = pRecord->Center;
		}
		
		// Calculate angle to optimal target position
		Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vTargetPos);
		Vec3 vForward;
		Math::AngleVectors(vAngleTo, &vForward);
		Vec3 vTraceEnd = vLocalPos + (vForward * flRange);

		// First try: simple trace
		bool bCanSee = H::AimUtils->TraceEntityMelee(target.Entity, vLocalPos, vTraceEnd);
		
		// Second try: hull trace if simple trace failed
		if (!bCanSee)
		{
			trace_t trace = {};
			CTraceFilterHitscan filter = {};
			filter.m_pIgnore = pLocal;
			
			H::AimUtils->TraceHull(vLocalPos, vTraceEnd, vSwingMins, vSwingMaxs, MASK_SOLID, &filter, &trace);
			bCanSee = trace.m_pEnt && trace.m_pEnt == target.Entity;
		}
		
		// Third try: scan multiple hitbox points for better coverage
		if (!bCanSee && pRecord && bIsPlayer)
		{
			// Try different hitboxes: body, pelvis, head
			static const int hitboxes[] = { HITBOX_BODY, HITBOX_PELVIS, HITBOX_HEAD };
			for (int hb : hitboxes)
			{
				Vec3 vHitboxPos = SDKUtils::GetHitboxPosFromMatrix(
					target.Entity->As<C_TFPlayer>(), hb, 
					const_cast<matrix3x4_t*>(pRecord->BoneMatrix)
				);
				
				Vec3 vHitboxAngle = Math::CalcAngle(vLocalPos, vHitboxPos);
				Vec3 vHitboxForward;
				Math::AngleVectors(vHitboxAngle, &vHitboxForward);
				Vec3 vHitboxTraceEnd = vLocalPos + (vHitboxForward * flRange);
				
				if (H::AimUtils->TraceEntityMelee(target.Entity, vLocalPos, vHitboxTraceEnd))
				{
					bCanSee = true;
					vAngleTo = vHitboxAngle;
					vTargetPos = vHitboxPos;
					break;
				}
			}
		}

		// Check if we can hit with current view angles (for smooth/assistive aim)
		if (CFG::Aimbot_Melee_Aim_Type == 2 || CFG::Aimbot_Melee_Aim_Type == 3)
		{
			Vec3 vCurrentForward;
			Math::AngleVectors(I::EngineClient->GetViewAngles(), &vCurrentForward);
			Vec3 vCurrentTraceEnd = vLocalPos + (vCurrentForward * flRange);
			
			target.MeleeTraceHit = H::AimUtils->TraceEntityMelee(target.Entity, vLocalPos, vCurrentTraceEnd);
		}
		else
		{
			target.MeleeTraceHit = bCanSee;
		}

		// Update target angle and position if we found a valid hit
		if (bCanSee)
		{
			target.AngleTo = vAngleTo;
			target.Position = vTargetPos;
		}

		// Restore lag record
		if (pRecord)
			F::LagRecordMatrixHelper->Restore();

		return bCanSee;
	};

	// Try current position first
	if (checkPos(pLocal->GetShootPos(), target.LagRecord))
	{
		return true;
	}

	// Try swing prediction if enabled
	if (!CFG::Aimbot_Melee_Predict_Swing || pLocal->InCond(TF_COND_SHIELD_CHARGE) || pWeapon->GetWeaponID() == TF_WEAPON_KNIFE)
	{
		return false;
	}

	if (iSwingTicks <= 0)
		return false;

	// Only simulate for enemies that are close enough to potentially hit within swing time
	// Skip simulation for friendly buildings (they don't move) and teammates
	if (target.bIsFriendlyBuilding)
		return false;

	// Skip simulation for targets too far away - max reasonable melee chase distance
	// Consider max speed (~450 u/s) * swing time + melee range
	const float flMaxSimDistance = 450.0f * (iSwingTicks * I::GlobalVars->interval_per_tick) + flRange + 100.0f;
	if (target.DistanceTo > flMaxSimDistance)
		return false;

	// Initialize movement simulations
	MoveStorage localStorage, targetStorage;
	F::MoveSim.Initialize(pLocal, localStorage, false, true);
	
	// Only simulate enemy players, not buildings (they don't move)
	if (bIsPlayer && target.Entity->m_iTeamNum() != pLocal->m_iTeamNum())
		F::MoveSim.Initialize(target.Entity, targetStorage, false);
	else if (bIsPlayer)
		return false; // Don't simulate teammates

	// Path storage for visualization
	std::vector<Vec3> vLocalPath, vTargetPath;
	vLocalPath.push_back(pLocal->m_vecOrigin());

	if (bIsPlayer)
		vTargetPath.push_back(target.Entity->m_vecOrigin());

	bool bFoundHit = false;

	// Simulate movement for swing duration
	const bool bSimulateTarget = bIsPlayer && !targetStorage.m_bFailed && !targetStorage.m_bInitFailed;
	
	for (int i = 0; i < iSwingTicks; i++)
	{
		// Simulate local player
		if (!localStorage.m_bFailed)
		{
			F::MoveSim.RunTick(localStorage);
			vLocalPath.push_back(localStorage.m_MoveData.m_vecAbsOrigin);
		}

		// Simulate target player (only for enemy players)
		if (bSimulateTarget)
		{
			F::MoveSim.RunTick(targetStorage);
			vTargetPath.push_back(targetStorage.m_vPredictedOrigin);
		}

		// Get predicted positions
		Vec3 vPredictedLocalPos = localStorage.m_bFailed ? pLocal->GetShootPos() : 
			localStorage.m_MoveData.m_vecAbsOrigin + pLocal->m_vecViewOffset();
		
		// Get predicted target position (only for enemy players, buildings stay static)
		Vec3 vPredictedTargetPos = target.Position;
		if (bSimulateTarget)
			vPredictedTargetPos = targetStorage.m_vPredictedOrigin;

		// Check if we can hit from predicted position with predicted target
		if (checkPos(vPredictedLocalPos, target.LagRecord, &vPredictedTargetPos))
		{
			bFoundHit = true;
			break;
		}
	}

	// Draw paths for visualization
	if (CFG::Aimbot_Melee_Visualize_Prediction && CFG::Visuals_Simulation_Movement_Style > 0)
	{
		const float flDrawDuration = I::GlobalVars->curtime + 0.1f;
		
		// Draw local player path (green)
		if (vLocalPath.size() > 1)
			G::PathStorage.emplace_back(vLocalPath, flDrawDuration, Color_t(0, 255, 0, 255), CFG::Visuals_Simulation_Movement_Style, true);
		
		// Draw target path (red)
		if (vTargetPath.size() > 1)
			G::PathStorage.emplace_back(vTargetPath, flDrawDuration, Color_t(255, 0, 0, 255), CFG::Visuals_Simulation_Movement_Style, true);
	}

	// Restore entities
	F::MoveSim.Restore(localStorage);
	if (bSimulateTarget)
		F::MoveSim.Restore(targetStorage);

	return bFoundHit;
}

bool CAimbotMelee::GetTarget(C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, MeleeTarget_t& outTarget)
{
	const Vec3 vLocalPos = pLocal->GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	m_vecTargets.clear();
	
	// Separate vector for friendly buildings - these bypass the target max limit
	std::vector<MeleeTarget_t> vecFriendlyBuildings;

	// Find player targets
	if (CFG::Aimbot_Target_Players)
	{
		auto group{ pWeapon->m_iItemDefinitionIndex() == Soldier_t_TheDisciplinaryAction ? EEntGroup::PLAYERS_ALL : EEntGroup::PLAYERS_ENEMIES };

		if (!CFG::Aimbot_Melee_Whip_Teammates)
		{
			group = EEntGroup::PLAYERS_ENEMIES;
		}

		for (const auto pEntity : H::Entities->GetGroup(group))
		{
			if (!pEntity)
				continue;

			const auto pPlayer = pEntity->As<C_TFPlayer>();
			if (pPlayer->deadflag() || pPlayer->InCond(TF_COND_HALLOWEEN_GHOST_MODE))
				continue;

			if (pPlayer->m_iTeamNum() != pLocal->m_iTeamNum())
			{
				if (CFG::Aimbot_Ignore_Friends && pPlayer->IsPlayerOnSteamFriendsList())
					continue;

				if (CFG::Aimbot_Ignore_Invisible && pPlayer->IsInvisible())
					continue;

				if (pWeapon->m_iItemDefinitionIndex() != Heavy_t_TheHolidayPunch && CFG::Aimbot_Ignore_Invulnerable && pPlayer->IsInvulnerable())
					continue;

				if (CFG::Aimbot_Ignore_Taunting && pPlayer->InCond(TF_COND_TAUNTING))
					continue;
			}

			if (pPlayer->m_iTeamNum() != pLocal->m_iTeamNum() && CFG::Aimbot_Melee_Target_LagRecords)
			{
				int nRecords = 0;
				bool bHasValidLagRecords = false;

				if (F::LagRecords->HasRecords(pPlayer, &nRecords))
				{
					// When fake latency is active, only target the last 5 backtrack records
					const bool bFakeLatencyActive = F::LagRecords->GetFakeLatency() > 0.0f;
					const int nStartRecord = bFakeLatencyActive ? std::max(1, nRecords - 5) : 1;
					// Never target the last 2 records - the last allowed is the 3rd from the end
					const int nEndRecord = std::max(nStartRecord, nRecords - 2);

					for (int n = nStartRecord; n < nEndRecord; n++)
					{
						const auto pRecord = F::LagRecords->GetRecord(pPlayer, n, true);
						if (!pRecord || !F::LagRecords->DiffersFromCurrent(pRecord))
							continue;

						bHasValidLagRecords = true;
						Vec3 vPos = SDKUtils::GetHitboxPosFromMatrix(pPlayer, HITBOX_BODY, const_cast<matrix3x4_t*>(pRecord->BoneMatrix));
						Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
						const float flFOVTo = CFG::Aimbot_Melee_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
						const float flDistTo = vLocalPos.DistTo(vPos);

						if (CFG::Aimbot_Melee_Sort == 0 && flFOVTo > CFG::Aimbot_Melee_FOV)
							continue;

						m_vecTargets.emplace_back(MeleeTarget_t{ pPlayer, vPos, vAngleTo, flFOVTo, flDistTo, pRecord->SimulationTime, pRecord });
					}
				}

				// Fallback: if no valid lag records exist that differ from current (enemy standing still), target the real model position
				if (!bHasValidLagRecords)
				{
					Vec3 vPos = pPlayer->GetHitboxPos(HITBOX_BODY);
					Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
					const float flFOVTo = CFG::Aimbot_Melee_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
					const float flDistTo = vLocalPos.DistTo(vPos);

					if (CFG::Aimbot_Melee_Sort == 0 && flFOVTo <= CFG::Aimbot_Melee_FOV)
					{
						m_vecTargets.emplace_back(MeleeTarget_t{ pPlayer, vPos, vAngleTo, flFOVTo, flDistTo, pPlayer->m_flSimulationTime() });
					}
				}
			}
			else
			{
				// Not using lag records, just target current position
				Vec3 vPos = pPlayer->GetHitboxPos(HITBOX_BODY);
				Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
				const float flFOVTo = CFG::Aimbot_Melee_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
				const float flDistTo = vLocalPos.DistTo(vPos);

				if (CFG::Aimbot_Melee_Sort == 0 && flFOVTo > CFG::Aimbot_Melee_FOV)
					continue;

				m_vecTargets.emplace_back(MeleeTarget_t{ pPlayer, vPos, vAngleTo, flFOVTo, flDistTo, pPlayer->m_flSimulationTime() });
			}
		}
	}

	// Find enemy building targets
	if (CFG::Aimbot_Target_Buildings)
	{
		for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_ENEMIES))
		{
			if (!pEntity)
				continue;

			const auto pBuilding = pEntity->As<C_BaseObject>();

			if (pBuilding->m_bPlacing())
				continue;

			Vec3 vPos = pBuilding->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			const float flFOVTo = CFG::Aimbot_Melee_Sort == 0 ? Math::CalcFov(vLocalAngles, vAngleTo) : 0.0f;
			const float flDistTo = vLocalPos.DistTo(vPos);

			if (CFG::Aimbot_Melee_Sort == 0 && flFOVTo > CFG::Aimbot_Melee_FOV)
				continue;

			m_vecTargets.emplace_back(MeleeTarget_t{ pBuilding, vPos, vAngleTo, flFOVTo, flDistTo });
		}
	}

	// Find friendly building targets (auto repair/upgrade) - stored separately to bypass target max
	if (CFG::Aimbot_Melee_Auto_Repair)
	{
		const bool bIsWrench = pWeapon->GetWeaponID() == TF_WEAPON_WRENCH;
		const bool bCanRemoveSapper = SDKUtils::AttribHookValue(0.f, "set_dmg_apply_to_sapper", pWeapon) > 0.f;
		
		if (bIsWrench || bCanRemoveSapper)
		{
			for (const auto pEntity : H::Entities->GetGroup(EEntGroup::BUILDINGS_TEAMMATES))
			{
				if (!pEntity)
					continue;

				const auto pBuilding = pEntity->As<C_BaseObject>();

				if (pBuilding->m_bPlacing() || pBuilding->m_bCarried())
					continue;

				// Check if this building needs attention
				if (!ShouldTargetFriendlyBuilding(pBuilding, pWeapon))
					continue;

				Vec3 vPos = pBuilding->GetCenter();
				Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
				const float flDistTo = vLocalPos.DistTo(vPos);

				// No FOV check for friendly buildings - auto repair should work regardless of where you're looking
				MeleeTarget_t target = { pBuilding, vPos, vAngleTo, 0.0f, flDistTo };
				target.bIsFriendlyBuilding = true;
				vecFriendlyBuildings.emplace_back(target);
			}
		}
	}

	if (m_vecTargets.empty() && vecFriendlyBuildings.empty())
		return false;

	// Sort main targets by priority
	if (!m_vecTargets.empty())
		F::AimbotCommon->Sort(m_vecTargets, CFG::Aimbot_Melee_Sort);
	
	// Sort friendly buildings by distance (closest first for repair priority)
	if (!vecFriendlyBuildings.empty())
	{
		std::sort(vecFriendlyBuildings.begin(), vecFriendlyBuildings.end(), [](const MeleeTarget_t& a, const MeleeTarget_t& b) {
			return a.DistanceTo < b.DistanceTo;
		});
	}

	const int itEnd = std::min(4, static_cast<int>(m_vecTargets.size()));

	// Find and return the first valid target from main targets
	for (int n = 0; n < itEnd; n++)
	{
		auto& target = m_vecTargets[n];

		if (!CanSee(pLocal, pWeapon, target))
			continue;

		outTarget = target;
		return true;
	}
	
	// If no main target found, check friendly buildings (bypasses target max limit)
	for (auto& target : vecFriendlyBuildings)
	{
		if (!CanSee(pLocal, pWeapon, target))
			continue;

		outTarget = target;
		return true;
	}

	return false;
}

bool CAimbotMelee::ShouldAim(const CUserCmd* pCmd, C_TFWeaponBase* pWeapon)
{
	return CFG::Aimbot_Melee_Aim_Type != 1 || IsFiring(pCmd, pWeapon);
}

void CAimbotMelee::Aim(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon, const Vec3& vAngles)
{
	Vec3 vAngleTo = vAngles - pLocal->m_vecPunchAngle();
	Math::ClampAngles(vAngleTo);

	switch (CFG::Aimbot_Melee_Aim_Type)
	{
		// Plaint
		case 0:
		{
			pCmd->viewangles = vAngleTo;
			break;
		}

		// Silent
		case 1:
		{
			if (IsFiring(pCmd, pWeapon))
			{
				H::AimUtils->FixMovement(pCmd, vAngleTo);
				pCmd->viewangles = vAngleTo;

				if (Shifting::bShifting && Shifting::bShiftingWarp)
					G::bSilentAngles = true;

				else G::bPSilentAngles = true;
			}

			break;
		}

		// Smooth
		case 2:
		{
			Vec3 vDelta = vAngleTo - pCmd->viewangles;
			Math::ClampAngles(vDelta);

			if (vDelta.Length() > 0.0f && CFG::Aimbot_Melee_Smoothing)
			{
				pCmd->viewangles += vDelta / CFG::Aimbot_Melee_Smoothing;
			}

			break;
		}

		default: break;
	}
}

bool CAimbotMelee::ShouldFire(const MeleeTarget_t& target)
{
	return !CFG::Aimbot_AutoShoot ? false : target.MeleeTraceHit;
}

void CAimbotMelee::HandleFire(CUserCmd* pCmd, C_TFWeaponBase* pWeapon)
{
	pCmd->buttons |= IN_ATTACK;
}

bool CAimbotMelee::IsFiring(const CUserCmd* pCmd, C_TFWeaponBase* pWeapon)
{
	if (Shifting::bShifting && Shifting::bShiftingWarp)
	{
		return true;
	}

	if (pWeapon->GetWeaponID() == TF_WEAPON_KNIFE)
	{
		return (pCmd->buttons & IN_ATTACK) && G::bCanPrimaryAttack;
	}

	return fabsf(pWeapon->m_flSmackTime() - I::GlobalVars->curtime) < I::GlobalVars->interval_per_tick * 2.0f;
}

void CAimbotMelee::CrouchWhileAirborne(CUserCmd* pCmd, C_TFPlayer* pLocal)
{
	if (!(pLocal->m_fFlags() & FL_ONGROUND))
	{
		// Trace down to find distance to ground
		// Use world-only filter so we ignore all players/buildings - we want to land on enemies crouched
		Vec3 vStart = pLocal->m_vecOrigin();
		Vec3 vEnd = vStart - Vec3(0, 0, 500.f); // Trace 500 units down
		
		trace_t trace;
		CTraceFilterWorldOnly filter;
		Ray_t ray;
		ray.Init(vStart, vEnd, pLocal->m_vecMins(), pLocal->m_vecMaxs());
		I::EngineTrace->TraceRay(ray, MASK_PLAYERSOLID, &filter, &trace);
		
		float flDistToGround = (vStart.z - trace.endpos.z);
		
		// Calculate time to land based on current velocity and gravity
		float flZVel = pLocal->m_vecVelocity().z;
		float flGravity = SDKUtils::GetGravity();
		
		// Solve: dist = vel*t + 0.5*g*t^2 for t
		// Using quadratic formula, approximate ticks to land
		float flTimeToLand = 0.f;
		if (flGravity > 0.f)
		{
			// Quadratic: 0.5*g*t^2 + vel*t - dist = 0
			float a = 0.5f * flGravity;
			float b = -flZVel; // negative because vel is negative when falling
			float c = -flDistToGround;
			float discriminant = b * b - 4 * a * c;
			if (discriminant >= 0.f)
				flTimeToLand = (-b + sqrtf(discriminant)) / (2 * a);
		}
		
		int iTicksToLand = static_cast<int>(flTimeToLand / I::GlobalVars->interval_per_tick);
		
		// Uncrouch 2 ticks before landing to land standing, otherwise crouch
		if (iTicksToLand > 2)
			pCmd->buttons |= IN_DUCK;
		else
			pCmd->buttons &= ~IN_DUCK;
	}
}

void CAimbotMelee::Run(CUserCmd* pCmd, C_TFPlayer* pLocal, C_TFWeaponBase* pWeapon)
{
	if (!CFG::Aimbot_Melee_Active)
		return;

	if (CFG::Aimbot_Melee_Sort == 0)
		G::flAimbotFOV = CFG::Aimbot_Melee_FOV;

	if (Shifting::bShifting && !Shifting::bShiftingWarp)
		return;

	// Skip sapper - handled by AutoSapper triggerbot
	if (pWeapon->GetWeaponID() == TF_WEAPON_BUILDER)
		return;

	// Crouch while airborne (melee-specific config)
	if (CFG::Aimbot_Melee_Crouch_Airborne)
		CrouchWhileAirborne(pCmd, pLocal);

	const bool isFiring = IsFiring(pCmd, pWeapon);

	MeleeTarget_t target = {};
	if (GetTarget(pLocal, pWeapon, target) && target.Entity)
	{
		const auto aimKeyDown = H::Input->IsDown(CFG::Aimbot_Key) || CFG::Aimbot_Melee_Always_Active;
		if (aimKeyDown || isFiring)
		{
			G::nTargetIndex = target.Entity->entindex();

			// Auto shoot
			if (aimKeyDown)
			{
				if (ShouldFire(target))
				{
					HandleFire(pCmd, pWeapon);
				}
			}

			const bool bIsFiring = IsFiring(pCmd, pWeapon);
			G::bFiring = bIsFiring;

			// Are we ready to aim?
			if (ShouldAim(pCmd, pWeapon) || bIsFiring)
			{
				if (aimKeyDown)
				{
					Aim(pCmd, pLocal, pWeapon, target.AngleTo);
				}

				// Anti-cheat compatibility: skip tick count manipulation
				if (CFG::Misc_AntiCheat_Enabled)
					return;

				// Set tick_count for backtrack when attacking a player with a lag record
				// For melee, we need to set this when the attack is initiated (IN_ATTACK pressed),
				// not when the smack lands, because the server processes the attack command immediately
				const bool bIsAttacking = (pCmd->buttons & IN_ATTACK) && G::bCanPrimaryAttack;
				
				if (CFG::Misc_Accuracy_Improvements)
				{
					if (bIsAttacking && target.Entity->GetClassId() == ETFClassIds::CTFPlayer && target.LagRecord)
					{
						pCmd->tick_count = TIME_TO_TICKS(target.SimulationTime + SDKUtils::GetLerp());
					}
				}
				else
				{
					if (bIsAttacking && target.LagRecord)
					{
						pCmd->tick_count = TIME_TO_TICKS(target.SimulationTime + GetClientInterpAmount());
					}
				}
			}

			// Walk to target
			if (CFG::Aimbot_Melee_Walk_To_Target && (pLocal->m_fFlags() & FL_ONGROUND))
			{
				SDKUtils::WalkTo(pCmd, pLocal->m_vecOrigin(), target.Position, 1.f);
			}
		}
	}
}
