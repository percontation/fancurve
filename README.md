## fancurve

A SMC daemon for Intel MacBooks to run the fan harder when the machine is hot.

I find that my i9 MacBook Pro overheats without this. 😑

### Build

`c++ -std=c++20 -framework IOKit -framework ApplicationServices ./fancurve.cc -o ./fancurve`

### Install

`./install.sh`

This installs fancurve in /Library/LaunchDaemons so it starts on boot. `./fancurve` can also run without being installed.

(But, avoid both installing it and running it standalone, or else the two processes will fight over the fan.)

### Disclaimer

I haven't tested this program anywhere besides my 2018 `MacBookPro15,1`.

The program likely works on most Intel MacBooks, and maybe even ARM and Desktops. But, it might not work or even cause problems due to SMC keys being different.

You have been warned. Don't use this if you're not willing to check that it's working properly for you.
