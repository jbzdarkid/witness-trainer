# Witness Trainer
This program is used to facilitate practice speedrunning The Witness.
This program is definitely cheating, and as such, should not be used during runs.

# Usage
Download the executable from the [Releases page](https://github.com/jbzdarkid/witness-trainer/releases/latest).
Once you run it, it will wait for The Witness to open, then automatically attach to it.
It should work with any Steam release of the game, but YMMV with GoG/Epic/Xbox versions.
This program is Windows-only for the forseeable future.

# Features
- Noclip mode [Hotkey: Control-N]
  * Noclip stands for "No Clipping", i.e. you will not have collision (clip) any geometry while using it.
  * In other words, you can fly around and through walls.
  * Note that some of the areas in the game will derender while out of bounds, so don't worry if you see the world flickering.
  * Exiting noclip mode will attempt to place your character directly below the current view. If there is no suitable ground, you may be teleported to the tutorial area.
- Sprint speed change
  * This changes your maximum walking and sprinting speed. This is quite helpful while practicing, since it saves you a lot of walking time.
- Field of View change
  * This lets you adjust your FoV beyond the normal limits of the game. This is useful for practicing snipes, since a lower FoV will zoom you in on the panel you're sniping.
  * Note that decreasing the FoV does not change what is or isn't rendered, so you won't be able to see panels that you couldn't before.
- Disable saving [Hotkey: Shift-Control-S]
  * Disabling saving is a great way to practice an individual segment over and over.
  * When you enable this checkbox (and disable saving), the game will make one final save from your current position.
  * Then, you can practice whatever segment you need to, and once you complete it (or fail), reload your latest save, which will bring you back to the start.
  * Note that on some older versions, this setting caused the game to crash. If it does so, it should still persist your save, so just restart it.
- Random doors practice
  * This setting will prevent the random doors from "succeeding", i.e. it will prevent them from opening if you solve both doors.
  * It does not remove the timer from the doors, so that you are still practicing fast solves.
  * However, if you fail to solve a panel in time, it will not be re-rolled, meaning that you will have to solve the same panel the next time the doors open.
- Disable challenge time limit
  * This setting is useful for practicing the challenge, if you are a newer runner. It is important to practice all the way through the challenge, especially since the harder parts are at the end.
  * Note that this does not remove the music, so you can still keep track of if you're finishing within the normal timer.
- Save Position (and Load Position) [Hotkeys: Control-P and Shift-Control-P]
  * Useful to test running times between locations.
  * Also useful for practicing extreme distance snipes, where angles are important (such as the mountaintop redirect snipe)
  * X represents your horizontal position (-X = west towards symmetry, +X = east towards mountain)
  * Y represents your vertical position (-Y = south towards peninsula, +Y = north towards keep)
  * Z represents your height (-Z = down, +Z = up)
  * Theta (Θ) represents your horizontal angle (-Θ = rotating right, +Θ = rotating left)
  * Psi (Φ) represents your vertical angle (-Φ = rotating down, +Φ = rotating up)
- Show unsolved panels
  * Fairly self-explanatory, but very helpful for figuring out what you missed in a failed 100% run.
- Lock to entity [Hotkey: Control-L]
  * When you have a previous panel or EP, this checkbox will keep your 3d view snapped into the startpoint of that entity.
  * This is helpful for practicing snipes, or trying to develop new snipes.
  * It is also helpful for solving panels through walls
- Disable distance gating
  * Allows you to solve all panels (notably laser panels) from any distance
- Challenge pillars practice
  * After completing the challenge, pause to power off the pillars, then solve the vault box inside the cage. This will turn on and re-randomize the pillars.
  * Closing the trainer will restore the box's original effect.
- Solvability overlay
  * This will show which panels and EPs can be started from your current location.
  * If it is possible to start a panel or EP, it will have a circle -- otherwise, it will have a triangle.
    * Note that this does not account for collision -- just the angle of attack.
  * While solving, all valid (connected) points are shown with circles, and all covered points are shown with diamonds.
