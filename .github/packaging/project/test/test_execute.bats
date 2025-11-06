#!/usr/bin/env bats

@test "can run asbench" {
  asbench --help
  [ "$?" -eq 0 ]
}
