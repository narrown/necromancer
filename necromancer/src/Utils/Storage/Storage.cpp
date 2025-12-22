#include "Storage.h"
#include <Windows.h>
#include <shlobj.h>

#include "../Assert/Assert.h"

bool AssureDirectory(const std::filesystem::path& path)
{
	if (!exists(path))
	{
		return create_directories(path);
	}

	return true;
}

void CStorage::Init(const std::string& folderName)
{
	// Necromancer saves to C:\necromancer_tf2
	m_WorkFolder = "C:\\necromancer_tf2";
	Assert(AssureDirectory(m_WorkFolder))

	m_ConfigFolder = m_WorkFolder / "Configs";
	Assert(AssureDirectory(m_ConfigFolder))

	// Legacy seonwdde config folder for loading old configs
	m_LegacyConfigFolder = std::filesystem::current_path() / "SEOwnedDE" / "Configs";
}

bool CStorage::HasLegacyConfigs()
{
	if (!std::filesystem::exists(m_LegacyConfigFolder))
		return false;

	for (const auto& entry : std::filesystem::directory_iterator(m_LegacyConfigFolder))
	{
		if (entry.path().extension() == ".json")
			return true;
	}
	return false;
}
