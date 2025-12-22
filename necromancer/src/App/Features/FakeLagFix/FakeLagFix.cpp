#include "FakeLagFix.h"
#include "../CFG.h"
#include "../LagRecords/LagRecords.h"

void CFakeLagFix::Reset()
{
	m_mPlayerData.clear();
	m_bInterpDisabledForShot = false;
	m_nInterpDisableTarget = 0;
}

void CFakeLagFix::Update()
{
	if (!CFG::Aimbot_Hitscan_FakeLagFix)
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag())
	{
		Reset();
		return;
	}

	m_bInterpDisabledForShot = false;
	m_nInterpDisableTarget = 0;

	std::vector<int> toRemove;

	for (const auto pEntity : H::Entities->GetGroup(EEntGroup::PLAYERS_ENEMIES))
	{
		if (!pEntity)
			continue;

		const auto pPlayer = pEntity->As<C_TFPlayer>();
		if (!pPlayer)
			continue;

		if (pPlayer->deadflag())
		{
			toRemove.push_back(pPlayer->entindex());
			continue;
		}

		UpdatePlayerData(pPlayer);
	}

	for (const int nIndex : toRemove)
		m_mPlayerData.erase(nIndex);
}

void CFakeLagFix::UpdatePlayerData(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return;

	const int nIndex = pPlayer->entindex();
	auto& data = m_mPlayerData[nIndex];

	const float flCurTime = I::GlobalVars->curtime;
	const float flSimTime = pPlayer->m_flSimulationTime();
	const float flTickInterval = I::GlobalVars->interval_per_tick;
	const auto pLocal = H::Entities->GetLocal();

	// Check if player updated (simtime changed)
	const bool bSimTimeChanged = fabsf(flSimTime - data.flLastSimTime) > 0.001f;

	if (!bSimTimeChanged)
	{
		// No update - increment current choke counter using our tick tracking
		if (pLocal && data.nLastUpdateTick > 0)
		{
			const int nLocalTick = pLocal->m_nTickBase();
			data.nCurrentChoke = nLocalTick - data.nLastUpdateTick;
			data.nCurrentChoke = std::clamp(data.nCurrentChoke, 0, FAKELAG_MAX_TICKS);
		}
		return;
	}

	// Player sent an update - calculate choke ticks
	// KEY FIX: Only use OUR stored flLastSimTime, not engine's m_flOldSimulationTime
	// This is how neo64 does it and avoids any engine clamping
	if (data.flLastSimTime > 0.f)
	{
		const float flSimDelta = flSimTime - data.flLastSimTime;
		int nChokedTicks = static_cast<int>(flSimDelta / flTickInterval + 0.5f);
		nChokedTicks = std::clamp(nChokedTicks, 0, FAKELAG_MAX_TICKS);

		// Store the last detected choke for display
		data.nLastDetectedChoke = nChokedTicks;

		if (nChokedTicks > 1)
		{
			ChokeSample_t sample;
			sample.nChokeTicks = nChokedTicks;
			sample.flTime = flCurTime;
			sample.flVelocity = pPlayer->m_vecVelocity().Length2D();
			sample.bWasMoving = sample.flVelocity > 10.f;

			data.vChokeHistory.push_front(sample);

			while (data.vChokeHistory.size() > FAKELAG_HISTORY_SIZE)
				data.vChokeHistory.pop_back();

			AnalyzePattern(data);
		}
	}

	// Reset choke and update tracking
	data.nCurrentChoke = 0;
	data.nLastUpdateTick = pLocal ? pLocal->m_nTickBase() : 0;
	data.flLastSimTime = flSimTime;
	data.flLastUpdateTime = flCurTime;
}

