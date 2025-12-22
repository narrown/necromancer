#pragma once

#include "../../../SDK/SDK.h"
#include <deque>

class CSeedPred
{
	bool m_Synced{false};
	float m_ServerTime{0.0f};
	float m_PrevServerTime{0.0f};
	float m_AskTime{0.0f};
	float m_GuessTime{0.0f};
	float m_SyncOffset{0.0f};
	bool m_WaitingForPP{false};
	float m_GuessDelta{0.0f};
	float m_ResponseTime{0.0f};

	// Amalgam-style extras
	std::deque<double> m_TimeDeltas{};
	float m_MantissaStep{0.0f};
	int m_CachedSeed{0};

public:
	void AskForPlayerPerf();
	bool ParsePlayerPerf(bf_read& msgData);
	int GetSeed();
	int GetSeedForCmd(const CUserCmd* cmd);
	int GetCachedSeed() const { return m_CachedSeed; }
	void Reset();
	void AdjustAngles(CUserCmd* cmd);
	void Paint();
};

MAKE_SINGLETON_SCOPED(CSeedPred, SeedPred, F);
