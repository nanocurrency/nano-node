# BLAKE2

This is the reference source code package of BLAKE2, which includes

* `ref/`: C implementations of BLAKE2b, BLAKE2bp, BLAKE2s, BLAKE2sp,
  aimed at portability and simplicity.

* `sse/`: C implementations of BLAKE2b, BLAKE2bp, BLAKE2s, BLAKE2sp,
  optimized for speed on CPUs supporting SSE2, SSSE3, SSE4.1, AVX, or
  XOP.

* `csharp/`: C# implementation of BLAKE2b.

* `b2sum/`: Command line utility to hash files, based on the `sse/`
  implementations.

* `bench/`: Benchmark tool to measure cycles-per-byte speeds and produce
  graphs copyright.

All code is triple-licensed under the [CC0](http://creativecommons.org/publicdomain/zero/1.0), the [OpenSSL Licence](https://www.openssl.org/source/license.html), or the [Apache Public License 2.0](http://www.apache.org/licenses/LICENSE-2.0),
at your choosing.

More: [https://blake2.net](https://blake2.net).

Contact: contact@blake2.net
