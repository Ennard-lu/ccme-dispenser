# 编译要求

## 项目结构
[+] 项目根目录下有CMakeLists.txt，由你编写。
[+] 第三方库放置于3rdparty中。
+ 头文件放置于include中，源文件放置于src中
[+] 在include与src的程序模块请另起一个目录

## 编译
请你采用cmake构建编译环境，编译文件于build目录中，编译采用aarch64跨平台编译，环境已经在wsl中准备完毕。
C++ 编译器指定为aarch64-linux-gnu-g++（GCC 16.1.0）。务必在wsl中进行编译任务！

### 交叉编译步骤
```bash
# 1. 准备 stub libsystemd（运行时使用Orange Pi上的真实libsystemd）
# 脚本: scripts/stub-libsystemd.c → /tmp/arm64-sysroot/lib/libsystemd.so.0

# 2. 配置
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake -DCMAKE_BUILD_TYPE=Release

# 3. 编译
cmake --build . -j$(nproc)
```

### 编译产物
6个可执行文件（均为ELF 64-bit ARM aarch64）：
- `build/src/pump/ccme-pump` - 蠕动泵控制
- `build/src/stirrer/ccme-stirrer` - 搅拌器控制
- `build/src/camera/ccme-camera` - 摄像头溶解检测
- `build/src/fmc/ccme-fmc` - FMC4030三轴运动控制
- `build/src/web/ccme-web` - HTTP服务器
- `build/src/orchestrator/ccme-dispenser` - 工作流编排器

## 配置
对于可配置的变量，比如使用的端口号，设备名字符串等等采用cmake的option或者configure统一管理。
