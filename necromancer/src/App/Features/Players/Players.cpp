#include "Players.h"

void CPlayers::Parse()
{
	// Init player data file path
	if (m_LogPath.empty())
	{
		m_LogPath = U::Storage->GetWorkFolder() / "players.json";

		if (!exists(m_LogPath))
		{
			std::ofstream file(m_LogPath, std::ios::app);

			if (!file.is_open())
			{
				return;
			}

			file.close();
		}
	}

	if (!m_Players.empty())
	{
		return;
	}

	// Open the file
	std::ifstream logFile(m_LogPath);
	if (!logFile.is_open() || logFile.peek() == std::ifstream::traits_type::eof())
	{
		return;
	}

	// Load all players
	nlohmann::json j = nlohmann::json::parse(logFile);
	for (const auto& item : j.items())
	{
		const auto key = HASH_RT(item.key().c_str());
		auto& playerEntry = j[item.key()];

		m_Players[key] = {
			playerEntry["ignored"].get<bool>(),
			playerEntry["cheater"].get<bool>(),
			playerEntry["retardlegit"].get<bool>()
		};
	}
}

void CPlayers::Mark(int entindex, const PlayerPriority& info)
{
	if (entindex == I::EngineClient->GetLocalPlayer())
	{
		return;
	}

	player_info_t playerInfo{};
	if (!I::EngineClient->GetPlayerInfo(entindex, &playerInfo) || playerInfo.fakeplayer)
	{
		return;
	}

	auto steamID = HASH_RT(std::string_view(playerInfo.guid).data());
	m_Players[steamID] = info;

	// Load the current playerlist
	nlohmann::json j{};
	std::ifstream readFile(m_LogPath);
	if (readFile.is_open() && readFile.peek() != std::ifstream::traits_type::eof())
	{
		readFile >> j;
	}

	readFile.close();

	// Open the output file
	std::ofstream outFile(m_LogPath);
	if (!outFile.is_open())
	{
		return;
	}

	auto& playerEntry = j[playerInfo.guid];
	playerEntry["ignored"] = info.Ignored;
	playerEntry["cheater"] = info.Cheater;
	playerEntry["retardlegit"] = info.RetardLegit;

	if (!info.Ignored && !info.Cheater && !info.RetardLegit)
	{
		j.erase(std::string(playerInfo.guid));
	}

	outFile << std::setw(4) << j;
	outFile.close();
}

bool CPlayers::GetInfo(int entindex, PlayerPriority& out)
{
	if (entindex == I::EngineClient->GetLocalPlayer())
	{
		return false;
	}

	player_info_t playerInfo{};
	if (!I::EngineClient->GetPlayerInfo(entindex, &playerInfo) || playerInfo.fakeplayer)
	{
		return false;
	}

	return GetInfoGUID(playerInfo.guid, out);
}

bool CPlayers::GetInfoGUID(const std::string& guid, PlayerPriority& out)
{
	const auto steamID = HASH_RT(guid.c_str());

	if (auto it = m_Players.find(steamID); it != std::end(m_Players))
	{
		const auto& [key, value]{ *it };
		out = value;
		return true;
	}

	return false;
}

void CPlayers::ImportLegacyPlayers()
{
	// Check if legacy seonwdde players.json exists
	const auto legacyPath = std::filesystem::current_path() / "SEOwnedDE" / "players.json";
	
	if (!std::filesystem::exists(legacyPath))
		return;

	// Check if we already have players (don't overwrite)
	if (!m_Players.empty())
		return;

	// Load legacy players
	std::ifstream legacyFile(legacyPath);
	if (!legacyFile.is_open() || legacyFile.peek() == std::ifstream::traits_type::eof())
		return;

	try
	{
		nlohmann::json legacyJson = nlohmann::json::parse(legacyFile);
		legacyFile.close();

		// Import each player
		for (const auto& item : legacyJson.items())
		{
			const auto key = HASH_RT(item.key().c_str());
			auto& playerEntry = legacyJson[item.key()];

			m_Players[key] = {
				playerEntry["ignored"].get<bool>(),
				playerEntry["cheater"].get<bool>(),
				playerEntry["retardlegit"].get<bool>()
			};
		}

		// Save to new location
		std::ofstream outFile(m_LogPath);
		if (outFile.is_open())
		{
			outFile << std::setw(4) << legacyJson;
			outFile.close();
		}
	}
	catch (...)
	{
		// Failed to parse legacy file, ignore
	}
}
