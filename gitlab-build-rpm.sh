#!/bin/bash

set -x -e -o pipefail

. /etc/os-release
CODENAME=${ID}_${VERSION_ID}
TAGNAME=`echo ${CI_COMMIT_REF_NAME} | cut -d '-' -f 1`

# set up an RPM build environment
yum install -y rpm-build rpmdevtools make gcc epel-release
rpmdev-setuptree
yum-builddep -y rpm/${CI_PROJECT_NAME}.spec

# create a tarball to build the RPM from
./bootstrap.sh
./configure
make dist

# copy it into position
cp ${CI_PROJECT_NAME}-*.tar.gz ~/rpmbuild/SOURCES/${TAGNAME}.tar.gz
cp rpm/*.patch ~/rpmbuild/SOURCES/ || true
cp rpm/${CI_PROJECT_NAME}.spec ~/rpmbuild/SPECS/

# build the RPM
cd ~/rpmbuild/
rpmbuild -bb --define "debug_package %{nil}" SPECS/${CI_PROJECT_NAME}.spec

# move the built RPM into position
mkdir -p ${CI_PROJECT_DIR}/built-packages/${CODENAME}/ || true
mv ~/rpmbuild/RPMS/*/*.rpm ${CI_PROJECT_DIR}/built-packages/${CODENAME}/
