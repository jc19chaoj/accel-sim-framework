# NVIDIA A100 (GA100) — GPGPU-Sim 4.0 仿真架构图

> 基于 `SM80_A100` 配置文件中的实际参数绘制，反映 GPGPU-Sim 4.0 trace-driven 模式下的建模粒度。

---

## 1. 芯片全局拓扑

```mermaid
graph TB
    subgraph GA100["GA100 Full Die — 108 SMs / 40 Memory Controllers"]
        direction TB

        subgraph GPC_ARRAY["GPC Array (SM Clusters × 108)"]
            direction LR
            SM0["SM 0"]
            SM1["SM 1"]
            SM2["SM 2"]
            SM_dots["· · ·"]
            SM107["SM 107"]
        end

        subgraph XBAR["Local Crossbar Interconnect (iSLIP Arbiter)"]
            direction LR
            REQ_NET["Request Network\n(flit 40B)"]
            REPLY_NET["Reply Network\n(flit 40B)"]
        end

        subgraph L2_SLICE["L2 Cache — 40 MB Total (160 Sub-partitions)"]
            direction LR
            subgraph MC0["MemCtrl 0"]
                L2_SP0["Sub-part 0"]
                L2_SP1["Sub-part 1"]
                L2_SP2["Sub-part 2"]
                L2_SP3["Sub-part 3"]
            end
            MC_dots["· · · × 40 Controllers"]
            subgraph MC39["MemCtrl 39"]
                L2_SP156["Sub-part 156"]
                L2_SP157["Sub-part 157"]
                L2_SP158["Sub-part 158"]
                L2_SP159["Sub-part 159"]
            end
        end

        subgraph HBM2e["HBM2e DRAM — 40 Channels"]
            direction LR
            DRAM0["Ch 0\n16B bus\nBL=2\n16 Banks\n4 Bank Groups"]
            DRAM_dots["· · ·"]
            DRAM39["Ch 39\n16B bus\nBL=2\n16 Banks\n4 Bank Groups"]
        end

        GPC_ARRAY --> XBAR
        XBAR --> L2_SLICE
        L2_SLICE --> HBM2e
    end

    style GA100 fill:#1a1a2e,color:#eee,stroke:#16213e
    style GPC_ARRAY fill:#0f3460,color:#eee,stroke:#533483
    style XBAR fill:#533483,color:#eee,stroke:#e94560
    style L2_SLICE fill:#16213e,color:#eee,stroke:#0f3460
    style HBM2e fill:#e94560,color:#fff,stroke:#1a1a2e
```

---

## 2. SM (Streaming Multiprocessor) 内部微架构

