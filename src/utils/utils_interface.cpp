#include "utils.h"
#include "gameconfig.h"
#include "iserver.h"
#include "interfaces.h"
#include "cs2surf.h"
#include "ctimer.h"
#include "keyvalues3.h"
#include <filesystem.h>
#include "checksum_md5.h"
#include "sdk/serversideclient.h"
#include "sdk/gamerules.h"

#include "memdbgon.h"

#define MAX_VELOCITY_KEY "sv_maxvelocity "

static_global char currentMapMD5[33];
static_global bool md5NeedsUpdating {};

extern CGameConfig *g_pGameConfig;

CGameConfig *SurfUtils::GetGameConfig()
{
	return g_pGameConfig;
}

const CGlobalVars *SurfUtils::GetServerGlobals()
{
	return g_SurfPlugin.simulatingPhysics ? &(g_SurfPlugin.serverGlobals) : this->GetGlobals();
}

CGlobalVars *SurfUtils::GetGlobals()
{
	INetworkGameServer *server = g_pNetworkServerService->GetIGameServer();

	if (!server)
	{
		return nullptr;
	}

	return server->GetGlobals();
}

CBaseEntity *SurfUtils::FindEntityByClassname(CEntityInstance *start, const char *name)
{
	return utils::FindEntityByClassname(start, name);
}

CBasePlayerController *SurfUtils::GetController(CBaseEntity *entity)
{
	return utils::GetController(entity);
}

CBasePlayerController *SurfUtils::GetController(CPlayerSlot slot)
{
	return utils::GetController(slot);
}

CPlayerSlot SurfUtils::GetEntityPlayerSlot(CBaseEntity *entity)
{
	return utils::GetEntityPlayerSlot(entity);
}

void SurfUtils::SendConVarValue(CPlayerSlot slot, ConVarRefAbstract conVar, const char *value)
{
	utils::SendConVarValue(slot, conVar, value);
}

void SurfUtils::SendMultipleConVarValues(CPlayerSlot slot, ConVarRefAbstract **conVars, const char **values, u32 size)
{
	utils::SendMultipleConVarValues(slot, conVars, values, size);
}

f32 SurfUtils::NormalizeDeg(f32 a)
{
	return utils::NormalizeDeg(a);
}

f32 SurfUtils::GetAngleDifference(const f32 source, const f32 target, const f32 c, bool relative)
{
	return utils::GetAngleDifference(source, target, c, relative);
}

CGameEntitySystem *SurfUtils::GetGameEntitySystem()
{
	return GameEntitySystem();
}

void SurfUtils::AddTimer(CTimerBase *timer, bool preserveMapChange)
{
	if (preserveMapChange)
	{
		g_PersistentTimers.AddToTail(timer);
	}
	else
	{
		g_NonPersistentTimers.AddToTail(timer);
	}
}

void SurfUtils::RemoveTimer(CTimerBase *timer)
{
	FOR_EACH_VEC(g_PersistentTimers, i)
	{
		if (g_PersistentTimers.Element(i) == timer)
		{
			g_PersistentTimers.Remove(i);
			return;
		}
	}
	FOR_EACH_VEC(g_NonPersistentTimers, i)
	{
		if (g_NonPersistentTimers.Element(i) == timer)
		{
			g_NonPersistentTimers.Remove(i);
			return;
		}
	}
}

CUtlVector<CServerSideClient *> *SurfUtils::GetClientList()
{
	if (!g_pNetworkServerService)
	{
		return nullptr;
	}
	static_persist const int offset = g_pGameConfig->GetOffset("ClientOffset");
	return (CUtlVector<CServerSideClient *> *)((char *)g_pNetworkServerService->GetIGameServer() + offset);
}

CUtlString SurfUtils::GetCurrentMapName(bool *result)
{
	CNetworkGameServerBase *networkGameServer = (CNetworkGameServerBase *)g_pNetworkServerService->GetIGameServer();
	if (networkGameServer)
	{
		if (result)
		{
			*result = true;
		}
		return networkGameServer->GetMapName();
	}

	auto currentMap = g_SurfPlugin.GetCurrentMap();
	if (currentMap.length() > 0)
	{
		if (result)
		{
			*result = true;
		}
		return currentMap.data();
	}

	if (result)
	{
		*result = false;
	}
	return "";
}

u64 SurfUtils::GetCurrentMapWorkshopID()
{
	CUtlString directory = this->GetCurrentMapDirectory();
	if (directory.MatchesPattern("*workshop*"))
	{
		return atoll(directory.UnqualifiedFilenameAlloc());
	}
	return 0;
}

