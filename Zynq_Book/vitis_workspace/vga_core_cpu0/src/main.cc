//  main.cc  CPU0 (VGA Core)
#include <stdio.h>
#include "xil_types.h"
#include "xparameters.h"
#include "xil_io.h"
#include "xil_exception.h"
#include "xscugic.h"
#include "xgpio.h"
#include "xil_printf.h"
#include "xil_cache.h"
#include "sleep.h"
#include "xtmrctr.h"
#include "shared_memory.h"
#include "beatmap.h"


#define INTC_DEVICE_ID          XPAR_PS7_SCUGIC_0_DEVICE_ID
#define TIMER_DEVICE_ID         XPAR_AXI_TIMER_0_DEVICE_ID
#define TIMER_IRPT_INTR         XPAR_FABRIC_AXI_TIMER_0_INTERRUPT_INTR

// Switches GPIO  for SW0 pause
#define SWITCHES_DEVICE_ID      XPAR_GPIO_SWITCHES_DEVICE_ID

#define RNG_BASE  0x43C20000

// Read buttons from mixer hardware register
#define MIXER_BUTTON_REG  0x43C10008

#define MIXER_VOLUME_REG  0x43C10010




#define RES_WIDTH   640
#define RES_HEIGHT  480
#define FRAMEBUFFER_SIZE  (RES_WIDTH * RES_HEIGHT * (int)sizeof(int))

volatile int *back_buffer = (int *)0x00900000;


#define BLACK       0x000000
#define WHITE       0xFFFFFF
#define RED         0x0000FF
#define GREEN       0x00FF00
#define BLUE        0xFF0000
#define YELLOW      0x00FFFF
#define CYAN        0xFFFF00
#define PINK        0xFF00FF
#define GREY        0x606060
#define DARK_GREY   0x404040
#define LIGHT_GREY  0xA0A0A0
#define ORANGE      0x0080FF
#define DARK_BG     0x1A1A2E
#define MENU_BG     0x16213E

static const int lane_colors[5] = {BLUE, RED, GREEN, CYAN, PINK};


static XGpio     SWInst;
static XScuGic   INTCInst;
static XTmrCtr   TimerInstancePtr;

static volatile int shift_up    = 0;
static volatile int shift_down  = 0;
static volatile int shift_left  = 0;
static volatile int shift_right = 0;
static volatile int toggle_mode = 0;

static volatile int tick_count = 0;
static u8 prev_btns = 0;
static int needs_redraw = 1;


typedef enum {
    SCREEN_TITLE,
    SCREEN_SONGSELECT,
    SCREEN_PLAY,
    SCREEN_PAUSE,
    SCREEN_ENDSCORE,
    SCREEN_HIGHSCORE,
    SCREEN_HIGHSCORE_SONG,
    SCREEN_SETTINGS
} ScreenState;

static ScreenState current_screen      = SCREEN_TITLE;
static ScreenState settings_return_to  = SCREEN_TITLE;

static int title_index    = 0;
static int song_index     = 0;
static int pause_index    = 0;
static int hs_menu_index  = 0;
static int settings_index = 0;
static int volume_level   = 5;

static int  note_deactivated[MAX_NOTES];
static u32  last_hit_seq      = 0;
static int  feedback_timer    = 0;
static int  feedback_type     = 0;
static int  selected_song     = 0;
static u32  prev_sw0          = 0;

static int  gameplay_init_done = 0;
static int  note_prev_y[MAX_NOTES];
static int  note_prev_visible[MAX_NOTES];
static u32  hud_prev_score = 0xFFFFFFFF;
static u32  hud_prev_combo = 0xFFFFFFFF;
static u32  hud_prev_mult  = 0xFFFFFFFF;
static int  prev_feedback_active = 0;

static u32  prng_effect_color = WHITE;


static u8   note_assigned_button[MAX_NOTES];
static int  note_button_set[MAX_NOTES];

static SongNote *active_display_beatmap = NULL;
static int       active_display_num_notes = 0;

static const u8 button_map[5] = { BTN_L, BTN_U, BTN_C, BTN_D, BTN_R };


