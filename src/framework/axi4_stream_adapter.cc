// src/framework/axi4_stream_adapter.cc
// AXI4 / AXI4-Lite Stream Adapter 实现（3 端口 valid/ready 握手）
// 功能描述：Axi4StreamAdapter 三端口数据路径实现，支持 backpressure 不丢事务
//          与 outstanding 请求 ID 关联（awid→bid / arid→rid）。
//
// 握手模型：
//   - 模块产出通道（master_req / slave_resp / cfg_resp）：
//       tick() 时 valid && ready=1 → 转移到对端（valid 清除，数据不再持有）。
//       ready=0 → valid 保持（backpressure 不丢事务）。
//   - 模块消费通道（master_resp / slave_req / cfg_req）：
//       valid 保持直到模块调用对应 consume() 读取。
// 作者 CppTLM Team / 日期 2026-11-03
// 参考: openspec/changes/2026-11-03-cpptlm-dgpu-axi-stream-adapter/spec.md

#include "framework/axi4_stream_adapter.hh"

#include <algorithm>

namespace cpptlm {

    // ===================== axi_master_out（EP → SoC，模块产出）=====================

    bool Axi4StreamAdapter::master_req(const bundles::Axi4Bundle& req, bool track_id) {
        if (master_req_valid_) {
            return false; // 上一请求未转移（backpressure），拒绝覆盖
        }
        master_req_data_ = req;
        master_req_valid_ = true;

        // 登记 outstanding 请求 ID：写请求以 awid 登记，读请求以 arid 登记
        // （读写独立 ID 空间，per design.md §6.3）
        if (!track_id) {
            return true; // burst 后续拍：不重复登记（整体作为一个事务）
        }
        if (req.awid.read() != 0 || req.awaddr.read() != 0 || req.awlen.read() != 0) {
            outstanding_wr_ids_.push_back(static_cast<uint16_t>(req.awid.read()));
        } else {
            outstanding_rd_ids_.push_back(static_cast<uint16_t>(req.arid.read()));
        }
        return true;
    }

    void Axi4StreamAdapter::set_master_ready(bool ready) {
        master_ready_ = ready;
    }

    bool Axi4StreamAdapter::master_ready() const {
        return master_ready_;
    }

    bool Axi4StreamAdapter::master_req_valid() const {
        return master_req_valid_;
    }

    const bundles::Axi4Bundle& Axi4StreamAdapter::master_req_data() const {
        return master_req_data_;
    }

    void Axi4StreamAdapter::master_req_consume() {
        master_req_valid_ = false;
    }

    bool Axi4StreamAdapter::master_resp(const bundles::Axi4Bundle& resp) {
        if (master_resp_valid_) {
            return false; // 上一响应未消费
        }
        master_resp_data_ = resp;
        master_resp_valid_ = true;

        // 按响应 ID 匹配并移除 outstanding 请求：
        //   读响应 → rid 匹配 arid，仅当 RLAST（多拍 burst 中途不清，条目存活到末拍）
        //   写响应 → bid 匹配 awid
        const uint16_t rid = static_cast<uint16_t>(resp.rid.read());
        const uint16_t bid = static_cast<uint16_t>(resp.bid.read());
        if (resp.rlast.read()) {
            auto it = std::find(outstanding_rd_ids_.begin(), outstanding_rd_ids_.end(), rid);
            if (it != outstanding_rd_ids_.end()) {
                outstanding_rd_ids_.erase(it);
            }
        }
        if (bid != 0) {
            auto it = std::find(outstanding_wr_ids_.begin(), outstanding_wr_ids_.end(), bid);
            if (it != outstanding_wr_ids_.end()) {
                outstanding_wr_ids_.erase(it);
            }
        }
        return true;
    }

    bool Axi4StreamAdapter::master_resp_valid() const {
        return master_resp_valid_;
    }

    const bundles::Axi4Bundle& Axi4StreamAdapter::master_resp_data() const {
        return master_resp_data_;
    }

    void Axi4StreamAdapter::master_resp_consume() {
        master_resp_valid_ = false;
    }

    // ===================== axi_slave_in（SoC → EP，模块消费）=====================

