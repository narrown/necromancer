# Amalgam Projectile Aimbot Port to SEOwnedDE

## Overview

This folder contains the ported Amalgam projectile aimbot system adapted for SEOwnedDE.

## Current Status

### Completed
- ✅ Type aliases (CTFPlayer -> C_TFPlayer, etc.)
- ✅ Class ID enum alias (ETFClassID -> ETFClassIds)
- ✅ Macro compatibility (ADD_FEATURE -> MAKE_SINGLETON_SCOPED)
- ✅ Vars namespace wrappers mapping to CFG::
- ✅ H::Entities helper wrapper
- ✅ SDK:: utility function wrappers
- ✅ G:: global state synchronization
- ✅ CPredictionCopy - Full implementation using game signature
- ✅ GetPredDescMap/GetIntermediateDataSize - Virtual function calls
- ✅ CTFGameRules with GetViewVectors - Signature-based access
- ✅ F::Backtrack stub for lag compensation
- ✅ Simplified Ticks (no doubletap, just shoot position tracking)
- ✅ Math::RotatePoint - Full Amalgam implementation with 3D rotation
- ✅ Math::SolveQuadratic/SolveCubic/SolveQuartic - Polynomial solvers
- ✅ CTraceFilterWorldAndPropsOnly - Amalgam trace filter implementation
- ✅ CTraceFilterCollideable - Full Amalgam implementation
- ✅ F::AmalgamAimbot.Store - Real path visualization support
- ✅ Entity helper functions (IsPlayer, IsWorld, IsSentrygun, etc.)

## Key Files

### Compatibility Layer
- `AmalgamCompat.h` - Type aliases, signatures, CPredictionCopy, CTFGameRules, etc.

### Core Components
- `AimbotGlobal/` - Target selection, priority, and ignore logic
- `AimbotProjectile/` - Main projectile aimbot logic
- `Simulation/MovementSimulation/` - Player movement prediction
- `Simulation/ProjectileSimulation/` - Projectile trajectory simulation (VPhysics)
- `EnginePrediction/` - Engine prediction for local player
- `Ticks/` - Simplified shoot position tracking

## Integration Steps

1. **Add to project**: Include all .cpp files in your Visual Studio project

2. **Update CFG.h**: Add the required config variables (see AmalgamCompat.h Vars namespace)

3. **Hook into CreateMove**: 
   ```cpp
   // Sync globals from SEOwnedDE
   G::SyncFromSEOwned();
   G::OriginalCmd = *pCmd;
   
   // Update ticks (saves shoot position)
   F::Ticks.CreateMove(pLocal, pWeapon, pCmd, &bSendPacket);
   
   // Store movement data for prediction
   F::AmalgamMoveSim->Store();
   
   // Run projectile aimbot
   if (pLocal && pWeapon)
       F::AimbotProjectile->Run(pLocal, pWeapon, pCmd);
   ```

## Type Mappings

| Amalgam | SEOwnedDE |
|---------|-----------|
| `CTFPlayer` | `C_TFPlayer` |
| `CTFWeaponBase` | `C_TFWeaponBase` |
| `CBaseEntity` | `C_BaseEntity` |
| `ETFClassID` | `ETFClassIds` |
| `ADD_FEATURE(X, Y)` | `MAKE_SINGLETON_SCOPED(X, Y, F)` |
| `H::Entities` | `g_AmalgamEntitiesExt` |
| `F::AimbotGlobal.X()` | `F::AimbotGlobal->X()` |

## Config Mappings

Amalgam uses `Vars::Aimbot::Projectile::X.Value` while SEOwnedDE uses `CFG::Aimbot_Projectile_X`.

The `AmalgamCompat.h` file provides wrapper structs that map between the two systems.

## Notes

- The projectile simulation uses VPhysics for accurate trajectory prediction
- Movement simulation predicts player positions based on recorded strafe patterns
- Splash prediction finds optimal ground/wall impact points for splash damage
- Tick manipulation (doubletap/warp) is NOT included - only basic functionality
- AutoAirblast is NOT ported - would require additional work

## SDK Modifications

The following files were modified in SEOwnedDE's SDK:

### SDK/Impl/TraceFilters/TraceFilters.h
- Added `CTraceFilterWorldAndPropsOnlyAmalgam` class (Amalgam-style)
- Note: Named differently to avoid conflict with existing `CTraceFilterWorldAndPropsOnly` in IEngineTrace.h

### SDK/Impl/TraceFilters/TraceFilters.cpp
- Added `CTraceFilterWorldAndPropsOnlyAmalgam::ShouldHitEntity()` implementation

## Math Functions

The `Math` namespace in `AmalgamCompat.h` provides Amalgam's exact implementations:

- `Math::RotatePoint(point, origin, angles)` - 3D point rotation using rotation matrix
- `Math::RemapVal(val, A, B, C, D, clamp)` - Value remapping with optional clamping
- `Math::SolveQuadratic(a, b, c)` - Quadratic equation solver
- `Math::SolveCubic(b, c, d)` - Cubic equation solver (normalized form)
- `Math::SolveQuartic(a, b, c, d, e)` - Quartic equation solver
