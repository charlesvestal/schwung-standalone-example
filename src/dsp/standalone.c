// SPDX-License-Identifier: MIT
// Standalone example for Ableton Move hardware.
// Opens the SPI device directly and runs a main loop with:
//   - Pad-triggered polyphonic sine wave synthesis (8 voices)
//   - LED color feedback on pad press/release
//   - 128x64 1-bit display rendering with embedded bitmap font
//   - Clean exit on Back button (CC 51) or SIGTERM
//
// Compile: ${CROSS_PREFIX}gcc -o standalone_example standalone_example.c -Isrc -Isrc/lib -lm

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

// Linux ioctl encoding macros (not available on macOS / non-Linux hosts)
#ifndef _IOC_NONE
#define _IOC_NONE 0U
#endif
#ifndef _IOC
#define _IOC(dir,type,nr,size) \
    (((dir) << 30) | ((type) << 8) | ((nr) << 0) | ((size) << 16))
#endif

#include "schwung_spi_lib.h"

// ============================================================================
// Embedded 5x7 bitmap font
// ============================================================================
// Each character: 7 bytes (rows top to bottom), 5 bits per row MSB-aligned.
// Covers: space (32), 0-9 (48-57), : (58), . (46), A-Z (65-90), a-z (97-122)

static const uint8_t font_5x7[][7] = {
    // space (index 0)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ! (index 1) - placeholder, not used
    {0x20,0x20,0x20,0x20,0x00,0x20,0x00},
    // " (index 2)
    {0x50,0x50,0x00,0x00,0x00,0x00,0x00},
    // # (index 3)
    {0x50,0xF8,0x50,0xF8,0x50,0x00,0x00},
    // $ (index 4)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // % (index 5)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // & (index 6)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ' (index 7)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ( (index 8)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ) (index 9)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // * (index 10)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // + (index 11)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // , (index 12)
    {0x00,0x00,0x00,0x00,0x00,0x20,0x40},
    // - (index 13)
    {0x00,0x00,0x00,0x70,0x00,0x00,0x00},
    // . (index 14)
    {0x00,0x00,0x00,0x00,0x00,0x20,0x00},
    // / (index 15)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // 0 (index 16)
    {0x70,0x88,0x98,0xA8,0xC8,0x70,0x00},
    // 1
    {0x20,0x60,0x20,0x20,0x20,0x70,0x00},
    // 2
    {0x70,0x88,0x08,0x30,0x40,0xF8,0x00},
    // 3
    {0x70,0x88,0x30,0x08,0x88,0x70,0x00},
    // 4
    {0x10,0x30,0x50,0x90,0xF8,0x10,0x00},
    // 5
    {0xF8,0x80,0xF0,0x08,0x88,0x70,0x00},
    // 6
    {0x30,0x40,0xF0,0x88,0x88,0x70,0x00},
    // 7
    {0xF8,0x08,0x10,0x20,0x40,0x40,0x00},
    // 8
    {0x70,0x88,0x70,0x88,0x88,0x70,0x00},
    // 9
    {0x70,0x88,0x88,0x78,0x10,0x60,0x00},
    // : (index 26)
    {0x00,0x20,0x00,0x00,0x20,0x00,0x00},
    // ; (index 27)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // < (index 28)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // = (index 29)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // > (index 30)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ? (index 31)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // @ (index 32)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // A (index 33)
    {0x70,0x88,0x88,0xF8,0x88,0x88,0x00},
    // B
    {0xF0,0x88,0xF0,0x88,0x88,0xF0,0x00},
    // C
    {0x70,0x88,0x80,0x80,0x88,0x70,0x00},
    // D
    {0xF0,0x88,0x88,0x88,0x88,0xF0,0x00},
    // E
    {0xF8,0x80,0xF0,0x80,0x80,0xF8,0x00},
    // F
    {0xF8,0x80,0xF0,0x80,0x80,0x80,0x00},
    // G
    {0x70,0x88,0x80,0xB8,0x88,0x70,0x00},
    // H
    {0x88,0x88,0xF8,0x88,0x88,0x88,0x00},
    // I
    {0x70,0x20,0x20,0x20,0x20,0x70,0x00},
    // J
    {0x38,0x10,0x10,0x10,0x90,0x60,0x00},
    // K
    {0x88,0x90,0xA0,0xC0,0xA0,0x90,0x00},
    // L
    {0x80,0x80,0x80,0x80,0x80,0xF8,0x00},
    // M
    {0x88,0xD8,0xA8,0x88,0x88,0x88,0x00},
    // N
    {0x88,0xC8,0xA8,0x98,0x88,0x88,0x00},
    // O
    {0x70,0x88,0x88,0x88,0x88,0x70,0x00},
    // P
    {0xF0,0x88,0x88,0xF0,0x80,0x80,0x00},
    // Q
    {0x70,0x88,0x88,0xA8,0x90,0x68,0x00},
    // R
    {0xF0,0x88,0x88,0xF0,0xA0,0x90,0x00},
    // S
    {0x70,0x80,0x70,0x08,0x88,0x70,0x00},
    // T
    {0xF8,0x20,0x20,0x20,0x20,0x20,0x00},
    // U
    {0x88,0x88,0x88,0x88,0x88,0x70,0x00},
    // V
    {0x88,0x88,0x88,0x50,0x50,0x20,0x00},
    // W
    {0x88,0x88,0x88,0xA8,0xD8,0x88,0x00},
    // X
    {0x88,0x50,0x20,0x20,0x50,0x88,0x00},
    // Y
    {0x88,0x88,0x50,0x20,0x20,0x20,0x00},
    // Z (index 58)
    {0xF8,0x08,0x10,0x20,0x40,0xF8,0x00},
    // [ (index 59) - placeholder
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // \ (index 60)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ] (index 61)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ^ (index 62)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // _ (index 63)
    {0x00,0x00,0x00,0x00,0x00,0xF8,0x00},
    // ` (index 64)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // a (index 65)
    {0x00,0x00,0x70,0x08,0x78,0x78,0x00},
    // b
    {0x80,0x80,0xF0,0x88,0x88,0xF0,0x00},
    // c
    {0x00,0x00,0x70,0x80,0x80,0x70,0x00},
    // d
    {0x08,0x08,0x78,0x88,0x88,0x78,0x00},
    // e
    {0x00,0x00,0x70,0xF8,0x80,0x70,0x00},
    // f
    {0x30,0x40,0xE0,0x40,0x40,0x40,0x00},
    // g
    {0x00,0x00,0x78,0x88,0x78,0x08,0x70},
    // h
    {0x80,0x80,0xF0,0x88,0x88,0x88,0x00},
    // i
    {0x20,0x00,0x60,0x20,0x20,0x70,0x00},
    // j
    {0x10,0x00,0x30,0x10,0x10,0x90,0x60},
    // k
    {0x80,0x80,0x90,0xA0,0xC0,0xA0,0x00},
    // l
    {0x60,0x20,0x20,0x20,0x20,0x70,0x00},
    // m
    {0x00,0x00,0xD0,0xA8,0xA8,0x88,0x00},
    // n
    {0x00,0x00,0xF0,0x88,0x88,0x88,0x00},
    // o
    {0x00,0x00,0x70,0x88,0x88,0x70,0x00},
    // p
    {0x00,0x00,0xF0,0x88,0xF0,0x80,0x80},
    // q
    {0x00,0x00,0x78,0x88,0x78,0x08,0x08},
    // r
    {0x00,0x00,0xB0,0xC8,0x80,0x80,0x00},
    // s
    {0x00,0x00,0x78,0xC0,0x38,0xF0,0x00},
    // t
    {0x40,0x40,0xE0,0x40,0x40,0x30,0x00},
    // u
    {0x00,0x00,0x88,0x88,0x88,0x78,0x00},
    // v
    {0x00,0x00,0x88,0x88,0x50,0x20,0x00},
    // w
    {0x00,0x00,0x88,0xA8,0xD8,0x88,0x00},
    // x
    {0x00,0x00,0x88,0x50,0x20,0x88,0x00},
    // y
    {0x00,0x00,0x88,0x88,0x78,0x08,0x70},
    // z (index 90)
    {0x00,0x00,0xF8,0x10,0x20,0xF8,0x00},
};

