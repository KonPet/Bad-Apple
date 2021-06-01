#include <nds.h>
#include <fat.h>
#include <cstdio>
#include <string>
#include "NDSBG.h"

// The flags for telling what the next frame will be
#define FLAG_COMPRESSION_STAY           1
#define FLAG_COMPRESSION_CHARACTERS     (1 << 1)
#define FLAG_COMPRESSION_LZ77           (1 << 2)

// Used for storing the compressed data
uint8_t vramBuffer[1024*64];

// All the audio stuff
constexpr int audioBufferSize = 15;             // Used for telling many audio blocks we have in our buffer
constexpr int sampleSize = 3200;                // How many points each audio block consists of
volatile uint8_t audioBlock = 0;                // Used to tell which audio blcok to write to/read from
uint16_t audioL[sampleSize * audioBufferSize];  // Audio buffer for the left speaker
uint16_t audioR[sampleSize * audioBufferSize];  // Audio buffer for the right speaker

constexpr int queueSize = 8;                        // How many frame buffers are in the queue
constexpr int frameBufferSize = 0xC000;             // Size of each frame buffer
uint8_t frameBuffers[queueSize][frameBufferSize];   // Frame buffer queue
volatile uint8_t drawQueue = 0;                     // Used for telling which frames to redraw and which frames stay the same

volatile uint8_t curFrameBuffer = 1;    // Stores which frame buffer to write to
volatile uint8_t drawFrame = 0;         // Stores which frame buffer to draw
volatile uint8_t numFramesQueue = 1;    // Stores the amount of loaded frame buffers

volatile uint16_t* palette = (volatile uint16_t*) 0x05000000;   // The palette holding all 32 brightness levels

// The places at which we'll store the images for top and bottom screen
void* vramA = (void*) 0x06000000;
void *vramC = (void*) 0x06204000;

uint8_t flags;                      // Stores the flags of the upcoming frame
uint16_t blockLength;               // Stores the size of the upcoming frame
volatile int frame = 0;             // Stores the amount of frames drawn
volatile int framesRead = 0;        // Stores the amount of frames read
int numFrames;                      // Stores the total number of frames in the video
volatile bool queueLoad = false;    // Used for stopping the next frame in case of a reading error

FILE* videoFile;

// Gets executed everytime a VBlank interrupt occurs (so exactly 60 times a second)
void VBlankProc() {
    if (queueLoad) {
        if (drawQueue & (1 << drawFrame)) {
            // Loads the next frame from the queue into VRam
            dmaCopyWordsAsynch(3, frameBuffers[drawFrame], vramA, frameBufferSize);
        }
        frame++;
        numFramesQueue--;
        drawFrame++;
        drawFrame %= queueSize;
        printf("\x1b[1;1H%i of %i       ", frame, numFrames);
    }
}