void CFakeLagFix::AnalyzePattern(FakeLagData_t& data)
{
	if (data.vChokeHistory.size() < FAKELAG_MIN_SAMPLES)
	{
		data.bIsFakeLagging = false;
		data.flConfidence = 0.f;
		return;
	}

	int nTotal = 0, nMin = INT_MAX, nMax = 0;
	int nHighChokeCount = 0, nMovingChokeCount = 0;

	for (const auto& sample : data.vChokeHistory)
	{
		nTotal += sample.nChokeTicks;
		nMin = std::min(nMin, sample.nChokeTicks);
		nMax = std::max(nMax, sample.nChokeTicks);

		if (sample.nChokeTicks > 2)
			nHighChokeCount++;
		if (sample.bWasMoving && sample.nChokeTicks > 2)
			nMovingChokeCount++;
	}

	const int nSamples = static_cast<int>(data.vChokeHistory.size());
	data.nAverageChoke = nTotal / nSamples;
	data.nMinChoke = nMin;
	data.nMaxChoke = nMax;

	// Skip network lag detection for now - if they're consistently choking high, it's fakelag
	const float flHighChokeRatio = static_cast<float>(nHighChokeCount) / nSamples;
	const float flMovingChokeRatio = nHighChokeCount > 0 ? static_cast<float>(nMovingChokeCount) / nHighChokeCount : 0.f;

	// Determine type
	EFakeLagType eType = EFakeLagType::None;
	if (flMovingChokeRatio > 0.7f)
		eType = EFakeLagType::Adaptive;
	else if (nMax - nMin > 6)
		eType = EFakeLagType::Random;
	else
		eType = EFakeLagType::Plain;

	// Fast detection: if we see consistent high choke, flag as fakelag
	if (flHighChokeRatio > 0.4f && data.nAverageChoke > 3)
	{
		data.bIsFakeLagging = true;
		data.eType = eType;
		data.flConfidence = flHighChokeRatio;
		data.nPredictedChoke = data.nMaxChoke; // Use max for prediction
	}
	else
	{
		data.bIsFakeLagging = false;
		data.eType = EFakeLagType::None;
		data.flConfidence = 0.f;
	}
}

// HYBRID APPROACH: Wait for unchoke, but with timeout
bool CFakeLagFix::IsOptimalShotTiming(C_TFPlayer* pTarget)
{
	if (!CFG::Aimbot_Hitscan_FakeLagFix || !pTarget)
		return true;

	const int nIndex = pTarget->entindex();
	auto it = m_mPlayerData.find(nIndex);
	if (it == m_mPlayerData.end())
		return true;

	const auto& data = it->second;

	if (!data.bIsFakeLagging)
		return true;

	// OPTIMAL: Just unchoked - shoot now!
	if (data.nCurrentChoke <= 1)
		return true;

	// TIMEOUT: If they've been choking longer than their max, shoot anyway
	// They might have changed their fakelag or something is wrong
	if (data.nCurrentChoke >= data.nMaxChoke)
		return true;

	// CLOSE TO UNCHOKE: If within 2 ticks of predicted unchoke, wait
	const int nTicksUntilUnchoke = data.nPredictedChoke - data.nCurrentChoke;
	if (nTicksUntilUnchoke > 0 && nTicksUntilUnchoke <= 3)
		return false; // Wait, they're about to unchoke

	// MID-CHOKE: Don't shoot, position is stale
	return false;
}

void CFakeLagFix::OnPreShot(C_TFPlayer* pTarget)
{
	if (!CFG::Aimbot_Hitscan_FakeLagFix || !pTarget)
		return;

	if (IsPlayerFakeLagging(pTarget))
	{
		m_bInterpDisabledForShot = true;
		m_nInterpDisableTarget = pTarget->entindex();
	}
}

void CFakeLagFix::OnPostShot()
{
	m_bInterpDisabledForShot = false;
	m_nInterpDisableTarget = 0;
}

bool CFakeLagFix::ShouldDisableInterp(C_BaseEntity* pEntity)
{
	if (!CFG::Aimbot_Hitscan_FakeLagFix || !pEntity)
		return false;

	if (m_bInterpDisabledForShot && pEntity->entindex() == m_nInterpDisableTarget)
		return true;

	if (pEntity->GetClassId() == ETFClassIds::CTFPlayer)
	{
		const int nIndex = pEntity->entindex();
		auto it = m_mPlayerData.find(nIndex);
		if (it != m_mPlayerData.end() && it->second.bIsFakeLagging)
			return true;
	}

	return false;
}

bool CFakeLagFix::ShouldWaitForShot(C_TFPlayer* pTarget)
{
	return !IsOptimalShotTiming(pTarget);
}

bool CFakeLagFix::IsPlayerFakeLagging(C_TFPlayer* pPlayer)
{
	if (!pPlayer) return false;
	auto it = m_mPlayerData.find(pPlayer->entindex());
	return it != m_mPlayerData.end() && it->second.bIsFakeLagging;
}

EFakeLagType CFakeLagFix::GetFakeLagType(C_TFPlayer* pPlayer)
{
	if (!pPlayer) return EFakeLagType::None;
	auto it = m_mPlayerData.find(pPlayer->entindex());
	return it != m_mPlayerData.end() ? it->second.eType : EFakeLagType::None;
}