// Map ASCII to font index. Returns -1 for unsupported characters.
static int font_index(char c) {
    if (c == ' ')  return 0;
    if (c >= '0' && c <= '9') return c - '0' + 16;
    if (c == ':')  return 26;
    if (c == '.')  return 14;
    if (c == '-')  return 13;
    if (c == ',')  return 12;
    if (c == '!')  return 1;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 33;
    if (c >= 'a' && c <= 'z') return c - 'a' + 65;
    return -1; // unsupported
}

// ============================================================================
// Voice / synth state
// ============================================================================

#define MAX_VOICES 8
#define ATTACK_SAMPLES 220   // ~5ms at 44100
#define RELEASE_SAMPLES 220

typedef struct {
    int active;          // 1 if note is currently held
    int releasing;       // 1 if in release phase
    uint8_t note;
    double phase;
    double phase_inc;
    int env_pos;         // envelope position (samples into attack or release)
} Voice;

static Voice voices[MAX_VOICES];

// ============================================================================
// Framebuffer (128x64, 8bpp for easy drawing, packed to 1024 bytes for SPI)
// ============================================================================

#define FB_WIDTH  128
#define FB_HEIGHT 64
#define FB_SIZE_8BPP (FB_WIDTH * FB_HEIGHT)  // 8192 bytes, 1 byte per pixel
#define FB_PACKED_SIZE 1024                   // 128 columns * 8 bands, 8 vertical pixels per byte

