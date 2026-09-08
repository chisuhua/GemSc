// src/tlm/router_tlm.cc
// RouterTLM: 5 端口多跳路由器实现
// 功能描述：实现六阶段流水线 (BW→RC→VA→SA→ST→LT) 和 Credit-based Flow Control
// 作者 CppTLM Team / 日期 2026-04-23
#include "tlm/router_tlm.hh"
#include <algorithm>
#include <cassert>
#include "core/module_factory.hh"
#include "core/param_rules.hh"
#include "core/sim_core.hh"

namespace tlm {

    // ============================================================================
    // XYRouting 实现
    // ============================================================================

    unsigned XYRouting::computeRoute(unsigned src_port, uint32_t dst_node, unsigned node_x,
                                     unsigned node_y, unsigned mesh_x, unsigned mesh_y) {
        unsigned dst_x = nodeToX(dst_node, mesh_x);
        unsigned dst_y = nodeToY(dst_node, mesh_x);

        // XY 路由：首先在 X 方向移动，然后在 Y 方向移动
        if (dst_x > node_x) {
            return static_cast<unsigned>(RouterPort::EAST);
        } else if (dst_x < node_x) {
            return static_cast<unsigned>(RouterPort::WEST);
        } else if (dst_y > node_y) {
            return static_cast<unsigned>(RouterPort::NORTH);
        } else if (dst_y < node_y) {
            return static_cast<unsigned>(RouterPort::SOUTH);
        } else {
            // 到达目标，返回本地端口
            return static_cast<unsigned>(RouterPort::LOCAL);
        }
    }

    // ============================================================================
    // RouterTLM 构造函数
    // ============================================================================

    RouterTLM::RouterTLM(const std::string& name, EventQueue* eq, unsigned node_x, unsigned node_y,
                         unsigned mesh_x, unsigned mesh_y)
        : ChStreamModuleBase(name, eq), node_x_(node_x), node_y_(node_y), mesh_x_(mesh_x),
          mesh_y_(mesh_y), stat_group_("router"),
          stats_flits_forwarded_(
              stat_group_.addScalar("flits_forwarded", "Total flits forwarded", "flits")),
          stats_packets_forwarded_(
              stat_group_.addScalar("packets_forwarded", "Total packets forwarded", "packets")),
          stats_total_hops_(stat_group_.addScalar("total_hops", "Total hop count", "hops")),
          stats_latency_(
              stat_group_.addDistribution("latency", "Packet latency distribution", "cycle")) {
        routing_algo_ = std::make_unique<XYRouting>();

        for (unsigned p = 0; p < NUM_PORTS; ++p) {
            for (unsigned v = 0; v < NUM_VCS; ++v) {
                downstream_credits_[p][v] = BUFFER_DEPTH;
                last_credit_return_cycle_[p][v] = 0;
            }
        }
    }

    void RouterTLM::on_config_loaded() {
        const auto& cfg = get_config();
        if (cfg.contains("node_x")) {
            node_x_ = cfg["node_x"].get<unsigned>();
        }
        if (cfg.contains("node_y")) {
            node_y_ = cfg["node_y"].get<unsigned>();
        }
        if (cfg.contains("mesh_x")) {
            mesh_x_ = cfg["mesh_x"].get<unsigned>();
        }
        if (cfg.contains("mesh_y")) {
            mesh_y_ = cfg["mesh_y"].get<unsigned>();
        }
        if (cfg.contains("credit_timeout")) {
            credit_timeout_ = cfg["credit_timeout"].get<uint64_t>();
        }
        DPRINTF(MODULE, "[CONFIG] RouterTLM %s: node=(%u,%u) mesh=(%u,%u) credit_timeout=%u\n",
                name.c_str(), node_x_, node_y_, mesh_x_, mesh_y_, credit_timeout_);
    }

    // ============================================================================
    // ChStreamModuleBase 接口
    // ============================================================================

    void RouterTLM::set_stream_adapter(cpptlm::StreamAdapterBase* adapter) {
        adapter_ = static_cast<PortAdapter*>(adapter);
    }

    // ============================================================================
    // 路由算法设置
    // ============================================================================

    void RouterTLM::set_routing_algorithm(std::unique_ptr<RoutingAlgorithm> algo) {
        routing_algo_ = std::move(algo);
    }

