#include "../../SDK/SDK.h"
#include "../Features/Crits/Crits.h"
#include "../Features/CFG.h"

// Static variable to store the seed from first-time prediction
static int s_iCurrentSeed = -1;

MAKE_HOOK(
	CTFWeaponBase_CalcIsAttackCritical, Signatures::CTFWeaponBase_CalcIsAttackCritical.Get(), void, __fastcall,
	void* rcx)
{
	auto pWeapon = reinterpret_cast<C_TFWeaponBase*>(rcx);
	if (!pWeapon)
	{
		CALL_ORIGINAL(rcx);
		return;
	}
	
	// Check if this is our weapon
	auto pLocal = H::Entities->GetLocal();
	bool bIsOurWeapon = pLocal && pWeapon->m_hOwner().Get() == pLocal;
	
	// Check if we should skip crits
	bool bShouldSkip = F::CritHack->ShouldSuppressCrit();
	
	// Fallback: check config directly if ShouldSuppressCrit isn't set
	if (!bShouldSkip && CFG::Exploits_Crits_Skip_Random_Crits)
	{
		bool bForcingCrits = H::Input->IsDown(CFG::Exploits_Crits_Force_Crit_Key) || 
		                     H::Input->IsDown(CFG::Exploits_Crits_Force_Crit_Key_Melee);
		if (!bForcingCrits && bIsOurWeapon)
		{
			bShouldSkip = true;
		}
	}
	
	// Check if user is holding force crit but can't actually attack
	// In this case, we should also protect the bucket from being drained
	bool bProtectBucket = false;
	if (bIsOurWeapon)
	{
		bool bForcingCrits = H::Input->IsDown(CFG::Exploits_Crits_Force_Crit_Key) || 
		                     H::Input->IsDown(CFG::Exploits_Crits_Force_Crit_Key_Melee);
		// If forcing crits but can't actually attack, protect the bucket
		if (bForcingCrits && !G::bCanPrimaryAttack)
		{
			bProtectBucket = true;
		}
	}

	const auto nPreviousWeaponMode = pWeapon->m_iWeaponMode();
	pWeapon->m_iWeaponMode() = 0; // TF_WEAPON_PRIMARY_MODE
	
	if (I::Prediction->m_bFirstTimePredicted)
	{
		// Save bucket values BEFORE the game runs
		float flOldCritTokenBucket = pWeapon->m_flCritTokenBucket();
		int nOldCritChecks = pWeapon->m_nCritChecks();
		int nOldCritSeedRequests = pWeapon->m_nCritSeedRequests();
		float flOldCritTime = pWeapon->m_flCritTime();
		
		CALL_ORIGINAL(rcx);
		
		s_iCurrentSeed = pWeapon->m_iCurrentSeed();
		
		// If user is holding force crit but can't attack, restore ALL values
		// This prevents the bucket from being drained when spamming attack while unable to shoot
		if (bProtectBucket)
		{
			// Restore everything - the attack didn't actually happen
			pWeapon->m_flCritTokenBucket() = flOldCritTokenBucket;
			pWeapon->m_nCritChecks() = nOldCritChecks;
			pWeapon->m_nCritSeedRequests() = nOldCritSeedRequests;
			pWeapon->m_flCritTime() = flOldCritTime;
		}
		// If skipping crits and the game determined a crit (deducted from bucket),
		// we need to "convert" it to a normal hit for the indicator
		else if (bShouldSkip)
		{
			// Check if the game determined a crit (crit seed requests increased)
			if (pWeapon->m_nCritSeedRequests() > nOldCritSeedRequests)
			{
				// Restore bucket and crit seed requests - undo the crit deduction
				pWeapon->m_flCritTokenBucket() = flOldCritTokenBucket;
				pWeapon->m_nCritSeedRequests() = nOldCritSeedRequests;
				
				// BUT keep m_nCritChecks incremented - this counts it as a normal hit
				// If the game didn't increment it, we need to
				if (pWeapon->m_nCritChecks() == nOldCritChecks)
				{
					pWeapon->m_nCritChecks() = nOldCritChecks + 1;
				}
				
				// Add damage to bucket as if it was a normal hit
				float flBaseDamage = pWeapon->GetDamage();
				static auto tf_weapon_criticals_bucket_cap = I::CVar->FindVar("tf_weapon_criticals_bucket_cap");
				float flBucketCap = tf_weapon_criticals_bucket_cap ? tf_weapon_criticals_bucket_cap->GetFloat() : 1000.f;
				if (pWeapon->m_flCritTokenBucket() < flBucketCap)
				{
					pWeapon->m_flCritTokenBucket() = std::min(pWeapon->m_flCritTokenBucket() + flBaseDamage, flBucketCap);
				}
			}
			
			// Reset crit time if it was set (suppress crit effect)
			if (pWeapon->m_flCritTime() > flOldCritTime)
			{
				pWeapon->m_flCritTime() = flOldCritTime;
			}
		}
	}
	else
	{
		// Re-prediction: save and restore values
		float flOldCritTokenBucket = pWeapon->m_flCritTokenBucket();
		int nOldCritChecks = pWeapon->m_nCritChecks();
		int nOldCritSeedRequests = pWeapon->m_nCritSeedRequests();
		float flOldLastRapidFireCritCheckTime = pWeapon->m_flLastRapidFireCritCheckTime();
		float flOldCritTime = pWeapon->m_flCritTime();
		
		CALL_ORIGINAL(rcx);
		
		pWeapon->m_flCritTokenBucket() = flOldCritTokenBucket;
		pWeapon->m_nCritChecks() = nOldCritChecks;
		pWeapon->m_nCritSeedRequests() = nOldCritSeedRequests;
		pWeapon->m_flLastRapidFireCritCheckTime() = flOldLastRapidFireCritCheckTime;
		pWeapon->m_flCritTime() = flOldCritTime;
		pWeapon->m_iCurrentSeed() = s_iCurrentSeed;
	}
	
	pWeapon->m_iWeaponMode() = nPreviousWeaponMode;
}
