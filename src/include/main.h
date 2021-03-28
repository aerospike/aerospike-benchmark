#pragma once

#include <check.h>

/*
 * sets the fork status of the global srunner, which can be called in
 * forked child processes in test cases so they always exit(1) instead of
 * jumping to the failure case on failed assertions
 */
void set_fork_status(enum fork_status status);

