#include "SeedPred.h"

#include "../CFG.h"
#include <vector>
#include <algorithm>
#include <ranges>
#include <limits>
#include <cmath>
#include <deque>
#include <numeric>

// Pre-compute spread offsets for all 256 seeds
// Server uses: RandomSeed(iSeed); x = RandomFloat(-0.5, 0.5) + RandomFloat(-0.5, 0.5)
void CSeedPred::InitSpreadLUT()
{
	if (m_SpreadInit)
		return;

	for (int n = 0; n <= 255; n++)
	{
		SDKUtils::RandomSeed(n);
		const float x = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
		const float y = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
		m_SpreadOffsets[n] = { x, y };
	}

	m_SpreadInit = true;
}

// Calculate mantissa step - determines seed prediction reliability
// IEEE 754: float = sign(1) | exponent(8) | mantissa(23)
// As server uptime increases, float precision decreases, making seed prediction more reliable
// Server seed: *(int*)&(Plat_FloatTime() * 1000.0f) & 255
float CSeedPred::CalcMantissaStep(float val) const
{
	const float t = val * 1000.0f;
	const int i = std::bit_cast<int>(t);
	const int e = (i >> 23) & 0xFF; // Extract exponent
	// 2^(exponent - 127 - 23) = step between consecutive float values
	return std::powf(2.0f, static_cast<float>(e) - 150.0f);
}

void CSeedPred::AskForPlayerPerf()
{
	if (!CFG::Exploits_SeedPred_Active)
	{
		Reset();
		return;
	}

	// Initialize spread LUT on first call
	InitSpreadLUT();

	// Does the current weapon have spread?
	const auto weapon = H::Entities->GetWeapon();
	if (!weapon || H::AimUtils->GetWeaponType(weapon) != EWeaponType::HITSCAN || weapon->GetWeaponSpread() <= 0.0f)
	{
		Reset();
		return;
	}

	// Are we dead?
	if (const auto local = H::Entities->GetLocal())
	{
		if (local->deadflag())
		{
			Reset();
			return;
		}
	}

	// Already waiting for response?
	if (m_WaitingForPP)
		return;

	// Adaptive resync based on mantissa step
	// Lower step = less reliable = resync more often
	if (m_ServerTime > 0.0f)
	{
		const float timeSinceRecv = static_cast<float>(Plat_FloatTime()) - m_RecvTime;
		
		// Adaptive interval: resync more frequently when precision is low
		float interval = RESYNC_INTERVAL;
		if (m_MantissaStep < 1.0f)
			interval = 1.0f; // Very unreliable, resync constantly
		else if (m_MantissaStep < 4.0f)
			interval = 2.0f; // Somewhat reliable
		else if (m_MantissaStep < 16.0f)
			interval = 5.0f; // Reliable
		else
			interval = 10.0f; // Very reliable, server has been up for hours

		if (timeSinceRecv < interval)
			return;
	}

	// Request perf data
	I::ClientState->SendStringCmd("playerperf");
	m_AskTime = static_cast<float>(Plat_FloatTime());
	m_WaitingForPP = true;
}

