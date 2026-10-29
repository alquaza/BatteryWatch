# BatteryWatch

A Rainmeter plugin and two desktop skins so you can see battery lives of wireless devices.

It works for SPECIFICALLY the Logitech PRO X SUPERLIGHT and the HyperX Cloud III S Wireless (in dongle mode). It could be adapted for use with other devices but I'm not concerned about implementing them because I just made this for myself. It is also only tested working on Windows 10.


- **Corner skin:** small "Mouse 64%  Headset 82%" text in the top-right of the desktop.
- **Warning skin:** a large "Charge Mouse!" message in the centre of the desktop. It is invisible until a device is at or below a set threshold.

Both skins sit on the desktop layer, behind every window, and ignore clicks. The plugin talks to the USB dongles directly. You don't need G HUB or NGENUITY, but leaving them installed doesn't cause issues.

## Contents

```
build.bat                     builds the plugin and the test tool
src/                          C++ source
skins/BatteryWatch/           the Rainmeter skins
  @Resources/Settings.inc     your thresholds, colours and wording
```

## Setup steps

### 1. Install Rainmeter

Download it from <https://www.rainmeter.net> and run the standard install. It starts with Windows by default. On first launch it loads some sample skins, which you can unload (deactivate) later in step 7. Right click on the icon in the taskbar and just close it for now.

### 2. Install the C++ compiler

If you don't already have it, install **Visual Studio Build Tools** or **Visual Studio Community** from <https://visualstudio.microsoft.com/downloads/>. In the installer, tick the **Desktop development with C++** workload. This is needed for building but can be uninstalled later if you want: Open the visual studio installer and click **More** -> **Uninstall**.

### 3. Build

1. Unzip this folder somewhere, for example `C:\BatteryWatch`.
2. From the Start menu, open **x64 Native Tools Command Prompt for VS**. You must use this and not a normal Command Prompt because it won't know where the compiler is.
3. Run:
   ```
   cd /d C:\BatteryWatch
   build.bat
   ```
4. It should create an "out" folder and inside, you should see `out\BatteryWatch.dll` and `out\BatteryCheck.exe`.

### 4. Test the readings before Rainmeter

1. Move the mouse to wake it, and switch the headset on.
2. Double-click `out\BatteryCheck.exe`.
3. It lists the Logitech and HyperX USB interfaces it can see, then prints:
   ```
   ==> Mouse battery: 71%
   ==> Headset battery: 88%
   ```
   Compare these with values you know, possibly from G HUB and NGENUITY.

If either line says "no battery reply" or "not found", something has gone wrong, sorry.

### 5. Install the plugin

1. If Rainmeter is running, exit it: right-click its tray icon and choose **Exit**.
2. Find the `Rainmeter` folder in `%APPDATA%`.
3. If there's no `Plugins` folder there, create one.
4. Copy `BatteryWatch.dll` into the `Plugins` folder.

### 6. Install the skins

Copy the `skins\BatteryWatch` folder into `Documents\Rainmeter\Skins\`, so that you end up with `Documents\Rainmeter\Skins\BatteryWatch\Corner\Corner.ini`.

### 7. Load the skins

1. Start Rainmeter.
2. Right-click its tray icon and choose **Manage**.
3. Click **Refresh all** at the bottom.
4. In the list, open **BatteryWatch → Corner**, select `Corner.ini`, and click **Load**.
5. Open **BatteryWatch → Warning**, select `Warning.ini`, and click **Load**.
6. Unload any sample skins you don't want: select each loaded one and click **Unload**.

The corner text should appear within a few seconds.

### 8. Check that the warning works

1. Open `Documents\Rainmeter\Skins\BatteryWatch\@Resources\Settings.inc` in Notepad or something.
2. Set `MouseThreshold=100` and save.
3. In Manage, click **Refresh all**. The centre warning should appear within a few seconds which means it works.
4. Set `MouseThreshold` back to the value you want, save, and click **Refresh all** again.

### 9. Set your thresholds

Edit `Settings.inc`:

- MouseThreshold and HeadsetThreshold: The middle warning will appear at or below this percent (default is 20%)
- PollSeconds: How often the device batteries are polled (default is 300 = 5 min, minimum 10)
- CornerFontSize, CornerColor, CornerGap: Change the look and position of the text in the corner
- WarningTitleSize, WarningTitleColor: Change the look of the warning in the center
- MouseTitle, HeadsetTitle, BothTitle: Change the wording of the warning messages

Colours are `R,G,B,Alpha` (0–255). After editing, click **Refresh all** in Manage.

## Optional settings

- **Game Mode:** to unload all skins while a fullscreen game runs, right-click the Rainmeter tray icon, open **Game mode**, and tick **Full-screen games**. Nothing polls the devices while it's active.
- **Hide Rainmeter's tray icon:** in Manage, go to the **Settings** tab and untick **Show notification area icon**. Afterwards, you can open Manage by running Rainmeter again from the Start menu.
- **Moving or editing a skin:** the skins are click-through and can't be dragged. Change them from Manage, or edit the `.ini` files.

## Behaviour

- Each device is read on a background thread, once per `PollSeconds`, shared by both skins. Rainmeter itself never waits on USB.
- If a device stops answering, the last known value stays on screen. That happens when the mouse is asleep or the headset is switched off. A low warning therefore stays up until a new reading says the battery is above the threshold.
- `--` means no reading has been taken yet since Rainmeter started.
- The original SUPERLIGHT reports battery as a voltage, which is converted to a percentage. It can wander by a few percent so don't rely on exact displayed percentages too much.

## Updating the plugin

Exit Rainmeter, rebuild, copy the new DLL over the old one, then start Rainmeter again. Windows won't let you replace the DLL while Rainmeter is running.

## Credits

The Logitech HID++ details come from: Solaar (https://github.com/pwr-Solaar/Solaar) and LGSTrayBattery (https://github.com/andyvorld/LGSTrayBattery) including the voltage table.

The HyperX Cloud III S battery query stuff by auto94/HyperX-Cloud-2-Battery-Monitor (https://github.com/auto94/HyperX-Cloud-2-Battery-Monitor) and dddd-zdf/gear-tray (https://github.com/dddd-zdf/gear-tray).
