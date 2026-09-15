# 避坑 Checklist

> 出问题先翻这一页。每条都是「现象 → 根因 → 解决」，按"最容易踩"排序。

## A. ESP8266 连不上云 / APP 里不在线

- [ ] **是不是拿 USB-TTL 的 3.3V 给 ESP 供电？**
      现象：APP 永远不在线、串口全是乱码、每隔几秒重启一次。
      根因：ESP8266 连 WiFi 瞬间要 **300–500mA**，USB-TTL 的 3.3V 只有几十 mA。
      解决：独立 3.3V（5V + AMS1117-3.3，≥500mA），模块脚上并 **470µF + 100nF**。
- [ ] **WiFi 是不是 5GHz？** ESP8266 **只支持 2.4GHz**。SSID 带 `-5G` 的一律连不上。
- [ ] **密码对不对**（注意结尾的特殊字符，比如 `.`）。
- [ ] **APP 里设备是不是"WiFi 接入"类型**？（建成了蓝牙设备就用 WiFi 代码连不上）
- [ ] **摇杆组件的键名是不是 `joy`**？（要和代码里 `joyKey` 一致，否则回调永远不触发）
- [ ] 都没问题还是连不上 → 在 sketch 里加 `BLINKER_DEBUG.stream(Serial);` 看 ESP 自己的日志。

## B. 串口相关

- [ ] **COM4 全是乱码，换什么波特率都不对**
      根因：WCH-Link 的串口桥没跟目标对上（**波特率本身没错**：57600~921600 全扫过，只有 115200 能出正常文字）。
      解决：**复位一次**（板上 S3 或让 openocd reset 一次）即恢复。
      判定方法：用 openocd 把 CPU halt 住 → 数据立刻归零，说明数据确实来自芯片、不是别的线在抢。
- [ ] **COM4 突然消失，设备管理器里状态是 `Unknown`**
      根因：板子断电 / USB 掉枚举。解决：插回 USB（板子下电时 COM4 消失是正常现象）。
- [ ] **串口是空的、`rx=0`**
      查三件事：ESP 有没有供电、TX/RX 有没有交叉接反、GND 有没有共地。
- [ ] **开机先吐一段乱码** —— 正常，那是 ESP8266 ROM 用 74880 波特率打的启动信息，协议解析器会自动丢掉。

## C. 烧录 / 工具链

- [ ] **openocd 报 `couldn't open D:WCH_CH32...`**（路径里的反斜杠全没了）
      根因：openocd 的 `-c` 参数由 **Tcl 解析**，Windows 路径的反斜杠被当转义（`\b` 变成退格）。
      解决：路径写**正斜杠** `D:/.../CAR_REMOTE.elf`。
- [ ] **脚本一调用 openocd 就抛 `NativeCommandError`**
      根因：openocd 把 banner 写到 **stderr**，而脚本设了 `$ErrorActionPreference='Stop'`。
      解决：调用前后临时设 `'Continue'`，用 `$LASTEXITCODE` 判断成败。
- [ ] **`.ps1` 报"字符串未终止 / 意外标记"**
      根因：Windows PowerShell 5.1 把**无 BOM 的 UTF-8 .ps1 当 GBK 解**，中文串会把引号吃掉。
      解决：脚本里的字符串只用 ASCII（本仓库的 .ps1 已全部改成 ASCII）。
- [ ] **命令行里给原生程序传 Windows 路径，反斜杠被吃掉**
      解决：尽量用 `powershell -File 脚本.ps1` 调用，路径在脚本内部处理。
- [ ] **Arduino 开发板管理器装 ESP8266 包总是超时**
      根因：**Arduino CLI 不读系统代理，也不读 `HTTP_PROXY` 环境变量**，直连 GitHub 被打断。
      解决：写入 `%LOCALAPPDATA%\Arduino15\arduino-cli.yaml`：
      ```yaml
      network:
        proxy: http://127.0.0.1:7897   # 换成你自己的代理端口
      ```
- [ ] **ESP 上传报 `Failed to connect to ESP8266`**
      查：IO0 有没短到 GND、供电够不够、端口选的是 USB-TTL 那个 COM 口（**不是 COM4**）。

## D. 代码 / 编译

- [ ] **`char auth[] = "xxx;` 少一个引号 → 后面所有行都报错**（这是最容易犯的编译错误）
- [ ] **Blinker 库编译时刷一屏 warning**（`-Wwrite-strings`、`WiFiServer::available` 已废弃）→ 库自身问题，忽略；
      我们自己的文件必须 0 warning。
- [ ] **改了 `app_config.h` 里的参数没生效** → 要重新 `build.ps1` + `flash.ps1`。
- [ ] **MounRiver 打开工程报找不到 SDK** → CLI 编译用仓库自带 `sdk/`，IDE 工程链接的是本机官方 EVT 路径。

## E. 电机 / 电调

- [ ] **电调一直在叫 / 不动** → 检查是否共地、上电顺序（先板子后电池）、脉宽是否在 1000–2000µs。
- [ ] **某个轮子反了** → 改 `app_config.h` 的 `MOTOR_LEFT_INVERT` / `MOTOR_RIGHT_INVERT` 为 1 重烧（比调线干净）。
- [ ] **第一次测试务必把履带架起来（轮子悬空）**，确认方向再落地。
- [ ] **电调红线（BEC）绝对不要接进板子**。

## F. 五分钟自检流程（怀疑哪里坏了就按这个走）

1. 插上 USB → 设备管理器看 `WCH-Link SERIAL (COM4)` 状态是否 `OK`
2. `log.ps1 -Seconds 10` → 有没有 `[stat] LINK-OK`？`frames` 在涨吗？（→ 板子在跑）
3. 推摇杆 → `[cmd]` 行的 x/y 变吗？（→ ESP 链路通）
4. 看 `1500/1500 us` 是否跟着变成 2000/2000 或 1000/1000（→ 电调输出通）
5. 万用表量 PA6/PA7 对 GND 的电压是否随之变化（→ 引脚真的在输出）
