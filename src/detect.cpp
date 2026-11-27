#include "detect.h"
#include "util.h"
#include <set>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace kt {

static const wchar_t* kSystemWhitelist[] = {
    L"smss.exe", L"csrss.exe", L"wininit.exe", L"services.exe",
    L"lsass.exe", L"winlogon.exe", L"fontdrvhost.exe", L"dwm.exe",
    L"system", L"registry", L"secure system", L"memory compression",
    nullptr
};

static const wchar_t* kAntiCheatWhitelist[] = {
    L"beservice.exe", L"beservice_x64.exe", L"beservice_x86.exe",
    L"easyanticheat.exe", L"easyanticheat_eos.exe", L"eacrashreporter.exe",
    L"easyanticheat_launcher.exe", L"easyanticheat_sys.exe",
    L"vgc.exe", L"vgtray.exe", L"vanguard.exe", L"vgcclient.exe",
    L"vgo.dll", L"vgo_service.exe", L"vgksvc.exe",
    L"nvcontainer.exe", L"nvcontainerdisplay.exe",
    L"faceit.exe", L"faceitclient.exe", L"faceitac.exe", L"faceitfilechecker.exe",
    L"faceitchat.exe", L"faceitclient_beta.exe",
    L"anticheatexpert.exe", L"anticheataiclient.exe",
    L"anticheatlauncher.exe", L"anticheatclient.exe",
    L"nprotect.exe", L"xigncode.exe", L"xigncode3.exe", L"wellbia.exe",
    L"hoyo_protect.exe", L"hipen.dll", L"gameguard.des", L"gamemon.exe",
    L"eac_launcher.exe", L"eacsys.exe",
    L"eseaclient.exe", L"eseadriver.exe", L"sguard64.exe", L"sguard.exe",
    L"sguard_x64.exe", L"sguardx64.exe", L"sguardload64.exe",
    L"doraemon.exe", L"tp3helper.exe", L"tp3svc.exe", L"gameguard.exe",
    L"egac.exe", L"egacclient.exe", L"aceanticheat.exe", L"aceanticheatclient.exe",
    L"punkbuster.exe", L"pbsvc.exe", L"pbls.exe",
    L"cornerswrapper.exe", L"ricochet.exe", L"ricochetlauncher.exe",
    nullptr
};

static const wchar_t* kAntiCheatPatterns[] = {
    L"anticheat", L"easyanticheat", L"battleye", L"beservice", L"bes_", L"_be.exe",
    L"faceit", L"vanguard", L"vgk", L"vgtray", L"esea", L"xigncode", L"nprotect",
    L"gameguard", L"gamemon", L"wellbia", L"hoyo_protect", L"hipen_",
    L"ricochet", L"punkbuster", L"hackshield", L"tenprotect",
    L"sguard", L"tp3helper", L"tp3svc", L"egac", L"aceanticheat", L"javelin",
    nullptr
};

static const wchar_t* kSecurityWhitelist[] = {
    L"msmpeng.exe", L"nissrv.exe", L"securityhealthservice.exe", L"securityhealthsystray.exe",
    L"avp.exe", L"avpui.exe", L"avgsvc.exe", L"avguard.exe", L"avastui.exe",
    L"wrsa.exe", L"mbamservice.exe", L"mbamtray.exe", L"ekrn.exe", L"bdagent.exe",
    L"savservice.exe", L"sophoshealth.exe", L"ccsvchst.exe", L"nortonsecurity.exe",
    nullptr
};

static bool IsLauncherProcess(const std::wstring& exeLower) {
    static const wchar_t* launchers[] = {
        L"steam.exe", L"steamwebhelper.exe", L"epicgameslauncher.exe", L"epicwebhelper.exe",
        L"riotclientservices.exe", L"riotclientux.exe", L"riotclientuxrender.exe", L"leagueclient.exe",
        L"goggalaxy.exe", L"galaxyclient.exe", L"gamingservices.exe", L"xboxpcapp.exe",
        L"battle.net.exe", L"agent.exe", L"ubisoftconnect.exe", L"upc.exe", L"eadesktop.exe",
        L"origin.exe", L"ealauncher.exe", L"ealaunchhelper.exe", L"minecraftlauncher.exe",
        L"prismlauncher.exe", L"robloxplayerlauncher.exe", L"robloxstudio.exe", L"hoyoplay.exe",
        L"accstarrail.exe", L"playgtav.exe", L"fortnitelauncher.exe", nullptr
    };
    for (int i = 0; launchers[i]; ++i) if (exeLower == launchers[i]) return true;
    return false;
}

