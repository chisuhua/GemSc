// test/test_sm_bundle_wiring.cc
// Task 17 L2: SM 8 种 Bundle 字段 + 流向验证 (per architecture/15 §15.8.1 L2)
// 作者 CppTLM Team / 日期 2027-02-09
#include "bundles/sm_bundles_tlm.hh"
#include "catch_amalgamated.hpp"
#include "tlm/gpu/instruction_descriptor.hh"

using namespace bundles::sm;
using namespace cpptlm::gpu;

TEST_CASE("FetchToIssueBundle: 字段 POD 正确性", "[sm-bundle][sm-l2]") {
    FetchToIssueBundle b{};
    b.pc = 0x1000;
    b.warp_id = 7;
    b.instr_desc.instr_id = 42;
    REQUIRE(b.pc == 0x1000);
    REQUIRE(b.warp_id == 7);
    REQUIRE(b.instr_desc.instr_id == 42);
}

TEST_CASE("DecodeToIssueBundle: 继承自 FetchToIssueBundle", "[sm-bundle][sm-l2]") {
    DecodeToIssueBundle b{};
    b.pc = 0x2000;
    b.pipe = PipeClass::kVectorALU;
    b.latency_class = LatencyClass::kFixed4Cycle;
    REQUIRE(b.pc == 0x2000);
    REQUIRE(b.pipe == PipeClass::kVectorALU);
    REQUIRE(b.latency_class == LatencyClass::kFixed4Cycle);
}

TEST_CASE("IssueToExecBundle: 继承链 + src_values 字段", "[sm-bundle][sm-l2]") {
    IssueToExecBundle b{};
    b.pc = 0x3000;
    b.src_values[0] = 42;
    b.src_values[1] = 100;
    b.src_valid[0] = true;
    REQUIRE(b.pc == 0x3000);
    REQUIRE(b.src_values[0] == 42);
    REQUIRE(b.src_values[1] == 100);
    REQUIRE(b.src_valid[0]);
}

TEST_CASE("ExecToWritebackBundle: 继承链 + result_value 字段", "[sm-bundle][sm-l2]") {
    ExecToWritebackBundle b{};
    b.pc = 0x4000;
    for (int i = 0; i < 2; ++i)
        b.result_value[i] = i * 10;
    b.result_num = 2;
    b.exec_cycles = 4;
    REQUIRE(b.pc == 0x4000);
    REQUIRE(b.result_value[0] == 0);
    REQUIRE(b.result_value[1] == 10);
    REQUIRE(b.result_num == 2);
    REQUIRE(b.exec_cycles == 4);
}

TEST_CASE("WritebackToRegFileBundle: dst_regs + values 字段", "[sm-bundle][sm-l2]") {
    WritebackToRegFileBundle b{};
    b.warp_id = 5;
    b.dst_regs[0] = 7;
    b.dst_regs[1] = 13;
    b.values[0] = 0xCAFE;
    b.is_accvgpr = true;
    REQUIRE(b.warp_id == 5);
    REQUIRE(b.dst_regs[0] == 7);
    REQUIRE(b.dst_regs[1] == 13);
    REQUIRE(b.values[0] == 0xCAFE);
    REQUIRE(b.is_accvgpr);
}

TEST_CASE("MemoryReqBundle: 异步内存请求字段", "[sm-bundle][sm-l2]") {
    MemoryReqBundle b{};
    b.vaddr = 0x100000;
    b.size = 64;
    b.is_write = true;
    b.sm_id = 0;
    b.wave_id = 3;
    REQUIRE(b.vaddr == 0x100000);
    REQUIRE(b.size == 64);
    REQUIRE(b.is_write);
    REQUIRE(b.sm_id == 0);
    REQUIRE(b.wave_id == 3);
}

TEST_CASE("MemoryRespBundle: tag + cycles for HazardTracker", "[sm-bundle][sm-l2]") {
    MemoryRespBundle b{};
    b.tag = 0xDEADBEEF;
    b.data = 0xCAFEBABE;
    b.cycles = 200;
    b.is_hit = false;
    REQUIRE(b.tag == 0xDEADBEEF);
    REQUIRE(b.data == 0xCAFEBABE);
    REQUIRE(b.cycles == 200);
    REQUIRE(!b.is_hit);
}

TEST_CASE("ScoreboardQueryBundle: HazardTracker 查询字段", "[sm-bundle][sm-l2]") {
    ScoreboardQueryBundle b{};
    b.query_type = ScoreboardQueryBundle::QueryType::kDecrement;
    b.warp_id = 13;
    b.sm_id = 0;
    REQUIRE(b.query_type == ScoreboardQueryBundle::QueryType::kDecrement);
    REQUIRE(b.warp_id == 13);
    REQUIRE(b.sm_id == 0);
}

TEST_CASE("8 Bundle 流向正确性: Fetch→Decode→Issue→Exec→Writeback", "[sm-bundle][sm-l2]") {
    FetchToIssueBundle f{};
    f.pc = 100;
    DecodeToIssueBundle d{};
    d.pc = f.pc;
    REQUIRE(d.pc == f.pc);
    IssueToExecBundle i{};
    i.pc = d.pc;
    REQUIRE(i.pc == d.pc);
    ExecToWritebackBundle e{};
    e.pc = i.pc;
    REQUIRE(e.pc == i.pc);
    WritebackToRegFileBundle w{};
    w.warp_id = 0;
    REQUIRE(e.pc == 100);
}

TEST_CASE("MemoryReq/Resp 配对: 同 tag", "[sm-bundle][sm-l2]") {
    MemoryReqBundle req{};
    req.tag = 0xABCD;
    MemoryRespBundle resp{};
    resp.tag = req.tag;
    REQUIRE(req.tag == resp.tag);
}
