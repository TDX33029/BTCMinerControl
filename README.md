# BTC Hashboard

[![Status](https://img.shields.io/badge/Status-Active%20Development-brightgreen)](#版本历史--changelog)
[![Discord](https://img.shields.io/badge/Community-Discord-7289DA?logo=discord&logoColor=white)](https://discord.com/channels/1091348375301013615)
[![License](https://img.shields.io/badge/License-Non--Commercial-orange)](#开源声明与权利说明)

BTC 比特币矿机芯片算力板卡，用于虚拟货币挖掘使用。支持通过有线以太网接入局域网主机/上位机实现集中调度与集群管理。

工程同步更新至开源矿工联盟：**OSMU-bitaxe-hardware-dev(个人维护版本，非公版)**。

---

## 硬件展示与架构

<img width="1424" height="915" alt="image" src="https://github.com/user-attachments/assets/ea1a3946-3d3e-4eb5-b83d-8ff5c08ccd40" />

### 核心物料清单 (BOM)

| 功能模块 | 芯片 / 器件型号 | 规格及设计说明 |
| :--- | :--- | :--- |
| **主控 MCU** | `STM32F107VCT6` | 兼容国产替代芯片 `GD32F107VCT6`（**注意：晶振型号更替**） |
| **算力单元** | `BM1366` | 比特大陆高性能 SHA-256 ASIC 芯片 |
| **以太网 PHY** | `DP83848DSK` | 独立供电与隔离走线设计 |
| **电平收发器** | `SN74AVC4T774PWR` | 逻辑电平转换 |

### 供电指标要求

* **系统主供电:** 要求最大瞬时满足 **12V @ 10A**。
* **自建供电变压模块:** 
  * 最大瞬时满足 **3.6V @ 60A**
  * 额定温升电流 **3.6V @ 35A**

<img width="1221" height="766" alt="image" src="https://github.com/user-attachments/assets/c38fcc09-06e1-491f-96ed-29457919680b" />

<img width="1358" height="808" alt="image" src="https://github.com/user-attachments/assets/0ff3de63-62c5-45f0-811f-26037e1a5071" />

---

## 网络架构与通信说明

网络采用 **LwIP** 进行通信，通信速度采用自协商。

* **带宽建议:** 对于老旧的交换设备，建议限制到 **10M** 以便减轻网络带宽压力。
* **拓扑结构:** 采用有线 Ethernet 连接主机上位机进行分发汇聚，而非 Stratum V2 直连矿池，便于设备集群进行管理。
* **局域网环境:** 注意交换机应具备 DHCP 功能或外挂路由器实现。
* **上位机 IP 溯源机制:** 
  * 板卡溯源上位机 IP **固定刷入 `10.8.1.3:4200`**。
  * 上位机部署设备应采用**静态 IP** 或路由器静态分配 IP，避免失联。

---

## 硬件版本警告

> [!CAUTION]
> **停止使用 Ver 1.0 与 Ver 1.1alpha 版本硬件！**  
> 早期工程存在较为严重的设计缺陷，已在后续版本中修复。

---

## 版本历史 (Changelog)

* **`v1.2`:** 修复 v1.1 中石英振荡器 MCO 输出共用错误，修复模拟地共享问题，弃用 TPS546D24 采用自建电源，重新分配 Ethernet 指示灯
* **`v1.1`:** 修复 v1.0alpha 存在的 RMII 错误
* **`v1.0alpha`:** 第一版未经验证实体工程文件
* **`v0.8`:** 第一版未二次核验工程文件
* **`v0.8alpha`:** 工程已被创建

---

## 开源声明与权利说明

* **讨论交流:** 欢迎访问 Discord 频道交流：[OSMU Discord](https://discord.com/channels/1091348375301013615)。
* **使用权限:** 所有文件全部开放。对于学习等访问权限保有个人权利，**禁止用于商业**等。
* **版权声明:** 对于软件以及硬件，包括但不限于上位机程序、板卡固件及源码、板卡工程文件等，保留所有权利。
