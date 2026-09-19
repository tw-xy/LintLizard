# 避坑 Checklist

> 出问题先翻这一页。每条都是「现象 → 根因 → 解决」，按"最容易踩"排序。

## A. ESP8266 连不上云 / APP 里不在线

- [ ] **车推出去了，但"跑几秒自己停"？**
      根因：**Blinker 的摇杆组件只在手指「移动」时上报数据**，按住不动是收不到任何数据的。
      早先 ESP 里写了"1 秒没收到数据就回中"的失效保护，于是把"按住不动"误判成"松手" → 主动归零。
      解决：已改成 **保持最后一次摇杆值**，只在 `!Blinker.connected()`（真的断线）时归零。
      **停车方式：把摇杆拖回中间**（有拖动就会上报 0,0）；手机/云断开 → ESP 自动停；串口异常 → CH32 的 300ms 兜底。
      教训：**别拿"多久没收到数据"当"松手"** —— 这类 App 的摇杆是"事件式"上报，不是周期上报。
- [ ] **单独供电时模块完全不启动（蓝灯不亮、串口没输出），插上烧录器却正常？**
      根因：**EN(CH_PD) 悬空**。官方 ESP-01S 原理图上 EN 有 10K 上拉（R2），但**不是所有模块都焊/都有效**——
      我们这块实测悬空就是死的。
      解决：**EN 直接接 3.3V**（或 10K 上拉到 3.3V），别悬空、更不能接 GND。
      顺带辟谣：ESP-01S 的**蓝灯接在 TXD/GPIO2 上，是数据灯不是电源灯**，别拿它判断供电；
      判断模块活没活请看串口（USB-TTL 的 RX ← ESP TX、GND 共地，看有没有数据）。
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

## F. 激光雷达 / WCH-LinkE 串口

- [ ] **EaiLidarTest 显示 `Automatic connection successful`，但一直报 `Lidar disconnection` / `Operation timed out`**
      先别怀疑雷达。用原始串口读 3 秒：如果收到 **0 字节**，说明电脑根本没接到雷达 Tx，不是解析或波特率问题。
      X2 的 4P 接口原厂定义是 **M_CTR / GND / Tx / VCC**；以 VCC 为第 1 脚往另一端数就是
      **VCC / Tx / GND / M_CTR**。第四脚是 **M_CTR 电机调速脚，不是 RXD**，卖家的 `RXD` 标注不可信。
      正确接法：雷达 `Tx → USB-UART RX`，雷达 `GND → USB-UART GND`，雷达 `VCC → 独立 5V`，
      `M_CTR → 3.3V`。能读到 `AA 55` 帧头才算通。
- [ ] **FTDI 回环测试正常，但接雷达还是 0 字节**
      FTDI 本身大概率没问题，优先怀疑信号线/脚位。换 **WCH-LinkE-R0-1v3** 的串口验证：
      Windows 识别为 `WCH-Link SERIAL (COM6)`，支持 115200/921600；把雷达 Tx 接 LinkE 的 RX、共地即可。
      WCH-LinkE 在 MounRiver 安装目录下自带 CDC 驱动：
      `...\WCH\Others\SWDTool\default\Drv_Link\WCHLinkDrv_WHQL_S.exe`。
- [ ] **EaiLidarTest 改完型号/串口后仍连错**
      配置在 `EaiLidarTest\config\config.json`，第一个 `lidars[0]` 才是默认型号。改前先退出软件；
      X2 用 `baudRate=115200`、`port` 填实际 COM 口。不要把 `crafts[0].motor.port` 设成同一个雷达串口，
      否则软件可能重复占用。改完后先点 `Start`，看是否出现点云。
- [ ] **雷达能读到数据，但避障方向不对**
      X2 的 0° 零方向是**从旋转中心指向电机/接插件凸出的那一侧**，必须把这侧朝车头。
      若实际装反 180°，在 `App/app_config.h` 把 `AVOID_FRONT_OFFSET_CDEG` 改成 `18000`。
      `front=0` 表示当前窗口没有有效前向回波，不是距离 0mm。

## G. 五分钟自检流程（怀疑哪里坏了就按这个走）

1. 插上 USB → 设备管理器看 `WCH-Link SERIAL (COM4)` 状态是否 `OK`
2. `log.ps1 -Seconds 10` → 有没有 `[stat] LINK-OK`？`frames` 在涨吗？（→ 板子在跑）
3. 推摇杆 → `[cmd]` 行的 x/y 变吗？（→ ESP 链路通）
4. 看 `1500/1500 us` 是否跟着变成 2000/2000 或 1000/1000（→ 电调输出通）
5. 万用表量 PA6/PA7 对 GND 的电压是否随之变化（→ 引脚真的在输出）
