# mcastroute
Simple program that can do multicast destination change <br>
### syntax Example
    mcastroute add vlan9@239.125.0.1:1234 vlan656@239.122.0.1:1122 
      adds "route"  which means take multicast from 239.125.0.1:1234 on vlan9 (sends igmp join on that interface) 
      and send output on group 239.122.0.1:1122 into interface vlan656<br>
    mcastroute del 239.125.0.1  
      del "route" argument should be source ip address 
    mcastroute show 
      print current translation table 

   
It uses Netgraph Framework and working only under FreeBSD.
