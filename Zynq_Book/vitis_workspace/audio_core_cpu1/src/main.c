//  main.c  CPU1 (Audio Core)

#include <stdio.h>
#include "xil_printf.h"
#include "xil_io.h"
#include "sleep.h"
#include "audio.h"
#include "xparameters.h"
#include "xgpiops.h"
#include "beatmap.h"
#include "shared_memory.h"


#define MIXER_BASE_ADDR      0x43C10000
#define MIXER_BACKING_REG    (MIXER_BASE_ADDR + 0x00)
#define MIXER_VOCAL_REG      (MIXER_BASE_ADDR + 0x04)
#define MIXER_BUTTON_REG     (MIXER_BASE_ADDR + 0x08)
#define MIXER_OUTPUT_REG     (MIXER_BASE_ADDR + 0x0C)
#define MIXER_VOLUME_REG     (MIXER_BASE_ADDR + 0x10)

#define AUDIO_CTRL_BASE      XPAR_ZED_AUDIO_CTRL_0_BASEADDR
#define I2S_DATA_TX_L_REG    (AUDIO_CTRL_BASE + 0x08)
#define I2S_DATA_TX_R_REG    (AUDIO_CTRL_BASE + 0x0C)


// Song 1  Harder Better Faster Stronger
#define S1_ADDR_BACKING     0x11000000
#define S1_LEN_BACKING      6084420

// Song 2 - Levels
#define S2_ADDR_BACKING     0x14000000
#define S2_LEN_BACKING      7070576

// Song 3 - beat it
#define S3_ADDR_BACKING     0x17000000
#define S3_LEN_BACKING      7672336


#define GPIO_PS_DEVICE_ID    XPAR_PS7_GPIO_0_DEVICE_ID
#define LED_MIO_PIN          7
#define LED_BLINK_DURATION   12000
#define HIT_RESULT_HOLD_SAMPLES  48000   // hold HIT/MISS indicator

static XGpioPs gpio_ps;


static u32       active_backing_addr;
static u32       active_backing_len;
static SongNote *active_beatmap;
static int       active_num_notes;
static int       active_song;


static u32 led_off_at       = 0;
static int led_blink_active = 0;


static u32 score              = 0;
static u32 combo              = 0;
static u32 multiplier         = 1;
static int next_miss_check    = 0;
static u32 notes_hit_count    = 0;
static u32 notes_missed_count = 0;
static u32 hit_seq            = 0;

static u32  playback_index           = 0;
static u8   btn_prev                 = 0;
static int  debounce_timer           = 0;
static u8   btn_now                  = 0;
static u32  hit_result_clear_at      = 0;
static int  hit_result_pending_clear = 0;
static int  backing_track_done       = 0;


static int current_segment    = -1;
static int next_segment_enter = 0;


static void silence_codec() {
    Xil_Out32(MIXER_BACKING_REG, 0);
    Xil_Out32(MIXER_VOCAL_REG,   0);
    Xil_Out32(I2S_DATA_TX_L_REG, 0);
    Xil_Out32(I2S_DATA_TX_R_REG, 0);
    XGpioPs_WritePin(&gpio_ps, LED_MIO_PIN, 0);
    led_blink_active = 0;
}



static void configure_song(u32 song_sel) {
    active_song = (int)song_sel;

    switch (song_sel) {

    case SONG_2:
        active_backing_addr = S2_ADDR_BACKING;
        active_backing_len  = S2_LEN_BACKING;
        active_beatmap      = beatmap_song2;
        active_num_notes    = NUM_NOTES_SONG2;
        xil_printf("Song 2 selected (%d segments)\r\n", active_num_notes);
        break;

    case SONG_3:
        active_backing_addr = S3_ADDR_BACKING;
        active_backing_len  = S3_LEN_BACKING;
        active_beatmap      = beatmap_song3;
        active_num_notes    = NUM_NOTES_SONG3;
        xil_printf("Song 3 selected (%d segments)\r\n", active_num_notes);
        break;

    case SONG_1:
    default:
        active_backing_addr = S1_ADDR_BACKING;
        active_backing_len  = S1_LEN_BACKING;
        active_beatmap      = beatmap_song1;
        active_num_notes    = NUM_NOTES_SONG1;
        xil_printf("Song 1 selected (%d segments)\r\n", active_num_notes);
        break;
    }
}


