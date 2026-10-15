# ST-Link 烧录故障说明

## 正常烧录步骤

1. 在 Keil 中按 `F7` 编译。
2. 确认 Build Output 为 `0 Error(s), 0 Warning(s)`。
3. 只按一次 `F8`（Download）。
4. 看到下面四行就表示本次烧录已经成功：

```text
Erase Done.
Programming Done.
Verify OK.
Flash Load finished
```

如果只是烧录运行，不需要再点击 `Start/Stop Debug Session`。

## 本项目已经采用的稳定设置

- Debug Adapter：`ST-LINK/V2`
- Port：`SW`
- SWD 请求时钟：`1 MHz`（实际约 `950 kHz`）
- `Load Application at Startup`：关闭
- `Update Target before Debugging`：关闭

`No target connected` 是根错误；随后出现的 `Flash Download failed - Target DLL has been cancelled` 只是连锁提示，不是第二个独立故障。

## 再次出现时检查

- ST-Link `SWDIO` 接 STM32 `PA13`
- ST-Link `SWCLK` 接 STM32 `PA14`
- 两边 `GND` 必须共地
- `BOOT0` 保持低电平
- 建议把 ST-Link 的 `NRST/RST` 接到开发板 `NRST/R`
- 若没有接 NRST，可在 `Options for Target -> Debug -> Settings` 中把 Connect 改为 `Normal`
- 传感器较多时使用稳定的 3.3 V 电源，不要让廉价 ST-Link 的 3.3 V 引脚承担整套模块供电

Build Output 会保留以前的错误行；判断是否成功时看最底部、时间最新的一组结果。
