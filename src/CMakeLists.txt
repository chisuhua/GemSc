# src/CMakeLists.txt — CppTLM 核心库 + 可执行文件

# ==============================================================================
# 核心静态库（显式列出源文件，不用 GLOB）
# ==============================================================================
# 注意：packet/packet.cc 和 noc/*.cc 为历史遗留文件，与当前头文件不兼容
#       暂不编译，待后续重构
set(CORE_SOURCES
    # core
    core/connection_resolver.cc
    core/coherence_domain.cc
    core/module_factory.cc
    core/module_factory_validate.cc
    core/plugin_loader.cc
    core/port_compatibility.cc
    core/param_parser.cc
    core/topology_node.cc
    core/topology_parser.cc
    # utils
    utils/dynamic_loader.cc
    # tlm (Phase 7: RouterTLM + NICTLM + LinkTLM + Phase 2 SimModule 容器 + Phase 3 ChStream helpers)
    tlm/router_tlm.cc
    tlm/nic_tlm.cc
    tlm/link_tlm.cc
    tlm/cache_tlm.cc
    tlm/crossbar_tlm.cc
    tlm/coherent_xbar_tlm.cc
    tlm/cluster/compute_cluster.cc
    tlm/cluster/tpc_cluster.cc
    tlm/cluster/gpc_cluster.cc
    tlm/cluster/gpu_cluster.cc
    tlm/cluster/cache_cluster.cc
    tlm/cluster/memory_cluster.cc
    tlm/cluster/gpu_noc_cluster.cc
    tlm/cluster/apu_soc.cc
    # gpu_soc Phase 8.A: 基础设施层 (Task 1-4 独立模块, 不依赖 F12)
    tlm/gpu/shared_memory_tlm.cc
    tlm/gpu/memory_cluster_tlm.cc
    tlm/gpu/gpu_mesh_noc_tlm.cc
    # Task 12: kernel_launch_tlm.cc + cuda_core_adapter_mvp.cc + ptx_emu_submodule_mvp.cc 删除
    # Task 16 (扩展): wavefront_tlm.cc + vector_regfile_tlm.cc + minimal_warp_scheduler_tlm.cc 删除
    # (.hh 保留为 [[deprecated]] typedef 桥接, .cc 已无引用)
    # Task 9: 删除 gpu_compute_unit_tlm.cc (rename -> streaming_multiprocessor_tlm.cc)
    tlm/gpu/gpu_soc_tlm.cc
    tlm/gpu/dgpu_soc.cc
    tlm/gpu/dgpu_board_shell.cc
    # SM Microarchitecture Rewrite (Task 4) — stub; Task 18 full impl
    tlm/gpu/streaming_multiprocessor_tlm.cc
    # SM Task 1.3 P1-3: ScalarALU 真值 (独立 cpptlm::gpu::ScalarALU, per plan)
    tlm/gpu/sm/scalar_alu.cc
    # SM Task 2.1 P1-1: 12 子模块 stub 拆分 (保类名 FetchUnitTLM, per plan v2 P0-5)
    tlm/gpu/sm/fetch_unit_tlm.cc
    tlm/gpu/sm/decode_unit_tlm.cc
    tlm/gpu/sm/issue_unit_tlm.cc
    tlm/gpu/sm/scalar_alu_tlm.cc        # stub (NOT cpptlm::gpu::ScalarALU 真值)
    tlm/gpu/sm/vector_alu_tlm.cc
    tlm/gpu/sm/matrix_core_tlm.cc
    tlm/gpu/sm/simt_lane_tlm.cc
    tlm/gpu/sm/lsu_global_tlm.cc
    tlm/gpu/sm/lsu_lds_tlm.cc
    tlm/gpu/sm/reg_file_unit_tlm.cc
    tlm/gpu/sm/writeback_unit_tlm.cc
    tlm/gpu/sm/hazard_tracker_tlm.cc
    # HSK-8 Phase 2 Step 4: 桥接残留簇删除 (PTX-EMU 已废弃 cpptlm_bridge.h)
    # - tlm/gpu/memory_bridge.cc       (deprecated by HSK-6, removed in v3.0 P4 — early delete OK)
    # - tlm/gpu/ptx_emu_driver_shim.cc (cpptlm_set_driver removed by HSK-6 — early delete OK)
    # - tlm/gpu/dgpu_bar.cc (s2 W3 BAR0+VRAM wrapper; T-bs-2d 93e05ab 已物理删除 — BAR0 由
    #   PcieEndpointTLM::bar0_registers JSON 接管; VRAM 由 MemoryTLM `vram0` 接管)
    tlm/gpu/doorbell_mvp.cc        # SQ tail register + strong-order write (T-bs-2d 后已不
                                   # 是 s2 顶级 API; 现为 PcieBarRouter 内部 helper,
                                   # 物理保留作 PIMPL 封装)
    tlm/gpu/completion_ring_mvp.cc # s2 W3: push + host_notify hook
    tlm/gpu/submit_queue_mvp.cc    # s2 W3: WDU distribution network (per Phase F-H.5)
    tlm/gpu/command_processor_mvp.cc  # s2 W3: CP skeleton (5-state FSM no-op + set_decoder injection)
    tlm/gpu/pm4_decoder_mvp.cc        # s3 T-s3-1: Pm4Decoder 4 method_addr ranges + UNKNOWN error channel
    tlm/gpu/tmu_dispatch_processor_tlm.cc  # s5 W5 T-bs-2c-TMU: TMU 命名提升 _mvp → _tlm
    tlm/gpu/pcie_config_space_mvp.cc  # pcie-endpoint T-pe-2: Config Space MVP
    tlm/gpu/pcie_bar_router_mvp.cc     # pcie-endpoint T-pe-2: BAR0 register routing (data-driven)
    tlm/gpu/msix_table_mvp.cc          # pcie-endpoint T-pe-2: MSI-X vector table + mask
    tlm/gpu/pcie_endpoint_tlm.cc       # pcie-endpoint T-pe-3: 4-port ChStream module
    tlm/gpu/sdma_engine_tlm.cc        # sdma-engine T-sd-2: 5-port ChStream SDMA/copy engine (PCIe master)
    # Task 13: PipelineTLM/ScoreboardTLM/TensorCoreTLM + cudart 物理删除 (功能内化到 SM 子模块)
    # pcie-ip-microarch Phase 1: 链路层 + FC Token Bucket (P1)
    tlm/pcie/pcie_flow_control_token_bucket.cc
    tlm/pcie/pcie_link_layer_tlm.cc
    # pcie-ip-microarch Phase 2: 128b/130b 编码延迟建模器 (T-P2-3)
    tlm/pcie/pcie_encoding_latency_model.cc
    # pcie-ip-microarch Phase 3: PHY Digital Ctrl + Bypass Mux (T-P3-1)
    tlm/pcie/pcie_bypass_mux.cc
    tlm/pcie/pcie_phy_digital_ctrl_tlm.cc
    # pcie-ip-microarch Phase 4: SR-IOV VF Pool (T-P4-4/5/6)
    tlm/pcie/pcie_sriov_vf_pool_tlm.cc
    tlm/pcie/pcie_completion_tracker_tlm.cc
    # pcie-ip-microarch Phase 4: PcieEndpointIP 新类 17 端口 (T-P4-7)
    tlm/pcie/pcie_endpoint_ip.cc
    # pcie-ip-microarch Phase 5: AXI Stream Adapter (T-P5-2)
    framework/axi4_stream_adapter.cc
    # pcie-ip-microarch Phase 5: PcieAxiAdapter (T-P5-3)
    tlm/pcie/pcie_axi_adapter_tlm.cc
    # pcie-ip-microarch Phase 6: AXI4Mapper 信号 ↔ Bundle 转换 (T-P6-1)
    framework/axi4_signal_to_bundle.cc
    framework/axi4_bundle_to_signal.cc
    # pcie-ip-microarch Phase 6: Axi4Mapper 独立模块 (T-P6-2..T-P6-3)
    framework/axi4_mapper.cc
    # pcie-ip-microarch Phase 7: HostBypassTLM (T-P7-1)
    tlm/pcie/host_bypass_tlm.cc
    # pcie-ip-microarch Phase 7: PcieRootComplexTLM (T-P7-3)
    tlm/pcie/pcie_root_complex_tlm.cc
)

