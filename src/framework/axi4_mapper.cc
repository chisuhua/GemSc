// src/framework/axi4_mapper.cc
// Axi4Mapper 实现（outstanding 跟踪 + OOO completion）
// 功能描述：Axi4Mapper 独立模块实现。读写独立 ID 空间跟踪（awid/arid），
//           容量上限 N（N+1 拒绝），完成释放槽位，rid 关联乱序 rdata 回原事务。
// 作者 CppTLM Team / 日期 2026-12-22
// 参考: openspec/changes/2026-12-22-cpptlm-dgpu-axi4-mapper/spec.md
//       openspec/changes/2026-09-01-cpptlm-dgpu-pcie-ip-microarch/design.md §6.4

#include "framework/axi4_mapper.hh"

#include <algorithm>

namespace cpptlm {

    Axi4Mapper::Axi4Mapper(std::size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            capacity_ = 1; // 至少 1，避免除零/空容量语义歧义
        }
    }

    bool Axi4Mapper::issue_write(const bundles::Axi4Bundle& req) {
        if (!can_issue_write()) {
            return false; // 容量满（N+1 拒绝）
        }
        outstanding_wr_ids_.push_back(static_cast<uint16_t>(req.awid.read()));
        return true;
    }

    bool Axi4Mapper::issue_read(const bundles::Axi4Bundle& req) {
        if (!can_issue_read()) {
            return false; // 容量满（N+1 拒绝）
        }
        const uint16_t arid = static_cast<uint16_t>(req.arid.read());
        outstanding_rd_ids_.push_back(arid);
        // 存储原读事务，供 OOO 完成时 rid 关联回原事务（araddr 等字段）
        pending_reads_[arid] = req;
        return true;
    }

    bool Axi4Mapper::can_issue_write() const {
        return outstanding_wr_ids_.size() < capacity_;
    }

    bool Axi4Mapper::can_issue_read() const {
        return outstanding_rd_ids_.size() < capacity_;
    }

    std::size_t Axi4Mapper::outstanding_wr() const {
        return outstanding_wr_ids_.size();
    }

    std::size_t Axi4Mapper::outstanding_rd() const {
        return outstanding_rd_ids_.size();
    }

    std::size_t Axi4Mapper::capacity() const {
        return capacity_;
    }

    bool Axi4Mapper::complete_write(uint16_t bid) {
        auto it = std::find(outstanding_wr_ids_.begin(), outstanding_wr_ids_.end(), bid);
        if (it == outstanding_wr_ids_.end()) {
            return false; // 不匹配：不消耗任何 outstanding
        }
        outstanding_wr_ids_.erase(it);
        return true;
    }

    bool Axi4Mapper::complete_read(uint16_t rid, uint64_t rdata, bool rlast) {
        auto it = std::find(outstanding_rd_ids_.begin(), outstanding_rd_ids_.end(), rid);
        if (it == outstanding_rd_ids_.end()) {
            return false; // 不匹配：不消耗任何 outstanding
        }

        // 将 rdata 关联回原事务（OOO completion 核心：rid → 原读事务）
        auto pit = pending_reads_.find(rid);
        if (pit != pending_reads_.end()) {
            pit->second.rdata.write(rdata);
        }
        read_data_[rid] = rdata; // 暂存已关联的 rdata（测试断言）

        if (rlast) {
            // 末拍：释放 outstanding 槽位
            outstanding_rd_ids_.erase(it);
            pending_reads_.erase(rid);
        }
        return true;
    }

    bool Axi4Mapper::has_read_data(uint16_t rid) const {
        return read_data_.find(rid) != read_data_.end();
    }

    uint64_t Axi4Mapper::read_data(uint16_t rid) const {
        auto it = read_data_.find(rid);
        if (it == read_data_.end()) {
            return 0;
        }
        return it->second;
    }

    const bundles::Axi4Bundle* Axi4Mapper::pending_read(uint16_t arid) const {
        auto it = pending_reads_.find(arid);
        if (it == pending_reads_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    void Axi4Mapper::reset() {
        outstanding_wr_ids_.clear();
        outstanding_rd_ids_.clear();
        pending_reads_.clear();
        read_data_.clear();
    }

} // namespace cpptlm
