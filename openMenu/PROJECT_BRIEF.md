# OpenMenu fork: consolidated project brief

Ideas and roadmap, as written by the project owner. This is the vision and specification; it is **not** a statement of what
is implemented. What exists in code is tracked in `ROADMAP.md` (brief against the code) and hardware results in
`TEST_CHECKLIST.md`. The implementation order in section 9 is a proposal, not a decision.

## 1. Project vision

The goal is to create a modified OpenMenu fork that makes the Dreamcast feel like a modern continuation of the original console experience, as if the year were 1999 again, but with today's online community and hardware capabilities.

The interface should retain the appearance, animations, typography, layout, and interaction style of the original Dreamcast BIOS. New features should look and behave as though Sega had designed them for the original console.

This is not simply a visual theme. The project expands OpenMenu into a unified interface for:

- Launching and managing Dreamcast games, versions, patches, and network configurations.
- Accessing online players, games, community news, and events.
- Configuring DreamPi and testing available network interfaces.
- Managing VMUs, virtual VMUs, controllers, and compatible accessories.
- Providing a consistent interface for a broader Dreamcast hardware ecosystem.

The main menu retains four destinations: Play, File, Internet, and Settings.

## 2. Design principles

1. Preserve the original BIOS identity. Use the original BIOS font, rounded translucent panels, colored borders, recognizable icons, and smooth 3D animations.
2. Maintain a stable 60 FPS target. Animations should remain smooth, with lightweight interfaces and careful memory use.
3. Make new functionality feel native. Popups, information boxes, selection states, and navigation should follow the visual language of the original BIOS.
4. Keep the controller interaction predictable. The same buttons should have consistent meanings wherever possible.
5. Make advanced functionality accessible from the console. Network settings, game patches, VMU configuration, and controller options should not require a separate computer for routine operations.
6. Use DreamPi as a companion where appropriate. Web scraping and other expensive data collection should happen on the DreamPi, which supplies prepared information to OpenMenu.
7. Design for an ecosystem, not one device. Consider DreamPi, serial adapters, W5500, broadband adapters, pico2maple, BlueRetro, iBlueControlMod, VM2, VMU Pro, and future compatible hardware.
8. Distinguish implemented features from planned integrations. Hardware-dependent functionality must be designed for, but not represented as working until the required firmware and communication interfaces exist.

## 3. Main menu

The main menu should look and behave almost identically to the original Dreamcast BIOS.

It contains four main icons: Play, File, Internet, Settings.

Retain the original background, font, icon style, transitions, and 3D animations.

Differences from the original BIOS:

- Replace the music destination with Internet because the intended GDEmu setup does not use music CDs.
- Add a blue swirl logo option for the PAL BIOS.
- Expand the top information row to support additional clock settings, connectivity status, online player counts, mouse and keyboard support, and a customizable logo.
- Display the active online player count in the main-menu taskbar, in the row above the clock.
- Integrate notifications from the online companion system where appropriate.

The main menu should remain recognizable as a Dreamcast BIOS rather than becoming a conventional modern dashboard.

## 4. Play menu

The Play menu is the game library and launcher.

### 4.1 Game list and layout

- Display a list of games using the original BIOS font.
- Show seven game rows at a time, consistent with the presentation.
- Display the selected game's rotating 3D GD-ROM model alongside the list, using an animation inspired by the original CD player.
- Show game artwork as a textured 3D jewel case, including front and back artwork where available.
- Display game information alongside the artwork.
- Include the game's favorite/star status and genre.
- Use rounded, semitransparent panels, colored borders, and BIOS-style popup windows.
- Show a small disc number beside multi-disc games.

The library must support multiple filtering, sorting, and grouping methods without losing the visual simplicity of the original BIOS.

### 4.2 Controller mappings