bool IsSystemWhitelisted(const std::wstring& exeLower) {
    for (int i = 0; kSystemWhitelist[i]; i++)
        if (exeLower == kSystemWhitelist[i]) return true;
    return false;
}

bool IsAntiCheatWhitelisted(const std::wstring& exeLower) {
    for (int i = 0; kAntiCheatWhitelist[i]; i++)
        if (exeLower == kAntiCheatWhitelist[i]) return true;
    for (int i = 0; kAntiCheatPatterns[i]; i++)
        if (exeLower.find(kAntiCheatPatterns[i]) != std::wstring::npos) return true;
    return false;
}

bool IsKernelAntiCheatGame(const std::wstring& exeLower) {
    static const wchar_t* patterns[] = {
        L"valorant",
        L"cod", L"callofduty", L"modernwarfare", L"warzone",
        L"fortniteclient",
        L"r5apex", L"apexlegends",
        L"rustclient", L"rust.exe",
        L"tarkov",
        L"tslgame", L"pubg",
        L"rainbow",
        L"dayz",
        L"deadbydaylight",
        L"hunt",
        L"thefinals",
        L"narakabladepoint",
        L"marvelrivals",
        L"deltaforce",
        L"battlefield",
        L"robloxplayerbeta", L"robloxplayer",
        L"gta5", L"gtav_enhanced",
        L"redm.exe",
        L"fragpunk",
        nullptr
    };
    for (int i = 0; patterns[i]; i++)
        if (exeLower.find(patterns[i]) != std::wstring::npos) return true;
    return false;
}

bool IsAntiCheatProtectedGame(const std::wstring& exeLower) {
    static const wchar_t* extra[] = {
        L"cs2", L"csgo", L"counter-strike", L"counterstrike",
        L"deadlock", L"spectredivide", L"spectre divide",
        nullptr
    };
    if (IsKernelAntiCheatGame(exeLower)) return true;
    for (int i = 0; extra[i]; i++)
        if (exeLower.find(extra[i]) != std::wstring::npos) return true;
    return false;
}

int AntiCheatKind() {
    enum Kind {
        None = 0, EAC = 1, BattlEye = 2, Vanguard = 3, Competition = 4,
        AsianAC = 5, Ricochet = 6, Tencent = 7, Other = 8
    };
    static const wchar_t* kVanguard[] = { L"vanguard", L"vgtray", L"vgksvc", nullptr };
    const auto& snap = GetCachedSnapshot();
    int found = None;
    for (const auto& e : snap.processes) {
        if (IsSystemWhitelisted(e.exeLower)) continue;
        if (IsKernelAntiCheatGame(e.exeLower)) continue;
        if (e.exeLower.find(L"easyanticheat") != std::wstring::npos ||
            e.exeLower.find(L"eac_") != std::wstring::npos)      { found = (std::max)(found, (int)EAC); continue; }
        if (e.exeLower.find(L"battleye") != std::wstring::npos ||
            e.exeLower.find(L"beservice") != std::wstring::npos) { found = (std::max)(found, (int)BattlEye); continue; }
        for (int k = 0; kVanguard[k]; k++)
            if (e.exeLower.find(kVanguard[k]) != std::wstring::npos) { found = (std::max)(found, (int)Vanguard); }
        if (e.exeLower.find(L"faceit") != std::wstring::npos ||
            e.exeLower.find(L"esea") != std::wstring::npos)      { found = (std::max)(found, (int)Competition); continue; }
        if (e.exeLower.find(L"nprotect") != std::wstring::npos ||
            e.exeLower.find(L"xigncode") != std::wstring::npos ||
            e.exeLower.find(L"gameguard") != std::wstring::npos ||
            e.exeLower.find(L"hoyo_protect") != std::wstring::npos) { found = (std::max)(found, (int)AsianAC); continue; }
        if (e.exeLower.find(L"ricochet") != std::wstring::npos)  { found = (std::max)(found, (int)Ricochet); continue; }
        if (e.exeLower.find(L"anticheatexpert") != std::wstring::npos ||
            e.exeLower.find(L"sguard") != std::wstring::npos ||
            e.exeLower.find(L"tp3helper") != std::wstring::npos ||
            e.exeLower.find(L"egac") != std::wstring::npos)      { found = (std::max)(found, (int)Tencent); continue; }
        if (IsAntiCheatWhitelisted(e.exeLower))                  { found = (std::max)(found, (int)Other); continue; }
    }
    return found;
}

bool IsAntiCheatActive() {
    return AntiCheatKind() != 0;
}

bool IsSecurityWhitelisted(const std::wstring& exeLower) {
    for (int i = 0; kSecurityWhitelist[i]; i++)
        if (exeLower == kSecurityWhitelist[i]) return true;
    return false;
}

