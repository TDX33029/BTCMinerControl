# Bitaxe Custom BM1366 Hashboard (OSMU Edition)

[![Status](https://img.shields.io/badge/Status-Active%20Development-brightgreen)](#版本历史--changelog)
[![Discord](https://img.shields.io/badge/Community-Discord-7289DA?logo=discord&logoColor=white)](https://discord.com/channels/1091348375301013615)
[![License](https://img.shields.io/badge/License-Non--Commercial-orange)](#开源声明与权利说明)

基于比特大陆 **BM1366** ASIC 芯片与 **STM32F107** 主控构建的比特币（BTC）独立算力板卡。支持通过有线以太网接入局域网主机/上位机实现集中调度与集群管理。

工程同步维护于开源矿工联盟：**OSMU-bitaxe-hardware-dev**。

---

## 硬件架构与选型

<p align="center">
  <img src="https://github.com/user-attachments/assets/ea1a3946-3d3e-4eb5-b83d-8ff5c08ccd40" alt="板卡全局效果图" width="85%" />
</p>

### 核心物料清单 (BOM Overview)

| 功能模块 | 芯片 / 器件型号 | 规格及设计说明 |
| :--- | :--- | :--- |
| **主控 MCU** | `STM32F107VCT6` | 兼容国产替代芯片 `GD32F107VCT6`（**注：若使用 GD32 请务必核对并匹配晶振配置参数**） |
| **算力单元** | `BM1366` | 比特大陆高性能 SHA-256 ASIC 芯片 |
| **以太网 PHY** | `DP83848DSK` | RMII 接口，独立供电与隔离走线设计 |
| **电平转换** | `SN74AVC4T774PWR` | 高速双向逻辑电平转换 |

### 供电指标要求 (Power Constraints)

* **系统主供电 (Main DC In):** 要求具备最大瞬时 **12V @ 10A** 输出裕量。
* **ASIC 核心供电 (自建降压模块):**
  * 额定温升电流：**3.6V @ 35A**
  * 最大瞬时瞬态电流：**3.6V @ 60A**

<p align="center">
  <img src="https://github.com/user-attachments/assets/c38fcc09-06e1-491f-96ed-29457919680b" alt="电源模块局部细节" width="48%" />
  <img src="https://github.com/user-attachments/assets/0ff3de63-62c5-45f0-811f-26037e1a5071" alt="布线与元器件细节" width="48%" />
</p>

---

## 网络架构与上位机部署

本项目网络协议栈采用 **LwIP**。为了便于大规模集群维护与集中调度，架构上**未采用**板载芯片直接直连矿池（Stratum V2）的方案，而是采用有线以太网直连上位机，由上位机统一进行任务分发与数据汇聚。

### 1. 物理链路与交换机设置
* 通信支持自协商速率。若部署于大型集群或老旧交换机环境下，**建议强制协商或限制至 10 Mbps 模式**，以规避网络拥塞并降低链路负载。
* 局域网内必须具备 DHCP 服务（可通过具备 DHCP 功能的托管交换机或外挂软路由/路由器实现）。

### 2. 上位机与板卡寻址机制
* **板卡溯源固化地址：** 板卡出厂固件内部**硬编码溯源上位机目标 IP 为 `10.8.1.3:4200`**。
* **上位机网络要求：** 部署汇聚/调度程序的上位机服务器必须配置为**静态固定 IP `10.8.1.3`**，或在网关处将其 MAC 地址与该 IP 绑定（静态 DHCP 分配），否则板卡上线后将无法建立通信。

---

## 硬件版本警告

> [!CAUTION]
> **严禁打样或继续使用 `v1.0` 与 `v1.1alpha` 版本的硬件工程！**
> 早期工程存在严重的 RMII 拓扑、模拟地与振荡器设计缺陷，相关问题已在 `v1.2` 正式版全面修复。

---

## 版本历史 (Changelog)

* **`v1.2` (当前稳定版):**
  * 修复 `v1.1` 中石英振荡器 MCO 输出共用导致的偶发时钟异常
  * 彻底分离并修复模拟地（AGND）与数字地（DGND）共享干扰问题
  * 弃用原 TPS546D24 方案，切换为自建高效分立供电电源模块
  * 重新分配并优化以太网状态 LED 指示灯引脚映射
* **`v1.1`:**
  * 修复 `v1.0alpha` 中存在的 RMII 布线与引脚定义错误
* **`v1.0alpha`:**
  * 第一版实体验证打样工程文件（存在设计缺陷，已废弃）
* **`v0.8`:**
  * 初版设计，未经二次复核的工程备份
* **`v0.8alpha`:**
  * 初始立项与原理图框架搭建

---

## 开源声明与权利说明

1. 本项目所有设计文件（包括但不限于上位机程序、板卡固件源码、PCB 原理图及 PCB 工程文件）**保留所有权利**。
2. 所有设计资料全面开放仅用于**个人学习、科研交流与非盈利性测试用途**。
3. **严禁未经授权用于任何形式的商业生产、销售、转售或二次商业分发。**
4. 欢迎加入社区讨论：[OSMU-bitaxe-hardware-dev Discord Channel](https://discord.com/channels/1091348375301013615)。