static const u8 font_8x8[][8] = {
 {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, //  ' '
 {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00}, //  '!'
 {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00}, // '"'
 {0x24,0x7E,0x24,0x24,0x7E,0x24,0x00,0x00}, //  '#'
 {0x08,0x3E,0x28,0x3E,0x0A,0x3E,0x08,0x00}, //  '$'
 {0x62,0x64,0x08,0x10,0x26,0x46,0x00,0x00}, //  '%'
 {0x30,0x48,0x30,0x56,0x48,0x34,0x00,0x00}, //  '&'
 {0x18,0x18,0x08,0x00,0x00,0x00,0x00,0x00}, //  '\''
 {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, //  '('
 {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, // ')'
 {0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00}, // '*'
 {0x00,0x08,0x08,0x3E,0x08,0x08,0x00,0x00}, // '+'
 {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x10}, // ','
 {0x00,0x00,0x00,0x3E,0x00,0x00,0x00,0x00}, // '-'
 {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // '.'
 {0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00}, // '/'
 {0x3C,0x46,0x4A,0x52,0x62,0x3C,0x00,0x00}, // '0'
 {0x18,0x38,0x18,0x18,0x18,0x3C,0x00,0x00}, // '1'
 {0x3C,0x42,0x02,0x0C,0x30,0x7E,0x00,0x00}, // '2'
 {0x3C,0x42,0x0C,0x02,0x42,0x3C,0x00,0x00}, //  '3'
 {0x08,0x18,0x28,0x48,0x7E,0x08,0x00,0x00}, //  '4'
 {0x7E,0x40,0x7C,0x02,0x42,0x3C,0x00,0x00}, //  '5'
 {0x1C,0x20,0x40,0x7C,0x42,0x3C,0x00,0x00}, //  '6'
 {0x7E,0x02,0x04,0x08,0x10,0x10,0x00,0x00}, //  '7'
 {0x3C,0x42,0x3C,0x42,0x42,0x3C,0x00,0x00}, //  '8'
 {0x3C,0x42,0x3E,0x02,0x04,0x38,0x00,0x00}, //  '9'
 {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, //  ':'
 {0x00,0x18,0x18,0x00,0x18,0x18,0x10,0x00}, //  ';'
 {0x04,0x08,0x10,0x20,0x10,0x08,0x04,0x00}, // '<'
 {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, //  '='
 {0x20,0x10,0x08,0x04,0x08,0x10,0x20,0x00}, //  '>'
 {0x3C,0x42,0x04,0x08,0x00,0x08,0x00,0x00}, //  '?'
 {0x3C,0x42,0x5E,0x56,0x5E,0x40,0x3C,0x00}, //  '@'
 {0x18,0x24,0x42,0x7E,0x42,0x42,0x00,0x00}, //  'A'
 {0x7C,0x42,0x7C,0x42,0x42,0x7C,0x00,0x00}, //  'B'
 {0x3C,0x42,0x40,0x40,0x42,0x3C,0x00,0x00}, // 'C'
 {0x78,0x44,0x42,0x42,0x44,0x78,0x00,0x00}, // 'D'
 {0x7E,0x40,0x7C,0x40,0x40,0x7E,0x00,0x00}, // 'E'
 {0x7E,0x40,0x7C,0x40,0x40,0x40,0x00,0x00}, // 'F'
 {0x3C,0x42,0x40,0x4E,0x42,0x3C,0x00,0x00}, // 'G'
 {0x42,0x42,0x7E,0x42,0x42,0x42,0x00,0x00}, //  'H'
 {0x3C,0x18,0x18,0x18,0x18,0x3C,0x00,0x00}, //  'I'
 {0x1E,0x04,0x04,0x04,0x44,0x38,0x00,0x00}, //  'J'
 {0x44,0x48,0x50,0x70,0x48,0x44,0x00,0x00}, //  'K'
 {0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00}, //  'L'
 {0x42,0x66,0x5A,0x42,0x42,0x42,0x00,0x00}, //  'M'
 {0x42,0x62,0x52,0x4A,0x46,0x42,0x00,0x00}, //  'N'
 {0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00}, //  'O'
 {0x7C,0x42,0x42,0x7C,0x40,0x40,0x00,0x00}, //  'P'
 {0x3C,0x42,0x42,0x4A,0x44,0x3A,0x00,0x00}, //  'Q'
 {0x7C,0x42,0x42,0x7C,0x48,0x44,0x00,0x00}, //  'R'
 {0x3C,0x42,0x40,0x3C,0x02,0x7C,0x00,0x00}, //  'S'
 {0x7E,0x18,0x18,0x18,0x18,0x18,0x00,0x00}, //  'T'
 {0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00}, //  'U'
 {0x42,0x42,0x42,0x42,0x24,0x18,0x00,0x00}, //  'V'
 {0x42,0x42,0x42,0x5A,0x66,0x42,0x00,0x00}, //  'W'
 {0x42,0x24,0x18,0x18,0x24,0x42,0x00,0x00}, //  'X'
 {0x42,0x42,0x24,0x18,0x18,0x18,0x00,0x00}, //  'Y'
 {0x7E,0x04,0x08,0x10,0x20,0x7E,0x00,0x00}, // 'Z'
};



static void Timer_InterruptHandler(XTmrCtr *ref, u8 num);
static int  setup_interrupts();
static u32  get_random();
static void poll_buttons();

static void clear_screen(u32 color);
static void draw_rect(int x, int y, int w, int h, u32 color);
static void draw_char_s(int x, int y, char c, u32 color, int scale);
static void draw_text_s(int x, int y, const char *text, u32 color, int scale);
static void draw_number(int x, int y, u32 num, u32 color, int scale);
static void draw_fretboard();
static void draw_note_sprite(int x, int y, int lane, u32 color);
static int  lane_to_x(int lane);
static int  button_to_lane(u8 btn);
static void draw_volume_bar(int x, int y, int level);

static void screen_update();
static void screen_draw();


static u32 get_random() {
    return Xil_In32(RNG_BASE);
}


static void clear_screen(u32 color) {
    for (int i = 0; i < RES_WIDTH * RES_HEIGHT; i++)
        back_buffer[i] = color;
}

static void draw_rect(int x, int y, int w, int h, u32 color) {
    for (int row = y; row < y + h; row++) {
        if (row < 0 || row >= RES_HEIGHT) continue;
        for (int col = x; col < x + w; col++) {
            if (col < 0 || col >= RES_WIDTH) continue;
            back_buffer[row * RES_WIDTH + col] = color;
        }
    }
}

static void draw_char_s(int x, int y, char c, u32 color, int scale) {
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    int idx = (int)c - 32;
    if (idx < 0 || idx >= 59) return;

    for (int row = 0; row < 8; row++) {
        u8 bits = font_8x8[idx][row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << (7 - col))) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        if (px >= 0 && px < RES_WIDTH && py >= 0 && py < RES_HEIGHT)
                            back_buffer[py * RES_WIDTH + px] = color;
                    }
                }
            }
        }
    }
}

static void draw_text_s(int x, int y, const char *text, u32 color, int scale) {
    while (*text) {
        draw_char_s(x, y, *text, color, scale);
        x += 8 * scale;
        text++;
    }
}

static void draw_number(int x, int y, u32 num, u32 color, int scale) {
    char buf[12];
    int i = 0;
    if (num == 0) { buf[i++] = '0'; }
    else {
        char tmp[12];
        int j = 0;
        while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
        while (j > 0) buf[i++] = tmp[--j];
    }
    buf[i] = '\0';
    draw_text_s(x, y, buf, color, scale);
}

static void draw_volume_bar(int x, int y, int level) {
    const int block_w  = 20;
    const int block_h  = 20;
    const int block_gap = 3;

    for (int i = 0; i < 10; i++) {
        int bx = x + i * (block_w + block_gap);

        if (i < level) {
            draw_rect(bx, y, block_w, block_h, CYAN);
        } else {
            draw_rect(bx, y, block_w, block_h, DARK_GREY);
            draw_rect(bx, y,                block_w, 1, GREY);
            draw_rect(bx, y + block_h - 1,  block_w, 1, GREY);
            draw_rect(bx,              y, 1, block_h, GREY);
            draw_rect(bx + block_w - 1, y, 1, block_h, GREY);
        }
    }
}



#define SPRITE_SIZE 48

static void draw_sprite_outline(int x, int y, int w, int h, u32 border_color) {
    draw_rect(x,         y,         w, 1, border_color);  // top
    draw_rect(x,         y + h - 1, w, 1, border_color);  // bottom
    draw_rect(x,         y,         1, h, border_color);  // left
    draw_rect(x + w - 1, y,         1, h, border_color);  // right
}

static void draw_up_arrow(int x, int y, u32 color) {
    int center = x + SPRITE_SIZE / 2;
    for (int row = 0; row < SPRITE_SIZE / 2; row++) {
        int hw = row;
        for (int col = -hw; col <= hw; col++) {
            int dx = center + col, dy = y + row;
            if (dx >= 0 && dx < RES_WIDTH && dy >= 0 && dy < RES_HEIGHT)
                back_buffer[dy * RES_WIDTH + dx] = color;
        }
    }
    int sh = 6, sv = 20;
    for (int row = 0; row < sv; row++)
        for (int col = -sh; col <= sh; col++) {
            int dx = center + col, dy = y + 24 + row;
            if (dx >= 0 && dx < RES_WIDTH && dy >= 0 && dy < RES_HEIGHT)
                back_buffer[dy * RES_WIDTH + dx] = color;
        }
}

static void draw_down_arrow(int x, int y, u32 color) {
    int center = x + SPRITE_SIZE / 2;
    int sh = 6, sv = 20;
    for (int row = 0; row < sv; row++)
        for (int col = -sh; col <= sh; col++) {
            int dx = center + col, dy = y + row;
            if (dx >= 0 && dx < RES_WIDTH && dy >= 0 && dy < RES_HEIGHT)
                back_buffer[dy * RES_WIDTH + dx] = color;
        }
    for (int row = 0; row < SPRITE_SIZE / 2; row++) {
        int hw = (SPRITE_SIZE / 2 - 1) - row;
        for (int col = -hw; col <= hw; col++) {
            int dx = center + col, dy = y + sv + row;
            if (dx >= 0 && dx < RES_WIDTH && dy >= 0 && dy < RES_HEIGHT)
                back_buffer[dy * RES_WIDTH + dx] = color;
        }
    }
}

static void draw_left_arrow(int x, int y, u32 color) {
    int center = y + SPRITE_SIZE / 2;
    for (int col = 0; col < 24; col++) {
        int hh = col;
        for (int row = -hh; row <= hh; row++) {
            int dx = x + col, dy = center + row;
            if (dx >= 0 && dx < RES_WIDTH && dy >= 0 && dy < RES_HEIGHT)
                back_buffer[dy * RES_WIDTH + dx] = color;
        }
    }
    int sh = 6;
    for (int col = 0; col < 20; col++)
        for (int row = -sh; row <= sh; row++) {
            int dx = x + 24 + col, dy = center + row;
            if (dx >= 0 && dx < RES_WIDTH && dy >= 0 && dy < RES_HEIGHT)
                back_buffer[dy * RES_WIDTH + dx] = color;
        }
}

static void draw_right_arrow(int x, int y, u32 color) {
    int center = y + SPRITE_SIZE / 2;
    int sh = 6;
    for (int col = 0; col < 20; col++)
        for (int row = -sh; row <= sh; row++) {
            int dx = x + col, dy = center + row;
            if (dx >= 0 && dx < RES_WIDTH && dy >= 0 && dy < RES_HEIGHT)
                back_buffer[dy * RES_WIDTH + dx] = color;
        }
    for (int col = 0; col < 24; col++) {
        int hh = (24 - 1) - col;
        for (int row = -hh; row <= hh; row++) {
            int dx = x + 20 + col, dy = center + row;
            if (dx >= 0 && dx < RES_WIDTH && dy >= 0 && dy < RES_HEIGHT)
                back_buffer[dy * RES_WIDTH + dx] = color;
        }
    }
}

static void draw_circle_note(int x, int y, u32 color) {
    int r = 20;
    int cx = x + SPRITE_SIZE / 2, cy = y + SPRITE_SIZE / 2;
    for (int row = -r; row <= r; row++)
        for (int col = -r; col <= r; col++)
            if (col * col + row * row <= r * r) {
                int dx = cx + col, dy = cy + row;
                if (dx >= 0 && dx < RES_WIDTH && dy >= 0 && dy < RES_HEIGHT)
                    back_buffer[dy * RES_WIDTH + dx] = color;
            }
}

static void draw_note_sprite(int x, int y, int lane, u32 color) {
    switch (lane) {
        case 0: draw_left_arrow(x, y, color);   break;
        case 1: draw_up_arrow(x, y, color);     break;
        case 2: draw_circle_note(x, y, color);  break;
        case 3: draw_down_arrow(x, y, color);   break;
        case 4: draw_right_arrow(x, y, color);  break;
    }

    u32 outline = (color >> 1) & 0x7F7F7F;
    draw_sprite_outline(x, y, SPRITE_SIZE, SPRITE_SIZE, outline);
}


#define SIDE_PANELS  200
#define SIDE_PANEL   (SIDE_PANELS / 2)
#define FRET_WIDTH   ((RES_WIDTH - SIDE_PANELS) / 5)
#define HIT_LINE_Y   400

static int lane_to_x(int lane) {
    return SIDE_PANEL + lane * FRET_WIDTH + (FRET_WIDTH / 2 - SPRITE_SIZE / 2);
}

static int button_to_lane(u8 btn) {
    if (btn == BTN_L) return 0;
    if (btn == BTN_U) return 1;
    if (btn == BTN_C) return 2;
    if (btn == BTN_D) return 3;
    if (btn == BTN_R) return 4;
    return -1;
}

static void draw_fretboard() {
    for (int x = 0; x < SIDE_PANEL; x++)
        for (int y = 0; y < RES_HEIGHT; y++)
            back_buffer[y * RES_WIDTH + x] = DARK_BG;

    for (int x = SIDE_PANEL + FRET_WIDTH * 5; x < RES_WIDTH; x++)
        for (int y = 0; y < RES_HEIGHT; y++)
            back_buffer[y * RES_WIDTH + x] = DARK_BG;

    for (int lane = 0; lane < 5; lane++) {
        int x0 = SIDE_PANEL + lane * FRET_WIDTH;
        int x1 = x0 + FRET_WIDTH;
        for (int x = x0; x < x1; x++) {
            for (int y = 0; y < RES_HEIGHT; y++) {
                if (x == x0 || x == x1 - 1)
                    back_buffer[y * RES_WIDTH + x] = GREY;
                else if (y >= HIT_LINE_Y && y < HIT_LINE_Y + 10)
                    back_buffer[y * RES_WIDTH + x] = YELLOW;
                else
                    back_buffer[y * RES_WIDTH + x] = DARK_GREY;
            }
        }
    }
}


#define TITLE_OPTION_COUNT 3

static const char *title_options[TITLE_OPTION_COUNT] = {
    "PLAY",
    "HIGH SCORES",
    "SETTINGS"
};

static void Title_Update() {
    if (shift_up)   { title_index--; shift_up = 0;
                      if (title_index < 0) title_index = TITLE_OPTION_COUNT - 1; }
    if (shift_down) { title_index++; shift_down = 0;
                      if (title_index >= TITLE_OPTION_COUNT) title_index = 0; }
    if (toggle_mode) {
        toggle_mode = 0;
        shift_up = shift_down = shift_left = shift_right = 0;
        switch (title_index) {
            case 0:
                current_screen = SCREEN_SONGSELECT;
                song_index = 0;
                break;
            case 1:
                current_screen = SCREEN_HIGHSCORE;
                hs_menu_index = 0;
                break;
            case 2:
                settings_return_to = SCREEN_TITLE;
                current_screen = SCREEN_SETTINGS;
                break;
        }
        needs_redraw = 1;
    }
}

static void Title_Draw() {
    clear_screen(DARK_BG);
    draw_text_s(120, 60,  "FPGA GUITAR HERO", YELLOW, 3);
    draw_text_s(200, 130, "RHYTHM GAME", GREY, 2);

    for (int i = 0; i < TITLE_OPTION_COUNT; i++) {
        int y = 220 + i * 40;
        u32 col = (i == title_index) ? YELLOW : WHITE;
        if (i == title_index)
            draw_text_s(160, y, "> ", YELLOW, 2);
        draw_text_s(192, y, title_options[i], col, 2);
    }

    draw_text_s(120, 440, "UP/DOWN: NAVIGATE  CTR: SELECT", GREY, 1);
}


static void prepare_song_start() {
    for (int i = 0; i < MAX_NOTES; i++) {
        note_deactivated[i] = 0;
        note_button_set[i] = 0;
        note_assigned_button[i] = 0;
        note_prev_visible[i] = 0;
        // Clear PRNG button assignments in shared memory
        Xil_Out32(SHARED_NOTE_BUTTONS + i * 4, 0);
    }
    last_hit_seq = 0;
    feedback_timer = 0;
    gameplay_init_done = 0;
}


#define SONG_OPTION_COUNT 4

static const char *song_options[SONG_OPTION_COUNT] = {
    "HARDER, BETTER, FASTER, STRONGER BY DAFT PUNK (EASY)",
    "LEVELS BY AVICII (MEDIUM)",
    "BEAT IT BY MICHAEL JACKSON (HARD)",
    "BACK"
};

static void SongSelect_Update() {
    if (shift_up)   { song_index--; shift_up = 0;
                      if (song_index < 0) song_index = SONG_OPTION_COUNT - 1; }
    if (shift_down) { song_index++; shift_down = 0;
                      if (song_index >= SONG_OPTION_COUNT) song_index = 0; }
    if (toggle_mode) {
        toggle_mode = 0;
        shift_up = shift_down = shift_left = shift_right = 0;
        switch (song_index) {
            case 0:
                selected_song = SONG_1;
                active_display_beatmap = beatmap_song1;
                active_display_num_notes = NUM_NOTES_SONG1;
                Xil_Out32(SHARED_SONG_SELECT, SONG_1);
                prepare_song_start();
                Xil_Out32(SHARED_GAME_STATE, STATE_PLAYING);
                current_screen = SCREEN_PLAY;
                break;
            case 1:
                selected_song = SONG_2;
                active_display_beatmap = beatmap_song2;
                active_display_num_notes = NUM_NOTES_SONG2;
                Xil_Out32(SHARED_SONG_SELECT, SONG_2);
                prepare_song_start();
                Xil_Out32(SHARED_GAME_STATE, STATE_PLAYING);
                current_screen = SCREEN_PLAY;
                break;
            case 2:
                selected_song = SONG_3;
                active_display_beatmap = beatmap_song3;
                active_display_num_notes = NUM_NOTES_SONG3;
                Xil_Out32(SHARED_SONG_SELECT, SONG_3);
                prepare_song_start();
                Xil_Out32(SHARED_GAME_STATE, STATE_PLAYING);
                current_screen = SCREEN_PLAY;
                break;
            case 3:
                current_screen = SCREEN_TITLE;
                break;
        }
        needs_redraw = 1;
    }
}

static void SongSelect_Draw() {
    clear_screen(DARK_BG);
    draw_text_s(180, 40, "SELECT SONG", YELLOW, 3);

    const char *song_line1[SONG_OPTION_COUNT] = {
        "HARDER, BETTER, FASTER,",
        "LEVELS - AVICII (MEDIUM)",
        "BEAT IT - MICHAEL JACKSON (HARD)",
        "BACK"
    };
    const char *song_line2[SONG_OPTION_COUNT] = {
        "STRONGER - DAFT PUNK (EASY)",
        NULL, NULL, NULL
    };

    int y = 140;
    for (int i = 0; i < SONG_OPTION_COUNT; i++) {
        u32 col = (i == song_index) ? YELLOW : WHITE;
        if (i == song_index)
            draw_text_s(24, y, ">", YELLOW, 2);
        draw_text_s(56, y, song_line1[i], col, 1);
        if (song_line2[i]) {
            draw_text_s(56, y + 12, song_line2[i], col, 1);
            y += 50;
        } else {
            y += 38;
        }
    }

    draw_text_s(80, 430, "UP/DOWN: NAVIGATE  CTR: SELECT", GREY, 1);
}



#define HS_OPTION_COUNT 4
static const char *hs_options[HS_OPTION_COUNT] = {
    "SONG 1", "SONG 2", "SONG 3", "BACK"
};

static int hs_viewing_song = -1;

static void HighScore_Update() {
    if (hs_viewing_song >= 0) {
        if (toggle_mode || shift_up || shift_down) {
            toggle_mode = shift_up = shift_down = 0;
            hs_viewing_song = -1;
        }
        return;
    }

    if (shift_up)   { hs_menu_index--; shift_up = 0;
                      if (hs_menu_index < 0) hs_menu_index = HS_OPTION_COUNT - 1; }
    if (shift_down) { hs_menu_index++; shift_down = 0;
                      if (hs_menu_index >= HS_OPTION_COUNT) hs_menu_index = 0; }
    if (toggle_mode) {
        toggle_mode = 0;
        shift_up = shift_down = shift_left = shift_right = 0;
        if (hs_menu_index < 3) {
            hs_viewing_song = hs_menu_index;
        } else {
            current_screen = SCREEN_TITLE;
        }
        needs_redraw = 1;
    }
}

static void HighScore_Draw() {
    clear_screen(DARK_BG);

    if (hs_viewing_song >= 0) {
        draw_text_s(160, 80, "HIGH SCORE", YELLOW, 3);
        const char *names[] = {"SONG 1", "SONG 2", "SONG 3"};
        draw_text_s(220, 160, names[hs_viewing_song], WHITE, 2);

        u32 hs_addr;
        switch (hs_viewing_song) {
            case 0:  hs_addr = SHARED_HIGH_SCORE_1; break;
            case 1:  hs_addr = SHARED_HIGH_SCORE_2; break;
            default: hs_addr = SHARED_HIGH_SCORE_3; break;
        }
        u32 hs = Xil_In32(hs_addr);

        draw_text_s(180, 240, "BEST: ", CYAN, 2);
        draw_number(276, 240, hs, CYAN, 2);
        draw_text_s(160, 400, "PRESS ANY BUTTON TO GO BACK", GREY, 1);
    } else {
        draw_text_s(160, 60, "HIGH SCORES", YELLOW, 3);

        for (int i = 0; i < HS_OPTION_COUNT; i++) {
            int y = 180 + i * 40;
            u32 col = (i == hs_menu_index) ? YELLOW : WHITE;
            if (i == hs_menu_index)
                draw_text_s(120, y, "> ", YELLOW, 2);
            draw_text_s(152, y, hs_options[i], col, 2);
        }
        draw_text_s(120, 420, "UP/DOWN: NAVIGATE  CTR: SELECT", GREY, 1);
    }
}



#define PAUSE_OPTION_COUNT 3
static const char *pause_options[PAUSE_OPTION_COUNT] = {
    "CONTINUE",
    "SETTINGS",
    "EXIT TO MENU"
};

static void Pause_Update() {
    if (shift_up)   { pause_index--; shift_up = 0;
                      if (pause_index < 0) pause_index = PAUSE_OPTION_COUNT - 1; }
    if (shift_down) { pause_index++; shift_down = 0;
                      if (pause_index >= PAUSE_OPTION_COUNT) pause_index = 0; }
    if (toggle_mode) {
        toggle_mode = 0;
        shift_up = shift_down = shift_left = shift_right = 0;
        switch (pause_index) {
            case 0:
                Xil_Out32(SHARED_GAME_STATE, STATE_PLAYING);
                gameplay_init_done = 0;
                current_screen = SCREEN_PLAY;
                break;
            case 1:
                settings_return_to = SCREEN_PAUSE;
                current_screen = SCREEN_SETTINGS;
                break;
            case 2:
                Xil_Out32(SHARED_GAME_STATE, STATE_MENU);
                gameplay_init_done = 0;
                current_screen = SCREEN_TITLE;
                title_index = 0;
                break;
        }
        needs_redraw = 1;
    }
}

static void Pause_Draw() {
    draw_rect(110, 140, 420, 220, DARK_BG);
    draw_rect(112, 142, 416, 216, MENU_BG);
    draw_text_s(225, 158, "PAUSED", YELLOW, 3);

    for (int i = 0; i < PAUSE_OPTION_COUNT; i++) {
        int y = 235 + i * 35;
        u32 col = (i == pause_index) ? YELLOW : WHITE;
        if (i == pause_index)
            draw_text_s(170, y, "> ", YELLOW, 2);
        draw_text_s(202, y, pause_options[i], col, 2);
    }
}



static void EndScore_Update() {
    if (toggle_mode) {
        toggle_mode = 0;
        shift_up = shift_down = shift_left = shift_right = 0;
        Xil_Out32(SHARED_GAME_STATE, STATE_MENU);
        current_screen = SCREEN_TITLE;
        title_index = 0;
    }
}

static void EndScore_Draw() {
    clear_screen(DARK_BG);
    draw_text_s(140, 50, "SONG COMPLETE!", YELLOW, 3);

    u32 final_score = Xil_In32(SHARED_SCORE);
    u32 hits        = Xil_In32(SHARED_NOTES_HIT);
    u32 misses      = Xil_In32(SHARED_NOTES_MISSED);
    u32 total       = Xil_In32(SHARED_TOTAL_NOTES);

    draw_text_s(100, 150, "SCORE: ", WHITE, 2);
    draw_number(212, 150, final_score, CYAN, 2);

    draw_text_s(100, 200, "NOTES HIT: ", WHITE, 2);
    draw_number(276, 200, hits, GREEN, 2);
    draw_text_s(276 + 48, 200, "/", WHITE, 2);
    draw_number(276 + 64, 200, total, WHITE, 2);

    draw_text_s(100, 240, "MISSED: ", WHITE, 2);
    draw_number(228, 240, misses, RED, 2);

    u32 hs_addr;
    switch (selected_song) {
        case SONG_1: hs_addr = SHARED_HIGH_SCORE_1; break;
        case SONG_2: hs_addr = SHARED_HIGH_SCORE_2; break;
        default:     hs_addr = SHARED_HIGH_SCORE_3; break;
    }
    u32 hs = Xil_In32(hs_addr);
    if (final_score >= hs && final_score > 0) {
        draw_text_s(140, 310, "NEW HIGH SCORE!", YELLOW, 2);
    } else {
        draw_text_s(100, 310, "HIGH SCORE: ", GREY, 2);
        draw_number(292, 310, hs, GREY, 2);
    }

    draw_text_s(140, 420, "PRESS CENTER TO CONTINUE", GREY, 1);
}



static void Settings_Update() {
    int changed = 0;

    if (shift_left) {
        if (volume_level > 0) { volume_level--; changed = 1; }
        shift_left = 0;
    }
    if (shift_right) {
        if (volume_level < 10) { volume_level++; changed = 1; }
        shift_right = 0;
    }

    if (changed) {
        Xil_Out32(MIXER_VOLUME_REG, (u32)volume_level);
        needs_redraw = 1;
    }

    if (toggle_mode) {
        toggle_mode = 0;
        shift_up = shift_down = shift_left = shift_right = 0;
        current_screen = settings_return_to;
        needs_redraw = 1;
    }

    shift_up = shift_down = 0;
}

static void Settings_Draw() {
    clear_screen(DARK_BG);
    draw_text_s(190, 70, "SETTINGS", YELLOW, 3);

    const int bar_x = 205;
    const int bar_y = 220;

    draw_text_s(220, 160, "VOLUME", WHITE, 2);

    draw_rect(280, 188, 48, 16, DARK_BG);
    draw_number(280, 188, (u32)volume_level, CYAN, 2);
    draw_text_s(296, 188, "/10", GREY, 2);

    draw_volume_bar(bar_x, bar_y, volume_level);

    draw_text_s(100, 300, "LEFT/RIGHT: ADJUST VOLUME", GREY, 1);
    draw_text_s(150, 320, "CENTER: GO BACK", GREY, 1);

    if (settings_return_to == SCREEN_PAUSE) {
        draw_text_s(160, 430, "RETURNING TO: PAUSE", DARK_GREY, 1);
    } else {
        draw_text_s(155, 430, "RETURNING TO: MAIN MENU", DARK_GREY, 1);
    }
}



#define NOTE_TRAVEL_TIME_SAMPLES  (48000 * 2)

static void erase_note_area(int lane, int y) {
    int x = lane_to_x(lane);
    for (int row = y; row < y + SPRITE_SIZE; row++) {
        if (row < 0 || row >= RES_HEIGHT) continue;
        for (int col = x; col < x + SPRITE_SIZE; col++) {
            if (col < 0 || col >= RES_WIDTH) continue;
            u32 bg;
            if (row >= HIT_LINE_Y && row < HIT_LINE_Y + 10)
                bg = YELLOW;
            else
                bg = DARK_GREY;
            back_buffer[row * RES_WIDTH + col] = bg;
        }
    }
}

static void init_gameplay_screen() {
    draw_fretboard();

    for (int lane = 0; lane < 5; lane++) {
        int x = lane_to_x(lane);
        u32 dim = (lane_colors[lane] >> 2) & 0x3F3F3F;
        draw_note_sprite(x, HIT_LINE_Y - SPRITE_SIZE / 2, lane, dim);
    }

    draw_text_s(4, 10, "SCORE", WHITE, 1);
    draw_number(4, 22, 0, CYAN, 1);
    draw_text_s(4, 44, "COMBO", WHITE, 1);
    draw_number(4, 56, 0, GREEN, 1);
    draw_text_s(4, 78, "MULT", WHITE, 1);
    draw_number(4, 90, 1, YELLOW, 1);
    draw_text_s(12, 90, "X", YELLOW, 1);

    if (selected_song == SONG_1)
        draw_text_s(RES_WIDTH - 92, 10, "SONG 1", WHITE, 1);
    else if (selected_song == SONG_2)
        draw_text_s(RES_WIDTH - 92, 10, "SONG 2", WHITE, 1);
    else
        draw_text_s(RES_WIDTH - 92, 10, "SONG 3", WHITE, 1);

    for (int i = 0; i < MAX_NOTES; i++) {
        note_prev_visible[i] = 0;
    }
    hud_prev_score = 0;
    hud_prev_combo = 0;
    hud_prev_mult  = 1;
    prev_feedback_active = 0;
    gameplay_init_done = 1;

    Xil_DCacheFlushRange((INTPTR)back_buffer, FRAMEBUFFER_SIZE);
}


static int get_display_lane(int note_idx) {
    if (!note_button_set[note_idx]) return -1;
    return button_to_lane(note_assigned_button[note_idx]);
}


static void assign_note_button(int note_idx) {
    u32 rng = get_random();
    u8 btn = button_map[rng % 5];

    note_assigned_button[note_idx] = btn;
    note_button_set[note_idx] = 1;

    Xil_Out32(SHARED_NOTE_BUTTONS + note_idx * 4, (u32)btn);
}


static void Play_Update() {
    // Check SW0 for pause
    u32 sw_val = XGpio_DiscreteRead(&SWInst, 1) & 0x01;
    if (sw_val == 1 && prev_sw0 == 0) {
        Xil_Out32(SHARED_GAME_STATE, STATE_PAUSED);
        current_screen = SCREEN_PAUSE;
        pause_index = 0;
        shift_up = shift_down = shift_left = shift_right = toggle_mode = 0;
        needs_redraw = 1;
    }
    prev_sw0 = sw_val;

    // Check if CPU1 signaled song end
    u32 gs = Xil_In32(SHARED_GAME_STATE);
    if (gs == STATE_SONG_WIN) {
        current_screen = SCREEN_ENDSCORE;
        needs_redraw = 1;
        return;
    }

    // Process hit/miss events from CPU1
    u32 current_seq = Xil_In32(SHARED_HIT_SEQ);
    if (current_seq != last_hit_seq) {
        u32 hr  = Xil_In32(SHARED_HIT_RESULT);
        u32 idx = Xil_In32(SHARED_LAST_NOTE_IDX);
        if ((int)idx < active_display_num_notes) {
            note_deactivated[idx] = 1;
        }
        feedback_timer = 30;
        feedback_type = (hr == HIT_RESULT_HIT) ? 1 : 2;

        if (hr == HIT_RESULT_HIT) {
            u32 rng = get_random();
            int ci = rng % 5;
            prng_effect_color = lane_colors[ci];
        } else {
            prng_effect_color = RED;
        }

        last_hit_seq = current_seq;
    }

    if (feedback_timer > 0) feedback_timer--;

    shift_up = shift_down = shift_left = shift_right = toggle_mode = 0;
}


static void Play_Draw() {
    if (!gameplay_init_done) {
        init_gameplay_screen();
        return;
    }

    u32 playback_idx = Xil_In32(SHARED_PLAYBACK_IDX);

    for (int i = 0; i < active_display_num_notes; i++) {
        if (!note_prev_visible[i]) continue;
        int lane = get_display_lane(i);
        if (lane >= 0) erase_note_area(lane, note_prev_y[i]);
        note_prev_visible[i] = 0;
    }

    if (prev_feedback_active && feedback_timer == 0) {
        for (int row = 195; row < 230; row++)
            for (int col = 220; col < 390; col++)
                if (col >= SIDE_PANEL && col < SIDE_PANEL + FRET_WIDTH * 5)
                    back_buffer[row * RES_WIDTH + col] = DARK_GREY;
        prev_feedback_active = 0;
    }

    for (int lane = 0; lane < 5; lane++) {
        int x = lane_to_x(lane);
        u32 dim = (lane_colors[lane] >> 2) & 0x3F3F3F;
        draw_note_sprite(x, HIT_LINE_Y - SPRITE_SIZE / 2, lane, dim);
    }

    for (int i = 0; i < active_display_num_notes; i++) {
        if (note_deactivated[i]) continue;

        u32 note_sample = active_display_beatmap[i].sample;
        s32 samples_until_hit = (s32)note_sample - (s32)playback_idx;

        if (samples_until_hit > (s32)NOTE_TRAVEL_TIME_SAMPLES) continue;
        if (samples_until_hit < -48000) {
            note_deactivated[i] = 1;
            continue;
        }

        if (!note_button_set[i]) {
            assign_note_button(i);
        }

        float progress = 1.0f - ((float)samples_until_hit / (float)NOTE_TRAVEL_TIME_SAMPLES);
        if (progress < 0.0f) continue;

        int y = (int)(progress * (float)HIT_LINE_Y);

        int lane = get_display_lane(i);
        if (lane < 0) continue;
        int x = lane_to_x(lane);

        u32 note_color = lane_colors[lane];

        draw_note_sprite(x, y, lane, note_color);
        note_prev_y[i] = y;
        note_prev_visible[i] = 1;
    }

    u32 sc   = Xil_In32(SHARED_SCORE);
    u32 cb   = Xil_In32(SHARED_COMBO);
    u32 mult = Xil_In32(SHARED_MULTIPLIER);

    if (sc != hud_prev_score) {
        draw_rect(4, 22, 80, 10, DARK_BG);
        draw_number(4, 22, sc, CYAN, 1);
        hud_prev_score = sc;
    }
    if (cb != hud_prev_combo) {
        draw_rect(4, 56, 80, 10, DARK_BG);
        draw_number(4, 56, cb, GREEN, 1);
        hud_prev_combo = cb;
    }
    if (mult != hud_prev_mult) {
        draw_rect(4, 90, 40, 10, DARK_BG);
        draw_number(4, 90, mult, YELLOW, 1);
        draw_text_s(12, 90, "X", YELLOW, 1);
        hud_prev_mult = mult;
    }

    if (feedback_timer > 0) {
        if (feedback_type == 1)
            draw_text_s(250, 200, "HIT!", GREEN, 3);
        else if (feedback_type == 2)
            draw_text_s(230, 200, "MISS!", RED, 3);
        prev_feedback_active = 1;
    }
}



static void screen_update() {
    switch (current_screen) {
        case SCREEN_TITLE:          Title_Update();      break;
        case SCREEN_SONGSELECT:     SongSelect_Update(); break;
        case SCREEN_PLAY:           Play_Update();       break;
        case SCREEN_PAUSE:          Pause_Update();      break;
        case SCREEN_ENDSCORE:       EndScore_Update();   break;
        case SCREEN_HIGHSCORE:
        case SCREEN_HIGHSCORE_SONG: HighScore_Update();  break;
        case SCREEN_SETTINGS:       Settings_Update();   break;
    }
}

static void screen_draw() {
    switch (current_screen) {
        case SCREEN_TITLE:          Title_Draw();      break;
        case SCREEN_SONGSELECT:     SongSelect_Draw(); break;
        case SCREEN_PLAY:           Play_Draw();       break;
        case SCREEN_PAUSE:          Pause_Draw();      break;
        case SCREEN_ENDSCORE:       EndScore_Draw();   break;
        case SCREEN_HIGHSCORE:
        case SCREEN_HIGHSCORE_SONG: HighScore_Draw();  break;
        case SCREEN_SETTINGS:       Settings_Draw();   break;
    }
}



static void poll_buttons() {
    u8 btns = (u8)(Xil_In32(MIXER_BUTTON_REG) & 0x1F);
    u8 pressed = btns & ~prev_btns;

    if (pressed) {
        if (pressed & BTN_L)  shift_left  = 1;
        if (pressed & BTN_R)  shift_right = 1;
        if (pressed & BTN_U)  shift_up    = 1;
        if (pressed & BTN_D)  shift_down  = 1;
        if (pressed & BTN_C)  toggle_mode = 1;
        needs_redraw = 1;
    }

    prev_btns = btns;
}

static void Timer_InterruptHandler(XTmrCtr *CallBackRef, u8 TmrCtrNumber) {
    (void)CallBackRef;
    (void)TmrCtrNumber;
    tick_count++;
}


static int setup_interrupts() {
    XScuGic_Config *cfg;
    int status;

    cfg = XScuGic_LookupConfig(INTC_DEVICE_ID);
    status = XScuGic_CfgInitialize(&INTCInst, cfg, cfg->CpuBaseAddress);
    if (status != XST_SUCCESS) return XST_FAILURE;

    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                                 (Xil_ExceptionHandler)XScuGic_InterruptHandler,
                                 &INTCInst);
    Xil_ExceptionEnable();

    status = XScuGic_Connect(&INTCInst, TIMER_IRPT_INTR,
                             (Xil_ExceptionHandler)XTmrCtr_InterruptHandler,
                             (void *)&TimerInstancePtr);
    if (status != XST_SUCCESS) return XST_FAILURE;
    XScuGic_Enable(&INTCInst, TIMER_IRPT_INTR);

    return XST_SUCCESS;
}