static uint8_t framebuffer[FB_SIZE_8BPP];     // 8bpp drawing buffer
static uint8_t packed_buffer[FB_PACKED_SIZE]; // packed for SPI display

// ============================================================================
// Global state
// ============================================================================

static volatile sig_atomic_t running = 1;
static int display_phase = 0; // cycles 0-6: phase 0 = new frame signal, phases 1-6 = slices
static int led_pkt_count = 0; // track MIDI OUT packets used this tick

// ============================================================================
// Signal handler
// ============================================================================

static void sigterm_handler(int sig) {
    (void)sig;
    running = 0;
}

// ============================================================================
// Framebuffer helpers
// ============================================================================

static void fb_clear(void) {
    memset(framebuffer, 0, FB_SIZE_8BPP);
}

static void fb_set_pixel(int x, int y) {
    if (x < 0 || x >= FB_WIDTH || y < 0 || y >= FB_HEIGHT) return;
    framebuffer[y * FB_WIDTH + x] = 1;
}

// Pack 8bpp framebuffer into 1024-byte packed format for SPI display.
// Format: 8 vertical bands of 128 columns. Each byte = 8 vertical pixels,
// bit 0 = top pixel, bit 7 = bottom pixel of the 8-pixel group.
static void fb_pack(void) {
    int i = 0;
    for (int band = 0; band < FB_HEIGHT / 8; band++) {
        for (int x = 0; x < FB_WIDTH; x++) {
            int base_y = band * 8;
            uint8_t packed = 0;
            for (int j = 0; j < 8; j++) {
                if (framebuffer[(base_y + j) * FB_WIDTH + x])
                    packed |= (1 << j);
            }
            packed_buffer[i++] = packed;
        }
    }
}

// Draw a single character at (x, y) pixel position.
static void fb_draw_char(int x, int y, char c) {
    int idx = font_index(c);
    if (idx < 0) return;
    const uint8_t *glyph = font_5x7[idx];
    for (int row = 0; row < 7; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (0x80 >> col)) {
                fb_set_pixel(x + col, y + row);
            }
        }
    }
}