    // ============================================================================
    // 六阶段流水线 tick()
    // ============================================================================

    void RouterTLM::tick() {
        current_cycle_ = getCurrentCycle();

        // 阶段 1: Buffer Write (BW)
        stage_buffer_write();

        // 阶段 2: Route Computation (RC)
        stage_route_computation();

        // 阶段 3: VC Allocation (VA)
        stage_vc_allocation();

        // 阶段 4: Switch Allocation (SA)
        stage_switch_allocation();

        // 阶段 5: Switch Traversal (ST)
        stage_switch_traversal();

        // 阶段 6: Link Traversal (LT) - 精确 credit 消耗在这里
        stage_link_traversal();

        // Credit 安全网：仅在启用超时检测且检测到死锁超时时触发
        if (credit_timeout_ > 0) {
            credit_safety_reset();
        }

        if (adapter_)
            adapter_->tick();
    }

    // ============================================================================
    // 阶段 1: Buffer Write
    // ============================================================================

    void RouterTLM::stage_buffer_write() {
        for (unsigned port = 0; port < NUM_PORTS; ++port) {
            auto& req_adapter = req_in_[port];
            if (req_adapter.valid()) {
                // 从 bundle 读取 VC ID
                unsigned vc = req_adapter.data().vc_id.read();
                if (vc >= NUM_VCS)
                    vc = 0; // 边界保护

                // 读取 flit
                RouterFlit flit(req_adapter.data(), port, vc, current_cycle_);
                req_adapter.consume();

                // 检查缓冲区空间
                if (input_buffer_[port][vc].size() < BUFFER_DEPTH) {
                    input_buffer_[port][vc].push(flit);
                }
            }
        }
    }

    // ============================================================================
    // 阶段 2: Route Computation
    // ============================================================================

    void RouterTLM::stage_route_computation() {
        for (unsigned port = 0; port < NUM_PORTS; ++port) {
            for (unsigned vc = 0; vc < NUM_VCS; ++vc) {
                auto& buf = input_buffer_[port][vc];
                if (buf.empty())
                    continue;

                RouterFlit& flit = buf.front();
                auto& stage = flit.stage;

                if (stage.active)
                    continue; // 已在流水线中

                // HEAD flit 需要路由计算
                if (flit.bundle.is_head()) {
                    unsigned out_port = routing_algo_->computeRoute(
                        port, flit.bundle.dst_node.read(), node_x_, node_y_, mesh_x_, mesh_y_);

                    // 更新流水线状态
                    stage.active = true;
                    stage.out_port = out_port;
                    stage.out_vc = vc;
                    stage.packet_id = flit.bundle.transaction_id.read();

                    // 记录到路由表
                    RoutingEntry entry{out_port, vc, true};
                    routing_table_[stage.packet_id] = entry;
                } else {
                    // BODY/TAIL flit: 查表获取已计算的路由
                    uint64_t pkt_id = flit.bundle.transaction_id.read();
                    auto it = routing_table_.find(pkt_id);
                    if (it != routing_table_.end() && it->second.valid) {
                        stage.active = true;
                        stage.out_port = it->second.out_port;
                        stage.out_vc = it->second.out_vc;
                        stage.packet_id = pkt_id;
                    }
                }
            }
        }
    }

    // ============================================================================
    // 阶段 3: VC Allocation
    // ============================================================================

    void RouterTLM::stage_vc_allocation() {
        for (unsigned port = 0; port < NUM_PORTS; ++port) {
            for (unsigned vc = 0; vc < NUM_VCS; ++vc) {
                auto& buf = input_buffer_[port][vc];
                if (buf.empty())
                    continue;

                RouterFlit& flit = buf.front();
                if (!flit.stage.active)
                    continue;
                if (flit.stage.vc_allocated)
                    continue; // 跳过已分配 VC 的 flit

                unsigned out_port = flit.stage.out_port;
                unsigned out_vc = allocate_vc(out_port);

                if (out_vc < NUM_VCS) {
                    if (!has_credit(out_port, out_vc)) {
                        release_vc(out_port, out_vc);
                        continue;
                    }
                    flit.stage.out_vc = out_vc;
                    flit.stage.vc_allocated = true;
                }
            }
        }
    }

