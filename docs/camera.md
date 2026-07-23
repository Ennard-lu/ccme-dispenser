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

## 判断溶解完全的方式

请你定期从视频流中截取若干帧，截取其中的霍夫圆，通过机器学习的逻辑回归判断是否溶解完全。
