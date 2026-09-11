# Screen Capture Plugin

Record the screen and audio from the Wii U GamePad/TV to a ring buffer in RAM like the Nintendo Switch.

## Usage

1. Put the `.wps` in `sd:/wiiu/environments/aroma/plugins/` and reboot.
2. Recording is always disabled by default.
3. Press **L + ZR + PLUS** to start recording (you'll see a notification); Press **L + ZR + MINUS** to save the recording. Both combos configurable in the Aroma config menu.
5. videos are saved in `fs:/vol/external01/wiiu/screencaptures/<AppName>/YYYY-MM-DD/`.

## Plugin details

- **Capture resolution** - Higher resolutions produce a cleaner video but uses much more memory  and takes longer to save,  recording at **854×480** (the highest) can cause a **"Save FAILED"** notification or crash the console. The default resolution **480×270** is reliable and stable, I would recommend using **854×480** only in Virtual console games / lighter Wii U Games.

- **Buffer duration** - **15s** buffer saves faster, **30s** buffer **45s** buffer records a longer video but may cause the console to crash depending on the game/application being recorded. Use **45s** only in virtual console games and light Wii U Games.

**Capture source** - Choose which display to record:
- **GamePad only** - Captures the GamePad screen only.
- **TV only** - Captures the TV screen only.
- **Both (two files)** - Captures both simultaneously. Writes two separate AVI files per save. Best at lower resolutions (428×240 or 480×270) and 30 seconds buffer duaration for a successful save.

**JPEG quality** - Adjust the clarity of the video.

**Save on buffer end** - Saves the video once it reaches the end of the buffer.

**Stop recording after saving** - Stops recording after a video is saved.

## Building

Requirements:
- [wut](https://github.com/devkitPro/wut)
- [wups](https://github.com/wiiu-env/WiiUPluginSystem)
- [wums](https://github.com/wiiu-env/WiiUModuleSystem)
- [notification module](https://github.com/wiiu-env/NotificationModule)

```bash
# run make
make

# With debug logging:
make DEBUG=1
```

Output: `Screen_Capture.wps` copy to `sd:/wiiu/environments/aroma/plugins/`.

## Credits

- Based on [StreamingPluginWiiU](https://github.com/Maschell/StreamingPluginWiiU) by [Maschell](https://github.com/Maschell)



