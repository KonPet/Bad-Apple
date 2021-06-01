# Bad-Apple-NDS
https://youtu.be/FeuL7pCHlI4

## Building (NDS)
To build `BadApple.nds`, you must have devkitPro with libnds installed. Then just go into the `NDS` directory and run `make`.

## Building (PC)
To build the encoder run these commands in the `PC` directory:

```
mkdir build
cd build
cmake .. -G "MSYS Makefiles"
make
```

To run the encoder, you must have ffmpeg installed. Then simply run these commands in the directory where `BadAppleEncoder.exe` is:

```
mkdir imgs
ffmpeg -i input.mp4 -vf scale=256:192 -r 60/1 imgs\%05d.png -acodec pcm_s16le -f s16le -ar 48000 audio.raw
BadAppleEncode.exe
```

Now my program should be generating a video file that you can play on your NDS.

## Running (NDS)
If you are running the homebrew through Unlaunch or no$gba, put `BadApple.kpv` onto the root directory of your SD card. Otherwise put it into the same directory as `BadApple.nds`. Now just run `BadApple.nds` in DSi mode with SD card access.

## Credits
I didn't do all of this myself. I got a lot of help from **Gericom**, who also made [the original version](https://gbatemp.net/threads/bad-apple-for-the-nintendo-ds.466504/) 4 years ago, which this version is heavily inspired by.
I sadly wasn't able to get my own LZSS compressor to work, so I had to use CUE's. For loading the images I chose stb_image.h

## Known bugs
If the video is too long, the audio will start to desynch. Bad Apple isn't long enough though. It may get fixed in a later release, but currently I don't have the time