    bool Axi4StreamAdapter::slave_req(const bundles::Axi4Bundle& req) {
        if (slave_req_valid_) {
            return false; // 上一请求未消费（backpressure），拒绝覆盖
        }
        slave_req_data_ = req;
        slave_req_valid_ = true;
        return true;
    }

    void Axi4StreamAdapter::set_slave_ready(bool ready) {
        slave_ready_ = ready;
    }

    bool Axi4StreamAdapter::slave_ready() const {
        return slave_ready_;
    }

    bool Axi4StreamAdapter::slave_req_valid() const {
        return slave_req_valid_;
    }

    void Axi4StreamAdapter::slave_req_consume() {
        slave_req_valid_ = false;
    }

    const bundles::Axi4Bundle& Axi4StreamAdapter::slave_req_data() const {
        return slave_req_data_;
    }

    bool Axi4StreamAdapter::slave_resp(const bundles::Axi4Bundle& resp) {
        if (slave_resp_valid_) {
            return false;
        }
        slave_resp_data_ = resp;
        slave_resp_valid_ = true;
        return true;
    }

    bool Axi4StreamAdapter::slave_resp_valid() const {
        return slave_resp_valid_;
    }

    void Axi4StreamAdapter::slave_resp_consume() {
        slave_resp_valid_ = false;
    }

    const bundles::Axi4Bundle& Axi4StreamAdapter::slave_resp_data() const {
        return slave_resp_data_;
    }

    // ===================== cfg_slave_in（AXI4-Lite 配置，模块消费）=====================

    bool Axi4StreamAdapter::cfg_req(const bundles::Axi4LiteBundle& req) {
        if (cfg_req_valid_) {
            return false; // 上一请求未消费（backpressure），拒绝覆盖
        }
        cfg_req_data_ = req;
        cfg_req_valid_ = true;
        return true;
    }

    void Axi4StreamAdapter::set_cfg_ready(bool ready) {
        cfg_ready_ = ready;
    }

    bool Axi4StreamAdapter::cfg_ready() const {
        return cfg_ready_;
    }

    bool Axi4StreamAdapter::cfg_req_valid() const {
        return cfg_req_valid_;
    }

    void Axi4StreamAdapter::cfg_req_consume() {
        cfg_req_valid_ = false;
    }

    const bundles::Axi4LiteBundle& Axi4StreamAdapter::cfg_req_data() const {
        return cfg_req_data_;
    }

    bool Axi4StreamAdapter::cfg_resp(const bundles::Axi4LiteBundle& resp) {
        if (cfg_resp_valid_) {
            return false;
        }
        cfg_resp_data_ = resp;
        cfg_resp_valid_ = true;
        return true;
    }

    bool Axi4StreamAdapter::cfg_resp_valid() const {
        return cfg_resp_valid_;
    }

    void Axi4StreamAdapter::cfg_resp_consume() {
        cfg_resp_valid_ = false;
    }

    const bundles::Axi4LiteBundle& Axi4StreamAdapter::cfg_resp_data() const {
        return cfg_resp_data_;
    }

    // ===================== 周期推进（backpressure 握手）=====================

    void Axi4StreamAdapter::tick() {
        // master 请求（模块产出）：valid && ready=1 → 转移到 SoC（valid 清除）；
        //                          ready=0 → valid 保持，数据不丢（backpressure）。
        if (master_req_valid_ && master_ready_) {
            master_req_valid_ = false;
        }

        // slave 响应（模块产出）：valid && ready=1 → 转移到 SoC。
        if (slave_resp_valid_ && slave_ready_) {
            slave_resp_valid_ = false;
        }

        // cfg 响应（模块产出）：valid && ready=1 → 转移到 SoC。
        if (cfg_resp_valid_ && cfg_ready_) {
            cfg_resp_valid_ = false;
        }

        // slave/cfg 请求（模块消费）：valid 保持直到模块调 consume()，天然不丢。
    }

    void Axi4StreamAdapter::reset() {
        master_req_valid_ = false;
        master_resp_valid_ = false;
        slave_req_valid_ = false;
        slave_resp_valid_ = false;
        cfg_req_valid_ = false;
        cfg_resp_valid_ = false;
        master_ready_ = true;
        slave_ready_ = true;
        cfg_ready_ = true;
        outstanding_wr_ids_.clear();
        outstanding_rd_ids_.clear();
    }

} // namespace cpptlm
