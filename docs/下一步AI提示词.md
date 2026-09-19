# 下一步 AI 提示词

> 用途：以后开新对话继续做这个项目时，**把第 1 节整段粘给 AI**，它就有完整上下文；
> 第 2 节是常见任务可以直接抄的句子。做完一个阶段记得回来更新第 1 节的"当前进度"。

## 1. 固定上下文（每次开新对话先粘这段）

```
我的板子：CH32V307V-EVT 评估板（丝印 CH32F&V30xV-R1-1v1，主控 CH32V307VCT6，板载 WCH-Link）

【环境，都已装好，直接用，不用问我】
- IDE：MounRiver Studio 2 → D:\MounRiverStudio\MounRiver_Studio2
- 工具链：...\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC\bin
  （riscv-none-embed-gcc 8.2.0）
- 烧录：...\WCH\OpenOCD\OpenOCD\bin\openocd.exe + 同目录 wch-riscv.cfg（WCH-LinkE，SDI 两线）
- 串口：COM4 @115200（板载 WCH-Link 透传的目标串口 USART1，你可以直接读写）
- Arduino IDE 2.3.10 + esp8266 包 3.1.2 + Blinker 库 0.3.10230510（在 C:\Users\hp\Documents\Arduino\libraries\Blinker）
- arduino-cli 在 D:\Arduino IDE 2.3.10\resources\app\lib\backend\resources\arduino-cli.exe
- 代理：ClashVerge 127.0.0.1:7897（Arduino CLI 已经在 %LOCALAPPDATA%\Arduino15\arduino-cli.yaml 里配了 network.proxy）

【项目】履带小车，仓库 D:\WCH_CH32V307_EVT\projects\CAR_REMOTE（也是 git 仓库）
- 源码：User/（主循环时间片）、Bsp/（bsp_time、bsp_uart 三串口、bsp_motor 电调、bsp_fan 风机、bsp_oled、bsp_syscalls）、
        App/（app_remote 协议、app_chassis 差速、app_cleaner 扫地、app_radar 雷达解析、app_avoid 避障、app_ui 界面、app_config 参数）
- SDK：仓库自带 sdk/（Core/Peripheral/Ld/Startup）
- 工具：build.ps1（编译）、flash.ps1（烧录）、log.ps1（录 COM4 日志到 logs\）
- 文档：docs/接线表.md、docs/避坑Checklist.md、docs/编译环境.md、docs/开发日志.md
- ESP 端：esp8266/ESP8266_Blinker_Joy/（账号密码在 secrets.h，不进 git）

【接线（已接好）】
- ESP8266-01S：TX→J4-1(PB10=USART3_TX)、RX→J4-3(PB11=USART3_RX)、GND→J4-7、3V3→独立 3.3V(≥500mA)
- 电调：左→J4-29(PA6=TIM3_CH1)、右→J4-31(PA7=TIM3_CH2)、两个 GND→J4-7，电调红线(BEC)悬空
- 激光雷达：X2 Tx→J3-34(PA3=USART2_RX)、GND→板子 GND、VCC→独立5V、M_CTR→3.3V；0°正前方=电机/接插件侧朝车头
- OLED：SCL→J4-22(PB6)、SDA→J4-20(PB7)、VCC→3.3V、GND→J4-7
- 扫地：边刷 EN→J3-43(PE8)/J3-45(PE10)，风机 PWM→J4-25(PB8/TIM4_CH3)
- 已占用/别动：PA9/PA10(调试串口)、PA13/PA14(SWD)、PA11/PA12(USB-HS)、PB6/PB7(USB-FS+OLED I2C1)、
  PC6~PC9(内置10M以太网PHY)、PE9/PE7(用户LED)、PD0/PD1(8M晶振)

【协议】ESP→CH32：115200 8N1，一行一帧 {"x":-100..100,"y":-100..100}\n
  x 左负右正；y 后退负、前进正；只认 { 开头的行（ESP 启动乱码自动丢弃）
  电调脉宽：1500µs=停、2000µs=满前进、1000µs=满倒车（50Hz）

【已实现 / 当前进度】
- 阶段一（遥控）✅：摇杆→ESP→协议→差速解算，方向全对，48帧/秒
- 阶段二（电调）✅：TIM3_CH1/CH2 50Hz/1000-2000µs，轮子转向已确认
- 阶段三（OLED）✅：SSD1306 软 I2C，无网络转圈/已连接/方向箭头
- 阶段四（扫地执行机构）✅：边刷 EN + 涡轮风机 18kHz 缓启动，实测通过
- 阶段五（激光雷达）✅：CH32 侧约 9400 B/s、168 包/秒；PC EaiLidarTest 点云 7.8Hz；
  前向避障实测：1m 满速、45-55cm 降速、≤30cm 停车，有障碍倒车/原地转仍可用，雷达失效前进锁定
- 待办：落地跑直线/原地转（含避障）→ 雷达上树莓派跑 YDLidar-SDK/建图 → 避障阈值精调
- 架构：非阻塞时间片（BSP_Millis + BSP_Every），串口全中断+环形缓冲，无任何 Delay_Ms
  三层失效保护：ESP 端 1s 无数据回中 → 链路 300ms 判离线 → 底盘 300ms 输出归零

【以后的规划】扫地（主扫/吸尘/边刷）、自动回充（红外寻桩+触点）、
  智能导航（GPS/罗盘/ToF/超声波）、OLED/WS2812/ESP32-CAM 图传 —— 都要模块化加，别破坏现有分层

【约定】
1. 烧录会覆盖板子当前程序，烧之前先说一声
2. 需要接线时给我"板子端引脚 + 模块端引脚"的接线表；拿不准我会拍照给你判断
3. 每完成一个阶段：更新文档 + git 提交
```

