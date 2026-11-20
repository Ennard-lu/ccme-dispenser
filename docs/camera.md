# 摄像头

摄像头已经预先摆放在溶液的正上方

## 连接方式

GStreamer 通过 `tee` 输出两路 H.264 流：MPEG-TS 供浏览器播放，raw H.264 供溶解检测。

### 推流脚本

```bash
./scripts/gstreamer-stream.sh
```

或手动指定设备：

```bash
./scripts/gstreamer-stream.sh /dev/video0
```

也可通过环境变量自定义参数：

```bash
CCME_BITRATE=6000000 CCME_MPEGTS_PORT=7777 CCME_H264_PORT=7778 ./scripts/gstreamer-stream.sh
```

### 手动推流命令

```bash
gst-launch-1.0 v4l2src device=/dev/video-camera0 io-mode=4 do-timestamp=false \
    min-buffers=1 min-leftover=0 ! \
    video/x-raw,width=1200,height=1200,format=NV12,framerate=30/1 ! \
    mpph264enc gop=15 rc-mode=1 bps=4000000 max-pending=1 qp-init=20 ! \
    tee name=t \
    t. ! queue leaky=downstream max-size-buffers=1 max-size-time=0 ! \
       h264parse config-interval=-1 ! mpegtsmux latency=0 ! \
       tcpserversink host=127.0.0.1 port=7777 sync=false async=false \
    t. ! queue leaky=downstream max-size-buffers=1 max-size-time=0 ! \
       h264parse config-interval=-1 ! \
       tcpserversink host=127.0.0.1 port=7778 sync=false async=false
```

- **Port 7777**: MPEG-TS → `ccme-web` HTTP 代理 → 浏览器 mpegts.js MSE（延迟 ~0.5-1s）
- **Port 7778**: Raw H.264 → `ccme-camera` 溶解检测

同一个 H.264 源，只编码一次，两路共享。

## 前端显示方式

- **MSE 模式**（低延迟，推荐）: 浏览器通过 `/api/hls/stream` 获取 MPEG-TS 流，使用 mpegts.js + MediaSource Extensions 硬解 H.264，延迟约 0.5-1 秒
- **MJPEG 模式**（兼容）: 浏览器通过 `/api/stream` 获取 MJPEG 流，OpenCV 解码后重编码为 JPEG，延迟约 2-3 秒

前端会优先尝试 MSE 模式，失败时自动回退到 MJPEG 模式。

## 判断溶解完全的方式（云端 AI）

`ccme-camera` 不再在本机执行霍夫圆检测和逻辑回归。每次检测时，它会从视频流中截取若干帧，缩放后编码为 JPEG，通过 **OpenAI 兼容的 Chat Completions 视觉接口**发送给云端模型，由模型返回结构化的溶解判断。

### 配置选项（CMake）

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `CCME_CAMERA_STREAM_URL` | `tcp://127.0.0.1:7778` | GStreamer 原始 H.264 流地址（用于截帧） |
| `CCME_CAMERA_CHECK_INTERVAL_MS` | `1000` | 编排器轮询 `CheckDissolution` 的间隔（毫秒） |
| `CCME_AI_API_URL` | `https://api.openai.com/v1/chat/completions` | OpenAI 兼容接口地址，也可只写 `https://api.openai.com` 或 `https://api.openai.com/v1` |
| `CCME_AI_API_KEY` | 空 | API 密钥；建议改用环境变量，避免写入构建缓存 |
| `CCME_AI_MODEL` | `gpt-4o-mini` | 支持图像输入的模型名 |
| `CCME_AI_TIMEOUT_SEC` | `30` | HTTP 请求超时（秒） |
| `CCME_AI_MAX_TOKENS` | `300` | 模型最大输出 token 数 |
| `CCME_AI_FRAME_COUNT` | `3` | 每次检测上传的帧数 |
| `CCME_AI_JPEG_QUALITY` | `85` | 上传帧的 JPEG 质量 |
| `CCME_AI_IMAGE_MAX_SIDE` | `1024` | 上传前最长边限制，`0` 表示不缩放 |
| `CCME_AI_PROMPT` | 空 | 追加到内置提示词后的额外要求 |
| `CCME_AI_DISABLE_THINKING` | `OFF` | 发送 `thinking.type=disabled`，推荐用于 Doubao-Seed-2.0-mini 以降低延迟 |
| `CCME_DISSOLUTION_TIMEOUT_SEC` | `3600` | 工作流等待溶解完成的最长时间（秒），`0` 表示不超时 |

### 环境变量（运行时覆盖）

启动 `ccme-camera` 前可设置以下变量，优先级高于 CMake 配置：

```bash
export CCME_AI_API_URL="https://api.openai.com/v1/chat/completions"
export CCME_AI_API_KEY="sk-..."
export CCME_AI_MODEL="gpt-4o-mini"
export CCME_AI_TIMEOUT_SEC="30"
export CCME_AI_PROMPT="请特别关注烧杯底部是否有未溶解的晶体。"
export CCME_AI_DISABLE_THINKING="1"

# 仅调试/演示时使用：跳过 AI 溶解检测
# export CCME_SKIP_AI_CHECK="y"
```

`CCME_AI_API_KEY` 不会写入日志；只会在启动日志中显示为 `set` 或 `not set`。

### OpenAI 兼容请求

请求体使用标准的 Chat Completions 格式：

