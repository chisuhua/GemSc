# design/topology_4x4_mesh.py
from noc_builder import *

# 创建所有路由器
routers = []
for y in range(4):
    for x in range(4):
        r = VcRouter(f"r{y}_{x}", router_id=y*4+x, network_dim=4)
        r.instantiate()  # 构建内部
        routers.append(r)

# 创建终端节点
terminals = [TerminalNode(f"term{i}") for i in range(4)]

# 构建顶层连接
connections = build_mesh_connections(routers) + connect_terminals(terminals, routers)

# 生成最终的 JSON 配置
final_config = {
    "plugin": ["libnoc.so"],
    "modules": [r.to_json() for r in routers] + [t.to_json() for t in terminals],
    "connections": connections
}

with open("configs/4x4_mesh.json", "w") as f:
    json.dump(final_config, f, indent=2)
