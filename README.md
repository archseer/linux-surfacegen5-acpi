# Linux ACPI (Platform) Drivers for 5th Gen. Surface Devices

Linux embedded controller driver for 5th generation (and later) Surface devices required for battery status and more.

_This has now been integrated into [jakeday/linux-surface](https://github.com/jakeday/linux-surface/)._
_If you have a Surface Book 2 you might also want to have a look at the [dtx-daemon][dtx-daemon] and the [surface-control][surface-control] utility._

## Supported Features and Devices

| Device           | Supported Features                                                     | Known Issues/Missing Features                                           |
|------------------|------------------------------------------------------------------------|-------------------------------------------------------------------------|
| Surface Book 2   | lid status, battery status, clipboard detach events, performance-modes | -                                                                       |
| Surface Laptop   | battery status, keyboard                                               | caps-lock indicator (#8), performance-modes (#18)                       |
| Surface Laptop 2 | battery status, keyboard                                               | caps-lock indicator (#8), performance-modes (#18)                       |
| Surface Pro 2017 | battery status                                                         | keyboard backlight enabled during suspend (#4), performance-modes (#18) |
| Surface Pro 6    | battery status                                                         | keyboard backlight enabled during suspend (#4), performance-modes (#18) |

If you want to help out, have a look at the corresponding issues.
In most cases, we just need a bit of information from someone who owns such a device.
Also, if you think there's anything missing here, feel free to open an issue!

## Notes on the Surface Book 2

### Clipboard Detachment

This driver now has basic support for clipboard detachment handling (e.g. unmounting of devices attached to the base).
The driver itself does not do anything more than sending an event to user-space and awaiting a reply.
A separate daemon is required to handle these events.
Have a look at [this][dtx-daemon] repository for a basic implementation of such a daemon.

[dtx-daemon]: https://github.com/qzed/linux-surface-dtx-daemon
[surface-control]: https://github.com/qzed/linux-surface-control

### Setting the Performance Mode

The performance-mode controls the power-management strategy.
It is currently unclear what exactly this includes, but one aspect is the fan-profile:
On the default performance-mode it can happen that the dGPU (and possibly also CPU in models with a CPU fan) cannot reach it's full potential due to the fans not ramping up appropriately.
Setting a higher performance-mode solves this problem.

The easiest way to set the performance-mode is to use the [surface-control][surface-control] command line utility by running
```
surface performance set <mode>
```
where the numeric mode-value (1-4) is described below.
Alternatively, the performance-mode can also be accessed via the `perf_mode` sysfs attribute on the  `MSHW0107` platform device, i.e. it can be set via
```
echo <mode> | sudo tee /sys/devices/platform/MSHW0107:00/perf_mode
```
where `<mode>` is the numeric value of the mode you want to set.
Reading from this attribute will return the current mode.

Valid performance-modes are:

| Value | Name (Windows)     | Notes                                            |
|-------|--------------------|--------------------------------------------------|
| 1     | Recommended        | Default mode.                                    |
| 2     | Battery Saver      | Only accessible on Windows when AC disconnected. |
| 3     | Better Performance |                                                  |
| 4     | Best Performance   |                                                  |

You can also set the initial performance-mode (being applied when the module is loaded) using the `perf_mode_init` module-parameter, as well as the state being applied when it is unloaded using the `perf_mode_exit` parameter.
In both cases, the special value of `0` will keep the performance-state as-is (this is the default behavior).

## Testing

To test this module, you need a custom kernel with the patches found in this repository applied.
For a full set of patches including Jake's linux-surface patches, see [this][patches-linux-surface] fork.
Furthermore, you need to ensure that the following config values are set:

```
CONFIG_SERIAL_DEV_BUS=y
CONFIG_SERIAL_DEV_CTRL_TTYPORT=y
```

There is a pre-compiled kernel available [here][prebuilt-linux-surface].

If you have all the prequisites, you can

### Build/Test the module

You can build the module with `make`.
After that, you can load the module with `insmod surface_sam.ko`, and after testing remove it with `rmmod surface_sam`.

### Permanently install the module

If you want to permanently install the module (or ensure it is loaded during boot), you can run `make dkms-install`.
To uninstall it, run `make dkms-uninstall`.
If you've installed a patched kernel already contiaining the in-kernel version of this module, you may want to keep it from loading by editing/creating `/etc/modprobe.d/surface-acpi.conf` and adding `blacklist surface_acpi`.
Note that you will need to undo these changes when you want to use the in-kernel module again.

[patches-linux-surface]: https://github.com/qzed/linux-surface/tree/master/patches/4.18
[prebuilt-linux-surface]: https://github.com/qzed/linux-surface/releases/tag/v4.18.16-pre1

## Getting Windows Logs for Reverse Engineering

1. Get the required software:
   - [IRPMon (beta)][irpmon]

2. Disable driver signature verification (required to get IRPmon working):

   Hold shift while clicking on the restart button in the start menu.
   Go through `Troubleshoot`, `Advanced Options`, `See more recovery options`, `Start-up Settings` and press `Restart`.
   Boot into windows.
   On the screen appearing afterwards press `7` to `Disable driver signature enforcement`.

   _Note: This step will re-boot your PC._

3. Start IRPMon via `x64/IRPMon.exe`.

   Select `Action`, `Select drivers / devices...` and search for `\Driver\iaLPSS2_UART2`.
   Expand and right-click on the inner-most entry and select `Hooked`.
   Select the `Data` option while hooking, then click `Ok` to close the selection window.

   Make sure there is a check mark next to `Monitoring`, `Capture Events`.
   If not activate this.

4. Perform a/the task involving the EC (eg. detaching the clipboard on the SB2).
   You should then see messages appearing in the window.
   You can see which items have data in the "Associated Data" column and look at the data under `Request`, `Details`, `Hexer`.
   You can save those to a file via `Action`, `Save`.

   Please try to submit concise logs containing one test at a time.
   Usually the messages should stop appearing after a short period of time and you can then assume that the exchange between Windows and the EC is complete.

[irpmon]: https://github.com/MartinDrab/IRPMon/releases/tag/v0.9-beta

## Notes on the Hardware

From what I can figure out, the (newer) Surface devices use two different ARM chips:

- [Kinetis K22F](http://cache.freescale.com/files/microcontrollers/doc/data_sheet/K22P121M120SF7.pdf) on Surface Book 2, Surface Pro 5, Surface Pro 6, ..?
- [Kinetis KL17](http://cache.freescale.com/files/32bit/doc/data_sheet/KL17P64M48SF6.pdf?fsrch=1&sr=1&pageNum=1) on Surface Pro 4, Surface Book 1, ..?

In addition to these two chips, there are also two different communication interfaces:

- The Surface Book 2, Surface Pro 5, Surface Pro 6, Surface Laptop 1, and Surface Laptop 2 use a UART serial bus.

- The Surface Book 1 and Surface Pro 4 use HID-over-I2C.

Currently only the first interface is supported, meaning this module does currently not support the Surface Book 1 and Surface Pro 4.
 

## Donations

_I can't really guarantee you anything._
_I can't promise you the support you may want._
_I'm doing this in my free time and I only have a Surface Book 2._
_I'll try to do my best, but it may take a while or at worst, your issue may never get resolved._
_Also please do not donate if you need the money, you probably need it more than me._
If that can't stop you:
https://www.paypal.me/maximilianluz
Don't get me wrong though, I do appreciate your donations.
Thank you for your support!
