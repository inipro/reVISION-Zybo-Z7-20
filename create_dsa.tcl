#SDSOC Platform DSA Generation Script

set pfm_board_id zybo_z7_20 
set pfm_vendor inipro.net
set project_name z7_20_hdmi
set pfm_ver 1.0
set pfm_name $pfm_vendor:$pfm_board_id:$project_name:$pfm_ver
set bd_name system

set_property PFM_NAME $pfm_name [get_files ./$project_name.srcs/sources_1/bd/$bd_name/$bd_name.bd]

set_property PFM.CLOCK { \
	BUFG_O {id "0" is_default "false" proc_sys_reset "rst_util_ds_buf_0_100M"} \
} [get_bd_cells /util_ds_buf_0]
set_property PFM.CLOCK { \
	FCLK_CLK3 {id "1" is_default "true" proc_sys_reset "rst_ps7_0_50M"} \
} [get_bd_cells /processing_system7_0]

set parVal []
for {set i 1} {$i < 64} {incr i} {
	lappend parVal M[format %02d $i]_AXI \
	{memport "M_AXI_GP"}
}
set_property PFM.AXI_PORT $parVal [get_bd_cells /ps7_0_axi_periph]

set parVal []
for {set i 4} {$i < 64} {incr i} {
	lappend parVal M[format %02d $i]_AXI \
	{memport "M_AXI_GP"}
}
set_property PFM.AXI_PORT $parVal [get_bd_cells /ps7_0_axi_periph_1]

set parVal []
for {set i 2} {$i < 16} {incr i} {
	lappend parVal S[format %02d $i]_AXI \
	{memport "S_AXI_HP" sptag "HP0" memory "ps7 HP0_DDR_LOWOCM"}
}
set_property PFM.AXI_PORT $parVal [get_bd_cells /axi_smc]


set_property PFM.AXI_PORT { \
	S_AXI_ACP {memport "S_AXI_ACP" sptag "ACP" memory "ps7 ACP_DDR_LOWOCM"} \
	S_AXI_HP1 {memport "S_AXI_HP" sptag "HP1" memory "ps7 HP1_DDR_LOWOCM"} \
	S_AXI_HP2 {memport "S_AXI_HP" sptag "HP2" memory "ps7 HP2_DDR_LOWOCM"} \
	S_AXI_HP3 {memport "S_AXI_HP" sptag "HP3" memory "ps7 HP3_DDR_LOWOCM"} \
} [get_bd_cells /processing_system7_0]

set intVar []
for {set i 3} {$i < 16} {incr i} {
	lappend intVar In$i {}
}
set_property PFM.IRQ $intVar [get_bd_cells /xlconcat_0]

set_property dsa.ip_cache_dir [get_property ip_output_repo [current_project]] [current_project]

#set_property dsa.board_id $pfm_board_id [current_project]
#set_property dsa.vendor $pfm_vendor [current_project]
#set_property dsa.version $pfm_ver [current_project]

write_dsa -force -include_bit ../$project_name.dsa