bool CSeedPred::ParsePlayerPerf(bf_read& msgData)
{
	if (!CFG::Exploits_SeedPred_Active)
		return false;

	char rawMsg[256]{};
	msgData.ReadString(rawMsg, sizeof(rawMsg), true);
	msgData.Seek(0);

	std::string msg(rawMsg);
	msg.erase(msg.begin()); // STX

	// playerperf format: "servertime ticks ents simtime frametime vel velocity"
	// Example: "12345.67 1000 50 0.015 0.015 vel 320.00"
	std::smatch matches{};
	std::regex_match(msg, matches, std::regex(R"((\d+.\d+)\s\d+\s\d+\s\d+.\d+\s\d+.\d+\svel\s\d+.\d+)"));

	if (matches.size() == 2)
	{
		m_WaitingForPP = false;
		m_RecvTime = static_cast<float>(Plat_FloatTime());

		const float newServerTime = std::stof(matches[1].str());

		if (newServerTime > m_ServerTime)
		{
			m_PrevServerTime = m_ServerTime;
			m_ServerTime = newServerTime;

			// Calculate round-trip time and estimate one-way latency
			const float rtt = m_RecvTime - m_AskTime;
			
			// Server time was recorded when processing our command, so we need to account for:
			// 1. Time for our request to reach server (~rtt/2)
			// 2. Server processing time (~1 tick)
			// Delta = ServerTime - ClientTimeWhenServerProcessed
			// ClientTimeWhenServerProcessed ≈ AskTime + rtt/2
			// So: Delta ≈ ServerTime - (AskTime + rtt/2) = ServerTime - AskTime - rtt/2
			// But we want: PredictedServerTime = ClientTime + Delta
			// Which means: Delta = ServerTime - AskTime + rtt/2 (to predict future server time)
			
			const double measuredDelta = static_cast<double>(m_ServerTime - m_AskTime) + 
			                             static_cast<double>(rtt * 0.5f) + 
			                             static_cast<double>(TICK_INTERVAL);
			
			// Average deltas to reduce jitter from network variance
			// Use weighted average - recent samples matter more
			m_TimeDeltas.push_back(measuredDelta);
			while (m_TimeDeltas.size() > 30)
				m_TimeDeltas.pop_front();

			// Weighted average: newer samples have more weight
			double weightedSum = 0.0;
			double totalWeight = 0.0;
			size_t idx = 0;
			for (const auto& delta : m_TimeDeltas)
			{
				const double weight = static_cast<double>(idx + 1); // 1, 2, 3, ... n
				weightedSum += delta * weight;
				totalWeight += weight;
				idx++;
			}
			const double avgDelta = totalWeight > 0.0 ? (weightedSum / totalWeight) : measuredDelta;
			m_SyncOffset = static_cast<float>(avgDelta);

			// Calculate mantissa step - higher = more reliable prediction
			// Step >= 1.0 means seed changes at most once per millisecond
			// Step >= 4.0 means seed changes at most once per 4ms (very reliable)
			m_MantissaStep = CalcMantissaStep(m_ServerTime);
			m_Synced = (m_MantissaStep >= 1.0f);
		}

		return true;
	}

	return std::regex_match(msg, std::regex(R"(\d+.\d+\s\d+\s\d+)"));
}

int CSeedPred::GetSeed() const
{
	// Server seed calculation (from TF2 source):
	// When sv_usercmd_custom_random_seed is enabled (default on Valve servers):
	// float flTime = Plat_FloatTime() * 1000.0f;
	// int iSeed = *(int*)&flTime & 255;
	
	const double dTime = static_cast<double>(Plat_FloatTime()) + static_cast<double>(m_SyncOffset);
	const float flTime = static_cast<float>(dTime * 1000.0);
	return std::bit_cast<int32_t>(flTime) & 255;
}

int CSeedPred::GetSeedForCmd(const CUserCmd* cmd)
{
	static ConVar* sv_usercmd_custom_random_seed = I::CVar->FindVar("sv_usercmd_custom_random_seed");
	if (sv_usercmd_custom_random_seed && sv_usercmd_custom_random_seed->GetBool())
	{
		// Server uses time-based seed
		return GetSeed();
	}
	// Fallback: engine MD5-based per-command seed
	return cmd ? (cmd->random_seed & 255) : GetSeed();
}

void CSeedPred::Reset()
{
	m_Synced = false;
	m_ServerTime = 0.0f;
	m_PrevServerTime = 0.0f;
	m_AskTime = 0.0f;
	m_RecvTime = 0.0f;
	m_SyncOffset = 0.0f;
	m_WaitingForPP = false;
	m_MantissaStep = 0.0f;
	m_TimeDeltas.clear();
}

