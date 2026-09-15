# 出厂固件备份

| 项目 | 值 |
|---|---|
| 文件 | `backup_factory_20260911.bin` |
| 来源 | 板子到手后（2026-09-11）用 openocd 从 CH32V307V-EVT 读出的整片 flash |
| 大小 | 294912 字节（288 KB，与芯片 flash 容量一致） |
| SHA256 | `85858166E930500259E78F408450DC05E45CA7922658EAB5BFB27DFEACD41AF3` |

## 什么时候用

* 板子跑着我们自己烧的程序，想回到出厂状态
* 想对照出厂程序的某个行为（比如某个外设默认怎么配的）

## 怎么恢复（⚠️ 会覆盖板子上的程序）

```powershell
$OPENOCD = 'D:\MounRiverStudio\MounRiver_Studio2\resources\app\resources\win32\components\WCH\OpenOCD\OpenOCD\bin\openocd.exe'
$CFG     = 'D:\MounRiverStudio\MounRiver_Studio2\resources\app\resources\win32\components\WCH\OpenOCD\OpenOCD\bin\wch-riscv.cfg'

# 注意：-c 后面是 Tcl 脚本，Windows 路径必须用正斜杠（反斜杠会被当转义吃掉）
$BIN = (Join-Path $PSScriptRoot 'backup_factory_20260911.bin').Replace('\','/')

& $OPENOCD -f $CFG -c "program $BIN verify reset exit"
```

也可以直接用 MounRiver Studio 的 Download 功能，或用 WCHISPTool 刷这个 `.bin`。

## 校验文件没坏

```powershell
(Get-FileHash .\backup\backup_factory_20260911.bin -Algorithm SHA256).Hash
# 应该等于 85858166E930500259E78F408450DC05E45CA7922658EAB5BFB27DFEACD41AF3
```

## 说明

* 这是**本机这块板子**的出厂固件，只对本机 CH32V307V-EVT 有意义（别人的板子不保证一样）。
* 除了仓库里这一份，建议在云盘或另一块硬盘再放一份——这份丢了就找不回来了。
