# noc_builder.py
import json
import math
from typing import List, Dict, Tuple

class Topology:
    """抽象的拓扑基类"""
    def generate_nodes(self) -> List[Dict]:
        raise NotImplementedError
    
    def generate_connections(self) -> List[Dict]:
        raise NotImplementedError
    
    def get_coordinates(self) -> Dict[str, Tuple[float, float]]:
        raise NotImplementedError

class MeshTopology(Topology):
    def __init__(self, rows: int, cols: int):
        self.rows = rows
        self.cols = cols
    
    def generate_nodes(self) -> List[Dict]:
        nodes = []
        for y in range(self.rows):
            for x in range(self.cols):
                node_id = f"r{y}_{x}"
                nodes.append({
                    "name": node_id,
                    "type": "Router",
                    # 可以在这里添加 router-specific config
                })
        return nodes
    
    def generate_connections(self) -> List[Dict]:
        connections = []
        for y in range(self.rows):
            for x in range(self.cols):
                current = f"r{y}_{x}"
                # 连接东边
                if x < self.cols - 1:
                    east = f"r{y}_{x+1}"
                    connections.append({"src": f"{current}.E_out", "dst": f"{east}.W_in"})
                    connections.append({"src": f"{east}.W_out", "dst": f"{current}.E_in"}) # 双向
                # 连接南边
                if y < self.rows - 1:
                    south = f"r{y+1}_{x}"
                    connections.append({"src": f"{current}.S_out", "dst": f"{south}.N_in"})
                    connections.append({"src": f"{south}.N_out", "dst": f"{current}.S_in"})
        return connections
    
    def get_coordinates(self) -> Dict[str, Tuple[float, float]]:
        coords = {}
        for y in range(self.rows):
            for x in range(self.cols):
                node_id = f"r{y}_{x}"
                # 简单的网格布局
                coords[node_id] = (x * 100, y * 100)
        return coords

def build_gemsc_config(topology: Topology, terminal_nodes: List[Dict], plugin_list: List[str]) -> Dict:
    """根据拓扑和终端节点构建完整的 gemsc 配置字典"""
    modules = topology.generate_nodes()
    modules.extend(terminal_nodes)
    
    connections = topology.generate_connections()
    # 添加终端节点的连接...
    
    config = {
        "plugin": plugin_list,
        "modules": modules,
        "connections": connections
    }
    return config

def generate_dot_file(config: Dict, output_file: str):
    """根据 gemsc 配置生成 Graphviz .dot 文件"""
    with open(output_file, 'w') as f:
        f.write("digraph G {\n")
        f.write("    rankdir=TB;\n") # 从上到下布局
        
        # 写入所有模块节点
        for mod in config["modules"]:
            f.write(f'    "{mod["name"]}" [label="{mod["name"]}\\n({mod["type"]})"];\n')
        
        # 写入所有连接
        for conn in config["connections"]:
            src_mod, src_port = conn["src"].split(".", 1)
            dst_mod, dst_port = conn["dst"].split(".", 1)
            # 可以根据端口名（如 E_out, W_in）来调整箭头样式
            f.write(f'    "{src_mod}" -> "{dst_mod}" [label="{src_port} -> {dst_port}"];\n')
        
        f.write("}\n")

def main():
    # 定义拓扑
    mesh_topo = MeshTopology(rows=4, cols=4)
    
    # 定义终端节点
    terminals = [
        {"name": "cpu_0", "type": "TerminalNode", "config": {"id": 0}},
        {"name": "mem_0", "type": "TerminalNode", "config": {"id": 1}}
        # ... 更多终端节点
    ]
    
    # 构建配置
    config = build_gemsc_config(mesh_topo, terminals, ["libnoc.so"])
    
    # 生成 JSON 配置文件
    with open("configs/mesh_4x4.json", "w") as f:
        json.dump(config, f, indent=2)
    
    # 生成 DOT 文件用于可视化
    generate_dot_file(config, "topologies/mesh_4x4.dot")
    
    print("Configuration and topology files generated successfully!")

if __name__ == "__main__":
    main()
