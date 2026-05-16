# PCSX2 Reliquary

PCSX2 Reliquary is a PCSX2 fork focused on accurate emulation of security flows and esoteric PS2-derived hardware.

It targets platforms and software outside the usual retail console path, including systems such as the Konami Python 1 & 2, and Namco System families.

## Features

- Full security process support end to end.
- Bring your own keys.
- Support for all Konami Python 2 titles.
- Support for CHD-compressed internal HDD images with writable overlays.
- Switch keying mode between Developer, Retail, Arcade, and Prototype on both mechacon and memory cards.
- Support raw PS2 memory card dumps with proper keying.
- Support for utility discs such as HDD installers and DVD installers.
- Initial support for COH-based machine functionality, including Python 1 and System 2x6.
- Boots COH memory card dongles when configured in Arcade mode.
- FireWire stub.
- Can be configured for Conquest cards, but that path is not functional yet.

## What This Fork Is For
PCSX2 Reliquary exists for preservation, research, and compatibility work around the parts of the PS2 ecosystem that most emulators never needed to care about.

## Status

Current work is centered on bringing up uncommon PS2-adjacent hardware cleanly while keeping the security and memory card paths accurate and configurable.

This is an experimental fork aimed at preservation, research, and hardware-specific compatibility work.

## Configuration

### Mechacon
Mechacon keystore is configured under the advanced menu tab
![mechacon-config.png](docs/mechacon-config.png)

### BIOS
You should be running off a **FULL BIOS DUMP**. This means you need not only the bios bin file from the system you wish to boot, but also it's associated NV Ram and Mechacon config sector. This is essential for proper iLinkID matching and security to pass. [biosdrain](https://github.com/F0bes/biosdrain) is a good utility for this.
The associated files should share the name of the base bios dump (.bin/.rom0) and live in the same folder
![bios-dump.png](docs/bios-dump.png)

### Memory Card
Each memory card slot has its own configuration. Each memory card has it's own security processor with it's own keys, particularly the conquest cards for Soul Calibur have different key configuration than the booting dongle.

Here is an example for a Konami Python 1 and Namco System 2x6 config
![memorycard-config.png](docs/memorycard-config.png)

Memory card IDs are currently hard-coded as `MechaPwn`. This is the same ID that is used in sd2psx so raw memory card images can be used between real hardware and this emulator easily.
PCSX2 memory cards expect the ECC data to be present. Some memorycard dumping utilities dump without hte ECC data, but that can easily be recovered using a tool like [this](https://github.com/ffgriever-pl/PS2-ECC-Memory-Card-Converter).

### Konami Python 2
Python 2 games require pairing of the game hdd image, the associated nvram, the white and black dongle data, as well as other hardware specifics like your e-amuse card id. This can all be configured through a `.py2` file that the game library scanner can read and interpret. Details on this file format are listed in this [wiki article](https://github.com/987123879113/pcsx2/wiki/PY2-Game-Entry-File-Example).

Python 2 HDD images can be provided as either raw `.raw` files or CHD-compressed `.chd` files. CHD images are opened read-only as the base image to reduce collection size, while any writes made by the emulated HDD are stored in a separate writable overlay under `hdd-overlays/` in the emulator settings directory. This keeps the compressed source CHD unchanged and allows per-install or per-user runtime data to persist. To reset a CHD-backed HDD to its base image, close the emulator and delete the matching `.overlay` and `.map` files.

**Ensure the mechacon keys are set properly in the advanced settings menu and that the key mode is set to "Retail"**

Here is an example config for SuperNova 2
```yaml
[Game]
; Friendly name to display in the game list
Name="DDR SuperNova 2"

; Path to HDD image file.
; Note: For Windows you must use \\ instead of just \ for file paths or it WILL NOT WORK.
HddImagePath=gdj_jaa_2007100800.chd

; HDD ID corresponding to the HDD image (required for unpatched drives)
HddIdPath=ps2_hdd_id

; NvRam corresponding to the HDD image (required for unpatched drives)
NvRamPath=ps2_nvram

; Black and white dongle files (required for unpatched games)
; Format of binary dongle file is:
; (Old format)
; 8 bytes - serial ID
; 32 bytes - encrypted dongle payload
;
; OR
;
; (MAME format)
; 32 bytes - encrypted dongle payload
; 8 bytes - serial ID
DongleBlackPath=ds2430_black_gqgdjjaa.bin
DongleWhitePath=ds2430_white_gqfdhjaa.bin

; Input types
; 0 = Drummania
; 1 = Guitar Freaks
; 2 = Dance Dance Revolution
; 3 = Toy's March
; 4 = Thrill Drive 3
; 5 = Dance 86.4 Funky Radio Station
InputType=2

; DIP Switches 1234 (NEW FORMAT)
; Change each individual dipswitch to true or false
DIPSW1=false
DIPSW2=false
DIPSW3=false
DIPSW4=false

; Optional, extended pnach patch file
PatchFile=ddrsn2j.pnach

; Force 31 kHz mode
; Will cause the top of the screen to not refresh in Guitar Freaks, Drummania, Toy's March (all GFDM engine-based games) which also occurs on real hardware.
Force31kHz=0

; Card files are text files with the 16 character card ID.
; Optional. You'll know if you have a need for this.
Player1Card=card1.txt
; Player2Card=card2.txt

; (RECOMMENDED) Manually set a unique ID number to the game as the CRC value.
; If this is not manually set then random number will be generated every time the file is added to the game list, resulting in a new gamesettings .ini to be created for the game each time it's newly imported so settings may not be shared as expected.
; WARNING: If you manually set this value then please make sure that the unique ID does not clash with any other game entries or else there may be bugs with game settings.
UniqueId=334281

; Manually set the region on the game list when imported
; Optional, will default to "NTSC-J".
; See wiki page for list of valid region codes.
; Region=NTSC-J
```

The Python 2 IO board (P2IO) is available as a USB device in the controller configuration screen. It must be plugged into port 1 for the inputs and dongles to be authenticated correctly.
![p2io-config.png](docs/p2io-config.png)

### Retail/Utility Disks
If your bios is a proper dump, and your mechacon and memory cards are setup in Retail mode, then any HDD based functionality will work like a real console. This lets you do things like run the HDD Utility disks, boot FMCB, install game HDD functionality or boot DVD update payloads.
![hdd-utility.png](docs/hdd-utility.png)
