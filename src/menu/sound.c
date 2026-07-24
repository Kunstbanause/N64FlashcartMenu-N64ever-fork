/**
 * @file sound.c
 * @brief Sound component implementation
 * @ingroup ui_components
 */

#include <stdbool.h>
#include <stdio.h>
#include <libdragon.h>
#include "mp3_player.h"
#include "sound.h"
#include "utils/fs.h"

#define SOUNDS_SD_DIRECTORY "menu/n64ever/audio"

#define DEFAULT_FREQUENCY   (44100)
#define NUM_BUFFERS         (4)
#define NUM_CHANNELS        (16)

static wav64_t sfx_cursor, sfx_error, sfx_enter, sfx_exit, sfx_setting;
static wav64_t sfx_grid_move, sfx_grid_enter, sfx_grid_back, sfx_launch;

static bool sound_initialized = false;
static bool sfx_enabled       = false;
static bool sfx_grid_loaded   = false;
static bool grid_sfx_active   = false;

/**
 * @brief Reconfigure the sound system with the specified frequency.
 * 
 * @param frequency The audio frequency.
 */
static void sound_reconfigure (int frequency) {
    if ((frequency > 0) && (audio_get_frequency() != frequency)) {
        
        sound_deinit();

        audio_init(frequency, NUM_BUFFERS);
        mixer_init(NUM_CHANNELS);

        // Attempt to initialize wav64 compression level 1
        wav64_init_compression(1);

        // Initialize MP3 player mixer
        mp3player_mixer_init();
        sound_initialized = true;

        if (sfx_enabled) {
            sound_init_sfx();
        }
    }
}

/**
 * @brief Initialize the default sound system.
 */
void sound_init_default (void) {
    sound_reconfigure(DEFAULT_FREQUENCY);
}

/**
 * @brief Initialize the sound system for MP3 playback.
 */
void sound_init_mp3_playback (void) {
    sound_reconfigure(mp3player_get_samplerate());
}

/**
 * @brief Initialize the sound effects.
 */
void sound_init_sfx (void) {
    // Ensure SFX channel can play standard 44.1 kHz effects even if the
    // global mixer/sample rate was reconfigured to a lower value for MP3.
    mixer_ch_set_limits(SOUND_SFX_CHANNEL, 16, DEFAULT_FREQUENCY, 0);
    mixer_ch_set_vol(SOUND_SFX_CHANNEL, 0.5f, 0.5f);
    wav64_open(&sfx_cursor,  "rom:/cursorsound.wav64");
    wav64_open(&sfx_exit,    "rom:/back.wav64");
    wav64_open(&sfx_setting, "rom:/settings.wav64");
    wav64_open(&sfx_enter,   "rom:/enter.wav64");
    wav64_open(&sfx_error,   "rom:/error.wav64");
    sfx_enabled = true;
}

/**
 * @brief Load grid SFX from ROM; if the user placed files at sd:/menu/sounds/ those override.
 */
void sound_init_grid_sfx(const char *storage_prefix) {
    wav64_open(&sfx_grid_move,  "rom:/grid_move.wav64");
    wav64_open(&sfx_grid_enter, "rom:/grid_enter.wav64");
    wav64_open(&sfx_grid_back,  "rom:/grid_back.wav64");
    wav64_open(&sfx_launch,     "rom:/launch.wav64");

    /* SD overrides: if user placed their own files, prefer those */
    char p[128];
    snprintf(p, sizeof(p), "%s/" SOUNDS_SD_DIRECTORY "/grid_move.wav64",  storage_prefix);
    if (file_exists(p)) { wav64_close(&sfx_grid_move);  wav64_open(&sfx_grid_move,  p); }
    snprintf(p, sizeof(p), "%s/" SOUNDS_SD_DIRECTORY "/grid_enter.wav64", storage_prefix);
    if (file_exists(p)) { wav64_close(&sfx_grid_enter); wav64_open(&sfx_grid_enter, p); }
    snprintf(p, sizeof(p), "%s/" SOUNDS_SD_DIRECTORY "/grid_back.wav64",  storage_prefix);
    if (file_exists(p)) { wav64_close(&sfx_grid_back);  wav64_open(&sfx_grid_back,  p); }
    snprintf(p, sizeof(p), "%s/" SOUNDS_SD_DIRECTORY "/launch.wav64",     storage_prefix);
    if (file_exists(p)) { wav64_close(&sfx_launch);     wav64_open(&sfx_launch,     p); }

    sfx_grid_loaded = true;
}

void sound_set_grid_sfx_enabled(bool enabled) {
    grid_sfx_active = enabled;
}

static void sound_deinit_grid_sfx(void) {
    if (sfx_grid_loaded) {
        wav64_close(&sfx_grid_move);
        wav64_close(&sfx_grid_enter);
        wav64_close(&sfx_grid_back);
        wav64_close(&sfx_launch);
        sfx_grid_loaded = false;
    }
}

/**
 * @brief Enable or disable sound effects.
 *
 * @param state True to enable, false to disable.
 */
void sound_use_sfx(bool state) {
    sfx_enabled = state;
}

/**
 * @brief Play a sound effect.
 * 
 * @param sfx The sound effect to play.
 */
void sound_play_effect(sound_effect_t sfx) {
    if(sfx_enabled) {
        switch (sfx) {
            case SFX_CURSOR:
                wav64_play(&sfx_cursor, SOUND_SFX_CHANNEL);
                break;
            case SFX_EXIT:
                wav64_play(&sfx_exit, SOUND_SFX_CHANNEL);
                break;
            case SFX_SETTING:
                wav64_play(&sfx_setting, SOUND_SFX_CHANNEL);
                break;
            case SFX_ENTER:
                wav64_play(&sfx_enter, SOUND_SFX_CHANNEL);
                break;
            case SFX_ERROR:
                wav64_play(&sfx_error, SOUND_SFX_CHANNEL);
                break;
            case SFX_GRID_MOVE:
                if (sfx_grid_loaded && grid_sfx_active) wav64_play(&sfx_grid_move, SOUND_SFX_CHANNEL);
                break;
            case SFX_GRID_ENTER:
                if (sfx_grid_loaded && grid_sfx_active) wav64_play(&sfx_grid_enter, SOUND_SFX_CHANNEL);
                break;
            case SFX_GRID_BACK:
                if (sfx_grid_loaded && grid_sfx_active) wav64_play(&sfx_grid_back, SOUND_SFX_CHANNEL);
                break;
            case SFX_LAUNCH:
                if (sfx_grid_loaded) wav64_play(&sfx_launch, SOUND_SFX_CHANNEL);
                break;
            default:
                break;
        } 
    }
}

/**
 * @brief Deinitialize the sound system.
 */
void sound_deinit (void) {
    if (sound_initialized) {
        sound_deinit_grid_sfx();
        if (sfx_enabled) {
            wav64_close(&sfx_cursor);
            wav64_close(&sfx_exit);
            wav64_close(&sfx_setting);
            wav64_close(&sfx_enter);
            wav64_close(&sfx_error);
        }
        mixer_close();
        audio_close();
        sound_initialized = false;
    }
}

/**
 * @brief Poll the sound system to process audio playback.
 */
void sound_poll (void) {
    if (sound_initialized) {
        
        // Check whether one audio buffer is ready, otherwise wait for next
        // frame to perform mixing.
        mixer_try_play();
    }
}
