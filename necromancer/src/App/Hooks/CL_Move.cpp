#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/NetworkFix/NetworkFix.h"
#include "../Features/SeedPred/SeedPred.h"
#include "../Features/ProjectileDodge/ProjectileDodge.h"
#include "../Features/Misc/AntiCheatCompat/AntiCheatCompat.h"
#include "../Features/AutoQueue/AutoQueue.h"

MAKE_SIGNATURE(CL_Move, "engine.dll", "40 55 53 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 83 3D", 0x0);

MAKE_HOOK(CL_Move, Signatures::CL_Move.Get(), void, __fastcall,
	float accumulated_extra_samples, bool bFinalTick)
{
	// Apply ping reducer BEFORE the lambda, just like Amalgam does
	F::NetworkFix->ApplyPingReducer();
	F::NetworkFix->ApplyAutoInterp();
	
	// Auto-queue BEFORE tick processing (like Amalgam does)
	// This ensures it runs even when not in-game
	F::AutoQueue->Run();
	
	// Calculate max ticks ONCE at the start (matching Amalgam's approach)
	// When anti-cheat is enabled, limit to 8 ticks maximum
	static auto sv_maxusrcmdprocessticks = I::CVar->FindVar("sv_maxusrcmdprocessticks");
	int nMaxTicks = sv_maxusrcmdprocessticks ? sv_maxusrcmdprocessticks->GetInt() : MAX_COMMANDS;
	if (CFG::Misc_AntiCheat_Enabled)
		nMaxTicks = std::min(nMaxTicks, 8);  // Hard limit to 8 ticks when anti-cheat enabled
	
	auto callOriginal = [&](bool bFinal)
	{
		// Update path storage (remove expired paths) - Amalgam style
		G::UpdatePathStorage();

		if (CFG::Misc_Ping_Reducer)
		{
			F::NetworkFix->FixInputDelay(bFinal);
		}

		F::SeedPred->AskForPlayerPerf();

		// Recharging is limited by nMaxTicks (which is already clamped to 8 when anti-cheat enabled)
		// This matches Amalgam's behavior in Ticks.cpp where m_iMaxShift is clamped to 8
		if (Shifting::nAvailableTicks < nMaxTicks)
		{
			if (H::Entities->GetWeapon())
			{
				if (!Shifting::bRecharging && !Shifting::bShifting && !Shifting::bShiftingWarp)
				{
					if (H::Input->IsDown(CFG::Exploits_Shifting_Recharge_Key))
					{
						Shifting::bRecharging = !I::MatSystemSurface->IsCursorVisible() && !I::EngineVGui->IsGameUIVisible();
					}
				}
			}

			if (Shifting::bRecharging)
			{
				// When anti-cheat is enabled, also limit recharging to 8 ticks max
				// This matches Amalgam's m_iMaxShift = std::min(m_iMaxShift, 8) behavior
				if (CFG::Misc_AntiCheat_Enabled && Shifting::nAvailableTicks >= 8)
				{
					Shifting::bRecharging = false;
				}
				else
				{
					Shifting::nAvailableTicks++;
					return;
				}
			}
		}
		else
		{
			Shifting::bRecharging = false;
		}

		CALL_ORIGINAL(accumulated_extra_samples, bFinal);
	};

	if (Shifting::bRapidFireWantShift)
	{
		Shifting::bRapidFireWantShift = false;
		Shifting::bShifting = true;
		Shifting::bShiftingRapidFire = true;  // Mark as rapid fire shifting (not warp)

		// Limit ticks to nMaxTicks (already clamped to 8 when anti-cheat enabled)
		int nTicks = std::min(CFG::Exploits_RapidFire_Ticks, nMaxTicks);
		nTicks = std::min(nTicks, Shifting::nAvailableTicks);  // Can't use more than we have
		
		for (int n = 0; n < nTicks; n++)
		{
			callOriginal(n == nTicks - 1);
			Shifting::nAvailableTicks--;
		}

		Shifting::bShifting = false;
		Shifting::bShiftingRapidFire = false;

		return;
	}

	if (const auto pLocal = H::Entities->GetLocal())
	{
		if (!pLocal->deadflag() && !Shifting::bRecharging && !Shifting::bShifting && !Shifting::bShiftingWarp && !Shifting::bRapidFireWantShift)
		{
			// Check if ProjectileDodge wants to warp
			if (F::ProjectileDodge->bWantWarp && Shifting::nAvailableTicks)
			{
				F::ProjectileDodge->bWantWarp = false; // Reset flag
				
				Shifting::bShifting = true;
				Shifting::bShiftingWarp = true;

				// Limit ticks to nMaxTicks (already clamped to 8 when anti-cheat enabled)
				int nTicks = std::min(Shifting::nAvailableTicks, nMaxTicks);

				for (int n = 0; n < nTicks; n++)
				{
					callOriginal(n == nTicks - 1);
					Shifting::nAvailableTicks--;
				}

				Shifting::bShifting = false;
				Shifting::bShiftingWarp = false;

				return;
			}
			
			if (!I::MatSystemSurface->IsCursorVisible() && !I::EngineVGui->IsGameUIVisible() && (H::Input->IsDown(CFG::Exploits_Warp_Key)))
			{
				if (Shifting::nAvailableTicks)
				{
					Shifting::bShifting = true;
					Shifting::bShiftingWarp = true;

					if (CFG::Exploits_Warp_Mode == 0)
					{
						for (int n = 0; n < 2; n++)
						{
							callOriginal(n == 1);
						}

						Shifting::nAvailableTicks--;
					}

					if (CFG::Exploits_Warp_Mode == 1)
					{
						// Limit ticks to nMaxTicks (already clamped to 8 when anti-cheat enabled)
						int nTicks = std::min(Shifting::nAvailableTicks, nMaxTicks);

						for (int n = 0; n < nTicks; n++)
						{
							callOriginal(n == nTicks - 1);
							Shifting::nAvailableTicks--;
						}
					}

					Shifting::bShifting = false;
					Shifting::bShiftingWarp = false;

					return;
				}
			}
		}
	}

	callOriginal(bFinalTick);
}
