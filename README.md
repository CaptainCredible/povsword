# POV sword

A 12-LED persistence-of-vision sword (ATtiny85), and the web editor that draws
images for it and sends them to the sword by blinking the screen into its light
sensor.

| Folder | What |
|--------|------|
| `cycle_pov_sword/` | Sword firmware (PlatformIO or Arduino IDE with ATTinyCore). Receives images over the light sensor. See its README for wiring, fuses, the wire format and the host test. |
| `web/` | The editor and gallery (`index.html` + PHP API). "Send to sword" is the transmitter. |
| `tools/` | `publish.sh`: uploads `web/` to the web host over SFTP. |
| `archive/` | Older versions of the sketch and editor, kept for reference. |

## Getting started

Open `povsword.code-workspace` in VS Code. It opens the repo root (for the deploy
tasks) and the firmware folder (so PlatformIO finds `platformio.ini`).

**Firmware:** in the firmware folder, `pio run -t fuses` once per chip, then
`pio run -t upload`. Details in `cycle_pov_sword/README.md`.

**Web:** the first time on a new computer:

```sh
cp web/config.example.php web/config.php      # then set the password, salt, privacy contact
cp tools/publish.conf.example tools/publish.conf
```

Then publish with `tools/publish.sh` (or the `deployPovsword` VS Code task).

`web/config.php` and `tools/publish.conf` are git-ignored: they hold the admin
password, IP salt and login details. The server keeps its own `config.php`.

## Keeping the two halves in sync

The light-link protocol is defined twice: in `cycle_pov_sword/pov_protocol.h` and
in `buildSchedule()` in `web/index.html`. Change both together, and run the
firmware's host test (it can also replay schedules exported from the editor).
