#!/bin/bash

# =============================================================================
# IKA RCT basic 控制脚本
# 基于 NAMUR 命令集，通过串口与设备通信
# 用法: ./ika_ctrl.sh [选项] <命令> [参数]
# 示例: ./ika_ctrl.sh -d /dev/ttyUSB0 speed 500
#       ./ika_ctrl.sh stir on
#       ./ika_ctrl.sh temp 80
#       ./ika_ctrl.sh get temp
# =============================================================================

# 默认串口设备（可通过 -d 或环境变量 IKA_DEVICE 覆盖）
DEFAULT_DEVICE="/dev/ttyUSB0"
DEVICE="${IKA_DEVICE:-$DEFAULT_DEVICE}"

# 命令终止符: 空格 + CR + 空格 + LF (根据手册)
TERMINATOR=$' \r \n'

# 串口通信超时（秒）
TIMEOUT=2

# 帮助信息
usage() {
    cat <<EOF
用法: $0 [选项] <命令> [参数]

选项:
  -d <设备>    串口设备文件 (默认: $DEFAULT_DEVICE)
  -h           显示此帮助

命令:
  stir on                   开启搅拌
  stir off                  关闭搅拌
  speed <rpm>               设定搅拌速度 (0-1500)
  get speed                 读取当前设定转速
  get actual_speed          读取实际转速 (IN_PV_4)

  heat on                   开启加热
  heat off                  关闭加热
  temp <°C>                 设定目标温度 (0-310)
  get temp                  读取目标温度
  get actual_temp           读取加热板实际温度 (IN_PV_2)
  get external_temp         读取外部 PT1000 传感器温度 (IN_PV_1)

  mode <A|b|d>              设置操作模式 (A, b, d)
  get name                  读取设备名称
  reset                     复位设备至正常操作模式

  status                    显示当前所有主要状态

示例:
  $0 -d /dev/ttyUSB0 stir on
  $0 speed 500
  $0 get actual_temp

环境变量:
  IKA_DEVICE   覆盖默认串口设备
EOF
    exit 0
}

# 初始化串口配置
init_serial() {
    if [[ ! -c "$DEVICE" ]]; then
        echo "错误: 串口设备 $DEVICE 不存在或不可读" >&2
        exit 1
    fi
    # 配置: 9600 7E1 (7数据位, 偶校验, 1停止位), 原始模式, 无回显
    stty -F "$DEVICE" 9600 cs7 parenb -parodd -cstopb cread clocal raw -echo \
        2>/dev/null || {
        echo "错误: 无法配置串口 $DEVICE (权限或设备问题)" >&2
        exit 1
    }
}

# 发送命令并读取响应（去掉终止符）
send_cmd() {
    local cmd="$1"
    local resp=""
    # 打开串口文件描述符
    exec 3<> "$DEVICE"
    # 发送命令 + 终止符
    printf "%s%s" "$cmd" "$TERMINATOR" >&3
    # 读取响应（使用 read 配合超时，终止符包含 \n 所以 read 可以截取一行）
    if read -t "$TIMEOUT" -r resp <&3; then
        # 去除可能残留的 CR
        resp="${resp%$'\r'}"
        echo "$resp"
    else
        echo "警告: 读取响应超时 (可能命令无响应)" >&2
        return 1
    fi
    exec 3>&-
    return 0
}

# 发送只读命令（无响应时返回空）
send_readonly() {
    send_cmd "$1" 2>/dev/null || true
}

# 发送控制命令并忽略响应
send_control() {
    send_cmd "$1" >/dev/null 2>&1 || true
}

# ---------- 命令实现 ----------
cmd_stir_on() {
    send_control "START_4"
    echo "搅拌已开启"
}

cmd_stir_off() {
    send_control "STOP_4"
    echo "搅拌已关闭"
}

cmd_speed_set() {
    local rpm="$1"
    if [[ ! "$rpm" =~ ^[0-9]+$ ]] || [ "$rpm" -lt 0 ] || [ "$rpm" -gt 1500 ]; then
        echo "错误: 转速必须在 0-1500 之间" >&2
        exit 1
    fi
    send_control "OUT_SP_4 $rpm"
    echo "设定转速为 $rpm rpm"
}

cmd_speed_get() {
    local resp
    resp=$(send_cmd "IN_SP_4")
    if [[ "$resp" =~ ^[0-9]+$ ]]; then
        echo "设定转速: $resp rpm"
    else
        echo "读取失败: $resp" >&2
        return 1
    fi
}

