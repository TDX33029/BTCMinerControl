BTC 比特币 矿机芯片算力板卡，用于虚拟货币挖掘使用
<img width="1424" height="915" alt="image" src="https://github.com/user-attachments/assets/ea1a3946-3d3e-4eb5-b83d-8ff5c08ccd40" />

主控采用STM32F107VCT6，工程样品兼容国产GD32F107VCT6 注意晶振型号更替 算力芯片为比特大陆BM1366 以太网PHY采用DP83848DSK 电平收发器采用SN74AVC4T774PWR 主供电要求最大瞬时满足12V@10A，自建供电变压模块要求最大瞬时满足3.6V@60A，温升电流3.6V@35A
<img width="1221" height="766" alt="image" src="https://github.com/user-attachments/assets/c38fcc09-06e1-491f-96ed-29457919680b" />
<img width="1358" height="808" alt="image" src="https://github.com/user-attachments/assets/0ff3de63-62c5-45f0-811f-26037e1a5071" />

网络采用LwIP进行通信，通信速度采用自协商，对于老旧的交换设备建议限制到10M以便减轻网络带宽压力

采用有线Ethernet连接主机上位机进行分发汇聚而非Stratum V2直连矿池，便于设备集群进行管理。注意交换机应具备DHCP功能或外挂路由器实现。注意板卡溯源上位机IP固定刷入10.8.1.3:4200，上位机部署设备应采用静态IP或静态分配IP避免失联。

停止使用Ver1.0与Ver1.1alpha版本硬件，存在较为严重的设计缺陷，已在后续版本修复

v0.8alpha 工程已被创建
v0.8      第一版未二次核验工程文件
v1.0alpha 第一版未经验证实体工程文件
v1.1      修复v1.0alpha存在的RMII错误
v1.2      修复v1.1中石英振荡器MCO输出共用错误
v1.3      弃用TPS546D24采用自建电源，重新分配Ethernet指示灯

工程同步更新至开源矿工联盟OSMU-bitaxe-hardware-dev，欢迎访问Discord频道交流https://discord.com/channels/1091348375301013615

所有文件全部开放，对于学习等访问权限，保有个人权利，禁止用于商业等。

对于软件以及硬件，包括但不限于上位机程序 板卡固件及源码 板卡工程文件等 保留所有权利
