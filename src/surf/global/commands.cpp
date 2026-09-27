#include <string_view>

#include "common.h"
#include "surf_global.h"
#include "surf/language/surf_language.h"
#include "surf/mode/surf_mode.h"
#include "surf/style/surf_style.h"
#include "surf/option/surf_option.h"
#include "utils/http.h"
#include "utils/simplecmds.h"

static_function std::string_view MakeStatusString(bool checkmark)
{
	using namespace std::literals::string_view_literals;
	return checkmark ? "{green}✓{default}"sv : "{darkred}✗{default}"sv;
}

/* TODO: global
SCMD(surf_globalcheck, SCFL_GLOBAL | SCFL_MAP | SCFL_PLAYER)
{
	SurfPlayer *player = g_pSurfPlayerManager->ToPlayer(controller);

	if (SurfGlobalService::IsAvailable())
	{
		bool apiStatus = true;
		bool serverStatus = true;
		bool mapStatus = SurfGlobalService::WithCurrentMap([](const Surf::API::Map *currentMap) { return currentMap != nullptr; });
		bool playerStatus = !player->globalService->playerInfo.isBanned;

		// clang-format off
		bool modeStatus = SurfGlobalService::WithGlobalModes([&](const auto& globalModes)
		{
			Surf::API::Mode mode;
			if (Surf::API::DecodeModeString(player->modeService->GetModeShortName(), mode))
			{
				SurfModeManager::ModePluginInfo modeInfo = Surf::mode::GetModeInfo(player->modeService->GetModeShortName());

				for (const auto &globalMode : globalModes)
				{
#ifdef _WIN32
					const std::string& checksum = globalMode.windowsChecksum;
#else
					const std::string& checksum = globalMode.linuxChecksum;
#endif

					if (mode == globalMode.mode && SURF_STREQ(modeInfo.md5, checksum.c_str()))
					{
						return true;
					}
				}
			}

			return false;
		});
		// clang-format on

		// clang-format off
		bool styleStatus = SurfGlobalService::WithGlobalStyles([&](const auto& globalStyles)
		{
			FOR_EACH_VEC(player->styleServices, i)
			{
				if (!styleStatus)
				{
					return false;
				}

				Surf::API::Style style;

				if (!Surf::API::DecodeStyleString(player->styleServices[i]->GetStyleShortName(), style))
				{
					return false;
				}

				auto styleInfo = Surf::style::GetStyleInfo(player->styleServices[i]);
				bool found = false;

				for (const auto &globalStyle : globalStyles)
				{
#ifdef _WIN32
					const std::string& checksum = globalStyle.windowsChecksum;
#else
					const std::string& checksum = globalStyle.linuxChecksum;
#endif

					if (style == globalStyle.style && SURF_STREQ(styleInfo.md5, checksum.c_str()))
					{
						found = true;
						break;
					}
				}

				if (!found)
				{
					return false;
				}
			}

			return true;
		});
		// clang-format on

		// clang-format off
		player->languageService->PrintChat(true, false, "Global Check",
				MakeStatusString(apiStatus),
				MakeStatusString(serverStatus),
				MakeStatusString(mapStatus),
				MakeStatusString(playerStatus),
				MakeStatusString(modeStatus),
				MakeStatusString(styleStatus));
		// clang-format on
	}
	else
	{
		HTTP::Request request(HTTP::Method::GET, SurfOptionService::GetOptionStr("apiUrl", "https://api.placeholder.org"));
		auto callback = [player](HTTP::Response response)
		{
			std::string_view apiStatus = MakeStatusString(response.status == 200);
			std::string_view serverStatus = MakeStatusString(false);
			std::string_view mapStatus = MakeStatusString(false);
			std::string_view playerStatus = MakeStatusString(false);
			std::string_view modeStatus = MakeStatusString(false);
			std::string_view styleStatus = MakeStatusString(false);

			if (SurfGlobalService::MayBecomeAvailable())
			{
				mapStatus = "{yellow}?";
				playerStatus = "{yellow}?";
				modeStatus = "{yellow}?";
				styleStatus = "{yellow}?";
			}

			// clang-format off
			player->languageService->PrintChat(true, false, "Global Check",
					apiStatus,
					serverStatus,
					mapStatus,
					playerStatus,
					modeStatus,
					styleStatus);
			// clang-format on
		};
		request.Send(callback);
	}

	return true;
}

SCMD_LINK(surf_gc, surf_globalcheck);
*/