cmd_actual_speed() {
    local resp
    resp=$(send_cmd "IN_PV_4")
    if [[ "$resp" =~ ^[0-9]+$ ]]; then
        echo "当前转速: $resp rpm"
    else
        echo "读取失败: $resp" >&2
        return 1
    fi
}

cmd_heat_on() {
    send_control "START_1"
    echo "加热已开启"
}

cmd_heat_off() {
    send_control "STOP_1"
    echo "加热已关闭"
}

cmd_temp_set() {
    local temp="$1"
    if [[ ! "$temp" =~ ^[0-9]+$ ]] || [ "$temp" -lt 0 ] || [ "$temp" -gt 310 ]; then
        echo "错误: 温度必须在 0-310 °C 之间" >&2
        exit 1
    fi
    send_control "OUT_SP_1 $temp"
    echo "设定目标温度为 $temp °C"
}

cmd_temp_get() {
    local resp
    resp=$(send_cmd "IN_SP_1")
    if [[ "$resp" =~ ^[0-9]+$ ]]; then
        echo "目标温度: $resp °C"
    else
        echo "读取失败: $resp" >&2
        return 1
    fi
}

cmd_actual_temp() {
    local resp
    resp=$(send_cmd "IN_PV_2")
    if [[ "$resp" =~ ^-?[0-9]+$ ]]; then
        echo "加热板实际温度: $resp °C"
    else
        echo "读取失败: $resp" >&2
        return 1
    fi
}

cmd_external_temp() {
    local resp
    resp=$(send_cmd "IN_PV_1")
    if [[ "$resp" =~ ^-?[0-9]+$ ]]; then
        echo "外部传感器温度: $resp °C"
    else
        echo "读取失败: (未连接传感器或错误)" >&2
        return 1
    fi
}

cmd_mode_set() {
    local mode="$1"
    if [[ "$mode" != "A" && "$mode" != "b" && "$mode" != "d" ]]; then
        echo "错误: 模式必须是 A, b 或 d" >&2
        exit 1
    fi
    send_control "SET_MODE_$mode"
    echo "操作模式设置为 $mode"
}

cmd_get_name() {
    local resp
    resp=$(send_cmd "IN_NAME")
    echo "设备名称: $resp"
}

cmd_reset() {
    send_control "RESET"
    echo "设备已复位"
}

cmd_status() {
    echo "========== IKA RCT basic 状态 =========="
    cmd_speed_get
    cmd_actual_speed
    echo "---"
    cmd_temp_get
    cmd_actual_temp
    cmd_external_temp
    echo "---"
    echo "注: 搅拌/加热实际开关状态需通过其他方式确认"
}

# ---------- 主参数解析 ----------
while getopts "d:h" opt; do
    case "$opt" in
        d) DEVICE="$OPTARG" ;;
        h) usage ;;
        *) usage ;;
    esac
done
shift $((OPTIND-1))

if [ $# -lt 1 ]; then
    usage
fi

# 初始化串口
init_serial

# 分发命令
CMD="$1"
shift

case "$CMD" in
    stir)
        case "$1" in
            on)   cmd_stir_on ;;
            off)  cmd_stir_off ;;
            *)    echo "用法: stir {on|off}" >&2; exit 1 ;;
        esac
        ;;
    speed)
        if [ "$1" == "get" ]; then
            cmd_speed_get
        elif [[ "$1" =~ ^[0-9]+$ ]]; then
            cmd_speed_set "$1"
        else
            echo "用法: speed <rpm> 或 speed get" >&2
            exit 1
        fi
        ;;
    get)
        case "$1" in
            speed)         cmd_speed_get ;;
            actual_speed)  cmd_actual_speed ;;
            temp)          cmd_temp_get ;;
            actual_temp)   cmd_actual_temp ;;
            external_temp) cmd_external_temp ;;
            name)          cmd_get_name ;;
            *) echo "未知获取项: $1" >&2; exit 1 ;;
        esac
        ;;
    heat)
        case "$1" in
            on)   cmd_heat_on ;;
            off)  cmd_heat_off ;;
            *)    echo "用法: heat {on|off}" >&2; exit 1 ;;
        esac
        ;;
    temp)
        if [[ "$1" =~ ^[0-9]+$ ]]; then
            cmd_temp_set "$1"
        else
            echo "用法: temp <°C>" >&2
            exit 1
        fi
        ;;
    mode)
        if [ -n "$1" ]; then
            cmd_mode_set "$1"
        else
            echo "用法: mode {A|b|d}" >&2
            exit 1
        fi
        ;;
    reset)
        cmd_reset
        ;;
    status)
        cmd_status
        ;;
    *)
        echo "未知命令: $CMD" >&2
        usage
        ;;
esac

exit 0