```mermaid
graph TB
    subgraph SM["SM — Ampere SM80 (Sub-core Model)"]
        direction TB

        subgraph FRONTEND["前端 — Instruction Fetch & Decode"]
            IL1["Instruction L1 Cache\n64 sets × 16-way × 128B\nPerfect Cache"]
            DECODE["Instruction Decode\n4-wide Issue"]
        end

        subgraph SCHED["4× Warp Scheduler (LRR)"]
            direction LR
            WS0["Scheduler 0\nIBuffer: 2 entries"]
            WS1["Scheduler 1\nIBuffer: 2 entries"]
            WS2["Scheduler 2\nIBuffer: 2 entries"]
            WS3["Scheduler 3\nIBuffer: 2 entries"]
        end

        subgraph OC["Operand Collector — 8 Units"]
            direction LR
            OC_IN["8 Input Ports"]
            OC_UNIT["8 Collector Units\nBipartite Arbitration"]
            OC_OUT["8 Output Ports"]
            OC_IN --> OC_UNIT --> OC_OUT
        end

        subgraph RF["Register File — 64 KB per SM"]
            direction LR
            RF0["RF Bank 0"]
            RF1["RF Bank 1"]
            RF_dots["· · ·"]
            RF31["RF Bank 31"]
        end

        subgraph EXEC["Execution Units (per Sub-core × 4)"]
            direction LR
            subgraph INT_PIPE["INT Pipeline"]
                INT["INT Unit\nADD/MUL/MAD: 4 cyc\nDIV: 21 cyc"]
            end
            subgraph SP_PIPE["FP32 Pipeline"]
                SP["SP Unit\nADD/MUL/MAD: 4 cyc\nDIV: 39 cyc"]
            end
            subgraph DP_PIPE["FP64 Pipeline"]
                DP["DP Unit\nADD/MUL/MAD: 6 cyc\nDIV: 330 cyc"]
            end
            subgraph SFU_PIPE["SFU Pipeline"]
                SFU["SFU\nLatency: 23 cyc\nInit: 8 cyc"]
            end
            subgraph TC_PIPE["Tensor Core"]
                TC["HMMA/IMMA\nLatency: 25 cyc\nInit: 16 cyc"]
            end
            subgraph BRA_PIPE["Branch Unit"]
                BRA["BRA\nLatency: 4 cyc"]
            end
            subgraph TEX_PIPE["Texture Unit"]
                TEX["TEX\nLatency: 200 cyc"]
            end
            subgraph UDP_PIPE["UDP"]
                UDP["Unified Data Path\nLatency: 4 cyc\nInit: 1 cyc"]
            end
        end

        subgraph LDST["Load/Store Unit"]
            COAL["Address Coalescing\n(Arch v80)"]
            SMEM["Shared Memory\n164 KB / 32 Banks\nLatency: 28 cyc"]
            L1D["L1 Data Cache (Sector)\n4 sets × 64-way × 128B\nLatency: 34 cyc\nMSHR: 512 entries"]
        end

        IL1 --> DECODE
        DECODE --> SCHED
        SCHED --> OC
        RF <-.-> OC
        OC --> EXEC
        OC --> LDST
        COAL --> SMEM
        COAL --> L1D
    end

    L1D -->|"Miss → ICNT"| XBAR_OUT["To Crossbar → L2"]

    style SM fill:#1a1a2e,color:#eee,stroke:#533483
    style FRONTEND fill:#0f3460,color:#eee,stroke:#533483
    style SCHED fill:#533483,color:#eee,stroke:#e94560
    style OC fill:#2a2a4e,color:#eee,stroke:#533483
    style RF fill:#16213e,color:#eee,stroke:#0f3460
    style EXEC fill:#0f3460,color:#eee,stroke:#e94560
    style LDST fill:#16213e,color:#eee,stroke:#e94560
```

---

## 3. 内存层次与数据通路

```mermaid
graph LR
    subgraph SM_SIDE["SM 端"]
        WARP["Warp (32 threads)"] --> COAL["地址合并\nCoalesce v80"]
        COAL --> SMEM_PATH["Shared Mem?\n(164 KB, 28 cyc)"]
        COAL --> L1_PATH["L1D Cache\nSector, 34 cyc"]
    end

    subgraph ICNT_LAYER["互连层"]
        L1_PATH -->|"Miss (32B port)"| ICNT["Crossbar\niSLIP × 2 subnets\nIn/Out buf: 512"]
    end

    subgraph L2_LAYER["L2 层"]
        ICNT --> L2["L2 Cache\n128 sets × 16-way × 128B\nSector Cache\nMSHR: 192 entries\nROP Latency: 200 cyc"]
    end

    subgraph DRAM_LAYER["DRAM 层"]
        L2 -->|"Miss"| DRAM_SCHED["FR-FCFS Scheduler\nQueue: 64 req"]
        DRAM_SCHED --> HBM["HBM2e\n16B × BL2 = 32B/access\nCL=22, RCD=22, RP=22\nRAS=50, RC=72\n@ 1512 MHz"]
    end

    DRAM_LAYER -->|"Return (192 deep)"| L2_LAYER
    L2_LAYER -->|"Fill"| ICNT_LAYER
    ICNT_LAYER -->|"Fill"| SM_SIDE

    style SM_SIDE fill:#0f3460,color:#eee
    style ICNT_LAYER fill:#533483,color:#eee
    style L2_LAYER fill:#16213e,color:#eee
    style DRAM_LAYER fill:#e94560,color:#fff
```

---

## 4. SM 流水线阶段与带宽

```mermaid
graph LR
    subgraph PIPELINE["SM Pipeline Stages (Width = flits/cycle)"]
        direction LR
        IF["Fetch"] -->|"4 instr/cyc"| ID["Decode"]
        ID -->|"ID→OC: 4"| OC["Operand\nCollect"]
        OC -->|"OC→EX: 4"| EX["Execute"]
        EX -->|"EX→WB: 8"| WB["Writeback"]
    end

    subgraph DETAIL["ID→OC / OC→EX Width per Unit Type"]
        direction TB
        D1["SP:  ID→OC=4, OC→EX=4"]
        D2["DP:  ID→OC=4, OC→EX=4"]
        D3["INT: ID→OC=4, OC→EX=4"]
        D4["SFU: ID→OC=4, OC→EX=4"]
        D5["MEM: ID→OC=4, OC→EX=4"]
        D6["TC:  ID→OC=4, OC→EX=4"]
    end

    style PIPELINE fill:#0f3460,color:#eee
    style DETAIL fill:#1a1a2e,color:#eee
```