bool IsProtectedByConfig(const Config& cfg, const std::wstring& exeLower) {
    for (const auto& r : cfg.processes)
        if (r.whitelist_only() && r.exe == exeLower) return true;
    return false;
}

static const std::unordered_map<std::wstring, std::wstring>& EpicManifestExes();

static const wchar_t* kKnownGames[] = {
    L"cs2.exe",
    L"fortniteclient-win64-shipping.exe",
    L"rocketleague.exe",
    L"valorant-win64-shipping.exe",
    L"fivem.exe", L"fivem_gtaprocess.exe",
    L"minecraft.exe", L"minecraftwindows.exe", L"minecraftlauncher.exe",
    L"prismlauncher.exe",
    L"fortniteclient.exe", L"fortnitelauncher.exe",
    L"payday2_win32_release.exe", L"payday2_win32_release_x86.exe",
    L"gta5.exe", L"gta5_enhanced.exe", L"playgtav.exe", L"gtaiv.exe", L"gta_sa.exe",
    L"rdr2.exe", L"reddead2.exe",
    L"cyberpunk2077.exe",
    L"eldenring.exe", L"darksoulsiii.exe", L"sekiro.exe",
    L"overwatch.exe", L"overwatch2.exe",
    L"league of legends.exe", L"leagueclient.exe", L"riotclient.exe",
    L"worldofwarcraft.exe", L"wowclassic.exe", L"wow.exe", L"wow-64.exe",
    L"starfield.exe", L"skyrimse.exe", L"skyrimvr.exe", L"fallout4.exe",
    L"escapefromtarkov.exe", L"tarkov.exe",
    L"rustclient.exe", L"rust.exe",
    L"baldursgate3.exe", L"bg3_dx11.exe",
    L"forzahorizon5.exe", L"forzahorizon4.exe", L"forzamotorsport.exe",
    L"assettocorsa.exe", L"assettocorsacompetizione.exe",
    L"projectcars2.exe", L"pcars2.exe",
    L"f1_23.exe", L"f1_24.exe", L"f1_25.exe",
    L"grid.exe", L"dirt5.exe",
    L"dota2.exe", L"hl2.exe", L"portal2.exe", L"left4dead2.exe",
    L"counter-strike_global_offensive.exe",
    L"apexlegends.exe", L"r5apex.exe",
    L"rainbow six siege.exe", L"rainbowsix.exe",
    L"destiny2.exe",
    L"warzone.exe", L"modernwarfare.exe", L"callofduty.exe", L"cod.exe",
    L"blackops.exe", L"bo4.exe", L"t6mp.exe",
    L"pubg.exe", L"tslgame.exe",
    L"deadbydaylight.exe",
    L"phasmophobia.exe",
    L"grounded.exe",
    L"valheim.exe",
    L"seaofthieves.exe",
    L"stalker2.exe",
    L"helldivers2.exe", L"helldivers.exe",
    L"warthunder.exe", L"warthunder_dx11.exe",
    L"warframe.exe", L"warframe.x64.exe",
    L"pathofexile.exe", L"pathofexile_x64.exe",
    L"diabloiv.exe", L"diablo4.exe", L"diablo iii.exe",
    L"finalfantasyxiv.exe", L"ffxiv_dx11.exe",
    L"monsterhunterworld.exe", L"monsterhunterwilds.exe",
    L"robloxplayerbeta.exe", L"robloxplayerlauncher.exe", L"robloxstudio.exe",
    L"freefire.exe",
    L"codm.exe", L"pubgm.exe",
    L"geometrydash.exe", L"gd.exe",
    L"terraria.exe", L"terraria.bin.x86_64",
    L"stardewvalley.exe",
    L"hollowknight.exe",
    L"celeste.exe",
    L"legion.exe",
    L"smite.exe",
    L"paladins.exe",
    L"warface.exe",
    L"crossfire.exe",
    L"pointblank.exe",
    L"combatarms.exe",
    L"suddenattack.exe",
    L"ava.exe",
    L"specialforce2.exe",
    L"cs16.exe", L"cstrike.exe", L"cstrike_orig.exe",
    L"csgo.exe", L"csgo_original.exe",
    L"aimlab.exe",
    L"kovaaaks.exe",
    L"osu!.exe", L"osu.exe",
    L"genshinimpact.exe", L"starrail.exe",
    L"zenlesszonezero.exe",
    L"tekken 8.exe", L"tekken8.exe", L"streetfighter6.exe",
    L"thefinals.exe", L"discovery-win64-shipping.exe",
    L"nba2k25.exe", L"nba2k24.exe", L"fifa25.exe", L"fc25.exe",
    L"farming simulator 25.exe", L"farmingsimulator25.exe",
    L"eurotrucksimulator2.exe", L"american truck simulator.exe",
    L"microsoftflight simulator.exe", L"flight simulator.exe",
    L"theisle.exe", L"arksurvivalevolved.exe", L"arkascended.exe",
    L"nightingale.exe", L"palworld.exe", L"enlisted.exe",
    L"overprime.exe", L"deadlock.exe",
    L"hunt showdown.exe", L"huntshowdown.exe",
    L"insurgency.exe", L"insurgencysandstorm.exe",
    L"ready or not.exe", L"readyornot.exe",
    L"ground branch.exe", L"zero hour.exe",
    L"squad.exe", L"hell let loose.exe", L"hellletloose.exe",
    L"post scriptum.exe", L"battlebit.exe", L"battlebit remastered.exe",
    L"marvel rivals.exe", L"marvelrivals.exe", L"valorant.exe",
    L"thecycle.exe", L"cyclefrontier.exe",
    L"farlight84.exe", L"naraka.exe", L"narakabladepoint.exe",
    L"eternal return.exe", L"ddr5.exe",
    L"kartriderdrift.exe", L"world of warships.exe", L"worldoftanks.exe", L"worldoftanks_blitz.exe",
    L"elder scrolls online.exe", L"eso.exe", L"eso64.exe",
    L"black desert.exe", L"bdo64.exe",
    L"lost ark.exe", L"lostark.exe", L"loster.exe",
    L"new world.exe", L"newworld.exe",
    L"throne and liberty.exe", L"throneandliberty.exe",
    L"tower of fantasy.exe", L"toweroffantasy.exe",
    L"jujutsu kaisen cursed clash.exe", L"dragon ball sparking zero.exe",
    L"guilty gear strive.exe", L"ggs.exe",
    L"mortal kombat 1.exe", L"mk1.exe", L"mortal kombat 11.exe", L"mk11.exe",
    L"black myth wukong.exe", L"blackmythwukong.exe",
    L"spacemarine2.exe", L"warhammer 40,000 space marine 2.exe",
    L"dragon age the veilguard.exe", L"dragonageveilguard.exe",
    L"kingdom come deliverance ii.exe", L"kc2.exe", L"kingdomcome2.exe",
    L"monsterhunterwilds.exe", L"monster hunter wilds.exe",
    L"elden ring nightreign.exe", L"nightreign.exe",
    L"deltaforce.exe", L"delta force.exe",
    L"pathofexile2.exe", L"path of exile 2.exe",
    L"ghostoftsushima.exe", L"ghost of tsushima.exe",
    L"the last of us part i.exe", L"tlou1.exe",
    L"ratchet & clank rift apart.exe",
    L"blackops6.exe", L"call of duty black ops 6.exe",
    L"fragpunk.exe",
    L"spectre divide.exe",
    L"aov.exe", L"arena of valor.exe",
    L"marvel's spider-man 2.exe", L"spiderman2.exe",
    L"silent hill 2.exe", L"silenthill2.exe",
    L"alien isolation.exe", L"deadspace.exe", L"dead space remake.exe",
    L"resident evil 4.exe", L"re4.exe",
    L"hollow knight silksong.exe", L"silksong.exe",
    L"assassin's creed shadows.exe", L"acshadows.exe",
    L"dungeonborne.exe",
    L"the horizon chase turbo.exe",
    L"doom.exe", L"doomdarkages.exe", L"doom the dark ages.exe",
    L"slay the spire 2.exe", L"slaythespire2.exe",
    L"the outer worlds 2.exe", L"outerworlds2.exe",
    L"fable.exe", L"fablegame.exe",
    L"IndianaJonesAndTheGreatCircle.exe", L"indiana jones and the great circle.exe",
    L"avowed.exe", L"avowedgame.exe",
    L"phantom-brave.exe", L"phantombrave.exe",
    L"maquette.exe", L"the medium.exe", L"medium.exe",
    L"scorn.exe", L"chernobylite.exe",
    L"green hell vr.exe", L"greenhellvr.exe",
    L"bonelab.exe", L"blade and sorcery.exe",
    L"zenless zone zero.exe", L"zzz.exe",
    L"genshinimpactcloudgame.exe", L"honkaistarrail.exe", L"honkai3rd.exe",
    L"starrail.exe", L"starrailcloudgame.exe",
    L"marvelsnap.exe", L"marvel snap.exe",
    L"skull and bones.exe", L"skullandbones.exe",
    L"sonsoftheforest.exe", L"sonsoftheforest_dx11.exe",
    L"theriftbreaker.exe", L"rune.exe",
    L"starcitizen.exe", L"star citizen.exe",
    L"robloxplayerlauncher.exe", L"robloxapp.exe", L"roblox.exe", L"robloxplayer.exe",
    L"ealauncher.exe", L"ealaunchhelper.exe",
    L"nightreign.exe", L"eldenringnightreign.exe",
    L"wutheringwaves.exe", L"wuthering waves.exe",
    L"accstarrail.exe", L"hoyoplay.exe",
    L"escapetarkov.exe", L"eft.exe",
    L"marvelrivals.exe", L"marvel rivals.exe",
    L"once human.exe", L"oncehuman.exe",
    L"the finals.exe", L"thefinals_win64_shipping.exe",
    L"heliushub.exe", L"palia.exe",
    L"contra.exe", L"contrareturns.exe",
    L"metallslug.exe", L"metal slug.exe",
    L"street fighter 6.exe", L"sf6.exe",
    L"tekken8.exe", L"tekken 8.exe",
    L"mortal kombat 2.exe", L"mk2.exe",
    L"hogwarts legacy.exe", L"hogwartslegacy.exe",
    L"cyberpunk2077.exe", L"cyberpunk2077_be.exe", L"cyberpunk.exe",
    L"witcher3.exe", L"witcher.exe", L"the witcher 3.exe",
    L"eastsidehockeymanager.exe", L"nhl25.exe", L"nhl24.exe",
    L"ufc5.exe", L"ea sports ufc.exe",
    L"ea sports fc.exe", L"easportsfc25.exe", L"fc25.exe",
    L"wwe2k25.exe", L"wwe2k.exe",
    L"dust.exe", L"concurrence.exe",
    L"wavesofthefatui.exe", L"azur lane.exe", L"gradecity.exe",
    L"expedition33.exe", L"clair obscure expedition 33.exe",
    L"hades2.exe", L"hades ii.exe",
    L"mafiaoldcountry.exe", L"mafia the old country.exe",
    L"lostsoulaside.exe", L"lost soul aside.exe",
    L"ghostofyotei.exe", L"ghost of yotei.exe",
    L"arcraiders.exe", L"arc raiders.exe",
    L"battlefield6.exe", L"bf6.exe", L"battlefield 6.exe",
    L"thealters.exe", L"the alters.exe",
    L"wuchang.exe", L"wuchang fallen feathers.exe",
    L"cronosnewdawn.exe", L"cronos the new dawn.exe",
    L"killingfloor3.exe", L"ready or not 2.exe",
    nullptr
};

