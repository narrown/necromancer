#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/NetworkFix/NetworkFix.h"
#include "../Features/SeedPred/SeedPred.h"
#include "../Features/ProjectileDodge/ProjectileDodge.h"
#include "../Features/Misc/AntiCheatCompat/AntiCheatCompat.h"
#include "../Features/AutoQueue/AutoQueue.h"
#include "../Features/FakeAngle/FakeAngle.h"

MAKE_SIGNATURE(CL_Move, "engine.dll", "40 55 53 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 83 3D", 0x0);

// Max ticks for the shifting system
constexpr int MAX_SHIFT_TICKS = 22;

MAKE_HOOK(CL_Move, Signatures::CL_Move.Get(), void, __fastcall,
	float accumulated_extra_samples, bool bFinalTick)
{
	// Apply ping reducer BEFORE the lambda (like Amalgam)
	F::NetworkFix->ApplyPingReducer();
	F::NetworkFix->ApplyAutoInterp();
	
	// Auto-queue BEFORE tick processing
	F::AutoQueue->Run();
	
	// Calculate max ticks (like Amalgam's Ticks.Move)
	static auto sv_maxusrcmdprocessticks = I::CVar->FindVar("sv_maxusrcmdprocessticks");
	int nServerMaxTicks = sv_maxusrcmdprocessticks ? sv_maxusrcmdprocessticks->GetInt() : MAX_COMMANDS;
	
	// Calculate effective max ticks
	int nMaxTicks = std::min(nServerMaxTicks, MAX_SHIFT_TICKS);
	if (CFG::Misc_AntiCheat_Enabled)
		nMaxTicks = std::min(nMaxTicks, 8);
	
	// AMALGAM STYLE: m_iMaxShift is the max ticks we can store
	// Anti-aim ticks are NOT subtracted here - they're handled by choking in CreateMove
	// The tick reservation happens naturally because anti-aim chokes packets
	int nMaxRechargeTicks = nMaxTicks;
	
	auto callOriginal = [&](bool bFinal)
	{
		// Update path storage (like Amalgam)
		G::UpdatePathStorage();

		if (CFG::Misc_Ping_Reducer)
			F::NetworkFix->FixInputDelay(bFinal);

		F::SeedPred->AskForPlayerPerf();

		// Recharging is limited by nMaxRechargeTicks (reserves ticks for anti-aim)
		if (Shifting::nAvailableTicks < nMaxRechargeTicks)
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
				// Limit recharging based on anti-cheat and anti-aim
				if (CFG::Misc_AntiCheat_Enabled && Shifting::nAvailableTicks >= 8)
				{
					Shifting::bRecharging = false;
				}
				else if (Shifting::nAvailableTicks >= nMaxRechargeTicks)
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

	// RapidFire shifting
	if (Shifting::bRapidFireWantShift)
	{
		Shifting::bRapidFireWantShift = false;
		Shifting::bShifting = true;
		Shifting::bShiftingRapidFire = true;

		int nTicks = std::min(CFG::Exploits_RapidFire_Ticks, nMaxTicks);
		nTicks = std::min(nTicks, Shifting::nAvailableTicks);
		
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
			// ProjectileDodge warp
			if (F::ProjectileDodge->bWantWarp && Shifting::nAvailableTicks)
			{
				F::ProjectileDodge->bWantWarp = false;
				
				Shifting::bShifting = true;
				Shifting::bShiftingWarp = true;

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
			
			// Manual warp
			if (!I::MatSystemSurface->IsCursorVisible() && !I::EngineVGui->IsGameUIVisible() && H::Input->IsDown(CFG::Exploits_Warp_Key))
			{
				if (Shifting::nAvailableTicks)
				{
					Shifting::bShifting = true;
					Shifting::bShiftingWarp = true;

					if (CFG::Exploits_Warp_Mode == 0)
					{
						for (int n = 0; n < 2; n++)
							callOriginal(n == 1);
						Shifting::nAvailableTicks--;
					}

					if (CFG::Exploits_Warp_Mode == 1)
					{
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
