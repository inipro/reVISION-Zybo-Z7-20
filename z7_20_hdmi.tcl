set project_name z7_20_hdmi
set part_name xc7z020clg400-1
set ip_dir repo
set bd_name system
set bd_path $project_name/$project_name.srcs/sources_1/bd/$bd_name

file delete -force $project_name

create_project -part $part_name $project_name $project_name

set_property ip_repo_paths $ip_dir [current_project]
update_ip_catalog

create_bd_design $bd_name

create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 processing_system7_0
source z7_20_hdmi_preset.tcl
set_property -dict [apply_preset IPINST] [get_bd_cells processing_system7_0]
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 -config {make_external "FIXED_IO, DDR" Master "Disable" Slave "Disable" }  [get_bd_cells processing_system7_0]

create_bd_cell -type ip -vlnv xilinx.com:ip:util_ds_buf:2.1 util_ds_buf_0
set_property -dict [list CONFIG.C_BUF_TYPE {BUFG}] [get_bd_cells util_ds_buf_0]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] [get_bd_pins util_ds_buf_0/BUFG_I]

create_bd_cell -type ip -vlnv xilinx.com:ip:v_frmbuf_wr:2.0 v_frmbuf_wr_0
set_property -dict [list CONFIG.C_M_AXI_MM_VIDEO_DATA_WIDTH {64} CONFIG.SAMPLES_PER_CLOCK {1} CONFIG.AXIMM_DATA_WIDTH {64}] [get_bd_cells v_frmbuf_wr_0]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {Master "/processing_system7_0/M_AXI_GP0" intc_ip "New AXI Interconnect" Clk_xbar "/util_ds_buf_0/BUFG_O (100 MHz)" Clk_master "/util_ds_buf_0/BUFG_O (100 MHz)" Clk_slave "/util_ds_buf_0/BUFG_O (100 MHz)" }  [get_bd_intf_pins v_frmbuf_wr_0/s_axi_CTRL]
set_property offset 0x40000000 [get_bd_addr_segs {processing_system7_0/Data/SEG_v_frmbuf_wr_0_Reg}]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_RESET0_N] [get_bd_pins rst_util_ds_buf_0_100M/ext_reset_in]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {Master "/v_frmbuf_wr_0/m_axi_mm_video" intc_ip "Auto" Clk_xbar "/util_ds_buf_0/BUFG_O (100 MHz)" Clk_master "/util_ds_buf_0/BUFG_O (100 MHz)" Clk_slave "/util_ds_buf_0/BUFG_O (100 MHz)" }  [get_bd_intf_pins processing_system7_0/S_AXI_HP0]

delete_bd_objs [get_bd_intf_nets v_frmbuf_wr_0_m_axi_mm_video]
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_data_fifo:2.1 axi_data_fifo_0
set_property -dict [list CONFIG.DATA_WIDTH {64} CONFIG.READ_FIFO_DEPTH {512} CONFIG.READ_FIFO_DELAY {1}] [get_bd_cells axi_data_fifo_0]
connect_bd_intf_net [get_bd_intf_pins axi_data_fifo_0/M_AXI] [get_bd_intf_pins axi_smc/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins v_frmbuf_wr_0/m_axi_mm_video] [get_bd_intf_pins axi_data_fifo_0/S_AXI]
apply_bd_automation -rule xilinx.com:bd_rule:clkrst -config {Clk "/util_ds_buf_0/BUFG_O (100 MHz)" }  [get_bd_pins axi_data_fifo_0/aclk]

create_bd_cell -type ip -vlnv xilinx.com:ip:v_vid_in_axi4s:4.0 v_vid_in_axi4s_0
set_property -dict [list CONFIG.C_HAS_ASYNC_CLK {1} CONFIG.C_ADDR_WIDTH {12}] [get_bd_cells v_vid_in_axi4s_0]
connect_bd_intf_net [get_bd_intf_pins v_vid_in_axi4s_0/video_out] [get_bd_intf_pins v_frmbuf_wr_0/s_axis_video]
apply_bd_automation -rule xilinx.com:bd_rule:clkrst -config {Clk "/util_ds_buf_0/BUFG_O (100 MHz)" }  [get_bd_pins v_vid_in_axi4s_0/aclk]

