// test/test_sm_bundles.cc
// 8 Bundle POD 字段验证 (per plan Task 6 Step 1)
//
// 作者 CppTLM Team / 日期 2027-02-09 (SM 重构 Task 6)
#include "bundles/sm_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "tlm/gpu/instruction_descriptor.hh"

using namespace bundles::sm;

TEST_CASE("FetchToIssueBundle carries instr_desc + warp_id + pc", "[sm-bundle][sm-microarch]") {
    FetchToIssueBundle b{};
    b.warp_id = 5;
    b.pc = 0x1000;
    b.instr_desc.instr_id = 42;
    REQUIRE(b.warp_id == 5);
    REQUIRE(b.pc == 0x1000);
    REQUIRE(b.instr_desc.instr_id == 42);
}

TEST_CASE("DecodeToIssueBundle adds PipeClass + LatencyClass", "[sm-bundle][sm-microarch]") {
    DecodeToIssueBundle b{};
    b.pipe = cpptlm::gpu::PipeClass::kVectorALU;
    b.latency_class = cpptlm::gpu::LatencyClass::kFixed4Cycle;
    REQUIRE(b.pipe == cpptlm::gpu::PipeClass::kVectorALU);
    REQUIRE(b.latency_class == cpptlm::gpu::LatencyClass::kFixed4Cycle);
}

TEST_CASE("IssueToExecBundle adds src_values + src_valid (PTX-EMU upstream sync)",
          "[sm-bundle][sm-microarch]") {
    IssueToExecBundle b{};
    b.src_values[0] = 0xCAFEBABE;
    b.src_values[1] = 0xDEADBEEF;
    b.src_valid[0] = true;
    b.src_valid[1] = true;
    REQUIRE(b.src_values[0] == 0xCAFEBABE);
    REQUIRE(b.src_values[1] == 0xDEADBEEF);
    REQUIRE(b.src_valid[0]);
    REQUIRE(b.src_valid[1]);
}

TEST_CASE("ExecToWritebackBundle adds result_value + memory_data + exec_cycles",
          "[sm-bundle][sm-microarch]") {
    ExecToWritebackBundle b{};
    b.result_value[0] = 0x1234;
    b.memory_data_valid = true;
    b.memory_data = 0xABCD;
    b.exec_cycles = 4;
    REQUIRE(b.result_value[0] == 0x1234);
    REQUIRE(b.memory_data_valid);
    REQUIRE(b.memory_data == 0xABCD);
    REQUIRE(b.exec_cycles == 4);
}

TEST_CASE("WritebackToRegFileBundle carries dst_regs + values + is_accvgpr",
          "[sm-bundle][sm-microarch]") {
    WritebackToRegFileBundle b{};
    b.warp_id = 3;
    b.dst_regs[0] = 8;
    b.values[0] = 0xCAFE;
    b.num_dst = 1;
    b.is_accvgpr = true;
    REQUIRE(b.warp_id == 3);
    REQUIRE(b.dst_regs[0] == 8);
    REQUIRE(b.values[0] == 0xCAFE);
    REQUIRE(b.num_dst == 1);
    REQUIRE(b.is_accvgpr);
}

TEST_CASE("MemoryReqBundle carries vaddr + size + lane_mask", "[sm-bundle][sm-microarch]") {
    MemoryReqBundle b{};
    b.vaddr = 0x10000000ull;
    b.size = 32;
    b.is_write = false;
    b.sm_id = 1;
    b.wave_id = 2;
    b.tag = 0xABCD;
    b.lane_mask = 0xFF;
    REQUIRE(b.vaddr == 0x10000000ull);
    REQUIRE(b.size == 32);
    REQUIRE(b.sm_id == 1);
    REQUIRE(b.wave_id == 2);
    REQUIRE(b.lane_mask == 0xFF);
}

TEST_CASE("MemoryRespBundle carries data + cycles (HazardTracker release)",
          "[sm-bundle][sm-microarch]") {
    MemoryRespBundle b{};
    b.tag = 0xABCD;
    b.data = 0xDEADBEEF;
    b.is_hit = true;
    b.cycles = 100;
    REQUIRE(b.tag == 0xABCD);
    REQUIRE(b.data == 0xDEADBEEF);
    REQUIRE(b.is_hit);
    REQUIRE(b.cycles == 100);
}

TEST_CASE("ScoreboardQueryBundle QueryType enum + ctrl bits", "[sm-bundle][sm-microarch]") {
    ScoreboardQueryBundle b{};
    b.query_type = ScoreboardQueryBundle::QueryType::kIncrement;
    b.warp_id = 7;
    b.sm_id = 0;
    b.ctrl.branch_type = 1; // COND
    REQUIRE(b.query_type == ScoreboardQueryBundle::QueryType::kIncrement);
    REQUIRE(b.warp_id == 7);
    REQUIRE(b.ctrl.branch_type == 1);
}