const std::unordered_set<std::wstring>& KnownGameSet() {
    static const std::unordered_set<std::wstring> set = [] {
        std::unordered_set<std::wstring> s;
        s.reserve(256);
        for (int i = 0; kKnownGames[i]; i++) s.insert(kKnownGames[i]);
        return s;
    }();
    return set;
}

std::wstring MatchKnownGame(const std::wstring& exeLower) {
    const auto& set = KnownGameSet();
    auto it = set.find(exeLower);
    return it != set.end() ? *it : L"";
}

const std::vector<std::wstring>& KnownGameNames() {
    static std::vector<std::wstring> names;
    if (names.empty())
        for (int i = 0; kKnownGames[i]; i++) names.push_back(kKnownGames[i]);
    return names;
}

std::wstring MatchGameName(const std::wstring& exeLower, const Config& cfg) {
    std::wstring n = MatchKnownGame(exeLower);
    if (!n.empty()) return n;
    for (const auto& g : cfg.custom_games) {
        if (g == exeLower) return g;
        if (g.find(L'*') != std::wstring::npos && WildMatch(g.c_str(), exeLower.c_str()))
            return exeLower;
    }
    return L"";
}

std::wstring MatchEpicGame(const std::wstring& exeLower, const std::wstring& pathLower) {
    const auto& map = EpicManifestExes();
    auto it = map.find(exeLower);
    if (it == map.end()) return L"";
    if (pathLower.empty() || pathLower.find(it->second) == std::wstring::npos) return L"";
    return exeLower;
}