// Draw a string. Returns width in pixels drawn.
static int fb_draw_string(int x, int y, const char *str) {
    int ox = x;
    while (*str) {
        fb_draw_char(x, y, *str);
        x += 6; // 5px char + 1px gap
        str++;
    }
    return x - ox;
}

// Draw a string centered horizontally on the display.
static void fb_draw_string_centered(int y, const char *str) {
    int len = (int)strlen(str);
    int w = len * 6 - 1; // total width minus trailing gap
    int x = (FB_WIDTH - w) / 2;
    if (x < 0) x = 0;
    fb_draw_string(x, y, str);
}

// ============================================================================
// Display push (one slice per tick)
// ============================================================================

static void display_push_slice(uint8_t *spi) {
    if (display_phase == 0) {
        // Phase 0: signal new frame — zero out slice area
        spi[SCHWUNG_OFF_OUT_DISP_STAT] = 0;
        memset(spi + SCHWUNG_OFF_OUT_DISP_DATA, 0, 172);
        // Pack framebuffer for upcoming slices
        fb_pack();
    } else {
        // Phases 1-6: write slice data
        int slice = display_phase - 1;
        int slice_start = slice * 172;
        int slice_bytes = (slice == 5) ? 164 : 172;

        spi[SCHWUNG_OFF_OUT_DISP_STAT] = (uint8_t)(slice + 1);
        memcpy(spi + SCHWUNG_OFF_OUT_DISP_DATA,
               packed_buffer + slice_start,
               slice_bytes);
    }

    display_phase = (display_phase + 1) % 7;
}

// ============================================================================
// Voice management
// ============================================================================

static void voice_note_on(uint8_t note, uint8_t velocity) {
    (void)velocity;
    double freq = 440.0 * pow(2.0, (note - 69) / 12.0);
    double inc = freq / (double)SCHWUNG_SAMPLE_RATE;

    // Check if note is already playing
    for (int i = 0; i < MAX_VOICES; i++) {
        if ((voices[i].active || voices[i].releasing) && voices[i].note == note) {
            voices[i].active = 1;
            voices[i].releasing = 0;
            voices[i].phase_inc = inc;
            voices[i].env_pos = 0;
            return;
        }
    }

    // Find free voice
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active && !voices[i].releasing) {
            voices[i].active = 1;
            voices[i].releasing = 0;
            voices[i].note = note;
            voices[i].phase = 0.0;
            voices[i].phase_inc = inc;
            voices[i].env_pos = 0;
            return;
        }
    }

    // Voice steal: take the oldest (first) active voice
    voices[0].active = 1;
    voices[0].releasing = 0;
    voices[0].note = note;
    voices[0].phase = 0.0;
    voices[0].phase_inc = inc;
    voices[0].env_pos = 0;
}

static void voice_note_off(uint8_t note) {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active && voices[i].note == note) {
            voices[i].active = 0;
            voices[i].releasing = 1;
            voices[i].env_pos = 0;
            return;
        }
    }
}

// ============================================================================
// Audio rendering
// ============================================================================

static void render_audio(uint8_t *spi) {
    int16_t *out = (int16_t *)(spi + SCHWUNG_OFF_OUT_AUDIO);
    int frames = SCHWUNG_AUDIO_FRAMES;

    for (int f = 0; f < frames; f++) {
        double mix = 0.0;

        for (int v = 0; v < MAX_VOICES; v++) {
            if (!voices[v].active && !voices[v].releasing) continue;

            // Generate sine
            double sample = sin(voices[v].phase * 2.0 * M_PI) * 8000.0;

            // Apply envelope
            double env = 1.0;
            if (voices[v].active) {
                // Attack
                if (voices[v].env_pos < ATTACK_SAMPLES) {
                    env = (double)voices[v].env_pos / (double)ATTACK_SAMPLES;
                    voices[v].env_pos++;
                }
            } else if (voices[v].releasing) {
                // Release
                if (voices[v].env_pos < RELEASE_SAMPLES) {
                    env = 1.0 - (double)voices[v].env_pos / (double)RELEASE_SAMPLES;
                    voices[v].env_pos++;
                } else {
                    voices[v].releasing = 0;
                    continue;
                }
            }

            mix += sample * env;

            // Advance phase
            voices[v].phase += voices[v].phase_inc;
            if (voices[v].phase >= 1.0) voices[v].phase -= 1.0;
        }

        // Clip to int16 range
        if (mix > 32767.0) mix = 32767.0;
        if (mix < -32768.0) mix = -32768.0;
        int16_t s = (int16_t)mix;

        // Interleaved stereo: [L, R, L, R, ...]
        out[f * 2]     = s;
        out[f * 2 + 1] = s;
    }
}

