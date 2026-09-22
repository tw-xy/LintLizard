# LintLizard 树莓派上位机（Web 控制）

浏览器打开 `http://<树莓派IP>:8080/`：左边虚拟摇杆、右上摄像头 MJPEG、右下雷达点云。
摇杆拖动即发指令，松手自动回中；指令经 `/dev/serial0` → AAT-1 → CH32 USART3。

## 键盘驾驶

点击网页后使用物理 W/A/S/D 键（大小写和中文输入法布局不影响键位）：

| 按键 | 动作 |
|---|---|
| W / S | 前进 / 后退 |
| A / D | 左转 / 右转 |
| W+A、W+D、S+A、S+D | 边走边转；对角输入归一化，避免额外提速 |
| 空格 / Esc / 停车按钮 | 回中并清除按键状态 |

相反方向同时按下互相抵消；松开全部方向键、窗口失焦、页面隐藏、触控取消都会回中。
键盘默认速度 50%，滑块可调 20%～100%，虚拟摇杆继续按拖动幅度控制。
键盘和摇杆以最后开始的操作为准，切换时清除上一输入；前台每 100ms 发送当前状态，
按住不动也持续发送。输入框和 Ctrl/Alt/Meta 快捷键不触发驾驶。

**需要配套的 CH32 手动模式固件**：Pi 发出的运动指令和 watchdog 回中均带 `"manual":1`。
`Roam_Update()` 收到该字段会解除自动巡航武装，直接输出手动方向；S=-100 正常后退，
松手不会在 1.5s 后启动巡航。ESP/旧协议省略该字段仍保留原巡航手势。
雷达避障、链路超时与电机失效保护继续生效。
HTTP 指令携带页面 session/seq，串口写入前拒绝同一页面迟到的旧序号，避免旧方向覆盖新停车。
网络请求失败后清除输入，需要重新按键才能继续。

2026-09-22 已部署并烧录校验通过；浏览器确认控件、摄像头和雷达同屏。
实车静态测试 LINK-OK、10 帧/秒、err=0、rxovf=0、roam=IDLE、电调1500/1500µs。
方向/组合键测试在模拟 DOM、PTY 和主机 C 测试中完成，未进行实车非零方向/落地测试。

## 文件

- `server.py` — 纯标准库 HTTP 服务（无 pip / 无第三方依赖）
  - `/cmd?x=&y=` 写 `{"x":-100..100,"y":-100..100,"manual":1}\n` 到串口
  - `/video` 用 `rpicam-vid` 出 MJPEG
  - `/scan` 点云 JSON，`points` 每项为 `[角度（度）, 距离（mm）]`，0° 在车头，90° 在右侧
  - `/status` 状态 JSON
  - 客户端停发 0.4s 后自动补发 `{"x":0,"y":0,"manual":1}` 兜底
- `lintlizard-web.service` — systemd 开机自启

## 部署

```sh
scp server.py                pi@<IP>:/home/pi/lintlizard-server.py
scp lintlizard-web.service   pi@<IP>:/tmp/
ssh pi@<IP> "sudo cp /tmp/lintlizard-web.service /etc/systemd/system/ && \
             sudo systemctl daemon-reload && sudo systemctl enable --now lintlizard-web"
```

## 前置条件

- **UART**：`/boot/firmware/config.txt` 末尾加 `enable_uart=1` 和 `dtoverlay=disable-bt`；
  `/boot/firmware/cmdline.txt` 去掉 `console=serial0,115200`，重启后 `/dev/serial0 -> ttyAMA0`。
- **权限**：登录用户需在 `dialout` 组（Raspberry Pi OS 默认已在）。
- **摄像头**：CSI 摄像头（实测 OV5647 可用；`rpicam-hello --list-cameras` 能看到即可）。

## 环境变量（均有默认值）

`LINT_SERIAL`(=/dev/serial0) `LINT_BAUD`(=115200) `LINT_PORT`(=8080)
`LINT_CAM_W`(=640) `LINT_CAM_H`(=480) `LINT_CAM_FPS`(=15)

## 排查

2026-09-22 实机验证通过：新固件烧录及校验成功，CH32 雷达约 9400 B/s、
168–170 包/秒，Pi 约 409 点/秒；连续 5.33s 采样显示 140–177 个点，
解析错误增量为 0。同期发送 50 条回中指令，CH32 LINK-OK、err=0、rxovf=0，
电调始终 1500/1500µs。启动初期转发丢弃 6 点，之后累计值保持不变。
浏览器已确认绿色点云与摄像头同时显示，截图见
`../docs/images/20260922_03_web_radar_live.png`。

CH32 从校验通过的雷达包中，每 8 个有效点转发 1 个到 USART3：
`R,<angle_cdeg>,<dist_mm>\n`（例如 `R,9000,1234` 表示右侧 1.234m）。
角度已经过测距修正和安装偏移修正。115200 8N1 的链路最多约 11520 B/s，
该抽样比例即使雷达输入满带宽，文本输出也不超过约 10080 B/s。
发送缓冲不足时整行丢弃，不等待、不影响避障解析；COM4 的 `[scan] sent/drop`
给出累计发送成功/丢弃点数。参数在 `App/app_config.h` 的 `RADAR_STREAM_*`。

Pi 按度保存最新点，最多 360 点；每个点 0.5s 过期，避免旧障碍残留。
`/scan` 和 `/status.radar` 包含 `online`、`age_ms`、`received`、`invalid`、`rx_bytes`。
网页每 400ms 更新，圆环为 1/2/3m，更远点可能在画布之外；此面板用于观察，未做建图。
若点云为空，先看 CH32 `[rad] rx` 是否非零，再看 `[scan] sent` 和 Pi `radar.rx_bytes`。

更新已有服务：复制 `server.py` 至 `/home/pi/lintlizard-server.py` 后重启
`lintlizard-web`。2026-09-22 部署前备份为
`/home/pi/lintlizard-server.py.before-radar-20260922`。

离线测试（不访问真实雷达和电机）：

```sh
python3 -m unittest discover -s pi -v
node pi/test_controls.js
gcc -std=c99 -Wall -Wextra -IApp -IBsp tests/radar_stream_test.c App/app_radar.c -o /tmp/radar_stream_test
/tmp/radar_stream_test
gcc -std=c99 -Wall -Wextra -IApp -IBsp tests/manual_control_test.c App/app_remote.c App/app_roam.c -o /tmp/manual_control_test
/tmp/manual_control_test
```

```sh
systemctl status lintlizard-web            # 服务状态
journalctl -u lintlizard-web -n 50         # 日志
curl -s localhost:8080/status              # 串口/摄像头状态
```