static const wchar_t* kGamePathRoots[] = {
    L"\\steamapps\\", L"\\steam\\steam.exe", L"steamapps\\common\\",
    L"\\epic games\\", L"\\epicgames\\",
    L"\\riot games\\", L"\\riot client\\",
    L"\\battle.net\\", L"\\blizzard\\",
    L"\\ubisoft\\", L"\\ubisoft game launcher\\", L"ubisoftconnect\\",
    L"\\origin games\\", L"\\ea games\\", L"\\electronic arts\\",
    L"\\gog galaxy\\", L"\\gog games\\", L"\\gog\\",
    L"\\amazon games\\", L"\\twitch\\games\\",
    L"\\program files\\windowsapps\\",
    L"\\windowsapps\\",
    L"\\game pass\\",
    L"\\unreal\\", L"\\unrealengine\\",
    nullptr
};

bool HeuristicGamePath(const std::wstring& pathLower) {
    if (pathLower.empty()) return false;
    for (int i = 0; kGamePathRoots[i]; i++)
        if (pathLower.find(kGamePathRoots[i]) != std::wstring::npos) return true;
    return false;
}

std::wstring GetProcPathByPid(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    wchar_t buf[MAX_PATH] = {};
    DWORD len = MAX_PATH;
    std::wstring out;
    if (QueryFullProcessImageNameW(h, 0, buf, &len)) {
        out.assign(buf, len);
    }
    CloseHandle(h);
    return WLower(out);
}