// ============================================================================
// LED colors for pads
// ============================================================================

static const uint8_t led_colors[] = {1, 3, 7, 17, 45, 120};
#define NUM_LED_COLORS (sizeof(led_colors) / sizeof(led_colors[0]))

static void write_led_packet(uint8_t *spi, uint8_t note, uint8_t color) {
    if (led_pkt_count >= SCHWUNG_MIDI_OUT_MAX) return;
    int offset = SCHWUNG_OFF_OUT_MIDI + led_pkt_count * 4;
    spi[offset + 0] = 0x09; // CIN=note_on, cable=0
    spi[offset + 1] = 0x90; // Note On, channel 0
    spi[offset + 2] = note;
    spi[offset + 3] = color;
    led_pkt_count++;
}

// ============================================================================
// MIDI processing
// ============================================================================

static void process_midi_in(uint8_t *spi) {
    uint8_t *midi_in = spi + SCHWUNG_OFF_IN_MIDI;

    for (int i = 0; i < SCHWUNG_MIDI_IN_MAX; i++) {
        uint8_t *evt = midi_in + i * 8;

        // Check if event is empty (all MIDI bytes zero)
        if (evt[0] == 0 && evt[1] == 0 && evt[2] == 0 && evt[3] == 0)
            continue;

        uint8_t status = evt[1];
        uint8_t data1  = evt[2];
        uint8_t data2  = evt[3];
        uint8_t type   = status & 0xF0;

        // Back button: CC 51, value 127
        if (type == 0xB0 && data1 == 51 && data2 == 127) {
            running = 0;
            return;
        }

        // Note On (velocity > 0)
        if (type == 0x90 && data2 > 0 && data1 >= 68 && data1 <= 99) {
            voice_note_on(data1, data2);
            uint8_t color = led_colors[(data1 - 68) % NUM_LED_COLORS];
            write_led_packet(spi, data1, color);
        }

        // Note Off or Note On with velocity 0
        if ((type == 0x80 || (type == 0x90 && data2 == 0)) &&
            data1 >= 68 && data1 <= 99) {
            voice_note_off(data1);
            write_led_packet(spi, data1, 0); // LED off
        }
    }
}

// ============================================================================
// Display update
// ============================================================================

static void update_display(void) {
    fb_clear();

    // Line 1: Title (centered)
    fb_draw_string_centered(2, "STANDALONE EXAMPLE");

    // Line 2: Instructions
    fb_draw_string_centered(14, "Press pads to play");

    // Line 3: Active notes
    char notes_str[80];
    int pos = 0;
    pos += snprintf(notes_str + pos, sizeof(notes_str) - pos, "Notes:");

    int any_active = 0;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (voices[v].active) {
            pos += snprintf(notes_str + pos, sizeof(notes_str) - pos,
                            " %d", voices[v].note);
            any_active = 1;
        }
    }
    if (!any_active) {
        pos += snprintf(notes_str + pos, sizeof(notes_str) - pos, " ---");
    }

    fb_draw_string_centered(28, notes_str);

    // Line 4: Exit hint
    fb_draw_string_centered(42, "Back: exit");
}

// ============================================================================
// Clear all pad LEDs (notes 68-99)
// ============================================================================

