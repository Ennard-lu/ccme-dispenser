# D-Bus 接口规范

所有后端模块通过 system bus 通信。服务名、对象路径、接口名和错误名统一定义在
`include/dbus/ccme_dbus.h` 的 `ccme::dbus` 命名空间中，服务和客户端都必须使用这些常量。

## 命名规则

| 项目 | 规则 | 示例 |
| --- | --- | --- |
| Bus name | `org.ccme.<Module>` | `org.ccme.Camera` |
| Object path | `/org/ccme/<module>` | `/org/ccme/camera` |
| Interface | `org.ccme.<Module>.Control` | `org.ccme.Camera.Control` |
| Error | `org.ccme.<Module>.Control.Error` | `org.ccme.Camera.Control.Error` |

`ccme::dbus::Client` 封装了同步方法调用，返回 `std::expected<T, ccme::dbus::Error>`，
不再在各模块中重复拼接服务名或直接处理 sdbus-c++ 异常。服务端使用
`ccme::dbus::Connect()`、`ccme::dbus::CreateObject()` 和
`ccme::dbus::ThrowError()` 完成注册与错误上报。

## 服务一览

`ccme-web` 只作为 D-Bus 客户端，不再占用 `org.ccme.Web` 服务名。

### org.ccme.Pump

对象路径：`/org/ccme/pump`，接口：`org.ccme.Pump.Control`

| 方法 | 参数 | 返回 |
| --- | --- | --- |
| `Start` | `double volume_ml` | `bool` |
| `Stop` | - | `bool` |
| `IsRunning` | - | `bool` |
| `Start2` | `double volume_ml` | `bool` |
| `Stop2` | - | `bool` |
| `IsRunning2` | - | `bool` |

### org.ccme.Stirrer

对象路径：`/org/ccme/stirrer`，接口：`org.ccme.Stirrer.Control`

| 方法 | 参数 | 返回 |
| --- | --- | --- |
| `StartStir` | `int speed_rpm` | `bool` |
| `StopStir` | - | `bool` |
| `IsStirring` | - | `bool` |
| `StartHeat` | `double temp_c` | `bool` |
| `StopHeat` | - | `bool` |
| `IsHeating` | - | `bool` |
| `GetSpeed` | - | `int` |
| `GetSetTemp` | - | `double` |
| `GetActualSpeed` | - | `int` |
| `GetActualTemp` | - | `double` |
| `GetExternalTemp` | - | `double` |

### org.ccme.Camera

对象路径：`/org/ccme/camera`，接口：`org.ccme.Camera.Control`

| 方法 | 参数 | 返回 | 说明 |
| --- | --- | --- | --- |
| `Connect` | - | `bool` | 打开视频流 |
| `Disconnect` | - | - | 关闭视频流 |
| `IsConnected` | - | `bool` | 视频流是否已打开 |
| `IsAiConfigured` | - | `bool` | 云端 AI 地址与模型是否已配置 |
| `CheckDissolution` | - | `bool` | 截取若干帧并调用云端 AI 判断是否溶解完全 |

`CheckDissolution` 的 D-Bus 调用超时应不小于 `CCME_AI_TIMEOUT_SEC + 30` 秒；
编排器已按此设置。

### org.ccme.FMC

对象路径：`/org/ccme/fmc`，接口：`org.ccme.FMC.Control`

| 方法 | 参数 | 返回 | 说明 |
| --- | --- | --- | --- |
| `Connect` | - | `bool` | 连接 FMC4030 控制器 |
| `IsConnected` | - | `bool` | 控制器是否已连接 |
| `Home` | - | `bool` | 三轴回零，编排器使用 220s 超时 |
| `MoveTo` | `double x, double y` | `bool` | 移动到绝对坐标 |
| `MoveToVial` | `int index` | `bool` | 移动到指定小瓶，编排器使用 620s 超时 |
| `IsMoving` | - | `bool` | 任一轴是否正在运动/回零 |

### org.ccme.Orchestrator

对象路径：`/org/ccme/orchestrator`，接口：`org.ccme.Orchestrator.Control`

| 方法 | 参数 | 返回 |
| --- | --- | --- |
| `Start` | `double volume_ml, int stir_speed_rpm, double heat_temp_c, double dispense_volume_ml` | `bool` |
| `Stop` | - | `bool` |
| `ClearError` | - | `bool` |
| `GetStatus` | - | `string`（JSON） |
| `GetState` | - | `string` |
| `GetCurrentVial` | - | `int` |
| `GetTotalVials` | - | `int` |

`GetStatus` 返回的 JSON 示例：

```json
{
  "state": "error",
  "current_vial": 0,
  "total_vials": 36,
  "error": {
    "active": true,
    "code": "fmc_home_failed",
    "message": "三轴回零失败: ...",
    "hint": "检查限位开关、机械结构和 FMC 状态",
    "module": "fmc",
    "stage": "homing",
    "recoverable": true,
    "timestamp_ms": 1760000000000
  }
}
```

没有错误时 `error` 为 `null`。前端每 500ms 读取该状态并展示错误横幅。

`ClearError` 的恢复语义：

- 流程处于 `error` 或 `complete`：停止所有执行器、清空错误并回到 `idle`
- 流程处于 `idle`：只清空历史错误
- 流程正在运行：返回错误 `not_in_error`，必须先停止

`Start` 在 `error` 状态下是允许的，会先清理错误再重新开始，因此用户修复外部问题后无需重启服务。

工作流状态字符串：

`idle`、`homing`、`injecting_water`、`heating`、`stirring`、
`checking_dissolution`、`moving_to_vial`、`dispensing`、`complete`、`error`。

## 调用关系

```
ccme-web (HTTP) ──Start/Stop──> ccme-orchestrator
                                      │
        ┌─────────────┬───────────────┼──────────────┬──────────────┐
        ▼             ▼               ▼              ▼              ▼
   ccme-pump     ccme-stirrer     ccme-camera     ccme-fmc       (状态查询)
```

`ccme-web` 每 500ms 查询一次各模块状态，用于前端展示；控制指令只发送给编排器。

## 权限

`dbus/org.ccme.conf` 允许任意用户拥有上述服务名，并允许向这些服务发送方法调用。
新增服务时需要同步更新该文件以及 `include/dbus/ccme_dbus.h`。