---

## 5. 自适应 L1/Shared Memory 分配

```mermaid
graph TB
    subgraph ADAPTIVE["Unified 192 KB On-Chip Memory"]
        direction LR
        CFG1["Config A\nSMEM: 0 KB\nL1D: 192 KB"]
        CFG2["Config B\nSMEM: 8 KB\nL1D: 184 KB"]
        CFG3["Config C\nSMEM: 16 KB\nL1D: 176 KB"]
        CFG4["Config D\nSMEM: 32 KB\nL1D: 160 KB"]
        CFG5["Config E\nSMEM: 64 KB\nL1D: 128 KB"]
        CFG6["Config F\nSMEM: 164 KB\nL1D: 28 KB"]
    end

    style ADAPTIVE fill:#16213e,color:#eee,stroke:#533483
```

---

## 6. 时钟域

```mermaid
graph LR
    subgraph CLOCKS["Independent Clock Domains"]
        direction LR
        CLK_CORE["Core\n1410 MHz\n(SM, L1)"]
        CLK_ICNT["Interconnect\n1410 MHz\n(Crossbar)"]
        CLK_L2["L2 / Partition\n1410 MHz\n(L2, MemCtrl)"]
        CLK_DRAM["DRAM\n1512 MHz\n(HBM2e)"]
    end

    CLK_CORE ---|"Async"| CLK_ICNT
    CLK_ICNT ---|"Async"| CLK_L2
    CLK_L2 ---|"Async"| CLK_DRAM

    style CLOCKS fill:#1a1a2e,color:#eee,stroke:#e94560
```

---

## 7. DRAM Bank 组织

```mermaid
graph TB
    subgraph DRAM_CH["HBM2e Channel (× 40)"]
        direction TB
        subgraph BG0["Bank Group 0"]
            B0["Bank 0"]
            B1["Bank 1"]
            B2["Bank 2"]
            B3["Bank 3"]
        end
        subgraph BG1["Bank Group 1"]
            B4["Bank 4"]
            B5["Bank 5"]
            B6["Bank 6"]
            B7["Bank 7"]
        end
        subgraph BG2["Bank Group 2"]
            B8["Bank 8"]
            B9["Bank 9"]
            B10["Bank 10"]
            B11["Bank 11"]
        end
        subgraph BG3["Bank Group 3"]
            B12["Bank 12"]
            B13["Bank 13"]
            B14["Bank 14"]
            B15["Bank 15"]
        end
    end

    NOTE["CCD=1 (跨组)\nCCDL=4 (组内)\nRRD=7\n16B bus × BL2"]

    style DRAM_CH fill:#e94560,color:#fff,stroke:#1a1a2e
    style NOTE fill:#1a1a2e,color:#eee,stroke:#e94560
```

---

## 参数速查表

| 参数 | 值 |
|------|-----|
| Compute Capability | 8.0 (Ampere) |
| SM 数量 | 108 |
| Warp Scheduler / SM | 4 (LRR) |
| 最大并发 Kernel | 128 |
| Register File / SM | 64 KB (32 banks) |
| Shared Memory / SM | 最大 164 KB (32 banks, 28 cyc) |
| L1D Cache / SM | Sector, 最大 192 KB, 34 cyc |
| L2 Cache 总量 | 40 MB (160 sub-partitions) |
| 内存控制器 | 40 × FR-FCFS |
| HBM2e 通道 | 40 (16B bus, BL=2, 16 banks/ch) |
| 互连 | Full Crossbar, iSLIP, 2 subnets |
| Core / ICNT / L2 时钟 | 1410 MHz |
| DRAM 时钟 | 1512 MHz |
| Tensor Core / SM | 4 units (latency 25 cyc) |
| FP32 Units / SM | 4 |
| FP64 Units / SM | 4 |
| INT Units / SM | 4 |
| SFU Units / SM | 4 |
