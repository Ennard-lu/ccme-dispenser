# 摄像头

摄像头已经预先摆放在溶液的正上方

## 连接方式

后台已经通过GStreamer将H.264视频流推送到开发板的7777端口

接收端使用GStreamer pipeline连接：
`tcpclientsrc host=127.0.0.1 port=7777 ! h264parse ! avdec_h264 ! videoconvert ! appsink`

## 判断溶解完全的方式

请你定期从视频流中截取若干帧，截取其中的霍夫圆，通过机器学习的逻辑回归判断是否溶解完全。
