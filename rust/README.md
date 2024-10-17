# Rust codebase

This folder contains all the Rust code of RsNano. 

The Rust code is structured according to A-frame architecture and is built with nullable infrastructure. This design and testing approach is extensively documented here:

[http://www.jamesshore.com/v2/projects/nullables/testing-without-mocks]

The following diagram shows how the crates are organized. The crates will be split up more when the codebase grows.

![crate diagram](http://www.plantuml.com/plantuml/proxy?cache=no&fmt=svg&src=https://raw.github.com/simpago/rsnano-node/develop/rust/doc/crates.puml)

* `main`: Contains the pure Rust node executable
* `ffi`: Contains all the glue code to connect the C++ and the Rust part (ffi = Foreign Function Interface)
* `node`: Contains the node implementation
* `rpc`: Contains the implemenation of the RPC server
* `ledger`: Contains the ledger implementation with. It is responsible for the consinstency of the data stores.
* `store_lmdb`: LMDB implementation of the data stores
* `messages`: Message types that nodes use for communication
* `network`: Manage outbound/inbound TCP channels to/from other nodes
* `core`: Contains the basic types like `BlockHash`, `Account`, `KeyPair`,...
* `nullables`: Nullable wrappers for infrastructure libraries

