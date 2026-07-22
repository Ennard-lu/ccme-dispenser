# 摄像头

摄像头已经预先摆放在溶液的正上方

## 连接方式

通过GStreamer将视频流推送到开发板的7777端口

### GStreamer 推流命令（需要 mpegtsmux）

```bash
gst-launch-1.0 v4l2src device=/dev/video-camera0 io-mode=4 do-timestamp=false ! \
    video/x-raw,width=1200,height=1200,format=NV12,framerate=30/1 ! \
    mpph264enc gop=15 rc-mode=1 bps=4000000 max-pending=2 qp-init=20 ! \
    h264parse config-interval=-1 ! \
    mpegtsmux ! \
    tcpserversink host=127.0.0.1 port=7777 sync=false
```

**注意**: 必须添加 `mpegtsmux` 元素，否则浏览器 MSE 无法解析 H.264 裸流。

## 前端显示方式

- **MSE 模式**（低延迟，推荐）: 浏览器通过 `/api/hls/stream` 获取 MPEG-TS 流，使用 mpegts.js + MediaSource Extensions 硬解 H.264，延迟约 0.5-1 秒
- **MJPEG 模式**（兼容）: 浏览器通过 `/api/stream` 获取 MJPEG 流，OpenCV 解码后重编码为 JPEG，延迟约 2-3 秒

前端会优先尝试 MSE 模式，失败时自动回退到 MJPEG 模式。

## 判断溶解完全的方式

请你定期从视频流中截取若干帧，截取其中的霍夫圆，通过机器学习的逻辑回归判断是否溶解完全。
