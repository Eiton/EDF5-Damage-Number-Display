# EDF5 Damage Number Display Mod #
This mod adds EDF6 style damage number display to EDF5 for PC.

-----------------------
## How to use ##
1. Install [EDFModLoader by BlueAmulet](https://github.com/BlueAmulet/EDFModLoader) and place the dll in Mods\Plugins.
2. Place EDF5-Damage-Number-Display.ini in the game directory.
3. You can edit the ini file to change the display and font setting. Damage display per hit/target is disabled by default

-----------------------
## How to build ##
Clone this repo. Build the x64 detour library. Open the project wilth Visual Studio and build.


-----------------------
## Known issues ##
1. Not work in split screen play. 
2. Damage by vehicle is displayed only when the player is inside it. (This is probabily impossible to fix)
3. Freezing when you change the resolution with the dll loaded.
4. Rare crashes. If you find reproducible crashes, please create an issue and include the details.