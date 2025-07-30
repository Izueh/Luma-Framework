Luma is modding framework that facilitates improving the graphics of DirectX 11 games.
It leverages the ReShade Addon system to add or modify rendering passes (and replace shaders (e.g. post processing)) through DirectX hooks.
While most of the generic shaders code is focused on HDR output support, there's a lot more to it already, and no limit to what it can do.
Multiple games are already in the code, and adding a new one is relatively easy.

# Development requirements
- Windows 11 (Windows 10 probably works fine too)
- Visual Studio 2022 (older versions might work too)
- Windows 11 SDK 10.0.26100.0 (older versions work, but don't support HDR as well)

# Instructions
- Set "VCPKG_ROOT" environment variable to your vcpkg installation folder if it wasn't already (download it from here "https://github.com/microsoft/vcpkg", the version integrated with Visual Studio doesn't seem to be as reliable).
- Install the latest VC++ redist before running the code (https://aka.ms/vs/17/release/vc_redist.x64.exe), we enforced users to update to the latest versions, but "_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR" could be defined to avoid that.
- Open "Luma.sln" and build it. Note that "Edit and Continue" build settings (\ZI) should not be used as they break the code patches generation (at least for projects that use DKUtil).
- The code hot spots are in core.hpp and each game's main.cpp files.
- Luma uses the game project for developing and shipping mods. Simply toggle between DEVELOPMENT, TEST and PUBLISHING configurations to enable their respective features. They automatically spread to shaders on the next load/compile. Building in Debug (as opposed to Release), simply adds debug symbols etc, but no additional development features.

# Adding a new game mod
- Add a new project to the solution and select the Luma Template project, add it under ".\Source\Games". You can name it with the full game name, including spaces etc. You can manually check out the Template project that is already in the Luma solution for more information.
- Check the newly created main.cpp file and replace what you need to replace, everything is explained there. Check out other game's mods for further inspiration.
- Each mod's version is stored in "Globals::VERSION" and can be increased there.
- Win32 (x86/x32) games are compatible too, "BioShock 2" is an example of them, make sure to select the platform for it.
- Add an environment variable called "LUMA_GAME_NAME_BIN_PATH" (e.g. "LUMA_BIOSHOCK_2_BIN_PATH" for Bioshock 2), and make it point to the game's executable folder (where ReShade goes). Go to the project settings post-build event page and set it to copy the binaries in that folder (it will already be there as "LUMA_TEMPLATE_BIN_PATH").
- Go to the project settings debugging page and set the Command to the game executable path (e.g. "$(LUMA_PREY_BIN_PATH)\Prey.exe", without the "), so it's run and attached to when debugging (this often doesn't work on games with DRM).
- Install ReShade 6.5.1+ in the game's directory.
- Build the project and run it for debugging, it should automatically run the game with the mod loaded.

# Shaders development
- The mod automatically dumps the game's shaders in development mode.
- Luma shaders can be found in ".\Shaders\GameName". Dumped shaders will go there, and hand created ones should also go there (unless they are generic, then they should go in the generic folder).
- Shader are saved and replaced by (cso/binary) hash.
- VSCode is suggested.
- Packaging mods for now is manual and the generic and game specific shader folders need to be put in a "Luma" folder in the mod binary directory.

# To do and ideas
- Add a utility to automatically package a game mod with all the files it might need
- Improve the way games can add custom cbuffer variables
- Allow packaging shaders with the mod binary (like RenoDX does)? I kinda like to have them as source tbh
- Move the core.hpp code in a static or dynamic library, instead of including it as code in every game specific project
- Add DirectX 12 support? Not planned for now. Luma is based on the simplicity of DX11
- Clang format/tidy (spacing and syntax is quite mix as of now)
- Make a generic mod that works on all games

# Comparison with RenoDX
Luma shares a lot with RenoDX (https://github.com/clshortfuse/renodx), where it got its original inspiration from, but Luma is more focused on modding games deep down, like for example adding and replacing entire rendering techniques.

# Relationship with Starfield and Kingdom Come Deliverance Luma mods
The Luma Framework was born out of the modding code I originally wrote for Prey (Luma). I soon after realize that I could make it all generic and re-use many of its features on other games.
Starfield and Kingdom Come Deliverance Luma mods are not based on the Luma (generic) Framework and thus should not be confused with it. They do share some of the authors, and some of the code features (e.g. HDR stuff), but they are separate entities.

# Why ReShade?
It'd be possible to achieve the same without ReShade and game specific code hooks, by only using generic DirectX hooks, but it'd be exponentially more complicated (Some engines re-use render target textures for different purposes, so we couldn't easily tell which ones to upgrade, and ReShade offers settings serialization and a bunch of other features).
