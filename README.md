# fakehttp
* high performance http server
* just echo time for now

##Technology
* multithread
* epoll
* TCP and fixed HTTP header
* ...

## Test Environment
* Two CentOS7.2 on the same host by VMware Workstation 12 Player
* 1 core i5-6500 CPU @ 3.20GHz and 1GB memory for each test vm machine

## Operation
### vm-one
* first:`make`
* second:`$./fakehttp`
* `Usage:  fakehttp -l <local address> -p <port> -d <delay (ms)>`

### vm-two
* `ab -n 80000 -c 200 -k http://IP-address:12321/`

## Result
* most to 50k QPS