float CFakeLagFix::GetConfidence(C_TFPlayer* pPlayer)
{
	if (!pPlayer) return 0.f;
	auto it = m_mPlayerData.find(pPlayer->entindex());
	return it != m_mPlayerData.end() ? it->second.flConfidence : 0.f;
}

int CFakeLagFix::GetPredictedChoke(C_TFPlayer* pPlayer)
{
	if (!pPlayer) return 0;
	auto it = m_mPlayerData.find(pPlayer->entindex());
	return it != m_mPlayerData.end() ? it->second.nPredictedChoke : 0;
}

int CFakeLagFix::GetCurrentChoke(C_TFPlayer* pPlayer)
{
	if (!pPlayer) return 0;
	auto it = m_mPlayerData.find(pPlayer->entindex());
	return it != m_mPlayerData.end() ? it->second.nCurrentChoke : 0;
}

const FakeLagData_t* CFakeLagFix::GetPlayerData(C_TFPlayer* pPlayer)
{
	if (!pPlayer) return nullptr;
	auto it = m_mPlayerData.find(pPlayer->entindex());
	return it != m_mPlayerData.end() ? &it->second : nullptr;
}

int CFakeLagFix::GetMaxDetectedFakeLag()
{
	int nMax = 0;
	for (const auto& [nIndex, data] : m_mPlayerData)
		if (data.bIsFakeLagging && data.nMaxChoke > nMax)
			nMax = data.nMaxChoke;
	return nMax;
}


void CFakeLagFix::Draw()
{
	if (!CFG::Aimbot_Hitscan_FakeLagFix_Indicator)
		return;

	if (!I::EngineClient || !I::EngineClient->IsInGame())
		return;

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->deadflag())
		return;

	if (m_mPlayerData.empty())
		return;

	if (CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot())
		return;

	int nFakelaggers = 0;
	for (const auto& [nIndex, data] : m_mPlayerData)
		if (data.bIsFakeLagging)
			nFakelaggers++;

	if (nFakelaggers == 0)
		return;

	int nY = 100;
	const int nX = 10;

	H::Draw->String(H::Fonts->Get(EFonts::Menu), nX, nY,
		Color_t{ 255, 200, 100, 255 }, POS_DEFAULT, "FakeLag Detected:");
	nY += 16;

	for (const auto& [nIndex, data] : m_mPlayerData)
	{
		if (!data.bIsFakeLagging)
			continue;

		if (nIndex <= 0 || nIndex > I::EngineClient->GetMaxClients())
			continue;

		player_info_t info = {};
		if (!I::EngineClient->GetPlayerInfo(nIndex, &info) || !info.name[0])
			continue;

		const char* szType = "Plain";
		if (data.eType == EFakeLagType::Random) szType = "Random";
		else if (data.eType == EFakeLagType::Adaptive) szType = "Adaptive";

		// Green = just unchoked (shoot!), Yellow = close to unchoke, Red = mid-choke
		Color_t color;
		if (data.nCurrentChoke <= 1)
			color = { 100, 255, 100, 255 }; // Green - SHOOT NOW
		else if (data.nCurrentChoke >= data.nPredictedChoke - 2)
			color = { 255, 255, 100, 255 }; // Yellow - about to unchoke
		else
			color = { 255, 100, 100, 255 }; // Red - mid-choke, waiting

		char szBuffer[256];
		snprintf(szBuffer, sizeof(szBuffer), "%s [%s] %d/%d (last:%d)",
			info.name, szType, data.nCurrentChoke, data.nMaxChoke, data.nLastDetectedChoke);

		H::Draw->String(H::Fonts->Get(EFonts::Menu), nX, nY, color, POS_DEFAULT, szBuffer);
		nY += 14;
	}

	// Warning for high fakelag
	const int nMaxFakeLag = GetMaxDetectedFakeLag();
	if (nMaxFakeLag >= 12 && CFG::Aimbot_Hitscan_Fake_Latency < 200.f)
	{
		nY += 6;
		char szWarning[128];
		snprintf(szWarning, sizeof(szWarning), "! High FL (%d) - Need 200+ms Fake Latency !", nMaxFakeLag);
		H::Draw->String(H::Fonts->Get(EFonts::Menu), nX, nY,
			Color_t{ 255, 50, 50, 255 }, POS_DEFAULT, szWarning);
	}
}