CUtlString SurfUtils::GetCurrentMapVPK()
{
	CUtlString map = this->GetCurrentMapName();
	if (map.IsEmpty())
	{
		return "";
	}
	CUtlVector<CUtlString> paths;
	char mapName[1024];

	g_SMAPI->PathFormat(mapName, sizeof(mapName), "maps/%s.vpk", map.Get());

	g_pFullFileSystem->FindFileAbsoluteList(paths, mapName, "GAME");
	if (paths.Count() > 0)
	{
		CUtlString realPath = paths[0];
		if (realPath.MatchesPattern("vpk:*"))
		{
			realPath = realPath.Slice((sizeof("vpk:") - 1));
			return realPath.StripFilename().StripExtension() + ".vpk";
		}
		return realPath;
	}
	return "";
}

CUtlString SurfUtils::GetCurrentMapDirectory()
{
	return this->GetCurrentMapVPK().DirName();
}

u64 SurfUtils::GetCurrentMapSize()
{
	CUtlString currentMap = this->GetCurrentMapVPK();
	u32 size = 0;
	if (g_pFullFileSystem->FileExists(currentMap.Get()))
	{
		size = g_pFullFileSystem->Size(currentMap.Get());
	}
	else
	{
		CUtlString newFileName = currentMap.StripExtension() + "_dir.vpk";
		size = g_pFullFileSystem->Size(newFileName.Get());
		for (u32 i = 0; g_pFullFileSystem->FileExists(newFileName.Get()); i++)
		{
			newFileName.Format("%s_%03i.vpk", currentMap.StripExtension().Get(), i);
			size += g_pFullFileSystem->Size(newFileName.Get());
		}
	}
	return size;
}

CUtlString SurfUtils::GetCurrentMapConfig()
{
	CUtlString map = this->GetCurrentMapName();
	if (map.IsEmpty())
	{
		return "";
	}
	CUtlVector<CUtlString> paths;
	char mapName[1024];

	g_SMAPI->PathFormat(mapName, sizeof(mapName), "cfg/maps/%s.cfg", map.Get());

	g_pFullFileSystem->FindFileAbsoluteList(paths, mapName, "GAME");

	if (paths.Count() > 0)
	{
		return paths[0];
	}
	return "";
}

u32 SurfUtils::GetCurrentMapMaxVelocity()
{
	FileHandle_t fp = g_pFullFileSystem->Open(this->GetCurrentMapConfig().Get(), "r");
	u32 maxVel = 0;
	if (fp)
	{
		int size = g_pFullFileSystem->Size(fp);
		char *buffer = new char[size + 1];

		g_pFullFileSystem->Read((void *)buffer, size, fp);
		buffer[size] = 0;
		g_pFullFileSystem->Close(fp);

		std::istringstream stream(buffer);
		std::string lineC;

		while (std::getline(stream, lineC))
		{
			// strip windows line endings
			if (!lineC.empty() && lineC.back() == '\r')
			{
				lineC.pop_back();
			}

			CUtlString line(lineC.c_str());
			if (line.MatchesPattern(CUtlString(MAX_VELOCITY_KEY) + "*") && utils::IsNumeric(line.Get() + strlen(MAX_VELOCITY_KEY)))
			{
				maxVel = atoi(line.Get() + strlen(MAX_VELOCITY_KEY));
				break;
			}
		}
		delete[] buffer;
	}
	return maxVel;
}

bool SurfUtils::UpdateCurrentMapMD5()
{
	return this->GetFileMD5(this->GetCurrentMapVPK(), currentMapMD5, sizeof(currentMapMD5));
}

bool SurfUtils::GetCurrentMapMD5(char *buffer, i32 size)
{
	if (md5NeedsUpdating)
	{
		md5NeedsUpdating = this->UpdateCurrentMapMD5();
	}
	if (!md5NeedsUpdating)
	{
		V_strncpy(buffer, currentMapMD5, size);
		return true;
	}
	return false;
}

