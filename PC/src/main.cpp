#include <iostream>
#include <filesystem>
#include <vector>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "lzss.h"

// Frame flags
#define FLAG_COMPRESSION_STAY            1
#define FLAG_COMPRESSION_CHARACTERS     (1 << 1)
#define FLAG_COMPRESSION_LZ77           (1 << 2)

// DS Screen data
constexpr int imgWidth = 256;
constexpr int imgHeight = 192;
constexpr int tileWidth = 8;
constexpr int tileHeight = 8;

constexpr int sampleSize = 3200;

// CUE's LZSS compressor
extern "C" char *LZS_Fast(unsigned char *raw_buffer, int raw_len, int *new_len);

// Storing characters in classes makes it a bit easier to work with them later on
class Character {
public:
    Character() : pixels{} {}

    uint8_t* getPixels() {
        return pixels;
    }

private:
    uint8_t pixels[tileWidth * tileHeight];
};

bool operator==(Character& c1, Character& c2) {
    for (int i = 0; i < tileWidth * tileHeight; i++) {
        if (c1.getPixels()[i] != c2.getPixels()[i]) {
            return false;
        }
    }
    return true;
}

// Get the perceived brightness
uint8_t getBrightness(const uint8_t* pixel) {
    return static_cast<uint8_t>((0.2126 * pixel[0]) + (0.7152 * pixel[1]) + (0.0722 * pixel[2]));
}

// Checks if a given tile is in the tile map
bool checkForTile(std::vector<Character>& tileMap, Character& tile, uint16_t* location = nullptr) {
    for (size_t i = 0; i < tileMap.size(); i++) {
        if (tileMap[i] == tile) {
            if (location != nullptr) {
                *location = i;
            }
            return true;
        }
    }
    return false;
}

// Loads tile map from image
void loadTileMap(std::vector<Character>& tileMap, uint16_t* map, uint8_t* img) {
    tileMap.clear();
    Character tileBuffer;
    uint16_t index;

    // Iterates over all tiles in the image
    for (int y = 0; y < imgHeight; y += tileHeight) {
        for (int x = 0; x < imgWidth; x += tileWidth) {
            // Copies a tile into the tileBuffer
            for (int h = 0; h < tileHeight; h++) {
                memcpy(&tileBuffer.getPixels()[h * tileWidth], &img[(y + h) * 256 + x], tileWidth);
            }

            // Checks if the tile is already in the tile map
            if (!checkForTile(tileMap, tileBuffer, &index)) {
                map[(y / tileHeight) * imgWidth / tileWidth + x / tileWidth] = tileMap.size() + 0x18;
                tileMap.push_back(tileBuffer);
            } else {
                map[(y / tileHeight) * imgWidth / tileWidth + x / tileWidth] = index + 0x18;
            }
        }
    }
}

void compressFrame(uint8_t* dataIn, uint8_t*& imgData, uint8_t* bufferImg, uint8_t& flags, size_t& imgDataSize) {
    // Used for calculating sizes later
    int mapSize = (imgWidth / tileWidth) * (imgHeight / tileHeight);
    int charBaseSize = (imgWidth / tileWidth) * (imgHeight / tileHeight) * (tileWidth * tileHeight);

    std::vector<Character> tileMap;

    auto *map = new uint16_t[charBaseSize / 2 + mapSize];   // Stores map and tiles
    auto *img = new uint8_t[imgWidth * imgHeight];          // Stores the grayscale image

    bool changed = false;

    // Converts the image that got loaded by stb_image to an image based on our grayscale perception
    // It also checks if the last frame was different
    for (int y = 0; y < imgHeight; y++) {
        for (int x = 0; x < imgWidth; x++) {
            img[y * imgWidth + x] = getBrightness(&dataIn[3 * (y * imgWidth + x)]);
            if (img[y * imgWidth + x] != bufferImg[y * imgWidth + x]) {
                changed = true;
            }
        }
    }

    // Executes if nothing has changed since the last frame
    if (!changed) {
        flags = FLAG_COMPRESSION_STAY;

        delete[] map;
        delete[] img;
        return;
    }

    loadTileMap(tileMap, map, img);
    uint16_t tileMapSize = tileMap.size() * tileWidth * tileHeight;    // Calculate size of tile map in bytes

    // Copies the tiles to the map
    for (size_t i = 0; i < tileMap.size(); i++) {
        memmove(&map[mapSize + 32 * i], tileMap[i].getPixels(), 64);
    }

    // Compress the image using CUE's LZSS function
    imgData = reinterpret_cast<uint8_t*>(LZS_Fast(reinterpret_cast<uint8_t*>(map), tileMapSize + mapSize * 2,
                                                  reinterpret_cast<int*> (&imgDataSize)));

    // Update the image buffer
    memmove(bufferImg, img, imgWidth * imgHeight);

    flags = FLAG_COMPRESSION_CHARACTERS | FLAG_COMPRESSION_LZ77;

    // Clean up the data
    delete[] map;
    delete[] img;
}

