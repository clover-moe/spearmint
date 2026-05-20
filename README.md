<img src="https://raw.githubusercontent.com/clover-moe/spearmint/master/misc/spearmint_text.png" width="256">

**Spearmint** is a fork of [Lilium Arena](https://github.com/clover-moe/lilium-arena) with two main goals; 1) provide a flexible engine for creating new games and mods, 2) support features from (and running) various id Tech 3-based games.

Spearmint can be used to play Quake III Arena, Quake III: Team Arena, and Turtle Arena. Progress has been made toward running Return to Castle Wolfenstein (MP) and Wolfenstein: Enemy Territory but there is still quite a bit left before it's possible. Spearmint is not compatible with existing mods (the QVM/DLL files) or demos (game recordings) for any game.

New Spearmint major releases (X.0.0) will break VM and network compatibility with previous releases.

The source code for the Spearmint Quake 3 game, cgame, and ui code and QVM compiler is at [clover-moe/mint-arena](https://github.com/clover-moe/mint-arena/). Map editor and map compiler are available at https://icculus.org/gtkradiant/.

## Download

Pre-built packages for Windows, GNU/Linux, and Mac OS X are available at the [Spearmint website](https://clover.moe/spearmint).


## Resources

  * [Development documentation](https://github.com/clover-moe/spearmint/wiki)
  * [Discussion / Technical support](https://clover.moe/open-source)


## Git branches

* `master` branch is for Spearmint 1.1 development.
* `1.0` branch is for Spearmint 1.0.x bug fix releases.
* `gh-pages` branch is the Spearmint website.


## License

Spearmint is licensed under a [modified version of the GNU GPLv3](COPYING.txt#L625) (or at your option, any later version). This is due to including code from Return to Castle Wolfenstein and Wolfenstein: Enemy Territory.

Submitted contributions must be given with permission to use as GPLv**2** (two) and any later version; unless the file is under a license besides the GPL, in which case that license applies. This allows me to potentially change the license to GPLv2 or later in the future.


## Credits

Spearmint is maintained by Clover.moe at https://github.com/clover-moe/spearmint.

* Zack Middleton (main developer)
* Tobias Kuehnhammer (feedback / bug reports / Bot AI fixes)
* And other contributors

Spearmint contains code from;
* Quake 3 - id Software
* ioquake3 - ioquake3 contributors
* RTCW SP - Gray Matter Interactive
* RTCW MP - Nerve Software
* Wolfenstein: Enemy Territory - Splash Damage
* Tremulous - Dark Legion Development
* World of Padman - Padworld Entertainment
* [ioEF engine](http://thilo.tjps.eu/efport-progress/) - Thilo Schulz
* NetRadiant's q3map2 - Rudolf Polzer
* OpenArena - OpenArena contributors
* OpenMoHAA - OpenMoHAA contributors
* Xreal (triangle mesh collision) - Robert Beckebans
* ZEQ2-lite (cel shading) - ZEQ2 project


## Contributing

High quality code contributions are more helpful than rushed contributions. LLM ("AI") contributions are not desired.

Reviewing pull requests is sometimes more work than a reviewer doing the work in the first place so pull requests may be disregarded.


## Derivatives

If you create a derivative project that you plan for others to use, it would be preferred to give it a different title such as "Spearmint (_your name_ ver.)" to make it easier to discuss and reduce confusion with the project maintained by Clover.moe.

If you're going to pick a different title, it would be preferred that you pick a character from one of your favorite video games to build on rather than the word Spearmint. (Spearmint is partially named after the character Gum in Jet Set Radio.)


