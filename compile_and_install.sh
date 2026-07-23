cmake -B build build
cmake --build build --parallel 4
sudo rm -rf $HOME/.local/bin
cmake --install build --prefix "$HOME/.local" --component backend
sudo cmake --install build --component frontend
ndir=`pwd`
cd $HOME/.local/bin
sudo chown root ccme-pump ccme-stirrer gstreamer-stream.sh start-ip.sh