int main()
{
    // Initialize SD card. Requires DSi mode
    if (!fatInitDefault()) {
        // If it fails, print an error and wait until you press START. Then exit
        consoleDemoInit();
        printf("Couldn't initialize FAT\nYou must run this game with SD card access");
        while (true) {
            scanKeys();
            if (keysDown() & KEY_START) break;
        }
    }

    // Initialize no$gba debug console
    consoleDebugInit(DebugDevice_NOCASH);
    fprintf(stderr, "Initialized FAT\n%p | %p | %p | %p, %x\n", vramBuffer, &blockLength, &numFrames, frameBuffers, frameBufferSize);

    // Load the video file
    videoFile = fopen("BadApple.kpv", "rb");

    if (!videoFile) {
        consoleDemoInit();
        printf("Couldn't open file");
        while (true) {
            scanKeys();
            if (keysDown() & KEY_START) break;
        }
    }

    // Read number of frames
    fread(&numFrames, 4, 1, videoFile);

    // Set video modes
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_3_2D);
    vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
    vramSetBankC(VRAM_C_SUB_BG_0x06200000);

    // Initialize the backgrounds for the images
    bgInit(0, BgType_Text8bpp, BgSize_T_256x256, 0, 0);
    bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 1, 0);

    // Initialize the console for the bottom screen frame counter
    consoleInit(nullptr, 0, BgType_Text4bpp, BgSize_T_256x256, 4, 0, false, true);

    // Reset the top screen
    memset((void*) vramA, 0, 256*256*2);

    // Set up the palette for the top screen
    for (int i = 0; i < 32; i++) {
        palette[31 - i] = i | (i << 5) | ((i << 10)) | (1 << 15);
    }

    // Set the console color
    *(uint16_t*)0x50005fe = 0x7518;

    // Display the background on the bottom screen
    memcpy(vramC, NDSBGBitmap, NDSBGBitmapLen);

    // Set up the VBlankProc to execute everytime the NDS calls the VBlank interrupt
    irqSet(IRQ_VBLANK, VBlankProc);

    // Reset audio buffers
    memset(audioL, 0, sampleSize * 2);
    memset(audioR, 0, sampleSize * 2);

    soundEnable();

    // Preload 12 audio blocks
    for (int i = 0; i < 12; i++) {
        fread(&audioL[audioBlock * sampleSize], 1, sampleSize * 2, videoFile);
        fread(&audioR[audioBlock * sampleSize], 1, sampleSize * 2, videoFile);
        audioBlock++;
    }

    // Don't forget to flush the cache
    DC_FlushAll();

    // Activate the drawing function
    queueLoad = true;
    frame = 0;

    // Main loop
    while(true)
    {
        // Wait until we're able to load a new frame
        while (numFramesQueue >= queueSize || ((curFrameBuffer + 1) % queueSize) == drawFrame);

        // Load audio every 4 frames
        if (!(framesRead % 4)) {
            if (framesRead < numFrames) {
                // Read audio blocks
                fread(&audioL[audioBlock * sampleSize], 1, sampleSize * 2, videoFile);
                fread(&audioR[audioBlock * sampleSize], 1, sampleSize * 2, videoFile);

                // Flush the cache
                DC_FlushRange(&audioL[audioBlock * sampleSize], sampleSize * 2);
                DC_FlushRange(&audioR[audioBlock * sampleSize], sampleSize * 2);

                // Activate audio streaming on frame 0
                if (framesRead == 0) {
                    soundPlaySample(audioL, SoundFormat_16Bit, sampleSize * audioBufferSize * 2, sampleSize * audioBufferSize, 127, 0, true, 0);
                    soundPlaySample(audioR, SoundFormat_16Bit, sampleSize * audioBufferSize * 2, sampleSize * audioBufferSize, 127, 127, true, 0);
                }

                audioBlock = (audioBlock + 1) % audioBufferSize;
            } else {
                memset(&audioL[audioBlock * sampleSize], 0, sampleSize * 2);
                memset(&audioR[audioBlock * sampleSize], 0, sampleSize * 2);
                audioBlock = (audioBlock + 1) % audioBufferSize;
            }
        }

        if (framesRead < numFrames) {
            fread(&flags, 1, 1, videoFile);
            if (flags & (FLAG_COMPRESSION_LZ77 | FLAG_COMPRESSION_CHARACTERS)) {
                fread(&blockLength, 2, 1, videoFile);
                fread(vramBuffer, 1, blockLength, videoFile);

                // Draw the frame that got loaded
                drawQueue |= (1 << curFrameBuffer);

                decompress(vramBuffer, frameBuffers[curFrameBuffer], LZ77);
                DC_FlushRange(frameBuffers[curFrameBuffer], frameBufferSize);
            } else if (flags == FLAG_COMPRESSION_STAY) {
                // Don't draw the loaded frame
                drawQueue &= ~(1 << curFrameBuffer);
            } else { // If the flag byte is invalid execute this
                // Don't draw the loaded frame
                queueLoad = false;

                // Invalidate the counter that counts the frames in the queue so we stop loading new ones
                curFrameBuffer = queueSize;
                printf("\x1b[11;1HCritical Error: Invalid flag byte");
            }

            // Increment frame counters
            curFrameBuffer = (curFrameBuffer + 1) % queueSize;
            numFramesQueue++;

            framesRead++;
        } else {
            memset(&frameBuffers[curFrameBuffer][1], 0, frameBufferSize);
        }

        scanKeys();
        if (keysDown() & KEY_START) {
            break;
        }
    }
    return 0;
}