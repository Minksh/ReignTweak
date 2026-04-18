#  ReignTweak 

![Proton](https://img.shields.io/badge/compatibility-Proton%2FWine-purple?style=for-the-badge)

 **ReignTweak** is a runtime tool that allows you to:
-  Patch the framerate
-  Apply ultrawide
-  Use experimental depth buffer scaling for a (potential) performance gain

Works **ONLY** on Proton/Wine due to how EAC is implemented on Linux.  
Use at your own risk (obviously)

---
<img src="https://github.com/user-attachments/assets/6bc5a4c4-4812-4697-94d6-2a596d582436" style="width: 50%;">

## Installation
put `reigntweak` somewhere in your $PATH (/usr/local/bin/). (from releases) or build it yourself using the provided bash script.


### DEPTH BUFFER SCALING IS VERY EXPERIMENTAL 
The lowest it will go is lowres_depth 480, not giving a value will default to half res. Might crash your game while loading.

## Usage


- reigntweak fps <val>         - Changes FPS limit
- reigntweak ultrawide         - Applies ultrawide patch (Can't be used with lowres_depth)
- reigntweak detect_buffers    - Scans for depth buffer logic (requires running game) DEBUGGING ONLY
- reigntweak lowres_depth      - Patches depth buffers to lower res, default value (when none is given) is half native res.

### MAKE SURE YOU RUN THE GAME IN WINDOWED OR BORDERLESS MODE!!!
###FOR ULTRAWIDE, CHANGE THE RESOLUTION IN GAME FOR IT TO TAKE EFFECT!
## Example launch options:

reigntweak fps 100 ultrawide %command%



## Acknowledgements

https://github.com/techiew/EldenRingMods For the Ultrawide patterns