create_bd_cell -type ip -vlnv xilinx.com:ip:v_tc:6.1 v_tc_0
set_property -dict [list CONFIG.horizontal_blank_detection {false} CONFIG.enable_generation {false} CONFIG.vertical_blank_detection {false}] [get_bd_cells v_tc_0]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {Master "/processing_system7_0/M_AXI_GP1" intc_ip "Auto" Clk_xbar "/processing_system7_0/FCLK_CLK3 (50 MHz)" Clk_master "/processing_system7_0/FCLK_CLK3 (50 MHz)" Clk_slave "/processing_system7_0/FCLK_CLK3 (50 MHz)" }  [get_bd_intf_pins v_tc_0/ctrl]
set_property offset 0x80000000 [get_bd_addr_segs {processing_system7_0/Data/SEG_v_tc_0_Reg}]
#apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {Master "/processing_system7_0/M_AXI_GP0" intc_ip "/ps7_0_axi_periph" Clk_xbar "/util_ds_buf_0/BUFG_O (100 MHz)" Clk_master "/util_ds_buf_0/BUFG_O (100 MHz)" Clk_slave "/util_ds_buf_0/BUFG_O (100 MHz)" }  [get_bd_intf_pins v_tc_0/ctrl]
#set_property offset 0x41000000 [get_bd_addr_segs {processing_system7_0/Data/SEG_v_tc_0_Reg}]
connect_bd_intf_net [get_bd_intf_pins v_vid_in_axi4s_0/vtiming_out] [get_bd_intf_pins v_tc_0/vtiming_in]

create_bd_cell -type ip -vlnv digilentinc.com:ip:dvi2rgb:1.8 dvi2rgb_0
set_property -dict [list CONFIG.kRstActiveHigh {false} CONFIG.kClkRange {2}] [get_bd_cells dvi2rgb_0]
apply_bd_automation -rule xilinx.com:bd_rule:clkrst -config {Clk "/processing_system7_0/FCLK_CLK1 (200 MHz)" }  [get_bd_pins dvi2rgb_0/RefClk]
connect_bd_net [get_bd_pins rst_util_ds_buf_0_100M/peripheral_aresetn] [get_bd_pins dvi2rgb_0/aRst_n]
connect_bd_intf_net [get_bd_intf_pins dvi2rgb_0/RGB] [get_bd_intf_pins v_vid_in_axi4s_0/vid_io_in]
connect_bd_net [get_bd_pins dvi2rgb_0/PixelClk] [get_bd_pins v_vid_in_axi4s_0/vid_io_in_clk]
connect_bd_net [get_bd_pins dvi2rgb_0/PixelClk] [get_bd_pins v_tc_0/clk]

create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 proc_sys_reset_0
connect_bd_net [get_bd_pins proc_sys_reset_0/ext_reset_in] [get_bd_pins processing_system7_0/FCLK_RESET0_N]
connect_bd_net [get_bd_pins dvi2rgb_0/PixelClk] [get_bd_pins proc_sys_reset_0/slowest_sync_clk]
connect_bd_net [get_bd_pins proc_sys_reset_0/peripheral_reset] [get_bd_pins v_vid_in_axi4s_0/vid_io_in_reset]
connect_bd_net [get_bd_pins proc_sys_reset_0/peripheral_aresetn] [get_bd_pins v_tc_0/resetn]
connect_bd_net [get_bd_pins dvi2rgb_0/aPixelClkLckd] [get_bd_pins proc_sys_reset_0/aux_reset_in]
connect_bd_net [get_bd_pins dvi2rgb_0/aPixelClkLckd] [get_bd_pins processing_system7_0/GPIO_I]

make_bd_intf_pins_external  [get_bd_intf_pins dvi2rgb_0/TMDS]
set_property name hdmi_in [get_bd_intf_ports TMDS_0]
make_bd_intf_pins_external  [get_bd_intf_pins dvi2rgb_0/DDC]
set_property name hdmi_in_ddc [get_bd_intf_ports DDC_0]

