// test/test_gpu_soc_tlm.cc
// GpuSocTLM 单元测试 (Task 11 重写: KernelLaunchTLM -> IComputeDevice)
// 验证 GpuSocTLM 构造 + 4 个 setter/getter + tick() 推进子模块
// 作者: CppTLM Team / 日期: 2026-07-02 (修订 2027-02-09 Task 11)
#include "chstream_register.hh"
#include "core/event_queue.hh"
#include "core/module_factory.hh"
#include "modules.hh"
#include "tlm/cluster/gpu_cluster.hh"
#include "tlm/gpu/gpu_mesh_noc_tlm.hh"
#include "tlm/gpu/gpu_soc_tlm.hh"
#include "tlm/gpu/i_compute_device.hh"
#include "tlm/gpu/memory_cluster_tlm.hh"
#include <catch2/catch_all.hpp>

using namespace tlm;
using namespace cpptlm::tlm;
using namespace cpptlm::gpu;

namespace {
    void registerModules() {
        static bool done = false;
        if (!done) {
            REGISTER_OBJECT;
            REGISTER_CHSTREAM;
            done = true;
        }
    }

    // Minimal stub: IComputeDevice 15 methods (Task 18 替换为 SM 顶层)
    class StubComputeDevice : public IComputeDevice {
    public:
        bool initialize(const DeviceConfig&) override {
            return true;
        }
        void shutdown() override {
        }
        int exe_once() override {
            return 0;
        }
        int sm_exe_once(uint32_t) override {
            return 0;
        }
        int warp_exe_once(uint32_t, uint32_t) override {
            return 0;
        }
        bool set_scoreboard(uint32_t, uint32_t, uint64_t) override {
            return true;
        }
        ThreadState get_thread_state(uint32_t, uint32_t, uint32_t) override {
            return ThreadState::kIdle;
        }
        bool set_active_mask(uint32_t, uint32_t, uint64_t) override {
            return true;
        }
        bool set_next_pc(uint32_t, uint32_t, uint32_t, uint32_t) override {
            return true;
        }
        WarpStatus get_warp_status(uint32_t, uint32_t) override {
            return {};
        }
        bool is_finished() override {
            return false;
        }
        void set_instr_descriptor_buf(const InstrDescriptor*, uint32_t) override {
        }
        bool get_register_value(uint32_t, uint32_t, uint32_t, uint64_t* out, uint32_t) override {
            *out = 0;
            return true;
        }
        bool is_instruction_completed(uint64_t) override {
            return false;
        }
        void reset() override {
        }
    };
} // namespace

TEST_CASE("GpuSocTLM.ConstructorDefaults", "[gpu][soc][phase8a][sm-microarch]") {
    registerModules();
    EventQueue eq;
    GpuSocTLM soc("soc", &eq);

    REQUIRE(soc.get_module_type() == "GpuSocTLM");
    REQUIRE(soc.get_gpu_cluster() == nullptr);
    REQUIRE(soc.get_noc() == nullptr);
    REQUIRE(soc.get_memory_cluster() == nullptr);
    REQUIRE(soc.get_compute_device() == nullptr);
}

TEST_CASE("GpuSocTLM.ComputeDeviceSetterGetter", "[gpu][soc][phase8a][sm-microarch]") {
    registerModules();
    EventQueue eq;

    GpuSocTLM soc("soc", &eq);
    StubComputeDevice dev;

    soc.set_compute_device(&dev);

    REQUIRE(soc.get_compute_device() == &dev);
}

TEST_CASE("GpuSocTLM.TickNoCrash", "[gpu][soc][phase8a][sm-microarch]") {
    registerModules();
    EventQueue eq;

    GpuSocTLM soc("soc", &eq);
    GpuCluster cluster("cluster", &eq);
    GpuMeshNoC noc("noc", &eq);
    MemoryClusterTLM mem("mem", &eq);
    StubComputeDevice dev;

    soc.set_gpu_cluster(&cluster);
    soc.set_noc(&noc);
    soc.set_memory_cluster(&mem);
    soc.set_compute_device(&dev);

    for (int i = 0; i < 10; ++i) {
        soc.tick();
    }
    REQUIRE(true);
}
