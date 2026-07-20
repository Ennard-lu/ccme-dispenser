# 编译要求

## 项目结构
[+] 项目根目录下有CMakeLists.txt，由你编写。
[+] 第三方库放置于3rdparty中。
+ 头文件放置于include中，源文件放置于src中
[+] 在include与src的程序模块请另起一个目录

## 编译
请你采用cmake构建编译环境，编译文件于build目录中，编译采用aarch64跨平台编译，环境已经在wsl中准备完毕。
C++ 编译器指定为arm-linux-gnueabihf-g++. 务必在wsl中进行编译任务！
