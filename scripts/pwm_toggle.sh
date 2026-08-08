#!/bin/bash

# =============================================================================
# PWM 周期性开关测试脚本
# 通过 Linux sysfs 接口 (pwmchip) 控制 PWM 输出
#
# 用法: $0 <chip> <period_ns> <interval_s> [channel]
#   <chip>         pwmchip 编号, 对应 /sys/class/pwm/pwmchip<chip>
#   <period_ns>    周期 (纳秒), duty_cycle 自动设为周期的一半
#   <interval_s>   PWM 使能持续时间 (秒, 支持小数)
#   [channel]      PWM 通道号 (默认 0)
#
# 示例:
#   $0 0 1000000 5
#   $0 2 2000000 3 1
#
# 流程:
#   1. 检查通道是否已 export, 未 export 则先 export
#   2. 写入 period 与 duty_cycle (period 的一半), 必须在 enable 之前
#   3. enable PWM 持续 interval 秒
#   4. disable PWM, 等待终端输入 y 后回到步骤 3 循环
#   5. 退出时自动 disable 并 unexport (仅当本脚本 export 时才 unexport)
# =============================================================================

usage() {
    cat <<EOF
用法: $0 <chip> <period_ns> <interval_s> [channel]

  <chip>         pwmchip 编号, 对应 /sys/class/pwm/pwmchip<chip>
  <period_ns>    周期 (纳秒), duty_cycle 自动设为周期的一半
  <interval_s>   PWM 使能持续时间 (秒, 支持小数)
  [channel]      PWM 通道号 (默认 0)

示例:
  $0 0 1000000 5
  $0 2 2000000 3 1
EOF
    exit 1
}

if [ $# -lt 3 ]; then
    usage
fi

CHIP="$1"
PERIOD="$2"
INTERVAL="$3"
CHANNEL="${4:-0}"

# 校验参数
if [[ ! "$CHIP" =~ ^[0-9]+$ ]] || [[ ! "$CHANNEL" =~ ^[0-9]+$ ]]; then
    echo "错误: chip 与 channel 必须是非负整数" >&2
    exit 1
fi
if [[ ! "$PERIOD" =~ ^[0-9]+$ ]] || [ "$PERIOD" -le 0 ]; then
    echo "错误: period 必须为正整数 (纳秒)" >&2
    exit 1
fi
if [[ ! "$INTERVAL" =~ ^[0-9]+([.][0-9]+)?$ ]] || ! awk "BEGIN{exit !($INTERVAL > 0)}"; then
    echo "错误: interval 必须为大于 0 的数值 (秒)" >&2
    exit 1
fi

DUTY=$((PERIOD / 2))

CHIP_DIR="/sys/class/pwm/pwmchip${CHIP}"
PWM_DIR="${CHIP_DIR}/pwm${CHANNEL}"

if [ ! -d "$CHIP_DIR" ]; then
    echo "错误: $CHIP_DIR 不存在" >&2
    exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "错误: 需要 root 权限访问 sysfs" >&2
    exit 1
fi

# 若通道尚未 export 则导出, 并记录以便退出时清理
EXPORTED=0
if [ ! -d "$PWM_DIR" ]; then
    echo "通道 $CHANNEL 未导出, 正在导出..."
    if ! echo "$CHANNEL" > "$CHIP_DIR/export" 2>/dev/null; then
        echo "错误: 导出通道 $CHANNEL 失败 (可能已被占用或不被支持)" >&2
        exit 1
    fi
    EXPORTED=1
    for _ in $(seq 1 50); do
        [ -d "$PWM_DIR" ] && break
        sleep 0.1
    done
    if [ ! -d "$PWM_DIR" ]; then
        echo "错误: $PWM_DIR 未创建" >&2
        exit 1
    fi
fi

cleanup() {
    echo "0" > "$PWM_DIR/enable" 2>/dev/null
    if [ "$EXPORTED" -eq 1 ]; then
        echo "卸载通道 $CHANNEL..."
        echo "$CHANNEL" > "$CHIP_DIR/unexport" 2>/dev/null
    fi
}
trap cleanup EXIT

if [ "$PERIOD" -le `cat ${PWM_DIR}/duty_cycle` ]; then
    if ! echo "$DUTY" > "$PWM_DIR/duty_cycle" 2>/dev/null; then
        echo "错误: 写入 duty_cycle 失败" >&2
        exit 1
    fi
    if ! echo "$PERIOD" > "$PWM_DIR/period" 2>/dev/null; then
        echo "错误: 写入 period 失败" >&2
        exit 1
    fi
else
# 写入 period 与 duty_cycle (必须在 enable 之前完成)
    if ! echo "$PERIOD" > "$PWM_DIR/period" 2>/dev/null; then
        echo "错误: 写入 period 失败" >&2
        exit 1
    fi
    if ! echo "$DUTY" > "$PWM_DIR/duty_cycle" 2>/dev/null; then
        echo "错误: 写入 duty_cycle 失败" >&2
        exit 1
    fi
fi
echo "使用 PWM: $PWM_DIR (period=${PERIOD}ns, duty_cycle=${DUTY}ns)"

while true; do
    echo "使能 PWM ${INTERVAL}s..."
    if ! echo "1" > "$PWM_DIR/enable" 2>/dev/null; then
        echo "错误: 使能 PWM 失败" >&2
        exit 1
    fi
    sleep "$INTERVAL"
    if ! echo "0" > "$PWM_DIR/enable" 2>/dev/null; then
        echo "错误: 禁用 PWM 失败" >&2
        exit 1
    fi
    echo "PWM 已禁用。输入 y 继续循环, 其他任意键退出:"
    read -r ans
    [ "$ans" = "y" ] || break
done

exit 0