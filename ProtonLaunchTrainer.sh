#!/bin/sh
# Adapted from https://gist.github.com/lunalucadou/132a84d6e5d248847f9e90160f7789d7 by micsthepick and darkid
#
# This script allows you to launch WitnessTrainer via Proton (and have it attach to The Witness)
#
# To use this script:
# 1. Save it somewhere (e.g. ~/Desktop/ProtonLaunchTrainer.sh)
# 2. Set the variables below appropriately
# 3. Make it executable (e.g. chmod +x ~/Desktop/ProtonLaunchTrainer.sh)
# 4. Run it (e.g. ~/Desktop/ProtonLaunchTrainer.sh)

##### Customize these variables as needed:

# The Steam library where you installed The Witness (default: ~/.steam/steam)
STEAM_GAME_LIBRARY="${HOME}/.steam/steam"

# The location (in Proton) where you downloaded WitnessTrainer
export TRAINER_EXE="Z:\\home\\user\\Documents\\WitnessTrainer.exe"


##### Leave the rest of these alone:
# The Witness has ID 210970
export SteamAppId="210970"
export SteamGameId="210970"

# Steam client path
export STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_GAME_LIBRARY}/steamapps"

# Steam compat data path
export STEAM_COMPAT_DATA_PATH="${STEAM_COMPAT_CLIENT_INSTALL_PATH}/compatdata/${SteamAppId}"

# Fully-qualified path to Proton executable (for The Witness -- other games may use other Proton versions)
PROTON_EXEC="${HOME}/.steam/steam/steamapps/common/Proton - Experimental/proton"

# Launch the app
python "${PROTON_EXEC}" run "${TRAINER_EXE}"
