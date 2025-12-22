# TF2 Shotgun Damage Prediction System

## Overview

This system implements accurate damage prediction for TF2 shotguns based on the Source SDK damage formulas. It enables smart shooting decisions for both double-tap and normal aimbot modes.

## TF2 Damage Falloff Formula (from Source SDK)

```
Distance-based damage ramp:
- 0 units (point blank): 150% damage (ramp up)
- 512 units (optimal): 100% damage
- 1024 units (max falloff): 50% damage (ramp down)

Linear interpolation between these points.
```

### Crit Damage
- **Critical Hit**: Base damage × 3.0 (ignores distance falloff)
- **Mini-Crit**: Base damage × 1.35 (no falloff below 100%, only ramp-up applies)

## Shotgun Stats

| Weapon | Damage/Pellet | Pellets | Total Base |
|--------|---------------|---------|------------|
| Scattergun | 6 | 10 | 60 |
| Force-A-Nature | 5.4 | 10 | 54 |
| Shortstop | 12 | 4 | 48 |
| Soda Popper | 6 | 10 | 60 |
| Baby Face's Blaster | 6 | 10 | 60 |
| Back Scatter | 6 | 10 | 60 |
| Stock Shotgun | 6 | 10 | 60 |
| Reserve Shooter | 6 | 10 | 60 (mini-crits airborne) |
| Family Business | 4.5 | 10 | 45 |
| Panic Attack | 6 | 10 | 60 |
| Frontier Justice | 6 | 10 | 60 |
| Widowmaker | 6 | 10 | 60 |

## Smart Shooting Flow

### Double Tap Mode
```
1. Check if user can double tap (stored ticks >= required ticks)
2. Check if rapid fire is ready
3. If YES to both:
   a. Get shotgun type and stats
   b. Calculate damage per pellet with distance falloff
   c. Apply crit/mini-crit multipliers if applicable
   d. Estimate pellets that can hit target (from no-spread data)
   e. If pellets can kill OR >= 50% threshold → SHOOT
   f. Otherwise → WAIT for more pellets
```

### Normal Aimbot Mode (No Double Tap)
```
1. Check if double tap is NOT available
2. If NO double tap:
   a. Get shotgun type and stats
   b. Calculate damage per pellet with distance falloff
   c. Apply crit/mini-crit multipliers if applicable
   d. Estimate pellets that can hit target (from no-spread data)
   e. If pellets can kill OR >= 10% threshold → SHOOT
   f. Otherwise → WAIT for more pellets
```

## Special Cases

### Reserve Shooter Mini-Crit
The Reserve Shooter mini-crits targets that are:
- Not on ground (FL_ONGROUND flag not set)
- In blast jump condition (TF_COND_BLASTJUMPING)
- Knocked into air (TF_COND_KNOCKED_INTO_AIR)

### Fixed Spread Pattern
When `tf_use_fixed_weaponspreads` is enabled, pellets follow a fixed pattern:
```cpp
// 10 pellets - Square pattern
Vec3(0, 0, 0),          // Center
Vec3(1, 0, 0),          // Right
Vec3(-1, 0, 0),         // Left
Vec3(0, -1, 0),         // Down
Vec3(0, 1, 0),          // Up
Vec3(0.85, -0.85, 0),   // Bottom-right
Vec3(0.85, 0.85, 0),    // Top-right
Vec3(-0.85, -0.85, 0),  // Bottom-left
Vec3(-0.85, 0.85, 0),   // Top-left
Vec3(0, 0, 0),          // Center (reward for fine aim)
```

### SeedPred Integration
When `tf_use_fixed_weaponspreads` is disabled (random spread), the system uses SeedPred to predict pellet positions:
- Gets the predicted seed from `F::SeedPred->GetSeed()`
- Uses `SDKUtils::RandomSeed()` and `SDKUtils::RandomFloat()` to calculate spread
- First pellet can be "perfect" (no spread) if weapon hasn't fired in 1.25+ seconds
- Traces each pellet to determine which will hit the target

## Integration

### With Existing Hitscan Aimbot
Add to `AimbotHitscan::ShouldFire()`:

```cpp
#include "../../ShotgunDamagePrediction/SmartShotgunAimbot.h"

// In ShouldFire(), before returning true:
if (IsShotgunWeapon(pWeapon))
{
    SmartShotgunConfig config;
    config.nDoubleTapTicksRequired = CFG::Exploits_RapidFire_Ticks;
    config.nDoubleTapTicksStored = Shifting::nAvailableTicks;
    config.bDoubleTapEnabled = CFG::Exploits_RapidFire_Active;
    config.flDoubleTapMinPelletPercent = 0.5f;  // 50%
    config.flNormalMinPelletPercent = 0.1f;     // 10%
    config.bCanCrit = pLocal->IsCritBoosted();
    config.bHoldingCritKey = /* your crit key check */;
    config.bRapidFireReady = F::RapidFire->IsReady();
    
    auto decision = g_SmartShotgun.ShouldShoot(pLocal, pWeapon, pTarget, config);
    if (!decision.bShouldShoot)
        return false;
}
```

## Files

- `ShotgunDamagePrediction.h/cpp` - Core damage calculation
- `SmartShotgunAimbot.h/cpp` - Smart shooting decision logic
