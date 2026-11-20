# 三轴控制器

具体C SDK文档参见fmc/FMC4030-SDK.pdf, 小瓶的位置处于一个方阵中，当有小瓶需要装配时，请该三轴系统移动到小瓶
上方，使得溶液流入小瓶中。

样品瓶矩阵参数（行列数、间距、原点）在运行时从 `config/ccme.json` 读取，修改后重启
`ccme-fmc` / `ccme-dispenser` / `ccme-web` 即可生效，无需重新编译。详见
[config.md](config.md)。