```json
{
  "model": "gpt-4o-mini",
  "messages": [
    { "role": "system", "content": "<内置溶解判断提示词>" },
    {
      "role": "user",
      "content": [
        { "type": "text", "text": "以下是按时间顺序拍摄的 3 帧图像..." },
        { "type": "image_url", "image_url": { "url": "data:image/jpeg;base64,..." } }
      ]
    }
  ],
  "temperature": 0,
  "max_tokens": 300
}
```

模型需要返回如下 JSON（允许外层有 Markdown 代码块，程序会自动提取第一个 `{` 到最后一个 `}`）：

```json
{ "dissolved": true, "confidence": 0.95, "reasoning": "溶液澄清，无可见固体残留。" }
```

`dissolved` 判定为 `true` 时，`ccme-camera` 的 D-Bus `CheckDissolution` 返回 `true`，编排器随即停止加热和搅拌并开始分装。

### 本地 OpenAI 兼容服务

也可以指向局域网中的 vLLM、Ollama、llama.cpp server 等：

```bash
export CCME_AI_API_URL="http://192.168.0.10:8000/v1/chat/completions"
export CCME_AI_MODEL="Qwen2.5-VL-7B-Instruct"
export CCME_AI_API_KEY=""
```

注意：`https://` 需要编译时找到 OpenSSL（`cpp-httplib` 的 CMake 选项 `HTTPLIB_USE_OPENSSL_IF_AVAILABLE` 默认开启）。若目标环境没有 OpenSSL，请使用 `http://` 的本地服务，或安装 OpenSSL 后重新编译。

### 接入 Doubao-Seed-2.0-mini（火山方舟）

火山方舟提供 OpenAI 兼容的 Chat Completions 接口，可直接使用：

```bash
export CCME_AI_API_URL="https://ark.cn-beijing.volces.com/api/v3/chat/completions"
export CCME_AI_API_KEY="<ARK_API_KEY>"
export CCME_AI_MODEL="doubao-seed-2-0-mini-260428"
export CCME_AI_DISABLE_THINKING="1"
```

也可以把地址写成 base URL，程序会自动补全 `/chat/completions`：

```bash
export CCME_AI_API_URL="https://ark.cn-beijing.volces.com/api/v3"
```

要点：

- **模型 ID**：`doubao-seed-2-0-mini-260428`。如果已在方舟创建推理接入点，也可以使用 `ep-...` 形式的接入点 ID。
- **区域**：默认使用 `cn-beijing`；其他区域请替换域名中的区域部分。
- **图片传入**：使用 `data:image/jpeg;base64,...` 的 `image_url` 方式，方舟 Chat API 原生支持；多帧会放在同一条 user message 的多个 `image_url` 中。
- **深度思考**：`CCME_AI_DISABLE_THINKING=1` 会额外发送 `"thinking": {"type": "disabled"}`。溶解判断是低复杂度分类任务，关闭思考可以显著降低延迟和 token 消耗。若关闭后识别准确率不足，可去掉该变量，让模型使用深度思考。
- **返回解析**：方舟可能同时返回 `reasoning_content` 和 `content`；程序只解析 `content` 中的 JSON，忽略思考过程。
- **单图兼容**：如果所使用的模型或代理只接受单张图片，可设置 `CCME_AI_FRAME_COUNT=1`。
- **tokens**：开启深度思考时会消耗额外 token；若保留思考能力，建议把 `CCME_AI_MAX_TOKENS` 提高到 `1000` 以上。
- **大小限制**：方舟单张 Base64 图片需小于 10 MB，请求体不超过 64 MB；默认 1024px、3 帧 JPEG 远低于限制。

使用 CMake 配置时对应写法：

```bash
cmake .. \
  -DCCME_AI_API_URL="https://ark.cn-beijing.volces.com/api/v3/chat/completions" \
  -DCCME_AI_MODEL="doubao-seed-2-0-mini-260428" \
  -DCCME_AI_DISABLE_THINKING=ON
```

API Key 仍建议通过 `CCME_AI_API_KEY` 环境变量提供。

### 跳过 AI 检测（仅调试）

设置：

```bash
export CCME_SKIP_AI_CHECK="y"
```

后重启 `ccme-dispenser`。可接受的开启值包括：

```text
y / Y / yes / 1 / true / on
```

启用后编排器会：

- 跳过云端 AI 配置检查
- 跳过摄像头连接检查
- 跳过整个溶解检测循环
- 在启动加热和搅拌后直接认为溶解完成，进入分装阶段

启动日志会输出醒目警告：

```text
[ORCH] WARNING: CCME_SKIP_AI_CHECK is enabled; AI dissolution detection will be skipped ...
```

前端「视觉检测」一栏会显示「已跳过」。

> **警告**：该模式不会验证固体是否真正溶解，仅用于硬件调试、联调或演示。正式实验请务必关闭。

### 失败处理

- 视频流断开：`ccme-camera` 会尝试重新连接；仍失败则 D-Bus 返回错误。
- API 未配置、网络错误、HTTP 非 2xx、响应无法解析：返回对应的 D-Bus 错误。
- 编排器连续 5 次检测失败或超过等待时间后进入 `error` 状态，并停止加热、搅拌和泵。
- 错误会携带 `code`、`message`、`hint`、`module`、`stage` 和时间戳，通过 `/api/status` 提供给前端；修复外部问题后可点击「清除错误」或直接重新启动流程，无需重启后端服务。
