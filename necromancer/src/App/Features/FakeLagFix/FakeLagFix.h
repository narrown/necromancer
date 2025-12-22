#pragma once
#include "../../../SDK/SDK.h"

// Fakelag detection and timing system for HITSCAN weapons
// Detects enemies using fakelag and waits for optimal shot timing
// Temporarily disables interpolation when shooting fakelagging targets

constexpr int FAKELAG_MAX_TICKS = 24;
constexpr int FAKELAG_MIN_SAMPLES = 3;        // Min samples before confident detection (fast detection)
constexpr int FAKELAG_HISTORY_SIZE = 16;      // History of choke patterns
constexpr float FAKELAG_DETECTION_THRESHOLD = 0.5f; // 50% confidence to flag as fakelag

enum class EFakeLagType
{
	None,
	Plain,      // Consistent choke amount
	Random,     // Random choke amounts
	Adaptive    // Changes based on situation
};

struct ChokeSample_t
{
	int nChokeTicks = 0;
	float flTime = 0.f;
	bool bWasMoving = false;
	float flVelocity = 0.f;
};

struct FakeLagData_t
{
	// Detection state
	bool bIsFakeLagging = false;
	EFakeLagType eType = EFakeLagType::None;
	float flConfidence = 0.f;
	
	// Timing prediction
	int nPredictedChoke = 0;
	int nCurrentChoke = 0;
	float flLastUpdateTime = 0.f;
	float flLastSimTime = 0.f;
	int nLastUpdateTick = 0;  // Track actual tick count for accurate detection
	
	// Pattern analysis
	std::deque<ChokeSample_t> vChokeHistory = {};
	int nAverageChoke = 0;
	int nMinChoke = 0;
	int nMaxChoke = 0;
	float flChokeVariance = 0.f;
	
	// Shot timing
	int nTicksSinceUpdate = 0;
	int nConsecutiveFakeLagDetections = 0;
	int nLastDetectedChoke = 0;  // Last choke amount when they unchoked
	
	void Reset()
	{
		bIsFakeLagging = false;
		eType = EFakeLagType::None;
		flConfidence = 0.f;
		nPredictedChoke = 0;
		nCurrentChoke = 0;
		flLastUpdateTime = 0.f;
		flLastSimTime = 0.f;
		nLastUpdateTick = 0;
		vChokeHistory.clear();
		nAverageChoke = 0;
		nMinChoke = 0;
		nMaxChoke = 0;
		flChokeVariance = 0.f;
		nTicksSinceUpdate = 0;
		nConsecutiveFakeLagDetections = 0;
		nLastDetectedChoke = 0;
	}
};

class CFakeLagFix
{
private:
	std::unordered_map<int, FakeLagData_t> m_mPlayerData = {};
	
	// Temporary interp disable state
	bool m_bInterpDisabledForShot = false;
	int m_nInterpDisableTarget = 0;
	
	void UpdatePlayerData(C_TFPlayer* pPlayer);
	void AnalyzePattern(FakeLagData_t& data);
	bool IsLikelyNetworkLag(const FakeLagData_t& data);
	int PredictNextUnchoke(const FakeLagData_t& data);
	
public:
	void Update();
	void Reset();
	
	// Main interface for hitscan aimbot
	bool ShouldWaitForShot(C_TFPlayer* pTarget);
	bool IsOptimalShotTiming(C_TFPlayer* pTarget);
	
	// Interp control for shooting fakelaggers
	void OnPreShot(C_TFPlayer* pTarget);   // Call before shooting
	void OnPostShot();                      // Call after shooting
	bool ShouldDisableInterp(C_BaseEntity* pEntity); // Hook check
	
	// Info getters
	bool IsPlayerFakeLagging(C_TFPlayer* pPlayer);
	EFakeLagType GetFakeLagType(C_TFPlayer* pPlayer);
	float GetConfidence(C_TFPlayer* pPlayer);
	int GetPredictedChoke(C_TFPlayer* pPlayer);
	int GetCurrentChoke(C_TFPlayer* pPlayer);
	const FakeLagData_t* GetPlayerData(C_TFPlayer* pPlayer);
	
	// Get max detected fakelag - useful for knowing if fake latency is needed
	int GetMaxDetectedFakeLag();
	
	// Debug
	void Draw();
};

MAKE_SINGLETON_SCOPED(CFakeLagFix, FakeLagFix, F);
