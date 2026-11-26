# Web API 与错误处理

`ccme-web` 在后端模块和前端之间转发控制指令，并缓存各模块状态。所有错误都以结构化 JSON 返回，前端会显示错误横幅并提供恢复操作。

## 状态接口

### `GET /api/status`

返回编排器状态、样品进度、当前错误和各模块在线状态：

```json
{
  "state": "error",
  "current_vial": 0,
  "total_vials": 36,
  "ai_check_skipped": false,
  "error": {
    "active": true,
    "code": "camera_stream_unavailable",
    "message": "摄像头视频流不可用: ...",
    "hint": "检查摄像头连接和 GStreamer 推流脚本",
    "module": "camera",
    "stage": "checking_dissolution",
    "recoverable": true,
    "timestamp_ms": 1760000000000
  },
  "modules": {
    "pump":  { "online": true,  "running": false, "error": "" },
    "pump2": { "online": true,  "running": false, "error": "" },
    "stirrer": {
      "online": true, "stirring": false, "heating": false,
      "speed": 0, "set_temp": 0.0, "temp": 25.0, "error": ""
    },
    "camera": {
      "online": true, "connected": true,
      "ai_configured": true, "error": ""
    },
    "fmc": {
      "online": true, "connected": true, "moving": false, "error": ""
    }
  }
}
```

- `error` 为 `null` 表示当前没有错误。
- `ai_check_skipped` 为 `true` 表示已通过 `CCME_SKIP_AI_CHECK` 跳过 AI 溶解检测，工作流会直接进入分装。
- 模块 `online: false` 时，`error` 包含对应的 D-Bus 错误信息。
- 编排器不可用时，`error.code` 为 `orchestrator_unavailable`。
- 前端每 500ms 轮询一次该接口，因此错误通常在一个轮询周期内显示。

## 控制接口

### `POST /api/start`

请求体：

```json
{
  "volume_ml": 100,
  "stir_speed_rpm": 200,
  "heat_temp_c": 50,
  "dispense_volume_ml": 10
}
```

- `200`：流程已启动
- `400`：请求 JSON/参数错误（`invalid_request`）
- `409`：流程已在运行（`already_running`）
- `500/503`：启动失败或编排器不可用

### `POST /api/stop`

- `200`：已停止
- `409`：流程未运行（`not_running`）
- `500/503`：停止失败

### `POST /api/reset`

清除锁存的错误并回到空闲状态：

- `200`：已重置
- `409`：流程仍在运行，需先停止（`not_in_error`）
- `500/503`：重置失败

该接口会调用编排器的 D-Bus `ClearError`，停止加热、搅拌和泵，并断开摄像头。

## 浏览器断连与恢复

- 关闭浏览器不会停止 `ccme-dispenser` 中的工作流；控制流程运行在独立进程，不依赖浏览器连接。
- `/api/hls/stream` 和 `/api/stream` 会在客户端断开后关闭本地流连接并释放 HTTP 工作线程。
- 视频响应带有 `X-Accel-Buffering: no` 和禁用缓存头，避免 nginx 或浏览器缓存造成卡帧。
- MSE 连续 6 秒画面不前进会自动重建播放器，最多重试 2 次，之后回退 MJPEG。
- MJPEG 连续 10 秒没有新帧会自动重连，服务端也会尝试重新打开视频源。
- MPEG-TS 代理使用 1 秒接收超时，即使上游暂无数据也会定期检查浏览器是否仍然连接。
- MJPEG 接口只在 MSE 失败时建立，避免一个页面同时占用多路视频流。
- 浏览器重新打开页面后会通过 `/api/status` 恢复当前流程、错误和样品瓶进度。

## 错误响应格式

非 2xx 响应统一为：

```json
{
  "success": false,
  "error": {
    "code": "start_failed",
    "message": "Workflow start failed: ...",
    "hint": "查看系统错误提示和 ccme-dispenser 日志"
  }
}
```

## 错误恢复流程

1. 任意模块失败 → 编排器停止加热、搅拌和泵，进入 `error`，记录 `code/message/hint/module/stage/timestamp`。
2. 前端状态轮询发现 `error` → 显示错误横幅，并保留具体原因和处理建议。
3. 用户修复外部问题（网络、摄像头、串口、AI 配置等）。
4. 点击「清除错误」或直接重新「启动」：
   - `ClearError` 将系统恢复到 `idle`
   - `Start` 会重新连接摄像头、FMC，重新初始化各执行器
5. 服务进程本身不需要重启；模块级错误不会使 D-Bus 服务退出。

对于摄像头和 FMC 连接，编排器会自动重试 3 次（间隔 500ms）；视觉检测连续失败达到 5 次后才进入错误状态，单次网络抖动不会中止流程。