    // ============================================================================
    // 阶段 4: Switch Allocation
    // ============================================================================

    void RouterTLM::stage_switch_allocation() {
        // 清零仲裁结果
        sa_winners_.clear();

        // 遍历所有输出端口，每个端口最多选中一个 flit
        for (unsigned out_port = 0; out_port < NUM_PORTS; ++out_port) {
            // 遍历所有输入端口，寻找目标为 out_port 且 VA 已完成的 flit
            for (unsigned in_port = 0; in_port < NUM_PORTS; ++in_port) {
                for (unsigned in_vc = 0; in_vc < NUM_VCS; ++in_vc) {
                    auto& buf = input_buffer_[in_port][in_vc];
                    if (buf.empty())
                        continue;

                    RouterFlit& flit = buf.front();
                    if (!flit.stage.active)
                        continue;
                    if (!flit.stage.vc_allocated)
                        continue; // VA 还未分配 VC，跳过
                    if (flit.stage.out_port != out_port)
                        continue;

                    // 使用 VA 阶段已分配的 out_vc
                    unsigned out_vc = flit.stage.out_vc;

                    // 检查下游是否有 credit
                    if (!has_credit(out_port, out_vc))
                        continue;

                    // 选中此 flit，加入 winners 列表
                    sa_winners_.push_back({in_port, in_vc, out_port, out_vc});
                    break; // 此 out_port 已选中 flit，跳到下一个 out_port
                }
                // 检查是否已为这个 out_port 选中了 winner
                bool found = false;
                for (const auto& w : sa_winners_) {
                    if (w.out_port == out_port) {
                        found = true;
                        break;
                    }
                }
                if (found)
                    break; // 已经为这个 out_port 选中了 flit，跳到下一个 out_port
            }
        }
    }

    // ============================================================================
    // 阶段 5: Switch Traversal
    // ============================================================================

    void RouterTLM::stage_switch_traversal() {
        // P0.1: 首先处理等待 credit 的 flit (从上一周期遗留)
        for (unsigned out_port = 0; out_port < NUM_PORTS; ++out_port) {
            for (unsigned out_vc = 0; out_vc < NUM_VCS; ++out_vc) {
                RouterFlit& waiting_flit = pipe_reg_[out_port][out_vc];
                if (!waiting_flit.stage.waiting_for_credit)
                    continue;

                // 检查下游是否有 credit
                if (!has_credit(out_port, out_vc))
                    continue; // 仍然没有 credit，继续等待

                // 下游有 credit，推进 flit 到 pending_link_
                waiting_flit.stage.waiting_for_credit = false;

                // 消耗下游 credit
                consume_credit(out_port, out_vc);

                // 更新 hop 计数
                waiting_flit.bundle.hops.write(waiting_flit.bundle.hops.read() + 1);
                waiting_flit.bundle.src_port.write(out_port);

                // 写入 pending_link_ 队列
                pending_link_.push({waiting_flit.bundle, out_port, out_vc, waiting_flit.in_port,
                                    waiting_flit.in_vc, waiting_flit.cycle_received});

                // 更新统计
                ++stats_flits_forwarded_;
                stats_total_hops_ += waiting_flit.bundle.hops.read();
                stats_latency_.sample(current_cycle_ - waiting_flit.cycle_received);

                // 触发 credit 返回到上游
                return_credit_to_upstream(waiting_flit.in_port, waiting_flit.in_vc);

                // TAIL flit: 释放 VC，更新 packet 统计
                if (waiting_flit.bundle.is_tail()) {
                    release_vc(out_port, out_vc);
                    ++stats_packets_forwarded_;
                    routing_table_.erase(waiting_flit.bundle.transaction_id.read());
                }

                // 清理 waiting flit
                waiting_flit = RouterFlit();
            }
        }

        // 处理新的 SA winners
        if (sa_winners_.empty())
            return;

        for (const auto& winner : sa_winners_) {
            auto& buf = input_buffer_[winner.in_port][winner.in_vc];
            if (buf.empty())
                continue;

            RouterFlit flit = buf.front();

            unsigned out_port = winner.out_port;
            unsigned out_vc = winner.out_vc;

            // P0.1: 检查下游是否有 credit
            if (!has_credit(out_port, out_vc)) {
                // 下游没有 credit，flit 保持在 pipe_reg_ 中等待
                flit.stage.waiting_for_credit = true;
                pipe_reg_[out_port][out_vc] = flit;
                buf.pop();

                // 仍然需要释放输入 VC 的占用（让上游可以继续发）
                return_credit_to_upstream(winner.in_port, winner.in_vc);
                continue;
            }

            // 下游有 credit，正常处理
            buf.pop();

            // 消耗下游 credit
            consume_credit(out_port, out_vc);

            // 更新 hop 计数
            flit.bundle.hops.write(flit.bundle.hops.read() + 1);
            flit.bundle.src_port.write(out_port);

            // 写入 pending_link_ 队列
            pending_link_.push(
                {flit.bundle, out_port, out_vc, flit.in_port, flit.in_vc, flit.cycle_received});

            // 更新统计
            ++stats_flits_forwarded_;
            stats_total_hops_ += flit.bundle.hops.read();
            stats_latency_.sample(current_cycle_ - flit.cycle_received);

            // 触发 credit 返回到上游
            return_credit_to_upstream(winner.in_port, winner.in_vc);

            // TAIL flit: 释放 VC，更新 packet 统计
            if (flit.bundle.is_tail()) {
                release_vc(out_port, out_vc);
                ++stats_packets_forwarded_;
                routing_table_.erase(flit.bundle.transaction_id.read());
            }
        }
    }

