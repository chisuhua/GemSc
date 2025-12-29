# GemSc 配置系统

GemSc 使用 JSON 配置文件来定义系统拓扑、模块参数和连接关系。本文档详细介绍了配置系统的使用方法。

## 1. 配置文件结构

一个典型的配置文件包含以下几个部分：

```json
{
  "plugin": [],
  "clock_domains": [
    {
      "name": "core_domain",
      "frequency": 2000,
      "threads": [
        {
          "name": "core_thread",
          "objects": [
            "cpu_core_0",
            "l1_cache_0"
          ]
        }
      ]
    }
  ],
  "modules": [
    {
      "name": "cpu_core_0",
      "type": "SimpleCPU"
    },
    {
      "name": "l1_cache_0",
      "type": "SimpleCache"
    }
  ],
  "connections": [
    {
      "src": "cpu_core_0.cache_port",
      "dst": "l1_cache_0.cpu_req",
      "latency": 1
    }
  ]
}
```

## 2. 模块定义

在 `modules` 部分定义系统中的所有模块：

```json
{
  "modules": [
    {
      "name": "模块唯一名称",
      "type": "模块类型（注册到ModuleFactory中的名称）",
      "parameters": {
        "param1": "value1",
        "param2": 100
      }
    }
  ]
}
```

## 3. 连接定义

在 `connections` 部分定义模块间的连接关系：

```json
{
  "connections": [
    {
      "src": "源模块名.源端口名",
      "dst": "目标模块名.目标端口名",
      "latency": 延迟周期数（默认为1）
    }
  ]
}
```

## 4. 时钟域与线程

GemSc 支持多时钟域和多线程：

```json
{
  "clock_domains": [
    {
      "name": "时钟域名称",
      "frequency": 时钟频率(MHz),
      "threads": [
        {
          "name": "线程名称",
          "objects": [
            "该线程上运行的模块名称列表"
          ]
        }
      ]
    }
  ]
}
```

## 5. JSON 包含机制

配置文件支持使用 `$include` 指令包含其他 JSON 文件：

```json
{
  "$include": "path/to/other_config.json",
  "modules": [
    // ...
  ]
}
```

## 6. 通配符和正则匹配

GemSc 支持使用通配符和正则表达式进行批量连接：

- 通配符匹配：使用 `*` 匹配多个模块
- 正则表达式：使用正则表达式进行复杂匹配

## 7. 层次化模块

支持定义层次化模块（[SimModule](file:///mnt/ubuntu/chisuhua/github/GemSc/include/sim_module.hh#L44-L59)）：

```json
{
  "modules": [
    {
      "name": "hier_module",
      "type": "HierModule",
      "config": {
        "modules": [
          // 层次化模块内部的子模块定义
        ],
        "connections": [
          // 层次化模块内部的连接定义
        ]
      }
    }
  ]
}
```

## 8. 示例配置

### 8.1 简单 CPU-Cache-Memory 系统

```json
{
  "modules": [
    {
      "name": "cpu",
      "type": "CPUSim"
    },
    {
      "name": "cache",
      "type": "CacheSim"
    },
    {
      "name": "mem",
      "type": "MemorySim"
    }
  ],
  "connections": [
    {
      "src": "cpu.cache_port",
      "dst": "cache.cpu_req",
      "latency": 1
    },
    {
      "src": "cache.mem_port",
      "dst": "mem.cache_req",
      "latency": 10
    }
  ]
}
```

### 8.2 多核系统

```json
{
  "clock_domains": [
    {
      "name": "cores",
      "frequency": 3000,
      "threads": [
        {
          "name": "core0_thread",
          "objects": ["core0", "l1_cache_0"]
        },
        {
          "name": "core1_thread",
          "objects": ["core1", "l1_cache_1"]
        }
      ]
    },
    {
      "name": "memory",
      "frequency": 1000,
      "threads": [
        {
          "name": "mem_thread",
          "objects": ["l2_cache", "memory"]
        }
      ]
    }
  ],
  "modules": [
    {
      "name": "core0",
      "type": "CPUSim"
    },
    {
      "name": "l1_cache_0",
      "type": "CacheSim"
    },
    {
      "name": "core1",
      "type": "CPUSim"
    },
    {
      "name": "l1_cache_1",
      "type": "CacheSim"
    },
    {
      "name": "l2_cache",
      "type": "CacheSim"
    },
    {
      "name": "memory",
      "type": "MemorySim"
    }
  ],
  "connections": [
    {
      "src": "core0.cache_port",
      "dst": "l1_cache_0.cpu_req",
      "latency": 1
    },
    {
      "src": "l1_cache_0.mem_port",
      "dst": "l2_cache.core0_req",
      "latency": 3
    },
    {
      "src": "core1.cache_port",
      "dst": "l1_cache_1.cpu_req",
      "latency": 1
    },
    {
      "src": "l1_cache_1.mem_port",
      "dst": "l2_cache.core1_req",
      "latency": 3
    },
    {
      "src": "l2_cache.mem_port",
      "dst": "memory.cache_req",
      "latency": 100
    }
  ]
}
```

## 9. 配置验证

配置系统会自动验证：
- 模块名称的唯一性
- 连接的有效性（源和目标模块是否存在）
- 端口名称的有效性
- 循环依赖的检测