# 运行时配置文件

样品瓶矩阵参数不再通过 CMake 编译期宏固定，而是在进程启动时从 JSON 文件读取。修改配置后只需重启相关服务，无需重新编译。

## 配置文件位置

查找顺序：

1. `CCME_CONFIG_PATH` 环境变量指定的文件
2. `/etc/ccme/ccme.json`
3. `/usr/etc/ccme/ccme.json`
4. `/usr/local/etc/ccme/ccme.json`
5. `$HOME/.local/etc/ccme/ccme.json`
6. `./config/ccme.json`（源码目录，便于开发）
7. `./ccme.json`

找到第一个可读文件后使用。若都找不到，会输出警告并使用内置默认值：

```text
rows=6, cols=6, spacing_x=-750, spacing_y=-750,
origin_x=-50, origin_y=-450, origin_z=0
```

推荐显式指定：

```bash
export CCME_CONFIG_PATH="/etc/ccme/ccme.json"
```

## 配置格式

```json
{
    "vial_matrix": {
        "rows": 6,
        "cols": 6,
        "spacing_x": -750,
        "spacing_y": -750,
        "origin_x": -50,
        "origin_y": -450,
        "origin_z": 0.0
    }
}
```

说明：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `rows` | int | 样品瓶行数，必须大于 0 |
| `cols` | int | 样品瓶列数，必须大于 0 |
| `spacing_x` | number | 相邻列在 X 方向的间距（mm） |
| `spacing_y` | number | 相邻行在 Y 方向的间距（mm） |
| `origin_x` | number | 第一个瓶子相对回零位置的 X 偏移（mm） |
| `origin_y` | number | 第一个瓶子相对回零位置的 Y 偏移（mm） |
| `origin_z` | number | Z 方向偏移（mm），当前版本预留 |

也支持扁平写法，即把上述字段直接放在根对象中，但推荐使用 `vial_matrix` 分组。

### 坐标计算

FMC 控制器使用：

```text
vial_x = home_x + origin_x + col * spacing_x
vial_y = home_y + origin_y + row * spacing_y
```

小瓶索引为从 0 开始的 `index`，其中：

```text
row = index / cols
col = (row 为偶数) ? index % cols : cols - 1 - index % cols
```

即偶数行从左到右，奇数行从右到左。

## 安装位置

`cmake --install` 会把默认配置安装到：

```text
${CMAKE_INSTALL_SYSCONFDIR}/ccme/ccme.json
```

使用 `--prefix "$HOME/.local"` 安装时，对应：

```text
$HOME/.local/etc/ccme/ccme.json
```

该路径已在运行时搜索列表中，因此安装后无需额外设置环境变量。

## 应用修改

配置在以下进程启动时读取：

- `ccme-fmc`：样品瓶行列数和间距
- `ccme-dispenser`：总样品瓶数量
- `ccme-web`：初始状态中的总样品瓶数量

修改后重启对应进程：

```bash
pkill -f ccme-fmc
pkill -f ccme-dispenser
pkill -f ccme-web
ccme-start
```

也可以只重启受影响的模块。最简单的做法是全部重启：

```bash
for svc in ccme-pump ccme-stirrer ccme-camera ccme-fmc ccme-web ccme-dispenser; do
    pkill -x "$svc" 2>/dev/null || true
done
ccme-start
```

启动日志会打印实际使用的文件：

```text
[CONFIG] Vial matrix source=/etc/ccme/ccme.json rows=6 cols=6 ...
[FMC] Initialized: ... config=/etc/ccme/ccme.json
[ORCH] Vial matrix: total=36 config=/etc/ccme/ccme.json
```

若看到：

```text
[CONFIG] no vial config file found; using built-in defaults
```

说明所有候选路径都没有找到配置，请检查文件位置或 `CCME_CONFIG_PATH`。

## 校验规则

- `rows` / `cols` 必须大于 0，否则回退到默认值并输出警告
- 字段类型必须正确（数字）
- JSON 解析失败、字段类型错误或文件不可读时，会输出 `[CONFIG] Warning:` 并使用默认值
- 未知字段会被忽略