## 2. 常见任务，直接抄

| 想干的事 | 直接这么说 |
|---|---|
| 继续开发 | 「按 docs/下一步AI提示词.md 的上下文，我们开始做 XXX」 |
| 加新功能 | 「在现有架构上加 XXX（比如编码器闭环/PID），先给我接线表，再改代码，编译烧录验证」 |
| 查问题 | 「现象是 XXX，log.ps1 录到的日志在 logs\xxx.log，帮我查」 |
| 看波形/总线 | 「用 openocd 读一下 TIM3 的寄存器，确认 XXX」 |
| 恢复出厂 | 「把出厂固件 D:\WCH_CH32V307_EVT\backup_factory_20260911.bin 刷回去」 |
| 调参数 | 「把死区改成 X、指数曲线改成 Y，重编译烧录」 |
| 提交仓库 | 「把这次的改动提交到 GitHub，写清楚改了什么的 commit message」 |

## 3. 维护提醒

每完成一个里程碑，回来更新这几处：

* 本文件第 1 节的「已实现 / 当前进度」和「接线（已接好）」
* `docs/开发日志.md` 的版本记录表
* `README.md` 的「当前进度」表
* 有新踩的坑 → 追加到 `docs/避坑Checklist.md`

## 4. 硬件采购待办

### 4.1 编码器（暂缓购买）

目标：给左右履带各加一个增量式编码器，用于 `/odom` 和闭环控制。

购买要求：

* 类型：AB 两相正交增量式，最好带 Z 索引；不要单通道测速传感器。
* 数量：左右各一个，同型号。
* 分辨率：电机轴 11–20 PPR（配减速箱）或输出轴 500–2000 CPR。
* 电平：3.3V 优先；5V 输出必须电平转换到 3.3V。
* 接口：VCC / GND / A / B 至少四线，开漏输出需要 3.3V 上拉。
* 频率：`CPR × RPM / 60`，最好低于 100kHz。
* 安装：优先换“带霍尔编码器的减速电机”；否则用 MT6701 / TLE5012B / AS5047P 磁编码器 + 径向磁铁 + 支架。

### 4.2 IMU（暂缓购买）

目标：提供稳定的姿态/yaw，和编码器一起组成可靠 `/odom`。

购买要求：

* 优先 BNO085 / BNO086：板载融合，最省事。
* 性价比方案：ICM42688-P + IST8310/QMC5883L，需要自己滤波和标定。
* 接口：I2C / SPI，3.3V 逻辑。
* 输出率：≥100Hz。
* 安装：车体中心附近刚性固定，加减震，远离电机和动力线。
* 不建议裸 MPU6050 作为最终建图/Nav2 的 IMU。

### 4.3 扫地执行机构电机

优先找扫地机器人拆机件：

* 主刷/滚刷：12V/24V 减速电机优先，确认电压、电流、转速、减速比、轴径和安装孔位。
* 边刷：小型减速电机，确认电压和驱动方式。
* 风机：已有涡轮风机，暂时不重复采购。
* 购买前先确认是“自带驱动”还是“需要外部电调/驱动板”。