static void clear_all_leds(uint8_t *spi, int fd) {
    // Need to clear 32 pad LEDs. Max 20 packets per SPI transfer.
    // Send in two batches.
    for (int batch = 0; batch < 2; batch++) {
        // Clear MIDI OUT region
        memset(spi + SCHWUNG_OFF_OUT_MIDI, 0, SCHWUNG_MIDI_OUT_MAX * 4);
        int start_note = 68 + batch * 16;
        int end_note = start_note + 16;
        if (end_note > 100) end_note = 100;

        int pkt = 0;
        for (int note = start_note; note < end_note; note++) {
            int offset = SCHWUNG_OFF_OUT_MIDI + pkt * 4;
            spi[offset + 0] = 0x09;
            spi[offset + 1] = 0x90;
            spi[offset + 2] = (uint8_t)note;
            spi[offset + 3] = 0; // off
            pkt++;
        }

        // Trigger SPI transfer
        unsigned long cmd = _IOC(_IOC_NONE, 0, SCHWUNG_IOCTL_WAIT_SEND_SIZE, 0);
        ioctl(fd, cmd, SCHWUNG_FRAME_SIZE);
    }
}

// ============================================================================
// main
// ============================================================================

int main(void) {
    // Install signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Initialize voices
    memset(voices, 0, sizeof(voices));

    // Open SPI device
    int fd = open(SCHWUNG_SPI_DEVICE, O_RDWR);
    if (fd < 0) {
        perror("Failed to open SPI device");
        return 1;
    }

    // mmap the SPI buffer
    uint8_t *spi = mmap(NULL, SCHWUNG_PAGE_SIZE,
                         PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (spi == MAP_FAILED) {
        perror("Failed to mmap SPI buffer");
        close(fd);
        return 1;
    }

    // Set SPI speed to 20MHz
    unsigned long speed_cmd = _IOC(_IOC_NONE, 0, SCHWUNG_IOCTL_SET_SPEED, 0);
    if (ioctl(fd, speed_cmd, SCHWUNG_SPI_FREQ) < 0) {
        perror("Failed to set SPI speed");
        munmap(spi, SCHWUNG_PAGE_SIZE);
        close(fd);
        return 1;
    }

    printf("Standalone example started. Press Back to exit.\n");

    // Build ioctl command for SPI transfer
    unsigned long xfer_cmd = _IOC(_IOC_NONE, 0, SCHWUNG_IOCTL_WAIT_SEND_SIZE, 0);

    // Clear display: push a full 7-phase blank frame
    fb_clear();
    for (int p = 0; p < 7; p++) {
        memset(spi, 0, SCHWUNG_OFF_IN_BASE);
        display_push_slice(spi);
        ioctl(fd, xfer_cmd, SCHWUNG_FRAME_SIZE);
    }

    // Clear all LEDs at startup
    clear_all_leds(spi, fd);

    // Main loop
    while (running) {
        // Clear entire output region for this tick
        memset(spi, 0, SCHWUNG_OFF_IN_BASE);
        led_pkt_count = 0;

        // Process incoming MIDI
        process_midi_in(spi);

        // Render audio
        render_audio(spi);

        // Update display framebuffer and push one slice
        update_display();
        display_push_slice(spi);

        // Trigger SPI transfer (blocking wait + send, 768 bytes)
        int ret = ioctl(fd, xfer_cmd, SCHWUNG_FRAME_SIZE);

        if (ret < 0 && running) {
            // EINTR from signal is expected during shutdown
            if (running) {
                perror("SPI ioctl failed");
                break;
            }
        }
    }

    printf("Shutting down...\n");

    // Clear LEDs before exit
    clear_all_leds(spi, fd);

    // Clear audio
    memset(spi + SCHWUNG_OFF_OUT_AUDIO, 0,
           SCHWUNG_AUDIO_FRAMES * 2 * sizeof(int16_t));

    // Final transfer to flush
    ioctl(fd, xfer_cmd, SCHWUNG_FRAME_SIZE);

    munmap(spi, SCHWUNG_PAGE_SIZE);
    close(fd);

    printf("Goodbye.\n");
    return 0;
}
