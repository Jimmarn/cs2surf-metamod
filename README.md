# [This repository is now hosted on Codeberg](https://codeberg.org/rcnoob/cs2surf-metamod)
## Mirror pushes will remain for visibility, please direct any issues/prs to the new repo

WIP, not ready for release 

# Requirements

- [Metamod 2.0.0](https://www.metamodsource.net/downloads.php/?branch=master) build 1459 or later

- Optional: [MultiAddonManager](https://github.com/Source2ZE/MultiAddonManager/releases/) v1.4.10, unused currently but may include menu addons in the future

- Optional: [SQL_MM](https://github.com/zer0k-z/sql_mm/releases) v1.3.4.4 or later for local database support

# Map voting

Built in, no CounterStrikeSharp needed. Players use:

| Command | What it does |
|---|---|
| `!vote` (`!rtv`) | Vote to change the map. When enough players agree (default 60%) a map vote starts. `!unvote` withdraws. |
| `!nominate <map>` (`!nom`) | Put a map on the next vote. Partial names work (`!nom utopia`). |
| `!1` .. `!6` | Pick an option while a vote is open (also shown in the HUD panel). |
| `!nextmap`, `!timeleft`, `!maps` | Next map / time left / print the map pool to the console. |

A vote also starts automatically a few minutes before `mp_timelimit` runs out, with an "extend map" option.

- Map pool: `cfg/cs2surf-maplist.txt`, one map per line (`name` or `name:workshopid`). Reload with `surf_vote_reload_maplist`.
- Settings: the `vote` block in `cfg/cs2surf-server-config.txt`.
- Admins can start a vote from the console with `surf_vote_start`.

# Installation:

- Download the latest version in the release section and extract them to your server's `csgo/` directory.

# Compilation
- Remember to *recursively* clone the plugin, and symlink needs to be enabled as well! ([this isn't the default on windows](https://stackoverflow.com/a/59761201))
   ```
   git clone -c core.symlinks=true --recursive https://github.com/rcnoob/cs2surf-metamod.git
   ```
- Latest [AMBuild](https://github.com/alliedmodders/ambuild/) needs to be installed for compilation.

- For each platform:
  
Windows (ambuild/msvc): 
```
mkdir build
cd build
python3 ../configure.py 
ambuild
``` 

For windows debugging with VS, build the project then add the following command at the end:
```
python3 ../configure.py --gen=vs --vs-version 17
``` 

Linux (ambuild/clang):
```
mkdir build
cd build
python3 ../configure.py 
ambuild
``` 

Linux (Docker w/ Valve SDK Image):
```
mkdir build
docker build -t cs2surf-linux-builder .
docker run --rm -v ./build:/app/build cs2surf-linux-builder
```

Note: does not work with gcc!

Copy the contents of `build/package/` to your server's `csgo/` directory.
