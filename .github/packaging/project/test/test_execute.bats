#!/usr/bin/env bats

@test "can run asbench" {
  asbench --help
  [ "$?" -eq 0 ]
}
@test "can run asrestore" {
  asrestore --help
  [ "$?" -eq 0 ]
}