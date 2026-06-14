# Screen Capture Plugin

Records the Wii U GamePad and/or TV screen to a ring buffer in RAM. Two button combos let you **start/stop recording** and **save recordings** to the SD card.
Both combos can be changed in the Aroma config menu.

## Usage

### Quick start

1. Put the `.wps` to `sd:/wiiu/environments/aroma/plugins/` and reboot.
2. Recording is always **disabled by default**.
3. Press **L + ZR + PLUS** to start recording (you'll see a notification).
4. Press **L + ZR + MINUS** to save the last seconds when something cool happens.
5. videos are saved in `fs:/vol/external01/wiiu/videos/<AppName>/YYYY-MM-DD/`.

### Recommended settings by use case

| Goal | Resolution | Buffer || Approx RAM |
|------|-----------|--------|--------|------------|
| Short clips, fast saves | 428×240 | 30 s || ~29 MB |
| General purpose (default) | 480×270 | 30 s || ~36 MB |
| Max quality | 854×480 | 30 s || ~108 MB |
| Long captures | 854×480 | 45 s || ~108 MB |

### Setting details

**Recording enabled** - Off by default for crash safety. Turn it on from the config menu or press the record combo during gameplay.

**Capture resolution** - Higher resolutions use significantly more GPU-mapped memory and will take longer to save. The default 480×270 is a good balance. 854×480 requires ~108 MB of mapped memory and may fail on some games. higher resolutions should work fine in virtual console games or for recording a quick moment depending on the game.

**Buffer duration** - A shorter buffer saves faster and uses less memory. A longer buffer gives you more time to press the save combo but uses more available memory and may cause the console to crash depending on the game being played.

**Capture source** - Choose which display to record:
- **GamePad only** - Captures the DRC screen. Best for GamePad-heavy games
- **TV only** - Captures the TV output. Use when the game's primary action is on TV.
- **Both (two files)** - Captures both simultaneously. Writes two separate AVI files per save. Uses 2× the mapped memory and takes longer to save. Best at lower resolutions (428×240 or 480×270) and 30 seconds buffer duaration for a successful save.

## Tips

- Saving at 854×480 or when capturing **Both** sources can take 2–5 seconds to save or may crash depending on the game. I recommend using the default (480×270) or lower unless you need the higher resolution. You will notice a brief stutter when saving.
- If you see a "Save FAILED" notification when saving, try a **lower resolution** and **buffer duration** - the game may not have enough free mapped memory.
- Recording **Both** sources at high resolutions may cause buffer allocation failures or a console crash. Start with 428×240 or 480×270 and work up.

## Building

Requirements:
- [wut](https://github.com/devkitPro/wut)
- [wups](https://github.com/wiiu-env/WiiUPluginSystem)
- [wums](https://github.com/wiiu-env/WiiUModuleSystem)
- [notifications module](https://github.com/wiiu-env/NotificationModule)

```bash
# run make
make

# With debug logging:
make DEBUG=1
```

Output: `Screen_Capture.wps` copy to `sd:/wiiu/environments/aroma/plugins/`.




