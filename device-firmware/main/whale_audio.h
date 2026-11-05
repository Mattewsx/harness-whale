#pragma once
#include <stdbool.h>

/* Blocking PCM playback. Call only from the peripheral worker, outside LVGL. */
bool whale_audio_done(void);