static const wchar_t* kEngineModules[] = {
    L"unityplayer.dll", L"libunity.dll", L"libmono.dll", L"unitycrashhandler64.dll",
    L"steamclient64.dll", L"steamclient.dll", L"steam_api64.dll", L"steam_api.dll",
    L"eossdk-win64-shipping.dll", L"eossdk.dll", L"riotgamesapi.dll",
    L"easyanticheat.dll", L"easyanticheat_eos.dll", L"beservice.dll",
    L"nvgamepad.dll", L"amdxc64.dll", L"amdags64.dll",
    L"godot_win64.exe", L"filesystem_stdio.dll", L"serverbrowser.dll",
    L"libgodot_windows_release_x86_64.exe", L"phasmophobia_win64.dll",
    nullptr
};

bool HeuristicEngineModules(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    HMODULE mods[1024];
    DWORD needed = 0;
    bool found = false;
    if (EnumProcessModulesEx(h, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
        int n = (int)(needed / sizeof(HMODULE));
        if (n > 1024) n = 1024;
        for (int i = 0; i < n && !found; i++) {
            wchar_t name[256];
            if (GetModuleBaseNameW(h, mods[i], name, 256)) {
                std::wstring low = WLower(name);
                for (int k = 0; kEngineModules[k]; k++) {
                    if (low == kEngineModules[k]) {
                        found = true;
                        break;
                    }
                }
            }
        }
    }
    CloseHandle(h);
    return found;
}

static const wchar_t* kGameCmdFlags[] = {
    L"-game=", L"/game=", L"-game :", L"/game :",
    L"-fullscreen", L"/fullscreen",
    L"-map=", L"/map=",
    L"-server", L"/server",
    L"-client", L"/client",
    L"-battle royale", L"/battle royale",
    nullptr
};

bool HeuristicCommandLine(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) return false;

    typedef NTSTATUS(NTAPI* pNtQIP)(HANDLE, UINT, PVOID, ULONG, PULONG);
    static auto NtQIP = (pNtQIP)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    if (!NtQIP) { CloseHandle(h); return false; }

    struct { ULONG_PTR PebBaseAddress; ULONG_PTR AffinityMask; } pbi = {};
    ULONG retLen = 0;
    NTSTATUS status = NtQIP(h, 0, &pbi, sizeof(pbi), &retLen);
    if (status != 0) { CloseHandle(h); return false; }

    struct { void* pad[2]; void* Ldr; void* ProcessParameters; } peb = {};
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(h, (PVOID)pbi.PebBaseAddress, &peb, sizeof(peb), &bytesRead)) {
        CloseHandle(h);
        return false;
    }

    struct { void* pad[14]; UNICODE_STRING CommandLine; } params = {};
    if (!ReadProcessMemory(h, peb.ProcessParameters, &params, sizeof(params), &bytesRead)) {
        CloseHandle(h);
        return false;
    }

    if (params.CommandLine.Length == 0 || params.CommandLine.Buffer == nullptr) {
        CloseHandle(h);
        return false;
    }

    wchar_t* cmdLine = new wchar_t[params.CommandLine.Length / sizeof(wchar_t) + 1];
    if (!ReadProcessMemory(h, params.CommandLine.Buffer, cmdLine,
                           params.CommandLine.Length, &bytesRead)) {
        delete[] cmdLine;
        CloseHandle(h);
        return false;
    }
    cmdLine[params.CommandLine.Length / sizeof(wchar_t)] = L'\0';

    std::wstring cmdLower = WLower(std::wstring(cmdLine, params.CommandLine.Length / sizeof(wchar_t)));
    delete[] cmdLine;
    CloseHandle(h);

    for (int i = 0; kGameCmdFlags[i]; i++) {
        if (cmdLower.find(kGameCmdFlags[i]) != std::wstring::npos)
            return true;
    }
    return false;
}

