#!/usr/bin/env bats

@test "can run asbench" {
  asbench --help
  [ "$?" -eq 0 ]
}

@test "asbench reports version" {
  asbench --version
  [ "$?" -eq 0 ]
}
