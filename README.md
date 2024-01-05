## fancurve

A SMC daemon for Intel MacBooks to run the fan harder when the machine is hot.

I find that my i9 MacBook Pro overheats without this. 🔥😑🔥

### Disclaimer

I haven't tested this program anywhere besides my 2018 `MacBookPro15,1`.

The program likely works on most Intel MacBooks, and maybe even ARM and
Mac desktop computers. But, it might not work or even cause problems due
to SMC keys being different.

You have been warned. Don't use this if you're not willing to check that
it's working properly for you.

### Build

`c++ -std=c++20 -framework IOKit -framework ApplicationServices ./fancurve.cc -o ./fancurve`

### Install

`./install.sh`

This installs fancurve in /Library/LaunchDaemons so it starts on boot.
`./fancurve` can also run without being installed.

(But, avoid both installing it and running it standalone, or else the
two processes will fight over the fan.)

### Output

By default, on a tty, the program will emit a rolling log of the target fan
speed expressed as a percentage, as well as the temperature and SMC key of the
sensor primarily responsible for causing elevated fan speed.

### Notes

The algorithm for setting the fan speed is approximately: each SMC temperature
sensor is read, and the value is clamped and linearly normalized to a 0%–100%
range. Then, the maximum percentage from this process is applied as the speed
of all fans. This process is repeated every couple of seconds.

For most temperature sensors, the clamping range is 60°C to 70°C, but there
are alternate hardcoded ranges. For example, the CPU core temp sensors have
a much higher range (allowing the cores to get much hotter than 70°C before
raising the fan speed), while the laptop exterior case temp sensors have a
much lower range (maxing out the fan speed above any skin-unsafe temperature).