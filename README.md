# FPGA Guitar Hero

A Guitar Hero–style rhythm game built on a Xilinx ZedBoard (Zynq-7000) for my ENSC 452 final project at SFU. You press buttons in time with the music and get points the better your timing is.

**Songs:**
- Harder Better Faster Stronger — Daft Punk
- Levels — Avicii
- Beat It — Michael Jackson

---

## What it does

A note highway scrolls on the VGA display. Notes are assigned to one of five pushbuttons randomly (via a hardware PRNG IP). When a note reaches the hit zone, you press the correct button. If you hit it in time, the vocal sample for that part of the song plays. If you miss, it stays silent and your combo resets.

The game has a main menu, song select, pause menu, volume control, and per-song high scores.

---

## Hardware

- **Board:** Xilinx ZedBoard (Zynq-7000)
- **Processors:** Dual ARM Cortex-A9 @ 200 MHz
- **Audio:** ADAU1761 codec, I2S interface
- **Display:** VGA 640×480
- **Input:** 5 GPIO pushbuttons + Switch 0 for pause
- **Tools:** Vivado 2020.2, Vitis 2020.2

---

## How it's structured

The Zynq has two ARM cores running separate programs:

**CPU1 (audio)** handles all audio playback and game logic: hit detection, scoring, combo/multiplier tracking, and writing game state to shared memory.

**CPU0 (VGA)** handles the display and menus. It reads the PRNG hardware IP to assign a random button to each note when it first appears on screen, then writes that assignment to shared memory so CPU1 can check whether you pressed the right button.

They communicate through a fixed region of DDR at `0x1F000000`. CPU1 has its D-cache fully disabled so it always reads directly from DDR. CPU0 has D-cache on and has to flush it after every shared memory write, otherwise CPU1 sees stale data.

---

## The custom AXI IP (audio_mixer_v1_0)

This is the custom VHDL IP core I designed for the course. It sits in the PL at `0x43C10000` and does a few things:

- Takes a backing track sample written by CPU1 and applies volume scaling
- Passes the current button state (from GPIO) back to CPU1 as a readable register
- Does the volume multiply in hardware using a multiply-shift trick to avoid a hardware divider: `result = (sample × 6554) >> 16`, which approximates dividing by 10 (volume scale for user is 0 to 10)

The reason volume scaling has to happen in hardware instead of software: the audio loop runs at 48 kHz (one sample every ~4166 clock cycles at 200 MHz), and I pace it with the PMCCNTR cycle counter. For the timing to be stable, the loop needs to do exactly 5 AXI transactions every iteration, with no branching. If I added a multiply/divide in software I'd need extra conditional reads or writes and the transaction count would vary, which causes the audio to play at the wrong speed. Putting it in the IP keeps the 5-transaction count constant.

The 5 transactions are:
1. Write backing sample -> Reg0
2. Write vocal sample -> Reg1
3. Read output <- Reg3
4. Write left channel -> I2S TX
5. Write right channel -> I2S TX

---

## How the game mechanic works (segment mode)

Each song is one full unedited audio file. The beatmap defines time windows (segments) where a vocal phrase happens. When playback enters a segment:

- CPU1 zeroes the sample before the AXI write (mutes it in software, not hardware — this keeps the 5-transaction count intact)
- CPU0 shows the note on screen
- If you press the right button within the window, CPU1 stops zeroing the sample and audio resumes
- If you miss, silence for the rest of the segment, combo resets

Song 1 (Harder Better Faster Stronger) has 57 annotated segments, Song 2 (Levels) has 91 segments, and Song 3 (Beat It) has 145 segments and a sick guitar solo. All 3 songs are fully functional.

### Beatmap annotation

I edited each of the songs and annotated beatmaps manually in Audacity.

---

## Scoring

- 100 points per hit
- Combo multiplier: 1× -> 2× at 10 hits -> 3× at 20 -> 4× at 30
- Miss resets combo and multiplier
- High scores saved per song in DDR, updated at end of song

---

## Audio files

Pre-processed in Audacity and loaded into DDR at boot:

- Format: signed 16-bit PCM, raw (no header), 48 kHz, mono
- Song 1: `0x11000000`
- Song 2: `0x14000000`
- Song 3: `0x17000000`

One thing I learned the hard way: if you export stereo and read it as mono, every sample is half a stereo pair so the song plays at double speed. Has to be mono.

---

## Running it

From the XSCT console in Vitis:

```
source C:/Zynq_Book/vitis_workspace/final.tcl
```

The script programs the FPGA, loads the audio files into DDR, loads both ELF files, starts CPU0, waits 3 seconds, then starts CPU1.

---

## Project files

`vivado/` contains the Vivado hardware project and the bitstream that gets loaded onto the FPGA.

`vitis_workspace/` contains the two CPU applications. `audio_core_cpu1/src/main.c` is the audio and game logic running on CPU1. `vga_core_cpu0/src/main.cc` is the VGA display and menu logic running on CPU0. Both applications share `beatmap.h` (note timestamps for all 3 songs) and `shared_memory.h` (the DDR memory map used for inter-core communication). `final.tcl` is the XSCT boot script that programs the board and starts both cores.

`ip/` contains the custom VHDL IP core: `audio_mixer_v1_0.vhd` is the top level and `audio_mixer_v1_0_S00_AXI.vhd` is the AXI slave with the volume scaling logic.

`data/audio/` contains the raw audio files for each song (48 kHz signed 16-bit PCM mono): `song1/FULL_daftpunk.raw`, `song2/full_levels.raw`, and `song3/song3.raw`.