static const std::unordered_map<std::wstring, std::wstring>& EpicManifestExes() {
    static const std::unordered_map<std::wstring, std::wstring> map = [] {
        std::unordered_map<std::wstring, std::wstring> m;
        std::wstring dir = L"C:\\ProgramData\\Epic\\EpicGamesLauncher\\Data\\Manifests";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*.item").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return m;
        do {
            std::ifstream f(dir + L"\\" + fd.cFileName, std::ios::binary);
            if (!f.is_open()) continue;
            std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            auto readStr = [&data](const char* key) -> std::wstring {
                std::string k = std::string("\"") + key + "\":";
                size_t p = data.find(k);
                if (p == std::string::npos) return L"";
                size_t q1 = data.find('"', p + k.size());
                if (q1 == std::string::npos) return L"";
                size_t q2 = data.find('"', q1 + 1);
                if (q2 == std::string::npos) return L"";
                std::string s = data.substr(q1 + 1, q2 - q1 - 1);
                std::wstring out;
                out.reserve(s.size());
                for (size_t i = 0; i < s.size(); i++) {
                    if (s[i] == '\\' && i + 1 < s.size()) { i++; if (s[i] != '\\' && s[i] != '"') continue; }
                    out += s[i];
                }
                return WLower(out);
            };
            std::wstring loc = readStr("InstallLocation");
            std::wstring exe = readStr("LaunchExecutable");
            if (!exe.empty() && !loc.empty()) m[exe] = loc;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return m;
    }();
    return map;
}

bool DetectSteamGames(const std::wstring& steamRoot, std::vector<std::wstring>& outGamePaths) {
    std::wstring vdfPath = steamRoot + L"\\config\\libraryfolders.vdf";
    std::ifstream file(vdfPath);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        size_t pos = line.find("\"path\"");
        if (pos == std::string::npos) continue;
        pos = line.find('"', pos + 6);
        if (pos == std::string::npos) continue;
        size_t end = line.find('"', pos + 1);
        if (end == std::string::npos) continue;
        std::string pathStr = line.substr(pos + 1, end - pos - 1);
        std::wstring wpath(pathStr.begin(), pathStr.end());
        outGamePaths.push_back(wpath + L"\\steamapps\\common\\");
    }
    return !outGamePaths.empty();
}

bool HeuristicActiveWindow(DWORD fgPid, const std::wstring& exeLower) {
    if (fgPid == 0) return false;
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD winPid = 0;
    GetWindowThreadProcessId(fg, &winPid);
    if (winPid != fgPid) return false;

    static const wchar_t* kNonGameFg[] = {
        L"explorer.exe", L"devenv.exe", L"code.exe", L"chrome.exe",
        L"msedge.exe", L"firefox.exe", L"discord.exe", L"notepad.exe",
        L"cmd.exe", L"powershell.exe", L"pwsh.exe", L"windowsterminal.exe",
        L"spotify.exe", L"vlc.exe", L"mpv.exe", L"winamp.exe", L"wmplayer.exe",
        L"telegram.exe", L"whatsapp.exe", L"teams.exe", L"slack.exe",
        L"zoom.exe", L"obs64.exe", L"obs32.exe", L"obs.exe",
        L"outlook.exe", L"winword.exe", L"excel.exe", L"powerpnt.exe",
        L"acrobat.exe", L"acrord32.exe", L"photoshop.exe", L"illustrator.exe",
        L"figma.exe", L"everything.exe", L"procexp.exe", L"procexp64.exe",
        L"taskmgr.exe", L"regedit.exe", L"resmon.exe", L"perfmon.exe",
        L"musicbee.exe", L"foobar2000.exe", L"mpc-hc.exe", L"mpc-hc64.exe",
        L"potplayer.exe", L"potplayer64.exe", L"sharex.exe", L"greenshot.exe",
        nullptr
    };
    for (int i = 0; kNonGameFg[i]; i++)
        if (exeLower == kNonGameFg[i]) return false;

    if (GetMenu(fg)) return false;

    LONG_PTR style = GetWindowLongPtrW(fg, GWL_STYLE);
    if (style & WS_CHILD) return false;

    RECT wRect, dRect;
    if (GetWindowRect(fg, &wRect) && SystemParametersInfoW(SPI_GETWORKAREA, 0, &dRect, 0)) {
        if (EqualRect(&wRect, &dRect)) return true;
    }
    return true;
}