# ==============================================================================
# HSK-8 Phase 2 (PTX-EMU owner ack 738b412c): 已物理删除 (Task 12)
# PTX-EMU 集成代码已废弃, 新架构走 IComputeDevice 接口 (per SM 重构 §15.5):
#   - ptx_emu_submodule_mvp.cc → 删除 (Task 12)
#   - cuda_core_adapter_mvp.cc → 删除 (Task 12)
# 旧 S1 (c2038a93 pin) 的 WU-2 stub.cc + WU-3/WU-4 OBJECT 库块 也已废弃.
# ==============================================================================
# CPPTLM_WITH_PTX_EMU 块已清空, 标记 deprecated. PTX-EMU 集成改走 IComputeDevice
# (per openspec/changes/cpptlm-dgpu-d1-cdna-isa-sm-rewrite)

# s2 W3-4: 8-component 单体 retired (T-bs-5, c064628 cycle); SOC topology
# 走 DGpuBoard C++ ABI shell + DGpuSoc SimModule 容器 (per ADR-SOC-07 D1/D7).
# UsrLinuxEmuIoctlStub (host-side IOCTL bridge) 已随 s2 W4 退役: IOCTL 语义
# 走 23 ABI cpptlm_emulator_register_callbacks (per ADR-088 §D5 + W3.2).
# CORE_SOURCES 此处留空 — s2 单体相关源已全部 T-bs-5 commit 1+2 清理.

