#pragma once
#include <stdint.h>
typedef struct { uint32_t offset, bytes; } whale_pcm_asset_t;
static const whale_pcm_asset_t whale_pcm_assets[8]={{0,13760},{13760,11840},{25600,4800},{30400,14080},{44480,30670},{75150,58992},{134142,21104},{155246,43404}};
typedef struct { uint32_t offset; uint16_t width, height; } whale_image_asset_t;
static const whale_image_asset_t whale_image_assets[8]={{0,196,196},{115248,196,196},{230496,196,196},{345744,196,196},{460992,196,196},{576240,196,196},{691488,48,44},{697824,24,34}};
#define WHALE_COMPANION_BYTES 700272
#define WHALE_PCM_BYTES 198650
