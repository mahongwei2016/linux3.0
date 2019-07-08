passwd root
adduser mhw
systemctl enable serial-getty@ttySAC2.service
echo "inet:x:3003:root" >> /etc/group
echo "net_raw:x:3004:root" >> /etc/group
echo "deb http://mirrors.tuna.tsinghua.edu.cn/debian jessie main non-free contrib" >> /etc/apt/sources.list
echo "nameserver 8.8.8.8" >> /etc/resolv.conf
