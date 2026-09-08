// src/tlm/coherent_xbar_tlm.cc
// CoherentXBarTLM 实现 (Phase 7.A/7.B: 骨架 + write-through 透传)
//
// 作者: CppTLM Team / 日期: 2026-06-19
#include "tlm/coherent_xbar_tlm.hh"
#include <algorithm> // P1: registerPeerCache dedup needs std::find_if
#include <stdexcept>
#include "core/packet.hh"
#include "core/packet_pool.hh" // PacketPool::acquire/release (Packet 不可拷贝)
#include "core/sim_core.hh"    // DPRINTF

namespace cpptlm {
    namespace tlm {

        CoherentXBarTLM::CoherentXBarTLM(const std::string& n, EventQueue* eq)
            : CrossbarTLM(n, eq) {
        }

        void CoherentXBarTLM::registerPeerCache(const std::string& cache_name,
                                                MasterPort* req_out) {
            if (!req_out) {
                throw std::runtime_error(
                    "CoherentXBarTLM::registerPeerCache: req_out is null for cache '" + cache_name +
                    "'");
            }
            // P1 幂等性: 同名 cache 不重复入队
            auto it = std::find_if(peer_cache_req_outs_.begin(), peer_cache_req_outs_.end(),
                                   [&](const auto& p) { return p.first == cache_name; });
            if (it != peer_cache_req_outs_.end()) {
                DPRINTF(MODULE, "[CoherentXBar] peer '%s' already registered, skip\n",
                        cache_name.c_str());
                return;
            }
            peer_cache_req_outs_.emplace_back(cache_name, req_out);
            DPRINTF(MODULE, "[CoherentXBar] Registered peer cache %s (req_out=%p)\n",
                    cache_name.c_str(), static_cast<void*>(req_out));
        }

        void CoherentXBarTLM::snoop_broadcast(Packet* pkt) {
            if (!pkt)
                return; // nullptr 是 no-op

            // Phase 7.A/7.B write-through 透传: 不下场做 coherence 决策
            // Phase 7.C 将引入 6×6 state table 判断是否真要广播
            //
            // 实现说明: Packet 的拷贝构造被 std::atomic<uint32_t> ref_count 隐式删除,
            //           故走 PacketPool::acquire() 拿独立 Packet + 手动字段拷贝
            //           (每个 peer 拿到独立所有权, ref_count 从 0 重新开始)
            for (auto& [cache_name, port] : peer_cache_req_outs_) {
                Packet* copy = PacketPool::get().acquire();
                // 复制原始 pkt 的可见字段 (ref_count 由 acquire() 重置为 0)
                copy->payload = pkt->payload; // 共享 tlm_generic_payload
                copy->type = pkt->type;
                copy->cmd = pkt->cmd;
                copy->src_cycle = pkt->src_cycle;
                copy->dst_cycle = pkt->dst_cycle;
                copy->stream_id = pkt->stream_id;
                copy->seq_num = pkt->seq_num;
                copy->vc_id = pkt->vc_id;
                copy->hop_count = pkt->hop_count;
                copy->priority = pkt->priority;
                copy->flow_id = pkt->flow_id;
                copy->original_req = pkt->original_req; // 共享原始请求引用
                copy->dependents = pkt->dependents;     // 共享依赖关系
                copy->route_path = pkt->route_path;     // 复制路由路径

                if (!port->sendReq(copy)) {
                    DPRINTF(MODULE, "[CoherentXBar] Snoop to %s dropped (VC full)\n",
                            cache_name.c_str());
                    PacketPool::get().release(copy); // 退还到 pool, 防泄漏
                } else {
                    DPRINTF(MODULE, "[CoherentXBar] Snoop to %s sent\n", cache_name.c_str());
                }
            }
            // 注: 原 pkt 由调用方负责生命周期 (helper_pairs_ / 测试的 PacketPool::release)
        }

    } // namespace tlm
} // namespace cpptlm