void CSeedPred::AdjustAngles(CUserCmd* cmd)
{
	static ConVar* sv_usercmd_custom_random_seed = I::CVar->FindVar("sv_usercmd_custom_random_seed");
	static ConVar* tf_use_fixed_weaponspreads = I::CVar->FindVar("tf_use_fixed_weaponspreads");
	const bool bTimeBased = sv_usercmd_custom_random_seed && sv_usercmd_custom_random_seed->GetBool();
	const bool bFixedSpread = tf_use_fixed_weaponspreads && tf_use_fixed_weaponspreads->GetBool();

	// For time-based seeding we need sync; for MD5-per-cmd we don't
	if (!CFG::Exploits_SeedPred_Active || !cmd || !G::bFiring || (bTimeBased && !m_Synced))
		return;

	const auto local = H::Entities->GetLocal();
	if (!local)
		return;

	const auto weapon = H::Entities->GetWeapon();
	if (!weapon || H::AimUtils->GetWeaponType(weapon) != EWeaponType::HITSCAN)
		return;

	float spread = weapon->GetWeaponSpread();
	if (spread <= 0.0f)
		return;

	auto bulletsPerShot = weapon->GetWeaponInfo()->GetWeaponData(TF_WEAPON_PRIMARY_MODE).m_nBulletsPerShot;
	bulletsPerShot = static_cast<int>(SDKUtils::AttribHookValue(static_cast<float>(bulletsPerShot), "mult_bullets_per_shot", weapon));

	const int seed = GetSeedForCmd(cmd);
	m_CachedSeed = seed;

	// Perfect shot logic from TF2 source (tf_fx_shared.cpp):
	// Multi-pellet: flTimeSinceLastShot > 0.25f
	// Single-pellet: flTimeSinceLastShot > 1.25f
	const float timeSinceLastShot = (local->m_nTickBase() * TICK_INTERVAL) - weapon->m_flLastFireTime();
	
	// Weapon-specific tuning
	const int weaponID = weapon->GetWeaponID();
	const bool isPistol = (weaponID == TF_WEAPON_PISTOL || weaponID == TF_WEAPON_PISTOL_SCOUT);
	const bool isMinigun = (weaponID == TF_WEAPON_MINIGUN);
	const bool isSMG = (weaponID == TF_WEAPON_SMG);
	
	// Minigun has dynamic spread - 1.5x during first second of firing (from TF2 source)
	// We can't easily track firing duration here, so we assume worst case for correction
	// This is handled by the game's spread value already
	
	float perfectShotThreshold = 1.25f;
	if (isPistol) perfectShotThreshold = 0.5f;
	else if (isSMG) perfectShotThreshold = 0.25f; // SMG recovers faster
	
	const bool perfectShot = (bulletsPerShot == 1 && timeSinceLastShot > perfectShotThreshold) ||
	                         (bulletsPerShot > 1 && timeSinceLastShot > 0.25f);

	// Fixed spread patterns (competitive mode) - no seed prediction needed!
	// Server uses predetermined pellet positions, not random
	if (bFixedSpread && bulletsPerShot > 1)
	{
		// In fixed spread mode, first pellet always goes center
		// Just aim normally - no correction needed
		return;
	}

	// Single-bullet weapons - direct angle correction
	if (bulletsPerShot == 1)
	{
		if (perfectShot)
			return; // First bullet has no spread after idle

		// Use pre-computed LUT when possible
		Vec2 multiplier;
		if (seed <= 255 && m_SpreadInit)
		{
			multiplier = m_SpreadOffsets[seed];
		}
		else
		{
			SDKUtils::RandomSeed(seed);
			multiplier.x = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
			multiplier.y = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
		}

		Vec3 forward{}, right{}, up{};
		Math::AngleVectors(cmd->viewangles, &forward, &right, &up);

		// Calculate where the bullet will actually go with this spread
		const Vec3 spreadDir = forward + (right * multiplier.x * spread) + (up * multiplier.y * spread);
		Vec3 spreadAngles{};
		Math::VectorAngles(spreadDir, spreadAngles);

		// Reverse the spread: aim where we need to so spread lands on target
		// If spread pushes bullet to spreadAngles, we need to aim at (2*target - spreadAngles)
		cmd->viewangles = (cmd->viewangles * 2.0f) - spreadAngles;
		Math::ClampAngles(cmd->viewangles);
		G::bSilentAngles = true;
		return;
	}

	// Multi-bullet weapons (shotguns) - find bullet closest to average spread
	// Also consider neighboring seeds (±1) to account for timing jitter
	std::vector<Vec3> bulletDirections{};
	Vec3 averageDir{};

	// Try current seed and neighbors if mantissa step is low (unreliable timing)
	const int seedOffset = (m_MantissaStep < 4.0f) ? 1 : 0;
	int bestSeedOffset = 0;
	float bestSpreadScore = std::numeric_limits<float>::max();

	for (int tryOffset = -seedOffset; tryOffset <= seedOffset; tryOffset++)
	{
		std::vector<Vec3> tryDirections{};
		Vec3 tryAverage{};
		const int trySeed = (seed + tryOffset + 256) % 256;

		for (int bullet = 0; bullet < bulletsPerShot; bullet++)
		{
			const int bulletSeed = (trySeed + bullet) % 256;

			Vec2 multiplier;
			if (bulletSeed <= 255 && m_SpreadInit)
			{
				multiplier = m_SpreadOffsets[bulletSeed];
			}
			else
			{
				SDKUtils::RandomSeed(bulletSeed);
				multiplier.x = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
				multiplier.y = SDKUtils::RandomFloat(-0.5f, 0.5f) + SDKUtils::RandomFloat(-0.5f, 0.5f);
			}

			if (bullet == 0 && perfectShot)
				multiplier = {0.0f, 0.0f};

			Vec3 forward{}, right{}, up{};
			Math::AngleVectors(cmd->viewangles, &forward, &right, &up);

			Vec3 bulletDir = forward + (right * multiplier.x * spread) + (up * multiplier.y * spread);
			bulletDir.NormalizeInPlace();
			tryAverage += bulletDir;
			tryDirections.push_back(bulletDir);
		}

		tryAverage /= static_cast<float>(bulletsPerShot);

		// Score this seed by how tight the spread is (lower = better)
		float spreadScore = 0.0f;
		for (const auto& dir : tryDirections)
			spreadScore += dir.DistTo(tryAverage);

		if (spreadScore < bestSpreadScore)
		{
			bestSpreadScore = spreadScore;
			bestSeedOffset = tryOffset;
			bulletDirections = std::move(tryDirections);
			averageDir = tryAverage;
		}
	}

	// Update cached seed if we picked a neighbor
	if (bestSeedOffset != 0)
		m_CachedSeed = (seed + bestSeedOffset + 256) % 256;

	// Find the bullet closest to average - this maximizes damage potential
	const auto bestBullet = std::ranges::min_element(bulletDirections,
		[&](const Vec3& lhs, const Vec3& rhs)
		{
			return lhs.DistTo(averageDir) < rhs.DistTo(averageDir);
		});

	if (bestBullet == bulletDirections.end())
		return;

	Vec3 bestAngles{};
	Math::VectorAngles(*bestBullet, bestAngles);

	// Apply correction to aim where the best bullet will land on target
	const Vec3 correction = cmd->viewangles - bestAngles;
	cmd->viewangles += correction;
	Math::ClampAngles(cmd->viewangles);

	G::bSilentAngles = true;
}


