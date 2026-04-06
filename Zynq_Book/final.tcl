#  final.tcl XSCT boot script 

connect

fpga -f C:/Zynq_Book/vitis_workspace/zedboard_platform/hw/mixer_volume_design_1_wrapper.bit
source C:/Zynq_Book/vitis_workspace/zedboard_platform/hw/ps7_init.tcl

# Reset cores
targets -set -nocase -filter {name =~ "*Cortex-A9 MPCore #0*"}
ps7_init
ps7_post_config

targets -set -nocase -filter {name =~ "*Cortex-A9 MPCore #0*"}
rst -processor
targets -set -nocase -filter {name =~ "*Cortex-A9 MPCore #1*"}
rst -processor



# Song 1 — Harder Better Faster Stronger (full original mix)
dow -data C:/Zynq_Book/vitis_workspace/data/audio/song1/FULL_daftpunk.raw  0x11000000

# Song 2 — Levels
dow -data C:/Zynq_Book/vitis_workspace/data/audio/song2/full_levels.raw  0x14000000

# Song 3 — Beat it
dow -data C:/Zynq_Book/vitis_workspace/data/audio/song3/song3.raw  0x17000000


targets -set -nocase -filter {name =~ "*Cortex-A9 MPCore #0*"}
dow C:/Zynq_Book/vitis_workspace/vga_core_cpu0/Debug/vga_core_cpu0.elf

targets -set -nocase -filter {name =~ "*Cortex-A9 MPCore #1*"}
dow C:/Zynq_Book/vitis_workspace/audio_core_cpu1/Debug/audio_core_cpu1.elf



targets -set -nocase -filter {name =~ "*Cortex-A9 MPCore #0*"}
con

after 3000

targets -set -nocase -filter {name =~ "*Cortex-A9 MPCore #1*"}
con

puts "System now running."