int main()
{
    // needed for stb_image
    int width, height, bpp;

    // Used to compress image data
    uint8_t bufferImg[imgWidth * imgHeight];   // Stores the last image
    uint8_t* imgData;                          // Stores the compressed image
    size_t imgDataSize;
    uint8_t flags;

    // Counts the frames
    unsigned int frameNum;

    frameNum = 0;
    uint8_t* img;

    std::vector<uint8_t> videoData;
    // Header of my video format
    videoData.push_back(0);
    videoData.push_back(0);
    videoData.push_back(0);
    videoData.push_back(0);

    memset(bufferImg, 0, imgWidth * imgHeight);

    std::ifstream audioFile("audio.raw", std::ios::binary);

    if (!audioFile) {
        printf("Error: Couldn't open audio.raw\n");
        return 1;
    }

    // Audio buffers used for unpacking one stereo track into 2 mono tracks
    char aBufferL[sampleSize * 2];
    char aBufferR[sampleSize * 2];

    size_t audioLen = std::filesystem::file_size("audio.raw");
    size_t audioOff = 0;

    // Preloads 12 audio blocks
    for (int i = 0; i < 12; i++) {
        for (int j = 0; j < sampleSize; j++) {
            // If there's still audio to be read read it. Otherwise fill the buffers with 0s
            if (audioOff < audioLen) {
                audioFile.read(&aBufferL[j * 2], 2);
                audioFile.read(&aBufferR[j * 2], 2);
            } else {
                aBufferL[j * 2] = 0;
                aBufferR[j * 2] = 0;
                aBufferL[j * 2 + 1] = 0;
                aBufferR[j * 2 + 1] = 0;
            }
            audioOff += 4;
        }

        videoData.insert(videoData.end(), &aBufferL[0], &aBufferL[sampleSize * 2]);
        videoData.insert(videoData.end(), &aBufferR[0], &aBufferR[sampleSize * 2]);
    }

    for (auto& p : std::filesystem::directory_iterator("imgs")) {
        if (!(frameNum % 4)) {
            for (int i = 0; i < sampleSize; i++) {
                if (audioOff < audioLen) {
                    audioFile.read(&aBufferL[i * 2], 2);
                    audioFile.read(&aBufferR[i * 2], 2);
                } else {
                    aBufferL[i * 2] = 0;
                    aBufferR[i * 2] = 0;
                    aBufferL[i * 2 + 1] = 0;
                    aBufferR[i * 2 + 1] = 0;
                }
                audioOff += 4;
            }

            videoData.insert(videoData.end(), &aBufferL[0], &aBufferL[sampleSize * 2]);
            videoData.insert(videoData.end(), &aBufferR[0], &aBufferR[sampleSize * 2]);
        }

        // Load image and divide all values by 8
        img = stbi_load(p.path().string().c_str(), &width, &height, &bpp, 0);
        for (int j = 0; j < 256 * 192 * 3; j += 3) {
            img[j] = img[j] >> 3;
            img[j + 1] = img[j + 1] >> 3;
            img[j + 2] = img[j + 2] >> 3;
        }

        compressFrame(img, imgData, bufferImg, flags, imgDataSize);

        videoData.push_back(flags);
        if (flags != FLAG_COMPRESSION_STAY) {
            videoData.push_back(imgDataSize & 0xFF);
            videoData.push_back((imgDataSize >> 8) & 0xFF);
            videoData.insert(videoData.end(), imgData, imgData + imgDataSize);
            free(imgData);  // Clean up memory used by CUE's LZSS function
        }

        frameNum++;

        if ((frameNum % 1000) == 0) {
            std::cout << frameNum << std::endl;
        }

        stbi_image_free(img);
    }
    std::cout << std::endl;

    // Write the number of frames into the file header
    memcpy(videoData.data(), &frameNum, 4);

    std::ofstream output("BadApple.kpv", std::ios::binary);

    if (!output) {
        std::cout << "Error: Couldn't open output file" << std::endl;
        return 1;
    }

    output.write(reinterpret_cast<char*>(videoData.data()), static_cast<long long int>(videoData.size()));
    output.close();

    return 0;
}
