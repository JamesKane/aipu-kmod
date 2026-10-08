# CIX NPU DKMS
This repo provides dkms source code of CIX NPU, which can be easily installed on morden linux system.

## DKMS packaging
It's easy to build dkms deb package from this repo:
```
# Get source code from this repo
git clone https://github.com/cixtech/cix_opensource__npu_driver.git -b cix_mainline_dev

# Build deb package via gbp command
gbp buildpackage --git-ignore-branch --git-builder='debuild --no-lintian -uc -us'
```
Then you get deb package like `cix-npu-driver-dkms_6.1.0-1_all.deb` at the parent directory.

## Install DKMS package
You can install the built two deb packages in a debian-based os:
```
sudo apt install ./cix-npu-driver-dkms_6.1.0-1_all.deb
```
