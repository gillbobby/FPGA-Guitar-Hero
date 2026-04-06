# Usage with Vitis IDE:
# In Vitis IDE create a Single Application Debug launch configuration,
# change the debug type to 'Attach to running target' and provide this 
# tcl script in 'Execute Script' option.
# Path of this script: C:\Zynq_Book\vitis_workspace\audio_core_cpu1_system\_ide\scripts\debugger_audio_core_cpu1-default.tcl
# 
# 
# Usage with xsct:
# To debug using xsct, launch xsct and run below command
# source C:\Zynq_Book\vitis_workspace\audio_core_cpu1_system\_ide\scripts\debugger_audio_core_cpu1-default.tcl
# 
connect -url tcp:127.0.0.1:3121
targets -set -nocase -filter {name =~"APU*"}
rst -system
after 3000
targets -set -filter {jtag_cable_name =~ "Digilent Zed 210248469767" && level==0 && jtag_device_ctx=="jsn-Zed-210248469767-03727093-0"}
fpga -file C:/Zynq_Book/vitis_workspace/audio_core_cpu1/_ide/bitstream/milestone_1_wrapper.bit
targets -set -nocase -filter {name =~"APU*"}
loadhw -hw C:/Zynq_Book/vitis_workspace/zedboard_platform/export/zedboard_platform/hw/milestone_1_wrapper.xsa -mem-ranges [list {0x40000000 0xbfffffff}] -regs
configparams force-mem-access 1
targets -set -nocase -filter {name =~"APU*"}
source C:/Zynq_Book/vitis_workspace/audio_core_cpu1/_ide/psinit/ps7_init.tcl
ps7_init
ps7_post_config
targets -set -nocase -filter {name =~ "*A9*#1"}
dow C:/Zynq_Book/vitis_workspace/audio_core_cpu1/Debug/audio_core_cpu1.elf
configparams force-mem-access 0
targets -set -nocase -filter {name =~ "*A9*#1"}
con
