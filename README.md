# AudioMoth-USB-Microphone-Cmd #

This is a command line tool for configuring the AudioMoth USB Microphone. It works with version 1.3.0 and greater of the AudioMoth-USB-Microphone firmware.

## How to use it ##

The AudioMoth-USB-Microphone command line tool is described in detail in the Application Note [here]().

The following command will set all connected AudioMoth USB Microphones to default settings with a sample rate of 48kHz.

```
> AudioMoth-USB-Microphone config 48000
```

The following command does the same on Windows.

```
> AudioMoth-USB-Microphone.exe config 48000
```

## Building ##

AudioMoth-USB-Microphone can be built on macOS using the Xcode Command Line Tools.

```
> clang -I../src/macOS/ -framework CoreFoundation -framework IOKit ../src/main.c ../src/macOS/hid.c -o AudioMoth-USB-Microphone   
```

AudioMoth-USB-Microphone can be built on Windows using the Microsoft Visual C++ Build Tools. Note that to build the correct version you should run the command in the correct environment. Use the 'x64 Native Tools Command Prompt' to build the 64-bit binary on a 64-bit machine, and the 'x64_x86 Cross Tools Command Prompt' to build the 32-bit binary on a 64-bit machine.

```
cl /I ..\src\windows\ ..\src\main.c ..\src\windows\hid.c /link /out:AudioMoth-USB-Microphone.exe SetupAPI.lib
```

AudioMoth-USB-Microphone can be built on Linux and Raspberry Pi using `gcc`.

```
gcc -Wall -std=c99 -I/usr/include/libusb-1.0 -I../src/linux/ ../src/main.c ../src/linux/hid.c -o AudioMoth-USB-Microphone -lusb-1.0 -lrt -lpthread
```

The libusb development library must be installed.

```
> sudo apt-get install libusb-1.0-0-dev
```

By default, Linux prevents writing to certain types of USB devices such as the AudioMoth. To use this application you must first navigate to `/lib/udev/rules.d/` and create a new file (or edit the existing file) with the name `99-audiomoth.rules`:

```
> cd /lib/udev/rules.d/
> sudo gedit 99-audiomoth.rules
```

Then enter the following text:

```
SUBSYSTEM=="usb", ATTRS{idVendor}=="16d0", ATTRS{idProduct}=="06f3", MODE="0666" 
```

On certain Linux distributions, you may have to manually set the permissions for ports to allow the app to communicate with the AudioMoth. If you experience connection issues, try the following command:
​
```
> sudo usermod -a -G dialout $(whoami)
```

On macOS, Linux and Raspberry Pi you can copy the resulting executable to `/usr/local/bin/` so it is immediately accessible from the terminal. On Windows copy the executable to a permanent location and add this location to the `PATH` variable.