static void reset_game_state() {
    score              = 0;
    combo              = 0;
    multiplier         = 1;
    next_miss_check    = 0;
    notes_hit_count    = 0;
    notes_missed_count = 0;
    hit_seq            = 0;

    playback_index           = 0;
    btn_prev                 = 0;
    debounce_timer           = 0;
    btn_now                  = 0;
    hit_result_clear_at      = 0;
    hit_result_pending_clear = 0;
    led_off_at               = 0;
    led_blink_active         = 0;
    backing_track_done       = 0;

    current_segment    = -1;
    next_segment_enter = 0;

    XGpioPs_WritePin(&gpio_ps, LED_MIO_PIN, 0);

    if (active_beatmap) {
        for (int i = 0; i < active_num_notes; i++) {
            active_beatmap[i].hit    = 0;
            active_beatmap[i].missed = 0;
        }
    }

    Xil_Out32(SHARED_SCORE,         0);
    Xil_Out32(SHARED_COMBO,         0);
    Xil_Out32(SHARED_MULTIPLIER,    1);
    Xil_Out32(SHARED_HIT_RESULT,    HIT_RESULT_NONE);
    Xil_Out32(SHARED_PLAYBACK_IDX,  0);
    Xil_Out32(SHARED_TOTAL_NOTES,   (u32)active_num_notes);
    Xil_Out32(SHARED_NOTES_HIT,     0);
    Xil_Out32(SHARED_NOTES_MISSED,  0);
    Xil_Out32(SHARED_LAST_NOTE_IDX, 0);
    Xil_Out32(SHARED_HIT_SEQ,       0);

    xil_printf("Game state reset.\r\n");
}


static void update_multiplier() {
    if      (combo >= MULT_4X_THRESHOLD) multiplier = 4;
    else if (combo >= MULT_3X_THRESHOLD) multiplier = 3;
    else if (combo >= MULT_2X_THRESHOLD) multiplier = 2;
    else                                  multiplier = 1;
}

static void register_hit(int note_idx) {
    active_beatmap[note_idx].hit = 1;
    combo++;
    notes_hit_count++;
    update_multiplier();
    score += SCORE_PER_HIT * multiplier;
    hit_seq++;

    Xil_Out32(SHARED_SCORE,         score);
    Xil_Out32(SHARED_COMBO,         combo);
    Xil_Out32(SHARED_MULTIPLIER,    multiplier);
    Xil_Out32(SHARED_HIT_RESULT,    HIT_RESULT_HIT);
    Xil_Out32(SHARED_NOTES_HIT,     notes_hit_count);
    Xil_Out32(SHARED_LAST_NOTE_IDX, (u32)note_idx);
    Xil_Out32(SHARED_HIT_SEQ,       hit_seq);

    hit_result_clear_at      = playback_index + HIT_RESULT_HOLD_SAMPLES;
    hit_result_pending_clear = 1;

    xil_printf("HIT  seg=%d  sample=%lu  score=%lu  combo=%lu  mult=%lux\r\n",
               note_idx, active_beatmap[note_idx].sample, score, combo, multiplier);
}

static void register_miss(int note_idx) {
    active_beatmap[note_idx].missed = 1;
    combo      = 0;
    multiplier = 1;
    notes_missed_count++;
    hit_seq++;

    Xil_Out32(SHARED_COMBO,         combo);
    Xil_Out32(SHARED_MULTIPLIER,    multiplier);
    Xil_Out32(SHARED_HIT_RESULT,    HIT_RESULT_MISS);
    Xil_Out32(SHARED_NOTES_MISSED,  notes_missed_count);
    Xil_Out32(SHARED_LAST_NOTE_IDX, (u32)note_idx);
    Xil_Out32(SHARED_HIT_SEQ,       hit_seq);

    hit_result_clear_at      = playback_index + HIT_RESULT_HOLD_SAMPLES;
    hit_result_pending_clear = 1;

    xil_printf("MISS seg=%d  sample=%lu  segment_end=%lu\r\n",
               note_idx,
               active_beatmap[note_idx].sample,
               active_beatmap[note_idx].segment_end);

    XGpioPs_WritePin(&gpio_ps, LED_MIO_PIN, 1);
    led_off_at       = playback_index + LED_BLINK_DURATION;
    led_blink_active = 1;
}