- **A, select.** Select the highlighted item or launch the selected game. Within submenus, A confirms the selected option.
- **B, back.** Navigation must follow this hierarchy: (1) from a game's options or information screen, return to the game list; (2) from the game list, B first returns to the bottom menu/filter row; (3) from the bottom menu/filter row, B returns to the main menu. Avoid unexpectedly skipping navigation levels.
- **X, game options popup.** Open a BIOS-style popup containing the selected game's launch options (4.6).
- **Y, game information / 3D case.** Open the detailed game information screen (4.7).

### 4.3 Sorting

Alphabetical (A-Z), release year, SD card order. The active sort mode should be visible in the bottom menu controls.

### 4.4 Grouping and filtering

The Group control must support: Folder, Favorites, Genre, Developer, Publisher, Homebrew, Tools, Browsers, Local multiplayer, Online multiplayer, Online features, Modem, Serial, W5500, Broadband.

These categories should make it possible to discover games by content, creator, or the capabilities and connection methods they support. Sorting and grouping are separate controls and should remain independently configurable.

### 4.5 Disc and version organization

Disc organization: display every disc as a separate row, or group multi-disc games into a single game entry.

Version organization: group multiple versions of the same game into one entry, or display versions as separate entries.

A version may represent a different region or a modified game image, such as PAL, NTSC, a translation patch, or a serial-network patch. When multiple versions are grouped, the game options popup must let the user choose the version to launch. The implementation should distinguish between separate discs of one game and separate builds or patched variants of a game.

### 4.6 X popup: game options

A popup menu styled like the original BIOS windows, with:

- **Cheats:** access available cheats for the selected game.
- **Disc picker:** choose the disc to launch where the game has multiple discs.
- **Version picker:** choose between installed versions of the game. Display meaningful labels, such as region and patch type, rather than ambiguous filenames where metadata is available. Only display it when multiple variants are installed.
- **Network selector:** choose between DCNow! and DCNet. Remember the selected network for that specific game, use the saved preference the next time that game is launched, and allow the user to change it before launch.

The per-game network choice is independent of the global preferred network setting. The global setting determines the general preference, while a game's saved preference determines the network used for that game. The precedence and fallback behaviour when a preferred network is unavailable are to be defined during implementation.

### 4.7 Y screen: game information and 3D jewel case

A detailed information screen centred around a textured, rotatable 3D game case. It should resemble the back of a physical Dreamcast game case or a detailed RetroArch game information page.

Display, where available: title and artwork, description and synopsis, genre, developer and publisher, release information, relevant metadata and supported features. Information should be scraped, collected, or otherwise prepared alongside the game library where possible.

Controls: joystick rotates the case; L/R zoom; Up/Down scroll the text; A launches the game; B returns to the game list; Y does the case front/back interaction.

Y behaviour: (1) if the case is rotated away from the front, Y first smoothly snaps it back to face the user; (2) Y again rotates the case quickly and smoothly to reveal its back; (3) it should feel like manipulating a physical jewel case, not switching between static images.

Opening the case and showing the disc is a possible future extension, optional.

## 5. File menu

Preserves the layout and visual identity of the original BIOS file manager while adding VMU and controller-management functionality, with familiar BIOS-style panels, icons, selection states, and information boxes.

### 5.1 Serial VMU

Turning the Serial VMU on and off stays in Settings for now (it is meant to move to the File menu later). Which VMU is used is chosen in the File menu: select a VMU on the card grid and press X for a small menu in the style of the X menu of the Play screen, with "Set as Serial VMU" and "Serial VMU Off". Back up now / load backup: still open.

### 5.2 VMU appearance and storage

