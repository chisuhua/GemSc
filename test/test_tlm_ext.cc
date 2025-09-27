// test_tlm_exts.cpp
#include <gtest/gtest.h>
#include <tlm>
#include <sstream>
#include "mem_exts.hh"

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
TEST(TLMExtensionTest, ReadCmdExt_Basic) {
    ReadCmd cmd(0x1000, 64, 0x01);
    ReadCmdExt ext(cmd);

    // 检查数据是否正确
    EXPECT_EQ(ext.data.addr, 0x1000);
    EXPECT_EQ(ext.data.size, 64);
    EXPECT_EQ(ext.data.flags, 0x01);

    // 检查打印输出（部分匹配）
    std::string output = capture_print(ext);
    EXPECT_NE(output.find("ReadCmdExt = ReadCmd{addr=0x1000"), std::string::npos);
}

TEST(TLMExtensionTest, ReadCmdExt_CloneCopy) {
    ReadCmd cmd(0x2000, 128, 0x02);
    ReadCmdExt ext1(cmd);

    // clone
    auto* ext2 = dynamic_cast<ReadCmdExt*>(ext1.clone());
    ASSERT_NE(ext2, nullptr);
    EXPECT_EQ(ext2->data.addr, 0x2000);
    EXPECT_EQ(ext2->data.size, 128);

    // copy_from
    ReadCmdExt ext3;
    ext3.copy_from(*ext2);
    EXPECT_EQ(ext3.data.addr, 0x2000);
    EXPECT_EQ(ext3.data.size, 128);

    delete ext2; // 手动清理 clone 出来的对象
}

TEST(TLMExtensionTest, ReadCmdExt_AttachToPayload) {
    tlm::tlm_generic_payload payload;
    ReadCmd cmd(0x3000, 256, 0x04);
    payload.set_extension(new ReadCmdExt(cmd));

    auto* ext = payload.get_extension<ReadCmdExt>();
    ASSERT_NE(ext, nullptr);
    EXPECT_EQ(ext->data.addr, 0x3000);

    // payload 析构时会自动 delete 扩展
}

// ========================
// 测试 WriteDataExt
// ========================
TEST(TLMExtensionTest, WriteDataExt_Basic) {
    WriteData wd;
    wd.valid_bytes = 4;
    wd.data[0] = 0xAA; wd.data[1] = 0xBB; wd.data[2] = 0xCC; wd.data[3] = 0xDD;
    wd.strb[0] = 0x0F;

    WriteDataExt ext(wd);

    EXPECT_EQ(ext.data.valid_bytes, 4);
    EXPECT_EQ(ext.data.data[0], 0xAA);
    EXPECT_EQ(ext.data.strb[0], 0x0F);

    std::string output = capture_print(ext);
    EXPECT_NE(output.find("Write
