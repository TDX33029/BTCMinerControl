# BTCMinerControl / BTCMinerMS 改动记录

日期：2026-07-29

## 备份

- 修改前完整备份：`D:\Document\tempPrj\BTCMinerControl\Backups\pre-web-auth-port-detection-20260729-025746`
- 备份包含源码、配置和工程文件；排除了可重新生成的编译中间目录。

## 开发板固件（BTCMinerMS）

- 目标 MCU：STM32F107VCT6；工具链固定为 ARMCC V5.06 update 7。
- 新增 TPS546D24A PMBus/I2C 支持：探测、输出开关、输入/输出遥测、功率及 VR 温度状态。
- 新增 TMP1075 I2C 支持：地址探测与温度采集。
- 扩展板卡网络协议：HELLO、遥测、TPS 电源控制和延迟探测帧。
- 固件版本更新为 2.4。
- TPS546D24A/TMP1075 缺失日志只在状态变化时输出；连续缺失不再周期刷屏，重新插入或掉线仍会各报告一次。
- 移除 USART2 的 LOOP/loop cnt 高频输出。
- 主循环不再调用 `HAL_Delay(1)`；以太网持续轮询，降低 ICMP 和控制命令延迟。
- 未安装 BM1366 时仍可完成以太网、I2C、上位机协议和界面联调。

## Windows 上位机（HostServices）

- 板卡管理界面改为浅色、方框、早期桌面软件风格。
- 板卡信息改为横向列表/表格；状态终端位于页面下方约 1/3 区域，保持宽屏且不自动换行。
- 表格只保留明确的功率和温度字段；器件或遥测不存在时显示 `N/A`。
- 每块板卡提供 TPS 输出开关，默认显示 ON；命令状态在响应期间保持，不会被轮询错误覆盖。
- 使用 Lucide `gauge` SVG 作为延迟测试图标；按钮无边框、靠近延迟单元格右侧，测试期间按钮和图标均不旋转。
- 支持单板延迟探测、顶部全部板卡探测，以及按配置周期自动探测所有在线板卡。
- uptime 使用服务端基准加浏览器本地计时，每 250 ms 平滑刷新；统计数据每秒刷新。
- 只在终端记录上线、离线、故障、电源状态等必要事件，格式为 `yyyy:mm:dd hh:mm:ss.SSS`。
- 配置新增板卡监听端口、Web 监听端口和板卡检测周期；检测周期立即生效，监听端口重启后生效。
- 配置新增矿池管理地址，页面提供跳转链接。
- Web 登录使用浏览器原生 HTTP Basic Auth。
- 用户名和密码可在 `/account` 修改；配置中仅保存 Windows DPAPI 加密后的 `dashboard_credentials`，不保存明文凭据。
- `/settings` 和 `/account` 均受相同认证保护。

## 关键验证结果

- VS/MSBuild x64 Release：编译成功。
- 上位机 `--self-test`：全部通过，包括协议、遥测、TPS 控制、延迟帧、TCP 分片/合包、多板任务隔离和后台周期探测。
- 隔离 Web 联调：HTTP 未认证返回 401；两块模拟板在线；单板/全部/自动延迟、TPS OFF/ON、设置持久化、账号页、N/A 和时间戳均通过。
- ARMCC V5 全量重建：`Code=42072 RO-data=1516 RW-data=296 ZI-data=30728`，0 errors / 0 warnings。
- J-Link：STM32F107VC、JTAG 5 MHz、S/N 308622870；Flash 内容核对一致并复位运行。
- COM7 115200：确认 FW 2.4、TCP 连接、HELLO 和任务接收正常；35 秒内 TPS/TMP 缺失各输出一次。

## 说明

- J-Link ARM V8 在程序已运行后的断开阶段偶尔报告 `Bad JTAG communication`；Flash compare/load 成功且板卡随后通过串口和 TCP 正常运行，因此该信息属于退出阶段的旧探针通信告警。
- 当前测试板没有 TPS546D24A、TMP1075 和 BM1366，Web 显示 `N/A`、串口首次报告未检测到属于预期测试状态。