    // ============================================================================
    // 阶段 6: Link Traversal
    // ============================================================================

    void RouterTLM::stage_link_traversal() {
        // 处理 flit 发送
        if (!pending_link_.empty()) {
            auto pf = pending_link_.front();
            resp_out_[pf.out_port].write(pf.bundle);
            pending_link_.pop();
        }

        // 处理 credit 返回延迟队列
        std::queue<PendingCreditReturn> next_queue;
        while (!pending_credit_returns_.empty()) {
            auto cr = pending_credit_returns_.front();
            pending_credit_returns_.pop();

            cr.remaining_cycles--;
            if (cr.remaining_cycles == 0) {
                // 链路延迟结束，发送 credit 到上游
                send_credit_to_upstream(cr.port, cr.vc);
            } else {
                next_queue.push(cr);
            }
        }
        pending_credit_returns_ = std::move(next_queue);
    }

    // ============================================================================
    // VC 分配
    // ============================================================================

    unsigned RouterTLM::allocate_vc(unsigned out_port) {
        for (unsigned vc = 0; vc < NUM_VCS; ++vc) {
            if (!vc_state_[out_port][vc].allocated) {
                vc_state_[out_port][vc].allocated = true;
                return vc;
            }
        }
        return NUM_VCS; // 无可用 VC
    }

    void RouterTLM::release_vc(unsigned out_port, unsigned vc) {
        if (vc < NUM_VCS) {
            vc_state_[out_port][vc].allocated = false;
        }
    }

    // ============================================================================
    // Credit-based Flow Control
    // ============================================================================

    bool RouterTLM::has_credit(unsigned out_port, unsigned vc) const {
        if (out_port >= NUM_PORTS || vc >= NUM_VCS)
            return false;
        return downstream_credits_[out_port][vc] > 0;
    }

    void RouterTLM::consume_credit(unsigned out_port, unsigned vc) {
        if (out_port >= NUM_PORTS || vc >= NUM_VCS)
            return;
        if (downstream_credits_[out_port][vc] > 0) {
            downstream_credits_[out_port][vc]--;
        }
    }

    void RouterTLM::receive_credit(unsigned in_port, unsigned vc) {
        if (in_port >= NUM_PORTS || vc >= NUM_VCS)
            return;
        if (downstream_credits_[in_port][vc] < BUFFER_DEPTH) {
            downstream_credits_[in_port][vc]++;
        }
    }

    // ============================================================================
    // Credit 返回机制
    // ============================================================================

    void RouterTLM::return_credit_to_upstream(unsigned in_port, unsigned vc) {
        unsigned reverse_p = reverse_port(in_port);

        // 创建 Credit 返回延迟事件（1 周期链路延迟）
        PendingCreditReturn cr;
        cr.port = reverse_p;
        cr.vc = vc;
        cr.remaining_cycles = 1;
        pending_credit_returns_.push(cr);
    }