add_library(cpptlm_core STATIC ${CORE_SOURCES})
# T-W3-2 (T-ae-1): cpptlm_core 增 PIC,使其可被链入 SHARED cpptlm_emulator
# (per Oracle Q2 + ADR-SOC-07 D5: cpptlm_core 保持 STATIC,仅增 PIC 不破坏
# PTX-EMU 端 ExternalProject_Add 静态消费路径; ABI 暴露走独立 SHARED).
set_target_properties(cpptlm_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CPPTLM_WITH_PTX_EMU)
    target_compile_definitions(cpptlm_core PRIVATE CPPTLM_WITH_PTX_EMU)
endif()
if(CPPTLM_WITH_PTX_EMU)
    # HSK-8 Phase 2 (per #22 comment step 2): link ptxemu_core (PUBLIC
    # propagates include path + lib deps to cpptlm_core consumers)
    target_link_libraries(cpptlm_core PUBLIC ptxemu_core)
endif()

# T-W3-2 (T-ae-1): 23 ABI 暴露层 SHARED 库 cpptlm_emulator
# (per ADR-088 §D5 + ADR-SOC-07 D5 + Oracle Q2: 19 forward ABI + 3 register
# 函数 = 22 显式 CPPTLM_EMULATOR_EXPORT 导出; 默认 -fvisibility=hidden)
add_library(cpptlm_emulator SHARED abi/cpptlm_emulator.cc)
target_compile_options(cpptlm_emulator PRIVATE -fvisibility=hidden)
target_link_libraries(cpptlm_emulator PRIVATE cpptlm_core)

# 包含目录：
#   - include/       → 公共头文件（core/*.hh, utils/*.hh 等）
#   - include/core/  → 兼容旧式 #include "xxx.hh"（无 core/ 前缀）
#   - external/json  → nlohmann/json
target_include_directories(cpptlm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/json
)

# 覆盖率选项
if(ENABLE_COVERAGE)
    target_compile_options(cpptlm_core PRIVATE --coverage)
    target_link_options(cpptlm_core PRIVATE --coverage)
endif()

# ==============================================================================
# 可执行文件
# ==============================================================================

# 主仿真程序（不依赖 SystemC 库，仅需 cpptlm_core 静态库）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
    add_executable(cpptlm_sim main.cpp)
    target_link_libraries(cpptlm_sim PRIVATE cpptlm_core)
    if(CPPTLM_WITH_PTX_EMU)
        # HSK-8 Phase 2 Step 6: cpptlm_sim 需要 PTX-EMU shared libs
        # (libptxemu_device.so / libptxsim.so / libptx_parser.so / libcudart.so
        #  提供 CudaDriver::instance() / SimpleMemoryAllocator::init() /
        #  PtxInterpreter::launchPtxInterpreter() / AtomicLockGuard 等符号)。
        target_link_libraries(cpptlm_sim PRIVATE
            ptxemu_device ptxsim ptx_parser ptxir cudart)
    endif()
endif()

    # CPU 模式主程序
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cpu_main.cpp")
    add_executable(cpptlm_cpu cpu_main.cpp)
    target_link_libraries(cpptlm_cpu PRIVATE cpptlm_core)
endif()

# 流量仿真主程序
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/traffic_main.cpp")
    add_executable(cpptlm_traffic traffic_main.cpp)
    target_link_libraries(cpptlm_traffic PRIVATE cpptlm_core)
endif()

# 注：输出目录已在根 CMakeLists.txt 中设置