int main() {
    int status;

    xil_printf("\r\n[CPU0 VGA] Starting...\r\n");

    // Initialize switch GPIO (for SW0 pause)
    status = XGpio_Initialize(&SWInst, SWITCHES_DEVICE_ID);
    if (status != XST_SUCCESS) {
        xil_printf("[CPU0] Switch GPIO init failed  SW0 pause disabled\r\n");
    }
    XGpio_SetDataDirection(&SWInst, 1, 0xFF);

    status = XTmrCtr_Initialize(&TimerInstancePtr, TIMER_DEVICE_ID);
    if (status != XST_SUCCESS) {
        xil_printf("[CPU0] Timer init failed\r\n");
        return XST_FAILURE;
    }
    XTmrCtr_SetHandler(&TimerInstancePtr,
                        (XTmrCtr_Handler)Timer_InterruptHandler,
                        &TimerInstancePtr);
    XTmrCtr_SetResetValue(&TimerInstancePtr, 0, 0xFFE03E2B);
    XTmrCtr_SetOptions(&TimerInstancePtr, 0,
                        XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION);

    status = setup_interrupts();
    if (status != XST_SUCCESS) {
        xil_printf("[CPU0] Interrupt setup failed\r\n");
        return XST_FAILURE;
    }

    Xil_Out32(SHARED_GAME_STATE,   STATE_MENU);
    Xil_Out32(SHARED_SONG_SELECT,  SONG_1);
    Xil_Out32(SHARED_HIGH_SCORE_1, 0);
    Xil_Out32(SHARED_HIGH_SCORE_2, 0);
    Xil_Out32(SHARED_HIGH_SCORE_3, 0);
    Xil_Out32(MIXER_VOLUME_REG, (u32)volume_level);

    for (int i = 0; i < MAX_NOTES; i++) {
        Xil_Out32(SHARED_NOTE_BUTTONS + i * 4, 0);
        note_button_set[i] = 0;
        note_assigned_button[i] = 0;
    }

    Xil_DCacheFlushRange(SHARED_BASE, SHARED_REGION_SIZE);

    XTmrCtr_Start(&TimerInstancePtr, 0);

    xil_printf("[CPU0] VGA core running.  Segment mode active.\r\n");

    while (1) {
        if (tick_count > 0) {
            tick_count--;

            Xil_DCacheInvalidateRange(SHARED_BASE, SHARED_REGION_SIZE);

            poll_buttons();
            screen_update();

            Xil_DCacheFlushRange(SHARED_BASE, SHARED_REGION_SIZE);

            if (needs_redraw || current_screen == SCREEN_PLAY) {
                screen_draw();
                needs_redraw = 0;
                Xil_DCacheFlushRange(SHARED_BASE, SHARED_REGION_SIZE);
                Xil_DCacheFlushRange((INTPTR)back_buffer, FRAMEBUFFER_SIZE);
            }
        }
    }

    return 0;
}