    void RouterTLM::send_credit_to_upstream(unsigned reverse_port, unsigned vc) {
        // 通过 resp_out_[reverse_port] 发送 Credit 信号
        // 上游路由器会通过其 resp_in 收到此信号并调用 receive_credit()
        DPRINTF(MODULE, "[CREDIT RETURN] send credit to reverse_port=%u vc=%u\n", reverse_port, vc);
        // 通过 BidirectionalPortAdapter 发送 Credit 信号（带链路延迟）
        send_credit_signal(reverse_port, vc);
    }

    void RouterTLM::send_credit_signal(unsigned port, unsigned vc) {
        if (!adapter_) {
            DPRINTF(MODULE, "[CREDIT ERROR] send_credit_signal called but adapter_ is null\n");
            return;
        }
        // 通过 adapter 发送 Credit（adapter 内部会处理 1 周期延迟）
        adapter_->send_credit(port, vc);
        DPRINTF(MODULE, "[CREDIT SIGNAL] sent credit to port=%u vc=%u via adapter\n", port, vc);
    }

    // Credit 超时检测：仅当 credit_timeout_ > 0 时启用，用于死锁恢复
    void RouterTLM::credit_safety_reset() {
        if (credit_timeout_ == 0)
            return;

        for (unsigned p = 0; p < NUM_PORTS; ++p) {
            for (unsigned v = 0; v < NUM_VCS; ++v) {
                if (downstream_credits_[p][v] < BUFFER_DEPTH) {
                    uint64_t elapsed = current_cycle_ - last_credit_return_cycle_[p][v];
                    if (elapsed >= credit_timeout_) {
                        DPRINTF(MODULE, "[CREDIT Timeout] port=%u vc=%u elapsed=%u, reset\n", p, v,
                                elapsed);
                        downstream_credits_[p][v] = BUFFER_DEPTH;
                        last_credit_return_cycle_[p][v] = current_cycle_;
                    }
                } else {
                    last_credit_return_cycle_[p][v] = current_cycle_;
                }
            }
        }
    }

    // ============================================================================
    // XY 路由便捷方法
    // ============================================================================

    unsigned RouterTLM::compute_xy_route(uint32_t dst_node) {
        return routing_algo_->computeRoute(0, // src_port 仅用于边界情况
                                           dst_node, node_x_, node_y_, mesh_x_, mesh_y_);
    }

    // RouterTLM registration is handled by REGISTER_CHSTREAM macro in chstream_register.hh

    cpptlm::ParamRules RouterTLM::get_param_rules() {
        return {
            {"node_x", cpptlm::ParamRule{"node_x", cpptlm::ParamType::INTEGER, true, std::nullopt,
                                         std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                                         std::nullopt, std::nullopt}},
            {"node_y", cpptlm::ParamRule{"node_y", cpptlm::ParamType::INTEGER, true, std::nullopt,
                                         std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                                         std::nullopt, std::nullopt}},
            {"mesh_x", cpptlm::ParamRule{"mesh_x", cpptlm::ParamType::INTEGER, true, std::nullopt,
                                         std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                                         std::nullopt, std::nullopt}},
            {"mesh_y", cpptlm::ParamRule{"mesh_y", cpptlm::ParamType::INTEGER, true, std::nullopt,
                                         std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                                         std::nullopt, std::nullopt}},
            {"flit_width",
             cpptlm::ParamRule{"flit_width", cpptlm::ParamType::INTEGER, false, 64, std::nullopt,
                               std::nullopt, std::nullopt, 64, 128, std::nullopt}},
            {"vc_count",
             cpptlm::ParamRule{"vc_count", cpptlm::ParamType::INTEGER, false, 2, std::nullopt,
                               std::nullopt, std::nullopt, 1, 8, std::nullopt}},
            {"buffer_size",
             cpptlm::ParamRule{"buffer_size", cpptlm::ParamType::INTEGER, false, 16, std::nullopt,
                               std::nullopt, std::nullopt, 1, 64, std::nullopt}},
        };
    }

} // namespace tlm
