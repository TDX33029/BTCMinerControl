# BTC Hashboard

[🇨🇳 简体中文](README.md) | [🇬🇧 English](README_EN.md)

[![Status](https://img.shields.io/badge/Status-Active%20Development-brightgreen)](#version-history-changelog)
[![Discord](https://img.shields.io/badge/Community-Discord-7289DA?logo=discord&logoColor=white)](https://discord.com/channels/1091348375301013615)
[![License](https://img.shields.io/badge/License-Non--Commercial-orange)](#open-source-statement--rights-notice)

BTC Bitcoin ASIC mining hashboard for cryptocurrency mining. Supports wired Ethernet connection to a LAN host / upper computer for centralized scheduling and cluster management.

The project is synchronized with Open Source Miners Union: **OSMU-bitaxe-hardware-dev (personally modified version, non-reference design)**.

---

## Hardware Showcase and Architecture

<img width="1424" height="915" alt="image" src="https://github.com/user-attachments/assets/ea1a3946-3d3e-4eb5-b83d-8ff5c08ccd40" />
<img width="877" height="524" alt="image" src="https://github.com/user-attachments/assets/df5a90d1-7dd6-456b-bb46-0cd853e8eb7c" />


### Core Bill of Materials (BOM)

| Functional Module | Chip / Component Model | Specifications & Design Notes |
| :--- | :--- | :--- |
| **Main Control MCU** | `STM32F107VCT6` | Compatible with domestic alternative `GD32F107VCT6` (**Note: Crystal oscillator replacement required**) |
| **Computing Unit** | `BM1366` | Bitmain high-performance SHA-256 ASIC chip |
| **Ethernet PHY** | `DP83848DSK` | Independent power supply and isolated routing design |
| **Level Transceiver** | `SN74AVC4T774PWR` | Logic level translation |

### Power Supply Requirements

* **Main System Power Supply:** Requires maximum instantaneous capability of **12V @ 10A**.
* **Custom Buck Converter Module:** 
  * Maximum instantaneous capability of **3.6V @ 60A**
  * Rated temperature rise current of **3.6V @ 35A**

<img width="1221" height="766" alt="image" src="https://github.com/user-attachments/assets/c38fcc09-06e1-491f-96ed-29457919680b" />

<img width="1358" height="808" alt="image" src="https://github.com/user-attachments/assets/0ff3de63-62c5-45f0-811f-26037e1a5071" />

---

## Network Architecture and Communication

Communication is implemented using **LwIP**, with auto-negotiated link speed.

* **Bandwidth Recommendation:** For legacy switching equipment, it is recommended to limit the speed to **10M** to reduce network bandwidth pressure.
* **Topology:** Uses wired Ethernet connection to a host / upper computer for task distribution and aggregation rather than connecting directly to mining pools via Stratum V2, facilitating cluster management.
* **LAN Environment:** Note that the switch should have DHCP functionality or be connected via an external router.
* **Host IP Tracing Mechanism:** 
  * The board hardcodes the tracing host IP to **`10.8.1.3:4200`**.
  * The device hosting the upper computer software should use a **static IP** or router-assigned static DHCP lease to prevent loss of connection.

---

## Hardware Version Warning

> [!CAUTION]
> **Stop using Ver 1.0 and Ver 1.1alpha hardware!**  
> Early revisions contain critical design flaws that have been resolved in subsequent versions.

---

## Version History (Changelog)

* **`v1.2`:** Fixed shared crystal oscillator MCO output error in v1.1, fixed analog ground sharing issue, deprecated TPS546D24 in favor of a custom power supply design, reassigned Ethernet LED indicators
* **`v1.1`:** Fixed RMII errors present in v1.0alpha
* **`v1.0alpha`:** First unverified physical engineering release
* **`v0.8`:** First preliminary engineering release without secondary verification
* **`v0.8alpha`:** Project created

---

## Open Source Statement & Rights Notice

* **Discussion & Community:** Welcome to join the discussion on our Discord channel: [OSMU Discord](https://discord.com/channels/1091348375301013615).
* **Usage Rights:** All files are open access. Personal rights are reserved for learning and non-commercial access; **commercial use is strictly prohibited**.
* **Copyright Notice:** All rights are reserved for software and hardware, including but not limited to host / upper computer programs, board firmware and source code, PCB engineering files, etc.
