#!/bin/bash
#
# GStreamer camera streaming script for CCME Dispenser.
#
# Outputs:
#   - MPEG-TS on port 7777 → ccme-web /api/hls/stream → browser MSE
#   - Raw H.264 on port 7778 → ccme-camera dissolution detection
#
# Usage:
#   ./gstreamer-stream.sh                # default device
#   ./gstreamer-stream.sh /dev/video0    # custom device
#
# Environment variables:
#   CCME_DEVICE         - V4L2 device path       (default: /dev/video-camera0)
#   CCME_WIDTH          - frame width             (default: 1200)
#   CCME_HEIGHT         - frame height            (default: 1200)
#   CCME_FPS            - frame rate              (default: 30)
#   CCME_BITRATE        - encoder bitrate bps     (default: 4000000)
#   CCME_GOP            - keyframe interval       (default: 15)
#   CCME_MPEGTS_PORT    - MPEG-TS TCP port       (default: 7777)
#   CCME_H264_PORT      - raw H.264 TCP port     (default: 7778)

set -euo pipefail

DEVICE="${1:-${CCME_DEVICE:-/dev/video-camera0}}"
WIDTH="${CCME_WIDTH:-1200}"
HEIGHT="${CCME_HEIGHT:-1200}"
FPS="${CCME_FPS:-10}"
BITRATE="${CCME_BITRATE:-4000000}"
GOP="${CCME_GOP:-15}"
MPEGTS_PORT="${CCME_MPEGTS_PORT:-7777}"
H264_PORT="${CCME_H264_PORT:-7778}"

echo "GStreamer stream: ${DEVICE} ${WIDTH}x${HEIGHT}@${FPS}fps"
echo "  MPEG-TS → :${MPEGTS_PORT}  (browser)"
echo "  H.264   → :${H264_PORT}    (camera module)"

exec gst-launch-1.0 \
    v4l2src device="${DEVICE}" io-mode=4 do-timestamp=false ! \
    "video/x-raw,width=${WIDTH},height=${HEIGHT},format=NV12,framerate=${FPS}/1" ! \
    mpph264enc gop="${GOP}" rc-mode=1 bps="${BITRATE}" max-pending=1 qp-init=20 ! \
    tee name=t \
    t. ! queue leaky=downstream max-size-buffers=1 max-size-time=0 ! \
       h264parse config-interval=-1 ! mpegtsmux latency=0 ! \
       tcpserversink host=127.0.0.1 port="${MPEGTS_PORT}" sync=false async=false \
    t. ! queue leaky=downstream max-size-buffers=1 max-size-time=0 ! \
       h264parse config-interval=-1 ! \
       tcpserversink host=127.0.0.1 port="${H264_PORT}" sync=false async=false
