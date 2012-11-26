#!/usr/bin/env bash

# Copyright 2012 Red Hat Inc., Durham, North Carolina.
# All Rights Reserved.

. $srcdir/../../../test_common.sh

function test_fedora {
    local FEDORA_VERSION=$1
    local EXPECTED_NA=0

    local RPM=$(which rpm)
    if [ "x$?" != "x0" ]; then
        EXPECTED_NA=1
    else
        FEDORA_RELEASE=$(rpm -q fedora-release)
        if [ "x$?" != "x0" ]; then
            EXPECTED_NA=1
        else
            echo "$FEDORA_RELEASE" | grep -F "fedora-release-${FEDORA_VERSION}-"
            if [ "x$?" != "x0" ]; then
                EXPECTED_NA=1
            else
                EXPECTED_NA=0
            fi
        fi
    fi

    local INPUT=fedora${FEDORA_VERSION}-xccdf.xml

    local TMP_RESULTS=`mktemp`
    $OSCAP xccdf eval --results $TMP_RESULTS $srcdir/$INPUT
    if [ "x$?" != "x0" ]; then
        return 1
    fi

    local NOTAPPLICABLE_COUNT=$($XPATH $TMP_RESULTS 'count(//result[text()="notapplicable"])')
    rm -f $TMP_RESULTS

    if [ "$NOTAPPLICABLE_COUNT" == "$EXPECTED_NA" ]; then
        return 0
    fi

    return 1
}

function test_rhel {
    local RHEL_VERSION=$1
    local EXPECTED_NA=0

    local RPM=$(which rpm)
    if [ "x$?" != "x0" ]; then
        EXPECTED_NA=1
    else
        RHEL_RELEASE=$(rpm -q redhat-release)
        if [ "x$?" != "x0" ]; then
            EXPECTED_NA=1
        else
            echo "$RHEL_RELEASE" | grep -F ".el${RHEL_VERSION}."
            if [ "x$?" != "x0" ]; then
                EXPECTED_NA=1
            else
                EXPECTED_NA=0
            fi
        fi
    fi

    local INPUT=rhel${RHEL_VERSION}-xccdf.xml

    local TMP_RESULTS=`mktemp`
    $OSCAP xccdf eval --results $TMP_RESULTS $srcdir/$INPUT
    if [ "x$?" != "x0" ]; then
        return 1
    fi

    local NOTAPPLICABLE_COUNT=$($XPATH $TMP_RESULTS 'count(//result[text()="notapplicable"])')
    rm -f $TMP_RESULTS

    if [ "$NOTAPPLICABLE_COUNT" == "$EXPECTED_NA" ]; then
        return 0
    fi

    return 1
}

# Testing.

test_init "test_api_xccdf_default_cpe.log"

test_run "test_api_xccdf_default_cpe_fedora16" test_fedora 16
test_run "test_api_xccdf_default_cpe_fedora17" test_fedora 17
test_run "test_api_xccdf_default_cpe_rhel5" test_rhel 5
test_run "test_api_xccdf_default_cpe_rhel6" test_rhel 6

test_exit