static void check_hit(u32 idx, u8 buttons_pressed) {
    if (!active_beatmap || active_num_notes == 0) return;
    for (int i = 0; i < active_num_notes; i++) {
        if (active_beatmap[i].hit || active_beatmap[i].missed) continue;
        u32 mid       = active_beatmap[i].sample;
        u32 win_start = (mid > HIT_WINDOW_HALF) ? (mid - HIT_WINDOW_HALF) : 0;
        u32 win_end   = mid + HIT_WINDOW_HALF;
        if (idx >= win_end) continue;
        if (idx < win_start) break;

        u32 assigned = Xil_In32(SHARED_NOTE_BUTTONS + i * 4);
        xil_printf("DEBUG: note=%d  pressed=0x%02x  assigned=0x%02x\r\n",
                   i, buttons_pressed, assigned);

        if (assigned != 0 && !(buttons_pressed & assigned)) {
            xil_printf("WRONG BTN  seg=%d  pressed=0x%02x  needed=0x%02x\r\n",
                       i, buttons_pressed, assigned);
            return;
        }
        register_hit(i);
        return;
    }
    xil_printf("PRESS  idx=%lu  (outside all windows)\r\n", idx);
}

static void check_missed_notes(u32 idx) {
	 if (!active_beatmap || active_num_notes == 0) return;

	    while (next_miss_check < active_num_notes) {
	        SongNote *n = &active_beatmap[next_miss_check];
	        if (n->hit || n->missed) { next_miss_check++; continue; }

	        u32 win_end = n->sample + HIT_WINDOW_HALF;

	        if (idx >= win_end) {
	            register_miss(next_miss_check);
	            next_miss_check++;
	        } else {
	            break;
	        }
	    }
}

static int check_song_end() {
	if (backing_track_done) {
	        return 1;
	    }
	    return 0;
}

static int update_segment_tracking() {
	if (!active_beatmap || active_num_notes == 0) return 0;

	    // Exit current segment when playback passes sample + HIT_WINDOW_HALF
	    if (current_segment >= 0) {
	        u32 win_end = active_beatmap[current_segment].sample + HIT_WINDOW_HALF;
	        if (playback_index >= win_end) {
	            current_segment = -1;
	        }
	    }

	    // Enter the next segment when playback reaches sample - HIT_WINDOW_HALF
	    if (current_segment < 0 && next_segment_enter < active_num_notes) {
	        u32 mid       = active_beatmap[next_segment_enter].sample;
	        u32 win_start = (mid > HIT_WINDOW_HALF) ? (mid - HIT_WINDOW_HALF) : 0;
	        if (playback_index >= win_start) {
	            current_segment = next_segment_enter;
	            next_segment_enter++;
	        }
	    }

	    if (current_segment >= 0 && !active_beatmap[current_segment].hit) {
	        return 1;
	    }

	    return 0;
}


