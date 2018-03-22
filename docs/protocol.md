# Introduction

This document describes version 7 of the Raw rai protocol (as mentioned in https://github.com/nanocurrency/raiblocks/wiki/Network-usage).

**Note that this document is a work in progress and is not complete and/or fully correct yet!**

Code references used:
- `rai/node/common.cpp`
  - `rai::message`
  - `rai::message::read_header()`
  - `rai::message::block_type_set()`
- `rai/node/common.hpp`
  - `enum class message_type`
- `rai/lib/blocks.hpp`
  - `enum class block_type`

## Values

The following different types of values are used in the protocol:

  * Account
    - The 256-bit Ed25519 public key of an account
    - Encoded as a sequence of 32 bytes
    - Note that the well-known `xrb_...` notation for an address/representative
      is a different encoding of this value
  * Representative
    - Same as Account, but in a different role
  * Block hash
    - The 256-bit hash of a block  
    - The hash is computed from a set of "hashables": fields in a block that are hashed
      with the Blake2b hashing function
    - Unless noted otherwise the hashables for a block are the block-specific fields
      listed in the table with each block description. They are to be hashed in the
      order they are listed.
    - The block hash is used (amongst other things) to make references among blocks
    - Encoded as a sequence of 32 bytes
  * Balance / Amount
    - A 128-bit unsigned integer
    - This number has unit "raw", where 1 XRB = 1 Mxrb = 10<sup>30</sup> raw.
      See [here](https://github.com/nanocurrency/raiblocks/wiki/Distribution,-Mining-and-Units)
      for more information.
    - Encoded as a sequence of 16 bytes, *big-endian* order
  * Signature
      - A 512-bit Ed25519 signature
      - Encoded as a sequence of 64 bytes
  * Work
      - A 64-bit unsigned integer Proof-of-Work value
      - Encoding depends on the type of block: *big-endian* for a State block,
        *little-endian* for other types of blocks

In general, integer values of up to 8 bytes are encoded as little-endian.

The Ed25519 implementation in Nano does not use the standard
SHA-512 hashing algorithm, but Blake2b. See [here](https://github.com/nanocurrency/raiblocks/wiki/Design-features)
for more information.

## UDP packets

There are currently 4 types of messages sent in UDP packets:

  | Type       | Description                                  |
  | ---------- | -------------------------------------------- |
  | Keepalive  | Status notification of peers being alive     |
  | Publish    | Publication of a (new?) block on the network |
  | ConfirmReq | Request to vote on a block?                  |
  | ConfirmAck | Vote by a representative on a block         |

There are other types of messages denoted by `message_type`, but these are
only used in the "bootstrapping" procedure when a node needs to synchronize
itself with all the blocks in the distributed ledger (in a bulk fashion).
Bootstrapping is done over TCP connections, not with UDP packets.

All UDP messages have a fixed-size header (see `rai::message::read_header()` in `rai/node/common.cpp`):

  | Field         | Size (bytes)  | Description |
  | ------------- | ------------- | ----------- |
  | Magic         | 2             | Magic string to indicate this is a Nano packet |
  | VersionMax    | 1             | Maximum protocol version the node that sent the packet can understand |
  | VersionUsing  | 1             | Protocol version the packet uses |
  | VersionMin    | 1             | Minimum protocol version the node that sent the packet can understand |
  | Type          | 1             | Message type |
  | Extensions    | 2             | 16-bit (little-endian) value that indicates possible extensions, optionally contains a block type  |

The 2-byte magic string has a value that depends on the type of Nano network on which
the message is sent:

  | Value | Network | Description |
  | ----- | ------- | ----------- |
  | `RA`  | Testing | Internal testing network used for development |
  | `RB`  | Beta    | Beta network used for development |
  | `RC`  | Live    | Live network, where real transaction in Nano are performed |

Each of these 3 networks has their own genesis account, block and amount. Blocks
intended for one network will not be accepted on another network, as they will
fail to validate. XXX is this true?

The VersionMax, VersionUsing and VersionMin values are related to the version
of the network protocol being used. The VersionUsing value indicates the protocol
version a packet is adhering to.

The message Type can have one of the following values (see `message_type` in `rai/node/common.hpp`):

  | Type       | Value |
  | ---------- | ----- |
  | Keepalive  | 2     |
  | Publish    | 3     |
  | ConfirmReq | 4     |
  | ConfirmAck | 5     |

The Extensions field currently only contains the 8-bit block type, in the highest
8 bits of the field. But this is only the case if the message contains a Block (i.e. Publish, ConfirmReq and ConfirmAck messages).
See `rai::message::block_type_set()` in `rai/node/common.cpp`.

## Keepalive message

Reference: class `rai::keepalive` in `rai/node/common.cpp`

A Keepalive message contains a list of IP addresses of peers, in the form of (IPv6 address, port) pairs.
Each IP address is stored as 16 bytes, each port as 2 bytes:

  | Field        | Size (bytes) | Description     |
  | ------------ | ------------ | --------------- |
  | IPv6 address | 16           | Big-endian?     |
  | Port         | 2            | Little-endian?! |

The number of pairs in the message can vary, from 1 up to 8 peers.

## Publish message

Reference: class `rai::publish` in `rai/node/common.cpp`

A Publish message simply contains a published Block, which was either created or
forwarded by another node.

See below for the way a block in encoded.

## ConfirmReq message

Reference: class `rai::confirm_req` in `rai/node/common.cpp`

XXX A ConfirmReq message contains a Block for which a vote has been requested?

  | Field | Size (bytes) | Description        |
  | ----- | ------------ | ------------------ |
  | Block | Varies       | A serialized Block |

## ConfirmAck message

Reference: class `rai::confirm_ack` in `rai/node/common.cpp`

A ConfirmAck message contains a Vote by a representative on a Block.

  | Field | Size (bytes) | Description       |
  | ----- | ------------ | ----------------- |    
  | Vote  | (varies)     | A serialized Vote |


## Vote

Reference: class `rai::vote` in `rai/common.cpp`

  | Field     | Size (bytes) | Description                          |
  | --------- | ------------ | ------------------------------------ |
  | Account   | 32           | Public key of the voting Account     |
  | Signature | 64           | Signature                            |
  | Sequence  | 8            | 64-bit sequence number (big-endian?) |
  | Block     | Varies       | The Block being voted on             |

The Signature of a Vote signs the following list of hashables: `hash(Block)`, `Sequence`

XXX what is the exact function of the sequence number?

# Blocks

Reference: `rai/lib/blocks.cpp`

Blocks are encoded into a byte stream, with each field directly following
the previous value. All blocks contain a Signature and a Work value, which follow
the block-specific fields:

  | Field     | Size (bytes) | Description                                |
  | --------- | ------------ | ------------------------------------------ |
  | *Block-specific fields* | |                                           |
  | Signature | 64           | Ed25519 signature of the block's hashables |
  | Work      | 8            | Unsigned 64-bit integer (little endian?)   |

The "hashables" of a block are the set of field values that are fed into
the Blake2b hashing algorithm in order to get a hash value for the block.
This hash value is then signed with the public key of the account that generated the block
and stored in the Signature field.

XXX Work value

The different block types are denoted by a Type value (see `enum class block_type` in `rai/lib/blocks.hpp`):

  | Block type | Value |
  | ---------- | ----- |
  | Send       | 2     |
  | Receive    | 3     |
  | Open       | 4     |
  | Change     | 5     |
  | State      | 6     |

## Transfer of funds

For each block in an account's chain a balance value can be determined, either from
a value stored in the block itself or by using differences between successive
blocks in the chain. Differences in balance values between successive blocks
can be used to compute the amount of funds transferred by blocks to other
accounts.

Only Send and State blocks contain an explicit Balance value. For other block
types balances and amounts needs to be deducted recursively:

* The balance of an Open block is equal to the amount being sent by the corresponding
  Send block. An exception to this is the Open block for the Genesis account, which always
  receives the maximum supply of 2<sup>128</sup>-1 raw.
* The balance of a Receive block is the balance of its predecessor in the
  account chain plus the amount sent by the corresponding Send block.
* The amount of funds being sent by a Send block is defined implicitly as
  the difference between the balance at the previous block in the account chain
  and the balance at the Send block. This amount must be > 0.
* A Change block has no influence on an account's balance, so its balance is
  the same as the balance of its predecessor.

All State blocks contain a Balance value, hence determining amounts sent or
received from one State block to the next is straightforward.

## Send block

Reference: `rai::send_block` in `rai/lib/blocks.cpp`

A Send block transfers funds from the account the block belongs to to a
different account.

A Send block contains the following block-specific fields:

  | Field         | Size (bytes) | Description |
  | ------------- | ----- | ----------------------------------------------- |
  | Previous      | 32    | Hash of the previous block in the account chain |
  | Destination   | 32    | The destination account being sent to |
  | Balance       | 32    | The sending account's new balance |

Note that the Balance value in the block is the balance of the sending
account *after the amount sent in the block has been deducted*.

XXX is it valid to send to yourself?

## Receive block

Reference: `rai::receive_block` in `rai/lib/blocks.cpp`

A Receive block contains the following block-specific fields:

  | Field         | Size (bytes) | Description |
  | ------------- | ----- | ----------------------------------------------- |
  | Previous      | 32    | Hash of the previous block in the account chain |
  | Source        | 32    | Hash of the corresponding send block from which the funds are sent |

The block referenced by Source must be a Send block.

## Open block

Reference: `rai::open_block` in `rai/lib/blocks.cpp`

An Open block creates a new account and is always the first block in the account chain.
It references a Send block from another account, from which the initial funds are sent.

An Open block contains the following block-specific fields:

  | Field          | Size (bytes) | Description |
  | -------------  | ----- | ----------------------------------------------- |
  | Source         | 32    | Hash of the corresponding send block from which the initial funds are sent |
  | Representative | 32    | The account representative for the newly opened account |
  | Account        | 32    | The account being opened |

The block referenced by Source must be a Send block.

An exception to the previous remark applies to the Open block of the Genesis
account: its Source value is the public key of the Genesis account. The Open
blocks for the 3 different networks are defined by the `..._genesis_data` JSON
blocks in `rai/common.cpp`.

## Change block

Reference: `rai::change_block` in `rai/lib/blocks.cpp`

A Change block sets a new representative for an account.

A Change block contains the following block-specific fields:

  | Field          | Size (bytes) | Description |
  | -------------  | ----- | ----------------------------------------------- |
  | Previous       | 32    | Hash of the previous block in the account chain |
  | Representative | 32    | The new account representative |

## State block (a.k.a. UTX block, Universal block)

Reference: `rai::state_block` in `rai/lib/blocks.cpp`

State blocks are a relatively new addition to the protocol and are set to
replace all other block types for newly created transactions.

State blocks differ slightly in certain respects to the other block types:

* The Work value is stored as big-endian

State blocks also contain more fields than other block types:

  | Field          | Size (bytes) | Description |
  | -------------  | ----- | ----------------------------------------------- |
  | Account        | 32    | The account to which the block belongs |
  | Previous       | 32    | Hash of the previous block in the account chain |
  | Representative | 32    | The account representative for the account |
  | Balance        | 32    | The balance of the account at the block's position in the chain |
  | Link           | 32    | If receiving: source *block hash*, if sending: destination *account* |

The difference between two sequential State blocks in an account chain
determines the type of state transition from one block to the next:

  - If the Representative value has changed then a new representative is set
    for the account
  - If the current Balance > previous Balance then the block represents a receive of funds.
    The Link field holds the hash of the corresponding source block in this case.
  - If the current Balance < previous Balance then the block represents a sending of funds.
    The Link field holds the destination account in this case.

Performing both a transfer of funds and updating the representative in the same block is allowed.

The opening a new account is signaled by setting Previous to all-zero. The Link field contains the sending block in this case.

Sending a zero amount is not valid, but receiving a zero amount from the Burn
account (when Link is all-zero) is.
