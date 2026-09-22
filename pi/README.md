# LintLizard 树莓派上位机（Web 控制）

浏览器打开 `http://<树莓派IP>:8080/`：左边虚拟摇杆、右上摄像头 MJPEG、右下雷达点云。
摇杆拖动即发指令，松手自动回中；指令经 `/dev/serial0` → AAT-1 → CH32 USART3。

## 文件

- `server.py` — 纯标准库 HTTP 服务（无 pip / 无第三方依赖）
  - `/cmd?x=&y=` 写 `{"x":-100..100,"y":-100..100}\n` 到串口
  - `/video` 用 `rpicam-vid` 出 MJPEG
  - `/scan` 点云 JSON，`points` 每项为 `[角度（度）, 距离（mm）]`，0° 在车头，90° 在右侧
  - `/status` 状态 JSON
  - 客户端停发 0.4s 后自动补发 `{"x":0,"y":0}` 兜底
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
gcc -std=c99 -Wall -Wextra -IApp -IBsp tests/radar_stream_test.c App/app_radar.c -o /tmp/radar_stream_test
/tmp/radar_stream_test
```

```sh
systemctl status lintlizard-web            # 服务状态
journalctl -u lintlizard-web -n 50         # 日志
curl -s localhost:8080/status              # 串口/摄像头状态
```