int main() {

    Xil_DCacheDisable();
    AudioPllConfig();
    AudioConfigureJacks();
    LineinLineoutConfig();

    XGpioPs_Config *gpio_cfg = XGpioPs_LookupConfig(GPIO_PS_DEVICE_ID);
    XGpioPs_CfgInitialize(&gpio_ps, gpio_cfg, gpio_cfg->BaseAddr);
    XGpioPs_SetDirectionPin(&gpio_ps, LED_MIO_PIN, 1);
    XGpioPs_SetOutputEnablePin(&gpio_ps, LED_MIO_PIN, 1);
    XGpioPs_WritePin(&gpio_ps, LED_MIO_PIN, 0);

    u32 prev_state       = STATE_MENU;
    int btn_poll_counter = 0;

    Xil_Out32(SHARED_PLAYBACK_IDX, 0);
    Xil_Out32(SHARED_SCORE,        0);
    Xil_Out32(SHARED_COMBO,        0);
    Xil_Out32(SHARED_MULTIPLIER,   1);
    Xil_Out32(SHARED_HIT_RESULT,   HIT_RESULT_NONE);
    Xil_Out32(SHARED_HIT_SEQ,      0);


    xil_printf("Audio core ready, waiting for CPU0 to set STATE_PLAYING\r\n");

    // MAIN LOOP
    while (1) {

        // Poll game state and volume every 480 samples
        btn_poll_counter++;
        int do_poll = (btn_poll_counter >= 480);
        if (do_poll) btn_poll_counter = 0;

        u32 game_state = do_poll
                         ? Xil_In32(SHARED_GAME_STATE)
                         : prev_state;

        if (game_state != prev_state) {

            if (game_state == STATE_PLAYING) {
                if (prev_state == STATE_PAUSED) {
                    xil_printf("Resumed at sample %lu  score=%lu\r\n",
                               playback_index, score);
                } else {
                    u32 song_sel = Xil_In32(SHARED_SONG_SELECT);
                    configure_song(song_sel);
                    reset_game_state();
                    xil_printf("Starting game (song %lu).\r\n", song_sel);
                }

            } else if (game_state == STATE_PAUSED) {
                silence_codec();
                xil_printf("Paused at sample %lu  score=%lu\r\n",
                           playback_index, score);

            } else if (game_state == STATE_MENU) {
                silence_codec();
                xil_printf("Returned to menu.\r\n");

            } else if (game_state == STATE_SONG_WIN) {
                silence_codec();
                xil_printf("Song complete! Final score=%lu\r\n", score);
            }

            prev_state = game_state;
        }

        if (game_state != STATE_PLAYING) {
        	usleep(1000);
            continue;
        }

        if (do_poll) {

            btn_now = Xil_In32(MIXER_BUTTON_REG) & 0x1F;
            u8 buttons_pressed = btn_now & ~btn_prev;

            if (debounce_timer > 0) {
                debounce_timer--;
                buttons_pressed = 0;
            }

            if (buttons_pressed) {
                check_hit(playback_index, buttons_pressed);
                debounce_timer = 15;
            }

            check_missed_notes(playback_index);

            if (hit_result_pending_clear &&
                playback_index >= hit_result_clear_at) {
                Xil_Out32(SHARED_HIT_RESULT, HIT_RESULT_NONE);
                hit_result_pending_clear = 0;
            }

            if (led_blink_active && playback_index >= led_off_at) {
                XGpioPs_WritePin(&gpio_ps, LED_MIO_PIN, 0);
                led_blink_active = 0;
            }

            if (check_song_end()) {
                u32 hs_addr;
                switch (active_song) {
                    case SONG_1: hs_addr = SHARED_HIGH_SCORE_1; break;
                    case SONG_2: hs_addr = SHARED_HIGH_SCORE_2; break;
                    case SONG_3: hs_addr = SHARED_HIGH_SCORE_3; break;
                    default:     hs_addr = SHARED_HIGH_SCORE_1; break;
                }
                u32 current_hs = Xil_In32(hs_addr);
                if (score > current_hs) {
                    Xil_Out32(hs_addr, score);
                    xil_printf("New high score: %lu\r\n", score);
                }

                Xil_Out32(SHARED_GAME_STATE, STATE_SONG_WIN);
                silence_codec();
                prev_state = STATE_SONG_WIN;
                xil_printf("Song ended. Score=%lu  Hit=%lu  Missed=%lu\r\n",
                           score, notes_hit_count, notes_missed_count);
                continue;
            }

            btn_prev = btn_now;
        }


        int mute_audio = update_segment_tracking();


        short *backing  = (short *)active_backing_addr;
        s16 back_sample = backing[playback_index];

        if (mute_audio) {
            back_sample = 0;
        }

        Xil_Out32(MIXER_BACKING_REG, (u32)(s32)back_sample);
        Xil_Out32(MIXER_VOCAL_REG,   (u32)(s32)back_sample);

        u32 hw_output = Xil_In32(MIXER_OUTPUT_REG);

        Xil_Out32(I2S_DATA_TX_L_REG, hw_output << 8);
        Xil_Out32(I2S_DATA_TX_R_REG, hw_output << 8);

        playback_index++;
        if (playback_index >= active_backing_len) {
            backing_track_done = 1;
            playback_index = 0;
        }

        Xil_Out32(SHARED_PLAYBACK_IDX, playback_index);
    }
}