bool SurfUtils::GetFileMD5(const char *filePath, char *buffer, i32 size)
{
	u8 chunk[8192];
	FileHandle_t file = g_pFullFileSystem->OpenEx(filePath, "rb");
	if (file)
	{
		i64 sizeRemaining = g_pFullFileSystem->Size(file);
		i32 bytesRead;
		MD5Context_t ctx;
		unsigned char digest[MD5_DIGEST_LENGTH];
		memset(&ctx, 0, sizeof(MD5Context_t));

		MD5Init(&ctx);
		while (sizeRemaining > 0)
		{
			bytesRead = g_pFullFileSystem->Read(chunk, sizeRemaining > sizeof(chunk) ? sizeof(chunk) : sizeRemaining, file);
			sizeRemaining -= bytesRead;
			if (bytesRead > 0)
			{
				MD5Update(&ctx, chunk, bytesRead);
			}
			if (g_pFullFileSystem->EndOfFile(file))
			{
				g_pFullFileSystem->Close(file);
				file = NULL;
				break;
			}
			else if (!g_pFullFileSystem->IsOk(file))
			{
				if (file)
				{
					g_pFullFileSystem->Close(file);
				}
				return false;
			}
		}

		MD5Final(digest, &ctx);
		char *data = MD5_Print(digest, sizeof(digest));
		V_strncpy(buffer, data, size);
		if (file)
		{
			g_pFullFileSystem->Close(file);
		}
		return true;
	}

	// Try and get the MD5 for multifile vpks.
	if (V_strlen(filePath) >= 4 && SURF_STREQI(filePath + V_strlen(filePath) - 4, ".vpk"))
	{
		i32 index = 0;
		CUtlString originalPath = filePath;
		CUtlString newFile = originalPath.StripExtension() + "_dir.vpk";
		file = g_pFullFileSystem->OpenEx(newFile.Get(), "rb");
		if (file)
		{
			i64 sizeRemaining = g_pFullFileSystem->Size(file);
			i32 bytesRead;
			MD5Context_t ctx;
			unsigned char digest[MD5_DIGEST_LENGTH];
			memset(&ctx, 0, sizeof(MD5Context_t));

			MD5Init(&ctx);
			while (sizeRemaining > 0)
			{
				bytesRead = g_pFullFileSystem->Read(chunk, sizeRemaining > sizeof(chunk) ? sizeof(chunk) : sizeRemaining, file);
				sizeRemaining -= bytesRead;
				if (bytesRead > 0)
				{
					MD5Update(&ctx, chunk, bytesRead);
				}
				if (g_pFullFileSystem->EndOfFile(file))
				{
					g_pFullFileSystem->Close(file);
					newFile.Format("%s_%03i.vpk", originalPath.StripExtension().Get(), index);
					file = g_pFullFileSystem->OpenEx(newFile.Get(), "rb");
					if (!file)
					{
						break;
					}
					sizeRemaining = g_pFullFileSystem->Size(file);
					index++;
				}
				else if (!g_pFullFileSystem->IsOk(file))
				{
					if (file)
					{
						g_pFullFileSystem->Close(file);
					}
					return false;
				}
			}

			MD5Final(digest, &ctx);
			char *data = MD5_Print(digest, sizeof(digest));
			V_strncpy(buffer, data, size);
			if (file)
			{
				g_pFullFileSystem->Close(file);
			}
			return true;
		}
		return false;
	}
	return false;
}

CCSGameRules *SurfUtils::GetGameRules()
{
	CCSGameRulesProxy *proxy = NULL;
	proxy = (CCSGameRulesProxy *)utils::FindEntityByClassname(proxy, "cs_gamerules");
	if (proxy)
	{
		return proxy->m_pGameRules();
	}
	return nullptr;
}

u32 SurfUtils::GetPlayerCount()
{
	u32 count = 0;
	auto clients = this->GetClientList();
	if (clients)
	{
		FOR_EACH_VEC(*clients, i)
		{
			CServerSideClient *client = clients->Element(i);

			if (client && client->IsConnected() && !client->IsFakeClient() && !client->IsHLTV())
			{
				count++;
			}
		}
	}
	return count;
}

void SurfUtils::AddTriangleOverlay(Vector const &p1, Vector const &p2, Vector const &p3, u8 r, u8 g, u8 b, u8 a, bool noDepthTest, f64 flDuration)
{
	void *debugoverlay = CALL_VIRTUAL(void *, g_pGameConfig->GetOffset("GetDebugOverlay"), interfaces::pServer);
	if (debugoverlay)
	{
		CALL_VIRTUAL(void, g_pGameConfig->GetOffset("DebugTriangle"), debugoverlay, &p1, &p2, &p3, r, g, b, a, noDepthTest, flDuration);
	}
}

void SurfUtils::ClearOverlays()
{
	void *debugoverlay = CALL_VIRTUAL(void *, g_pGameConfig->GetOffset("GetDebugOverlay"), interfaces::pServer);
	if (debugoverlay)
	{
		CALL_VIRTUAL(void, g_pGameConfig->GetOffset("ClearOverlays"), debugoverlay);
	}
}
