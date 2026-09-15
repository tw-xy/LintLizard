# LintLizard —— 履带扫地机器人 · 固件仓库

[![build](https://github.com/tw-xy/LintLizard/actions/workflows/build.yml/badge.svg)](https://github.com/tw-xy/LintLizard/actions/workflows/build.yml)

> **LintLizard** 是我给这台履带扫地机器人起的名字：**Lint**（毛絮、灰尘）+ **Lizard**（蜥蜴）——
> 一只贴着地面爬来爬去、专门吃掉毛絮的蜥蜴。
>
> 本仓库是它的**固件与文档**，会跟着功能一起长大。目前处于
> **第一阶段：遥控底盘**；后面依次加入扫地、自动回充、避障导航、状态显示。

```
手机 Blinker 摇杆 → ESP8266-01S → CH32V307 → 标准差速转向 → 双有刷电调 → 履带底盘
```

板子：**CH32V307V-EVT**（丝印 `CH32F&V30xV-R1-1v1`，主控 CH32V307VCT6，板载 WCH-Link）

> 仓库名对应的是**整机**（LintLizard），不是当前阶段，所以以后加功能不用改名；
> 要区分版本时用 `LintLizard-v1` 这种形式。

## 当前进度

| 阶段 | 状态 |
|---|---|
| 一、娱乐与遥控（摇杆 → 串口协议 → 差速解算 → COM4 打印） | ✅ 已完成并实测 |
| 二、电调输出（TIM3 双路 50Hz / 1000–2000µs PWM） | ✅ 信号层已实测；**轮子实际转向待确认** |
| 三、编码器闭环 / 扫地 / 回充 / 避障 | 未开始（架构已预留接口，见下） |

## 目录结构

```
User/  Bsp/  App/        CH32V307 固件源码
  User/main.c            主循环时间片调度
  Bsp/bsp_time.c         TIM2 1ms 时基 + 软定时器
  Bsp/bsp_uart.c         USART1 调试口 / USART3 遥控口（中断 + 环形缓冲）
  Bsp/bsp_motor.c        TIM3_CH1/CH2 → 两路电调 PWM
  App/app_remote.c       行协议解析 + 链路超时
  App/app_chassis.c      差速解算（换底盘只动这里）
  App/app_config.h       全部可调参数
sdk/                     随仓库自带的一份 WCH 官方库（Core/Peripheral/Ld/Startup）
esp8266/ESP8266_Blinker_Joy/   ESP8266 端 Arduino 工程
docs/                    接线表 / 避坑清单 / 编译环境 / 开发日志 / AI 提示词
build.ps1 flash.ps1 log.ps1    编译 / 烧录 / 串口记录
```

## 五分钟上手

```powershell
cd <本仓库目录>

# 1. 编译（输出 build\CAR_REMOTE.elf / .hex / .bin）
powershell -ExecutionPolicy Bypass -File .\build.ps1

# 2. 烧录（WCH-LinkE，会覆盖板子上现有程序）
powershell -ExecutionPolicy Bypass -File .\flash.ps1

# 3. 看板子在说什么（COM4 @115200，存到 logs\serial_*.log）
powershell -ExecutionPolicy Bypass -File .\log.ps1 -Seconds 60
```

正常应该看到：

```
[cmd ] x=  +0 y=  +0 | L=   +0 R=   +0 | 1500/1500 us | ok
[stat] LINK-OK  frames=2971 (48/s) err=0 rx=41594 rxovf=0 txdrop=0
```

ESP8266 端：用 Arduino IDE 打开 `esp8266/ESP8266_Blinker_Joy/`，
把 `secrets.h.example` 复制成 `secrets.h` 填上 WiFi / Blinker Secret Key（该文件已被 git 忽略），
开发板选 `Generic ESP8266 Module` / `1MB (FS:none OTA:~502KB)` / 26MHz / 80MHz。

## 硬件接线

见 [docs/接线表.md](docs/接线表.md)：板子 ↔ ESP8266、板子 ↔ 电调、板子 ↔ USB-TTL，
以及**板子上已经被占用的引脚清单**（接新东西前先看这张表）。

## 文档

| 文档 | 用途 |
|---|---|
| [docs/接线表.md](docs/接线表.md) | 全部接线 + 电源拓扑 + 已占用引脚 |
| [docs/避坑Checklist.md](docs/避坑Checklist.md) | 出问题先翻这个（串口乱码/连不上云/烧不进…） |
| [docs/编译环境.md](docs/编译环境.md) | 需要装什么、什么版本、怎么验证、怎么排错 |
| [docs/开发日志.md](docs/开发日志.md) | 完整开发记录：协议、算法、实测数据、踩坑过程 |
| [docs/下一步AI提示词.md](docs/下一步AI提示词.md) | 开新对话继续开发时，直接粘给 AI 的上下文 |

## 设计要点

* **非阻塞**：`BSP_Millis()` + `BSP_Every()` 时间片；串口收发全中断 + 环形缓冲；**没有任何 `Delay_Ms()` 死等**
* **三层失效保护**：ESP 端 1s 无摇杆数据回中 → 链路 300ms 无有效帧判离线 → 底盘 300ms 无新指令输出归零
* **协议可扩展**：`{"x":-100,"y":100}\n` 一行一帧，只认 `{` 开头的行，多余的键随便加
* **归一化差速**：`l=y+x, r=y-x` 后等比缩放，保证"斜着推"时转向比例不失真

## 后续扩展接口

| 功能 | 加在哪 | 接口 |
|---|---|---|
| 编码器测速 / PID | `Bsp/bsp_encoder.c` + `App/app_chassis.c` | 复用 `Chassis_Output_t` |
| 扫地（主扫/吸尘/边刷） | `App/app_cleaner.c` | 新增 PWM 通道 |
| 自动回充（红外 + 触点） | `App/app_charge.c` | 调 `Chassis_SetInput()` |
| ToF/超声波避障 | `App/app_avoid.c` | 在 `main.c` 里与遥控做仲裁 |
| OLED / WS2812 | `App/app_ui.c` | 只读状态，不阻塞 |

## 说明

* `sdk/` 为 WCH 官方 CH32V30x 外设库（随 EVT 包分发），版权归南京沁恒微电子所有，此处仅为便于编译而随仓库附带。
* 本仓库不包含任何 WiFi 密码或设备密钥。
