# mcastroute
Simple program that can do multicast destination change 
syntax simple mcastroute add vlan9@239.125.0.1:1234 vlan656@239.122.0.1:1122
which means take multicast from 239.125.0.1:1234 on vlan9 (sends igmp join on that interface) 
and send output on group 239.122.0.1:1122 into interface vlan656<br>
It uses Netgraph Framework and working only under FreeBSD.
