# sma-bluetooth

A C program to connect to sma inverters and export data.

## Build Dependencies

* libcurl
* libbluez
* libxml2
* libmysqlclient
* libfmt

### debian/ubuntu

To install dependencies on Debian/Ubuntu:

```
sudo apt install libcurl4-openssl-dev libbluetooth-dev \
                 libxml2-dev libmysqlclient-dev libfmt-dev
```

## build

Default CMake style build
```shell script
mkdir build
cd build
cmake ..
make
```

Issues - use github issue tracker.