The card popup of the File menu (A on the card in the file window) has: Copy all, Delete all (memory reset, which is the format), Change Color and Logo (the BIOS's icon and colour pickers, but nothing is erased), Use full size and Cancel. Use full size opens a second popup with a warning and what it means for the VMU, and Cancel / Extend capacity at the bottom. The full-storage option and any changes to existing VMU data must respect the actual storage capabilities and limitations of the underlying implementation. (Setting a VMU colour or a logo for openMenu itself is not planned.)

### 5.3 VMU slot selection

For supported virtual VMU hardware, a way to select the active VMU slot, showing which slot is selected. Planned integrations: pico2maple, VMU Pro, VM2. Availability depends on the relevant device firmware and communication protocol.

### 5.4 GameID selection

A GameID control showing whether GameID is on or off. Show the selected VMU slot number and GameID state as a small number or text box over the lower-right corner of the VMU graphic, in typography consistent with the original BIOS menu titles.

### 5.5 Controller type selection

For each connected controller, select or display its type: dual joystick, rumble controller, steering wheel, mouse, keyboard, other supported types. Show the type in a small text bar along the bottom of the controller icon in the upper portion of the page, so it is clear which type is selected or being emulated.

### 5.6 Bluetooth pairing by controller port

A Bluetooth synchronisation/pairing button for each controller port, inspired by the Nintendo Switch controller-pairing interface, so users can connect controllers without managing all ports together.

### 5.7 Future hardware integrations

Consider future management of modified versions of pico2maple, BlueRetro, iBlueControlMod, VM2, VMU Pro and other compatible devices. These are not assumed to exist; the modified firmware and interfaces may need to be developed first. When designing the UI and firmware communication layer, consider whether OpenMenu could eventually configure device options, select controller modes, select VMU slots, show GameID state, and start Bluetooth pairing.

## 6. Internet menu

A major new feature. It extends the Dreamcast online experience and is not simply a web browser: a console-native interface to the online Dreamcast community, games, events, network configuration, and DreamPi integration. It combines the BIOS visual style with elements inspired by early-2000s and Y2K-era websites.

Four sections: Online, Play, Events, Settings.

### 6.1 DreamPi companion script

A companion script on the DreamPi collects and prepares online information from selected web sources and supplies it to OpenMenu, to reduce loading time and memory use on the Dreamcast. Potential information: players online, games being played, player counts, upcoming and ongoing events, community news, game and service metadata, notifications for starred players, games and events. Define a lightweight communication interface between OpenMenu and DreamPi; the Dreamcast should not scrape complex websites directly.

### 6.2 Online section

Two lists: players currently online, and games currently being played (with player counts where available). The user can select a game and press A to join or launch it; star a player to be notified when they come online; star a game to be notified when it becomes active or players are available; view information supplied by the companion script. Notifications are coordinated with the companion script and shown in OpenMenu, including on the main menu where appropriate. The lists should aggregate across supported online networks where data is available.

### 6.3 Play section

A catalogue of online-compatible Dreamcast games, distinct from the general Play library. For each game: supported online features (multiplayer, leaderboards/high scores, websites), supported connection methods, the online service or community network used, and relevant launch options. The user can launch a listed game directly. A web browser is reachable from this section as well.

### 6.4 Events section

Upcoming and ongoing Dreamcast community events, initially from DC99. Users can select or star events to be notified through the companion script and OpenMenu. Distinguish current from upcoming events and give useful details where available.

### 6.5 News

A news page with recent Dreamcast community news. Sources are configurable or chosen by the maintainer (DC99, Dreamcast Live, other chosen community sources). Collection is done by the companion script where appropriate.

### 6.6 Internet settings

- **ISP settings:** configure the Dreamcast ISP/dial-up settings directly from OpenMenu, with presets for common configurations where practical, so routine setup needs no web browser.
- **DreamPi integration:** information or instructions for installing and configuring the companion script; connection and integration status.
- **DreamPi Ethernet settings** and **Wi-Fi settings** (when a compatible wireless interface is available). These need matching functionality in the companion script; do not assume OpenMenu can configure them without it.
- **Network selection:** set the preferred network or connection method; per-game network preferences; show the selected network when relevant.
- **Network-interface testing**, subject to hardware availability: modem, broadband adapter, serial, W5500. Tests should show whether the interface works and, where practical, assess speed, connectivity and port forwarding. Clearly distinguish tests that run locally from those that need an external server, a running DreamPi service, or extra hardware.

## 7. Settings menu

An evolution of the original BIOS settings interface.

- A scrollable list of original BIOS settings and additional OpenMenu settings, about four to five rows at a time.
- The original 3D icons and familiar navigation.
- A detached information panel at the bottom explaining the selected setting.
- Settings already managed by the File and Internet menus stay there rather than being duplicated.

Preserve all original settings and add the OpenMenu-specific ones that do not belong in File or Internet. Add: choose the BIOS background colour from a small predefined selection; dark mode. The colour choices stay consistent with the BIOS aesthetic, not arbitrary modern themes.

## 8. Ecosystem and architecture

OpenMenu (Dreamcast side) is responsible for: BIOS-style UI and navigation; game discovery, sorting, grouping and launching; game versions, discs and launch options; displaying online data and notifications; VMU and controller configuration interfaces; network configuration and test results.

The DreamPi companion is responsible for: collecting and preparing online data; news and event information; monitoring online players and games; coordinating notifications; supporting DreamPi networking configuration; exposing a lightweight interface for OpenMenu. The API, data format, polling strategy and transport remain implementation decisions.

Hardware to account for: DreamPi, serial modem connections, broadband adapters, W5500-based adapters, pico2maple, BlueRetro, iBlueControlMod, VM2, VMU Pro. Some need new or modified firmware. The interface should anticipate their future capabilities without pretending the integration is already available.

## 9. Suggested implementation priorities

A proposed development sequence, not a claim about what is implemented.

- **Phase 1, core interface and Play menu:** preserve the BIOS-style main menu; Play menu navigation and controller mappings; sorting and grouping; disc and version organisation; the X options popup and per-game DCNow!/DCNet selection; the Y information screen and 3D case interaction.
- **Phase 2, File and Settings menus:** Serial VMU controls; VMU appearance settings; controller-type display and selection where supported; VMU slot and GameID displays; the scrollable Settings menu, background-colour options and dark mode.
- **Phase 3, Internet menu and DreamPi integration:** define the companion-script interface; online players and active games; starring and notifications; news and events; online-compatible game listings and launching; ISP configuration and network diagnostics.
- **Phase 4, advanced hardware integrations:** DreamPi Ethernet and Wi-Fi configuration; expanded network testing; supported virtual VMU devices; controller-port Bluetooth pairing and controller-mode configuration; modified pico2maple and BlueRetro firmware as needed.

The actual order should be adjusted to the existing codebase, available APIs, hardware constraints and dependencies.

## 10. Implementation guidance

Before changing code, inspect the existing OpenMenu fork, its architecture, current functionality, and relevant firmware or companion projects. Use the existing project conventions and preserve working functionality.

- Do not treat the presentation mockups as proof that a feature already exists.
- Separate UI mockups, implemented behaviour, and future hardware-dependent functionality.
- Reuse existing OpenMenu mechanisms wherever possible.
- Identify missing APIs, protocol support, firmware changes and DreamPi-side requirements before implementing dependent UI.
- Avoid large, unnecessary abstractions for features that can be integrated cleanly into the existing code.
- Keep the BIOS visual language consistent across all screens.
- Make the button mappings and navigation hierarchy match this specification.
- Make unsupported functionality explicit in the UI or development notes rather than silently presenting it as operational.
- For any unresolved technical decision, explain the options and constraints before committing to an implementation.

Success criteria: the result should feel like a natural extension of the Dreamcast BIOS. The original console experience stays intact, while games, patches, online services, VMUs, controllers and network connections become easier to discover and manage directly from the Dreamcast. The guiding principle is a unified Dreamcast experience, not a collection of disconnected features.

Key points not to misunderstand:

- X and Y have distinct jobs: X opens game launch options; Y opens the interactive 3D case and information screen.
- Network preferences are remembered per game: DCNow! or DCNet persists for that game's next launch.
- Game grouping has two separate dimensions: multiple discs and multiple versions/patches are not the same thing.
- The Internet menu is a console interface backed by DreamPi: the companion script reduces work and memory use on the Dreamcast.
- The File menu is also a hardware-management interface: VMUs, controller types, slots, GameID and pairing are part of the long-term vision.
- Future hardware support is conditional: pico2maple and BlueRetro integrations may require modified firmware first.
