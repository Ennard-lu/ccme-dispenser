# 蠕动泵操控指南

## 连接方式
蠕动泵控制器与开发板的PWM端口连接，位于/sys/class/pwm/pwmchip[x]中, 频率越快，蠕动泵转速越快。
同时控制器的dir端口连接在开发板的gpio端口中，请保持其电位工作正常。

## 转动方向

每个泵的 dir GPIO 有效电平由 CMake 选项控制：

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `CCME_PUMP0_DIR_ACTIVE_LEVEL` | `1` | 一号泵（注水泵）运行时 dir GPIO 电平 |
| `CCME_PUMP1_DIR_ACTIVE_LEVEL` | `0` | 二号泵（分装泵）运行时 dir GPIO 电平 |

泵停止时会写入相反电平。若现场接线导致方向仍然相反，只需修改对应选项并重新编译，无需改代码：

```bash
cmake -S . -B build \
    -DCCME_PUMP1_DIR_ACTIVE_LEVEL=1 \
    -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake
```