void CSeedPred::Paint()
{
	if (!CFG::Exploits_SeedPred_Active || I::EngineVGui->IsGameUIVisible() || SDKUtils::BInEndOfMatch() || m_ServerTime <= 0.0f)
		return;

	// Anti-Screenshot
	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
		return;

	// Indicator
	if (CFG::Exploits_SeedPred_DrawIndicator)
	{
		const std::chrono::hh_mm_ss time{std::chrono::seconds(static_cast<int>(m_ServerTime))};

		int x = 2;
		int y = 2;

		H::Draw->String(
			H::Fonts->Get(EFonts::ESP_SMALL),
			x, y,
			{200, 200, 200, 255}, POS_DEFAULT,
			std::format("{}h {}m {}s (step {:.0f})", time.hours().count(), time.minutes().count(), time.seconds().count(), m_MantissaStep).c_str()
		);

		y += 10;

		H::Draw->String(
			H::Fonts->Get(EFonts::ESP_SMALL),
			x, y,
			!m_Synced ? Color_t{250, 130, 49, 255} : Color_t{32, 191, 107, 255}, POS_DEFAULT,
			!m_Synced ? "syncing.." : std::format("synced ({:.3f})", m_SyncOffset).c_str()
		);

		y += 10;

		H::Draw->String(
			H::Fonts->Get(EFonts::ESP_SMALL),
			x, y,
			{200, 200, 200, 255}, POS_DEFAULT,
			std::format("seed: {}", GetSeed()).c_str()
		);
	}
}
