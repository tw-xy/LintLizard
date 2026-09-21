# LintLizard 树莓派上位机（Web 控制）

浏览器打开 `http://<树莓派IP>:8080/`：左边虚拟摇杆、右上摄像头 MJPEG、右下雷达点云。
摇杆拖动即发指令，松手自动回中；指令经 `/dev/serial0` → AAT-1 → CH32 USART3。

## 文件

- `server.py` — 纯标准库 HTTP 服务（无 pip / 无第三方依赖）
  - `/cmd?x=&y=` 写 `{"x":-100..100,"y":-100..100}\n` 到串口
  - `/video` 用 `rpicam-vid` 出 MJPEG
  - `/scan` 点云 JSON（待 CH32 转发雷达数据后填充）
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

```sh
systemctl status lintlizard-web            # 服务状态
journalctl -u lintlizard-web -n 50         # 日志
curl -s localhost:8080/status              # 串口/摄像头状态
```
