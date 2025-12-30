// test_tlm_exts.cpp
#include "catch_amalgamated.hpp"
#include <tlm>
#include <sstream>
#include "ext/mem_exts.hh"

// 辅助函数：捕获 print() 输出
template<typename ExtType>
std::string capture_print(const ExtType& ext) {
    std::ostringstream oss;
    ext.print(oss);
    return oss.str();
}

// ========================
// 测试 ReadCmdExt
// ========================
TEST_CASE("TLM Extension Tests - ReadCmdExt", "[tlm][extension][read]") {
    SECTION("ReadCmdExt_Basic - Basic functionality") {
        ReadCmd cmd(0x1000, 64, 0x01);
        ReadCmdExt ext(cmd);

        // 检查数据是否正确
        REQUIRE(ext.data.addr == 0x1000);
        REQUIRE(ext.data.size == 64);
        REQUIRE(ext.data.flags == 0x01);

        // 检查打印输出（部分匹配）
        std::string output = capture_print(ext);
        REQUIRE(output.find("ReadCmdExt = ReadCmd{addr=0x1000") != std::string::npos);
    }

    SECTION("ReadCmdExt_CloneCopy - Clone and copy functionality") {
        ReadCmd cmd(0x2000, 128, 0x02);
        ReadCmdExt ext1(cmd);

        // clone
        auto* ext2 = dynamic_cast<ReadCmdExt*>(ext1.clone());
        REQUIRE(ext2 != nullptr);
        REQUIRE(ext2->data.addr == 0x2000);
        REQUIRE(ext2->data.size == 128);

        // copy_from
        ReadCmdExt ext3;
        ext3.copy_from(*ext2);
        REQUIRE(ext3.data.addr == 0x2000);
        REQUIRE(ext3.data.size == 128);

        delete ext2; // 手动清理 clone 出来的对象
    }

    SECTION("ReadCmdExt_AttachToPayload - Attach to payload functionality") {
        tlm::tlm_generic_payload payload;
        ReadCmd cmd(0x3000, 256, 0x04);
        payload.set_extension(new ReadCmdExt(cmd));

        auto* ext = payload.get_extension<ReadCmdExt>();
        REQUIRE(ext != nullptr);
        REQUIRE(ext->data.addr == 0x3000);

        // payload 析构时会自动 delete 扩展
    }
}

// ========================
// 测试 WriteDataExt
// ========================
TEST_CASE("TLM Extension Tests - WriteDataExt", "[tlm][extension][write]") {
    SECTION("WriteDataExt_Basic - Basic functionality") {
        WriteData wd;
        wd.valid_bytes = 4;
        wd.data[0] = 0xAA; wd.data[1] = 0xBB; wd.data[2] = 0xCC; wd.data[3] = 0xDD;
        wd.strb[0] = 0x0F;

        WriteDataExt ext(wd);

        REQUIRE(ext.data.valid_bytes == 4);
        REQUIRE(ext.data.data[0] == 0xAA);
        REQUIRE(ext.data.strb[0] == 0x0F);

        std::string output = capture_print(ext);
        REQUIRE(output.find("WriteDataExt") != std::string::npos);
    }

    SECTION("WriteDataExt_CloneCopy - Clone and copy functionality") {
        WriteData wd;
        wd.valid_bytes = 8;
        for(int i = 0; i < 8; i++) {
            wd.data[i] = i + 0x10;
        }
        wd.strb[0] = 0xFF;

        WriteDataExt ext1(wd);

        // clone
        auto* ext2 = dynamic_cast<WriteDataExt*>(ext1.clone());
        REQUIRE(ext2 != nullptr);
        REQUIRE(ext2->data.valid_bytes == 8);
        REQUIRE(ext2->data.data[0] == 0x10);
        REQUIRE(ext2->data.strb[0] == 0xFF);

        // copy_from
        WriteDataExt ext3;
        ext3.copy_from(*ext2);
        REQUIRE(ext3.data.valid_bytes == 8);
        REQUIRE(ext3.data.data[0] == 0x10);

        delete ext2; // 手动清理 clone 出来的对象
    }

    SECTION("WriteDataExt_AttachToPayload - Attach to payload functionality") {
        tlm::tlm_generic_payload payload;
        WriteData wd;
        wd.valid_bytes = 4;
        wd.data[0] = 0xDE; wd.data[1] = 0xAD; wd.data[2] = 0xBE; wd.data[3] = 0xEF;
        WriteDataExt ext(wd);
        
        payload.set_extension(new WriteDataExt(ext));

        auto* ext_ptr = payload.get_extension<WriteDataExt>();
        REQUIRE(ext_ptr != nullptr);
        REQUIRE(ext_ptr->data.data[0] == 0xDE);
        REQUIRE(ext_ptr->data.data[1] == 0xAD);
        REQUIRE(ext_ptr->data.data[2] == 0xBE);
        REQUIRE(ext_ptr->data.data[3] == 0xEF);

        // payload 析构时会自动 delete 扩展
    }
}