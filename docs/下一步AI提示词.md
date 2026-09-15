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
- 源码：User/（主循环）、Bsp/（bsp_time 时基、bsp_uart 双串口、bsp_motor 电调 PWM、bsp_syscalls）、
        App/（app_remote 协议解析、app_chassis 差速解算、app_config 参数）
- SDK：仓库自带 sdk/（Core/Peripheral/Ld/Startup）
- 工具：build.ps1（编译）、flash.ps1（烧录）、log.ps1（录 COM4 日志到 logs\）
- 文档：docs/接线表.md、docs/避坑Checklist.md、docs/编译环境.md、docs/开发日志.md
- ESP 端：esp8266/ESP8266_Blinker_Joy/（账号密码在 secrets.h，不进 git）

【接线（已接好）】
- ESP8266-01S：TX→J4-1(PB10=USART3_TX)、RX→J4-3(PB11=USART3_RX)、GND→J4-7、3V3→独立 3.3V(≥500mA)
- 电调：左→J4-29(PA6=TIM3_CH1)、右→J4-31(PA7=TIM3_CH2)、两个 GND→J4-7，电调红线(BEC)悬空
- 已占用/别动：PA9/PA10(调试串口)、PA13/PA14(SWD)、PA11/PA12(USB-HS)、PB6/PB7(USB-FS+OLED I2C1)、
  PC6~PC9(内置10M以太网PHY)、PE9/PE7(用户LED)、PD0/PD1(8M晶振)

【协议】ESP→CH32：115200 8N1，一行一帧 {"x":-100..100,"y":-100..100}\n
  x 左负右正；y 后退负、前进正；只认 { 开头的行（ESP 启动乱码自动丢弃）
  电调脉宽：1500µs=停、2000µs=满前进、1000µs=满倒车（50Hz）

【已实现 / 当前进度】
- 阶段一（娱乐与遥控）✅：摇杆→ESP→协议解析→差速解算→COM4 打印，方向与失效保护实测通过
- 阶段二（电调输出）✅ 信号层：TIM3_CH1/CH2 50Hz/1000-2000µs，寄存器级验证 + 推杆实测通过
  实测：推前 2000/2000µs、推后 1001/1048µs、松手 1500/1500µs
- 待办：轮子实际转向确认（架起来看四个方向）→ 落地跑直线/原地转
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
