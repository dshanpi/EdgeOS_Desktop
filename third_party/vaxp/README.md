# VAXP protocol headers

This directory contains the VAXP 1.0 wire-protocol headers required by EdgeOS
Desktop. They are vendored from
[`dshanpi/vaxp-host-sdk`](https://github.com/dshanpi/vaxp-host-sdk), currently
from commit `7ddc841`, so an EdgeOS checkout does not depend on untracked files
outside this repository.

The headers are distributed under the repository's MIT license. Keep all three
files in `include/` synchronized when updating the protocol ABI.