create_bd_port -dir O hdmi_in_hpd
create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 xlconstant_0
connect_bd_net [get_bd_ports hdmi_in_hpd] [get_bd_pins xlconstant_0/dout]

create_bd_cell -type ip -vlnv xilinx.com:ip:axi_vdma:6.3 axi_vdma_0
set_property -dict [list CONFIG.c_m_axis_mm2s_tdata_width {24} CONFIG.c_num_fstores {1} CONFIG.c_s2mm_genlock_mode {0} CONFIG.c_mm2s_linebuffer_depth {2048} CONFIG.c_mm2s_max_burst_length {32} CONFIG.c_include_s2mm {0} CONFIG.c_include_mm2s_dre {1}] [get_bd_cells axi_vdma_0]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {Master "/processing_system7_0/M_AXI_GP1" intc_ip "Auto" Clk_xbar "/processing_system7_0/FCLK_CLK3 (50 MHz)" Clk_master "/processing_system7_0/FCLK_CLK3 (50 MHz)" Clk_slave "/processing_system7_0/FCLK_CLK3 (50 MHz)" }  [get_bd_intf_pins axi_vdma_0/S_AXI_LITE]
set_property offset 0x81000000 [get_bd_addr_segs {processing_system7_0/Data/SEG_axi_vdma_0_Reg}]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {Slave "/processing_system7_0/S_AXI_HP0" intc_ip "/axi_smc" Clk_xbar "/util_ds_buf_0/BUFG_O (100 MHz)" Clk_master "/util_ds_buf_0/BUFG_O (100 MHz)" Clk_slave "/util_ds_buf_0/BUFG_O (100 MHz)" }  [get_bd_intf_pins axi_vdma_0/M_AXI_MM2S]

create_bd_cell -type ip -vlnv xilinx.com:ip:axis_subset_converter:1.1 axis_subset_converter_0
set_property -dict [list CONFIG.S_TDATA_NUM_BYTES {3} CONFIG.M_TDATA_NUM_BYTES {3} CONFIG.S_TUSER_WIDTH {1} CONFIG.M_TUSER_WIDTH {1} CONFIG.S_HAS_TLAST {1} CONFIG.M_HAS_TLAST {1} CONFIG.TDATA_REMAP {tdata[7:0],tdata[23:16],tdata[15:8]} CONFIG.TUSER_REMAP {tuser[0:0]} CONFIG.TLAST_REMAP {tlast[0]}] [get_bd_cells axis_subset_converter_0]
connect_bd_intf_net [get_bd_intf_pins axi_vdma_0/M_AXIS_MM2S] [get_bd_intf_pins axis_subset_converter_0/S_AXIS]
apply_bd_automation -rule xilinx.com:bd_rule:clkrst -config {Clk "/util_ds_buf_0/BUFG_O (100 MHz)" }  [get_bd_pins axi_vdma_0/m_axis_mm2s_aclk]

create_bd_cell -type ip -vlnv xilinx.com:ip:v_axi4s_vid_out:4.0 v_axi4s_vid_out_0
set_property -dict [list CONFIG.C_HAS_ASYNC_CLK {1} CONFIG.C_ADDR_WIDTH {12} CONFIG.C_VTG_MASTER_SLAVE {1}] [get_bd_cells v_axi4s_vid_out_0]
connect_bd_intf_net [get_bd_intf_pins axis_subset_converter_0/M_AXIS] [get_bd_intf_pins v_axi4s_vid_out_0/video_in]
apply_bd_automation -rule xilinx.com:bd_rule:clkrst -config {Clk "/util_ds_buf_0/BUFG_O (100 MHz)" }  [get_bd_pins v_axi4s_vid_out_0/aclk]

create_bd_cell -type ip -vlnv digilentinc.com:ip:axi_dynclk:1.0 axi_dynclk_0
apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {Master "/processing_system7_0/M_AXI_GP1" intc_ip "Auto" Clk_xbar "/processing_system7_0/FCLK_CLK3 (50 MHz)" Clk_master "/processing_system7_0/FCLK_CLK3 (50 MHz)" Clk_slave "/processing_system7_0/FCLK_CLK3 (50 MHz)" }  [get_bd_intf_pins axi_dynclk_0/s00_axi]
set_property offset 0x82000000 [get_bd_addr_segs {processing_system7_0/Data/SEG_axi_dynclk_0_reg0}]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK2] [get_bd_pins axi_dynclk_0/REF_CLK_I]