std::vector<DWORD> FindPidsByName(const std::wstring& exeLower) {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring n = WLower(pe.szExeFile);
            if (n == exeLower) pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

DWORD GetForegroundProcessId() {
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid;
}

static ProcessSnapshot s_cachedSnapshot;
static ULONGLONG s_snapshotTime = 0;
static const ULONGLONG SNAPSHOT_CACHE_MS = 500;

const ProcessSnapshot& GetCachedSnapshot() {
    ULONGLONG now = GetTickCount64();
    if (now - s_snapshotTime < SNAPSHOT_CACHE_MS && !s_cachedSnapshot.processes.empty())
        return s_cachedSnapshot;

    s_cachedSnapshot.processes.clear();
    s_cachedSnapshot.fgPid = GetForegroundProcessId();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                ProcessSnapshot::Entry e;
                e.pid = pe.th32ProcessID;
                e.parentPid = pe.th32ParentProcessID;
                e.exeLower = WLower(pe.szExeFile);
                e.pathLower = GetProcPathByPid(pe.th32ProcessID);
                s_cachedSnapshot.processes.push_back(e);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    s_snapshotTime = now;
    return s_cachedSnapshot;
}

std::vector<DetectionResult> DetectGamePidsEx(const Config& cfg,
                                               const std::wstring& forcedExe) {
    std::vector<DetectionResult> results;
    const auto& snap = GetCachedSnapshot();

    for (const auto& e : snap.processes) {
        DetectionResult dr;
        dr.pid = e.pid;

        if (!forcedExe.empty() && e.exeLower == forcedExe) {
            dr.name = forcedExe;
            dr.confidence = 100;
            dr.heuristic = true;
            dr.method = "forced_target";
            results.push_back(dr);
            continue;
        }

        if (IsSystemWhitelisted(e.exeLower) || IsAntiCheatWhitelisted(e.exeLower) ||
            IsSecurityWhitelisted(e.exeLower) || IsProtectedByConfig(cfg, e.exeLower)) continue;

        const bool foreground = snap.fgPid == e.pid;
        const bool gamePath = HeuristicGamePath(e.pathLower);
        bool launcherParent = false;
        for (const auto& parent : snap.processes) {
            if (parent.pid == e.parentPid && IsLauncherProcess(parent.exeLower)) {
                launcherParent = true;
                break;
            }
        }

        std::wstring game = MatchGameName(e.exeLower, cfg);
        const bool customGame = std::find(cfg.custom_games.begin(), cfg.custom_games.end(), e.exeLower) != cfg.custom_games.end();
        bool egsManifest = game.empty() && !MatchEpicGame(e.exeLower, e.pathLower).empty();
        if ((!game.empty() || egsManifest) && (!IsLauncherProcess(e.exeLower) || customGame)) {
            dr.name = egsManifest ? e.exeLower : game;
            if (customGame) {
                dr.confidence = 100;
                dr.heuristic = false;
                dr.method = "user_game";
                results.push_back(dr);
                continue;
            }
            const bool activeWindow = foreground && HeuristicActiveWindow(snap.fgPid, e.exeLower);
            if (gamePath || launcherParent || activeWindow) {
                dr.confidence = 85 + (gamePath ? 10 : 0) + (launcherParent ? 5 : 0);
                dr.confidence = min(dr.confidence, 100);
                dr.heuristic = false;
                dr.method = egsManifest ? "egs_manifest_verified" : "known_game_verified";
                results.push_back(dr);
                continue;
            }
        }

        if (IsLauncherProcess(e.exeLower)) continue;

        if (foreground) {
            if (!HeuristicActiveWindow(snap.fgPid, e.exeLower)) continue;
        } else if (!gamePath) {
            continue;
        }

        int conf = 0;
        std::string methods;

        if (HeuristicEngineModules(e.pid)) {
            conf += 40;
            methods += "engine_modules ";
        }

        if (gamePath) {
            conf += 35;
            methods += "game_path ";
        }

        if (HeuristicCommandLine(e.pid)) {
            conf += 25;
            methods += "cmd_line ";
        }

        if (launcherParent) {
            conf += 15;
            methods += "launcher_child ";
        }

        HWND fg = GetForegroundWindow();
        if (foreground && fg) {
            RECT wRect, dRect;
            if (GetWindowRect(fg, &wRect) && SystemParametersInfoW(SPI_GETWORKAREA, 0, &dRect, 0)) {
                if (EqualRect(&wRect, &dRect)) {
                    conf += 10;
                    methods += "fullscreen ";
                }
            }
        }

        if (!foreground) methods += "background_game ";

        const int threshold = foreground ? 50 : 60;
        if (conf >= threshold) {
            dr.name = e.exeLower;
            dr.confidence = min(conf, 100);
            dr.heuristic = true;
            dr.method = methods;
            results.push_back(dr);
        }
    }

    std::sort(results.begin(), results.end(),
        [](const DetectionResult& a, const DetectionResult& b) {
            return a.confidence > b.confidence;
        });

    return results;
}

}
