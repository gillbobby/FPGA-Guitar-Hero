#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include "xil_io.h"

#define SHARED_BASE          0x1F000000

#define SHARED_PLAYBACK_IDX  (SHARED_BASE + 0x00)  // current sample index
#define SHARED_SCORE         (SHARED_BASE + 0x04)  // total score
#define SHARED_COMBO         (SHARED_BASE + 0x08)  // combo streak count
#define SHARED_MULTIPLIER    (SHARED_BASE + 0x0C)  // 1, 2, 3, or 4
#define SHARED_HIT_RESULT    (SHARED_BASE + 0x10)  // 0=none 1=HIT 2=MISS
#define SHARED_TOTAL_NOTES   (SHARED_BASE + 0x28)  // total notes in song
#define SHARED_NOTES_HIT     (SHARED_BASE + 0x2C)  // count of notes hit
#define SHARED_NOTES_MISSED  (SHARED_BASE + 0x30)  // count of notes missed
#define SHARED_LAST_NOTE_IDX (SHARED_BASE + 0x34)  // index of last hit/missed note
#define SHARED_HIT_SEQ       (SHARED_BASE + 0x38)  // increments on each hit/miss event


#define SHARED_GAME_STATE    (SHARED_BASE + 0x14)
#define SHARED_SONG_SELECT   (SHARED_BASE + 0x18)  // 0 = Song 1, 1 = song 2, 2 = song3
#define SHARED_VOLUME        (SHARED_BASE + 0x3C)  // master volume 0-10, CPU0 writes, CPU1 reads

#define SHARED_HIGH_SCORE_1  (SHARED_BASE + 0x1C)
#define SHARED_HIGH_SCORE_2  (SHARED_BASE + 0x20)
#define SHARED_HIGH_SCORE_3  (SHARED_BASE + 0x24)

#define SHARED_NOTE_BUTTONS  (SHARED_BASE + 0x100)


#define SHARED_REGION_SIZE   0x500
#define MAX_NOTES  200

#define STATE_MENU      0
#define STATE_PLAYING   1
#define STATE_PAUSED    2
#define STATE_GAME_OVER 3
#define STATE_SONG_WIN  4


#define HIT_RESULT_NONE  0
#define HIT_RESULT_HIT   1
#define HIT_RESULT_MISS  2

#define SONG_1  0
#define SONG_2  1
#define SONG_3  2


#define BTN_C  0x01
#define BTN_D  0x02
#define BTN_L  0x04
#define BTN_R  0x08
#define BTN_U  0x10


#define SCORE_PER_HIT        100
#define MULT_2X_THRESHOLD    10
#define MULT_3X_THRESHOLD    20
#define MULT_4X_THRESHOLD    30
#define HIT_WINDOW_HALF  	 7200

#endif