create_bd_cell -type ip -vlnv xilinx.com:ip:v_tc:6.1 v_tc_1
set_property -dict [list CONFIG.enable_detection {false}] [get_bd_cells v_tc_1]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config {Master "/processing_system7_0/M_AXI_GP1" intc_ip "Auto" Clk_xbar "/processing_system7_0/FCLK_CLK3 (50 MHz)" Clk_master "/processing_system7_0/FCLK_CLK3 (50 MHz)" Clk_slave "/processing_system7_0/FCLK_CLK3 (50 MHz)" }  [get_bd_intf_pins v_tc_1/ctrl]
set_property offset 0x83000000 [get_bd_addr_segs {processing_system7_0/Data/SEG_v_tc_1_Reg}]
connect_bd_net [get_bd_pins axi_dynclk_0/PXL_CLK_O] [get_bd_pins v_tc_1/clk]
connect_bd_net [get_bd_pins axi_dynclk_0/PXL_CLK_O] [get_bd_pins v_axi4s_vid_out_0/vid_io_out_clk]
connect_bd_intf_net [get_bd_intf_pins v_tc_1/vtiming_out] [get_bd_intf_pins v_axi4s_vid_out_0/vtiming_in]
connect_bd_net [get_bd_pins v_axi4s_vid_out_0/vtg_ce] [get_bd_pins v_tc_1/gen_clken]

create_bd_cell -type ip -vlnv digilentinc.com:ip:rgb2dvi:1.4 rgb2dvi_0
set_property -dict [list CONFIG.kRstActiveHigh {false} CONFIG.kGenerateSerialClk {false}] [get_bd_cells rgb2dvi_0]
connect_bd_net [get_bd_pins axi_dynclk_0/PXL_CLK_O] [get_bd_pins rgb2dvi_0/PixelClk]
connect_bd_net [get_bd_pins axi_dynclk_0/PXL_CLK_5X_O] [get_bd_pins rgb2dvi_0/SerialClk]
connect_bd_net [get_bd_pins axi_dynclk_0/LOCKED_O] [get_bd_pins rgb2dvi_0/aRst_n]
connect_bd_intf_net [get_bd_intf_pins v_axi4s_vid_out_0/vid_io_out] [get_bd_intf_pins rgb2dvi_0/RGB]
make_bd_intf_pins_external  [get_bd_intf_pins rgb2dvi_0/TMDS]
set_property name hdmi_out [get_bd_intf_ports TMDS_0]

make_bd_intf_pins_external  [get_bd_intf_pins processing_system7_0/IIC_0]
set_property name hdmi_out_ddc [get_bd_intf_ports IIC_0_0]

create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat:2.1 xlconcat_0
set_property -dict [list CONFIG.NUM_PORTS {3}] [get_bd_cells xlconcat_0]
connect_bd_net [get_bd_pins v_frmbuf_wr_0/interrupt] [get_bd_pins xlconcat_0/In0]
connect_bd_net [get_bd_pins axi_vdma_0/mm2s_introut] [get_bd_pins xlconcat_0/In1]
connect_bd_net [get_bd_pins v_tc_1/irq] [get_bd_pins xlconcat_0/In2]
connect_bd_net [get_bd_pins xlconcat_0/dout] [get_bd_pins processing_system7_0/IRQ_F2P]

regenerate_bd_layout
validate_bd_design
save_bd_design

generate_target all [get_files $bd_path/$bd_name.bd]
make_wrapper -files [get_files $bd_path/$bd_name.bd] -top

add_files -norecurse $bd_path/hdl/${bd_name}_wrapper.v

add_files -norecurse -fileset constrs_1 z7_20_hdmi.xdc
import_files -fileset constrs_1 z7_20_hdmi.xdc

set_property verilog_define {TOOL_VIVADO} [current_fileset]

close_project

