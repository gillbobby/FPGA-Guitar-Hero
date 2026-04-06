# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct C:\Zynq_Book\vitis_workspace\zedboard_platform\platform.tcl
# 
# OR launch xsct and run below command.
# source C:\Zynq_Book\vitis_workspace\zedboard_platform\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {zedboard_platform}\
-hw {C:\Zynq_Book\vga_tutorial\vga_tutorial\milestone_1_wrapper.xsa}\
-proc {ps7_cortexa9_0} -os {standalone} -no-boot-bsp -fsbl-target {psu_cortexa53_0} -out {C:/Zynq_Book/vitis_workspace}

platform write
platform generate -domains 
platform active {zedboard_platform}
platform generate
domain create -name {standalone_ps7_cortexa9_1} -os {standalone} -proc {ps7_cortexa9_1} -arch {32-bit} -display-name {standalone_ps7_cortexa9_1} -desc {} -runtime {cpp}
platform generate -domains 
platform write
domain -report -json
platform clean
platform generate
